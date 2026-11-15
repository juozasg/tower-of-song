//! MIDI Keys: a Vectarine plugin that adds MIDI keyboard input and a small synth
//! (piano, pad, bell, reverb, echo, sample-accurate phrase playback).
//!
//! Luau: `const midi = require("@vectarine/midi_keys")`. See plugin.luau for the API.

mod audio;
mod fx;
mod midi;
mod voices;

use std::ffi::{CStr, c_int};
use std::sync::Mutex;
use std::sync::atomic::Ordering;

use mlua::ffi::{self, lua_State};

use audio::{Audio, Cmd, Fx};
use midi::Midi;

const VERSION: i32 = 1;

/// Mirrors `vectarine_plugin_sdk::plugininterface::PluginInterface` (Vectarine 0.7.0).
/// Depending on mlua directly (same revision and features) avoids building the SDK's
/// SDL2, wgpu and egui dependencies just to receive this pointer.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PluginInterface<'a> {
    pub lua: &'a mlua::Lua,
}

struct State {
    audio: Option<Audio>,
    audio_error: String,
    midi: Midi,
}

static STATE: Mutex<Option<State>> = Mutex::new(None);

/// With MIDI_KEYS_DEBUG=1 in the environment, logs plugin start-up and the first call of each
/// Luau function to stderr (the release runtime shows no Lua errors, so this is how to check
/// that a game is talking to the plugin).
fn debug_log(what: &str) {
    use std::collections::HashSet;
    use std::sync::OnceLock;
    static SEEN: Mutex<Option<HashSet<String>>> = Mutex::new(None);
    static ENABLED: OnceLock<bool> = OnceLock::new();
    if !*ENABLED.get_or_init(|| std::env::var_os("MIDI_KEYS_DEBUG").is_some()) {
        return;
    }
    if let Ok(mut seen) = SEEN.lock() {
        if seen.get_or_insert_with(HashSet::new).insert(what.to_string()) {
            eprintln!("midi_keys: {what}");
        }
    }
}

fn with_state<R>(f: impl FnOnce(&mut State) -> R) -> Option<R> {
    STATE.lock().ok()?.as_mut().map(f)
}

fn start_state() -> State {
    let ((cmd_tx, cmd_rx), (live_tx, live_rx)) = audio::queues();
    let (audio, audio_error) = match Audio::start(cmd_tx, cmd_rx, live_rx) {
        Ok(a) => (Some(a), String::new()),
        Err(e) => (None, e),
    };
    let midi = Midi::new(live_tx);
    State { audio, audio_error, midi }
}

// ------------------------------------------------------------------ Luau helpers

/// Creates a table that always has a real hash part.
///
/// The engine and this plugin each contain their own copy of Luau. An empty hash part points at
/// Luau's static `dummynode`, and each copy has its own. A table created here with an empty hash
/// part would point at the plugin's copy; when the engine's garbage collector frees the table it
/// doesn't recognise that address and frees a static, which crashes. One reserved slot avoids it.
unsafe fn new_table(l: *mut lua_State, array: usize, hash: usize) {
    unsafe { ffi::lua_createtable(l, array as c_int, hash.max(1) as c_int) };
}

unsafe fn set_number(l: *mut lua_State, key: &CStr, value: f64) {
    unsafe {
        ffi::lua_pushnumber(l, value);
        ffi::lua_setfield(l, -2, key.as_ptr());
    }
}

unsafe fn set_bool(l: *mut lua_State, key: &CStr, value: bool) {
    unsafe {
        ffi::lua_pushboolean(l, value as c_int);
        ffi::lua_setfield(l, -2, key.as_ptr());
    }
}

unsafe fn set_string(l: *mut lua_State, key: &CStr, value: &str) {
    unsafe {
        let s = std::ffi::CString::new(value.replace('\0', "")).unwrap_or_default();
        ffi::lua_pushstring(l, s.as_ptr());
        ffi::lua_setfield(l, -2, key.as_ptr());
    }
}

unsafe fn field_number(l: *mut lua_State, idx: c_int, key: &CStr) -> Option<f64> {
    unsafe {
        ffi::lua_getfield(l, idx, key.as_ptr());
        let mut ok: c_int = 0;
        let v = ffi::lua_tonumberx(l, -1, &mut ok);
        ffi::lua_pop(l, 1);
        (ok != 0).then_some(v)
    }
}

unsafe fn arg_number(l: *mut lua_State, idx: c_int, default: f64) -> f64 {
    unsafe {
        let mut ok: c_int = 0;
        let v = ffi::lua_tonumberx(l, idx, &mut ok);
        if ok != 0 { v } else { default }
    }
}

fn note_arg(v: f64) -> u8 {
    v.round().clamp(0.0, 127.0) as u8
}

// ------------------------------------------------------------------ Luau functions

unsafe extern "C-unwind" fn l_poll(l: *mut lua_State) -> c_int {
    debug_log("poll() called");
    let events = with_state(|s| s.midi.drain()).unwrap_or_default();
    unsafe {
        new_table(l, events.len(), 0);
        for (i, e) in events.iter().enumerate() {
            new_table(l, 0, 3);
            set_number(l, c"note", e.note as f64);
            set_number(l, c"velocity", e.velocity as f64);
            set_bool(l, c"on", e.on);
            ffi::lua_rawseti(l, -2, (i + 1) as ffi::lua_Integer);
        }
    }
    1
}

unsafe extern "C-unwind" fn l_held(l: *mut lua_State) -> c_int {
    debug_log("held() called");
    let held = with_state(|s| s.midi.held()).unwrap_or_default();
    unsafe {
        new_table(l, held.len(), 0);
        for (i, n) in held.iter().enumerate() {
            ffi::lua_pushnumber(l, *n as f64);
            ffi::lua_rawseti(l, -2, (i + 1) as ffi::lua_Integer);
        }
    }
    1
}

unsafe extern "C-unwind" fn l_devices(l: *mut lua_State) -> c_int {
    debug_log("devices() called");
    let devices = with_state(|s| s.midi.devices()).unwrap_or_default();
    unsafe {
        new_table(l, devices.len(), 0);
        for (i, d) in devices.iter().enumerate() {
            new_table(l, 0, 3);
            set_string(l, c"name", &d.name);
            set_bool(l, c"open", d.open);
            set_string(l, c"error", &d.error);
            ffi::lua_rawseti(l, -2, (i + 1) as ffi::lua_Integer);
        }
    }
    1
}

unsafe extern "C-unwind" fn l_note_on(l: *mut lua_State) -> c_int {
    debug_log("note_on() called");
    let (note, vel, inst) = unsafe { (note_arg(arg_number(l, 1, 60.0)), arg_number(l, 2, 0.75), arg_number(l, 3, 1.0)) };
    with_state(|s| {
        if let Some(a) = s.audio.as_mut() {
            a.send(Cmd::LiveOn { note, vel: vel as f32, inst: inst as u8 });
        }
    });
    0
}

unsafe extern "C-unwind" fn l_note_off(l: *mut lua_State) -> c_int {
    debug_log("note_off() called");
    let (note, inst) = unsafe { (note_arg(arg_number(l, 1, 60.0)), arg_number(l, 2, 1.0)) };
    with_state(|s| {
        if let Some(a) = s.audio.as_mut() {
            a.send(Cmd::LiveOff { note, inst: inst as u8 });
        }
    });
    0
}

unsafe extern "C-unwind" fn l_all_notes_off(_l: *mut lua_State) -> c_int {
    debug_log("all_notes_off() called");
    with_state(|s| {
        if let Some(a) = s.audio.as_mut() {
            a.send(Cmd::LiveAllOff);
        }
    });
    0
}

unsafe extern "C-unwind" fn l_play_phrase(l: *mut lua_State) -> c_int {
    debug_log("play_phrase() called");
    let mut notes = Vec::new();
    let mut fx = Fx::Room;
    unsafe {
        if ffi::lua_istable(l, 1) != 0 {
            let n = ffi::lua_objlen(l, 1).min(4000);
            for i in 1..=n {
                ffi::lua_rawgeti(l, 1, i as ffi::lua_Integer);
                if ffi::lua_istable(l, -1) != 0 {
                    let start = field_number(l, -1, c"start").unwrap_or(0.0);
                    let dur = field_number(l, -1, c"duration").unwrap_or(0.5);
                    let note = field_number(l, -1, c"note").unwrap_or(60.0);
                    let vel = field_number(l, -1, c"velocity").unwrap_or(0.8);
                    let inst = field_number(l, -1, c"instrument").unwrap_or(1.0);
                    notes.push((start as f32, dur as f32, note_arg(note), vel as f32, inst as u8));
                }
                ffi::lua_pop(l, 1);
            }
        }
        let s = ffi::lua_tolstring(l, 2, std::ptr::null_mut());
        if !s.is_null() {
            fx = Fx::parse(&CStr::from_ptr(s).to_string_lossy());
        }
    }
    let id = with_state(|s| s.audio.as_mut().map(|a| a.play_phrase(&notes, fx)).unwrap_or(0)).unwrap_or(0);
    debug_log(&format!("first phrase: {} notes, {fx:?}, id {id}", notes.len()));
    unsafe { ffi::lua_pushnumber(l, id as f64) };
    1
}

unsafe extern "C-unwind" fn l_stop(_l: *mut lua_State) -> c_int {
    debug_log("stop() called");
    with_state(|s| {
        if let Some(a) = s.audio.as_mut() {
            a.send(Cmd::Stop);
            a.shared.playing_id.store(0, Ordering::Relaxed);
        }
    });
    0
}

unsafe extern "C-unwind" fn l_playing_id(l: *mut lua_State) -> c_int {
    debug_log("playing_id() called");
    let id = with_state(|s| s.audio.as_ref().map(|a| a.shared.playing_id.load(Ordering::Relaxed)).unwrap_or(0)).unwrap_or(0);
    unsafe { ffi::lua_pushnumber(l, id as f64) };
    1
}

unsafe extern "C-unwind" fn l_is_playing(l: *mut lua_State) -> c_int {
    debug_log("is_playing() called");
    let playing = with_state(|s| s.audio.as_ref().is_some_and(|a| a.shared.playing_id.load(Ordering::Relaxed) != 0)).unwrap_or(false);
    unsafe { ffi::lua_pushboolean(l, playing as c_int) };
    1
}

unsafe extern "C-unwind" fn l_position(l: *mut lua_State) -> c_int {
    debug_log("position() called");
    let pos = with_state(|s| s.audio.as_ref().map(|a| a.shared.position()).unwrap_or(0.0)).unwrap_or(0.0);
    unsafe { ffi::lua_pushnumber(l, pos) };
    1
}

unsafe extern "C-unwind" fn l_set_volume(l: *mut lua_State) -> c_int {
    debug_log("set_volume() called");
    let v = unsafe { arg_number(l, 1, 0.8) };
    with_state(|s| {
        if let Some(a) = s.audio.as_ref() {
            a.shared.set_volume(v as f32);
        }
    });
    0
}

unsafe extern "C-unwind" fn l_volume(l: *mut lua_State) -> c_int {
    debug_log("volume() called");
    let v = with_state(|s| s.audio.as_ref().map(|a| a.shared.volume()).unwrap_or(0.0)).unwrap_or(0.0);
    unsafe { ffi::lua_pushnumber(l, v as f64) };
    1
}

unsafe extern "C-unwind" fn l_status(l: *mut lua_State) -> c_int {
    debug_log("status() called");
    let (audio, error, midi_error) = with_state(|s| {
        (s.audio.as_ref().map(|a| a.info.clone()).unwrap_or_default(), s.audio_error.clone(), s.midi.error.clone())
    })
    .unwrap_or_default();
    unsafe {
        new_table(l, 0, 3);
        set_string(l, c"audio", &audio);
        set_string(l, c"audioError", &error);
        set_string(l, c"midiError", &midi_error);
    }
    1
}

// ------------------------------------------------------------------ hooks

#[unsafe(no_mangle)]
#[allow(improper_ctypes_definitions)]
pub extern "C" fn init_hook(plugin_interface: PluginInterface) {
    if let Ok(mut state) = STATE.lock() {
        if state.is_none() {
            let started = start_state();
            debug_log(&format!(
                "loaded v{VERSION}, audio: {}{}",
                started.audio.as_ref().map(|a| a.info.as_str()).unwrap_or(""),
                started.audio_error
            ));
            *state = Some(started);
        }
    }
    let lua = plugin_interface.lua;
    let Ok(module) = lua.create_table_with_capacity(0, 32) else { return };
    let _ = module.set("VERSION", VERSION);
    let _ = module.set("NAME", "MIDI Keys");
    let _ = module.set("PIANO", voices::PIANO);
    let _ = module.set("PAD", voices::PAD);
    let _ = module.set("BELL", voices::BELL);

    let functions: [(&str, ffi::lua_CFunction); 15] = [
        ("poll", l_poll),
        ("held", l_held),
        ("devices", l_devices),
        ("noteOn", l_note_on),
        ("noteOff", l_note_off),
        ("allNotesOff", l_all_notes_off),
        ("playPhrase", l_play_phrase),
        ("stop", l_stop),
        ("playingId", l_playing_id),
        ("isPlaying", l_is_playing),
        ("position", l_position),
        ("setVolume", l_set_volume),
        ("volume", l_volume),
        ("status", l_status),
        ("version", l_version),
    ];
    for (name, f) in functions {
        // mlua's Rust closures can't cross the plugin boundary; plain C functions can.
        if let Ok(func) = unsafe { lua.create_c_function(f) } {
            let _ = module.set(name, func);
        }
    }
    let _ = lua.register_module("@vectarine/midi_keys", module);
}


unsafe extern "C-unwind" fn l_version(l: *mut lua_State) -> c_int {
    debug_log("version() called");
    unsafe { ffi::lua_pushnumber(l, VERSION as f64) };
    1
}

#[unsafe(no_mangle)]
#[allow(improper_ctypes_definitions)]
pub extern "C" fn pre_lua_hook(_plugin_interface: PluginInterface) {
    with_state(|s| s.midi.poll());
}

#[unsafe(no_mangle)]
#[allow(improper_ctypes_definitions)]
pub extern "C" fn release_hook(_plugin_interface: PluginInterface) {
    if let Ok(mut state) = STATE.lock() {
        *state = None; // closes MIDI ports and the audio stream
    }
}

#[cfg(test)]
mod tests {
    use super::audio::{Cmd, Fx, Renderer, Shared};
    use std::sync::Arc;

    #[test]
    fn renders_a_phrase_and_live_notes() {
        let ((mut tx, rx), (mut live_tx, live_rx)) = super::audio::queues();
        let shared = Arc::new(Shared::new());
        let mut r = Renderer::new(48000.0, rx, live_rx, shared.clone());
        tx.push(Cmd::Begin { id: 1, fx: Fx::Hall }).unwrap();
        for (i, note) in [60u8, 64, 67, 72].iter().enumerate() {
            for inst in 1..=3u8 {
                tx.push(Cmd::Note { start: i as f32 * 0.3, dur: 0.4, note: *note, vel: 0.8, inst }).unwrap();
            }
        }
        tx.push(Cmd::End).unwrap();
        live_tx.push(Cmd::LiveOn { note: 48, vel: 0.9, inst: 1 }).unwrap();

        let mut buf = vec![0.0f32; 48000 * 2]; // one second, stereo
        r.fill(&mut buf, 2);
        let rms = (buf.iter().map(|x| x * x).sum::<f32>() / buf.len() as f32).sqrt();
        let peak = buf.iter().fold(0.0f32, |m, x| m.max(x.abs()));
        assert!(buf.iter().all(|x| x.is_finite()));
        assert!(rms > 0.01, "rms {rms}");
        assert!(peak < 1.0);
        assert_eq!(shared.playing_id.load(std::sync::atomic::Ordering::Relaxed), 1);
        assert!(shared.position() > 0.9);

        // Long after the phrase ends, playback reports finished.
        let mut tail = vec![0.0f32; 48000 * 2 * 8];
        r.fill(&mut tail, 2);
        assert_eq!(shared.playing_id.load(std::sync::atomic::Ordering::Relaxed), 0);
    }
}
