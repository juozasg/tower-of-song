--[[
Title screen: THE TOWER OF SONG, the floors (levels) with best scores, keyboard and audio status,
and a piano to try your keys on.
]]
local draw = require("scripts/lib/draw")
local keyboard = require("scripts/lib/keyboard")
local sound = require("scripts/lib/sound")

const io = require("@vectarine/io")

local M = {}

local col = draw.col

export type Floor = { id: number, label: string, title: string, description: string, open: boolean, difficulty: number }

M.FLOORS = {
	{ id = 1, label = "GROUND FLOOR", title = "The Melody", description = "Two bars of melody. Play it back.", open = true, difficulty = 1.0 },
	{ id = 2, label = "SECOND FLOOR", title = "The Chords", description = "Four chords, one per bar.", open = false, difficulty = 1.5 },
	{ id = 3, label = "THE TOP", title = "The Song", description = "Melody over chords, in an echoing hall.", open = false, difficulty = 2.0 },
} :: { Floor }

local time = 0

local function sky(w: number, h: number)
	draw.gradient(0, 0, w, h, col.nightTop, col.night, 32)
	-- Fixed pseudo-random stars that twinkle.
	local seed = 12345
	for i = 1, 120 do
		seed = (seed * 16807) % 2147483647
		local x = seed % 10000 / 10000 * w
		seed = (seed * 16807) % 2147483647
		local y = seed % 10000 / 10000 * h * 0.8
		seed = (seed * 16807) % 2147483647
		local r = 0.8 + seed % 100 / 100 * 1.4
		local twinkle = 0.55 + 0.45 * math.sin(time * (0.8 + i % 5 * 0.3) + i)
		if not (y < 140 and math.abs(x - w / 2) < 360) then
			draw.circle(x, y, r, draw.rgb(255, 244, 220, 0.35 + 0.5 * twinkle))
		end
	end
	draw.circle(w * 0.86, 96, 34, draw.rgb(246, 238, 212))
	draw.circle(w * 0.86 + 13, 86, 31, col.nightTop)
end

local function windowArch(x: number, y: number, w: number, h: number, lit: boolean)
	local glass = if lit then col.gold else draw.rgb(26, 28, 48)
	if lit then
		draw.circle(x + w / 2, y + h / 2, w * 1.2, draw.rgb(240, 192, 96, 0.12))
	end
	draw.rect(x, y + w / 2, w, h - w / 2, glass)
	draw.circle(x + w / 2, y + w / 2, w / 2, glass)
	draw.rect(x + w / 2 - 1.5, y, 3, h, col.stoneDark)
	draw.rect(x, y + h * 0.55, w, 3, col.stoneDark)
end

local function floorCard(f: Floor, x: number, y: number, w: number, h: number, best: number, plays: number): boolean
	local hover = f.open and draw.hovered(x, y, w, h)
	local stone = if not f.open then col.stoneDark elseif hover then col.stoneHi else col.stone
	draw.rect(x, y, w, h, stone)
	-- Brick courses.
	local row = 0
	local by = y + 24
	while by < y + h - 4 do
		draw.rect(x, by, w, 1, draw.rgb(0, 0, 0, 0.18))
		local bx = x + (if row % 2 == 0 then 32 else 64)
		while bx < x + w do
			draw.rect(bx, by - 24, 1, 24, draw.rgb(0, 0, 0, 0.12))
			bx += 64
		end
		by += 24
		row += 1
	end
	if hover then
		draw.frame(x, y, w, h, 2, col.gold)
	end

	windowArch(x + w - 84, y + 30, 50, 70, best > 0)
	local tx = x + 26
	draw.text(f.label, tx, y + 16, 15, col.muted)
	draw.text(f.title, tx, y + 34, 30, if f.open then col.text else col.faint)
	draw.text(f.description, tx, y + 72, 16, col.muted)
	local line = if not f.open then "Locked  ·  coming next"
		elseif best > 0 then string.format("Best %d   ·   played %d", best, plays)
		elseif plays > 0 then string.format("Not cleared yet   ·   tried %d", plays)
		else "Not climbed yet"
	draw.text(line, tx, y + h - 26, 16, if best > 0 then col.gold else col.faint)
	return hover and draw.clicked(x, y, w, h)
end

local function panel(x: number, y: number, w: number, h: number)
	draw.roundRect(x, y, w, h, 14, col.panel)
end

local function guide(x: number, y: number, w: number, h: number)
	panel(x, y, w, h)
	local cy = y + 20
	draw.text("How to climb", x + 20, cy, 22, col.gold)
	cy += 40
	local steps = {
		"Listen. The phrase plays when you enter a floor. Space plays it again.",
		"Find the notes on your keyboard. The first 12 notes in each bar are free.",
		"Press Tab for Answer mode and play your answer.",
		"Press Enter to check. Right notes lock in and score straight away.",
	}
	for i, s in steps do
		draw.text(tostring(i), x + 20, cy, 18, col.gold)
		cy += draw.wrapped(s, x + 44, cy, w - 64, 17, col.text) + 8
	end
	cy += 6
	draw.rect(x + 20, cy, w - 40, 1, col.panelHi)
	cy += 16
	cy += draw.wrapped("Stuck? Every bar has a ladder of help, from hearing it slowly to revealing a note. Help costs part of that bar's points, never all of them.", x + 20, cy, w - 40, 16, col.muted)
	cy += 8
	draw.wrapped("Small wins count: solve one bar at a time.", x + 20, cy, w - 40, 16, col.muted)
end

local function setup(x: number, y: number, w: number, h: number)
	panel(x, y, w, h)
	local cy = y + 20
	draw.text("Your keyboard", x + 20, cy, 22, col.gold)
	cy += 40
	local devices = sound.midi.devices()
	if #devices == 0 then
		cy += draw.wrapped(if sound.available then "No MIDI keyboard found. Plug one in at any time." else "MIDI plugin not loaded.", x + 20, cy, w - 40, 16, col.faint)
	end
	for _, d in devices do
		draw.circle(x + 26, cy + 9, 5, if d.open then col.green else col.red)
		cy += draw.wrapped(d.name, x + 40, cy, w - 60, 17, col.text)
		if not d.open then
			cy += draw.wrapped(d.error, x + 40, cy, w - 60, 15, col.red)
		end
	end
	cy += 8
	cy += draw.wrapped("Computer keys play too: A W S E D F T G Y H U J K.", x + 20, cy, w - 40, 16, col.muted)
	cy += draw.wrapped(string.format("Z / X change octave (now C%d).", sound.octave), x + 20, cy, w - 40, 16, col.muted)

	cy += 12
	draw.rect(x + 20, cy, w - 40, 1, col.panelHi)
	cy += 16
	draw.text("Sound", x + 20, cy, 22, col.gold)
	cy += 36
	local st = sound.midi.status()
	if st.audio ~= "" then
		cy += draw.wrapped(st.audio, x + 20, cy, w - 40, 16, col.text)
	end
	if st.audioError ~= "" then
		cy += draw.wrapped(st.audioError, x + 20, cy, w - 40, 16, col.red)
	end
	cy += 8
	local vol = sound.midi.volume()
	draw.text(string.format("Volume %d%%", math.floor(vol * 100 + 0.5)), x + 20, cy + 8, 17, col.text)
	if draw.button("-", x + w - 116, cy, 44, 34, { size = 26 }) then
		sound.midi.setVolume(math.max(0, vol - 0.1))
	end
	if draw.button("+", x + w - 64, cy, 44, 34, { size = 26 }) then
		sound.midi.setVolume(math.min(1.5, vol + 0.1))
	end
end

--- Returns the floor id to start, or nil.
function M.draw(progress: { [string]: any }, dt: number): number?
	time += dt
	local w, h = draw.size()
	sky(w, h)

	draw.text("THE TOWER OF SONG", w / 2 + 3, 31, 72, draw.rgb(0, 0, 0, 0.35), "center")
	draw.text("THE TOWER OF SONG", w / 2, 28, 72, col.gold, "center")
	draw.text("a transcription game", w / 2, 104, 26, col.muted, "center")

	-- The tower: floor 1 at the bottom.
	local towerW = math.min(470, w * 0.37)
	local floorH = 140
	local pianoH = 92
	local groundY = h - pianoH - 40
	local cx = w / 2
	local x0 = cx - towerW / 2
	local start: number? = nil

	draw.rect(0, groundY, w, h - groundY, draw.rgb(24, 26, 40))
	draw.rect(x0 - 30, groundY - 8, towerW + 60, 12, col.stoneDark)
	local topY = groundY - 8
	for i, f in M.FLOORS do
		local yb = groundY - 8 - (i - 1) * floorH
		local ya = yb - floorH + 8
		local stats = progress[tostring(f.id)] or {}
		if floorCard(f, x0, ya, towerW, floorH - 8, stats.best or 0, stats.plays or 0) then
			start = f.id
		end
		draw.rect(x0 - 10, ya - 8, towerW + 20, 8, col.stoneDark)
		topY = ya - 8
	end
	local roofH = 62
	draw.poly({ { x0 - 24, topY }, { x0 + towerW + 24, topY }, { cx, topY - roofH } }, col.roof)
	draw.rect(cx - 1, topY - roofH - 36, 2, 36, col.muted)
	local wave = math.sin(time * 3) * 4
	draw.poly({ { cx + 1, topY - roofH - 36 }, { cx + 34, topY - roofH - 28 + wave }, { cx + 1, topY - roofH - 20 } }, col.gold)

	local panelW = math.min(330, (w - towerW) / 2 - 48)
	local panelY = 170
	local panelH = groundY - panelY - 20
	guide(24, panelY, panelW, panelH)
	setup(w - 24 - panelW, panelY, panelW, panelH)

	draw.text("Try your keys", 24, h - pianoH - 30, 15, col.faint)
	keyboard.draw(24, h - pianoH - 10, w - 48, pianoH, 36, 96)

	if io.isKeyJustPressed("Return") or io.isKeyJustPressed("1") then
		start = 1
	end
	return start
end

return M
