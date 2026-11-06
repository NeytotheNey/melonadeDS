-- description: Shows types, weaknesses and move effectiveness during Pokemon White 2 battles. Reads only.
-- option: show_tags type=toggle default=true label="Show foe info by its HP box and on the bottom screen"
-- option: show_move_tags type=toggle default=true label="Show effectiveness on the move buttons"
-- option: card_button type=choice values=L,R,X,Y,SELECT default=L label="Hold this button for the full card"
-- option: show_foe_hidden type=toggle default=true label="Show foe's ability and exact HP"

local games = require("pbi.games")

local reader, ui
local armed = false -- nothing shows until the first menu of a battle
local card_mask = 0
local show_tags, show_move_tags, show_foe_hidden = true, true, true

function on_load(game_code)
  local game = games[game_code]
  if not game then
    melon.log("battle_info: nothing to do for game " .. tostring(game_code))
    return
  end
  local options = melon.options or {}
  show_tags = options.show_tags ~= false
  show_move_tags = options.show_move_tags ~= false
  show_foe_hidden = options.show_foe_hidden ~= false
  card_mask = melon.KEY[options.card_button] or melon.KEY.L
  reader = require(game.generation.reader).new(game)
  ui = require("pbi.ui").new(game, reader)
end

local function reset()
  armed = false
  if reader then reader.reset() end
end

function on_state_loaded()
  reset()
end

function on_frame_end()
  if not reader then return end
  local ctx = reader.chain()
  if not ctx then
    reset()
    return
  end
  local screen = ctx.screen
  -- The intro is all screen 0. Any menu arms it, so a save state on the move screen shows tags right away.
  if screen >= 1 and screen <= 4 then armed = true end
  if not armed then return end

  -- Triples and rotation have no room for these, and weren't tested. The card still works.
  local small_rule = ctx.rule < 2
  local want_card = melon.input.held() & card_mask ~= 0
  local want_panels = show_tags and small_rule and not want_card and screen >= 1 and screen <= 3
  local want_tags = show_move_tags and small_rule and screen == 2 and ctx.busy == 0 and ctx.pressed == -1
  if not (want_card or want_panels or want_tags) then return end

  local battle = reader.battle(ctx)
  if not battle then return end
  if want_panels then ui.panels(battle, show_foe_hidden, screen) end
  if want_tags then ui.move_tags(battle, show_foe_hidden) end
  if want_card then ui.card(battle, show_foe_hidden) end
end
