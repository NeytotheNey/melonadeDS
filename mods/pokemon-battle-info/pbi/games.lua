-- Which games the mod knows. To add one: a reader for its generation (pbi/gen5.lua is the example) and a line in GAMES, with its
-- own screen layout. Some things are still Gen-5-specific and will need moving when Gen 4 is added: the screen numbers and rule values in
-- battle_info.lua's on_frame_end, the species and ability names ui.lua loads, and the ability and move IDs in matchup.lua.
local typechart = require("pbi.typechart")

local GEN5 = {
  reader = "pbi.gen5",
  type_ids = typechart.GEN5_IDS,
  moves = "pbi.data_moves",
  personal = "pbi.data_personal",
}

-- Where things are on the screens, in DS pixels. The panels hang off the HP boxes. Rects are x, y, w, h.
local B2W2_LAYOUT = {
  foe_box = { 4, 30, 106, 22 },
  my_box = { 142, 112, 114, 28 },
  foe_panel_w = 118,
  -- The doubles rows go under the lower foe box, the first icon after the 1 or 2 at icons_x
  lower_foe_box = { 6, 41, 104, 22 },
  rows_x = 1,
  rows_icons_x = 7,
  rows_limit = 140,
  -- The move buttons on the bottom screen, top-left corners
  move_buttons = { { 2, 33 }, { 130, 33 }, { 2, 81 }, { 130, 81 } },
  -- Inside a button: pills in the band above the name, so a 12-letter name stays readable next to "2x.25". The singles pill and
  -- the doubles right pill end at pill_right, the doubles left pill starts at pill_left.
  pill_y = 4,
  pill_right = 106,
  pill_left = 14,
  strip = { 14, 40, 96, 2 },
}

local GAMES = {
  -- docs/pokemon-battle-ram.md, "Verified in game"
  IRDO = { name = "White 2", generation = GEN5, base_pointer = 0x02000024, party_table = 0x526A8, layout = B2W2_LAYOUT },
  -- Black 2 is untested. The doc says it has the same layout.
  IREO = { name = "Black 2", generation = GEN5, base_pointer = 0x02000024, party_table = 0x526A8, layout = B2W2_LAYOUT },
}

return GAMES
