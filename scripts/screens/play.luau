--[[
The play screen for a melody floor: listen, explore, answer bar by bar, take help, check.
]]
local draw = require("scripts/lib/draw")
local keyboard = require("scripts/lib/keyboard")
local melody = require("scripts/lib/melody")
local puzzle = require("scripts/lib/puzzle")
local sound = require("scripts/lib/sound")
local theory = require("scripts/lib/theory")

const io = require("@vectarine/io")

local M = {}

local col = draw.col
const MARGIN = 20

export type PlayState = {
	puzzle: puzzle.Puzzle,
	phraseId: number,
	spans: { melody.Span },
	autoPlayIn: number,
	cleared: boolean,
	newBest: boolean,
	best: number,
}

function M.new(p: puzzle.Puzzle, best: number): PlayState
	return { puzzle = p, phraseId = 0, spans = {}, autoPlayIn = 0.6, cleared = false, newBest = false, best = best }
end

local function play(s: PlayState, opts: melody.PlayOptions)
	local notes, fx, spans = melody.arrange(s.puzzle.question, opts)
	s.phraseId = sound.midi.playPhrase(notes, fx)
	s.spans = spans
end

local function playingBar(s: PlayState): number?
	if s.phraseId == 0 or sound.midi.playingId() ~= s.phraseId then
		return nil
	end
	local t = sound.midi.position()
	for _, span in s.spans do
		if t >= span.startSec and t < span.endSec then
			return span.bar
		end
	end
	return nil
end

local function costLabel(factor: number): string
	if factor >= 0.9999 then
		return "free"
	end
	local pct = (1 - factor) * 100
	return if pct < 0.95 then string.format("-%.1f%%", pct) else string.format("-%d%%", math.floor(pct + 0.5))
end

local function slotBox(p: puzzle.Puzzle, slot: puzzle.Slot, x: number, y: number, w: number, h: number, cursor: boolean, time: number)
	local bg = col.panelHi
	if slot.state == "solved" then
		bg = col.greenDark
	elseif slot.state == "wrong" then
		bg = col.redDark
	end
	draw.roundRect(x, y, w, h, 8, bg)

	local flats = puzzle.flats(p)
	local label, color = "?", col.faint
	if slot.state == "solved" then
		label, color = theory.pcName(slot.target, flats), col.green
	elseif slot.state == "entered" or slot.state == "wrong" then
		label = theory.pcName(slot.entered :: number, flats)
		color = if slot.state == "wrong" then col.red else col.text
	elseif slot.revealed then
		label, color = theory.pcName(slot.target, flats), col.goldDim
	end
	draw.text(label, x + w / 2, y + (h - 24) / 2, 24, color, "center")
	if slot.revealed and slot.state ~= "solved" then
		draw.frame(x + 3, y + 3, w - 6, h - 6, 1, col.goldDim)
	end
	if cursor then
		local pulse = 0.5 + 0.5 * math.sin(time * 6)
		draw.frame(x, y, w, h, 3, draw.rgb(240, 192, 96, 0.45 + 0.55 * pulse))
	end
end

local time = 0

--- Draws the screen and handles input. Returns "tower" to go back, "next" for a new puzzle.
function M.draw(s: PlayState, events: { sound.NoteEvent }, dt: number): string?
	time += dt
	local p = s.puzzle
	local q = p.question
	local w, h = draw.size()
	local width = w - MARGIN * 2
	local result: string? = nil

	if not s.cleared then
		p.stats.seconds += dt
	end
	if s.autoPlayIn > 0 then
		s.autoPlayIn -= dt
		if s.autoPlayIn <= 0 then
			play(s, puzzle.useListen(p, puzzle.LISTEN[1]))
		end
	end

	-- Notes from MIDI keyboards, the computer keyboard and the mouse.
	if not s.cleared then
		for _, e in events do
			if e.on then
				puzzle.noteOn(p, e.note)
			end
		end
	end

	draw.gradient(0, 0, w, h, col.nightTop, col.night, 24)

	-- ---------------------------------------------------------- top bar
	if draw.button("< Tower", MARGIN, 16, 100, 42) then
		result = "tower"
	end
	draw.text("Ground floor  ·  The Melody", MARGIN + 120, 14, 30, col.text)
	draw.text(string.format("%s  ·  %d bpm  ·  %s", theory.keyName(q.keyPc), q.bpm, q.source), MARGIN + 121, 46, 15, col.muted)
	local score = tostring(puzzle.score(p))
	draw.text(score, w - MARGIN, 8, 48, col.gold, "right")
	draw.text("SCORE", w - MARGIN - draw.textWidth(score, 48) - 10, 24, 15, col.muted, "right")
	draw.text(string.format("still available %d  ·  perfect %d", puzzle.potential(p), puzzle.perfect(p)), w - MARGIN, 52, 14, col.muted, "right")

	-- ---------------------------------------------------------- transport
	local ty = 78
	local bx = MARGIN
	for i, l in puzzle.LISTEN do
		local label = if l.id == "play" then "Play  (Space)" else l.label
		if draw.button(label, bx, ty, 140, 52, { primary = i == 1, sub = costLabel(puzzle.listenCost(p, l)) }) then
			play(s, puzzle.useListen(p, l))
		end
		bx += 150
	end
	if draw.button("Stop  (Esc)", bx, ty, 120, 52) then
		sound.midi.stop()
	end
	local modeW = 150
	if draw.button("Explore", w - MARGIN - modeW * 2 - 10, ty, modeW, 52, { active = not p.answering, sub = "play freely" }) then
		puzzle.setAnswering(p, false)
	end
	if draw.button("Answer  (Tab)", w - MARGIN - modeW, ty, modeW, 52, { active = p.answering, sub = "fill the slots" }) then
		puzzle.setAnswering(p, true)
	end

	-- ---------------------------------------------------------- bars
	local barsY = 148
	local cardH = 196
	local gap = 16
	local cardW = (width - gap * (q.bars - 1)) / q.bars
	local playing = playingBar(s)
	local cursor = puzzle.cursor(p)
	for bar = 0, q.bars - 1 do
		local x = MARGIN + bar * (cardW + gap)
		local b = p.bars[bar + 1]
		local selected = p.selected == bar
		local solved = puzzle.barSolved(p, bar)
		draw.roundRect(x, barsY, cardW, cardH, 12, if draw.hovered(x, barsY, cardW, cardH) then col.panelHi else col.panel)
		if playing == bar then
			draw.frame(x, barsY, cardW, cardH, 3, col.gold)
		elseif selected then
			draw.frame(x, barsY, cardW, cardH, 2, col.blue)
		end
		if draw.clicked(x, barsY, cardW, cardH) then
			p.selected = bar
		end

		draw.text("BAR " .. (bar + 1), x + 16, barsY + 14, 16, if selected then col.blue else col.muted)
		if solved then
			draw.text("SOLVED", x + 76, barsY + 14, 16, col.green)
		end
		local meterW = 90
		draw.rect(x + cardW - 16 - meterW, barsY + 20, meterW, 6, col.stoneDark)
		draw.rect(x + cardW - 16 - meterW, barsY + 20, meterW * b.share, 6, if solved then col.green else col.gold)
		draw.text(string.format("%d%%", math.floor(b.share * 100 + 0.5)), x + cardW - 24 - meterW, barsY + 14, 15, col.muted, "right")

		local idxs = puzzle.slotsIn(p, bar)
		local boxW = math.min(64, (cardW - 32 - 10 * (#idxs - 1)) / #idxs)
		for k, i in idxs do
			slotBox(p, p.slots[i], x + 16 + (k - 1) * (boxW + 10), barsY + 50, boxW, 62, i == cursor, time)
		end
		local ly = barsY + 126
		if b.shown.firstName then
			draw.text("first note  " .. puzzle.firstNoteText(p, bar), x + 18, ly, 16, col.gold)
			ly += 22
		end
		if b.shown.contour then
			draw.text("contour  " .. puzzle.contourText(p, bar), x + 18, ly, 16, col.gold)
			ly += 22
		end
		if b.shown.intervals then
			draw.text("intervals  " .. puzzle.intervalsText(p, bar), x + 18, ly, 16, col.gold)
		end
	end

	-- ---------------------------------------------------------- help and answer panels
	local keysH = 130
	local keysY = h - keysH - 14
	local panelY = barsY + cardH + 16
	local panelH = keysY - panelY - 14
	local helpW = math.floor(width * 0.5 - 8)
	local bar = p.selected

	draw.roundRect(MARGIN, panelY, helpW, panelH, 12, col.panel)
	draw.text("Help for bar " .. (bar + 1), MARGIN + 18, panelY + 16, 22, col.gold)
	local barShare = p.bars[bar + 1].share
	draw.text(if puzzle.barSolved(p, bar) then "solved, help is free" else string.format("worth %d%% of its points", math.floor(barShare * 100 + 0.5)), MARGIN + 190, panelY + 20, 16, col.muted)
	local nextHint = puzzle.nextHint(p, bar)
	if draw.button("Next hint", MARGIN + helpW - 150, panelY + 12, 132, 36, { primary = nextHint ~= nil, disabled = nextHint == nil }) and nextHint then
		local opts = puzzle.useHint(p, nextHint, bar)
		if opts then
			play(s, opts)
		end
	end
	local colW = (helpW - 36 - 10) / 2
	local hy = panelY + 60
	local column = 0
	for _, hint in puzzle.HINTS do
		local available = puzzle.hintAvailable(p, hint, bar)
		local uses = p.bars[bar + 1].uses[hint.id] or 0
		if available or uses > 0 then
			local hx = MARGIN + 18 + column * (colW + 10)
			local hover = available and draw.hovered(hx, hy, colW, 36)
			draw.roundRect(hx, hy, colW, 36, 7, if not available then col.panel elseif hover then col.stoneHi else col.panelHi)
			local labelColor = if not available then col.faint elseif hint.reveal then draw.rgb(244, 216, 160) else col.text
			draw.text(hint.label, hx + 12, hy + 9, 16, labelColor)
			local cost = if available then costLabel(puzzle.hintCost(p, hint, bar)) else "used"
			draw.text(cost, hx + colW - 12, hy + 9, 15, if available then col.gold else col.faint, "right")
			if uses > 0 then
				draw.text("x" .. uses, hx + colW - 12 - draw.textWidth(cost, 15) - 10, hy + 10, 13, col.muted, "right")
			end
			if available and draw.clicked(hx, hy, colW, 36) then
				local opts = puzzle.useHint(p, hint, bar)
				if opts then
					play(s, opts)
				end
			end
			column += 1
			if column == 2 then
				column = 0
				hy += 44
			end
		end
	end

	local ax = MARGIN + helpW + 16
	local aw = width - helpW - 16
	draw.roundRect(ax, panelY, aw, panelH, 12, col.panel)
	draw.text(if p.answering then "Answer mode" else "Explore mode", ax + 18, panelY + 16, 22, col.gold)
	local explain = if p.answering then "Each note you play fills the next slot. Wrong ones turn red; play them again."
		else string.format("Play freely to find the notes. %d free per bar, then each costs 1%% of the selected bar.", puzzle.FREE_EXPLORE)
	draw.wrapped(explain, ax + 18, panelY + 50, aw - 36, 16, col.muted)
	local by = panelY + 100
	if draw.button("Undo  (Backspace)", ax + 18, by, 170, 40) then
		puzzle.undo(p)
	end
	if draw.button("Clear", ax + 198, by, 80, 40) then
		puzzle.clear(p)
	end
	local doCheck = draw.button("Check  (Enter)", ax + 288, by, 170, 40, { primary = true })
	if p.message ~= "" then
		draw.text(p.message, ax + 18, by + 56, 18, col.text)
	end
	local st = p.stats
	draw.text(string.format("listens %d  ·  bar listens %d  ·  explored %d  ·  hints %d  ·  wrong %d  ·  %d:%02d",
		st.listens, st.barListens, st.explored, st.hints, st.wrong, st.seconds // 60, math.floor(st.seconds % 60)),
		ax + 18, panelY + panelH - 32, 14, col.faint)

	keyboard.draw(MARGIN, keysY, width, keysH, 36, 96)

	-- ---------------------------------------------------------- shortcuts
	if not s.cleared then
		if io.isKeyJustPressed("Space") then
			if sound.midi.isPlaying() then
				sound.midi.stop()
			else
				play(s, puzzle.useListen(p, puzzle.LISTEN[1]))
			end
		end
		if io.isKeyJustPressed("Tab") then
			puzzle.setAnswering(p, not p.answering)
		end
		if io.isKeyJustPressed("Backspace") then
			puzzle.undo(p)
		end
		for i = 1, q.bars do
			if io.isKeyJustPressed(tostring(i)) then
				p.selected = i - 1
			end
		end
		if doCheck or io.isKeyJustPressed("Return") then
			local _, complete = puzzle.check(p)
			if complete then
				s.cleared = true
				result = "cleared"
			end
		end
		if io.isKeyJustPressed("Escape") then
			if sound.midi.isPlaying() then
				sound.midi.stop()
			else
				result = "tower"
			end
		end
	else
		result = M.drawCleared(s, w, h) or result
	end
	return result
end

--- The "floor cleared" card. Returns "next" or "tower" when chosen.
function M.drawCleared(s: PlayState, w: number, h: number): string?
	local p = s.puzzle
	draw.rect(0, 0, w, h, draw.rgb(8, 8, 20, 0.65))
	local cw, ch = 540, 330
	local x, y = (w - cw) / 2, (h - ch) / 2 - 40
	draw.roundRect(x, y, cw, ch, 16, col.panelHi)
	draw.text("Floor cleared!", x + 30, y + 26, 34, col.gold)
	draw.text(tostring(puzzle.score(p)), x + 30, y + 70, 64, col.text)
	draw.text(if s.newBest then "New best for this floor!" else string.format("Best on this floor: %d", s.best), x + 30, y + 146, 18, if s.newBest then col.green else col.muted)
	local names = {}
	for _, slot in p.slots do
		table.insert(names, theory.pcName(slot.target, puzzle.flats(p)))
	end
	draw.text("Melody  " .. table.concat(names, " "), x + 30, y + 180, 17, col.text)
	local st = p.stats
	draw.text(string.format("%d listens, %d bar listens, %d notes explored, %d hints, %d wrong",
		st.listens, st.barListens, st.explored, st.hints, st.wrong), x + 30, y + 208, 15, col.muted)
	local choice: string? = nil
	if draw.button("Next puzzle  (Enter)", x + 30, y + ch - 76, 220, 48, { primary = true }) or io.isKeyJustPressed("Return") then
		choice = "next"
	end
	if draw.button("Hear it again", x + 262, y + ch - 76, 130, 48) then
		play(s, {})
	end
	if draw.button("Tower", x + 404, y + ch - 76, 106, 48) or io.isKeyJustPressed("Escape") then
		choice = "tower"
	end
	return choice
end

return M
