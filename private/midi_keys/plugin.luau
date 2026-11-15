--[[
MIDI keyboard input and a small built-in synth.

```luau
const midi = require("@vectarine/midi_keys")

function Update(deltaTime: number)
	for _, e in midi.poll() do
		if e.on then
			print("pressed", e.note)
		end
	end
end
```

Keyboards are opened automatically and reconnected when plugged back in. Notes played on a MIDI
keyboard sound immediately (inside the plugin), so the game only needs `poll` to react to them.
]]
local module = {}

module.VERSION = 1
module.NAME = "MIDI Keys"

--- Instrument numbers for `noteOn` and `playPhrase`.
module.PIANO = 1
module.PAD = 2
module.BELL = 3

export type NoteEvent = {
	note: number, -- MIDI note number, 60 = middle C
	velocity: number, -- 0..1, 0 for note-off
	on: boolean,
}

export type Device = {
	name: string,
	open: boolean,
	error: string, -- why the device could not be opened (for example another app holds it)
}

export type PhraseNote = {
	start: number, -- seconds from the start of the phrase
	duration: number, -- seconds
	note: number,
	velocity: number?, -- 0..1, default 0.8
	instrument: number?, -- PIANO (default), PAD or BELL
}

export type Status = {
	audio: string, -- output device, sample rate and buffer size
	audioError: string,
	midiError: string,
}

--- Returns the note-on / note-off events received from MIDI keyboards since the last call.
function module.poll(): { NoteEvent }
	error("Implemented in native code")
end

--- Returns the notes currently held down on MIDI keyboards.
function module.held(): { number }
	error("Implemented in native code")
end

--- Lists MIDI input devices and whether they are open.
function module.devices(): { Device }
	error("Implemented in native code")
end

--- Starts a note on the live synth (use it for mouse or computer-keyboard playing).
function module.noteOn(note: number, velocity: number?, instrument: number?): ()
	error("Implemented in native code")
end

--- Releases a note started with `noteOn`.
function module.noteOff(note: number, instrument: number?): ()
	error("Implemented in native code")
end

--- Releases every live note (MIDI keyboard and `noteOn`).
function module.allNotesOff(): ()
	error("Implemented in native code")
end

--- Plays a phrase with sample-accurate timing, replacing any phrase already playing.
--- `fx` is "dry", "room" (default) or "hall" (big reverb plus echo).
--- Returns an id that `playingId` reports while the phrase plays.
function module.playPhrase(notes: { PhraseNote }, fx: string?): number
	error("Implemented in native code")
end

--- Stops the phrase that is playing.
function module.stop(): ()
	error("Implemented in native code")
end

--- The id of the phrase that is playing, or 0.
function module.playingId(): number
	error("Implemented in native code")
end

function module.isPlaying(): boolean
	error("Implemented in native code")
end

--- Seconds since the current phrase started.
function module.position(): number
	error("Implemented in native code")
end

--- Master volume, 0..2 (default 0.8).
function module.setVolume(volume: number): ()
	error("Implemented in native code")
end

function module.volume(): number
	error("Implemented in native code")
end

function module.status(): Status
	error("Implemented in native code")
end

--- The version of the native code (should match VERSION).
function module.version(): number
	error("Implemented in native code")
end

return module
