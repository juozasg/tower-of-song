//! MIDI keyboard input: opens every input port, reconnects on hot-plug, filters noise.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use midir::{Ignore, MidiInput, MidiInputConnection};
use rtrb::Producer;

use crate::audio::Cmd;
use crate::voices::PIANO;

#[derive(Clone, Copy, Debug)]
pub struct NoteEvent {
    pub note: u8,
    pub velocity: f32,
    pub on: bool,
}

pub struct Device {
    pub name: String,
    pub open: bool,
    pub error: String,
}

struct Input {
    name: String,
    connection: Option<MidiInputConnection<()>>,
    error: String,
}

const RESCAN_EVERY: Duration = Duration::from_millis(1500);

pub struct Midi {
    scanner: Option<MidiInput>,
    inputs: Vec<Input>,
    last_scan: Option<Instant>,
    events: Arc<Mutex<Vec<NoteEvent>>>,
    held: Arc<Mutex<[bool; 128]>>,
    live: Arc<Mutex<Producer<Cmd>>>,
    pub error: String,
}

impl Midi {
    pub fn new(live: Producer<Cmd>) -> Self {
        let (scanner, error) = match MidiInput::new("Tower of Song scanner") {
            Ok(s) => (Some(s), String::new()),
            Err(e) => (None, format!("MIDI unavailable: {e}")),
        };
        Midi {
            scanner,
            inputs: Vec::new(),
            last_scan: None,
            events: Arc::new(Mutex::new(Vec::new())),
            held: Arc::new(Mutex::new([false; 128])),
            live: Arc::new(Mutex::new(live)),
            error,
        }
    }

    /// Opens new ports, closes vanished ones and retries busy ones. Cheap to call every frame.
    pub fn poll(&mut self) {
        if self.last_scan.is_some_and(|t| t.elapsed() < RESCAN_EVERY) {
            return;
        }
        self.last_scan = Some(Instant::now());
        let Some(scanner) = &self.scanner else { return };

        let mut names: Vec<(String, midir::MidiInputPort)> = Vec::new();
        for port in scanner.ports() {
            let Ok(mut name) = scanner.port_name(&port) else { continue };
            // Two identical keyboards would share a name; keep them apart.
            let base = name.clone();
            let mut n = 2;
            while names.iter().any(|(existing, _)| *existing == name) {
                name = format!("{base} ({n})");
                n += 1;
            }
            names.push((name, port));
        }

        let before = self.inputs.len();
        self.inputs.retain(|input| names.iter().any(|(name, _)| *name == input.name));
        if self.inputs.len() != before {
            // An unplugged keyboard can't send its note-offs.
            if let Ok(mut live) = self.live.lock() {
                let _ = live.push(Cmd::LiveAllOff);
            }
            if let Ok(mut held) = self.held.lock() {
                *held = [false; 128];
            }
        }

        for (name, port) in names {
            if !self.inputs.iter().any(|i| i.name == name) {
                self.inputs.push(Input { name: name.clone(), connection: None, error: String::new() });
            }
            let idx = self.inputs.iter().position(|i| i.name == name).expect("just inserted");
            if self.inputs[idx].connection.is_some() {
                continue;
            }
            let result = self.connect(&port, &name);
            let input = &mut self.inputs[idx];
            match result {
                Ok(c) => {
                    input.connection = Some(c);
                    input.error.clear();
                }
                Err(e) => input.error = e,
            }
        }
    }

    fn connect(&self, port: &midir::MidiInputPort, name: &str) -> Result<MidiInputConnection<()>, String> {
        let mut client = MidiInput::new("Tower of Song").map_err(|e| e.to_string())?;
        client.ignore(Ignore::All); // sysex, clock, active sensing
        let events = self.events.clone();
        let held = self.held.clone();
        let live = self.live.clone();
        client
            .connect(
                port,
                name,
                move |_timestamp, bytes, _| handle_message(bytes, &events, &held, &live),
                (),
            )
            .map_err(|_| "Unavailable - another app may be using it".to_string())
    }

    pub fn devices(&self) -> Vec<Device> {
        self.inputs
            .iter()
            .map(|i| Device { name: i.name.clone(), open: i.connection.is_some(), error: i.error.clone() })
            .collect()
    }

    pub fn drain(&self) -> Vec<NoteEvent> {
        self.events.lock().map(|mut e| std::mem::take(&mut *e)).unwrap_or_default()
    }

    pub fn held(&self) -> Vec<u8> {
        self.held
            .lock()
            .map(|h| (0..128u8).filter(|&n| h[n as usize]).collect())
            .unwrap_or_default()
    }
}

fn handle_message(
    bytes: &[u8],
    events: &Mutex<Vec<NoteEvent>>,
    held: &Mutex<[bool; 128]>,
    live: &Mutex<Producer<Cmd>>,
) {
    if bytes.len() < 3 {
        return;
    }
    let (status, d1, d2) = (bytes[0] & 0xF0, bytes[1] & 0x7F, bytes[2] & 0x7F);
    let (event, cmd) = match status {
        0x90 if d2 > 0 => {
            let vel = d2 as f32 / 127.0;
            (Some(NoteEvent { note: d1, velocity: vel, on: true }), Cmd::LiveOn { note: d1, vel, inst: PIANO })
        }
        // Note-off, or note-on with velocity 0 (running-status keyboards send this).
        0x80 | 0x90 => (Some(NoteEvent { note: d1, velocity: 0.0, on: false }), Cmd::LiveOff { note: d1, inst: PIANO }),
        0xB0 if d1 == 64 => (None, Cmd::LivePedal(d2 >= 64)),
        0xB0 if d1 == 120 || d1 == 123 => (None, Cmd::LiveAllOff),
        _ => return,
    };

    // Sound first: this is the latency the player feels.
    if let Ok(mut live) = live.lock() {
        let _ = live.push(cmd);
    }
    if let Some(e) = event {
        if let Ok(mut h) = held.lock() {
            h[e.note as usize] = e.on;
        }
        if let Ok(mut q) = events.lock() {
            if q.len() < 4096 {
                q.push(e);
            }
        }
    }
}
