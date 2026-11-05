-- Reads a Gen 5 battle from RAM. Reads only. See docs/pokemon-battle-ram.md, "Verified in game".
--
-- It uses the live chain every frame, because battle status, the 0x41 byte and the "ready" check all stay on for a while
-- after the battle data is freed. It reads the client copy of the party table only: the server copy holds the turn's results
-- before they play out.
local M = {}

local R8, R16, R32, RS32 = melon.mem.read8, melon.mem.read16, melon.mem.read32, melon.mem.read_s32

local RAM_START, RAM_END = 0x02000000, 0x02400000
local SPECIES_MAX, LEVEL_MAX, REAL_SPECIES_MAX, ABILITY_MAX = 650, 100, 649, 164
local MOVE_SLOTS = 4
local CLIENT_STRIDE = 0x1C
-- Mon IDs in the records: clients 0 to 3 start at 0, 12, 6 and 18 (sClientFirstMonIds in the decomp), so your side is under 12
local FOE_FIRST_ID, LAST_ID = 12, 23

local function in_ram(p)
  return p >= RAM_START and p < RAM_END
end

-- Status decoding. Only paralysis and sleep have been seen in game, so this is the place to fix if the others read wrong.
-- A condition word (0x1C + id * 4) is on when its low 3 bits aren't zero. The game takes the first hit of IDs 1 to 5.
local MAJOR_STATUS = { "PAR", "SLP", "FRZ", "BRN", "PSN" }
local POISON_ID, CONFUSION_ID, ROOST_ID, GASTRO_ACID_ID = 5, 6, 24, 16
-- Conditions that change what hits, from constants/battle.h in the decomp. Foresight and Miracle Eye are the same one: its
-- parameter is the type that loses its immunity (HandlerForesight sets 7, Ghost; HandlerMiracleEye sets 16, Dark).
local FORESIGHT_ID, INGRAIN_ID, MAGNET_RISE_ID, SMACK_DOWN_ID, TELEKINESIS_ID = 17, 21, 30, 31, 32

local function condition_word(rec, id)
  return R32(rec + 0x1C + id * 4)
end

local function condition_on(rec, id)
  return condition_word(rec, id) & 7 ~= 0
end

--- A condition's parameter (Condition_GetParam in the decomp): its place depends on the kind in the low 3 bits
local function condition_param(word)
  if word & 7 == 4 then
    return (word >> 15) & 0xFFFF
  end
  return (word >> 9) & 0xFFFF
end

local function read_status(rec)
  local badges = {}
  for id = 1, #MAJOR_STATUS do
    if condition_on(rec, id) then
      local label = MAJOR_STATUS[id]
      if id == POISON_ID and condition_word(rec, id) & 0x1FF == 0x79 then
        label = "TOX"
      end
      badges[1] = label
      break
    end
  end
  if condition_on(rec, CONFUSION_ID) then
    badges[#badges + 1] = "CNF"
  end
  return badges
end

-- Illusion's flag in a record. No real game has confirmed this bit, so this is the one place to change.
local function is_disguised(rec)
  return R8(rec + 0x1B) & 0x40 ~= 0
end

local STAGE_NAMES = { "ATK", "DEF", "SPA", "SPD", "SPE", "ACC", "EVA" }

-- Party entry block order by index: which block sits at each of the four places
local BLOCK_ORDERS = {
  "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
  "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA",
}
local BLOCK_WORDS = 16
local BAD_EGG, DECRYPTED = 4, 3

--- The species, form and ability in an encrypted party entry, or nil when it isn't a valid one. Checks the checksum, so garbage doesn't pass.
local function read_party_entry(entry)
  if not in_ram(entry) then return nil end
  local pid, flags, checksum = R32(entry), R16(entry + 4), R16(entry + 6)
  if flags & BAD_EGG ~= 0 then return nil end
  local plain = flags & DECRYPTED ~= 0
  local key, total = checksum, 0
  local words = {}
  for i = 0, BLOCK_WORDS * 4 - 1 do
    local word = R16(entry + 8 + i * 2)
    if not plain then
      key = (key * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
      word = word ~ (key >> 16)
    end
    words[i] = word
    total = (total + word) & 0xFFFF
  end
  if total ~= checksum then return nil end
  local order = BLOCK_ORDERS[((pid & 0x3E000) >> 13) % 24 + 1]
  local block_a = order:find("A", 1, true) - 1
  local block_b = order:find("B", 1, true) - 1
  local species = words[block_a * BLOCK_WORDS]
  if species < 1 or species > REAL_SPECIES_MAX then return nil end
  -- The form is in bits 3-7 of the byte at 0x18 of block B
  local form = (words[block_b * BLOCK_WORDS + 0x0C] & 0xFF) >> 3
  -- The ability is the byte at 0x0D of block A (PkmBlockA in the decomp)
  local ability = words[block_a * BLOCK_WORDS + 0x06] >> 8
  if ability < 1 or ability > ABILITY_MAX then return nil end
  return species, form, ability
end

--- A reader for one game; cfg is its line in games.lua
function M.new(cfg)
  local self = {}
  local type_ids = cfg.generation.type_ids
  local personal = require(cfg.generation.personal)
  local disguises = {}

  --- The types as the game counts them (splitTypeCore in the decomp): Roost hides Flying, and a pure Flying mon counts as Normal.
  local function effective_types(id1, id2, roost)
    local t1, t2 = type_ids[id1], type_ids[id2]
    if t2 == t1 then t2 = nil end
    if roost and (t1 == "FLY" or t2 == "FLY") then
      if t1 == "FLY" then t1 = nil end
      if t2 == "FLY" then t2 = nil end
      if not t1 and not t2 then t1 = type_ids[0] end
    end
    local types = {}
    if t1 then types[#types + 1] = t1 end
    if t2 then types[#types + 1] = t2 end
    return types
  end

  local function read_stages(rec)
    local stages = {}
    for i = 1, #STAGE_NAMES do
      local v = R8(rec + 0xFB + i)
      if v > 12 then
        return {}
      end
      if v ~= 6 then
        stages[#stages + 1] = { STAGE_NAMES[i], v - 6 }
      end
    end
    return stages
  end

  --- What a disguise's party entry says: species, raw type IDs and ability, or nil. A good result is kept until the battle resets.
  local function read_disguise(entry)
    local known = disguises[entry]
    if known then return known end
    local species, form, ability = read_party_entry(entry)
    if not species then return nil end
    local forms = personal.forms[species]
    local form_types = forms and forms[form]
    local view = {
      species = species,
      type1 = form_types and form_types[1] or personal.type1[species],
      type2 = form_types and form_types[2] or personal.type2[species],
      ability = ability,
    }
    disguises[entry] = view
    return view
  end

  --- A battle record, or nil if it doesn't look like one. foe_side says which range its mon ID has to be in.
  local function read_mon(rec, foe_side)
    if not in_ram(rec) then return nil end
    local species, level = R16(rec + 0x0C), R8(rec + 0x18)
    if species < 1 or species > SPECIES_MAX or level < 1 or level > LEVEL_MAX then return nil end
    local max_hp, hp = R16(rec + 0x0E), R16(rec + 0x10)
    if max_hp == 0 or hp > max_hp then return nil end
    local id = R8(rec + 0x19)
    if id > LAST_ID or (id >= FOE_FIRST_ID) ~= foe_side then return nil end

    local roost = condition_on(rec, ROOST_ID)
    local gastro_acid = condition_on(rec, GASTRO_ACID_ID)
    local seen_word = condition_word(rec, FORESIGHT_ID)
    local mon = {
      rec = rec,
      species = species,
      level = level,
      hp = hp,
      max_hp = max_hp,
      ability = gastro_acid and 0 or R16(rec + 0x13C),
      types = effective_types(R8(rec + 0xF8), R8(rec + 0xF9), roost),
      status = read_status(rec),
      stages = read_stages(rec),
      grounded = condition_on(rec, INGRAIN_ID) or condition_on(rec, SMACK_DOWN_ID),
      lifted = condition_on(rec, MAGNET_RISE_ID) or condition_on(rec, TELEKINESIS_ID),
      seen_type = seen_word & 7 ~= 0 and type_ids[condition_param(seen_word)] or nil,
    }
    if foe_side and is_disguised(rec) then
      -- Show a real foe of that species with that ability, so nothing gives Zoroark away. With no readable disguise, the foe shows nothing.
      local view = read_disguise(R32(rec + 0x04))
      if view then
        mon.species = view.species
        mon.types = effective_types(view.type1, view.type2, roost)
        mon.ability = gastro_acid and 0 or view.ability
      else
        mon.no_info = true
      end
    end
    return mon
  end

  --- Forgets what was kept from the battle that's gone
  function self.reset()
    if next(disguises) then disguises = {} end
  end

  --- The live chain, or nil. Every link has to point back at its owner, so a freed battle can't pass.
  function self.chain()
    local base = R32(cfg.base_pointer)
    if not in_ram(base) then return nil end
    local party = base + cfg.party_table
    if not in_ram(party) then return nil end
    local main = R32(party - 4)
    if not in_ram(main) or main + 0xCC ~= party then return nil end
    local core = R32(main + 4)
    if not in_ram(core) or R32(core) ~= main then return nil end
    local scd = R32(core + 0x1C4)
    if not in_ram(scd) or R32(scd + 0xAC) ~= main or R32(scd + 0xA8) ~= core then return nil end
    local input = R32(scd + 0xBC)
    if not in_ram(input) then return nil end
    local count = R8(party + 0x18)
    if count < 1 or count > 6 then return nil end
    local screen = R32(input + 0x58)
    if screen > 7 then return nil end
    return {
      party = party,
      scd = scd,
      input = input,
      screen = screen,
      busy = R32(input + 0x68) & 1,
      pressed = RS32(input + 0x70),
      rule = R32(input + 0x50), -- a u32 in the decomp's BtlvInput: 0 singles, 1 doubles, higher is triples and rotation
    }
  end

  local function client_mon(ctx, client, slot, foe_side)
    return read_mon(R32(ctx.party + client * CLIENT_STRIDE + slot * 4), foe_side) or false
  end

  --- The active mons on both sides and the one choosing. Entries in foes and mine are false when that record isn't usable.
  function self.battle(ctx)
    local party = ctx.party
    local active = ctx.rule == 0 and 1 or (ctx.rule == 1 and 2 or 3)
    local second_foe_client = R8(party + 3 * CLIENT_STRIDE + 0x18) ~= 0
    local ally_client = R8(party + 2 * CLIENT_STRIDE + 0x18) ~= 0
    if R8(party + CLIENT_STRIDE + 0x18) == 0 then return nil end

    local foes, mine = {}, {}
    if second_foe_client then
      -- Multi: clients are player, enemy 1, ally, enemy 2. Each has its own party with one mon out. Not run yet.
      foes[1] = client_mon(ctx, 1, 0, true)
      foes[2] = client_mon(ctx, 3, 0, true)
    else
      for slot = 0, active - 1 do
        foes[slot + 1] = client_mon(ctx, 1, slot, true)
      end
    end
    if ally_client then
      mine[1] = client_mon(ctx, 0, 0, false)
      mine[2] = client_mon(ctx, 2, 0, false)
    else
      for slot = 0, active - 1 do
        mine[slot + 1] = client_mon(ctx, 0, slot, false)
      end
    end

    local choosing_rec = R32(ctx.scd + 0x6C)
    local choosing
    for i = 1, #mine do
      if mine[i] and mine[i].rec == choosing_rec then
        choosing = mine[i]
      end
    end
    return { rule = ctx.rule, multi = second_foe_client or ally_client, foes = foes, mine = mine, choosing = choosing }
  end

  --- The move IDs in use in a mon's four slots (Mimic and Transform replace these, not the true ones). 0 is an empty slot.
  function self.moves(mon)
    local out = {}
    for i = 1, MOVE_SLOTS do
      out[i] = R16(mon.rec + 0x104 + (i - 1) * 0x0E + 6)
    end
    return out
  end

  return self
end

return M
