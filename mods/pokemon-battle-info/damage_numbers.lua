-- description: Shows damage and healing as floating numbers during Pokemon White 2 battles: white for damage, yellow for critical hits, green for healing. Reads only.
-- option: text_size type=number min=1 max=3 step=1 default=2 label="Number size"

local games = require("pbi.games")

local reader, numbers

function on_load(game_code)
  local game = games[game_code]
  if not game then
    melon.log("damage_numbers: nothing to do for game " .. tostring(game_code))
    return
  end
  local options = melon.options or {}
  reader = require(game.generation.reader).new(game)
  numbers = require("pbi.damage").new(game, reader, options.text_size or 2)
end

function on_state_loaded()
  if numbers then numbers.reset() end
end

function on_frame_end()
  if numbers then numbers.frame() end
end
