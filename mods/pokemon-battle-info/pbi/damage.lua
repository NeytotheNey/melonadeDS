-- Floating damage and heal numbers on the top screen. It compares each active mon's record HP frame to frame, so the number is the HP
-- it really lost or gained, not the move's raw damage. Hits on a Substitute don't change HP, so they show nothing. A disguised foe's number
-- is in the disguise's HP, at least 1, and one whose disguise can't be read shows nothing.
local font = require("pbi.font")

local M = {}

local TOP = "top"
local DAMAGE, CRIT, HEAL, OUTLINE = 0xFFFFFF, 0xFFE040, 0x60E060, 0x101418
local LIFE = 45
local FADE = 15
local RISE = 20
local LINE_H = 7 -- per text size, LuaOverlay's kLineHeight
local OUTLINE_H = 2 -- the outline adds a pixel above and below
local OUTLINE_OFFSETS = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } }

local function text_width(text, size)
  local w = 0
  for i = 1, #text do
    w = w + (font[text:byte(i) - 31] or 12)
  end
  return w / 2 * size
end

--- A display for one game. size is the text size, 1 to 3.
function M.new(game, reader, size)
  local self = {}
  local anchors = game.layout.damage_numbers
  local known = {} -- what was at each position last frame, from active_hp. A position with no usable mon that frame isn't in it.
  local live = {}
  local rule_anchors = anchors.singles

  local function position_key(mon)
    return (mon.foe and 0 or 8) + mon.slot
  end

  local function current_y(number)
    return number.y - RISE * number.age / LIFE
  end

  --- The first y from there down where a number clears the others at that place. They all rise alike, so clear now means clear for good.
  local function clear_y(key, y)
    local h = size * LINE_H + OUTLINE_H
    local moved = true
    while moved do
      moved = false
      for i = 1, #live do
        local number = live[i]
        if number.key == key then
          local top = current_y(number)
          if y < top + h and top < y + h then
            y = top + h
            moved = true
          end
        end
      end
    end
    return y
  end

  local function add_number(mon, diff, color)
    local side = mon.foe and rule_anchors.foes or rule_anchors.mine
    local anchor = side[mon.slot]
    if not anchor then return end
    local key = position_key(mon)
    local text = tostring(diff)
    live[#live + 1] = {
      key = key, text = text, color = color, age = 0,
      x = anchor[1] - text_width(text, size) / 2, y = clear_y(key, anchor[2]),
    }
    melon.log("damage_numbers: mon " .. mon.id .. " " .. text .. (color == CRIT and " crit" or (color == HEAL and " heal" or "")))
  end

  local function watch(ctx, mon, now)
    local key = position_key(mon)
    local last = known[key]
    now[key] = mon
    -- Only the same mon in the same place counts, so a switch-in says nothing about what happened on the bench
    if not last or last.id ~= mon.id or last.real_hp == mon.real_hp then return end
    -- A level-up raises both on one frame
    if last.real_max_hp ~= mon.real_max_hp or reader.gaining_exp(ctx) then return end
    -- A disguise going on or off changes the scale, not the HP
    if last.disguised ~= mon.disguised or last.hidden or mon.hidden then return end
    local diff = math.max(math.abs(mon.hp - last.hp), 1)
    if mon.real_hp > last.real_hp then
      add_number(mon, diff, HEAL)
    else
      add_number(mon, diff, reader.hit_is_crit(ctx, mon.id) and CRIT or DAMAGE)
    end
  end

  local function draw_number(number)
    local alpha = 255
    if number.age > LIFE - FADE then
      alpha = (255 * (LIFE - number.age) // FADE)
    end
    local x, y = number.x, current_y(number)
    local outline = (alpha << 24) | OUTLINE
    for i = 1, #OUTLINE_OFFSETS do
      local offset = OUTLINE_OFFSETS[i]
      melon.draw.text(TOP, x + offset[1], y + offset[2], number.text, outline, nil, size)
    end
    melon.draw.text(TOP, x, y, number.text, (alpha << 24) | number.color, nil, size)
  end

  --- Forgets everything, for a battle that's gone or a loaded state
  function self.reset()
    if next(known) then known = {} end
    if #live > 0 then live = {} end
    reader.reset()
  end

  --- Call once per frame
  function self.frame()
    local ctx = reader.chain()
    -- Triples and rotation weren't tested and the anchors don't cover them
    if not ctx or ctx.rule >= 2 then
      self.reset()
      return
    end
    rule_anchors = ctx.rule == 0 and anchors.singles or anchors.doubles
    local mons = reader.active_hp(ctx)
    -- Rebuilt every frame, so a record that goes unusable and comes back starts over instead of showing all that changed meanwhile
    local now = {}
    for i = 1, #mons do
      watch(ctx, mons[i], now)
    end
    known = now

    local kept = 0
    for i = 1, #live do
      local number = live[i]
      if number.age < LIFE then
        draw_number(number)
        number.age = number.age + 1
        kept = kept + 1
        live[kept] = number
      end
    end
    for i = #live, kept + 1, -1 do
      live[i] = nil
    end
  end

  return self
end

return M
