--[[
Music theory helpers: note names, intervals, the major scale.
]]
local M = {}

const SHARPS = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }
const FLATS = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" }
const MAJOR_STEPS = { 0, 2, 4, 5, 7, 9, 11 }
const INTERVALS = { "P1", "m2", "M2", "m3", "M3", "P4", "TT", "P5", "m6", "M6", "m7", "M7", "P8" }

function M.pitchClass(note: number): number
	return note % 12
end

--- Flat keys (F, Bb, Eb, Ab, Db, Gb) spell black keys as flats.
function M.prefersFlats(keyPc: number): boolean
	local pc = keyPc % 12
	return pc == 5 or pc == 10 or pc == 3 or pc == 8 or pc == 1 or pc == 6
end

function M.pcName(note: number, flats: boolean?): string
	return (if flats then FLATS else SHARPS)[note % 12 + 1]
end

function M.noteName(note: number, flats: boolean?): string
	return M.pcName(note, flats) .. tostring(note // 12 - 1)
end

function M.keyName(keyPc: number): string
	return M.pcName(keyPc, M.prefersFlats(keyPc)) .. " major"
end

function M.intervalName(semitones: number): string
	local s = math.abs(semitones)
	if s <= 12 then
		return INTERVALS[s + 1]
	end
	return tostring(s // 12) .. "oct+" .. INTERVALS[s % 12 + 1]
end

--- "+M3", "-m2" or "=" for a melodic step.
function M.signedInterval(semitones: number): string
	if semitones == 0 then
		return "="
	end
	return (if semitones > 0 then "+" else "-") .. M.intervalName(semitones)
end

--- MIDI note of a major-scale degree (0 = tonic; negative and > 6 wrap octaves).
function M.scaleNote(tonic: number, degree: number): number
	local octave = degree // 7
	local idx = degree - octave * 7
	return tonic + MAJOR_STEPS[idx + 1] + 12 * octave
end

function M.isDiatonic(keyPc: number, note: number): boolean
	local rel = (note - keyPc) % 12
	return table.find(MAJOR_STEPS, rel) ~= nil
end

--- Pitch classes of the diatonic triad on `degree` (0..6) of a major key.
function M.triad(keyPc: number, degree: number): { number }
	local tonic = keyPc
	return {
		M.scaleNote(tonic, degree) % 12,
		M.scaleNote(tonic, degree + 2) % 12,
		M.scaleNote(tonic, degree + 4) % 12,
	}
end

return M
