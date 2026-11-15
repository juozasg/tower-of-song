--[[
The rules of a melody puzzle: entering answers, checking, help costs and scoring.

The puzzle is a plain table so it survives hot reloads; these functions operate on it.
Points for a note = BASE x floor difficulty x the bar's remaining share x chunk bonus.
Help shrinks a bar's share but never below MIN_SHARE, so a right answer always scores.
]]
local melody = require("./melody")
local theory = require("./theory")

local M = {}

M.BASE = 100
M.MIN_SHARE = 0.05
M.WRONG_CHECK = 0.9 -- per bar with a wrong answer in a check
M.EXPLORE_COST = 0.99 -- per explored note after the free ones
M.FREE_EXPLORE = 12 -- free exploring notes per bar
M.CHUNK_BONUS = 0.04 -- per extra note solved in the same check
M.MAX_CHUNK = 1.4

export type Hint = {
	id: string,
	label: string,
	tip: string,
	first: number, -- share kept on first use
	again: number, -- share kept on each later use
	play: melody.PlayOptions?,
	reveal: string?,
}

-- Ordered from least to most helpful.
M.HINTS = {
	{ id = "hearBar", label = "Hear this bar", tip = "Just this bar, as written.", first = 0.96, again = 0.995, play = {} },
	{ id = "dry", label = "Hear it dry", tip = "This bar without reverb.", first = 0.97, again = 0.995, play = { dry = true } },
	{ id = "slow", label = "Slowly", tip = "This bar at half speed.", first = 0.92, again = 0.995, play = { tempoScale = 0.5 } },
	{ id = "noteByNote", label = "Note by note", tip = "Evenly spaced, rhythm removed.", first = 0.88, again = 0.99, play = { style = "noteByNote" } },
	{ id = "firstNote", label = "First note alone", tip = "The bar's first note, held.", first = 0.82, again = 0.99, play = { style = "firstNote" } },
	{ id = "contour", label = "Show contour", tip = "Up, down or same for each step.", first = 0.80, again = 1, reveal = "contour" },
	{ id = "intervals", label = "Show intervals", tip = "The interval of each step.", first = 0.70, again = 1, reveal = "intervals" },
	{ id = "firstName", label = "Show first note", tip = "The name of the bar's first note.", first = 0.70, again = 1, reveal = "firstName" },
	{ id = "revealNext", label = "Reveal next note", tip = "Shows the next note. You still play it.", first = 0.60, again = 0.60, reveal = "next" },
} :: { Hint }

-- Whole-phrase listening.
M.LISTEN = {
	{ id = "play", label = "Play", tip = "The whole phrase. The first three plays are free.", free = 3, first = 0.98, again = 0.98, play = {} },
	{ id = "slowAll", label = "Slow", tip = "The whole phrase at half speed.", free = 0, first = 0.95, again = 0.99, play = { tempoScale = 0.5 } },
}

export type Slot = {
	bar: number,
	target: number,
	state: string, -- "empty" | "entered" | "wrong" | "solved"
	entered: number?,
	revealed: boolean,
	points: number,
	wrongCount: number,
}

export type Bar = {
	share: number,
	explored: number,
	uses: { [string]: number },
	shown: { [string]: boolean },
}

export type Puzzle = {
	floor: number,
	difficulty: number,
	question: melody.Question,
	slots: { Slot },
	bars: { Bar },
	selected: number, -- 0-based bar
	answering: boolean,
	listens: { [string]: number },
	entries: { number },
	message: string,
	stats: { listens: number, barListens: number, explored: number, hints: number, wrong: number, checks: number, seconds: number },
	done: boolean,
}

function M.new(floor: number, difficulty: number, q: melody.Question): Puzzle
	local p: Puzzle = {
		floor = floor,
		difficulty = difficulty,
		question = q,
		slots = {},
		bars = {},
		selected = 0,
		answering = false,
		listens = {},
		entries = {},
		message = "",
		stats = { listens = 0, barListens = 0, explored = 0, hints = 0, wrong = 0, checks = 0, seconds = 0 },
		done = false,
	}
	for bar = 0, q.bars - 1 do
		p.bars[bar + 1] = { share = 1, explored = 0, uses = {}, shown = {} }
	end
	for _, n in q.notes do
		table.insert(p.slots, { bar = melody.barOf(q, n.start), target = n.note, state = "empty", revealed = false, points = 0, wrongCount = 0 })
	end
	return p
end

function M.flats(p: Puzzle): boolean
	return theory.prefersFlats(p.question.keyPc)
end

function M.slotsIn(p: Puzzle, bar: number): { number }
	local result = {}
	for i, s in p.slots do
		if s.bar == bar then
			table.insert(result, i)
		end
	end
	return result
end

function M.barSolved(p: Puzzle, bar: number): boolean
	for _, s in p.slots do
		if s.bar == bar and s.state ~= "solved" then
			return false
		end
	end
	return true
end

function M.complete(p: Puzzle): boolean
	for _, s in p.slots do
		if s.state ~= "solved" then
			return false
		end
	end
	return #p.slots > 0
end

local function charge(p: Puzzle, bar: number, factor: number)
	if factor >= 1 or M.barSolved(p, bar) then
		return
	end
	local b = p.bars[bar + 1]
	b.share = math.max(M.MIN_SHARE, b.share * factor)
end

function M.value(p: Puzzle, slot: Slot): number
	return M.BASE * p.difficulty * p.bars[slot.bar + 1].share
end

-- ------------------------------------------------------------------ entering

--- Index of the slot the next answer note goes into, or nil.
function M.cursor(p: Puzzle): number?
	if not p.answering then
		return nil
	end
	for pass = 1, 2 do
		for i, s in p.slots do
			local inRange = if pass == 1 then s.bar >= p.selected else s.bar < p.selected
			if inRange and (s.state == "empty" or s.state == "wrong") then
				return i
			end
		end
	end
	return nil
end

local function advanceIfFull(p: Puzzle)
	local function open(bar: number): boolean
		for _, s in p.slots do
			if s.bar == bar and (s.state == "empty" or s.state == "wrong") then
				return true
			end
		end
		return false
	end
	if open(p.selected) then
		return
	end
	for k = 1, p.question.bars - 1 do
		local bar = (p.selected + k) % p.question.bars
		if open(bar) then
			p.selected = bar
			return
		end
	end
end

function M.noteOn(p: Puzzle, note: number)
	if p.done then
		return
	end
	if not p.answering then
		local b = p.bars[p.selected + 1]
		b.explored += 1
		p.stats.explored += 1
		if b.explored > M.FREE_EXPLORE then
			charge(p, p.selected, M.EXPLORE_COST)
		end
		return
	end
	local i = M.cursor(p)
	if not i then
		p.message = "Every note is filled in. Press Enter to check."
		return
	end
	local s = p.slots[i]
	s.state = "entered"
	s.entered = note
	table.insert(p.entries, i)
	advanceIfFull(p)
end

function M.undo(p: Puzzle)
	while #p.entries > 0 do
		local i = table.remove(p.entries) :: number
		local s = p.slots[i]
		if s.state == "entered" then
			s.state = "empty"
			s.entered = nil
			p.selected = s.bar
			return
		end
	end
end

function M.clear(p: Puzzle)
	for _, s in p.slots do
		if s.state == "entered" then
			s.state = "empty"
			s.entered = nil
		end
	end
	p.entries = {}
end

function M.setAnswering(p: Puzzle, on: boolean)
	p.answering = on
end

--- Grades every entered note. Returns points earned and whether the puzzle is now complete.
function M.check(p: Puzzle): (number, boolean)
	local correct, wrong = {}, 0
	local wrongBars = {}
	for i, s in p.slots do
		if s.state == "entered" then
			-- Graded by pitch class: the octave doesn't matter.
			if (s.entered :: number) % 12 == s.target % 12 then
				table.insert(correct, i)
			else
				s.state = "wrong"
				s.wrongCount += 1
				wrong += 1
				wrongBars[s.bar] = true
			end
		end
	end
	if #correct == 0 and wrong == 0 then
		p.message = if p.answering then "Nothing to check yet. Play your answer first." else "Press Tab for Answer mode, then play your answer."
		return 0, false
	end

	p.stats.checks += 1
	p.stats.wrong += wrong
	local bonus = math.min(M.MAX_CHUNK, 1 + M.CHUNK_BONUS * math.max(0, #correct - 1))
	local points = 0
	for _, i in correct do
		local s = p.slots[i]
		s.points = math.floor(M.value(p, s) * bonus + 0.5)
		s.state = "solved"
		points += s.points
	end
	for bar in wrongBars do
		charge(p, bar, M.WRONG_CHECK)
	end
	local remaining = {}
	for _, i in p.entries do
		if p.slots[i].state == "entered" then
			table.insert(remaining, i)
		end
	end
	p.entries = remaining

	local complete = M.complete(p)
	if complete then
		p.done = true
		p.message = "Solved! +" .. points
	elseif wrong == 0 then
		p.message = #correct .. " correct, +" .. points
	elseif #correct == 0 then
		p.message = wrong .. " not right yet. Try again or take a hint."
	else
		p.message = #correct .. " correct (+" .. points .. "), " .. wrong .. " not right yet."
	end
	advanceIfFull(p)
	return points, complete
end

-- ------------------------------------------------------------------ help

function M.hintById(id: string): Hint?
	for _, h in M.HINTS do
		if h.id == id then
			return h
		end
	end
	return nil
end

function M.hintAvailable(p: Puzzle, h: Hint, bar: number): boolean
	local b = p.bars[bar + 1]
	local idxs = M.slotsIn(p, bar)
	if h.id == "dry" then
		return p.question.fx ~= "dry"
	end
	if h.reveal == nil then
		return #idxs > 0
	end
	if M.barSolved(p, bar) then
		return false
	end
	if h.reveal == "next" then
		for _, i in idxs do
			if not p.slots[i].revealed and p.slots[i].state ~= "solved" then
				return true
			end
		end
		return false
	end
	if h.reveal ~= "firstName" and #idxs < 2 then
		return false
	end
	return not b.shown[h.reveal]
end

--- Share of the bar kept if the hint is used now (1 = free).
function M.hintCost(p: Puzzle, h: Hint, bar: number): number
	if M.barSolved(p, bar) then
		return 1
	end
	local uses = p.bars[bar + 1].uses[h.id] or 0
	return if uses == 0 then h.first else h.again
end

--- Applies the hint's cost and effect. Returns playback options for listening hints.
function M.useHint(p: Puzzle, h: Hint, bar: number): melody.PlayOptions?
	if not M.hintAvailable(p, h, bar) then
		return nil
	end
	local b = p.bars[bar + 1]
	local factor = M.hintCost(p, h, bar)
	if (b.uses[h.id] or 0) == 0 then
		p.stats.hints += 1
	end
	b.uses[h.id] = (b.uses[h.id] or 0) + 1
	charge(p, bar, factor)

	if h.reveal == "next" then
		-- Prefer a note the player hasn't filled in yet.
		local target: number? = nil
		for _, i in M.slotsIn(p, bar) do
			local s = p.slots[i]
			if s.state ~= "solved" and not s.revealed then
				if target == nil or (p.slots[target].state == "entered" and s.state ~= "entered") then
					target = i
				end
			end
		end
		if target then
			p.slots[target].revealed = true
		end
	elseif h.reveal then
		b.shown[h.reveal] = true
	end

	if h.play then
		p.stats.barListens += 1
		local o = table.clone(h.play) :: melody.PlayOptions
		o.fromBar, o.toBar = bar, bar
		return o
	end
	return nil
end

function M.nextHint(p: Puzzle, bar: number): Hint?
	for _, h in M.HINTS do
		if M.hintAvailable(p, h, bar) and (p.bars[bar + 1].uses[h.id] or 0) == 0 then
			return h
		end
	end
	local reveal = M.hintById("revealNext") :: Hint
	return if M.hintAvailable(p, reveal, bar) then reveal else nil
end

function M.listenCost(p: Puzzle, l: any): number
	local uses = p.listens[l.id] or 0
	if uses < l.free then
		return 1
	end
	return if uses == l.free then l.first else l.again
end

function M.useListen(p: Puzzle, l: any): melody.PlayOptions
	local factor = M.listenCost(p, l)
	p.listens[l.id] = (p.listens[l.id] or 0) + 1
	p.stats.listens += 1
	for bar = 0, p.question.bars - 1 do
		charge(p, bar, factor)
	end
	return table.clone(l.play)
end

-- ------------------------------------------------------------------ display

local function barNotes(p: Puzzle, bar: number): { number }
	local notes = {}
	for _, i in M.slotsIn(p, bar) do
		table.insert(notes, p.slots[i].target)
	end
	return notes
end

function M.contourText(p: Puzzle, bar: number): string
	local n = barNotes(p, bar)
	local parts = {}
	for i = 2, #n do
		table.insert(parts, if n[i] > n[i - 1] then "up" elseif n[i] < n[i - 1] then "down" else "same")
	end
	return table.concat(parts, " ")
end

function M.intervalsText(p: Puzzle, bar: number): string
	local n = barNotes(p, bar)
	local parts = {}
	for i = 2, #n do
		table.insert(parts, theory.signedInterval(n[i] - n[i - 1]))
	end
	return table.concat(parts, " ")
end

function M.firstNoteText(p: Puzzle, bar: number): string
	local n = barNotes(p, bar)
	return if #n > 0 then theory.pcName(n[1], M.flats(p)) else ""
end

function M.score(p: Puzzle): number
	local total = 0
	for _, s in p.slots do
		total += s.points
	end
	return total
end

function M.potential(p: Puzzle): number
	local total = 0
	for _, s in p.slots do
		if s.state ~= "solved" then
			total += M.value(p, s)
		end
	end
	return math.floor(total + 0.5)
end

function M.perfect(p: Puzzle): number
	local n = #p.slots
	local bonus = math.min(M.MAX_CHUNK, 1 + M.CHUNK_BONUS * math.max(0, n - 1))
	return math.floor(M.BASE * p.difficulty * n * bonus + 0.5)
end

return M
