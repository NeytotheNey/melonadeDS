-- Which games the mod knows. To add one: a reader for its generation (pbi/gen5.lua is the example) and a line in GAMES, with its
-- own screen layout. Some things are still Gen-5-specific and will need moving when Gen 4 is added: the screen numbers and rule values in
-- battle_info.lua's on_frame_end and ui.lua's ACTION_SCREEN, the species and ability names ui.lua loads, and the ability and move IDs in
-- matchup.lua.
local typechart = require("pbi.typechart")

local GEN5 = {
  reader = "pbi.gen5",
  type_ids = typechart.GEN5_IDS,
  moves = "pbi.data_moves",
  personal = "pbi.data_personal",
}

-- Where things are on the screens, in DS pixels. Rects are x, y, w, h.
local B2W2_LAYOUT = {
  -- Singles: the foe's status and stat changes go under its HP box, below the HP numbers
  foe_box = { 4, 30, 106, 24 },
  foe_panel_w = 118,
  -- Doubles: the same go under the lower foe box and its HP numbers, one row per foe that has any, the first icon after the 1 or 2 at icons_x
  lower_foe_box = { 6, 41, 104, 25 },
  rows_x = 1,
  rows_icons_x = 7,
  rows_limit = 140,
  -- The foe's HP numbers, from its name box's center: where they end, at the bar's end like your box's, and their top, just under the bar
  foe_hp = { right = 32, below = 6 },
  -- The foe's type icons on the grey part of its HP bar: where the last icon ends, just left of "HP", and the icons' top
  foe_bar_types = { right = 26.5, y = 40.5 },
  -- By foe number. foes[1] is slot 0, the upper HP box, and foes[2] the lower: docs/pokemon-battle-ram.md, "Verified in game".
  doubles_bar_types = { { right = 30.5, y = 29 }, { right = 26.5, y = 53 } },
  -- Weak, resist and immune on the bottom screen. The action menu's BAG button takes the lower left, so there they go above it in a
  -- narrow column and wrap; "moves" is the move menu and target select. Rows start at x, and limit is where they have to end.
  singles_matchups = {
    action = { x = 3, y = 88, limit = 79, lines = 2 },
    moves = { x = 3, y = 146, limit = 176, lines = 1 },
  },
  -- The same in doubles, ys by foe number, the 1 or 2 at number_x
  doubles_matchups = {
    action = { number_x = 3, x = 10, ys = { 40, 88 }, limit = 79, lines = 2 },
    moves = { number_x = 3, x = 10, ys = { 146, 168 }, limit = 176, lines = 1 },
  },
  -- The move buttons on the bottom screen, top-left corners
  move_buttons = { { 2, 33 }, { 130, 33 }, { 2, 81 }, { 130, 81 } },
  -- Inside a button: pills in the band above the name, so a 12-letter name stays readable next to "2x.25". The singles pill and
  -- the doubles right pill end at pill_right, the doubles left pill starts at pill_left.
  pill_y = 4,
  pill_right = 106,
  pill_left = 14,
  strip = { 14, 40, 96, 2 },
  -- Where damage numbers start on the top screen, the middle of the number's top edge, near each mon at the usual camera angle. The camera
  -- pans, so these are only close. In the order of the reader's foes and mine: in doubles the upper HP box's foe stands on the right, and your
  -- first mon on the left. Multi battles use the doubles ones.
  damage_numbers = {
    singles = { foes = { { 186, 62 } }, mine = { { 72, 108 } } },
    doubles = { foes = { { 216, 50 }, { 160, 50 } }, mine = { { 38, 100 }, { 112, 128 } } },
  },
}

local GAMES = {
  -- docs/pokemon-battle-ram.md, "Verified in game"
  IRDO = { name = "White 2", generation = GEN5, base_pointer = 0x02000024, party_table = 0x526A8, effect_pointer = 0x021F4280, layout = B2W2_LAYOUT },
  -- Black 2 is untested. The doc says it has the same layout.
  IREO = { name = "Black 2", generation = GEN5, base_pointer = 0x02000024, party_table = 0x526A8, effect_pointer = 0x021F4240, layout = B2W2_LAYOUT },
}

return GAMES
