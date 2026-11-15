//! Effects: a Freeverb-style stereo reverb and a ping-pong echo.

const COMBS: [usize; 8] = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
const ALLPASSES: [usize; 4] = [556, 441, 341, 225];
const SPREAD: usize = 23;

struct Comb {
    buf: Vec<f32>,
    pos: usize,
    store: f32,
}

impl Comb {
    fn new(len: usize) -> Self {
        Comb { buf: vec![0.0; len.max(1)], pos: 0, store: 0.0 }
    }

    fn process(&mut self, input: f32, feedback: f32, damp: f32) -> f32 {
        let out = self.buf[self.pos];
        self.store = out * (1.0 - damp) + self.store * damp;
        self.buf[self.pos] = input + self.store * feedback;
        self.pos = (self.pos + 1) % self.buf.len();
        out
    }
}

struct Allpass {
    buf: Vec<f32>,
    pos: usize,
}

impl Allpass {
    fn new(len: usize) -> Self {
        Allpass { buf: vec![0.0; len.max(1)], pos: 0 }
    }

    fn process(&mut self, input: f32) -> f32 {
        let buffered = self.buf[self.pos];
        self.buf[self.pos] = input + buffered * 0.5;
        self.pos = (self.pos + 1) % self.buf.len();
        buffered - input
    }
}

pub struct Reverb {
    combs: [Vec<Comb>; 2],
    allpasses: [Vec<Allpass>; 2],
    feedback: f32,
    damp: f32,
    wet: f32,
    target_wet: f32,
}

impl Reverb {
    pub fn new(sr: f32) -> Self {
        let scale = sr / 44100.0;
        let len = |n: usize, side: usize| ((n + side * SPREAD) as f32 * scale) as usize;
        Reverb {
            combs: [0, 1].map(|s| COMBS.iter().map(|&n| Comb::new(len(n, s))).collect()),
            allpasses: [0, 1].map(|s| ALLPASSES.iter().map(|&n| Allpass::new(len(n, s))).collect()),
            feedback: 0.84,
            damp: 0.2,
            wet: 0.0,
            target_wet: 0.0,
        }
    }

    /// `size` and `damping` in 0..1; `wet` is the reverb level added to the dry signal.
    pub fn set(&mut self, size: f32, damping: f32, wet: f32) {
        self.feedback = size * 0.28 + 0.7;
        self.damp = damping * 0.4;
        self.target_wet = wet;
    }

    pub fn process(&mut self, l: &mut [f32], r: &mut [f32]) {
        if self.target_wet == 0.0 && self.wet < 1.0e-4 {
            self.wet = 0.0;
            return;
        }
        for i in 0..l.len() {
            self.wet += 0.0005 * (self.target_wet - self.wet);
            let input = (l[i] + r[i]) * 0.015;
            let mut out = [0.0f32; 2];
            for side in 0..2 {
                let mut acc = 0.0;
                for c in self.combs[side].iter_mut() {
                    acc += c.process(input, self.feedback, self.damp);
                }
                for a in self.allpasses[side].iter_mut() {
                    acc = a.process(acc);
                }
                out[side] = acc;
            }
            l[i] += out[0] * self.wet * 3.0;
            r[i] += out[1] * self.wet * 3.0;
        }
    }
}

pub struct Echo {
    l: Vec<f32>,
    r: Vec<f32>,
    pos: usize,
    delay: usize,
    feedback: f32,
    mix: f32,
    target_mix: f32,
}

impl Echo {
    pub fn new(sr: f32) -> Self {
        let n = (sr * 2.0) as usize;
        Echo { l: vec![0.0; n], r: vec![0.0; n], pos: 0, delay: 1, feedback: 0.0, mix: 0.0, target_mix: 0.0 }
    }

    pub fn set(&mut self, seconds: f32, sr: f32, feedback: f32, mix: f32) {
        self.delay = ((seconds * sr) as usize).clamp(1, self.l.len() - 1);
        self.feedback = feedback;
        self.target_mix = mix;
    }

    pub fn process(&mut self, l: &mut [f32], r: &mut [f32]) {
        if self.target_mix == 0.0 && self.mix < 1.0e-4 {
            self.mix = 0.0;
            return;
        }
        let size = self.l.len();
        for i in 0..l.len() {
            self.mix += 0.0005 * (self.target_mix - self.mix);
            let read = (self.pos + size - self.delay) % size;
            let (dl, dr) = (self.l[read], self.r[read]);
            self.l[self.pos] = l[i] + self.feedback * dr;
            self.r[self.pos] = r[i] + self.feedback * dl;
            l[i] += self.mix * dl;
            r[i] += self.mix * dr;
            self.pos = (self.pos + 1) % size;
        }
    }
}
