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

local function put_entry(pid, species_id, form, plain, ability)
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
  w16(ENTRY + 4, plain and 2 or 0)
  w16(ENTRY + 6, sum)
  local key = sum
  for i = 0, 63 do
    key = (key * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
    w16(ENTRY + 8 + i * 2, plain and words[i] or (words[i] ~ (key >> 16)))
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

--- What's on the top screen in draw order: text, without its outline, and icon names
local function top_view()
  local out = {}
  for _, c in ipairs(commands) do
    if c.screen == "top" and c.kind == "text" and c.args[4] ~= DARK then
      out[#out + 1] = c.args[3]
    elseif c.screen == "top" and c.kind == "image" then
      local src = c.args[4].src
      out[#out + 1] = icon_at[src[1] .. "," .. src[2]] or "?icon"
    end
  end
  return table.concat(out, " ")
end

local function top_rects()
  local n = 0
  for _, c in ipairs(commands) do
    if c.screen == "top" and c.kind == "rect" then n = n + 1 end
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

-- Panels: icons, no background, approved row order
fresh_singles()
commands = {}
ui.panels(battle(), true)
check("panel view", top_view(), "WAT FLY x4 ELE x2 ROC x.5 FIR WAT FIG BUG STE x0 GRO FIR FIG x2 WAT GRO FLY PSY")
check("panels have no background", top_rects(), 0)
for _, c in ipairs(commands) do
  if c.kind == "image" then
    check("icons are one image pixel per overlay pixel", c.args[4].scale, 0.5)
  end
end
-- Row positions: foe panel from x=4, y=53, 10 apart; your panel ends at y=111 at x=142
local first_foe, last_mine = commands[1], commands[#commands]
check("foe panel x", first_foe.args[1], 4)
check("foe panel y", first_foe.args[2], 53)
local my_first
for _, c in ipairs(commands) do
  if c.args[1] == 142 then my_first = my_first or c end
end
check("your panel x", my_first.args[1], 142)
check("your panel top", my_first.args[2], 92)
check("your panel bottom", last_mine.args[2] + 9, 111)

-- x4 and x2 are separate groups on your panel and in the doubles rows
fresh_singles()
set_mon(RECS.me1, 0, 460, "GRA", "ICE", 117, { EMBER })
commands = {}
ui.panels(battle(), true)
check("your x4 and x2 are separate", top_view():find("x4 FIR x2 FIG", 1, true) ~= nil, true)
check("your x2 group is whole", top_view():find("x2 FIG POI FLY BUG ROC STE", 1, true) ~= nil, true)
no_top_past_right_edge()

-- Your ability counts in your rows, unless a shown foe's Mold Breaker bypasses it
fresh_singles()
set_mon(RECS.me1, 0, 479, "ELE", "GHO", LEVITATE, { EMBER })
set_mon(RECS.foe1, 12, 130, "WAT", "FLY", 22)
commands = {}
ui.panels(battle(), true)
local levitating = top_view()
check("your levitate row", levitating:match("ELE GHO (.*)$"):find("GRO", 1, true), nil)
set_mon(RECS.foe1, 12, 130, "WAT", "FLY", MOLD)
commands = {}
ui.panels(battle(), true)
check("shown mold breaker bypasses your levitate", top_view():match("ELE GHO (.*)$"):find("GRO", 1, true) ~= nil, true)
commands = {}
ui.panels(battle(), false)
check("hidden mold breaker doesn't", top_view():match("ELE GHO (.*)$"):find("GRO", 1, true), nil)
-- A foe's Scrappy counts in your rows like its Mold Breaker family, only when foe abilities are shown. Its Tinted Lens stays out.
set_mon(RECS.me1, 0, 94, "GHO", "POI", 26)
set_mon(RECS.foe1, 12, 130, "NOR", nil, SCRAPPY)
local function your_immune_row(show)
  commands = {}
  ui.card(battle(), show)
  local section = top_view():match("YOU .*$")
  return section:match("IMMUNE (.-) STRONG")
end
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
ui.panels(battle(), true)
check("disguised panel", top_view():sub(1, 7), "WAT FLY")
commands = {}
ui.card(battle(), true)
check("disguised card shows the disguise's name", top_view():find("Gyarados Lv42", 1, true) ~= nil, true)
check("disguised card hides the name", top_view():find("Zoroark", 1, true), nil)
check("disguised card hides the real ability", top_view():find("Illusion", 1, true), nil)
check("disguised card shows the disguise's ability", top_view():find("Intimidate", 1, true) ~= nil, true)
check("disguised card has no ???", top_view():find("???", 1, true), nil)

-- A disguise has to look exactly like a real foe of that species and ability, apart from HP
local function foe_views(show)
  local bt = battle()
  commands = {}
  ui.panels(bt, show)
  local panel = top_view()
  commands = {}
  ui.move_tags(bt, show)
  local tags = pills()
  commands = {}
  ui.card(bt, show)
  return panel, tags, (top_view():gsub("HP %d+/%d+", "HP"))
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

-- Disguise that can't be read: nothing about that foe, and no ??? anywhere
disguised_foe()
commands = {}
ui.move_tags(battle(), true)
check("no info: no pills", pills(), "")
commands = {}
ui.panels(battle(), true)
check("no info: no foe panel", top_view():sub(1, 3), "FIR")
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

fresh_doubles()
commands = {}
ui.move_tags(battle(), true)
check("doubles tags", pills(), "1x.5 2x2 1x0 2x2 1? 2?")
check("doubles no strips", strips(), 0)
commands = {}
ui.panels(battle(), true)
check("doubles rows", top_view(), "1 WAT FLY x4 ELE x2 ROC 2 GRO STE x2 FIR WAT FIG GRO")
check("doubles rows have no background", top_rects(), 0)
local row_y = {}
for _, c in ipairs(commands) do
  if c.kind == "image" then row_y[c.args[2]] = true end
end
check("doubles row 1 at 64", row_y[64], true)
check("doubles row 2 at 74", row_y[74], true)

fresh_doubles()
w16(RECS.foe1 + 0x10, 0)
commands = {}
ui.move_tags(battle(), true)
check("fainted foe has no pill", pills(), "2x2 2x2 2?")
commands = {}
ui.panels(battle(), true)
check("fainted foe has no row", top_view():sub(1, 1), "2")
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
ui.panels(battle(), true)
check("fainted disguised foe, no panel", top_view():find("DAR", 1, true), nil)
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
ui.panels(three_foes, true)
check("3 clients draw doubles rows", top_view():sub(1, 1), "1")
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
  ui.panels(bt, true)
  ui.move_tags(bt, true)
  ui.card(bt, true)
end)
check("crowded card fits", bottom_of_card() <= 192, true)
no_top_past_right_edge()
print(string.format("work: triple card %d instructions, %d commands; full doubles frame %d instructions, %d commands (limits 1000000 and 4096)", i_tri, c_tri, i_full, c_full))
check("full frame budget", i_full < 100000, true)
check("full frame commands", c_full < 1000, true)

-- Singles panel with the most resists: wraps to 2 rows of icons, and +N past that
fresh_singles()
set_mon(RECS.foe1, 12, 306, "STE", nil, 69)
commands = {}
ui.panels(battle(), true)
local steel = top_view()
check("steel panel lists its x.5s", steel:find("x.5 NOR", 1, true) ~= nil, true)
fresh_singles()
set_mon(RECS.foe1, 12, 292, "BUG", "GHO", WG)
commands = {}
ui.panels(battle(), true)
check("wonder guard immune row wraps whole", top_view():find("x0 NOR WAT ELE GRA ICE FIG POI GRO PSY BUG DRA STE", 1, true) ~= nil, true)
local wrapped = {}
for _, c in ipairs(commands) do
  if c.kind == "image" and c.args[1] < 142 and c.args[1] + c.args[4].w > 4 + 118 then wrapped[#wrapped + 1] = c end
end
check("foe panel stays in its width", #wrapped, 0)
no_top_past_right_edge()
-- Stage text is what overflows a first row: it ends in an outlined +N
fresh_singles()
for i = 0, 6 do w8(RECS.foe1 + 0xFC + i, i % 2 == 0 and 8 or 3) end
commands = {}
ui.panels(battle(), true)
local plus_token
for _, c in ipairs(commands) do
  if c.kind == "text" and c.args[3]:match("^%+%d+$") then plus_token = c.args[3] end
end
check("too many stages end in +N", plus_token ~= nil, true)

-- Without the sheet: logged once, text chips, no errors
have_sheet = false
logged = {}
local chip_ui = require("pbi.ui").new(game, reader)
check("sheet failure logged once", #logged, 1)
fresh_singles()
commands = {}
chip_ui.panels(battle(), true)
check("chips are text", top_view():sub(1, 3), "WAT")
check("no icons without a sheet", top_view():find("?icon", 1, true), nil)
local any_image = false
for _, c in ipairs(commands) do any_image = any_image or c.kind == "image" end
check("no image draws without a sheet", any_image, false)
commands = {}
chip_ui.card(battle(), true)
check("chip card draws", #commands > 0, true)
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

-- Arming: any menu screen, so a load on the move screen shows tags straight away
local function bottom_commands()
  local n = 0
  for _, c in ipairs(commands) do
    if c.screen == "bottom" then n = n + 1 end
  end
  return n
end
on_state_loaded()
w32(INPUT + 0x58, 2)
frame()
check("armed by move select", bottom_commands() > 0, true)
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
check("tags at move select", bottom_commands() > 0, true)
w32(INPUT + 0x68, 1)
frame()
check("no tags while busy", bottom_commands(), 0)
w32(INPUT + 0x68, 0)
w32(INPUT + 0x70, 2)
frame()
check("no tags once a button is pressed", bottom_commands(), 0)
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
check("show_move_tags off", bottom_commands(), 0)
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

-- The fake refuses I/O reads like the engine, and nothing above made one
check("the fake refuses 0x04xxxxxx", pcall(r8, 0x04000000), false)
forbidden_reads = forbidden_reads - 1
check("no forbidden reads", forbidden_reads, 0)

print(failures == 0 and "all passed" or (failures .. " failed"))
os.exit(failures == 0 and 0 or 1)
