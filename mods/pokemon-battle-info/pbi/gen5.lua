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

-- The party entry's battle stats, enciphered from its PID (PartyPkm in the decomp). Only the party flag says they're plain: the box flag is for the blocks.
local PARTY_STATS, PARTY_MAX_HP_WORD = 0x88, 4
local PARTY_DECRYPTED = 1
local MAX_HP_LIMIT = 714

--- The max HP in a party entry, or nil if it can't be a real one
local function read_party_max_hp(entry)
  local max_hp = R16(entry + PARTY_STATS + PARTY_MAX_HP_WORD * 2)
  if R16(entry + 4) & PARTY_DECRYPTED == 0 then
    local key = R32(entry)
    for _ = 0, PARTY_MAX_HP_WORD do
      key = (key * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
    end
    max_hp = max_hp ~ (key >> 16)
  end
  if max_hp < 1 or max_hp > MAX_HP_LIMIT then return nil end
  return max_hp
end

--- HP as a disguise with that max HP would show it: rounds up, so anything alive shows at least 1, and full stays full
local function scale_hp(hp, real_max, shown_max)
  if hp <= 0 then return 0 end
  if hp >= real_max then return shown_max end
  return (hp * shown_max + real_max - 1) // real_max
end

--- A battle record's species, level, max HP, HP and mon ID, or nil if it doesn't look like one. foe_side says which range its mon ID has to be in.
local function check_record(rec, foe_side)
  if not in_ram(rec) then return nil end
  local species, level = R16(rec + 0x0C), R8(rec + 0x18)
  if species < 1 or species > SPECIES_MAX or level < 1 or level > LEVEL_MAX then return nil end
  local max_hp, hp = R16(rec + 0x0E), R16(rec + 0x10)
  if max_hp == 0 or hp > max_hp then return nil end
  local id = R8(rec + 0x19)
  if id > LAST_ID or (id >= FOE_FIRST_ID) ~= foe_side then return nil end
  return species, level, max_hp, hp, id
end

-- The client's command queue, values big-endian. docs/pokemon-battle-ram.md, "Damage, heals and crits".
local CLIENT_QUEUE, CLIENT_COMMAND = 0x13C, 0x180
local QUEUE_BYTES = 8
local QUEUE_SIZE = 3000
local CMD_DAMAGE, CMD_DAMAGE_PLURAL, CMD_EXP_GAIN, CMD_MESSAGE, CMD_MESSAGE_ARGS = 50, 51, 69, 90, 91
local ARGS_END = 0xFFFF0000
local MESSAGE_MAX_ARGS = 15
-- Messages that can sit between a hit and its crit message. Singles and doubles have their own IDs.
local MESSAGE_CRIT, MESSAGE_CRIT_ON_MON = 0x51, 0x180
local EFFECTIVENESS_MESSAGE = { [0x4E] = true, [0x4F] = true }
local EFFECTIVENESS_MESSAGE_ON_MON = { [6] = true, [9] = true, [0xC] = true, [0xF] = true, [0x12] = true, [0x15] = true }
-- A hit has at most one message per target ahead of its crit, and more than this means it's not what we think it is
local CRIT_SCAN_MESSAGES = 8

local function queue_u16(queue, pos)
  return (R8(queue + QUEUE_BYTES + pos) << 8) | R8(queue + QUEUE_BYTES + pos + 1)
end

local function queue_u32(queue, pos)
  return (queue_u16(queue, pos) << 16) | queue_u16(queue, pos + 2)
end

--- The client's record, or nil. It has to point back at main, so a freed battle can't pass.
local function live_client(ctx)
  local client = R32(ctx.core + 4)
  if not in_ram(client) or R32(client) ~= ctx.main then return nil end
  return client
end

--- Where a message command ends, or nil if its args run off the end of what's written
local function after_message(queue, pos, write)
  local p = pos + 4
  for _ = 1, MESSAGE_MAX_ARGS do
    if p + 4 > write then return nil end
    local arg = queue_u32(queue, p)
    p = p + 4
    if arg == ARGS_END then return p end
  end
  return nil
end

-- The game's HP boxes, through its battle effect work. docs/pokemon-battle-ram.md, "HP boxes".
local EFFECT_GAUGE, EFFECT_MAIN = 0x1A4, 0x1E4
-- Counts down from 0 to -16 as the screen fades out when the battle ends. The chain stays live for about 30 frames more, with the boxes still up.
local MAIN_EXIT_FADE = 0x460
local GAUGE_ENTRIES, GAUGE_ENTRY_STRIDE = 0x40, 0x84
local ENTRY_NAME_ACTOR, ENTRY_BAR_ACTOR, ENTRY_MON = 0x00, 0x08, 0x40
local BOX_STYLE, BOX_SIDE, BOX_HP, BOX_MAX_HP, BOX_HP_ANIM, BOX_HP_DELTA, BOX_FLAGS = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x28, 0x30
local BOX_HP_MOVING, BOX_SHOWN = 1, 8
local BOX_STYLE_SINGLE_OR_DOUBLE, BOX_SIDE_FOE = 0, 1
local HP_ANIM_START = 0x80000000
-- Below this many max HP the box keeps its moving HP in 1/256ths
local BAR_PIXELS = 48
local ACTOR_X, ACTOR_Y, ACTOR_FLAGS = 0x0C, 0x0E, 0x60
local ACTOR_VISIBLE = 1 << 26
-- The boxes by view position: the foe's by the reader's foe order, for singles and doubles
local FOE_BOXES = { [0] = { 1 }, [1] = { 3, 5 } }

local function s16(v)
  return v >= 0x8000 and v - 0x10000 or v
end

--- The HP the bar shows now, the way the box works out its own numbers. Record HP leads this.
local function box_hp(box)
  local hp = R32(box + BOX_HP)
  if R32(box + BOX_FLAGS) & BOX_HP_MOVING == 0 then return hp end
  local anim = R32(box + BOX_HP_ANIM)
  if anim == HP_ANIM_START then return hp end
  if R32(box + BOX_MAX_HP) >= BAR_PIXELS then return anim end
  local shown = anim >> 8
  if RS32(box + BOX_HP_DELTA) > 0 and anim & 0xFF > 0 then
    shown = shown + 1
  end
  return shown
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

  --- What a disguise's party entry says: species, raw type IDs, ability and max HP, or nil. A good result is kept until the battle resets.
  local function read_disguise(entry)
    local known = disguises[entry]
    if known then return known end
    local species, form, ability = read_party_entry(entry)
    if not species then return nil end
    local max_hp = read_party_max_hp(entry)
    if not max_hp then return nil end
    local forms = personal.forms[species]
    local form_types = forms and forms[form]
    local view = {
      species = species,
      type1 = form_types and form_types[1] or personal.type1[species],
      type2 = form_types and form_types[2] or personal.type2[species],
      ability = ability,
      max_hp = max_hp,
    }
    disguises[entry] = view
    return view
  end

  --- A battle record, or nil if it doesn't look like one. foe_side says which range its mon ID has to be in.
  local function read_mon(rec, foe_side)
    local species, level, max_hp, hp = check_record(rec, foe_side)
    if not species then return nil end

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
        mon.hp = scale_hp(hp, max_hp, view.max_hp)
        mon.max_hp = view.max_hp
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
      main = main,
      core = core,
      scd = scd,
      input = input,
      screen = screen,
      busy = R32(input + 0x68) & 1,
      pressed = RS32(input + 0x70),
      rule = R32(input + 0x50), -- a u32 in the decomp's BtlvInput: 0 singles, 1 doubles, higher is triples and rotation
    }
  end

  --- The records of the active mons, by position on each side, and whether it's a multi battle. Nil before the foe's client exists.
  local function active_records(ctx)
    local party = ctx.party
    local active = ctx.rule == 0 and 1 or (ctx.rule == 1 and 2 or 3)
    local second_foe_client = R8(party + 3 * CLIENT_STRIDE + 0x18) ~= 0
    local ally_client = R8(party + 2 * CLIENT_STRIDE + 0x18) ~= 0
    if R8(party + CLIENT_STRIDE + 0x18) == 0 then return nil end

    local function record(client, slot)
      return R32(party + client * CLIENT_STRIDE + slot * 4)
    end
    local foes, mine = {}, {}
    if second_foe_client then
      -- Multi: clients are player, enemy 1, ally, enemy 2. Each has its own party with one mon out. Not run yet.
      foes[1] = record(1, 0)
      foes[2] = record(3, 0)
    else
      for slot = 0, active - 1 do
        foes[slot + 1] = record(1, slot)
      end
    end
    if ally_client then
      mine[1] = record(0, 0)
      mine[2] = record(2, 0)
    else
      for slot = 0, active - 1 do
        mine[slot + 1] = record(0, slot)
      end
    end
    return foes, mine, second_foe_client or ally_client
  end

  --- The HP of each active mon, cheaply: a list of { id, hp, max_hp, real_hp, real_max_hp, foe, slot }, with slot counted from 1 on its side.
  --- hp and max_hp are what a player should see, so a disguised foe has the disguise's: it also has disguised, and hidden if the disguise
  --- can't be read. Unusable records are left out.
  function self.active_hp(ctx)
    local foes, mine = active_records(ctx)
    local out = {}
    if not foes then return out end
    local function add(records, foe)
      for slot = 1, #records do
        local rec = records[slot]
        local species, _, max_hp, hp, id = check_record(rec, foe)
        if species then
          local mon = { id = id, hp = hp, max_hp = max_hp, real_hp = hp, real_max_hp = max_hp, foe = foe, slot = slot }
          if foe and is_disguised(rec) then
            mon.disguised = true
            local view = read_disguise(R32(rec + 0x04))
            if view then
              mon.hp, mon.max_hp = scale_hp(hp, max_hp, view.max_hp), view.max_hp
            else
              mon.hidden = true
            end
          end
          out[#out + 1] = mon
        end
      end
    end
    add(foes, true)
    add(mine, false)
    return out
  end

  --- True while the client plays the command that raises HP and max HP at a level-up
  function self.gaining_exp(ctx)
    local client = live_client(ctx)
    return client ~= nil and R32(client + CLIENT_COMMAND) == CMD_EXP_GAIN
  end

  --- True if an HP drop this frame is a critical hit on that mon. Reads on from the hit that's landing only, up to where its own messages
  --- end: the queue holds the rest of the turn, which would spoil it. Anything unexpected is false.
  function self.hit_is_crit(ctx, mon_id)
    local client = live_client(ctx)
    if not client then return false end
    local command = R32(client + CLIENT_COMMAND)
    if command ~= CMD_DAMAGE and command ~= CMD_DAMAGE_PLURAL then return false end
    local queue = R32(client + CLIENT_QUEUE)
    if not in_ram(queue) then return false end
    local write, pos = R32(queue), R32(queue + 4)
    if write > QUEUE_SIZE or pos > write then return false end
    for _ = 1, CRIT_SCAN_MESSAGES do
      if pos + 4 > write then return false end
      local id, message = queue_u16(queue, pos), queue_u16(queue, pos + 2)
      local skip
      if id == CMD_MESSAGE then
        if message == MESSAGE_CRIT then return true end
        skip = EFFECTIVENESS_MESSAGE[message]
      elseif id == CMD_MESSAGE_ARGS then
        if message == MESSAGE_CRIT_ON_MON then
          if pos + 8 > write then return false end
          if queue_u32(queue, pos + 4) == mon_id then return true end
          skip = true
        else
          skip = EFFECTIVENESS_MESSAGE_ON_MON[message]
        end
      end
      if not skip then return false end
      pos = after_message(queue, pos, write)
      if not pos then return false end
    end
    return false
  end

  --- One foe's entry for foe_boxes, or nil
  local function read_foe_box(gauge, view_pos, slot, rec)
    local view
    local disguised = in_ram(rec) and is_disguised(rec)
    if disguised then
      view = read_disguise(R32(rec + 0x04))
      if not view then return { slot = slot, disguised = true, hidden = true, unreadable = true } end
    end
    local entry = gauge + GAUGE_ENTRIES + view_pos * GAUGE_ENTRY_STRIDE
    local name, bar = R32(entry + ENTRY_NAME_ACTOR), R32(entry + ENTRY_BAR_ACTOR)
    local box = entry + ENTRY_MON
    local up = in_ram(name) and in_ram(bar) and R32(box + BOX_FLAGS) & BOX_SHOWN ~= 0
      and R32(name + ACTOR_FLAGS) & ACTOR_VISIBLE ~= 0 and R32(bar + ACTOR_FLAGS) & ACTOR_VISIBLE ~= 0
      and R32(box + BOX_STYLE) == BOX_STYLE_SINGLE_OR_DOUBLE and R32(box + BOX_SIDE) == BOX_SIDE_FOE
    local shown
    if up then
      local species, _, real_max = check_record(rec, true)
      local max_hp, hp = R32(box + BOX_MAX_HP), box_hp(box)
      local x, y = s16(R16(name + ACTOR_X)), s16(R16(name + ACTOR_Y))
      if species and max_hp == real_max and hp >= 1 and hp <= max_hp and x >= 0 and x < 256 and y >= 0 and y < 192 then
        shown = { slot = slot, hp = hp, max_hp = max_hp, x = x, y = y, disguised = disguised }
        if view then
          shown.hp, shown.max_hp = scale_hp(hp, max_hp, view.max_hp), view.max_hp
        end
      end
    end
    -- Saying it's disguised even with no box up keeps a real value that's still waiting from being drawn
    return shown or (disguised and { slot = slot, disguised = true, hidden = true }) or nil
  end

  --- What's showing in the foe's HP boxes right now: a list of { slot, hp, max_hp, x, y, disguised }, with the name box's center on the top
  --- screen, for each box on screen with a foe it's safe to show, and a disguised foe's HP as the disguise would show it. A disguised foe
  --- with no box up gets { slot, disguised, hidden }, and one whose disguise can't be read gets unreadable as well. Anything unexpected
  --- leaves that foe out. Not for multi battles, which haven't been run.
  function self.foe_boxes(ctx)
    local out = {}
    local views = FOE_BOXES[ctx.rule]
    if not cfg.effect_pointer or not views or R32(ctx.main + MAIN_EXIT_FADE) ~= 0 then return out end
    local effect = R32(cfg.effect_pointer)
    if not in_ram(effect) or R32(effect + EFFECT_MAIN) ~= ctx.main then return out end
    local gauge = R32(effect + EFFECT_GAUGE)
    if not in_ram(gauge) then return out end
    local foes, _, multi = active_records(ctx)
    if not foes or multi then return out end
    for i = 1, #foes do
      out[#out + 1] = read_foe_box(gauge, views[i], i, foes[i])
    end
    return out
  end

  local function client_mon(rec, foe_side)
    return read_mon(rec, foe_side) or false
  end

  --- The active mons on both sides and the one choosing. Entries in foes and mine are false when that record isn't usable.
  function self.battle(ctx)
    local foe_records, my_records, multi = active_records(ctx)
    if not foe_records then return nil end

    local foes, mine = {}, {}
    for i = 1, #foe_records do
      foes[i] = client_mon(foe_records[i], true)
    end
    for i = 1, #my_records do
      mine[i] = client_mon(my_records[i], false)
    end

    local choosing_rec = R32(ctx.scd + 0x6C)
    local choosing
    for i = 1, #mine do
      if mine[i] and mine[i].rec == choosing_rec then
        choosing = mine[i]
      end
    end
    return { rule = ctx.rule, multi = multi, foes = foes, mine = mine, choosing = choosing }
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
