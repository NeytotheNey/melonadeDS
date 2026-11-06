-- Draws the foe panels (types on its HP bar, status and stat changes under its HP box, weak, resist and immune on the bottom screen), the
-- move tags and the card. Everything here works on what a reader returns and on the game's layout, so it doesn't care which game this is.
--
-- Rows are lists of tokens (icons, text) that get laid out first and drawn after, because the card's background has to go under them and
-- its height depends on how many lines they wrap to. When the icon sheet can't be loaded, types and status fall back to text chips, and
-- the ATK and DEF stat changes to text like the other stats.
local font = require("pbi.font")
local matchup = require("pbi.matchup")
local typechart = require("pbi.typechart")
local species_names = require("pbi.data_species")
local ability_names = require("pbi.data_abilities")
local icon_rects = require("pbi.data_icons")

local M = {}

local TOP, BOTTOM = "top", "bottom"
local ICON_SHEET = "img/icons.png"
local ACTION_SCREEN = 1 -- the "screen" value of the action menu: docs/pokemon-battle-ram.md, "Verified in game"

local TYPE_COLOR = {
  NOR = 0xFF9A9A82, FIR = 0xFFE2622C, WAT = 0xFF3F7FE0, ELE = 0xFFF0C81E, GRA = 0xFF5AAE46, ICE = 0xFF7CD0D0,
  FIG = 0xFFB03428, POI = 0xFF9A44A0, GRO = 0xFFCFA850, FLY = 0xFF8F7EE6, PSY = 0xFFEE5C86, BUG = 0xFF9AAE22,
  ROC = 0xFFA88F38, GHO = 0xFF6A5490, DRA = 0xFF6838F0, DAR = 0xFF6A5040, STE = 0xFFA8A8C2,
}
local DARK_TEXT_TYPE = { ELE = true, ICE = true, GRO = true, NOR = true, BUG = true, STE = true, ROC = true, GRA = true, FLY = true }
local STATUS_COLOR = {
  PAR = 0xFFF0C81E, BRN = 0xFFE2622C, PSN = 0xFF9A44A0, TOX = 0xFF6E1E86, SLP = 0xFF8A8A9A, FRZ = 0xFF7CD0D0, CNF = 0xFFEE5C86,
  FNT = 0xFF4A5560,
}
local DARK_TEXT_STATUS = { PAR = true, FRZ = true, SLP = true }

local CARD, CARD_LINE = 0xF0101418, 0xFF4A5560
local DARK, WHITE, DIM = 0xFF101418, 0xFFFFFFFF, 0xFFB4BCC6
local HOT, WARM, NEUTRAL, COOL, OFF, GREEN = 0xFFFF7A1A, 0xFFFFB347, 0xFFE6E6E6, 0xFF62B6FF, 0xFF9AA2AC, 0xFFA6E08A

local ICON_H = 9
local CHIP_H = 8
local MARKER_COLOR = { ["x4"] = HOT, ["x2"] = WARM, ["x.5"] = COOL, ["x.25"] = COOL, ["x0"] = OFF }

-- The sheet and one draw options table per icon, nil while there's no sheet
local sheet, icon_options

local function text_width(s)
  local w = 0
  for i = 1, #s do
    w = w + (font[s:byte(i) - 31] or 12)
  end
  return w / 2
end

local function load_icons()
  sheet, icon_options = nil, nil
  local image, reason
  if melon.image then
    image, reason = melon.image.load(ICON_SHEET)
  end
  if not image then
    melon.log("battle_info: no icons, using text chips: " .. tostring(reason))
    return
  end
  sheet = image
  icon_options = {}
  for name, rect in pairs(icon_rects) do
    icon_options[name] = { scale = 0.5, src = rect, w = rect[3] / 2 }
  end
end

-- Tokens ------------------------------------------------------------------------------------------------------

local function icon_token(name, gap, dy)
  local options = icon_options[name]
  return { icon = options, w = options.w, gap = gap or 0, dy = dy or 0, counts = true }
end

local function chip_token(text, bg, fg, gap)
  return { text = text, w = text_width(text) + 2, bg = bg or NEUTRAL, fg = fg, gap = gap or 0, counts = true }
end

local function type_token(t, gap)
  if icon_options then return icon_token(t, gap) end
  return chip_token(t, TYPE_COLOR[t], DARK_TEXT_TYPE[t] and DARK or WHITE, gap)
end

local function status_token(label, gap, dy)
  if icon_options then return icon_token(label, gap, dy) end
  return chip_token(label, STATUS_COLOR[label], DARK_TEXT_STATUS[label] and DARK or WHITE, gap)
end

--- Plain text. A group label stays on the same line as the icon after it.
local function words(text, color, gap, group)
  return { text = text, w = text_width(text), fg = color, gap = gap or 0, group = group, dy = 1 }
end

--- An icon that stays on the same line as the token after it, and counts as one with it for +N
local function leading_icon_token(name, gap)
  local token = icon_token(name, gap)
  token.counts = false
  token.group = true
  return token
end

--- The mark in front of a group: an arrow or the no sign, or its text without the sheet. Stays with the icon after it.
local function marker_token(label, gap)
  if icon_options then return leading_icon_token(label, gap) end
  local token = words(label, MARKER_COLOR[label], gap, true)
  token.outline = true
  return token
end

--- One stat change: the stat's icon and the number when the sheet has an icon for it, otherwise text like "SPA+1"
local function add_stage(tokens, entry, gap, outline)
  local name, n = entry[1], entry[2]
  local text = (n > 0 and "+" or "") .. n
  if icon_options and icon_options[name] then
    tokens[#tokens + 1] = leading_icon_token(name, gap)
    gap = -.5 -- half a pixel after the icon, not the usual 1
  else
    text = name .. text
  end
  tokens[#tokens + 1] = { text = text, w = text_width(text), fg = n > 0 and WARM or COOL, gap = gap, counts = true, dy = 1, outline = outline }
end

local function add_types(tokens, types, gap)
  if not types then return end
  for i = 1, #types do
    tokens[#tokens + 1] = type_token(types[i], i == 1 and gap or 0)
  end
end

local function add_status(tokens, mon, gap, dy)
  for i = 1, #mon.status do
    tokens[#tokens + 1] = status_token(mon.status[i], i == 1 and gap or 0, dy)
  end
end

local function add_stages(tokens, mon, gap, outline)
  for i = 1, #mon.stages do
    add_stage(tokens, mon.stages[i], i == 1 and gap or 2, outline)
  end
end

-- Layout -------------------------------------------------------------------------------------------------------

--- Places tokens from x, wrapping to indent, up to max_lines. When they don't all fit, the last line ends with +N.
--- Returns a row: its placed tokens, how many lines it took and where the first free x is on its last line.
local function lay_out(tokens, x, indent, limit, max_lines, outline)
  local placed, line, i = {}, 1, 1
  local n = #tokens
  local pen = x
  while i <= n do
    local tk = tokens[i]
    local start = pen + tk.gap
    local need = tk.w
    if tk.group and tokens[i + 1] then
      need = need + 1 + tokens[i + 1].gap + tokens[i + 1].w
    end
    if start + need > limit then
      if line < max_lines then
        line = line + 1
        start = indent
      else
        break
      end
    end
    placed[#placed + 1] = { tk = tk, x = start, line = line }
    pen = start + tk.w + 1
    i = i + 1
  end

  if i <= n then
    local left = 0
    for j = i, n do
      if tokens[j].counts then left = left + 1 end
    end
    if left > 0 then
      local function plus()
        local text = "+" .. left
        return { text = text, w = text_width(text), fg = DIM, gap = 1, dy = 1, outline = outline }
      end
      local more = plus()
      while #placed > 0 do
        local last = placed[#placed]
        local dangling = last.tk.group
        if not dangling and pen + more.gap + more.w <= limit then break end
        if last.line ~= line then break end
        placed[#placed] = nil
        if last.tk.counts then
          left = left + 1
          more = plus()
        end
        pen = last.x
      end
      placed[#placed + 1] = { tk = more, x = pen + more.gap, line = line }
      pen = pen + more.gap + more.w + 1
    end
  end
  return { placed = placed, lines = line, right_end = pen }
end

local function draw_text(screen, tk, x, y)
  if tk.outline then
    for dy = -1, 1 do
      for dx = -1, 1 do
        if dx ~= 0 or dy ~= 0 then
          melon.draw.text(screen, x + dx, y + dy, tk.text, DARK)
        end
      end
    end
  end
  melon.draw.text(screen, x, y, tk.text, tk.fg)
end

local function draw_token(screen, tk, x, y)
  if tk.icon then
    melon.draw.image(screen, x, y + tk.dy, sheet, tk.icon)
  elseif tk.bg then
    melon.draw.rect(screen, x, y, tk.w, CHIP_H, tk.bg, true)
    melon.draw.text(screen, x + 1, y + .5, tk.text, tk.fg)
  else
    draw_text(screen, tk, x, y + tk.dy)
  end
end

local function draw_row(screen, row, y, row_h)
  for _, p in ipairs(row.placed) do
    draw_token(screen, p.tk, p.x, y + (p.line - 1) * row_h)
  end
  if row.right then
    for _, p in ipairs(row.right) do
      draw_token(screen, p.tk, p.x, y)
    end
  end
end

--- Right-aligned tokens on a row's first line. Returns false when they'd run into what's on that line.
local function place_right(row, tokens, limit, left_end)
  local total = 0
  for i = 1, #tokens do
    total = total + tokens[i].w + (i > 1 and tokens[i].gap + 1 or 0)
  end
  local x = limit - total
  if x < left_end then return false end
  local placed = {}
  for i = 1, #tokens do
    if i > 1 then x = x + tokens[i].gap end
    placed[i] = { tk = tokens[i], x = x }
    x = x + tokens[i].w + 1
  end
  row.right = placed
  return true
end

--- Moves the token after the row's title to a column, so icons line up across rows
local function to_column(tokens, column)
  if tokens[2] then
    tokens[2].gap = math.max(0, column - tokens[1].w - 1)
  end
  return tokens
end

-- Things every view needs --------------------------------------------------------------------------------------

local function is_alive(mon)
  return mon and mon.hp > 0 and not mon.no_info
end

--- The mon that counts as attacker when no move is being picked: the one choosing, otherwise the first one out
local function acting_mon(battle)
  if battle.choosing then return battle.choosing end
  for i = 1, #battle.mine do
    if battle.mine[i] then return battle.mine[i] end
  end
  return nil
end

--- The ability that counts for a foe's multipliers, nil when it mustn't be used. A disguised foe's is its disguise's, as the reader gives it.
local function foe_ability(foe, show_hidden)
  if show_hidden then return foe.ability end
  return nil
end

local function foe_groups(foe, attacker, show_hidden)
  return matchup.groups(foe, foe_ability(foe, show_hidden), attacker and attacker.ability)
end

--- The foes' abilities that change your own mon's rows. Only known when the foe's abilities are shown.
local function foe_row_abilities(battle, show_hidden)
  local abilities = {}
  if show_hidden then
    for i = 1, #battle.foes do
      local foe = battle.foes[i]
      if is_alive(foe) and matchup.changes_your_rows(foe.ability) then
        abilities[#abilities + 1] = foe.ability
      end
    end
  end
  return abilities
end

local function my_groups(mon, battle, show_hidden)
  return matchup.groups_against(mon, mon.ability, foe_row_abilities(battle, show_hidden))
end

local function effect_color(m)
  if m == 4 then return HOT end
  if m == 2 then return WARM end
  if m == 1 then return NEUTRAL end
  if m == 0 then return OFF end
  return COOL
end

-- Foe panels: no background, a marker in front of each group -------------------------------------------------------

local PANEL_ROW_H = 10
local GROUP_GAP = 2
local MARKER_W = 7

local function add_group(tokens, label, list, gap)
  if not list or #list == 0 then return end
  tokens[#tokens + 1] = marker_token(label, #tokens > 0 and gap or 0)
  add_types(tokens, list)
end

--- x4 and x2 as separate groups
local function weak_tokens(groups)
  local tokens = {}
  add_group(tokens, "x4", groups[4], GROUP_GAP)
  add_group(tokens, "x2", groups[2], GROUP_GAP)
  return tokens
end

--- x.25 (with the rare x1/8 under the same marker) and x.5 as separate groups
local function resist_tokens(groups)
  local tokens = {}
  local quarter = {}
  for _, m in ipairs({ .125, .25 }) do
    for _, t in ipairs(groups[m] or {}) do quarter[#quarter + 1] = t end
  end
  add_group(tokens, "x.25", quarter, GROUP_GAP)
  add_group(tokens, "x.5", groups[.5], GROUP_GAP)
  return tokens
end

local function number_token(number)
  local token = words(tostring(number), WHITE)
  token.outline = true
  return token
end

--- The foe's types on its HP bar, ending at spot.right
local function draw_bar_types(foe, spot)
  local tokens = {}
  add_types(tokens, foe.types)
  local row = { placed = {}, lines = 1 }
  -- Only text chips can be too wide for the room left of "HP". Those start at the screen's edge instead.
  if not place_right(row, tokens, spot.right, 0) then
    row = lay_out(tokens, 0, 0, 256, 1, true)
  end
  draw_row(TOP, row, spot.y, PANEL_ROW_H)
end

local function has_state(mon)
  return #mon.status > 0 or #mon.stages > 0
end

--- Status, then stat changes. The first of them goes after a gap of first_gap.
local function add_state(tokens, mon, first_gap)
  add_status(tokens, mon, first_gap)
  add_stages(tokens, mon, #mon.status > 0 and GROUP_GAP or first_gap, true)
end

local function singles_foe_state(layout, foe)
  local box = layout.foe_box
  local x = box[1]
  local tokens = {}
  add_state(tokens, foe, 0)
  draw_row(TOP, lay_out(tokens, x, x, x + layout.foe_panel_w, 1, true), box[2] + box[4] + 1, PANEL_ROW_H)
end

--- row is which row this is, as only a foe with something to show gets one
local function doubles_foe_state(layout, foe, number, row)
  local box = layout.lower_foe_box
  local x = layout.rows_x
  local tokens = { number_token(number) }
  add_state(tokens, foe, layout.rows_icons_x - x - tokens[1].w - 1)
  draw_row(TOP, lay_out(tokens, x, x, layout.rows_limit, 1, true), box[2] + box[4] + 1 + (row - 1) * PANEL_ROW_H, PANEL_ROW_H)
end

--- Draws the token lists that aren't empty on the bottom screen, one under the other from y
local function draw_matchup_rows(lists, spot, y)
  for _, tokens in ipairs(lists) do
    if #tokens > 0 then
      local row = lay_out(tokens, spot.x, spot.x + MARKER_W, spot.limit, spot.lines, true)
      draw_row(BOTTOM, row, y, PANEL_ROW_H)
      y = y + row.lines * PANEL_ROW_H
    end
  end
end

local function singles_matchups(spot, groups)
  local immune = {}
  add_group(immune, "x0", groups[0], 0)
  draw_matchup_rows({ weak_tokens(groups), resist_tokens(groups), immune }, spot, spot.y)
end

--- Weak, then resist and immune together, next to the foe's number
local function doubles_matchups(spot, number, groups)
  local weak, others = weak_tokens(groups), resist_tokens(groups)
  add_group(others, "x0", groups[0], GROUP_GAP)
  if #weak == 0 and #others == 0 then return end
  local y = spot.ys[number]
  draw_token(BOTTOM, number_token(number), spot.number_x, y)
  draw_matchup_rows({ weak, others }, spot, y)
end

-- Card ----------------------------------------------------------------------------------------------------------

local CARD_X, CARD_Y, CARD_W, CARD_MAX_H = 3, 3, 250, 186
local CARD_LEFT, CARD_RIGHT, CARD_COLUMN, CARD_NAME_COLUMN = 8, 248, 46, 30
local CARD_PAD_TOP, CARD_PAD_BOTTOM = 3, 2

local function card_list_row(title, color, groups_in_order)
  local tokens = { words(title, color) }
  local any = false
  for _, entry in ipairs(groups_in_order) do
    if entry.list and #entry.list > 0 then
      any = true
      if entry.label then
        tokens[#tokens + 1] = words(entry.label, entry.color, GROUP_GAP * 2, true)
      end
      add_types(tokens, entry.list, entry.label and 0 or GROUP_GAP * 2)
    end
  end
  if not any then
    tokens[#tokens + 1] = words("none", OFF, 1)
  end
  to_column(tokens, CARD_COLUMN - CARD_LEFT)
  local row = lay_out(tokens, CARD_LEFT, CARD_COLUMN, CARD_RIGHT, 2)
  row.empty = not any
  return row
end

local function section_rows(mon, side, is_foe, groups, show_hidden)
  local rows = {}
  local fainted = mon.hp == 0

  -- A fainted foe gets no name: fainting clears the Illusion bit, which would show the real species
  if fainted and is_foe then
    local tokens = { words(side, DIM), words("FNT", OFF, 2) }
    for _, token in ipairs(tokens) do token.dy = 0 end
    rows[1] = lay_out(tokens, CARD_LEFT, CARD_LEFT, CARD_RIGHT, 1)
    return rows
  end

  local name = species_names[mon.species] or ("#" .. mon.species)
  local head = { words(side, DIM), words(name .. " Lv" .. mon.level, WHITE) }
  if not is_foe or show_hidden then
    head[#head + 1] = words("HP " .. mon.hp .. "/" .. mon.max_hp, DIM, 5)
  end
  for _, token in ipairs(head) do token.dy = 0 end
  to_column(head, CARD_NAME_COLUMN - CARD_LEFT)
  head[2].gap = math.max(head[2].gap, 2) -- "FOE 1" is wider than the column, so this keeps a space after it
  local head_row = lay_out(head, CARD_LEFT, CARD_LEFT, CARD_RIGHT, 1)
  local badges = {}
  if fainted then
    badges[1] = chip_token("FNT", STATUS_COLOR.FNT, WHITE)
  else
    add_status(badges, mon, 1, -1)
  end
  if #badges > 0 then
    place_right(head_row, badges, CARD_RIGHT, head_row.right_end)
  end
  rows[1] = head_row
  if fainted then return rows end

  local identity = {}
  add_types(identity, mon.types)
  local ability = (not is_foe or show_hidden) and mon.ability and ability_names[mon.ability] or nil
  if ability then
    identity[#identity + 1] = words(ability, DIM, 4)
  end
  local id_row = lay_out(identity, CARD_LEFT, CARD_LEFT, CARD_RIGHT, 1)
  rows[#rows + 1] = id_row
  local stages = {}
  add_stages(stages, mon, 0)
  if #stages > 0 and not place_right(id_row, stages, CARD_RIGHT, id_row.right_end + 3) then
    local row = { placed = {}, lines = 1 }
    if place_right(row, stages, CARD_RIGHT, CARD_LEFT) then
      rows[#rows + 1] = row
    end
  end

  rows[#rows + 1] = card_list_row("WEAK", WARM, {
    { list = groups[4], label = "x4", color = HOT },
    { list = groups[2], label = "x2", color = WARM },
  })
  rows[#rows + 1] = card_list_row("RESIST", COOL, {
    { list = groups[.125], label = matchup.label(.125), color = COOL },
    { list = groups[.25], label = "x.25", color = COOL },
    { list = groups[.5], label = "x.5", color = COOL },
  })
  rows[#rows + 1] = card_list_row("IMMUNE", OFF, { { list = groups[0] } })
  rows[#rows + 1] = card_list_row("STRONG", GREEN, { { list = typechart.strong_against(mon.types) } })
  return rows
end

local function section_lines(rows)
  local lines = 0
  for _, row in ipairs(rows) do lines = lines + row.lines end
  return lines
end

local function card_height(sections, row_h, gap)
  local h = CARD_PAD_TOP + CARD_PAD_BOTTOM - (row_h - ICON_H)
  for i, rows in ipairs(sections) do
    h = h + section_lines(rows) * row_h
    if i > 1 then h = h + gap end
  end
  return h
end

--- Picks a row height that fits; when even the tightest doesn't, leaves out empty rows and then whole sections from the end
local function fit_card(sections)
  local two_or_less = #sections <= 2
  local gap = two_or_less and 4 or 3
  for _, row_h in ipairs(two_or_less and { 10, ICON_H } or { ICON_H }) do
    if card_height(sections, row_h, gap) <= CARD_MAX_H then
      return row_h, gap
    end
  end
  for _, rows in ipairs(sections) do
    for i = #rows, 1, -1 do
      if rows[i].empty then table.remove(rows, i) end
    end
  end
  while #sections > 1 and card_height(sections, ICON_H, gap) > CARD_MAX_H do
    sections[#sections] = nil
  end
  return ICON_H, gap
end

-- The whole thing ------------------------------------------------------------------------------------------------

--- ui for one game; game is its line in games.lua
function M.new(game, reader)
  local self = {}
  local moves = require(game.generation.moves)
  local type_ids = game.generation.type_ids
  local layout = game.layout
  load_icons()

  -- What foe_boxes said the last two frames. A change to a box reaches the OAM the next frame and the picture the frame after that, so
  -- drawing what it said two frames ago keeps the numbers with the box, instead of floating there before it appears.
  local boxes_1_ago, boxes_2_ago

  --- Forgets the boxes of a battle that's gone or a state that's been replaced
  function self.reset()
    boxes_1_ago, boxes_2_ago = nil, nil
  end

  --- Whether what's known now rules out a box that's waiting: its foe has a disguise that can't be read, or it was read as its real self before
  --- the disguise went on, so it would show real numbers
  local function ruled_out(boxes, waiting)
    for i = 1, #boxes do
      local now = boxes[i]
      if now.slot == waiting.slot then
        return now.unreadable or (now.disguised and not waiting.disguised)
      end
    end
    return false
  end

  --- The foe's HP as numbers on each box in the list, from the reader's foe_boxes, the way your own box shows yours. Call it every frame.
  function self.foe_hp(boxes)
    local spot = layout.foe_hp
    local showing = boxes_2_ago
    boxes_2_ago, boxes_1_ago = boxes_1_ago, boxes
    if not showing then return end
    for i = 1, #showing do
      local box = showing[i]
      if not box.hidden and not ruled_out(boxes, box) then
        local text = box.hp .. "/" .. box.max_hp
        draw_text(TOP, { text = text, fg = WHITE, outline = true }, box.x + spot.right - text_width(text), box.y + spot.below)
      end
    end
  end

  --- screen says where the bottom screen's rows go: the action menu has less room than the move menu and target select
  function self.panels(battle, show_hidden, screen)
    local attacker = acting_mon(battle)
    local menu = screen == ACTION_SCREEN and "action" or "moves"
    if battle.rule == 0 and not battle.multi then
      local foe = battle.foes[1]
      if is_alive(foe) then
        draw_bar_types(foe, layout.foe_bar_types)
        if has_state(foe) then singles_foe_state(layout, foe) end
        singles_matchups(layout.singles_matchups[menu], foe_groups(foe, attacker, show_hidden))
      end
    else
      local state_rows = 0
      for i = 1, math.min(2, #battle.foes) do
        local foe = battle.foes[i]
        if is_alive(foe) then
          draw_bar_types(foe, layout.doubles_bar_types[i])
          if has_state(foe) then
            state_rows = state_rows + 1
            doubles_foe_state(layout, foe, i, state_rows)
          end
          doubles_matchups(layout.doubles_matchups[menu], i, foe_groups(foe, attacker, show_hidden))
        end
      end
    end
  end

  local function pill(x, y, text, color)
    local w = text_width(text) + 2
    melon.draw.rect(BOTTOM, x, y, w, CHIP_H, color, true)
    melon.draw.text(BOTTOM, x + 1, y + .5, text, DARK)
    return w
  end

  --- The tag for one move on one foe: its multiplier, "?" or nil
  local function tag_for(move, foe, attacker, show_hidden)
    if not is_alive(foe) then return nil end
    return matchup.move_tag(move, foe, foe_ability(foe, show_hidden), attacker)
  end

  local function tag_text(tag)
    if tag == "?" then return "?" end
    return matchup.label(tag)
  end

  local function tag_color(tag)
    if tag == "?" then return NEUTRAL end
    return effect_color(tag)
  end

  function self.move_tags(battle, show_hidden)
    local attacker = battle.choosing
    if not attacker then return end
    local ids = reader.moves(attacker)
    local doubles = battle.rule ~= 0 or battle.multi
    local buttons = layout.move_buttons
    local pill_y, pill_right, pill_left = layout.pill_y, layout.pill_right, layout.pill_left
    local strip = layout.strip
    for i = 1, #buttons do
      local id = ids[i]
      if id and id >= 1 and moves.type[id] then
        local move = { id = id, type = type_ids[moves.type[id]], category = moves.category[id], sound = moves.sound[id] }
        local bx, by = buttons[i][1], buttons[i][2]
        if doubles then
          local tag = tag_for(move, battle.foes[1], attacker, show_hidden)
          if tag ~= nil then
            pill(bx + pill_left, by + pill_y, "1" .. tag_text(tag), tag_color(tag))
          end
          tag = tag_for(move, battle.foes[2], attacker, show_hidden)
          if tag ~= nil then
            local text = "2" .. tag_text(tag)
            pill(bx + pill_right - text_width(text) - 2, by + pill_y, text, tag_color(tag))
          end
        else
          local tag = tag_for(move, battle.foes[1], attacker, show_hidden)
          if tag ~= nil then
            local text = tag_text(tag)
            pill(bx + pill_right - text_width(text) - 2, by + pill_y, text, tag_color(tag))
            if tag ~= "?" then
              melon.draw.rect(BOTTOM, bx + strip[1], by + strip[2], strip[3], strip[4], tag_color(tag), true)
            end
          end
        end
      end
    end
  end

  function self.card(battle, show_hidden)
    local attacker = acting_mon(battle)
    local sections = {}
    local foe_count, mine_count = #battle.foes, #battle.mine
    for i = 1, foe_count do
      local foe = battle.foes[i]
      if foe and not foe.no_info then
        local side = foe_count > 1 and ("FOE " .. i) or "FOE"
        sections[#sections + 1] = section_rows(foe, side, true, foe_groups(foe, attacker, show_hidden), show_hidden)
      end
    end
    if battle.rule >= 2 then
      -- Triples and rotation: your active mons, as far as they fit
      for i = 1, mine_count do
        local mon = battle.mine[i]
        if mon then
          sections[#sections + 1] = section_rows(mon, "YOU " .. i, false, my_groups(mon, battle, show_hidden), show_hidden)
        end
      end
    elseif attacker then
      sections[#sections + 1] = section_rows(attacker, "YOU", false, my_groups(attacker, battle, show_hidden), show_hidden)
    end
    if #sections == 0 then return end

    local row_h, gap = fit_card(sections)
    local h = card_height(sections, row_h, gap)
    melon.draw.rect(TOP, CARD_X, CARD_Y, CARD_W, h, CARD, true)
    melon.draw.rect(TOP, CARD_X, CARD_Y, CARD_W, h, CARD_LINE, false)
    local y = CARD_Y + CARD_PAD_TOP
    for i, rows in ipairs(sections) do
      if i > 1 then
        melon.draw.rect(TOP, CARD_LEFT, y + 1 - (row_h - ICON_H), CARD_RIGHT - CARD_LEFT, .5, CARD_LINE, true)
        y = y + gap
      end
      for _, row in ipairs(rows) do
        draw_row(TOP, row, y, row_h)
        y = y + row.lines * row_h
      end
    end
  end

  return self
end

return M
