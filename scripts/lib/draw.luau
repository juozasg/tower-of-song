--[[
Drawing and mouse helpers in pixel coordinates (origin top-left, y down), plus the palette.
Vectarine's own drawing works in OpenGL coordinates (-1..1, y up); everything here converts.
]]
const graphics = require("@vectarine/graphics")
const io = require("@vectarine/io")
const text = require("@vectarine/text")
const vec = require("@vectarine/vec")
const vec4 = require("@vectarine/vec4")

const V2 = vec.V2

local M = {}

local W, H = 1280, 800
local mouseX, mouseY = 0, 0
local mouse = { isLeftDown = false, isLeftJustPressed = false }

function M.rgb(r: number, g: number, b: number, a: number?): vec4.Vec4
	return vec4.createColor(r / 255, g / 255, b / 255, a or 1)
end

M.col = {
	night = M.rgb(17, 20, 38),
	nightTop = M.rgb(34, 30, 70),
	panel = M.rgb(30, 35, 62),
	panelHi = M.rgb(44, 51, 90),
	stone = M.rgb(70, 64, 92),
	stoneHi = M.rgb(92, 84, 118),
	stoneDark = M.rgb(46, 42, 62),
	gold = M.rgb(240, 192, 96),
	goldDim = M.rgb(150, 118, 62),
	text = M.rgb(240, 234, 220),
	muted = M.rgb(152, 158, 188),
	faint = M.rgb(96, 102, 134),
	green = M.rgb(98, 206, 140),
	greenDark = M.rgb(40, 86, 66),
	red = M.rgb(232, 110, 110),
	redDark = M.rgb(94, 44, 54),
	blue = M.rgb(110, 172, 240),
	pink = M.rgb(240, 120, 170),
	teal = M.rgb(80, 200, 200),
	white = M.rgb(250, 248, 242),
	black = M.rgb(24, 22, 30),
	roof = M.rgb(150, 60, 80),
}

--- Call once at the start of every frame.
function M.beginFrame()
	local size = io.getWindowSize()
	W, H = size.x, size.y
	local m = io.getMouse()
	mouseX, mouseY = (m.x + 1) / 2 * W, (1 - m.y) / 2 * H
	mouse = io.getMouseState()
end

function M.size(): (number, number)
	return W, H
end

local function gx(x: number): number
	return x / W * 2 - 1
end

local function gy(y: number): number
	return 1 - y / H * 2
end

function M.rect(x: number, y: number, w: number, h: number, color: vec4.Vec4)
	if w <= 0 or h <= 0 then
		return
	end
	graphics.drawRect(V2(gx(x), gy(y + h)), V2(w / W * 2, h / H * 2), color)
end

function M.circle(cx: number, cy: number, r: number, color: vec4.Vec4)
	graphics.drawEllipse(V2(gx(cx), gy(cy)), V2(r / W * 2, r / H * 2), color)
end

function M.roundRect(x: number, y: number, w: number, h: number, r: number, color: vec4.Vec4)
	r = math.min(r, w / 2, h / 2)
	M.rect(x + r, y, w - 2 * r, h, color)
	M.rect(x, y + r, r, h - 2 * r, color)
	M.rect(x + w - r, y + r, r, h - 2 * r, color)
	M.circle(x + r, y + r, r, color)
	M.circle(x + w - r, y + r, r, color)
	M.circle(x + r, y + h - r, r, color)
	M.circle(x + w - r, y + h - r, r, color)
end

--- An outline made of four thin rectangles.
function M.frame(x: number, y: number, w: number, h: number, t: number, color: vec4.Vec4)
	M.rect(x, y, w, t, color)
	M.rect(x, y + h - t, w, t, color)
	M.rect(x, y, t, h, color)
	M.rect(x + w - t, y, t, h, color)
end

function M.line(x1: number, y1: number, x2: number, y2: number, color: vec4.Vec4, thickness: number?)
	graphics.drawLine(V2(gx(x1), gy(y1)), V2(gx(x2), gy(y2)), color, (thickness or 2) / H)
end

function M.poly(points: { { number } }, color: vec4.Vec4)
	local gl = {}
	for i, p in points do
		gl[i] = V2(gx(p[1]), gy(p[2]))
	end
	graphics.drawPolygon(gl, color)
end

--- Vertical gradient made of horizontal bands.
function M.gradient(x: number, y: number, w: number, h: number, top: vec4.Vec4, bottom: vec4.Vec4, bands: number?)
	local n = bands or 24
	for i = 0, n - 1 do
		local c = top:lerp(bottom, i / (n - 1))
		M.rect(x, y + h * i / n, w, h / n + 1, c)
	end
end

function M.textWidth(s: string, size: number): number
	return text.font:measureText(s, size / H * 2).width * W / 2
end

--- Draws text whose top edge is at `y` (size is the line height in pixels).
function M.text(s: string, x: number, y: number, size: number, color: vec4.Vec4, align: ("left" | "center" | "right")?)
	if align == "center" then
		x -= M.textWidth(s, size) / 2
	elseif align == "right" then
		x -= M.textWidth(s, size)
	end
	text.font:drawText(s, V2(gx(x), gy(y + size * M.baseline)), size / H * 2, color)
end

-- Where the baseline sits as a fraction of the line height (calibrated against screenshots).
M.baseline = 0.8

--- Word-wraps `s` to `width` pixels; returns the height used.
function M.wrapped(s: string, x: number, y: number, width: number, size: number, color: vec4.Vec4, lineGap: number?): number
	local line = ""
	local dy = 0
	local step = size * (lineGap or 1.3)
	for word in s:gmatch("%S+") do
		local candidate = if line == "" then word else line .. " " .. word
		if M.textWidth(candidate, size) > width and line ~= "" then
			M.text(line, x, y + dy, size, color)
			dy += step
			line = word
		else
			line = candidate
		end
	end
	if line ~= "" then
		M.text(line, x, y + dy, size, color)
		dy += step
	end
	return dy
end

-- ------------------------------------------------------------------ mouse

function M.mouse(): (number, number)
	return mouseX, mouseY
end

function M.mouseDown(): boolean
	return mouse.isLeftDown
end

function M.hovered(x: number, y: number, w: number, h: number): boolean
	return mouseX >= x and mouseX < x + w and mouseY >= y and mouseY < y + h
end

function M.clicked(x: number, y: number, w: number, h: number): boolean
	return mouse.isLeftJustPressed and M.hovered(x, y, w, h)
end

export type ButtonStyle = {
	primary: boolean?,
	active: boolean?,
	disabled: boolean?,
	sub: string?, -- small second line, e.g. a cost
	size: number?,
}

--- A rounded button. Returns true when clicked.
function M.button(label: string, x: number, y: number, w: number, h: number, style: ButtonStyle?): boolean
	local s = style or {}
	local hover = not s.disabled and M.hovered(x, y, w, h)
	local bg = M.col.panelHi
	local fg = M.col.text
	if s.disabled then
		bg = M.col.panel
		fg = M.col.faint
	elseif s.primary or s.active then
		bg = if hover then M.rgb(250, 212, 132) else M.col.gold
		fg = M.col.black
	elseif hover then
		bg = M.col.stoneHi
	end
	M.roundRect(x, y, w, h, 8, bg)
	local size = s.size or 16
	if s.sub then
		M.text(label, x + w / 2, y + h / 2 - size * 0.95, size, fg, "center")
		local subColor = if s.primary or s.active then M.rgb(60, 46, 20) else (if s.disabled then M.col.faint else M.col.gold)
		M.text(s.sub, x + w / 2, y + h / 2 + 2, size * 0.85, subColor, "center")
	else
		M.text(label, x + w / 2, y + (h - size) / 2, size, fg, "center")
	end
	return not s.disabled and M.clicked(x, y, w, h)
end

return M
