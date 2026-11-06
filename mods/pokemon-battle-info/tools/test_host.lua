-- Host-side test, needs a plain Lua 5.4 with the debug library: lua test_host.lua <path to mods/pokemon-battle-info>
-- It runs the real modules against a fake melon table and a fake battle in memory. Nothing here runs in the app.
local root = arg[1] or ".."
package.path = root .. "/?.lua;" .. package.path

local failures = 0
local function check(name, got, want)
  if got ~= want then
    failures = failures + 1
    print("FAIL " .. name .. ": got " .. tostring(got) .. ", want " .. tostring(want))
  end
end

-- Fake melon ---------------------------------------------------------------------------------------------------

local mem = {}
local forbidden_reads = 0
local function r8(a)
  if a >> 24 == 4 then
    forbidden_reads = forbidden_reads + 1
    error("reads in 0x04xxxxxx aren't allowed", 2)
  end
  return mem[a] or 0
end
local function r16(a)
  return r8(a) | (r8(a + 1) << 8)
end
local function r32(a)
  return r16(a) | (r16(a + 2) << 16)
end
local function w8(a, v)
  mem[a] = v & 0xFF
end
local function w16(a, v)
  w8(a, v)
  w8(a + 1, v >> 8)
end
local function w32(a, v)
  w16(a, v)
  w16(a + 2, v >> 16)
end

local commands = {}
local function record(kind)
  return function(screen, ...)
    commands[#commands + 1] = { kind = kind, screen = screen, args = { ... } }
    return 0
  end
end

local have_sheet = true
local logged = {}
melon = {
  mem = {
    read8 = r8, read16 = r16, read32 = r32,
    read_s32 = function(a)
      local v = r32(a)
      return v >= 0x80000000 and v - 0x100000000 or v
    end,
  },
  draw = { rect = record("rect"), text = record("text"), image = record("image") },
  image = {
    load = function(path)
      if have_sheet and path == "img/icons.png" then return "the sheet" end
      return nil, "not found"
    end,
  },
  input = { held = function() return 0 end },
  KEY = { L = 512, R = 256, X = 1024, Y = 2048, SELECT = 4 },
  options = {},
  log = function(text) logged[#logged + 1] = text end,
}
local held = 0
melon.input.held = function() return held end

-- A fake battle --------------------------------------------------------------------------------------------------

local BASE = 0x02204D04
local P = BASE + 0x526A8
local MAIN, CORE, SCD, INPUT = 0x022572E0, 0x02284538, 0x02293144, 0x022BAF54
local RECS = { me1 = 0x0225B224, me2 = 0x0225B448, foe1 = 0x0225BEFC, foe2 = 0x0225C120 }

local function set_chain()
  w32(0x02000024, BASE)
  w32(P - 4, MAIN)
  w32(MAIN + 4, CORE)
  w32(CORE, MAIN)
  w32(CORE + 0x1C4, SCD)
  w32(SCD + 0xAC, MAIN)
  w32(SCD + 0xA8, CORE)
  w32(SCD + 0xBC, INPUT)
  w8(P + 0x18, 6)
  w8(P + 0x1C + 0x18, 6)
  w32(INPUT + 0x58, 1)
  w32(INPUT + 0x70, 0xFFFFFFFF)
end

local GEN5 = { NOR = 0, FIG = 1, FLY = 2, POI = 3, GRO = 4, ROC = 5, BUG = 6, GHO = 7, STE = 8, FIR = 9, WAT = 10, GRA = 11, ELE = 12, PSY = 13, ICE = 14, DRA = 15, DAR = 16 }

local function set_mon(rec, id, species, t1, t2, ability, moves)
  w16(rec + 0x0C, species)
  w16(rec + 0x0E, 148)
  w16(rec + 0x10, 89)
  w8(rec + 0x18, 42)
  w8(rec + 0x19, id)
  w8(rec + 0xF8, GEN5[t1])
  w8(rec + 0xF9, GEN5[t2 or t1])
  w16(rec + 0x13C, ability)
  for i = 0, 6 do w8(rec + 0xFC + i, 6) end
  for i = 1, 4 do w16(rec + 0x104 + (i - 1) * 0x0E + 6, moves and moves[i] or 0) end
end

local function set_condition(rec, id, word)
  w32(rec + 0x1C + id * 4, word)
end

-- Modules --------------------------------------------------------------------------------------------------------

local typechart = require("pbi.typechart")
local matchup = require("pbi.matchup")
local games = require("pbi.games")
local data_moves = require("pbi.data_moves")
local personal = require("pbi.data_personal")
local species = require("pbi.data_species")
local abilities = require("pbi.data_abilities")
local game = games.IRDO
local reader = require("pbi.gen5").new(game)
local ui = require("pbi.ui").new(game, reader)

local function T(types)
  return { types = types }
end

-- The chart: every cell ----------------------------------------------------------------------------------------------

-- A copy written out separately: rows are attacks and columns defenders, both in the game's ID order (Normal, Fighting, Flying, Poison,
-- Ground, Rock, Bug, Ghost, Steel, Fire, Water, Grass, Electric, Psychic, Ice, Dragon, Dark). 0 no effect, h half, . normal, 2 double.
local INDEPENDENT_CHART = {
  ".....h.0h........", -- Normal
  "2.hh.2h02....h2.2", -- Fighting
  ".2...h2.h..2h....", -- Flying
  "...hhh.h0..2.....", -- Poison
  "..02.2h.22.h2....", -- Ground
  ".h2.h.2.h2....2..", -- Rock
  ".hhh...hhh.2.2..2", -- Bug
  "0......2h....2..h", -- Ghost
  ".....2..hhh.h.2..", -- Steel
  ".....h2.2hh2..2h.", -- Fire
  "....22...2hh...h.", -- Water
  "..hh22h.hh2h...h.", -- Grass
  "..2.0.....2hh..h.", -- Electric
  ".2.2....h....h..0", -- Psychic
  "..2.2...hhh2..h2.", -- Ice
  "........h......2.", -- Dragon
  ".h.....2h....2..h", -- Dark
}
local CELL = { ["0"] = 0, h = .5, ["."] = 1, ["2"] = 2 }
local cells = 0
for attack = 0, 16 do
  local row = INDEPENDENT_CHART[attack + 1]
  for defend = 0, 16 do
    local a, d = typechart.GEN5_IDS[attack], typechart.GEN5_IDS[defend]
    local want = CELL[row:sub(defend + 1, defend + 1)]
    check("chart " .. a .. " on " .. d, typechart.effect(a, { d }), want)
    check("cell " .. a .. " on " .. d, typechart.cell(a, d), want)
    cells = cells + 1
  end
end
check("chart cells checked", cells, 289)
check("typeless id", typechart.GEN5_IDS[17], nil)

check("electric on water/flying", typechart.effect("ELE", { "WAT", "FLY" }), 4)
check("ground on water/flying", typechart.effect("GRO", { "WAT", "FLY" }), 0)
check("grass on fire/flying", typechart.effect("GRA", { "FIR", "FLY" }), .25)
check("no types", typechart.effect("FIR", {}), 1)
check("strong fire/fighting", table.concat(typechart.strong_against({ "FIR", "FIG" }), ","), "NOR,GRA,ICE,BUG,ROC,DAR,STE")

-- Abilities -------------------------------------------------------------------------------------------------------

local LEVITATE, VOLT, LROD, MOTOR, WABS, DRAIN, DRY, FLASH, SAP, WG, FAT, HEAT, SOLID, FILTER = 26, 10, 31, 78, 11, 114, 87, 18, 157, 25, 47, 85, 116, 111
local MOLD, TERA, TURBO, SOUNDPROOF, NORMALIZE, TINTED, SCRAPPY = 104, 164, 163, 43, 96, 110, 113
for id, name in pairs({
  [LEVITATE] = "Levitate", [VOLT] = "Volt Absorb", [LROD] = "Lightning Rod", [MOTOR] = "Motor Drive", [WABS] = "Water Absorb",
  [DRAIN] = "Storm Drain", [DRY] = "Dry Skin", [FLASH] = "Flash Fire", [SAP] = "Sap Sipper", [WG] = "Wonder Guard", [FAT] = "Thick Fat",
  [HEAT] = "Heatproof", [SOLID] = "Solid Rock", [FILTER] = "Filter", [MOLD] = "Mold Breaker", [TERA] = "Teravolt", [TURBO] = "Turboblaze",
  [SOUNDPROOF] = "Soundproof", [NORMALIZE] = "Normalize", [TINTED] = "Tinted Lens", [SCRAPPY] = "Scrappy",
}) do
  check("ability " .. id, abilities[id], name)
end

check("levitate ground", matchup.effect("GRO", T{ "ELE" }, LEVITATE, 0), 0)
check("levitate fire", matchup.effect("FIR", T{ "ELE" }, LEVITATE, 0), 1)
check("levitate vs mold breaker", matchup.effect("GRO", T{ "ELE" }, LEVITATE, MOLD), 2)
check("levitate vs teravolt", matchup.effect("GRO", T{ "ELE" }, LEVITATE, TERA), 2)
check("levitate vs turboblaze", matchup.effect("GRO", T{ "ELE" }, LEVITATE, TURBO), 2)
check("levitate hidden", matchup.effect("GRO", T{ "ELE" }, nil, 0), 2)
check("volt absorb", matchup.effect("ELE", T{ "WAT" }, VOLT, 0), 0)
check("lightning rod", matchup.effect("ELE", T{ "WAT" }, LROD, 0), 0)
check("motor drive", matchup.effect("ELE", T{ "WAT" }, MOTOR, 0), 0)
check("water absorb", matchup.effect("WAT", T{ "FIR" }, WABS, 0), 0)
check("storm drain", matchup.effect("WAT", T{ "FIR" }, DRAIN, 0), 0)
check("dry skin water", matchup.effect("WAT", T{ "FIR" }, DRY, 0), 0)
check("dry skin fire is no change", matchup.effect("FIR", T{ "FIR" }, DRY, 0), .5)
check("flash fire", matchup.effect("FIR", T{ "GRA" }, FLASH, 0), 0)
check("sap sipper", matchup.effect("GRA", T{ "WAT" }, SAP, 0), 0)
check("wonder guard neutral", matchup.effect("NOR", T{ "BUG", "GHO" }, WG, 0), 0)
check("wonder guard super", matchup.effect("FIR", T{ "BUG", "GHO" }, WG, 0), 2)
check("wonder guard vs mold breaker", matchup.effect("WAT", T{ "BUG", "GHO" }, WG, MOLD), 1)
check("thick fat fire", matchup.effect("FIR", T{ "NOR" }, FAT, 0), .5)
check("thick fat ice x2", matchup.effect("ICE", T{ "GRO" }, FAT, 0), 1)
check("thick fat water", matchup.effect("WAT", T{ "GRO" }, FAT, 0), 2)
check("thick fat x.25", matchup.effect("FIR", T{ "WAT", "ROC" }, FAT, 0), .125)
check("heatproof fire", matchup.effect("FIR", T{ "STE" }, HEAT, 0), 1)
check("heatproof ice", matchup.effect("ICE", T{ "GRO" }, HEAT, 0), 2)
check("solid rock is no change", matchup.effect("WAT", T{ "FIR" }, SOLID, 0), 2)
check("filter is no change", matchup.effect("WAT", T{ "FIR" }, FILTER, 0), 2)

local g = matchup.groups(T{ "WAT", "FLY" })
check("groups x4", table.concat(g[4], ","), "ELE")
check("groups x2", table.concat(g[2], ","), "ROC")
check("groups x0", table.concat(g[0], ","), "GRO")
check("groups x.5", table.concat(g[.5], ","), "FIR,WAT,FIG,BUG,STE")
local gl = matchup.groups(T{ "ELE" }, LEVITATE, 0)
check("levitate row", table.concat(gl[0], ","), "GRO")
check("levitate not weak", gl[2], nil)
local gw = matchup.groups(T{ "BUG", "GHO" }, WG, 0)
check("wonder guard x2 row", table.concat(gw[2], ","), "FIR,FLY,ROC,GHO,DAR")
check("wonder guard x0 count", #gw[0], 12)

-- Conditions on the defender (Condition_CheckFloating and Condition_CheckTypeImmunity in the decomp) -------------------------------

local function D(types, fields)
  local d = { types = types }
  for k, v in pairs(fields) do d[k] = v end
  return d
end
local GROUNDED, LIFTED = { grounded = true }, { lifted = true }
check("ingrain on flying", matchup.effect("GRO", D({ "FLY" }, GROUNDED)), 1)
check("smack down on steel/flying is x2", matchup.effect("GRO", D({ "STE", "FLY" }, GROUNDED)), 2)
check("smack down on water/flying is x1", matchup.effect("GRO", D({ "WAT", "FLY" }, GROUNDED)), 1)
check("grounded levitate is hit", matchup.effect("GRO", D({ "ELE" }, GROUNDED), LEVITATE), 2)
check("grounded beats magnet rise", matchup.effect("GRO", D({ "ELE" }, { grounded = true, lifted = true })), 2)
check("magnet rise", matchup.effect("GRO", D({ "ELE" }, LIFTED)), 0)
check("magnet rise on fire", matchup.effect("FIR", D({ "ELE" }, LIFTED)), 1)
check("magnet rise vs mold breaker stays", matchup.effect("GRO", D({ "ELE" }, LIFTED), nil, MOLD), 0)
check("telekinesis on flying", matchup.effect("GRO", D({ "FLY" }, LIFTED)), 0)
check("flying stays immune without a condition", matchup.effect("GRO", T{ "FLY" }), 0)
check("grounded fire move", matchup.effect("FIR", D({ "FLY" }, GROUNDED)), 1)
check("foresight normal on ghost", matchup.effect("NOR", D({ "GHO" }, { seen_type = "GHO" })), 1)
check("foresight fighting on ghost", matchup.effect("FIG", D({ "GHO" }, { seen_type = "GHO" })), 1)
check("foresight fighting on ghost/normal", matchup.effect("FIG", D({ "GHO", "NOR" }, { seen_type = "GHO" })), 2)
check("foresight doesn't touch dark", matchup.effect("PSY", D({ "DAR" }, { seen_type = "GHO" })), 0)
check("miracle eye", matchup.effect("PSY", D({ "DAR" }, { seen_type = "DAR" })), 1)
check("miracle eye on poison/dark", matchup.effect("PSY", D({ "POI", "DAR" }, { seen_type = "DAR" })), 2)
check("miracle eye doesn't touch ghost", matchup.effect("NOR", D({ "GHO" }, { seen_type = "DAR" })), 0)
local gf = matchup.groups(D({ "NOR", "FLY" }, GROUNDED))
check("smack down row has no immunity to ground", table.concat(gf[0], ","), "GHO")

-- Your abilities as the attacker ------------------------------------------------------------------------------------------------

check("scrappy normal on ghost", matchup.effect("NOR", T{ "GHO" }, nil, SCRAPPY), 1)
check("scrappy fighting on ghost/dark", matchup.effect("FIG", T{ "GHO", "DAR" }, nil, SCRAPPY), 2)
check("scrappy doesn't touch ground on flying", matchup.effect("GRO", T{ "FLY" }, nil, SCRAPPY), 0)
check("tinted lens x.5", matchup.effect("FIR", T{ "WAT" }, nil, TINTED), 1)
check("tinted lens x.25", matchup.effect("FIR", T{ "WAT", "ROC" }, nil, TINTED), .5)
check("tinted lens x2 stays", matchup.effect("FIR", T{ "GRA" }, nil, TINTED), 2)
check("tinted lens x0 stays", matchup.effect("NOR", T{ "GHO" }, nil, TINTED), 0)
check("tinted lens doesn't get past wonder guard", matchup.effect("FIR", T{ "ROC" }, WG, TINTED), 0)
check("tinted lens doesn't touch thick fat's half", matchup.effect("FIR", T{ "NOR" }, FAT, TINTED), .5)

-- Move tags --------------------------------------------------------------------------------------------------------

local function move_data(id)
  return { id = id, type = typechart.GEN5_IDS[data_moves.type[id]], category = data_moves.category[id], sound = data_moves.sound[id] }
end
local function move_tag(id, types, ability, attacker_ability, attacker_types)
  return matchup.move_tag(move_data(id), T(types), ability, { ability = attacker_ability, types = attacker_types or {} })
end
check("ember on grass", move_tag(52, { "GRA" }), 2)
check("ember on water/rock", move_tag(52, { "WAT", "ROC" }), .25)
check("earthquake on flying", move_tag(89, { "FLY" }), 0)
check("earthquake on levitate", move_tag(89, { "ELE" }, LEVITATE), 0)
check("earthquake mold breaker", move_tag(89, { "ELE" }, LEVITATE, MOLD), 2)
check("thunder wave is status", move_tag(86, { "GRO" }), nil)
check("charm stays normal", typechart.GEN5_IDS[data_moves.type[204]], "NOR")
check("moonlight stays normal", typechart.GEN5_IDS[data_moves.type[236]], "NOR")
check("curse is ghost", typechart.GEN5_IDS[data_moves.type[174]], "GHO")
for _, id in ipairs({ 237, 311, 449, 363, 546 }) do
  check("variable type " .. id, move_tag(id, { "GRA" }), "?")
  check("variable type under normalize " .. id, move_tag(id, { "GHO" }, nil, NORMALIZE), "?")
end
check("seismic toss on normal", move_tag(69, { "NOR" }), nil)
check("seismic toss on ghost", move_tag(69, { "GHO" }), 0)
check("night shade on normal", move_tag(101, { "NOR" }), 0)
check("night shade on psychic", move_tag(101, { "PSY" }), nil)
check("dragon rage on steel", move_tag(82, { "STE" }), nil)
check("sonic boom on ghost", move_tag(49, { "GHO" }), 0)
check("super fang on ghost", move_tag(162, { "GHO" }), 0)
check("psywave on dark", move_tag(149, { "DAR" }), 0)
check("endeavor on normal", move_tag(283, { "NOR" }), nil)
check("final gambit on ghost", move_tag(515, { "GHO" }), 0)
check("counter on ghost", move_tag(68, { "GHO" }), 0)
check("mirror coat on dark", move_tag(243, { "DAR" }), 0)
check("metal burst on fire", move_tag(368, { "FIR" }), nil)
check("bide on ghost", move_tag(117, { "GHO" }), 0)
check("fissure on flying", move_tag(90, { "FLY" }), 0)
check("fissure on rock", move_tag(90, { "ROC" }), nil)
check("horn drill on ghost", move_tag(32, { "GHO" }), 0)
check("guillotine on normal", move_tag(12, { "NOR" }), nil)
check("sheer cold on fire", move_tag(329, { "FIR" }), nil)
check("seismic toss wonder guard", move_tag(69, { "BUG", "GHO" }, WG), 0)
check("empty move", matchup.move_tag({ id = 0 }, T{ "NOR" }), nil)

-- Normalize: every move is Normal except the five above
check("normalize ember on ghost", move_tag(52, { "GHO" }, nil, NORMALIZE), 0)
check("normalize ember on rock", move_tag(52, { "ROC" }, nil, NORMALIZE), .5)
check("normalize ember on grass", move_tag(52, { "GRA" }, nil, NORMALIZE), 1)
check("normalize seismic toss on ghost", move_tag(69, { "GHO" }, nil, NORMALIZE), 0)
check("normalize status still no tag", move_tag(86, { "GRO" }, nil, NORMALIZE), nil)

-- Soundproof: the ROM's sound flag
local SOUND_MOVES = { 45, 46, 47, 48, 103, 173, 195, 215, 253, 304, 319, 320, 405, 448, 496, 497, 547, 555 }
local sound_count = 0
for _ in pairs(data_moves.sound) do sound_count = sound_count + 1 end
check("sound move count", sound_count, #SOUND_MOVES)
for _, id in ipairs(SOUND_MOVES) do check("sound move " .. id, data_moves.sound[id], true) end
check("hyper voice on soundproof", move_tag(304, { "NOR" }, SOUNDPROOF), 0)
check("hyper voice", move_tag(304, { "NOR" }), 1)
check("hyper voice soundproof hidden", move_tag(304, { "NOR" }, nil), 1)
check("hyper voice vs mold breaker", move_tag(304, { "NOR" }, SOUNDPROOF, MOLD), 1)
check("bug buzz on soundproof", move_tag(405, { "GRA" }, SOUNDPROOF), 0)
check("ember on soundproof", move_tag(52, { "GRA" }, SOUNDPROOF), 2)
check("growl is status", move_tag(45, { "NOR" }, SOUNDPROOF), nil)

-- Synchronoise is 485, Psychic
check("synchronoise is psychic", typechart.GEN5_IDS[data_moves.type[485]], "PSY")
check("synchronoise no shared type", move_tag(485, { "FIG" }, nil, 0, { "FIR" }), 0)
check("synchronoise shared type", move_tag(485, { "FIG", "FIR" }, nil, 0, { "FIR" }), 2)
check("synchronoise shared second type", move_tag(485, { "PSY" }, nil, 0, { "NOR", "PSY" }), .5)
check("synchronoise on no types", move_tag(485, {}, nil, 0, { "FIR" }), 0)

check("x4 label", matchup.label(4), "x4")
check("x1 label from float", matchup.label(0.5 * 2), "x1")
check("x.25 label", matchup.label(.25), "x.25")
check("x0 label", matchup.label(0), "x0")
check("x1/8 label", matchup.label(.125), "x1/8")

-- Data tables --------------------------------------------------------------------------------------------------------

check("move types", #data_moves.type, 559)
check("move categories", #data_moves.category, 559)
check("species", #species, 650)
check("abilities", #abilities, 164)
check("species 29", species[29], "Nidoran F")
check("species 32", species[32], "Nidoran M")
check("species 83", species[83], "Farfetch'd")
check("species 649", species[649], "Genesect")
check("personal count", #personal.type1, 649)
check("personal count 2", #personal.type2, 649)
check("bulbasaur grass", personal.type1[1], GEN5.GRA)
check("bulbasaur poison", personal.type2[1], GEN5.POI)
check("gyarados water", personal.type1[130], GEN5.WAT)
check("gyarados flying", personal.type2[130], GEN5.FLY)
check("zoroark dark", personal.type1[571], GEN5.DAR)
check("rotom wash", personal.forms[479][2][2], GEN5.WAT)
check("shaymin sky", personal.forms[492][1][2], GEN5.FLY)
for i, name in ipairs(species) do
  if name:find("[^\32-\126]") then check("species ascii " .. i, name, "ascii") end
end
for i, name in ipairs(abilities) do
  if name:find("[^\32-\126]") then check("ability ascii " .. i, name, "ascii") end
end

-- Reader --------------------------------------------------------------------------------------------------------------

local EMBER, EQ, TWAVE, HP, SEISMIC = 52, 89, 86, 237, 69

local function fresh_singles()
  mem = {}
  reader.reset()
  set_chain()
  set_mon(RECS.me1, 0, 500, "FIR", "FIG", 66, { EMBER, EQ, TWAVE, HP })
  w16(RECS.me1 + 0x0E, 164)
  w16(RECS.me1 + 0x10, 131)
  set_mon(RECS.foe1, 12, 130, "WAT", "FLY", 22, { 16 })
  w32(P, RECS.me1)
  w32(P + 0x1C, RECS.foe1)
  w32(SCD + 0x6C, RECS.me1)
end

fresh_singles()
local ctx = reader.chain()
check("chain live", ctx ~= nil, true)
check("screen", ctx.screen, 1)
check("pressed", ctx.pressed, -1)

local function chain_breaks(name, addr)
  fresh_singles()
  w32(addr, 0)
  check("chain drops on " .. name, reader.chain(), nil)
end
chain_breaks("base", 0x02000024)
chain_breaks("main", P - 4)
chain_breaks("core", MAIN + 4)
chain_breaks("core back", CORE)
chain_breaks("scd", CORE + 0x1C4)
chain_breaks("scd back main", SCD + 0xAC)
chain_breaks("scd back core", SCD + 0xA8)
chain_breaks("input", SCD + 0xBC)
fresh_singles()
w32(P - 4, MAIN + 4)
check("chain drops on main+0xCC", reader.chain(), nil)
fresh_singles()
w8(P + 0x18, 0)
check("chain drops on count", reader.chain(), nil)
fresh_singles()
w32(INPUT + 0x58, 8)
check("chain drops on screen", reader.chain(), nil)

local function battle()
  return reader.battle(reader.chain())
end

fresh_singles()
local b = battle()
check("foe species", b.foes[1].species, 130)
check("foe types", table.concat(b.foes[1].types, ","), "WAT,FLY")
check("my types", table.concat(b.mine[1].types, ","), "FIR,FIG")
check("choosing", b.choosing, b.mine[1])
check("no multi", b.multi, false)

fresh_singles()
w8(RECS.foe1 + 0xF9, GEN5.WAT)
check("single type deduped", #battle().foes[1].types, 1)

fresh_singles()
w16(RECS.foe1 + 0x0C, 29604)
check("garbage species", battle().foes[1], false)
fresh_singles()
w8(RECS.foe1 + 0x18, 0)
check("level 0", battle().foes[1], false)
fresh_singles()
w8(RECS.foe1 + 0x18, 101)
check("level 101", battle().foes[1], false)
fresh_singles()
w32(P + 0x1C, 0x04000000)
check("record out of RAM", battle().foes[1], false)
fresh_singles()
w8(RECS.foe1 + 0x19, 3)
check("foe record from the wrong side", battle().foes[1], false)

-- Roost (splitTypeCore)
fresh_singles()
set_condition(RECS.foe1, 24, 1)
check("roost hides flying", table.concat(battle().foes[1].types, ","), "WAT")
fresh_singles()
set_mon(RECS.foe1, 12, 641, "FLY", "FLY", 22)
set_condition(RECS.foe1, 24, 1)
check("roost on pure flying is normal", table.concat(battle().foes[1].types, ","), "NOR")
fresh_singles()
set_mon(RECS.foe1, 12, 641, "FLY", "FLY", 22)
check("pure flying without roost", table.concat(battle().foes[1].types, ","), "FLY")

fresh_singles()
set_condition(RECS.foe1, 16, 1)
check("gastro acid", battle().foes[1].ability, 0)

-- Conditions: on when the low 3 bits aren't zero
for name, id in pairs({ ingrain = 21, ["smack down"] = 31 }) do
  fresh_singles()
  set_condition(RECS.foe1, id, 1)
  check(name .. " grounds", battle().foes[1].grounded, true)
end
for name, id in pairs({ ["magnet rise"] = 30, telekinesis = 32 }) do
  fresh_singles()
  set_condition(RECS.foe1, id, 2)
  check(name .. " lifts", battle().foes[1].lifted, true)
end
fresh_singles()
check("not grounded", battle().foes[1].grounded, false)
check("not lifted", battle().foes[1].lifted, false)
check("nothing seen", battle().foes[1].seen_type, nil)
fresh_singles()
set_condition(RECS.foe1, 17, 1 | (7 << 9))
check("foresight, permanent kind", battle().foes[1].seen_type, "GHO")
fresh_singles()
set_condition(RECS.foe1, 17, 3 | (16 << 9))
check("miracle eye, depends on a mon", battle().foes[1].seen_type, "DAR")
fresh_singles()
set_condition(RECS.foe1, 17, 4 | (7 << 15))
check("foresight, depends on a mon with turns", battle().foes[1].seen_type, "GHO")

local function status_of(id, word)
  fresh_singles()
  set_condition(RECS.foe1, id, word)
  return table.concat(battle().foes[1].status, ",")
end
check("par", status_of(1, 1), "PAR")
check("slp", status_of(2, 3), "SLP")
check("frz", status_of(3, 1), "FRZ")
check("brn", status_of(4, 1), "BRN")
check("psn", status_of(5, 1), "PSN")
check("tox", status_of(5, 0x79), "TOX")
check("cnf", status_of(6, 2), "CNF")
check("low bits off", status_of(1, 8), "")
fresh_singles()
set_condition(RECS.foe1, 1, 1)
set_condition(RECS.foe1, 6, 1)
check("par and cnf", table.concat(battle().foes[1].status, ","), "PAR,CNF")

fresh_singles()
w8(RECS.foe1 + 0xFC, 7)
w8(RECS.foe1 + 0xFC + 5, 4)
local st = battle().foes[1].stages
check("stage count", #st, 2)
check("stage atk", st[1][1] .. st[1][2], "ATK1")
check("stage acc", st[2][1] .. st[2][2], "ACC-2")

-- Illusion: an encrypted party entry written the way the doc says -------------------------------------------------------------

local ORDERS = {
  "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
  "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA",
}
local ENTRY = 0x02258874

--- plain says the blocks are plain, and party_plain the battle stats as well, like the game's two flags
local function put_entry(pid, species_id, form, plain, ability, max_hp, party_plain)
  local words = {}
  for i = 0, 63 do words[i] = (i * 257 + 11) & 0xFFFF end
  local order = ORDERS[((pid & 0x3E000) >> 13) % 24 + 1]
  local block_a = (order:find("A", 1, true) - 1) * 16
  words[block_a] = species_id
  words[block_a + 6] = (words[block_a + 6] & 0xFF) | ((ability or 22) << 8)
  local form_word = (order:find("B", 1, true) - 1) * 16 + 0x0C
  words[form_word] = (words[form_word] & 0xFF00) | (form << 3)
  local sum = 0
  for i = 0, 63 do sum = (sum + words[i]) & 0xFFFF end
  w32(ENTRY, pid)
  w16(ENTRY + 4, (plain and 2 or 0) | (party_plain and 1 or 0))
  w16(ENTRY + 6, sum)
  local key = sum
  for i = 0, 63 do
    key = (key * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
    w16(ENTRY + 8 + i * 2, plain and words[i] or (words[i] ~ (key >> 16)))
  end
  -- The battle stats: status, level, HP, max HP, the rest, each word enciphered from the PID
  key = pid
  for i = 0, 41 do
    key = (key * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
    local word = i == 4 and (max_hp or 148) or ((i * 89 + 3) & 0xFFFF)
    w16(ENTRY + 0x88 + i * 2, party_plain and word or (word ~ (key >> 16)))
  end
end

local function disguised_foe()
  fresh_singles()
  set_mon(RECS.foe1, 12, 571, "DAR", "DAR", 149)
  w8(RECS.foe1 + 0x1B, 0x40)
  w32(RECS.foe1 + 0x04, ENTRY)
end

-- Every block order
for i = 0, 23 do
  disguised_foe()
  put_entry(i << 13, 130, 0, false, 22)
  local foe = battle().foes[1]
  check("disguise species, order " .. i, foe.species, 130)
  check("disguise types, order " .. i, table.concat(foe.types, ","), "WAT,FLY")
  check("disguise's ability, order " .. i, foe.ability, 22)
  check("disguise has info, order " .. i, foe.no_info, nil)
end
disguised_foe()
put_entry(0x12345678, 479, 2)
local washed = battle().foes[1]
check("disguise form name", washed.species, 479)
check("disguise form types", table.concat(washed.types, ","), "ELE,WAT")
disguised_foe()
put_entry(0x89ABCDEF, 25, 0, true)
check("already decrypted entry", battle().foes[1].species, 25)
disguised_foe()
put_entry(0x89ABCDEF, 130, 0)
w8(ENTRY + 20, r8(ENTRY + 20) ~ 0x55)
local broken = battle().foes[1]
check("bad checksum gives no info", broken.no_info, true)
disguised_foe()
w32(RECS.foe1 + 0x04, 0)
check("no pointer gives no info", battle().foes[1].no_info, true)
disguised_foe()
put_entry(0x12345678, 700, 0)
check("bad species gives no info", battle().foes[1].no_info, true)
disguised_foe()
put_entry(0x12345678, 130, 0, false, 0)
check("no ability gives no info", battle().foes[1].no_info, true)
disguised_foe()
put_entry(0x12345678, 130, 0, false, 165)
check("ability out of range gives no info", battle().foes[1].no_info, true)
disguised_foe()
put_entry(0x12345678, 130, 0, false, 26)
set_condition(RECS.foe1, 16, 1)
check("gastro acid on a disguise", battle().foes[1].ability, 0)
fresh_singles()
w8(RECS.me1 + 0x1B, 0x40)
check("your own disguise isn't touched", battle().mine[1].species, 500)
disguised_foe()
put_entry(0x12345678, 641, 0)
set_condition(RECS.foe1, 24, 1)
check("roost applies to the disguise's types", battle().foes[1].types[1], "NOR")

-- Drawing -----------------------------------------------------------------------------------------------------------

local font = require("pbi.font")
local icon_at = {}
for name, rect in pairs(require("pbi.data_icons")) do
  icon_at[rect[1] .. "," .. rect[2]] = name
end

local DARK = 0xFF101418
local function pills()
  local out = {}
  for _, c in ipairs(commands) do
    if c.screen == "bottom" and c.kind == "text" then
      out[#out + 1] = c.args[3]
    end
  end
  return table.concat(out, " ")
end

local function strips()
  local n = 0
  for _, c in ipairs(commands) do
    if c.screen == "bottom" and c.kind == "rect" and c.args[4] == 2 then n = n + 1 end
  end
  return n
end

--- Everything on a screen in draw order: text, without its outline, and icons, each with its name, x and y
local function draws(screen)
  local out = {}
  for _, c in ipairs(commands) do
    if c.screen == screen and c.kind == "text" and c.args[4] ~= DARK then
      out[#out + 1] = { name = c.args[3], x = c.args[1], y = c.args[2], color = c.args[4] }
    elseif c.screen == screen and c.kind == "image" then
      local src = c.args[4].src
      out[#out + 1] = { name = icon_at[src[1] .. "," .. src[2]] or "?icon", x = c.args[1], y = c.args[2], w = c.args[4].w, icon = true }
    end
  end
  return out
end

local function view(screen)
  local out = {}
  for _, d in ipairs(draws(screen)) do
    out[#out + 1] = d.name
  end
  return table.concat(out, " ")
end

local function top_view()
  return view("top")
end

local function bottom_view()
  return view("bottom")
end

--- The first draw with that name on a screen
local function drawn(screen, name)
  for _, d in ipairs(draws(screen)) do
    if d.name == name then return d end
  end
  return {}
end

--- Where a draw ends on the right, whether it's an icon or text
local function right_edge(d)
  if not d.x then return -1 end
  if d.icon then return d.x + d.w end
  local w = 0
  for i = 1, #d.name do
    w = w + (font[d.name:byte(i) - 31] or 12)
  end
  return d.x + w / 2
end

--- The types listed under a marker in a view with one foe's rows
local function under_marker(text, marker)
  local out, on = {}, false
  for word in text:gmatch("%S+") do
    if word:match("^x[%d%.]+$") then
      on = word == marker
    elseif on then
      out[#out + 1] = word
    end
  end
  return table.concat(out, " ")
end

local function rects(screen)
  local n = 0
  for _, c in ipairs(commands) do
    if c.screen == screen and c.kind == "rect" then n = n + 1 end
  end
  return n
end

local function no_top_past_right_edge()
  for _, c in ipairs(commands) do
    if c.screen == "top" and c.kind == "image" and c.args[1] + c.args[4].w > 256.01 then
      check("icon in screen", c.args[1] + c.args[4].w, "<=256")
    end
    if c.screen == "top" and c.kind == "rect" and c.args[1] + c.args[3] > 256.01 then
      check("rect in screen", c.args[1] + c.args[3], "<=256")
    end
  end
end

local ACTION_MENU, MOVE_MENU, TARGET_SELECT = 1, 2, 3

-- Panels: icons, no background. Only the foe's, types on its bar and the rest on the bottom screen.
fresh_singles()
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("panel top", top_view(), "WAT FLY")
check("panel bottom", bottom_view(), "x4 ELE x2 ROC x.5 FIR WAT FIG BUG STE x0 GRO")
check("panels have no background", rects("top") + rects("bottom"), 0)
for _, c in ipairs(commands) do
  if c.kind == "image" then
    check("icons are one image pixel per overlay pixel", c.args[4].scale, 0.5)
  end
end
-- Types end just left of "HP", and one type sits next to it
check("bar type 1 x", drawn("top", "WAT").x, 7.5)
check("bar type 2 x", drawn("top", "FLY").x, 17.5)
check("bar types y", drawn("top", "FLY").y, 40.5)
-- Bottom rows: weak, resist, immune, 10 apart from y=146 on the move menu and target select, from y=88 on the action menu
for _, screen in ipairs({ MOVE_MENU, TARGET_SELECT }) do
  commands = {}
  ui.panels(battle(), true, screen)
  check("weak row", drawn("bottom", "x4").x .. "," .. drawn("bottom", "x4").y, "3,146")
  check("resist row", drawn("bottom", "x.5").x .. "," .. drawn("bottom", "x.5").y, "3,156")
  check("immune row", drawn("bottom", "x0").x .. "," .. drawn("bottom", "x0").y, "3,166")
end
commands = {}
ui.panels(battle(), true, ACTION_MENU)
check("action weak row", drawn("bottom", "x4").x .. "," .. drawn("bottom", "x4").y, "3,88")
check("action resist row", drawn("bottom", "x.5").y, 98)
check("action immune row", drawn("bottom", "x0").y, 108)

-- A single type, and empty rows get skipped: Normal has no resists
fresh_singles()
set_mon(RECS.foe1, 12, 19, "NOR", nil, 22)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("one type sits next to HP", drawn("top", "NOR").x, 17.5)
check("no resist row", bottom_view(), "x2 FIG x0 GHO")
check("immune moves up", drawn("bottom", "x0").y, 156)

-- Under the HP box: nothing without a status or stat change. ATK and DEF are an icon and a number, the rest stay text.
local WARM_COLOR, COOL_COLOR = 0xFFFFB347, 0xFF62B6FF
local function stages_and_burn(rec)
  w8(rec + 0xFC, 5)
  w8(rec + 0xFD, 7)
  w8(rec + 0xFE, 7)
  set_condition(rec, 4, 1)
end
fresh_singles()
commands = {}
ui.panels(battle(), true, ACTION_MENU)
for _, d in ipairs(draws("top")) do
  check("nothing but types without a state: " .. d.name, d.y, 40.5)
end
stages_and_burn(RECS.foe1)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("state row", top_view(), "WAT FLY BRN ATK -1 DEF +1 SPA+1")
check("state row x", drawn("top", "BRN").x, 4)
check("state row y", drawn("top", "BRN").y, 55)
check("stat icon gap", drawn("top", "ATK").x - drawn("top", "BRN").x, 9 + 1 + 2)
check("number after its icon", drawn("top", "-1").x, drawn("top", "ATK").x + 9.5)
check("number on the icon's line", drawn("top", "-1").y, drawn("top", "ATK").y + 1)
check("down is cool", drawn("top", "-1").color, COOL_COLOR)
check("up is warm", drawn("top", "+1").color, WARM_COLOR)
commands = {}
ui.card(battle(), true)
check("card has the icons and numbers", top_view():find("ATK -1 DEF +1 SPA+1", 1, true) ~= nil, true)
check("card keeps the number on the icon's line", drawn("top", "-1").y, drawn("top", "ATK").y + 1)
check("card's stages end at its right edge", right_edge(drawn("top", "SPA+1")), 248)
-- All seven take their own row, and still end there
fresh_singles()
for i = 0, 6 do w8(RECS.foe1 + 0xFC + i, i % 2 == 0 and 8 or 3) end
commands = {}
ui.card(battle(), true)
check("card's full row of stages ends at its right edge", right_edge(drawn("top", "EVA+2")), 248)

-- An icon never gets left without its number, and each pair counts as one for +N, however narrow the row is, down to what a +N needs
local all_ui = setmetatable({ foe_panel_w = 0 }, { __index = game.layout })
local narrow_ui = require("pbi.ui").new(setmetatable({ layout = all_ui }, { __index = game }), reader)
for width = 12, 100 do
  all_ui.foe_panel_w = width
  fresh_singles()
  w8(RECS.foe1 + 0xFC, 8)
  w8(RECS.foe1 + 0xFD, 3)
  w8(RECS.foe1 + 0xFE, 8)
  commands = {}
  narrow_ui.panels(battle(), true, MOVE_MENU)
  local under = {}
  for _, d in ipairs(draws("top")) do
    if d.y >= 53 then under[#under + 1] = d.name end
  end
  local shown, more = 0, 0
  for i, name in ipairs(under) do
    if name == "ATK" then check("atk has its number, width " .. width, under[i + 1], "+2") end
    if name == "DEF" then check("def has its number, width " .. width, under[i + 1], "-3") end
    if name == "ATK" or name == "DEF" or name == "SPA+2" then shown = shown + 1 end
    -- A +N is the last thing on the row, and it isn't ATK's own number
    if i == #under and under[i - 1] ~= "ATK" and name:match("^%+%d+$") then more = tonumber(name:sub(2)) end
  end
  check("stat changes add up, width " .. width, shown + more, 3)
  check("the row stays in its width, width " .. width, (function()
    for _, d in ipairs(draws("top")) do
      if d.y >= 53 and right_edge(d) > 4 + width then return false end
    end
    return true
  end)(), true)
end

-- Your rows are only in the card now. x4 and x2 are separate groups.
fresh_singles()
set_mon(RECS.me1, 0, 460, "GRA", "ICE", 117, { EMBER })
commands = {}
ui.card(battle(), true)
check("your x4 and x2 are separate", top_view():find("x4 FIR x2 FIG", 1, true) ~= nil, true)
check("your x2 group is whole", top_view():find("x2 FIG POI FLY BUG ROC STE", 1, true) ~= nil, true)
no_top_past_right_edge()

local function your_immune_row(show)
  commands = {}
  ui.card(battle(), show)
  local section = top_view():match("YOU .*$")
  return section:match("IMMUNE (.-) STRONG")
end

-- Your ability counts in your rows, unless a shown foe's Mold Breaker bypasses it
fresh_singles()
set_mon(RECS.me1, 0, 479, "ELE", "GHO", LEVITATE, { EMBER })
set_mon(RECS.foe1, 12, 130, "WAT", "FLY", 22)
check("your levitate row", your_immune_row(true), "NOR FIG GRO")
set_mon(RECS.foe1, 12, 130, "WAT", "FLY", MOLD)
check("shown mold breaker bypasses your levitate", your_immune_row(true), "NOR FIG")
check("hidden mold breaker doesn't", your_immune_row(false), "NOR FIG GRO")
-- A foe's Scrappy counts in your rows like its Mold Breaker family, only when foe abilities are shown. Its Tinted Lens stays out.
set_mon(RECS.me1, 0, 94, "GHO", "POI", 26)
set_mon(RECS.foe1, 12, 130, "NOR", nil, SCRAPPY)
check("shown scrappy takes your ghost immunities off", your_immune_row(true), "GRO")

check("hidden scrappy doesn't", your_immune_row(false), "NOR FIG GRO")
set_mon(RECS.foe1, 12, 130, "NOR", nil, TINTED)
check("foe tinted lens stays out", your_immune_row(true), "NOR FIG GRO")

-- Singles tags: fire, ground, status, hidden power on water/flying
fresh_singles()
commands = {}
ui.move_tags(battle(), true)
check("singles tags", pills(), "x.5 x0 ?")
check("singles strips", strips(), 2)

fresh_singles()
w32(SCD + 0x6C, 0)
commands = {}
ui.move_tags(battle(), true)
check("no tags without a chooser", #commands, 0)

-- Levitate foe on and off
fresh_singles()
set_mon(RECS.foe1, 12, 479, "ELE", "GHO", LEVITATE)
commands = {}
ui.move_tags(battle(), true)
check("levitate shown", pills(), "x1 x0 ?")
commands = {}
ui.move_tags(battle(), false)
check("levitate hidden", pills(), "x1 x2 ?")

-- Conditions count whatever the option says
fresh_singles()
set_mon(RECS.foe1, 12, 479, "ELE", "GHO", LEVITATE)
set_condition(RECS.foe1, 31, 1)
commands = {}
ui.move_tags(battle(), true)
check("smack down gets past levitate", pills(), "x1 x2 ?")
fresh_singles()
set_mon(RECS.foe1, 12, 479, "ELE", "GHO", 0)
set_condition(RECS.foe1, 30, 1)
commands = {}
ui.move_tags(battle(), false)
check("magnet rise shows with abilities hidden", pills(), "x1 x0 ?")

-- Mold Breaker attacker
fresh_singles()
set_mon(RECS.foe1, 12, 479, "ELE", "GHO", LEVITATE)
w16(RECS.me1 + 0x13C, MOLD)
commands = {}
ui.move_tags(battle(), true)
check("mold breaker tags", pills(), "x1 x2 ?")

-- Your Scrappy, Tinted Lens and Normalize reach the tags
fresh_singles()
set_mon(RECS.me1, 0, 500, "FIR", "FIG", SCRAPPY, { 7, 5, 0, 0 })
set_mon(RECS.foe1, 12, 94, "GHO", "POI", 0)
commands = {}
ui.move_tags(battle(), true)
check("scrappy fire punch and mega punch", pills(), "x1 x1")
fresh_singles()
set_mon(RECS.me1, 0, 500, "FIR", "FIG", TINTED, { EMBER })
set_mon(RECS.foe1, 12, 130, "WAT", nil, 0)
commands = {}
ui.move_tags(battle(), true)
check("tinted lens tag", pills(), "x1")
fresh_singles()
set_mon(RECS.me1, 0, 500, "FIR", "FIG", NORMALIZE, { EMBER, EQ })
set_mon(RECS.foe1, 12, 94, "GHO", "POI", 0)
commands = {}
ui.move_tags(battle(), true)
check("normalize tags", pills(), "x0 x0")

-- Disguised foe: the mod tells the disguise's story. Real types are dark, the disguise is Gyarados.
disguised_foe()
put_entry(0x12345678, 130, 0)
commands = {}
ui.move_tags(battle(), true)
check("disguised tags use the disguise's types", pills(), "x.5 x0 ?")
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("disguised panel", top_view(), "WAT FLY")
check("disguised bottom rows use the disguise's types", bottom_view():sub(1, 6), "x4 ELE")
commands = {}
ui.card(battle(), true)
check("disguised card shows the disguise's name", top_view():find("Gyarados Lv42", 1, true) ~= nil, true)
check("disguised card hides the name", top_view():find("Zoroark", 1, true), nil)
check("disguised card hides the real ability", top_view():find("Illusion", 1, true), nil)
check("disguised card shows the disguise's ability", top_view():find("Intimidate", 1, true) ~= nil, true)
check("disguised card has no ???", top_view():find("???", 1, true), nil)

-- A disguise has to look exactly like a real foe of that species and ability, its HP in the disguise's numbers included
local function foe_views(show)
  local bt = battle()
  commands = {}
  ui.panels(bt, show, MOVE_MENU)
  local panel = top_view() .. " | " .. bottom_view()
  commands = {}
  ui.move_tags(bt, show)
  local tags = pills()
  commands = {}
  ui.card(bt, show)
  return panel, tags, top_view()
end
for _, setup in ipairs({
  { "Gyarados with Intimidate", 130, "WAT", "FLY", 22 },
  { "Rotom with Levitate", 479, "ELE", "GHO", LEVITATE },
  { "Shedinja with Wonder Guard", 292, "BUG", "GHO", WG },
  { "Whismur with Soundproof", 293, "NOR", nil, SOUNDPROOF },
}) do
  local name, id, t1, t2, ability = table.unpack(setup)
  for _, show in ipairs({ true, false }) do
    fresh_singles()
    set_mon(RECS.foe1, 12, id, t1, t2, ability, { 16 })
    -- The disguised one below is at 77/201, which is 57/148 scaled up
    w16(RECS.foe1 + 0x10, 57)
    local real_panel, real_tags, real_card = foe_views(show)
    disguised_foe()
    w16(RECS.foe1 + 0x0E, 201)
    w16(RECS.foe1 + 0x10, 77)
    put_entry(0x12345678, id, 0, false, ability)
    local panel, tags, card = foe_views(show)
    local which = name .. (show and ", shown" or ", hidden")
    check("disguise panel matches a real foe: " .. which, panel, real_panel)
    check("disguise tags match a real foe: " .. which, tags, real_tags)
    check("disguise card matches a real foe: " .. which, card, real_card)
  end
end

-- A disguise's immunity ability reaches the pills and the rows, and stays out when hidden
disguised_foe()
put_entry(0x12345678, 479, 0, false, LEVITATE)
commands = {}
ui.move_tags(battle(), true)
check("disguise's levitate on the pills", pills(), "x1 x0 ?")
commands = {}
ui.move_tags(battle(), false)
check("disguise's levitate hidden", pills(), "x1 x2 ?")
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("disguise's levitate on the rows", under_marker(bottom_view(), "x0"), "NOR FIG GRO")
commands = {}
ui.panels(battle(), false, MOVE_MENU)
check("disguise's levitate hidden from the rows", under_marker(bottom_view(), "x2"), "GRO GHO DAR")
check("disguise's levitate hidden from the x0 row", under_marker(bottom_view(), "x0"), "NOR FIG")

-- Disguise that can't be read: nothing about that foe, and no ??? anywhere
disguised_foe()
commands = {}
ui.move_tags(battle(), true)
check("no info: no pills", pills(), "")
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("no info: no panels", #commands, 0)
commands = {}
ui.card(battle(), true)
check("no info: only your section", top_view():sub(1, 3), "YOU")
check("no info: no ???", top_view():find("???", 1, true), nil)

local function fresh_doubles()
  fresh_singles()
  w32(INPUT + 0x50, 1)
  set_mon(RECS.me2, 1, 183, "WAT", nil, 47, { 55 })
  set_mon(RECS.foe2, 13, 530, "GRO", "STE", 159, { 89 })
  w32(P + 4, RECS.me2)
  w32(P + 0x1C + 4, RECS.foe2)
end

-- The bottom rows count the foe's ability only when it's shown, and your own ability always. Rotom with Levitate.
for _, doubles in ipairs({ false, true }) do
  local name = doubles and "doubles " or "singles "
  local function foe_rows(show)
    commands = {}
    ui.panels(battle(), show, MOVE_MENU)
    return doubles and bottom_view():match("^1 (.-) 2 ") or bottom_view()
  end
  local setup = doubles and fresh_doubles or fresh_singles
  setup()
  set_mon(RECS.foe1, 12, 479, "ELE", "GHO", LEVITATE)
  check(name .. "shown levitate is immune", under_marker(foe_rows(true), "x0"), "NOR FIG GRO")
  check(name .. "shown levitate isn't weak", under_marker(foe_rows(true), "x2"), "GHO DAR")
  check(name .. "hidden levitate is weak", under_marker(foe_rows(false), "x2"), "GRO GHO DAR")
  check(name .. "hidden levitate isn't immune", under_marker(foe_rows(false), "x0"), "NOR FIG")
  w16(RECS.me1 + 0x13C, MOLD)
  check(name .. "your mold breaker takes it off x0", under_marker(foe_rows(true), "x0"), "NOR FIG")
  check(name .. "your mold breaker puts it on x2", under_marker(foe_rows(true), "x2"), "GRO GHO DAR")
end

fresh_doubles()
commands = {}
ui.move_tags(battle(), true)
check("doubles tags", pills(), "1x.5 2x2 1x0 2x2 1? 2?")
check("doubles no strips", strips(), 0)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("doubles top without a state", top_view(), "WAT FLY GRO STE")
check("doubles bottom", bottom_view(), "1 x4 ELE x2 ROC x.5 FIR WAT FIG BUG STE x0 GRO 2 x2 FIR WAT FIG GRO x.25 ROC x.5 NOR FLY PSY BUG GHO DRA DAR STE x0 ELE POI")
check("doubles have no background", rects("top") + rects("bottom"), 0)
-- foes[1] owns the upper box and foes[2] the lower, like the 1 and 2 on the move pills
check("foe 1 bar type x", drawn("top", "WAT").x, 11.5)
check("foe 1 bar type ends", drawn("top", "FLY").x + 9, 30.5)
check("foe 1 bar y", drawn("top", "FLY").y, 29)
check("foe 2 bar type ends", drawn("top", "STE").x + 9, 26.5)
check("foe 2 bar y", drawn("top", "STE").y, 53)
-- Each foe: its number at x=3, weak from x=10, the row below it with resist and immune together
local function check_doubles_spots(name, y1, y2)
  local foe1_rows = {}
  for _, d in ipairs(draws("bottom")) do
    if d.name == "1" or d.name == "2" then
      check(name .. " number " .. d.name .. " x", d.x, 3)
      check(name .. " number " .. d.name .. " y", d.y, (d.name == "1" and y1 or y2) + 1)
    elseif d.icon then
      foe1_rows[d.y] = true
      check(name .. " nothing left of the column: " .. d.name, d.x >= 10, true)
    end
  end
  check(name .. " foe 1 weak row", foe1_rows[y1], true)
  check(name .. " foe 1 second row", foe1_rows[y1 + 10] or foe1_rows[y1 + 20], true)
  check(name .. " foe 2 weak row", foe1_rows[y2], true)
  check(name .. " foe 2 second row", foe1_rows[y2 + 10] or foe1_rows[y2 + 20], true)
end
check_doubles_spots("move menu", 146, 168)
check("doubles resist row", drawn("bottom", "x.5").y, 156)
check("doubles immune shares the resist row", drawn("bottom", "x0").y, 156)
commands = {}
ui.panels(battle(), true, TARGET_SELECT)
check_doubles_spots("target select", 146, 168)
commands = {}
ui.panels(battle(), true, ACTION_MENU)
check_doubles_spots("action menu", 40, 88)
for _, d in ipairs(draws("bottom")) do
  if d.icon then check("action menu stays in its column: " .. d.name, d.x + d.w <= 79, true) end
end

-- Under the HP boxes: only a foe with a state gets a row, so a single row moves up to y=64
fresh_doubles()
w8(RECS.foe2 + 0xFD, 4)
set_condition(RECS.foe2, 2, 1)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("only foe 2 has a row", top_view(), "WAT FLY GRO STE 2 SLP DEF -2")
check("its number", drawn("top", "2").x, 1)
check("its first icon", drawn("top", "SLP").x, 7)
check("its row moves up", drawn("top", "SLP").y, 67)
fresh_doubles()
stages_and_burn(RECS.foe1)
w8(RECS.foe2 + 0xFD, 4)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("both foes have a row", top_view(), "WAT FLY 1 BRN ATK -1 DEF +1 SPA+1 GRO STE 2 DEF -2")
check("row 1 at 67", drawn("top", "BRN").y, 67)
check("row 2 at 78", drawn("top", "-2").y, 78)
check("stages alone start at the column", drawn("top", "DEF").x, 7 + 12 + 19.5 + 2)

fresh_doubles()
w16(RECS.foe1 + 0x10, 0)
commands = {}
ui.move_tags(battle(), true)
check("fainted foe has no pill", pills(), "2x2 2x2 2?")
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("fainted foe has no bar types", top_view(), "GRO STE")
check("fainted foe has no rows", bottom_view():sub(1, 1), "2")
check("the other foe stays in its place", drawn("bottom", "x2").y, 168)
commands = {}
ui.panels(battle(), true, ACTION_MENU)
check("the other foe stays in its place, action menu", drawn("bottom", "x2").y, 88)
commands = {}
ui.card(battle(), true)
check("fainted foe in doubles has no name", top_view():sub(1, 15), "FOE 1 FNT FOE 2")
check("fainted foe shows no species", top_view():find("Gyarados", 1, true), nil)

-- Fainting clears the Illusion bit on the client copy, so the record says Zoroark: nothing may show it
fresh_singles()
set_mon(RECS.foe1, 12, 571, "DAR", "DAR", 149)
w16(RECS.foe1 + 0x10, 0)
commands = {}
ui.card(battle(), true)
check("fainted disguised foe, card", top_view():sub(1, 7), "FOE FNT")
check("fainted disguised foe, no species", top_view():find("Zoroark", 1, true), nil)
check("fainted disguised foe, no ability", top_view():find("Illusion", 1, true), nil)
check("fainted disguised foe, no HP", top_view():find("0/148", 1, true), nil)
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("fainted disguised foe, no panel", #commands, 0)
commands = {}
ui.move_tags(battle(), true)
check("fainted disguised foe, no pills", pills(), "")
fresh_doubles()
set_mon(RECS.foe2, 13, 571, "DAR", "DAR", 149)
w16(RECS.foe2 + 0x10, 0)
commands = {}
ui.card(battle(), true)
check("fainted disguised foe 2 in doubles", top_view():find("FOE 2 FNT", 1, true) ~= nil, true)
check("no real species in doubles", top_view():find("Zoroark", 1, true), nil)
-- A fainted mon of yours keeps its name
fresh_singles()
w16(RECS.me1 + 0x10, 0)
w32(SCD + 0x6C, 0)
commands = {}
ui.card(battle(), true)
check("your fainted mon keeps its name", top_view():find("YOU Emboar", 1, true) ~= nil, true)

-- Spacing: a space's worth (3 pixels) between FOE 1 and the name
fresh_doubles()
commands = {}
ui.card(battle(), true)
local side_x, name_x
for _, c in ipairs(commands) do
  if c.kind == "text" and c.args[3] == "FOE 1" then side_x = c.args[1] end
  if c.kind == "text" and c.args[3]:find("Gyarados", 1, true) then name_x = c.args[1] end
end
check("space after FOE 1", name_x - side_x >= 23 + 3, true)

-- Clients: 2 is the usual doubles, more than that has its own parties. Mon IDs start at 0, 12, 6, 18 per client.
local function second_side()
  fresh_doubles()
  w8(P + 2 * 0x1C + 0x18, 0)
  w8(P + 3 * 0x1C + 0x18, 0)
end
second_side()
local two = battle()
check("2 clients aren't multi", two.multi, false)
check("2 clients, foe 2 from client 1", two.foes[2].rec, RECS.foe2)
check("2 clients, your 2 from client 0", two.mine[2].rec, RECS.me2)

local ALLY, FOE3 = 0x0225D000, 0x0225E000
second_side()
set_mon(ALLY, 6, 25, "ELE", nil, 9)
w32(P + 2 * 0x1C, ALLY)
w8(P + 2 * 0x1C + 0x18, 1)
local three_ally = battle()
check("3 clients with an ally are multi", three_ally.multi, true)
check("3 clients with an ally, foes stay client 1's", three_ally.foes[2].rec, RECS.foe2)
check("3 clients with an ally, your second is the ally", three_ally.mine[2].rec, ALLY)

second_side()
set_mon(FOE3, 18, 133, "NOR", nil, 50)
w32(P + 3 * 0x1C, FOE3)
w8(P + 3 * 0x1C + 0x18, 1)
local three_foes = battle()
check("3 clients with a second foe are multi", three_foes.multi, true)
check("3 clients with a second foe, foe 2 from client 3", three_foes.foes[2].rec, FOE3)
check("3 clients with a second foe, your second stays yours", three_foes.mine[2].rec, RECS.me2)
commands = {}
ui.panels(three_foes, true, MOVE_MENU)
check("3 clients draw doubles rows", bottom_view():sub(1, 1), "1")
commands = {}
ui.card(three_foes, true)
check("3 clients draw a card", #commands > 0, true)

second_side()
set_mon(ALLY, 6, 25, "ELE", nil, 9)
set_mon(FOE3, 18, 133, "NOR", nil, 50)
w32(P + 2 * 0x1C, ALLY)
w32(P + 3 * 0x1C, FOE3)
w8(P + 2 * 0x1C + 0x18, 1)
w8(P + 3 * 0x1C + 0x18, 1)
local four = battle()
check("4 clients, foes 1 and 3", four.foes[1].rec == RECS.foe1 and four.foes[2].rec == FOE3, true)
check("4 clients, you and the ally", four.mine[1].rec == RECS.me1 and four.mine[2].rec == ALLY, true)

-- A record on the wrong side, as a wrong client order would give, shows nothing rather than wrong data
second_side()
set_mon(FOE3, 3, 133, "NOR", nil, 50)
w32(P + 3 * 0x1C, FOE3)
w8(P + 3 * 0x1C + 0x18, 1)
check("wrong-side record in a multi is skipped", battle().foes[2], false)
commands = {}
ui.move_tags(battle(), true)
check("wrong-side record has no pills", pills():find("2x", 1, true), nil)

-- A disguise in doubles that can't be read: the other foe still shows
fresh_doubles()
w8(RECS.foe1 + 0x1B, 0x40)
commands = {}
ui.move_tags(battle(), true)
check("no info in doubles: only the other pill", pills(), "2x2 2x2 2?")

-- Doubles fixed damage
fresh_doubles()
set_mon(RECS.me1, 0, 500, "FIR", "FIG", 66, { SEISMIC })
set_mon(RECS.foe1, 12, 92, "GHO", "POI", 26)
commands = {}
ui.move_tags(battle(), true)
check("fixed damage in doubles", pills(), "1x0")

-- The rare x1/8 fits a doubles pill
fresh_doubles()
set_mon(RECS.me1, 0, 500, "FIR", "FIG", 66, { EMBER })
set_mon(RECS.foe1, 12, 92, "WAT", "ROC", FAT)
commands = {}
ui.move_tags(battle(), true)
check("x1/8 pill", pills():sub(1, 5), "1x1/8")

-- Triples: card only, and it has to fit
local function fresh_triples()
  fresh_doubles()
  w32(INPUT + 0x50, 2)
  local me3, foe3 = 0x0225B66C, 0x0225C344
  set_mon(me3, 2, 208, "STE", "GRO", 5, {})
  set_mon(foe3, 14, 227, "STE", "FLY", 5, {})
  w32(P + 8, me3)
  w32(P + 0x1C + 8, foe3)
end

local function worst(fn)
  commands = {}
  local steps = 0
  debug.sethook(function() steps = steps + 1 end, "", 100)
  fn()
  debug.sethook()
  return steps * 100, #commands
end

local function bottom_of_card()
  local max = 0
  for _, c in ipairs(commands) do
    if c.screen == "top" then
      local h = c.kind == "rect" and c.args[4] or 9
      max = math.max(max, c.args[2] + h)
    end
  end
  return max
end

fresh_triples()
local i_tri, c_tri = worst(function() ui.card(battle(), true) end)
check("triple card fits", bottom_of_card() <= 192, true)
no_top_past_right_edge()

-- Steel/flying, Wonder Guard and everything status-wise in a full doubles frame, with a disguise to decrypt each time
fresh_doubles()
w32(INPUT + 0x58, 2)
set_mon(RECS.foe1, 12, 227, "STE", "FLY", 5)
set_mon(RECS.foe2, 13, 292, "BUG", "GHO", WG)
w8(RECS.foe2 + 0x1B, 0x40)
w32(RECS.foe2 + 0x04, ENTRY)
put_entry(0x12345678, 292, 0)
for i = 0, 6 do w8(RECS.foe1 + 0xFC + i, i % 2 == 0 and 12 or 0) end
for i = 0, 6 do w8(RECS.me1 + 0xFC + i, i % 2 == 0 and 12 or 0) end
set_condition(RECS.foe1, 1, 1)
set_condition(RECS.foe1, 6, 1)
local i_full, c_full = worst(function()
  reader.reset()
  local bt = battle()
  ui.panels(bt, true, MOVE_MENU)
  ui.move_tags(bt, true)
  ui.card(bt, true)
end)
check("crowded card fits", bottom_of_card() <= 192, true)
no_top_past_right_edge()
print(string.format("work: triple card %d instructions, %d commands; full doubles frame %d instructions, %d commands (limits 1000000 and 4096)", i_tri, c_tri, i_full, c_full))
check("full frame budget", i_full < 100000, true)
check("full frame commands", c_full < 1000, true)

-- The narrow action menu column: wraps to 2 lines per row, with the indent after the marker
fresh_singles()
set_mon(RECS.foe1, 12, 306, "STE", nil, 69)
commands = {}
ui.panels(battle(), true, ACTION_MENU)
check("steel panel lists its x.5s", bottom_view():find("x.5 NOR", 1, true) ~= nil, true)
fresh_singles()
set_mon(RECS.foe1, 12, 292, "BUG", "GHO", WG)
commands = {}
ui.panels(battle(), true, ACTION_MENU)
check("wonder guard immune row wraps whole", bottom_view():find("x0 NOR WAT ELE GRA ICE FIG POI GRO PSY BUG DRA STE", 1, true) ~= nil, true)
check("the wrapped line starts after the marker", drawn("bottom", "POI").x, 10)
check("the wrapped line is the next one", drawn("bottom", "POI").y, drawn("bottom", "x0").y + 10)
-- Stage text is what overflows the row under the HP box: it ends in an outlined +N
fresh_singles()
for i = 0, 6 do w8(RECS.foe1 + 0xFC + i, i % 2 == 0 and 8 or 3) end
commands = {}
ui.panels(battle(), true, MOVE_MENU)
check("too many stages end in +N", top_view():match(" (%+%d+)$") ~= nil, true)
check("the +N is outlined", (function()
  for _, c in ipairs(commands) do
    if c.kind == "text" and c.args[3] == top_view():match(" (%+%d+)$") and c.args[4] == DARK then return true end
  end
  return false
end)(), true)

-- Every pair of types, with the abilities that change the most, on all the menus: everything stays in its place, and nothing's cut
local function fake_foe(t1, t2, ability)
  return { hp = 1, max_hp = 1, types = t1 == t2 and { t1 } or { t1, t2 }, ability = ability, status = {}, stages = {} }
end
local function bottom_extent()
  local right, bottom_edge = 0, 0
  for _, d in ipairs(draws("bottom")) do
    if d.icon then
      right = math.max(right, d.x + d.w)
      bottom_edge = math.max(bottom_edge, d.y + 9)
    end
  end
  return right, bottom_edge
end
local function has_plus()
  return bottom_view():find("+", 1, true) ~= nil
end
local worst_action_bottom, doubles_action_plus = 0, 0
for a = 0, 16 do
  for d = a, 16 do
    local t1, t2 = typechart.GEN5_IDS[a], typechart.GEN5_IDS[d]
    for _, ability in ipairs({ 0, LEVITATE, WG, VOLT }) do
      local pair = t1 .. "/" .. t2 .. " ability " .. ability
      -- Wonder Guard on anything but Bug/Ghost (Trace, Skill Swap) has too many x0s to always fit, so that one may end in +N
      local natural = ability ~= WG or (t1 == "BUG" and t2 == "GHO")
      local one = { rule = 0, multi = false, foes = { fake_foe(t1, t2, ability) }, mine = {} }
      commands = {}
      ui.panels(one, true, ACTION_MENU)
      local right, low = bottom_extent()
      worst_action_bottom = math.max(worst_action_bottom, low)
      check("singles action right edge, " .. pair, right <= 79, true)
      check("singles action ends by 144, " .. pair, low <= 144, true)
      if natural then check("singles action has no +N, " .. pair, has_plus(), false) end
      commands = {}
      ui.panels(one, true, MOVE_MENU)
      right, low = bottom_extent()
      check("singles moves right edge, " .. pair, right <= 176, true)
      check("singles moves in screen, " .. pair, low <= 192, true)
      if natural then check("singles moves has no +N, " .. pair, has_plus(), false) end
      local two = { rule = 1, multi = false, foes = { fake_foe(t1, t2, ability), fake_foe(t1, t2, ability) }, mine = {} }
      commands = {}
      ui.panels(two, true, ACTION_MENU)
      right, low = bottom_extent()
      check("doubles action right edge, " .. pair, right <= 79, true)
      check("doubles action height, " .. pair, low <= 144, true)
      for _, row in ipairs(draws("bottom")) do
        if row.icon and row.y < 88 then check("doubles action foe 1 stays above foe 2, " .. pair, row.y + 9 <= 88, true) end
      end
      if ability == 0 and has_plus() then doubles_action_plus = doubles_action_plus + 1 end
      commands = {}
      ui.panels(two, true, MOVE_MENU)
      right, low = bottom_extent()
      check("doubles moves right edge, " .. pair, right <= 176, true)
      check("doubles moves in screen, " .. pair, low <= 192, true)
      -- A given ability adds an x0 at most, which can tip the fullest row (Steel/Electric with Levitate) into +N
      if ability == 0 then check("doubles moves has no +N, " .. pair, has_plus(), false) end
    end
  end
end
print(string.format("singles action menu ends at y=%d at most; doubles action menu ends in +N for %d of 153 type pairs", worst_action_bottom, doubles_action_plus))

-- Without the sheet: logged once, text chips, no errors
have_sheet = false
logged = {}
local chip_ui = require("pbi.ui").new(game, reader)
check("sheet failure logged once", #logged, 1)
fresh_singles()
stages_and_burn(RECS.foe1)
commands = {}
chip_ui.panels(battle(), true, ACTION_MENU)
check("chips are text", top_view():sub(1, 3), "WAT")
check("no icons without a sheet", top_view():find("?icon", 1, true), nil)
check("atk and def stay text without a sheet", top_view():find("BRN ATK-1 DEF+1 SPA+1", 1, true) ~= nil, true)
check("two chips start at the screen's edge", drawn("top", "WAT").x, 1)
local any_image = false
for _, c in ipairs(commands) do any_image = any_image or c.kind == "image" end
check("no image draws without a sheet", any_image, false)
commands = {}
chip_ui.panels(battle(), true, MOVE_MENU)
check("bottom rows draw without a sheet", bottom_view():sub(1, 2), "x4")
commands = {}
chip_ui.card(battle(), true)
check("chip card draws", #commands > 0, true)
check("atk and def stay text in the card", top_view():find("ATK-1 DEF+1", 1, true) ~= nil, true)
fresh_doubles()
commands = {}
chip_ui.panels(battle(), true, ACTION_MENU)
check("doubles chips draw", #commands > 0, true)
have_sheet = true
ui = require("pbi.ui").new(game, reader)

-- Main script: reset rules ----------------------------------------------------------------------------------------

local chunk = assert(loadfile(root .. "/battle_info.lua"))
local function start(options, code)
  melon.options = options or {}
  chunk()
  on_load(code or "IRDO")
end

local function frame()
  commands = {}
  on_frame_end()
  return #commands
end

start()
fresh_singles()
w32(INPUT + 0x58, 0)
check("nothing before the first menu", frame(), 0)
held = melon.KEY.L
check("no card before the first menu", frame(), 0)
held = 0
w32(INPUT + 0x58, 1)
check("panels at the action menu", frame() > 0, true)
w32(INPUT + 0x58, 0)
check("nothing in animations", frame(), 0)
held = melon.KEY.L
check("card in animations", frame() > 0, true)
held = 0
w32(CORE, 0)
check("nothing when the chain drops", frame(), 0)
w32(CORE, MAIN)
check("still nothing after it comes back", frame(), 0)
w32(INPUT + 0x58, 1)
check("armed again", frame() > 0, true)
on_state_loaded()
w32(INPUT + 0x58, 0)
check("state load resets", frame(), 0)

-- The script tells the panels which menu is up. Holding the card hides them on both screens.
on_state_loaded()
w32(INPUT + 0x58, 1)
frame()
check("panels at the action menu's spot", drawn("bottom", "x4").y, 88)
check("bar types without the card", drawn("top", "WAT").y, 40.5)
held = melon.KEY.L
frame()
check("the card hides the bottom rows", bottom_view(), "")
local on_the_bar = 0
for _, d in ipairs(draws("top")) do
  if d.y == 40.5 then on_the_bar = on_the_bar + 1 end
end
check("the card hides the bar types", on_the_bar, 0)
held = 0
w32(INPUT + 0x58, 2)
frame()
check("panels at the move menu's spot", drawn("bottom", "x4").y, 146)
w32(INPUT + 0x58, 3)
frame()
check("panels at target select's spot", drawn("bottom", "x4").y, 146)

-- Arming: any menu screen, so a load on the move screen shows tags straight away. These look at the pills alone, not at the rows
-- that share the bottom screen. With the sheet, only a pill draws a rect of this height.
local function pills_drawn()
  local n = 0
  for _, c in ipairs(commands) do
    if c.screen == "bottom" and c.kind == "rect" and c.args[4] == 8 then n = n + 1 end
  end
  return n
end
on_state_loaded()
w32(INPUT + 0x58, 2)
frame()
check("armed by move select", pills_drawn() > 0, true)
on_state_loaded()
w32(INPUT + 0x58, 3)
check("armed by target select", frame() > 0, true)
on_state_loaded()
w32(INPUT + 0x58, 4)
held = melon.KEY.L
check("armed by yes/no", frame() > 0, true)
held = 0
on_state_loaded()
w32(INPUT + 0x58, 5)
check("not armed by other screens", frame(), 0)

-- Move tags only when move select is really up
w32(INPUT + 0x58, 2)
frame()
check("tags at move select", pills_drawn() > 0, true)
w32(INPUT + 0x68, 1)
frame()
check("no tags while busy", pills_drawn(), 0)
w32(INPUT + 0x68, 0)
w32(INPUT + 0x70, 2)
frame()
check("no tags once a button is pressed", pills_drawn(), 0)
w32(INPUT + 0x70, 0xFFFFFFFF)

-- Triples: no panels or tags, the card still works
w32(INPUT + 0x58, 1)
frame()
fresh_triples()
w32(INPUT + 0x58, 2)
check("no panels or tags in triples", frame(), 0)
held = melon.KEY.L
check("card in triples", frame() > 0, true)
held = 0

-- Options
fresh_singles()
start({ show_tags = false })
check("show_tags off", frame(), 0)
start({ show_move_tags = false })
w32(INPUT + 0x58, 1)
frame()
w32(INPUT + 0x58, 2)
frame()
check("show_move_tags off", pills_drawn(), 0)
start({ card_button = "X" })
w32(INPUT + 0x58, 1)
frame()
held = melon.KEY.L
local with_l = frame()
held = melon.KEY.X
check("card follows its option", frame() > with_l, true)
held = 0
start({ show_foe_hidden = false })
frame()
held = melon.KEY.L
frame()
check("foe hp hidden", top_view():find("89/148", 1, true), nil)
check("foe ability hidden", top_view():find("Intimidate", 1, true), nil)
check("your hp stays", top_view():find("131/164", 1, true) ~= nil, true)
held = 0

-- Another game does nothing
start({}, "ADAE")
fresh_singles()
check("other game draws nothing", frame(), 0)

-- The foe's HP numbers ----------------------------------------------------------------------------------------------

do
local EFFECT_POINTER, EFFECT, GAUGE = 0x021F4280, 0x02294BE8, 0x022A8454
local NAME_ACTORS, BAR_ACTORS = 0x022A9000, 0x022AA000
local ACTOR_VISIBLE = 1 << 26

--- A foe box as the game keeps it, at view position view. Options change one field each, for what has to hide it.
local function set_box(view, hp, max_hp, x, y, fields)
  fields = fields or {}
  w32(EFFECT_POINTER, EFFECT)
  w32(EFFECT + 0x1E4, fields.main or MAIN)
  w32(EFFECT + 0x1A4, GAUGE)
  local entry = GAUGE + 0x40 + view * 0x84
  local name, bar = NAME_ACTORS + view * 0x80, BAR_ACTORS + view * 0x80
  w32(entry, fields.no_name and 0 or name)
  w32(entry + 8, bar)
  local box = entry + 0x40
  w32(box, fields.style or 0)
  w32(box + 4, fields.side or 1)
  w32(box + 8, hp)
  w32(box + 0x0C, max_hp)
  w32(box + 0x10, fields.anim or 0)
  w32(box + 0x28, fields.delta or 0)
  w32(box + 0x30, (fields.moving and 1 or 0) | (fields.hidden and 0 or 8))
  w32(name + 0x60, fields.name_off and 0 or ACTOR_VISIBLE)
  w32(bar + 0x60, fields.bar_off and 0 or ACTOR_VISIBLE)
  w16(name + 0x0C, x)
  w16(name + 0x0E, y)
end

--- The white text of the HP numbers drawn this frame: each { text, x, y }. Everything else stays out of the way: no menu in these frames.
local function hp_numbers_now()
  commands = {}
  on_frame_end()
  local out = {}
  for _, c in ipairs(commands) do
    if c.kind == "text" and c.screen == "top" and c.args[3]:find("^%d+/%d+$") and c.args[4] == 0xFFFFFFFF then
      out[#out + 1] = { text = c.args[3], x = c.args[1], y = c.args[2] }
    end
  end
  return out
end

--- The same with the box left as it is for long enough: what's drawn lags the box by two frames
local function hp_numbers()
  on_frame_end()
  on_frame_end()
  return hp_numbers_now()
end

local function hp_text()
  local shown = hp_numbers()
  return shown[1] and shown[1].text or nil
end

local function foe_hp_singles(fields, hp, max_hp)
  fresh_singles()
  w32(INPUT + 0x58, 0) -- no menu, so only the numbers draw
  set_box(1, hp or 89, max_hp or 148, 60, 40, fields)
end

start()
foe_hp_singles()
local at = hp_numbers()
check("foe hp text", at[1] and at[1].text, "89/148")
check("foe hp shows with no menu and before the first one", #at, 1)
check("foe hp ends at the bar's end", at[1].x + 29, 92) -- "89/148" is 29 wide
check("foe hp sits under the bar", at[1].y, 46)
local dark = 0
for _, c in ipairs(commands) do
  if c.kind == "text" and c.args[4] == 0xFF101418 then dark = dark + 1 end
end
check("foe hp has an outline", dark, 8)

-- It follows the box as it slides or shakes
foe_hp_singles()
set_box(1, 89, 148, 52, 41)
at = hp_numbers()
check("foe hp follows the box across", at[1].x + 29, 84)
check("foe hp follows the box down", at[1].y, 47)

-- What has to hide it. Each of these leaves the box in place otherwise.
local hidden_by = {
  ["the box isn't shown"] = { hidden = true },
  ["the name is hidden"] = { name_off = true },
  ["the bar is hidden"] = { bar_off = true },
  ["no actor"] = { no_name = true },
  ["another battle's work"] = { main = 0x02200000 },
  ["triple style box"] = { style = 2 },
  ["your side's box"] = { side = 0 },
  ["moving HP past the max"] = { moving = true, anim = 149 << 8, delta = 3 },
}
for name, fields in pairs(hidden_by) do
  foe_hp_singles(fields)
  check("foe hp hides: " .. name, hp_text(), nil)
end
foe_hp_singles(nil, 89, 150)
check("foe hp hides: max HP isn't the record's", hp_text(), nil)
foe_hp_singles(nil, 0)
check("foe hp hides: nothing left", hp_text(), nil)
foe_hp_singles(nil, 149)
check("foe hp hides: HP past max", hp_text(), nil)
foe_hp_singles()
w32(EFFECT_POINTER, 0)
check("foe hp hides: no effect work", hp_text(), nil)
foe_hp_singles()
w32(EFFECT + 0x1A4, 0)
check("foe hp hides: no gauge", hp_text(), nil)
foe_hp_singles()
set_box(1, 89, 148, 0xFF80, 40) -- off to the left, like a hidden box
check("foe hp hides: box off the screen", hp_text(), nil)
foe_hp_singles()
w32(CORE, 0)
check("foe hp hides: no chain", hp_text(), nil)
w32(CORE, MAIN)
foe_hp_singles()
w32(MAIN + 0x460, 0xFFFFFFFF)
check("foe hp hides: the exit's fade", hp_text(), nil)

-- The value counts with the bar, not the record. Numbers from a real hit: 14 max HP, 14 to 11, in 1/256ths.
local function box_value(hp, max_hp, anim, delta)
  w16(RECS.foe1 + 0x0E, max_hp)
  w16(RECS.foe1 + 0x10, hp - delta)
  set_box(1, hp, max_hp, 60, 40, { moving = true, anim = anim, delta = delta })
  return hp_text()
end
foe_hp_singles()
check("before the bar starts", box_value(14, 14, 0x80000000, 3), "14/14")
check("first step down rounds up", box_value(14, 14, 0xD6C, 3), "14/14")
check("mid drain", box_value(14, 14, 0xC44, 3), "13/14")
check("whole number", box_value(14, 14, 0xB00, 3), "11/14")
check("draining heals round down", box_value(10, 14, 0xB80, -3), "11/14")
check("48 max HP and up moves in whole HP", box_value(100, 148, 73, 50), "73/148")
check("47 max HP is still 1/256ths", box_value(47, 47, 0x2E40, 5), "47/47")
-- After the drain the box has the new HP and its moving HP is zero
foe_hp_singles()
w16(RECS.foe1 + 0x10, 80)
set_box(1, 80, 148, 60, 40)
check("finished bar", hp_text(), "80/148")
-- The record leads the bar: the bar's what shows
foe_hp_singles()
w16(RECS.foe1 + 0x10, 40)
check("record ahead of the bar", hp_text(), "89/148")
-- A drain to nothing shows until the bar's there, while the record has been 0 all along
foe_hp_singles()
w16(RECS.foe1 + 0x10, 0)
set_box(1, 5, 148, 60, 40, { moving = true, anim = 2, delta = 5 })
check("draining to a faint", hp_text(), "2/148")
set_box(1, 5, 148, 60, 40, { moving = true, anim = 0, delta = 5 })
check("nothing once the bar's empty", hp_text(), nil)

-- Illusion: a disguised foe shows its HP as the disguise would, on the box and in the card. One that can't be read shows nothing.
local function card_shows(text)
  -- The card waits for the first menu of the battle
  w32(INPUT + 0x58, 1)
  on_frame_end()
  w32(INPUT + 0x58, 0)
  held = melon.KEY.L
  commands = {}
  on_frame_end()
  held = 0
  for _, c in ipairs(commands) do
    if c.kind == "text" and c.args[3]:find(text, 1, true) then return true end
  end
  return false
end

local function disguised_box(real_hp, real_max, disguise_max, fields)
  on_state_loaded() -- forgets the disguise it read last time
  disguised_foe()
  put_entry(0x12345678, 130, 0, false, 22, disguise_max)
  w32(INPUT + 0x58, 0)
  w16(RECS.foe1 + 0x0E, real_max)
  w16(RECS.foe1 + 0x10, real_hp)
  set_box(1, real_hp, real_max, 60, 40, fields)
end

start()
foe_hp_singles()
check("plain foe: box numbers", hp_text(), "89/148")
check("plain foe: card numbers", card_shows("HP 89/148"), true)

-- Scaled in both directions: a disguise with more HP than the real mon, and with less
disguised_box(50, 100, 300)
check("bigger disguise: box", hp_text(), "150/300")
check("bigger disguise: card", card_shows("HP 150/300"), true)
check("bigger disguise: still the disguise", card_shows("Gyarados"), true)
check("bigger disguise: no real species", card_shows("Zoroark"), false)
disguised_box(50, 100, 37)
check("smaller disguise rounds up: box", hp_text(), "19/37")
check("smaller disguise rounds up: card", card_shows("HP 19/37"), true)
-- Both ends
disguised_box(100, 100, 37)
check("full stays full", hp_text(), "37/37")
disguised_box(1, 714, 11)
check("1 HP left shows at least 1", hp_text(), "1/11")
check("1 HP left in the card", card_shows("HP 1/11"), true)
disguised_box(713, 714, 11)
check("almost full can round up to full", hp_text(), "11/11")
-- 0 is a faint: the box hides, and so does the card's HP
disguised_box(0, 100, 37)
check("nothing at 0", hp_text(), nil)
-- The real max has to match the record's, not the disguise's
disguised_box(50, 100, 300)
set_box(1, 50, 300, 60, 40)
check("the box's max is checked against the real one", hp_text(), nil)

-- It moves with the bar, not the record: a drain and a heal through the real 14 max HP bar, shown as 50 max HP
local function scaled_value(hp, anim, delta)
  disguised_box(hp, 14, 50, { moving = true, anim = anim, delta = delta })
  return hp_text()
end
check("drain starts where it was", scaled_value(14, 0x80000000, 3), "50/50")
check("mid drain, 13 of 14", scaled_value(14, 0xC44, 3), "47/50")
check("end of the drain, 11 of 14", scaled_value(14, 0xB00, 3), "40/50")
check("mid heal, 11 of 14", scaled_value(10, 0xB80, -3), "40/50")

-- The party flag says the stats are plain. The box flag alone doesn't.
disguised_foe()
put_entry(0x12345678, 130, 0, true, 22, 300, true)
w32(INPUT + 0x58, 0)
set_box(1, 89, 148, 60, 40)
check("plain battle stats", battle().foes[1].max_hp, 300)
reader.reset()
disguised_foe()
put_entry(0x12345678, 130, 0, true, 22, 300, false)
check("box flag alone, stats still enciphered", battle().foes[1].max_hp, 300)
reader.reset()

-- A max HP that can't be real makes the entry unreadable: 0, and past the top
for _, bad in ipairs({ 0, 715 }) do
  disguised_foe()
  put_entry(0x12345678, 130, 0, false, 22, bad)
  check("bad max HP " .. bad .. ": no info", battle().foes[1].no_info, true)
  reader.reset()
end
disguised_foe()
put_entry(0x12345678, 130, 0, false, 22, 714)
check("the top max HP is fine", battle().foes[1].max_hp, 714)
reader.reset()

-- Unreadable: nothing on the box or the card
foe_hp_singles()
w8(RECS.foe1 + 0x1B, 0x40)
w32(RECS.foe1 + 0x04, 0)
check("unreadable disguise: no box numbers", hp_text(), nil)
check("unreadable disguise: no card numbers", card_shows("HP 89/148"), false)
check("unreadable disguise: no real species", card_shows("Zoroark"), false)

-- The read is kept: writing over the entry later changes nothing, so it's not read each frame
disguised_box(50, 100, 300)
check("kept: first read", hp_text(), "150/300")
put_entry(0x12345678, 130, 0, false, 22, 37)
check("kept: same after", hp_text(), "150/300")

-- The disguise breaking: the real numbers take over, and the card with them
local function now_numbers()
  local shown = hp_numbers_now()
  return shown[1] and shown[1].text or nil
end
disguised_box(50, 100, 300)
hp_text()
w8(RECS.foe1 + 0x1B, 0)
check("breaking: what's waiting is the disguise's", now_numbers(), "150/300")
check("breaking: and the next", now_numbers(), "150/300")
check("broken: the real numbers", now_numbers(), "50/100")
check("broken: real numbers in the card", card_shows("HP 50/100"), true)
check("broken: real species", card_shows("Zoroark"), true)

-- Real numbers read just before the disguise went on can't flash, box or no box
disguised_box(50, 100, 300)
w8(RECS.foe1 + 0x1B, 0)
hp_text()
w8(RECS.foe1 + 0x1B, 0x40)
check("flag on: the real numbers waiting don't show", now_numbers(), nil)
check("flag on: nor the next", now_numbers(), nil)
check("flag on: then the disguise's", now_numbers(), "150/300")
w8(RECS.foe1 + 0x1B, 0)
hp_text()
w8(RECS.foe1 + 0x1B, 0x40)
set_box(1, 50, 100, 60, 40, { hidden = true })
check("flag on, box gone: nothing", now_numbers(), nil)
check("flag on, box gone: nothing next", now_numbers(), nil)
check("flag on, box gone: still nothing", now_numbers(), nil)

-- Doubles: only the disguised foe's numbers scale
fresh_doubles()
start()
w32(INPUT + 0x58, 0)
w16(RECS.foe2 + 0x10, 74)
w8(RECS.foe2 + 0x1B, 0x40)
w32(RECS.foe2 + 0x04, ENTRY)
put_entry(0x12345678, 130, 0, false, 22, 300)
set_box(3, 89, 148, 64, 28)
set_box(5, 74, 148, 60, 52)
local both = hp_numbers()
check("doubles: upper foe's real", both[1].text, "89/148")
check("doubles: lower foe's scaled", both[2].text, "150/300")
-- And an unreadable one leaves the other alone
w32(RECS.foe2 + 0x04, 0)
local one_left = hp_numbers_now()
check("doubles: unreadable goes at once, the other stays", #one_left, 1)
check("doubles: and it's the upper box", one_left[1].y, 34)
local listed = reader.foe_boxes(reader.chain())
check("reader: unreadable has no HP", listed[2].hp, nil)
check("reader: says which", listed[2].slot, 2)
check("reader: says unreadable", listed[2].unreadable, true)

-- Doubles: each box, upper first, and only the one that's up
fresh_doubles()
w32(INPUT + 0x58, 0)
w16(RECS.foe2 + 0x10, 70)
set_box(3, 89, 148, 64, 28)
set_box(5, 70, 148, 60, 52)
at = hp_numbers()
check("both foes", #at, 2)
check("upper box is the first foe", at[1].text, "89/148")
check("upper y", at[1].y, 34)
check("lower box is the second foe", at[2].text, "70/148")
check("lower y", at[2].y, 58)
set_box(3, 89, 148, 64, 28, { hidden = true })
at = hp_numbers()
check("only the box that's up", at[1] and at[1].text, "70/148")
check("one box", #at, 1)
-- Triples, rotation and multi
w32(INPUT + 0x50, 2)
check("triples: none", hp_text(), nil)
w32(INPUT + 0x50, 3)
check("rotation: none", hp_text(), nil)
w32(INPUT + 0x50, 1)
set_box(3, 89, 148, 64, 28)
w32(P + 3 * 0x1C, RECS.foe2)
w8(P + 3 * 0x1C + 0x18, 6)
check("multi: none", hp_text(), nil)

-- Clear of the type icons and the rows under the box
foe_hp_singles()
set_mon(RECS.foe1, 12, 130, "WAT", "FLY", 22, { 16 })
w16(RECS.foe1 + 0x0E, 748)
w16(RECS.foe1 + 0x10, 748)
set_box(1, 748, 748, 60, 40)
at = hp_numbers()
check("widest numbers clear the type icons", at[1].x - 1 > game.layout.foe_bar_types.right, true)
check("numbers clear the row under the box", at[1].y + 7 + 1 <= game.layout.foe_box[2] + game.layout.foe_box[4] + 1, true)
fresh_doubles()
w32(INPUT + 0x58, 0)
set_box(5, 89, 148, 60, 52)
at = hp_numbers()
check("numbers clear the row under the lower box", at[1].y + 7 + 1 <= game.layout.lower_foe_box[2] + game.layout.lower_foe_box[4] + 1, true)
for i, types in ipairs(game.layout.doubles_bar_types) do
  check("doubles numbers clear the type icons " .. i, at[1].x - 1 > types.right, true)
end

-- The numbers lag the box's state by two frames, as the picture does, coming and going and while the bar moves
start()
foe_hp_singles(nil, 89)
local function now_text()
  local shown = hp_numbers_now()
  return shown[1] and shown[1].text or nil
end
set_box(1, 89, 148, 60, 40, { hidden = true })
now_text()
now_text()
now_text()
check("box not up yet", now_text(), nil)
set_box(1, 89, 148, 60, 40)
check("box up, frame 1 of the state", now_text(), nil)
check("box up, frame 2", now_text(), nil)
check("box up, frame 3 is in the picture", now_text(), "89/148")
set_box(1, 88, 148, 60, 40)
check("value lags too, frame 1", now_text(), "89/148")
check("value lags too, frame 2", now_text(), "89/148")
check("value lags too, frame 3", now_text(), "88/148")
set_box(1, 88, 148, 60, 40, { hidden = true })
check("box going, still in the picture, frame 1", now_text(), "88/148")
check("box going, still in the picture, frame 2", now_text(), "88/148")
check("box gone from the picture", now_text(), nil)
-- A state load or a lost chain forgets what's waiting
set_box(1, 89, 148, 60, 40)
now_text()
now_text()
check("up again", now_text(), "89/148")
on_state_loaded()
check("nothing right after a state load", now_text(), nil)
check("nor the next frame", now_text(), nil)
check("back on the third", now_text(), "89/148")
w32(CORE, 0)
check("nothing with no chain", now_text(), nil)
w32(CORE, MAIN)
check("nothing right after the chain's back", now_text(), nil)
check("nor the next frame after it", now_text(), nil)
check("back on the third after it", now_text(), "89/148")

-- Options
foe_hp_singles()
start({ show_foe_hp = false })
check("show_foe_hp off", hp_text(), nil)
start({ show_foe_hidden = false })
check("show_foe_hidden off hides it too", hp_text(), nil)
start({})
check("on by default", hp_text(), "89/148")
-- Nothing's kept from frame to frame, so a state load starts right
on_state_loaded()
check("after a state load", hp_text(), "89/148")
-- A game with no effect pointer draws none
local saved_pointer = game.effect_pointer
game.effect_pointer = nil
check("no pointer for this game", hp_text(), nil)
game.effect_pointer = saved_pointer
end

-- Damage numbers ----------------------------------------------------------------------------------------------------

local CLIENT, QUEUE = 0x02290000, 0x02291000
local CMD_DAMAGE, CMD_DAMAGE_PLURAL, CMD_GAUGE, CMD_EXP = 50, 51, 65, 69
local ARGS_END = 0xFFFF0000
local WHITE_NUMBER, YELLOW_NUMBER, GREEN_NUMBER = 0xFFFFFFFF, 0xFFFFE040, 0xFF60E060

--- The queue from the read position on. Each item is { command, message, args... }.
local function set_queue(command, items)
  w32(CORE + 4, CLIENT)
  w32(CLIENT, MAIN)
  w32(CLIENT + 0x13C, QUEUE)
  w32(CLIENT + 0x180, command)
  local read = 20
  local pos = read
  local function put16(v)
    w8(QUEUE + 8 + pos, v >> 8)
    w8(QUEUE + 9 + pos, v)
    pos = pos + 2
  end
  local function put32(v)
    put16(v >> 16)
    put16(v & 0xFFFF)
  end
  for _, item in ipairs(items or {}) do
    put16(item[1])
    if item[2] then
      put16(item[2])
      for i = 3, #item do put32(item[i]) end
      put32(ARGS_END)
    end
  end
  w32(QUEUE, pos)
  w32(QUEUE + 4, read)
end

local numbers_chunk = assert(loadfile(root .. "/damage_numbers.lua"))
local function start_numbers(options, code)
  melon.options = options or {}
  numbers_chunk()
  on_load(code or "IRDO")
end

--- Runs a frame and gives back the numbers drawn: the top-layer text of each, skipping its outline
local function number_frame()
  commands = {}
  on_frame_end()
  local out = {}
  for _, c in ipairs(commands) do
    check("numbers are text on the top screen", c.kind == "text" and c.screen == "top", true)
    local color = c.args[4] & 0xFFFFFFFF
    if color & 0xFFFFFF ~= 0x101418 then
      out[#out + 1] = { x = c.args[1], y = c.args[2], text = c.args[3], color = color, size = c.args[6] }
    end
  end
  return out
end

--- A hit on a record, during that command, with that queue after it. Gives back the one new number, or nil.
local function hit(rec, loss, command, items)
  set_queue(command, items)
  w16(rec + 0x10, r16(rec + 0x10) - loss)
  local shown = number_frame()
  return shown[#shown]
end

local function clear_numbers()
  on_state_loaded()
  number_frame()
end

start_numbers()
fresh_singles()
set_queue(CMD_GAUGE)
check("first frame shows nothing", #number_frame(), 0)
check("no change shows nothing", #number_frame(), 0)

local plain = hit(RECS.foe1, 12, CMD_DAMAGE_PLURAL)
check("plain hit", plain.text, "12")
check("plain hit is white", plain.color, WHITE_NUMBER)
check("default size", plain.size, 2)
local anchor = game.layout.damage_numbers.singles.foes[1]
check("number is centered on the foe's anchor", plain.x + 8, anchor[1]) -- "12" is 16 wide at size 2
check("number starts at the foe's anchor", plain.y, anchor[2])
local outlined = 0
for _, c in ipairs(commands) do
  if c.args[4] & 0xFFFFFF == 0x101418 then outlined = outlined + 1 end
end
check("number has an outline", outlined, 8)

-- It rises, fades over the last 15 frames, and goes after 45
local rising = number_frame()[1]
check("number rises", rising.y < plain.y, true)
check("number starts opaque", rising.color >> 24, 0xFF)
local last_seen
for _ = 3, 45 do
  last_seen = number_frame()[1]
end
check("number still there on its last frame", last_seen ~= nil, true)
check("number almost faded on its last frame", last_seen.color >> 24 < 0x20, true)
check("number rose about 20", plain.y - last_seen.y > 18 and plain.y - last_seen.y <= 20, true)
check("number gone after its life", #number_frame(), 0)

check("crit in singles", hit(RECS.foe1, 5, CMD_DAMAGE_PLURAL, { { 90, 0x51 } }).color, YELLOW_NUMBER)
clear_numbers()
check("crit after effectiveness", hit(RECS.foe1, 5, CMD_DAMAGE, { { 90, 0x4E }, { 90, 0x51 } }).color, YELLOW_NUMBER)
clear_numbers()
check("crit message after another command is another hit", hit(RECS.foe1, 5, CMD_DAMAGE_PLURAL, { { 57 }, { 90, 0x51 } }).color, WHITE_NUMBER)
clear_numbers()
check("crit message after another message", hit(RECS.foe1, 5, CMD_DAMAGE_PLURAL, { { 90, 0x99 }, { 90, 0x51 } }).color, WHITE_NUMBER)
clear_numbers()
check("drop outside a hit isn't a crit", hit(RECS.foe1, 5, CMD_GAUGE, { { 90, 0x51 } }).color, WHITE_NUMBER)
clear_numbers()

-- A queue that doesn't look right reads as no crit, not an error
local function broken_queue(name, breaks)
  set_queue(CMD_DAMAGE_PLURAL, { { 90, 0x51 } })
  breaks()
  w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) - 1)
  local shown = number_frame()
  check(name, shown[#shown].color, WHITE_NUMBER)
  clear_numbers()
  w32(CLIENT, MAIN)
  w32(CLIENT + 0x13C, QUEUE)
end
broken_queue("freed client", function() w32(CLIENT, 0) end)
broken_queue("no queue", function() w32(CLIENT + 0x13C, 0) end)
broken_queue("read past write", function() w32(QUEUE + 4, r32(QUEUE) + 1) end)
broken_queue("write past the end", function() w32(QUEUE, 3001) end)
broken_queue("cut-off message", function() w32(QUEUE, 22) end)
-- Message args that never end
set_queue(CMD_DAMAGE_PLURAL, { { 91, 6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, { 90, 0x51 } })
w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) - 1)
check("endless args", number_frame()[1].color, WHITE_NUMBER)
clear_numbers()

-- Heals
set_queue(CMD_GAUGE)
number_frame()
w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) + 7)
local healed = number_frame()[1]
check("heal text", healed.text, "7")
check("heal is green", healed.color, GREEN_NUMBER)
clear_numbers()

-- Your side
local my_hit = hit(RECS.me1, 3, CMD_DAMAGE_PLURAL)
check("your side's anchor", my_hit.y, game.layout.damage_numbers.singles.mine[1][2])
clear_numbers()

-- Level-up: both rise, no heal. The command alone is enough too.
set_queue(CMD_EXP)
w16(RECS.me1 + 0x10, r16(RECS.me1 + 0x10) + 2)
w16(RECS.me1 + 0x0E, r16(RECS.me1 + 0x0E) + 2)
check("max HP change is quiet", #number_frame(), 0)
set_queue(CMD_GAUGE)
w16(RECS.me1 + 0x10, r16(RECS.me1 + 0x10) + 2)
w16(RECS.me1 + 0x0E, r16(RECS.me1 + 0x0E) + 2)
check("max HP change alone is quiet", #number_frame(), 0)
set_queue(CMD_EXP)
w16(RECS.me1 + 0x10, r16(RECS.me1 + 0x10) - 2)
check("exp command is quiet", #number_frame(), 0)
clear_numbers()

-- A switch: another mon in that place, with other HP, says nothing
set_queue(CMD_GAUGE)
number_frame()
set_mon(RECS.me2, 1, 183, "WAT", nil, 47, { 55 })
w32(P, RECS.me2)
check("switch is quiet", #number_frame(), 0)
w16(RECS.me2 + 0x10, r16(RECS.me2 + 0x10) - 4)
check("the new mon counts from then on", number_frame()[1].text, "4")
w32(P, RECS.me1)
clear_numbers()

-- Crowding depends on real overlap at the text size: hits the way a multi-hit move lands them, and right after each other
local function overlaps(shown, size)
  local h = size * 7 + 2 - 0.001 -- touching is fine, and rounding can make it look a hair less
  for i = 1, #shown do
    for j = i + 1, #shown do
      if shown[i].y < shown[j].y + h and shown[j].y < shown[i].y + h then return true end
    end
  end
  return false
end

for size = 1, 3 do
  for _, gap in ipairs({ 1, 5, 20, 34, 44 }) do
    start_numbers({ text_size = size })
    fresh_singles()
    set_queue(CMD_GAUGE)
    number_frame()
    local clear = true
    for _ = 1, 4 do
      local shown = hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL)
      clear = clear and shown ~= nil
      for _ = 1, gap do
        clear = clear and not overlaps(number_frame(), size)
      end
    end
    check("numbers clear at size " .. size .. " every " .. gap .. " frames", clear, true)
  end
end

-- At 34 frames a number has risen 15: enough to clear size 1 with no push, not size 3. Size 2 is 16 high, so just short too.
local function second_hit_start(size)
  start_numbers({ text_size = size })
  fresh_singles()
  set_queue(CMD_GAUGE)
  number_frame()
  local first = hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL)
  for _ = 1, 33 do number_frame() end
  return hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL).y - first.y
end
check("no push when already clear", second_hit_start(1), 0)
check("pushed down at size 2", second_hit_start(2) > 0, true)
check("pushed further at size 3", second_hit_start(3) > second_hit_start(2), true)

-- Numbers at other places don't push
start_numbers()
fresh_doubles()
set_queue(CMD_GAUGE)
number_frame()
hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL)
check("another place doesn't push", hit(RECS.foe2, 1, CMD_DAMAGE_PLURAL).y, game.layout.damage_numbers.doubles.foes[2][2])
start_numbers()
fresh_singles()
set_queue(CMD_GAUGE)
number_frame()

-- A record that goes unusable and comes back starts over, however much changed meanwhile
local foe_species = r16(RECS.foe1 + 0x0C)
w16(RECS.foe1 + 0x0C, 0)
check("unusable record draws nothing", #number_frame(), 0)
w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) - 30)
w16(RECS.foe1 + 0x0C, foe_species)
check("no number covering the gap", #number_frame(), 0)
check("a hit right after still shows", hit(RECS.foe1, 2, CMD_DAMAGE_PLURAL).text, "2")
clear_numbers()
-- The other mon isn't affected, and a hit on it in that gap shows
w16(RECS.foe1 + 0x0C, 0)
number_frame()
check("hit on the other side during a gap", hit(RECS.me1, 3, CMD_DAMAGE_PLURAL).text, "3")
w16(RECS.foe1 + 0x0C, foe_species)
clear_numbers()
-- A foe client that isn't there yet is a gap too
w8(P + 0x1C + 0x18, 0)
number_frame()
w8(P + 0x1C + 0x18, 6)
w16(RECS.me1 + 0x10, r16(RECS.me1 + 0x10) - 5)
check("no number after the whole list was empty", #number_frame(), 0)
clear_numbers()

-- A state load and a lost chain clear what's live and what's known
on_state_loaded()
check("state load clears numbers", #number_frame(), 0)
number_frame()
hit(RECS.foe1, 1, CMD_GAUGE)
on_state_loaded()
w16(RECS.foe1 + 0x10, 40)
check("no number from before a state load", #number_frame(), 0)
hit(RECS.foe1, 1, CMD_GAUGE)
w32(CORE, 0)
check("lost chain draws nothing", #number_frame(), 0)
w32(CORE, MAIN)
w16(RECS.foe1 + 0x10, 20)
check("no number from before a lost chain", #number_frame(), 0)

-- Doubles: each target gets its own place and its own crit
fresh_doubles()
start_numbers({ text_size = 3 })
set_queue(CMD_GAUGE)
number_frame()
w16(RECS.foe1 + 0x10, 80)
w16(RECS.foe2 + 0x10, 70)
set_queue(CMD_DAMAGE_PLURAL, { { 91, 9, 13 }, { 91, 0x180, 13 } })
local both = number_frame()
check("spread hits two", #both, 2)
check("first foe isn't the crit", both[1].color, WHITE_NUMBER)
check("second foe is the crit", both[2].color, YELLOW_NUMBER)
check("size option", both[1].size, 3)
check("doubles anchors", both[1].y, game.layout.damage_numbers.doubles.foes[1][2])
check("doubles foes are apart", both[1].x ~= both[2].x, true)
clear_numbers()
check("crit message for the other mon only", hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL, { { 91, 0x180, 13 } }).color, WHITE_NUMBER)
clear_numbers()
w16(RECS.foe2 + 0x10, 60)
w16(RECS.foe1 + 0x10, 60)
set_queue(CMD_DAMAGE_PLURAL, { { 91, 0x180, 12 }, { 91, 0x180, 13 } })
number_frame()
w16(RECS.foe2 + 0x10, 50)
w16(RECS.foe1 + 0x10, 50)
both = number_frame()
check("both crit", both[#both].color == YELLOW_NUMBER and both[#both - 1].color == YELLOW_NUMBER, true)
clear_numbers()

-- The most it costs: both sides hit on each of 6 frames, all still live
fresh_doubles()
start_numbers({ text_size = 3 })
set_queue(CMD_GAUGE)
number_frame()
local most_steps = 0
for _ = 1, 6 do
  for _, rec in ipairs({ RECS.foe1, RECS.foe2, RECS.me1, RECS.me2 }) do
    w16(rec + 0x10, r16(rec + 0x10) - 1)
  end
  set_queue(CMD_DAMAGE_PLURAL, { { 91, 9, 13 }, { 91, 0x180, 13 } })
  local steps = 0
  debug.sethook(function() steps = steps + 1 end, "", 100)
  number_frame()
  debug.sethook()
  most_steps = math.max(most_steps, steps * 100)
end
print(string.format("work: 24 live damage numbers %d instructions, %d commands (limits 1000000 and 4096)", most_steps, #commands))
check("numbers stay well inside the budget", most_steps < 100000, true)
clear_numbers()

-- Multi uses the doubles places, from clients 2 and 3
fresh_doubles()
w32(P + 2 * 0x1C, RECS.me2)
w8(P + 2 * 0x1C + 0x18, 6)
w32(P + 3 * 0x1C, RECS.foe2)
w8(P + 3 * 0x1C + 0x18, 6)
local multi = reader.active_hp(reader.chain())
check("multi mons", #multi, 4)
check("multi foe 2", multi[2].id, 13)
check("multi mine 2", multi[4].id, 1)
check("multi slot", multi[4].slot, 2)

-- A bad record is left out
fresh_singles()
w16(RECS.foe1 + 0x0C, 0)
local only_mine = reader.active_hp(reader.chain())
check("bad record left out", #only_mine, 1)
check("the other stays", only_mine[1].id, 0)

-- A disguised foe's numbers are in the disguise's HP
local function disguised_numbers(real_hp, real_max, disguise_max)
  start_numbers()
  disguised_foe()
  put_entry(0x12345678, 130, 0, false, 22, disguise_max)
  w16(RECS.foe1 + 0x0E, real_max)
  w16(RECS.foe1 + 0x10, real_hp)
  set_queue(CMD_GAUGE)
  number_frame()
end

disguised_numbers(100, 100, 300)
check("scaled damage", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "30")
check("scaled crit stays a crit", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL, { { 90, 0x51 } }).color, YELLOW_NUMBER)
set_queue(CMD_GAUGE)
w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) + 20)
local scaled_heal = number_frame()
check("scaled heal", scaled_heal[#scaled_heal].text, "60")
check("scaled heal is green", scaled_heal[#scaled_heal].color, GREEN_NUMBER)

-- They add up to what the box lost, rounding and all
disguised_numbers(97, 97, 300)
local lost = 0
for _, loss in ipairs({ 13, 7, 29, 1, 11 }) do
  lost = lost + tonumber(hit(RECS.foe1, loss, CMD_DAMAGE_PLURAL).text)
end
local real_now = r16(RECS.foe1 + 0x10)
check("numbers sum to the scaled drop", lost, 300 - ((real_now * 300 + 96) // 97))

-- Smaller than the real max: a hit too small to move the scaled HP still shows 1
disguised_numbers(700, 700, 20)
check("at least 1", hit(RECS.foe1, 1, CMD_DAMAGE_PLURAL).text, "1")

-- Unreadable: nothing for that foe, and the rest as usual
disguised_numbers(100, 100, 300)
w32(RECS.foe1 + 0x04, 0)
number_frame()
check("unreadable: no number", #number_frame(), 0)
w16(RECS.foe1 + 0x10, 90)
check("unreadable: nothing for a hit", #number_frame(), 0)
check("unreadable: your side as usual", hit(RECS.me1, 3, CMD_DAMAGE_PLURAL).text, "3")

-- The bit changing on the frame HP changes shows no number, either way, and the next hit counts in the new numbers
disguised_numbers(100, 100, 300)
w8(RECS.foe1 + 0x1B, 0)
w16(RECS.foe1 + 0x10, 60)
check("bit off with a hit: nothing", #number_frame(), 0)
check("bit off, then real numbers", hit(RECS.foe1, 5, CMD_DAMAGE_PLURAL).text, "5")
for _ = 1, 50 do number_frame() end
w8(RECS.foe1 + 0x1B, 0x40)
w16(RECS.foe1 + 0x10, 40)
check("bit on with a hit: nothing", #number_frame(), 0)
check("bit on, then the disguise's numbers", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "30")
-- With no HP change, the scale changing alone is nothing at all
for _ = 1, 50 do number_frame() end
w8(RECS.foe1 + 0x1B, 0)
check("bit off, no hit: nothing", #number_frame(), 0)

-- A state load and a lost chain forget the disguise that was read, so another at the same address counts in its own max HP
disguised_numbers(100, 100, 300)
check("before the load: the first disguise's numbers", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "30")
on_state_loaded()
put_entry(0x12345678, 130, 0, false, 22, 40)
set_queue(CMD_GAUGE)
number_frame()
check("after a state load: the new disguise's numbers", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "4")
disguised_numbers(100, 100, 300)
check("before the lost chain: the first disguise's numbers", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "30")
w32(CORE, 0)
number_frame()
w32(CORE, MAIN)
put_entry(0x12345678, 130, 0, false, 22, 40)
set_queue(CMD_GAUGE)
number_frame()
check("after a lost chain: the new disguise's numbers", hit(RECS.foe1, 10, CMD_DAMAGE_PLURAL).text, "4")

-- Doubles with one disguised
start_numbers()
fresh_doubles()
w8(RECS.foe2 + 0x1B, 0x40)
w32(RECS.foe2 + 0x04, ENTRY)
put_entry(0x12345678, 130, 0, false, 22, 296)
set_queue(CMD_GAUGE)
number_frame()
w16(RECS.foe1 + 0x10, r16(RECS.foe1 + 0x10) - 4)
w16(RECS.foe2 + 0x10, r16(RECS.foe2 + 0x10) - 4)
set_queue(CMD_DAMAGE_PLURAL)
local two = number_frame()
check("doubles: real foe real number", two[1].text, "4")
check("doubles: disguised foe scaled", two[2].text, "8")

-- Triples and rotation draw nothing
start_numbers()
fresh_doubles()
set_queue(CMD_GAUGE)
number_frame()
w32(INPUT + 0x50, 2)
w16(RECS.foe1 + 0x10, 10)
check("triples draw nothing", #number_frame(), 0)

-- Another game does nothing
start_numbers({}, "ADAE")
fresh_singles()
set_queue(CMD_GAUGE)
commands = {}
on_frame_end()
on_state_loaded()
check("numbers: other game draws nothing", #commands, 0)

-- The fake refuses I/O reads like the engine, and nothing above made one
check("the fake refuses 0x04xxxxxx", pcall(r8, 0x04000000), false)
forbidden_reads = forbidden_reads - 1
check("no forbidden reads", forbidden_reads, 0)

print(failures == 0 and "all passed" or (failures .. " failed"))
os.exit(failures == 0 and 0 or 1)
