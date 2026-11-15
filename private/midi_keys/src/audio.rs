//! Audio output (cpal), the phrase scheduler and the mixer.
//!
//! The game thread sends commands through a lock-free queue; MIDI threads send live notes
//! through a second queue. The audio callback never locks or allocates.

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::mpsc;
use std::thread::JoinHandle;

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use rtrb::{Consumer, Producer, RingBuffer};

use crate::fx::{Echo, Reverb};
use crate::voices::Synth;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Fx {
    Dry,
    Room,
    Hall,
}

impl Fx {
    pub fn parse(name: &str) -> Fx {
        match name {
            "dry" => Fx::Dry,
            "hall" => Fx::Hall,
            _ => Fx::Room,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub enum Cmd {
    Begin { id: u32, fx: Fx },
    Note { start: f32, dur: f32, note: u8, vel: f32, inst: u8 },
    End,
    Stop,
    LiveOn { note: u8, vel: f32, inst: u8 },
    LiveOff { note: u8, inst: u8 },
    LivePedal(bool),
    LiveAllOff,
}

/// Values the game thread reads back.
pub struct Shared {
    pub playing_id: AtomicU32,
    position_bits: AtomicU64,
    volume_bits: AtomicU32,
}

impl Shared {
    pub fn new() -> Self {
        Shared { playing_id: AtomicU32::new(0), position_bits: AtomicU64::new(0), volume_bits: AtomicU32::new(0.8f32.to_bits()) }
    }
    pub fn position(&self) -> f64 {
        f64::from_bits(self.position_bits.load(Ordering::Relaxed))
    }
    pub fn volume(&self) -> f32 {
        f32::from_bits(self.volume_bits.load(Ordering::Relaxed))
    }
    pub fn set_volume(&self, v: f32) {
        self.volume_bits.store(v.clamp(0.0, 2.0).to_bits(), Ordering::Relaxed);
    }
}

#[derive(Clone, Copy)]
struct Event {
    sample: u64,
    on: bool,
    note: u8,
    vel: f32,
    inst: u8,
}

const BLOCK: usize = 64; // events land on 64-sample boundaries (~1.3 ms)
const MAX_EVENTS: usize = 16384;

/// Everything that runs inside the audio callback.
pub struct Renderer {
    sr: f32,
    phrase: Synth,
    live: Synth,
    phrase_reverb: Reverb,
    live_reverb: Reverb,
    echo: Echo,
    cmds: Consumer<Cmd>,
    live_cmds: Consumer<Cmd>,
    events: Vec<Event>,
    next_event: usize,
    clock: u64,
    phrase_start: u64,
    phrase_end: u64,
    active_id: u32,
    building: bool,
    pl: [f32; BLOCK],
    pr: [f32; BLOCK],
    ll: [f32; BLOCK],
    lr: [f32; BLOCK],
    block_pos: usize,
    shared: Arc<Shared>,
}

impl Renderer {
    pub fn new(sr: f32, cmds: Consumer<Cmd>, live_cmds: Consumer<Cmd>, shared: Arc<Shared>) -> Self {
        let mut live_reverb = Reverb::new(sr);
        live_reverb.set(0.35, 0.5, 0.06);
        let mut r = Renderer {
            sr,
            phrase: Synth::new(sr),
            live: Synth::new(sr),
            phrase_reverb: Reverb::new(sr),
            live_reverb,
            echo: Echo::new(sr),
            cmds,
            live_cmds,
            events: Vec::with_capacity(MAX_EVENTS),
            next_event: 0,
            clock: 0,
            phrase_start: 0,
            phrase_end: 0,
            active_id: 0,
            building: false,
            pl: [0.0; BLOCK],
            pr: [0.0; BLOCK],
            ll: [0.0; BLOCK],
            lr: [0.0; BLOCK],
            block_pos: BLOCK,
            shared,
        };
        r.apply_fx(Fx::Room);
        r
    }

    fn apply_fx(&mut self, fx: Fx) {
        match fx {
            Fx::Dry => {
                self.phrase_reverb.set(0.3, 0.5, 0.0);
                self.echo.set(0.3, self.sr, 0.0, 0.0);
            }
            Fx::Room => {
                self.phrase_reverb.set(0.42, 0.5, 0.09);
                self.echo.set(0.3, self.sr, 0.0, 0.0);
            }
            Fx::Hall => {
                self.phrase_reverb.set(0.88, 0.3, 0.2);
                self.echo.set(0.36, self.sr, 0.38, 0.3);
            }
        }
    }

    fn handle(&mut self, c: Cmd) {
        match c {
            Cmd::Begin { id, fx } => {
                self.phrase.all_notes_off();
                self.events.clear();
                self.next_event = 0;
                self.phrase_start = self.clock + (0.03 * self.sr) as u64;
                self.phrase_end = self.phrase_start;
                self.active_id = id;
                self.building = true;
                self.apply_fx(fx);
            }
            Cmd::Note { start, dur, note, vel, inst } => {
                if self.events.len() + 2 > self.events.capacity() {
                    return;
                }
                let on = self.phrase_start + (start.max(0.0) * self.sr) as u64;
                let off = on + ((dur * self.sr) as u64).max(1);
                self.events.push(Event { sample: on, on: true, note, vel, inst });
                self.events.push(Event { sample: off, on: false, note, vel: 0.0, inst });
                self.phrase_end = self.phrase_end.max(off);
            }
            Cmd::End => {
                // Note-offs before note-ons at the same time, so repeated notes re-strike.
                self.events.sort_unstable_by_key(|e| (e.sample, e.on));
                self.building = false;
                self.shared.playing_id.store(self.active_id, Ordering::Relaxed);
            }
            Cmd::Stop => {
                self.events.clear();
                self.next_event = 0;
                self.building = false;
                self.active_id = 0;
                self.shared.playing_id.store(0, Ordering::Relaxed);
                self.phrase.all_notes_off();
            }
            Cmd::LiveOn { note, vel, inst } => self.live.note_on(inst, note, vel),
            Cmd::LiveOff { note, inst } => self.live.note_off(inst, note),
            Cmd::LivePedal(down) => self.live.set_pedal(down),
            Cmd::LiveAllOff => self.live.all_notes_off(),
        }
    }

    fn render_block(&mut self) {
        while let Ok(c) = self.cmds.pop() {
            self.handle(c);
        }
        while let Ok(c) = self.live_cmds.pop() {
            self.handle(c);
        }

        let block_end = self.clock + BLOCK as u64;
        if !self.building {
            while self.next_event < self.events.len() && self.events[self.next_event].sample < block_end {
                let e = self.events[self.next_event];
                self.next_event += 1;
                if e.on {
                    self.phrase.note_on(e.inst, e.note, e.vel);
                } else {
                    self.phrase.note_off(e.inst, e.note);
                }
            }
        }

        self.pl.fill(0.0);
        self.pr.fill(0.0);
        self.ll.fill(0.0);
        self.lr.fill(0.0);
        self.phrase.render(&mut self.pl, &mut self.pr);
        self.live.render(&mut self.ll, &mut self.lr);
        self.echo.process(&mut self.pl, &mut self.pr);
        self.phrase_reverb.process(&mut self.pl, &mut self.pr);
        self.live_reverb.process(&mut self.ll, &mut self.lr);

        let gain = self.shared.volume();
        for i in 0..BLOCK {
            self.pl[i] = ((self.pl[i] + self.ll[i]) * gain).tanh();
            self.pr[i] = ((self.pr[i] + self.lr[i]) * gain).tanh();
        }

        self.clock = block_end;
        if self.active_id != 0 && !self.building {
            let pos = (self.clock as f64 - self.phrase_start as f64) / self.sr as f64;
            self.shared.position_bits.store(pos.to_bits(), Ordering::Relaxed);
            if self.clock > self.phrase_end + (0.25 * self.sr) as u64 {
                self.active_id = 0;
                self.shared.playing_id.store(0, Ordering::Relaxed);
            }
        }
    }

    /// Fills an interleaved buffer of any length, rendering 64-sample blocks as needed.
    pub fn fill(&mut self, out: &mut [f32], channels: usize) {
        for frame in out.chunks_mut(channels.max(1)) {
            if self.block_pos >= BLOCK {
                self.render_block();
                self.block_pos = 0;
            }
            let (l, r) = (self.pl[self.block_pos], self.pr[self.block_pos]);
            for (ch, sample) in frame.iter_mut().enumerate() {
                *sample = if ch % 2 == 0 { l } else { r };
            }
            self.block_pos += 1;
        }
    }
}

/// The game-thread side of the audio engine.
pub struct Audio {
    cmds: Producer<Cmd>,
    pub shared: Arc<Shared>,
    pub info: String,
    next_id: u32,
    stop: Option<mpsc::Sender<()>>,
    thread: Option<JoinHandle<()>>,
}

pub fn queues() -> ((Producer<Cmd>, Consumer<Cmd>), (Producer<Cmd>, Consumer<Cmd>)) {
    (RingBuffer::new(MAX_EVENTS + 64), RingBuffer::new(1024))
}

impl Audio {
    /// Opens the default output device. `live_rx` receives notes from MIDI threads.
    pub fn start(cmds: Producer<Cmd>, cmds_rx: Consumer<Cmd>, live_rx: Consumer<Cmd>) -> Result<Audio, String> {
        let shared = Arc::new(Shared::new());
        let (info_tx, info_rx) = mpsc::channel::<Result<String, String>>();
        let (stop_tx, stop_rx) = mpsc::channel::<()>();
        let thread_shared = shared.clone();

        // cpal streams are not Send on every platform, so one thread owns the stream for its lifetime.
        let thread = std::thread::Builder::new()
            .name("midi_keys audio".into())
            .spawn(move || {
                let stream = match open_stream(cmds_rx, live_rx, thread_shared) {
                    Ok((stream, info)) => {
                        let _ = info_tx.send(Ok(info));
                        stream
                    }
                    Err(e) => {
                        let _ = info_tx.send(Err(e));
                        return;
                    }
                };
                let _ = stop_rx.recv();
                drop(stream);
            })
            .map_err(|e| e.to_string())?;

        match info_rx.recv() {
            Ok(Ok(info)) => Ok(Audio { cmds, shared, info, next_id: 1, stop: Some(stop_tx), thread: Some(thread) }),
            Ok(Err(e)) => Err(e),
            Err(_) => Err("audio thread exited".into()),
        }
    }

    pub fn send(&mut self, c: Cmd) -> bool {
        self.cmds.push(c).is_ok()
    }

    pub fn play_phrase(&mut self, notes: &[(f32, f32, u8, f32, u8)], fx: Fx) -> u32 {
        if self.cmds.slots() < notes.len() + 2 {
            return 0;
        }
        let id = self.next_id;
        self.next_id += 1;
        self.send(Cmd::Begin { id, fx });
        for &(start, dur, note, vel, inst) in notes {
            self.send(Cmd::Note { start, dur, note, vel, inst });
        }
        self.send(Cmd::End);
        // Report "playing" right away so the game doesn't see a gap before the audio thread starts.
        self.shared.playing_id.store(id, Ordering::Relaxed);
        self.shared.position_bits.store(0f64.to_bits(), Ordering::Relaxed);
        id
    }
}

impl Drop for Audio {
    fn drop(&mut self) {
        if let Some(stop) = self.stop.take() {
            let _ = stop.send(());
        }
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}

fn open_stream(cmds: Consumer<Cmd>, live: Consumer<Cmd>, shared: Arc<Shared>) -> Result<(cpal::Stream, String), String> {
    let host = cpal::default_host();
    let device = host.default_output_device().ok_or("no audio output device")?;
    let default = device.default_output_config().map_err(|e| e.to_string())?;
    if default.sample_format() != cpal::SampleFormat::F32 {
        return Err(format!("unsupported sample format {:?}", default.sample_format()));
    }
    let mut config: cpal::StreamConfig = default.config();
    // Ask for a small buffer: playing along needs low latency.
    if let cpal::SupportedBufferSize::Range { min, max } = default.buffer_size() {
        config.buffer_size = cpal::BufferSize::Fixed(256u32.clamp(*min, *max));
    }

    let sr = config.sample_rate as f32;
    let channels = config.channels as usize;
    let mut renderer = Renderer::new(sr, cmds, live, shared);
    let stream = device
        .build_output_stream(
            config.clone(),
            move |data: &mut [f32], _| renderer.fill(data, channels),
            |err| eprintln!("midi_keys audio error: {err}"),
            None,
        )
        .map_err(|e| e.to_string())?;
    stream.play().map_err(|e| e.to_string())?;

    let name = device.description().map(|d| d.to_string()).unwrap_or_else(|_| "audio output".into());
    let buffer = match config.buffer_size {
        cpal::BufferSize::Fixed(n) => format!("{n} samples"),
        cpal::BufferSize::Default => "default buffer".into(),
    };
    Ok((stream, format!("{name}, {} Hz, {buffer}", sr as u32)))
}
