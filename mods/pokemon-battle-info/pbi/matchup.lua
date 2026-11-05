-- Multipliers: the type chart plus the abilities and conditions that change it. No game memory in here, so it can be tested on a PC.
-- It follows the game's ServerControl_CheckMoveEffectiveness in the pokebw2 decomp (src/ov167/btl_server_flow.c).
--
-- Not handled on purpose: items (Air Balloon, Iron Ball, Ring Target...), Gravity, weather (Weather Ball shows ?), and the abilities that
-- only change damage by other amounts or in other ways (Dry Skin's fire damage, Solid Rock and Filter's x0.75, Normalize's STAB...).
--
-- Known limits: with Normalize the rows still describe types rather than your actual moves (the pills are right); the panel files x1/8
-- under the x.25 marker; Pledge combos aren't counted; a disguise of Arceus ignores its plate; and Soak or Reflect Type on a disguised
-- foe doesn't show, because a disguise's types come from its species.
--
-- A defender is a table: types (short names), grounded (Ingrain or Smack Down), lifted (Magnet Rise or Telekinesis) and seen_type (the
-- type that Foresight or Miracle Eye took the immunity off). Its ability goes in separately, because it isn't always known to the player.
local typechart = require("pbi.typechart")

local M = {}

-- Ability IDs are the Gen 5 ones
local LEVITATE = 26
local WONDER_GUARD = 25
local SOUNDPROOF = 43
local NORMALIZE = 96
local TINTED_LENS = 110
local SCRAPPY = 113
local IMMUNE_TO = {
  [10] = "ELE",  -- Volt Absorb
  [31] = "ELE",  -- Lightning Rod
  [78] = "ELE",  -- Motor Drive
  [11] = "WAT",  -- Water Absorb
  [114] = "WAT", -- Storm Drain
  [87] = "WAT",  -- Dry Skin
  [18] = "FIR",  -- Flash Fire
  [157] = "GRA", -- Sap Sipper
}
local HALVES = {
  [47] = { FIR = true, ICE = true }, -- Thick Fat
  [85] = { FIR = true },             -- Heatproof
}
local IGNORES_ABILITIES = {
  [104] = true, -- Mold Breaker
  [163] = true, -- Turboblaze
  [164] = true, -- Teravolt
}

-- Fixed damage: type multipliers don't count, only immunity does
local FIXED_DAMAGE = {
  [69] = true, [101] = true, [82] = true, [49] = true, [162] = true, [149] = true, -- Seismic Toss, Night Shade, Dragon Rage, Sonic Boom, Super Fang, Psywave
  [283] = true, [515] = true, [68] = true, [243] = true, [368] = true, [117] = true, -- Endeavor, Final Gambit, Counter, Mirror Coat, Metal Burst, Bide
  [12] = true, [32] = true, [90] = true, [329] = true, -- One-hit KOs: Guillotine, Horn Drill, Fissure, Sheer Cold
}
-- These change type in play, so the table's type can't be trusted. They keep their own type under Normalize too: a move's handler runs
-- before an ability's, and the type can only be rewritten once.
local VARIABLE_TYPE = {
  [237] = true, [311] = true, [449] = true, [363] = true, [546] = true, -- Hidden Power, Weather Ball, Judgment, Natural Gift, Techno Blast
}
local SYNCHRONOISE = 485

--- Whether a foe's ability changes your own rows: its Mold Breaker family and Scrappy do. Its Tinted Lens is about damage, not what you
--- resist, so it stays out.
function M.changes_your_rows(ability)
  return IGNORES_ABILITIES[ability] == true or ability == SCRAPPY
end

--- Whether an immunity of this defending type stops counting: Foresight, Miracle Eye and Scrappy per type, Ingrain and Smack Down for Ground
local function immunity_off(attack, defend_type, defender, attack_ability)
  if defender.seen_type == defend_type then return true end
  if attack_ability == SCRAPPY and defend_type == "GHO" then return true end
  return attack == "GRO" and defender.grounded
end

--- The type chart for this defender, before any multiplier from abilities
local function type_effect(attack, defender, defend_ability, ignore_ability, attack_ability)
  local types = defender.types
  local m = 1
  for i = 1, #types do
    local e = typechart.cell(attack, types[i])
    if e == 0 and immunity_off(attack, types[i], defender, attack_ability) then
      e = 1
    end
    m = m * e
  end
  if attack == "GRO" and m ~= 0 and not defender.grounded then
    -- Ingrain and Smack Down beat everything that keeps a mon in the air
    if defender.lifted or (defend_ability == LEVITATE and not ignore_ability) then
      return 0
    end
  end
  return m
end

--- The multiplier of an attack on a defender. defend_ability is nil when it mustn't be used (hidden, or unknown).
function M.effect(attack, defender, defend_ability, attack_ability)
  local ignore_ability = IGNORES_ABILITIES[attack_ability]
  local m = type_effect(attack, defender, defend_ability, ignore_ability, attack_ability)
  if defend_ability and not ignore_ability then
    if IMMUNE_TO[defend_ability] == attack then
      return 0
    end
    if defend_ability == WONDER_GUARD and m < 2 then
      return 0
    end
  end
  if attack_ability == TINTED_LENS and m > 0 and m < 1 then
    m = m * 2
  end
  if defend_ability and not ignore_ability then
    local halves = HALVES[defend_ability]
    if halves and halves[attack] then
      m = m / 2
    end
  end
  return m
end

local function shares_a_type(a, b)
  for i = 1, #a do
    for j = 1, #b do
      if a[i] == b[j] then return true end
    end
  end
  return false
end

--- What a move's tag says: nil for no tag, "?" when the type can't be known, otherwise a multiplier.
--- move is { id, type (short name), category, sound }, attacker is { ability, types }.
function M.move_tag(move, defender, defend_ability, attacker)
  if not move.type or move.category == 0 or move.category == nil then
    return nil
  end
  if VARIABLE_TYPE[move.id] then
    return "?"
  end
  local attack_ability = attacker and attacker.ability
  local move_type = attack_ability == NORMALIZE and "NOR" or move.type
  if defend_ability == SOUNDPROOF and move.sound and not IGNORES_ABILITIES[attack_ability] then
    return 0
  end
  if move.id == SYNCHRONOISE and attacker and not shares_a_type(attacker.types, defender.types) then
    return 0
  end
  local m = M.effect(move_type, defender, defend_ability, attack_ability)
  if FIXED_DAMAGE[move.id] and m ~= 0 then
    return nil
  end
  return m
end

--- Every type, grouped by what it does to this defender: groups[2] is the list of types that hit for x2
function M.groups(defender, defend_ability, attack_ability)
  local groups = {}
  for _, t in ipairs(typechart.ORDER) do
    local m = M.effect(t, defender, defend_ability, attack_ability)
    local list = groups[m]
    if not list then
      list = {}
      groups[m] = list
    end
    list[#list + 1] = t
  end
  return groups
end

--- The same groups for your own mon, against what the foes out could do: each type goes by the worst of these attacking abilities.
function M.groups_against(defender, defend_ability, attack_abilities)
  if #attack_abilities == 0 then
    return M.groups(defender, defend_ability)
  end
  local groups = {}
  for _, t in ipairs(typechart.ORDER) do
    local m = M.effect(t, defender, defend_ability, attack_abilities[1])
    for i = 2, #attack_abilities do
      m = math.max(m, M.effect(t, defender, defend_ability, attack_abilities[i]))
    end
    local list = groups[m]
    if not list then
      list = {}
      groups[m] = list
    end
    list[#list + 1] = t
  end
  return groups
end

local LABELS = { [4] = "x4", [2] = "x2", [1] = "x1", [.5] = "x.5", [.25] = "x.25", [.125] = "x1/8", [0] = "x0" }

--- The short text for a multiplier
function M.label(m)
  return LABELS[m] or "x?"
end

return M
