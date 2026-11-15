--[[
Question generation and playback arrangement.

A question is plain data (hot-reload safe):
  { seed, keyPc, bpm, beatsPerBar, bars, fx, notes = { {start = beats, length = beats, note, velocity} } }
]]
local theory = require("./theory")

local M = {}

export type Note = { start: number, length: number, note: number, velocity: number }
export type Question = {
	seed: number,
	keyPc: number,
	bpm: number,
	beatsPerBar: number,
	bars: number,
	fx: string,
	notes: { Note },
	source: string,
}

--- Deterministic random numbers (Park-Miller), so a seed always gives the same puzzle.
local function makeRng(seed: number)
	local state = seed % 2147483646 + 1
	local rng = {}
	function rng.float(): number
		state = (state * 16807) % 2147483647
		return (state - 1) / 2147483646
	end
	function rng.int(lo: number, hi: number): number
		return lo + math.floor(rng.float() * (hi - lo + 1))
	end
	function rng.pick<T>(items: { T }): T
		return items[rng.int(1, #items)]
	end
	function rng.weighted(weights: { number }): number
		local total = 0
		for _, w in weights do
			total += w
		end
		local r = rng.float() * total
		for i, w in weights do
			r -= w
			if r <= 0 then
				return i
			end
		end
		return #weights
	end
	return rng
end

M.makeRng = makeRng

const EASY_KEYS = { 0, 7, 5, 2 } -- C, G, F, D
const PATTERNS = { { 1, 1, 2 }, { 2, 1, 1 }, { 1, 1, 1, 1 }, { 1, 2, 1 } }
const END_PATTERNS = { { 1, 1, 2 }, { 2, 2 }, { 1, 1, 2 } }
-- Implied harmony for two bars (scale degrees of the chord roots): shapes the melody, not played.
const IMPLIED = { { 0, 4 }, { 0, 3 }, { 4, 0 }, { 3, 0 }, { 0, 0 } }

local function contains(list: { number }, v: number): boolean
	return table.find(list, v) ~= nil
end

--- A singable diatonic melody: chord tones on strong beats, steps between, ending on a chord tone.
function M.generateMelody(seed: number, bars: number?): Question
	local rng = makeRng(seed)
	local nbars = bars or 2
	local keyPc = rng.pick(EASY_KEYS)
	local tonic = 60 + keyPc - (if keyPc > 6 then 12 else 0)
	local low, high = 64, 79
	local implied = rng.pick(IMPLIED)

	local lowDeg, highDeg = -14, 21
	while theory.scaleNote(tonic, lowDeg) < low do
		lowDeg += 1
	end
	while theory.scaleNote(tonic, highDeg) > high do
		highDeg -= 1
	end

	local function chordTones(bar: number): { number }
		return theory.triad(keyPc, implied[(bar % #implied) + 1])
	end

	-- Start on a chord tone near the middle of the range.
	local middle = (low + high) / 2
	local current = lowDeg
	local best = math.huge
	for d = lowDeg, highDeg do
		local n = theory.scaleNote(tonic, d)
		if contains(chordTones(0), n % 12) and math.abs(n - middle) < best then
			best = math.abs(n - middle)
			current = d
		end
	end

	local notes: { Note } = {}
	local first = true
	for bar = 0, nbars - 1 do
		local lastBar = bar == nbars - 1
		local pattern = if lastBar then rng.pick(END_PATTERNS) else rng.pick(PATTERNS)
		local beat = 0
		for i, length in pattern do
			local strong = beat % 2 == 0
			local lastNote = lastBar and i == #pattern
			if not first then
				local candidates, weights = {}, {}
				for d = math.max(lowDeg, current - 4), math.min(highDeg, current + 4) do
					local n = theory.scaleNote(tonic, d)
					local dist = math.abs(d - current)
					local w = 0
					if strong or lastNote then
						if contains(chordTones(bar), n % 12) then
							w = math.exp(-0.6 * dist) * (if dist == 0 then 0.5 else 1)
							if lastNote and n % 12 == keyPc then
								w *= 4
							end
						end
					else
						w = ({ 0.2, 1.0, 0.4, 0.15, 0.05 })[dist + 1]
					end
					if w > 0 then
						table.insert(candidates, d)
						table.insert(weights, w)
					end
				end
				if #candidates > 0 then
					current = candidates[rng.weighted(weights)]
				end
			end
			first = false
			table.insert(notes, {
				start = bar * 4 + beat,
				length = length,
				note = theory.scaleNote(tonic, current),
				velocity = 0.76 + (if strong then 0.06 else 0) + (rng.float() - 0.5) * 0.08,
			})
			beat += length
		end
	end

	return {
		seed = seed,
		keyPc = keyPc,
		bpm = 84 + rng.int(0, 16),
		beatsPerBar = 4,
		bars = nbars,
		fx = "room",
		notes = notes,
		source = "generated",
	}
end

function M.barOf(q: Question, beat: number): number
	return math.floor(beat / q.beatsPerBar + 1e-6)
end

export type PlayOptions = {
	fromBar: number?, -- 0-based, inclusive
	toBar: number?,
	tempoScale: number?, -- 0.5 = half speed
	dry: boolean?,
	style: ("normal" | "noteByNote" | "firstNote")?,
}

export type PhraseNote = { start: number, duration: number, note: number, velocity: number, instrument: number }
export type Span = { bar: number, startSec: number, endSec: number }

--- Turns (part of) a question into timed notes for midi.playPhrase, plus when each bar sounds.
function M.arrange(q: Question, o: PlayOptions?): ({ PhraseNote }, string, { Span })
	local opts = o or {}
	local from = opts.fromBar or 0
	local to = opts.toBar or (q.bars - 1)
	local style = opts.style or "normal"
	local fx = if opts.dry then "dry" else q.fx
	local notes: { PhraseNote } = {}
	local spans: { Span } = {}
	local preRoll = 0.05

	if style == "normal" then
		local spb = 60 / q.bpm / (opts.tempoScale or 1)
		local offset = from * q.beatsPerBar
		local function sec(beat: number): number
			return preRoll + (beat - offset) * spb
		end
		for bar = from, to do
			table.insert(spans, { bar = bar, startSec = sec(bar * q.beatsPerBar), endSec = sec((bar + 1) * q.beatsPerBar) })
		end
		for _, n in q.notes do
			local bar = M.barOf(q, n.start)
			if bar >= from and bar <= to then
				table.insert(notes, { start = sec(n.start), duration = n.length * spb * 0.95, note = n.note, velocity = n.velocity, instrument = 1 })
			end
		end
		return notes, fx, spans
	end

	-- Slow demonstrations: rhythm removed, one note at a time.
	local t = preRoll
	for bar = from, to do
		local barStart = t
		local firstInBar = true
		for _, n in q.notes do
			if M.barOf(q, n.start) == bar then
				if style == "firstNote" then
					if firstInBar then
						table.insert(notes, { start = t, duration = 1.3, note = n.note, velocity = 0.8, instrument = 1 })
						t += 1.6
					end
				else
					table.insert(notes, { start = t, duration = 0.45, note = n.note, velocity = 0.8, instrument = 1 })
					t += 0.65
				end
				firstInBar = false
			end
		end
		t += 0.3
		table.insert(spans, { bar = bar, startSec = barStart, endSec = t })
	end
	return notes, fx, spans
end

return M
