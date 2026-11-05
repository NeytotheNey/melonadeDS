-- The Gen 4/5 type chart, on short type names. The game's own type IDs are mapped to these per generation (see games.lua).
local M = {}

-- The order chips are listed in
M.ORDER = { "NOR", "FIR", "WAT", "ELE", "GRA", "ICE", "FIG", "POI", "GRO", "FLY", "PSY", "BUG", "ROC", "GHO", "DRA", "DAR", "STE" }

-- Gen 5 has no Fairy; 17 is typeless and isn't in here, so it reads as no type
M.GEN5_IDS = {
  [0] = "NOR", "FIG", "FLY", "POI", "GRO", "ROC", "BUG", "GHO", "STE", "FIR", "WAT", "GRA", "ELE", "PSY", "ICE", "DRA", "DAR",
}

-- CHART[attack][defend], anything missing is x1
local CHART = {
  NOR = { ROC = .5, GHO = 0, STE = .5 },
  FIR = { FIR = .5, WAT = .5, GRA = 2, ICE = 2, BUG = 2, ROC = .5, DRA = .5, STE = 2 },
  WAT = { FIR = 2, WAT = .5, GRA = .5, GRO = 2, ROC = 2, DRA = .5 },
  ELE = { WAT = 2, ELE = .5, GRA = .5, GRO = 0, FLY = 2, DRA = .5 },
  GRA = { FIR = .5, WAT = 2, GRA = .5, POI = .5, GRO = 2, FLY = .5, BUG = .5, ROC = 2, DRA = .5, STE = .5 },
  ICE = { FIR = .5, WAT = .5, GRA = 2, ICE = .5, GRO = 2, FLY = 2, DRA = 2, STE = .5 },
  FIG = { NOR = 2, ICE = 2, POI = .5, FLY = .5, PSY = .5, BUG = .5, ROC = 2, GHO = 0, DAR = 2, STE = 2 },
  POI = { GRA = 2, POI = .5, GRO = .5, ROC = .5, GHO = .5, STE = 0 },
  GRO = { FIR = 2, ELE = 2, GRA = .5, POI = 2, FLY = 0, BUG = .5, ROC = 2, STE = 2 },
  FLY = { ELE = .5, GRA = 2, FIG = 2, BUG = 2, ROC = .5, STE = .5 },
  PSY = { FIG = 2, POI = 2, PSY = .5, DAR = 0, STE = .5 },
  BUG = { FIR = .5, GRA = 2, FIG = .5, POI = .5, FLY = .5, PSY = 2, GHO = .5, DAR = 2, STE = .5 },
  ROC = { FIR = 2, ICE = 2, FIG = .5, GRO = .5, FLY = 2, BUG = 2, STE = .5 },
  GHO = { NOR = 0, PSY = 2, GHO = 2, DAR = .5, STE = .5 },
  DRA = { DRA = 2, STE = .5 },
  DAR = { FIG = .5, PSY = 2, GHO = 2, DAR = .5, STE = .5 },
  STE = { FIR = .5, WAT = .5, ELE = .5, ICE = 2, ROC = 2, STE = .5 },
}

--- How much an attack of this type does to one defending type
function M.cell(attack, defend_type)
  local row = CHART[attack]
  return row and row[defend_type] or 1
end

--- How much an attack of this type does to a list of defending types
function M.effect(attack, defend_types)
  local row = CHART[attack]
  if not row then return 1 end
  local m = 1
  for i = 1, #defend_types do
    m = m * (row[defend_types[i]] or 1)
  end
  return m
end

--- The types that at least one of these attacking types hits for double
function M.strong_against(types)
  local out = {}
  for _, t in ipairs(M.ORDER) do
    for i = 1, #types do
      local row = CHART[types[i]]
      if row and row[t] == 2 then
        out[#out + 1] = t
        break
      end
    end
  end
  return out
end

return M
