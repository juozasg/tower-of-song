--[[
The Tower of Song: a transcription game.

Hear a phrase, find it on the keys, play it back. The less help you need, the more you score.
]]
const graphics = require("@vectarine/graphics")
const io = require("@vectarine/io")
const persist = require("@vectarine/persist")

local draw = require("./lib/draw")
local melody = require("./lib/melody")
local puzzle = require("./lib/puzzle")
local sound = require("./lib/sound")
local play = require("./screens/play")
local title = require("./screens/title")

io.setWindowTitle("The Tower of Song")
io.setResizeable(true)

-- Survives hot reloads, so editing a script doesn't throw you out of a puzzle.
local app = persist.onReload({
	screen = "title",
	play = nil :: play.PlayState?,
	progress = (persist.load("progress") or {}) :: { [string]: any },
}, "tos_app")

local function floorStats(id: number): { [string]: number }
	local key = tostring(id)
	app.progress[key] = app.progress[key] or { best = 0, plays = 0, cleared = 0 }
	return app.progress[key]
end

local function startFloor(id: number)
	local floor = title.FLOORS[id]
	if not floor or not floor.open then
		return
	end
	local stats = floorStats(id)
	stats.plays += 1
	persist.save("progress", app.progress)
	local q = melody.generateMelody(math.random(1, 2147483000), 2)
	app.play = play.new(puzzle.new(id, floor.difficulty, q), stats.best)
	app.screen = "play"
end

function Update(deltaTime: number)
	draw.beginFrame()
	graphics.clear(draw.col.night)
	local events = sound.update()

	if app.screen == "play" and app.play then
		local result = play.draw(app.play, events, deltaTime)
		if result == "cleared" then
			local stats = floorStats(app.play.puzzle.floor)
			local score = puzzle.score(app.play.puzzle)
			stats.cleared += 1
			app.play.newBest = score > stats.best
			stats.best = math.max(stats.best, score)
			persist.save("progress", app.progress)
		elseif result == "next" then
			startFloor(app.play.puzzle.floor)
		elseif result == "tower" then
			sound.midi.stop()
			app.screen = "title"
			app.play = nil
		end
	else
		local floor = title.draw(app.progress, deltaTime)
		if floor then
			startFloor(floor)
		end
	end
end
