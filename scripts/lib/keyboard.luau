--[[
An on-screen piano: lights held keys, labels the computer-keyboard mapping, plays with the mouse
(click or drag across keys).
]]
local draw = require("./draw")
local sound = require("./sound")
local theory = require("./theory")

local M = {}

local mouseNote: number? = nil

local function isBlack(note: number): boolean
	local pc = note % 12
	return pc == 1 or pc == 3 or pc == 6 or pc == 8 or pc == 10
end

--- Draws the piano and sends mouse-played notes through `sound.userNote`.
function M.draw(x: number, y: number, w: number, h: number, low: number, high: number)
	local whites = 0
	for n = low, high do
		if not isBlack(n) then
			whites += 1
		end
	end
	local ww = w / whites
	local bw, bh = ww * 0.62, h * 0.62

	local keys = {}
	local blacks = {}
	local wi = 0
	for n = low, high do
		if isBlack(n) then
			table.insert(blacks, { note = n, x = x + wi * ww - bw / 2, y = y, w = bw, h = bh })
		else
			table.insert(keys, { note = n, x = x + wi * ww, y = y, w = ww, h = h })
			wi += 1
		end
	end

	-- Mouse: black keys sit on top, so test them first.
	local hovered: number? = nil
	for _, k in blacks do
		if draw.hovered(k.x, k.y, k.w, k.h) then
			hovered = k.note
		end
	end
	if not hovered then
		for _, k in keys do
			if draw.hovered(k.x, k.y, k.w, k.h) then
				hovered = k.note
			end
		end
	end
	local wanted = if draw.mouseDown() then hovered else nil
	if wanted ~= mouseNote then
		-- Only start a mouse note when the press began on the keyboard.
		if mouseNote ~= nil then
			sound.userNote(mouseNote, false)
		end
		if wanted ~= nil and (mouseNote ~= nil or draw.clicked(x, y, w, h)) then
			sound.userNote(wanted, true)
			mouseNote = wanted
		else
			mouseNote = nil
		end
	end

	local lit = sound.lit()
	local base = sound.qwertyBase()
	for _, k in keys do
		local c = if lit[k.note] then draw.col.blue elseif hovered == k.note then draw.rgb(232, 230, 222) else draw.col.white
		draw.rect(k.x + 1, k.y, k.w - 2, k.h - 3, c)
		draw.rect(k.x + 1, k.y + k.h - 6, k.w - 2, 3, draw.rgb(200, 196, 186))
		if k.note % 12 == 0 then
			draw.text(theory.noteName(k.note), k.x + 4, k.y + k.h - 22, 13, draw.col.faint)
		end
		local q = sound.QWERTY[k.note - base + 1]
		if q then
			draw.text(q, k.x + k.w / 2, k.y + k.h - 44, 15, draw.col.goldDim, "center")
		end
	end
	for _, k in blacks do
		local c = if lit[k.note] then draw.col.blue elseif hovered == k.note then draw.rgb(70, 66, 84) else draw.col.black
		draw.rect(k.x, k.y, k.w, k.h, c)
		local q = sound.QWERTY[k.note - base + 1]
		if q then
			draw.text(q, k.x + k.w / 2, k.y + k.h - 20, 13, draw.col.gold, "center")
		end
	end
	-- Felt strip along the top.
	draw.rect(x, y, w, 4, draw.rgb(130, 40, 56))
end

return M
