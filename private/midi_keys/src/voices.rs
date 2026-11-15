//! Synth voices: an additive piano, a warm pad and a bell, with simple voice allocation.

use std::f32::consts::{PI, TAU};

pub const PIANO: u8 = 1;
pub const PAD: u8 = 2;
pub const BELL: u8 = 3;

fn hz(note: u8) -> f64 {
    440.0 * 2f64.powf((note as f64 - 69.0) / 12.0)
}

fn decay_per_sample(t60: f32, sr: f32) -> f32 {
    (-6.9078 / (t60 * sr)).exp()
}

fn pan_gains(note: u8) -> (f32, f32) {
    let pan = ((note as f32 - 60.0) / 48.0).clamp(-0.6, 0.6);
    let angle = (pan + 1.0) * 0.25 * PI;
    (angle.cos(), angle.sin())
}

/// Small deterministic noise source (the audio thread must not lock or allocate).
#[derive(Clone, Copy)]
struct Rng(u32);

impl Rng {
    fn next(&mut self) -> f32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        (self.0 as f32 / u32::MAX as f32) * 2.0 - 1.0
    }
}

const MAX_PARTIALS: usize = 14;

#[derive(Clone, Copy, Default)]
struct Partial {
    re: [f32; 2],
    im: [f32; 2],
    cs: [f32; 2],
    sn: [f32; 2],
    amp: f32,
    fast: f32,
    slow: f32,
    fast_mul: f32,
    slow_mul: f32,
}

/// Additive piano: stretched partials, two detuned strings each, two-stage decay,
/// hammer-position comb and a short noise burst.
#[derive(Clone, Copy)]
struct Piano {
    partials: [Partial; MAX_PARTIALS],
    count: usize,
    gain: (f32, f32),
    attack: f32,
    attack_step: f32,
    release: f32,
    release_mul: f32,
    releasing: bool,
    noise_env: f32,
    noise_mul: f32,
    noise_lp: f32,
    noise_coef: f32,
    noise_amp: f32,
    rng: Rng,
    since_normalise: usize,
}

impl Piano {
    fn new(note: u8, velocity: f32, sr: f32, seed: u32) -> Self {
        let f0 = hz(note);
        let vel = velocity.clamp(0.05, 1.0);
        let mut rng = Rng(seed | 1);
        let inharm = (0.00025 * 2f64.powf((note as f64 - 60.0) / 18.0)).clamp(0.00005, 0.003);
        let base_t60 = (12.0 * 2f32.powf(-(note as f32 - 36.0) / 14.0)).clamp(0.7, 14.0);
        let rolloff = 1.9 - vel;
        let detune = 0.35 + 0.25 * (rng.next() * 0.5 + 0.5);

        let mut partials = [Partial::default(); MAX_PARTIALS];
        let mut count = 0;
        let mut amp_sum = 0.0;
        for n in 1..=MAX_PARTIALS {
            let fnn = n as f64 * f0 * (1.0 + inharm * (n * n) as f64).sqrt();
            if fnn > sr as f64 * 0.45 {
                break;
            }
            let p = &mut partials[count];
            count += 1;
            let comb = 0.15 + 0.85 * (PI * n as f32 * 0.12).sin().abs();
            p.amp = comb / (n as f32).powf(rolloff);
            amp_sum += p.amp;
            for s in 0..2 {
                let cents = if s == 0 { -detune } else { detune } as f64;
                let w = std::f64::consts::TAU * fnn * 2f64.powf(cents / 1200.0) / sr as f64;
                p.cs[s] = w.cos() as f32;
                p.sn[s] = w.sin() as f32;
                let phase = (rng.next() * 0.5 + 0.5) * 0.3;
                p.re[s] = phase.cos();
                p.im[s] = phase.sin();
            }
            let t60 = base_t60 / (1.0 + 0.35 * (n as f32 - 1.0) + 0.0004 * fnn as f32);
            p.fast = 0.55;
            p.slow = 0.45;
            p.fast_mul = decay_per_sample(t60 * 0.18, sr);
            p.slow_mul = decay_per_sample(t60, sr);
        }
        let loudness = 0.3 * vel.powf(1.4) / amp_sum.max(0.001);
        for p in partials.iter_mut().take(count) {
            p.amp *= loudness;
        }

        Piano {
            partials,
            count,
            gain: pan_gains(note),
            attack: 0.0,
            attack_step: 1.0 / (0.0015 * sr),
            release: 1.0,
            release_mul: decay_per_sample(if note < 48 { 0.45 } else { 0.28 }, sr),
            releasing: false,
            noise_env: 1.0,
            noise_mul: decay_per_sample(0.03, sr),
            noise_lp: 0.0,
            noise_coef: (f0 as f32 * 6.0 / sr).clamp(0.02, 0.6),
            noise_amp: 0.05 * vel * vel,
            rng,
            since_normalise: 0,
        }
    }

    /// Returns false once the note has died away.
    fn render(&mut self, l: &mut [f32], r: &mut [f32]) -> bool {
        for i in 0..l.len() {
            let mut sum = 0.0;
            for p in self.partials.iter_mut().take(self.count) {
                let mut s = 0.0;
                for k in 0..2 {
                    let re = p.re[k] * p.cs[k] - p.im[k] * p.sn[k];
                    let im = p.re[k] * p.sn[k] + p.im[k] * p.cs[k];
                    p.re[k] = re;
                    p.im[k] = im;
                    s += im;
                }
                p.fast *= p.fast_mul;
                p.slow *= p.slow_mul;
                sum += s * p.amp * (p.fast + p.slow);
            }
            self.noise_lp += self.noise_coef * (self.rng.next() - self.noise_lp);
            sum += self.noise_lp * self.noise_amp * self.noise_env;
            self.noise_env *= self.noise_mul;
            if self.attack < 1.0 {
                self.attack = (self.attack + self.attack_step).min(1.0);
            }
            if self.releasing {
                self.release *= self.release_mul;
            }
            let v = sum * self.attack * self.release;
            l[i] += v * self.gain.0;
            r[i] += v * self.gain.1;
        }

        // Keep the phasors on the unit circle despite rounding.
        self.since_normalise += l.len();
        if self.since_normalise > 4096 {
            self.since_normalise = 0;
            for p in self.partials.iter_mut().take(self.count) {
                for k in 0..2 {
                    let mag = (p.re[k] * p.re[k] + p.im[k] * p.im[k]).sqrt();
                    p.re[k] /= mag;
                    p.im[k] /= mag;
                }
            }
        }

        let level = if self.count > 0 { (self.partials[0].fast + self.partials[0].slow) * self.release } else { 0.0 };
        level > 0.0005
    }
}

/// Attack / decay / sustain / release envelope.
#[derive(Clone, Copy)]
struct Adsr {
    level: f32,
    stage: u8, // 0 attack, 1 decay/sustain, 2 release, 3 done
    attack_step: f32,
    decay_mul: f32,
    sustain: f32,
    release_mul: f32,
}

impl Adsr {
    fn new(attack: f32, decay: f32, sustain: f32, release: f32, sr: f32) -> Self {
        Adsr {
            level: 0.0,
            stage: 0,
            attack_step: 1.0 / (attack * sr).max(1.0),
            decay_mul: decay_per_sample(decay, sr),
            sustain,
            release_mul: decay_per_sample(release, sr),
        }
    }

    fn next(&mut self) -> f32 {
        match self.stage {
            0 => {
                self.level += self.attack_step;
                if self.level >= 1.0 {
                    self.level = 1.0;
                    self.stage = 1;
                }
            }
            1 => self.level = self.sustain + (self.level - self.sustain) * self.decay_mul,
            2 => {
                self.level *= self.release_mul;
                if self.level < 0.0005 {
                    self.stage = 3;
                    self.level = 0.0;
                }
            }
            _ => {}
        }
        self.level
    }

    fn release(&mut self) {
        if self.stage < 2 {
            self.stage = 2;
        }
    }

    fn done(&self) -> bool {
        self.stage == 3
    }
}

/// Two detuned PolyBLEP saws plus a sub sine through a 2-pole low-pass.
#[derive(Clone, Copy)]
struct Pad {
    env: Adsr,
    phase: [f64; 3],
    inc: [f64; 3],
    lp1: f32,
    lp2: f32,
    coef: f32,
    level: f32,
}

fn poly_blep(t: f64, dt: f64) -> f32 {
    if t < dt {
        let t = t / dt;
        (t + t - t * t - 1.0) as f32
    } else if t > 1.0 - dt {
        let t = (t - 1.0) / dt;
        (t * t + t + t + 1.0) as f32
    } else {
        0.0
    }
}

impl Pad {
    fn new(note: u8, velocity: f32, sr: f32) -> Self {
        let f0 = hz(note);
        let cutoff = (f0 * 6.0).clamp(600.0, 5000.0);
        Pad {
            env: Adsr::new(0.02, 0.3, 0.75, 0.35, sr),
            phase: [0.0, 0.37, 0.0],
            inc: [
                f0 * 2f64.powf(-7.0 / 1200.0) / sr as f64,
                f0 * 2f64.powf(7.0 / 1200.0) / sr as f64,
                f0 * 0.5 / sr as f64,
            ],
            lp1: 0.0,
            lp2: 0.0,
            coef: 1.0 - (-TAU * cutoff as f32 / sr).exp(),
            level: 0.16 * velocity.clamp(0.1, 1.0),
        }
    }

    fn render(&mut self, l: &mut [f32], r: &mut [f32]) -> bool {
        for i in 0..l.len() {
            let mut s = 0.0;
            for k in 0..2 {
                s += (2.0 * self.phase[k] - 1.0) as f32 - poly_blep(self.phase[k], self.inc[k]);
                self.phase[k] = (self.phase[k] + self.inc[k]).fract();
            }
            s = s * 0.5 + 0.3 * (TAU * self.phase[2] as f32).sin();
            self.phase[2] = (self.phase[2] + self.inc[2]).fract();
            self.lp1 += self.coef * (s - self.lp1);
            self.lp2 += self.coef * (self.lp1 - self.lp2);
            let v = self.lp2 * self.level * self.env.next() * 0.7;
            l[i] += v;
            r[i] += v;
        }
        !self.env.done()
    }
}

/// Near-sine bell, clear for hearing intervals.
#[derive(Clone, Copy)]
struct Bell {
    env: Adsr,
    phase: f64,
    inc: f64,
    level: f32,
}

impl Bell {
    fn new(note: u8, velocity: f32, sr: f32) -> Self {
        Bell {
            env: Adsr::new(0.005, 1.2, 0.35, 0.4, sr),
            phase: 0.0,
            inc: hz(note) / sr as f64,
            level: 0.2 * velocity.clamp(0.1, 1.0),
        }
    }

    fn render(&mut self, l: &mut [f32], r: &mut [f32]) -> bool {
        for i in 0..l.len() {
            let ph = TAU * self.phase as f32;
            let s = ph.sin() + 0.2 * (2.0 * ph).sin() + 0.05 * (3.0 * ph).sin();
            self.phase = (self.phase + self.inc).fract();
            let v = s * self.level * self.env.next() * 0.7;
            l[i] += v;
            r[i] += v;
        }
        !self.env.done()
    }
}

#[derive(Clone, Copy)]
enum Sound {
    Piano(Piano),
    Pad(Pad),
    Bell(Bell),
}

#[derive(Clone, Copy)]
struct Voice {
    sound: Option<Sound>,
    note: u8,
    instrument: u8,
    key_down: bool,
    held_by_pedal: bool,
    age: u64,
}

/// A polyphonic synth with fixed voice pools per instrument (no allocation after creation).
pub struct Synth {
    voices: Vec<Voice>,
    sr: f32,
    clock: u64,
    pedal: bool,
}

impl Synth {
    pub fn new(sr: f32) -> Self {
        let empty = Voice { sound: None, note: 0, instrument: 0, key_down: false, held_by_pedal: false, age: 0 };
        let mut voices = Vec::new();
        for (instrument, count) in [(PIANO, 24), (PAD, 12), (BELL, 8)] {
            for _ in 0..count {
                voices.push(Voice { instrument, ..empty });
            }
        }
        Synth { voices, sr, clock: 0, pedal: false }
    }

    pub fn note_on(&mut self, instrument: u8, note: u8, velocity: f32) {
        let instrument = if (PIANO..=BELL).contains(&instrument) { instrument } else { PIANO };
        // A re-struck note replaces the one still ringing.
        self.note_off_now(instrument, note);
        self.clock += 1;

        let pool = self.voices.iter().enumerate().filter(|(_, v)| v.instrument == instrument);
        let idx = pool
            .clone()
            .find(|(_, v)| v.sound.is_none())
            .or_else(|| pool.min_by_key(|(_, v)| (v.key_down, v.age)))
            .map(|(i, _)| i);
        let Some(idx) = idx else { return };

        let seed = (self.clock as u32).wrapping_mul(2654435761) ^ note as u32;
        let sound = match instrument {
            PAD => Sound::Pad(Pad::new(note, velocity, self.sr)),
            BELL => Sound::Bell(Bell::new(note, velocity, self.sr)),
            _ => Sound::Piano(Piano::new(note, velocity, self.sr, seed)),
        };
        self.voices[idx] = Voice { sound: Some(sound), note, instrument, key_down: true, held_by_pedal: false, age: self.clock };
    }

    fn note_off_now(&mut self, instrument: u8, note: u8) {
        for v in self.voices.iter_mut() {
            if v.sound.is_some() && v.instrument == instrument && v.note == note {
                v.key_down = false;
                v.held_by_pedal = false;
                release(v);
            }
        }
    }

    pub fn note_off(&mut self, instrument: u8, note: u8) {
        for v in self.voices.iter_mut() {
            if v.sound.is_some() && v.instrument == instrument && v.note == note && v.key_down {
                v.key_down = false;
                if self.pedal {
                    v.held_by_pedal = true;
                } else {
                    release(v);
                }
            }
        }
    }

    pub fn set_pedal(&mut self, down: bool) {
        self.pedal = down;
        if !down {
            for v in self.voices.iter_mut() {
                if v.held_by_pedal {
                    v.held_by_pedal = false;
                    release(v);
                }
            }
        }
    }

    pub fn all_notes_off(&mut self) {
        self.pedal = false;
        for v in self.voices.iter_mut() {
            v.key_down = false;
            v.held_by_pedal = false;
            release(v);
        }
    }

    /// Adds every sounding voice into `l` / `r`.
    pub fn render(&mut self, l: &mut [f32], r: &mut [f32]) {
        for v in self.voices.iter_mut() {
            let alive = match v.sound.as_mut() {
                Some(Sound::Piano(p)) => p.render(l, r),
                Some(Sound::Pad(p)) => p.render(l, r),
                Some(Sound::Bell(b)) => b.render(l, r),
                None => continue,
            };
            if !alive {
                v.sound = None;
            }
        }
    }
}

fn release(v: &mut Voice) {
    match v.sound.as_mut() {
        Some(Sound::Piano(p)) => p.releasing = true,
        Some(Sound::Pad(p)) => p.env.release(),
        Some(Sound::Bell(b)) => b.env.release(),
        None => {}
    }
}
