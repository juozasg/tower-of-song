--[[
Sound and note input in one place.

Wraps the MIDI Keys plugin (MIDI keyboards + synth). When the plugin isn't loaded (for example in
`vecta screenshot` / `vecta test`, which run games without plugins) a silent stand-in keeps the game
working. Also turns the computer keyboard into a piano.
]]
const io = require("@vectarine/io")

local M = {}

export type NoteEvent = { note: number, on: boolean, velocity: number }

local ok, plugin = pcall(function()
	return require("@vectarine/midi_keys")
end)

M.available = ok and type(plugin) == "table"

local stub = {
	PIANO = 1,
	PAD = 2,
	BELL = 3,
	poll = function()
		return {}
	end,
	held = function()
		return {}
	end,
	devices = function()
		return {}
	end,
	noteOn = function(...) end,
	noteOff = function(...) end,
	allNotesOff = function() end,
	playPhrase = function(...)
		return 0
	end,
	stop = function() end,
	playingId = function()
		return 0
	end,
	isPlaying = function()
		return false
	end,
	position = function()
		return 0
	end,
	setVolume = function(...) end,
	volume = function()
		return 0.8
	end,
	status = function()
		return { audio = "", audioError = "MIDI Keys plugin not loaded", midiError = "" }
	end,
}

M.midi = if M.available then plugin else stub

-- Computer keys as a piano, laid out like most DAWs: A = C, W = C#, S = D ...
const QWERTY = { "A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J", "K", "O", "L", "P", ";", "'" }
M.QWERTY = QWERTY

M.octave = 4
local qwertyDown: { [string]: number } = {}
local userHeld: { [number]: boolean } = {}
local midiHeld: { [number]: boolean } = {}
local events: { NoteEvent } = {}
local enabled = true

function M.qwertyBase(): number
	return 12 * (M.octave + 1)
end

--- Notes from mouse clicks and other on-screen input: sounds and records them.
function M.userNote(note: number, on: boolean)
	if on then
		M.midi.noteOn(note, 0.75)
		userHeld[note] = true
	else
		M.midi.noteOff(note)
		userHeld[note] = nil
	end
	table.insert(events, { note = note, on = on, velocity = if on then 0.75 else 0 })
end

--- Disables the computer-keyboard piano (for example while a text field has focus).
function M.setKeyboardEnabled(on: boolean)
	enabled = on
end

--- Collects this frame's note events from MIDI keyboards and the computer keyboard.
--- Call once per frame, before the screens; mouse notes added during the frame appear next frame.
function M.update(): { NoteEvent }
	local result = events
	events = {}

	-- MIDI notes already sound inside the plugin; here we only track and report them.
	for _, e in M.midi.poll() do
		midiHeld[e.note] = if e.on then true else nil
		table.insert(result, { note = e.note, on = e.on, velocity = e.velocity })
	end

	if enabled then
		if io.isKeyJustPressed("Z") then
			M.octave = math.max(2, M.octave - 1)
		elseif io.isKeyJustPressed("X") then
			M.octave = math.min(6, M.octave + 1)
		end
		for i, key in QWERTY do
			if io.isKeyJustPressed(key) and not qwertyDown[key] then
				local note = M.qwertyBase() + i - 1
				qwertyDown[key] = note
				M.midi.noteOn(note, 0.75)
				userHeld[note] = true
				table.insert(result, { note = note, on = true, velocity = 0.75 })
			end
		end
	end
	-- Releases are tracked by the note that was started, even if the octave changed meanwhile.
	for key, note in qwertyDown do
		if not io.isKeyDown(key) then
			qwertyDown[key] = nil
			M.midi.noteOff(note)
			userHeld[note] = nil
			table.insert(result, { note = note, on = false, velocity = 0 })
		end
	end
	return result
end

--- Notes held right now, from any source.
function M.lit(): { [number]: boolean }
	local lit = {}
	for n in midiHeld do
		lit[n] = true
	end
	for n in userHeld do
		lit[n] = true
	end
	return lit
end

return M
