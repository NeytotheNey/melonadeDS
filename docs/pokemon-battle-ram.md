# Reading Pokémon battle state from RAM

Reference for the battle info Lua mod. All addresses are ARM9 bus addresses (`melon.mem.read*`). We reuse facts like addresses and constants here, not code; use our own field names rather than the decomp's.

How sure each fact is: **[S]** a source states it, **[X]** two independent sources agree, **[I]** inferred, verify it. Nothing here had been run against a ROM when this was written, so see "Verified in game" at the end.

## White 2, English (`IRDO`)

Detect with `read32(0x023FFE0C) == 0x4F445249`. Black 2 (`IREO`) is `0x4F455249`. [S tracker]

`base = read32(0x02000024)`. Every `base+` below hangs off it. [S tracker only] Expected value about `0x02204D04`. [I]

| Item | Address | Tag |
|---|---|---|
| Battle status (u16) | `0x021B5178` (Black 2: `0x021B5138`) | S tracker |
| Field party count (u32) | `base+0x19724` | X |
| Field party, first mon (stride 220) | `base+0x19728` | X |
| Battle copy of player party | `base+0x53610` | S tracker |
| Enemy party | `base+0x53B70` | S tracker, conflict below |
| Enemy trainer ID (u16, 0 = wild) | `base+0x5262E` | S tracker |
| Battle party table `P` | `base+0x526A8` | S tracker |
| Double/triple flag (u8) | `base+0x900A0` | S tracker |

**In a battle:** status `0x2100` or `0x2101` means in battle, `0x2800` means not; the tracker treats anything else as not. A second signal: byte at `0x02258DC6` is `0x41` during a battle. [S]

**Data ready:** the tracker waits 60 frames after status turns on, then needs `read32(base+0x53610) ~= 0`, `read32(base+0x53B70) ~= 0` and `read32(base+0x53610) == read32(base+0x19728)`. [S]

**Active battlers:** `P` is an array with stride `0x1C`: 6 pointers to battle records, then a u8 count. Active mons come first and the game swaps pointers on a switch, so array order is the slot. [X]
- Singles: player `read32(P)`, enemy `read32(P+0x1C)`.
- Doubles/triples: flag 0→1 active, 1→2, 2→3, 3→3. Slot `i` is `read32(P+i*4)` for the player and `read32(P+0x1C+i*4)` for the enemy. [S]
- Clients: walk `P+0x18` with stride `0x1C` until zero. More than 2 means a multi battle, ordered player, enemy 1, ally, enemy 2. [S]

### Battle record (plain, `0x1F8` bytes)

| Offset | Field | Note |
|---|---|---|
| `0x00` | pointer to the encrypted party entry | X |
| `0x04` | pointer to the Illusion disguise's party entry | X |
| `0x0C` | species u16 | |
| `0x0E` / `0x10` | max HP / HP u16 | X |
| `0x12` | held item u16 | |
| `0x16` | base ability u16 | |
| `0x18` | level u8 | X |
| `0x19` | mon ID u8 | second side starts at 12 |
| `0x1B` | bits 0-4 base form, 5 transformed, 6 illusion, 7 appeared | bit order I |
| `0x1C` | 36 condition words, u32 each | IDs under "Status" |
| `0xAC` | 36 condition counters, u8 | toxic counter at `0xB1` I |
| `0xEE`-`0xF6` | Atk, Def, SpA, SpD, Spe u16 | X |
| `0xF8` / `0xF9` | type 1 / type 2 u8 | decomp only |
| `0xFC`-`0x102` | stages: Atk, Def, SpA, SpD, Spe, Acc, Eva | 0-12, 6 neutral; X |
| `0x104` | 4 move slots, stride `0x0E` | X |
| `0x13C` | current ability u16 | decomp only |
| `0x141` | form u8 | |
| `0x1F2` | Substitute HP u16 | |

Move slot: true move ID +0, PP +2, max PP +3; in-use move ID +6, PP +8, max PP +9. Use the in-use one, since Mimic and Transform replace it.

### Things that catch you out

- **Status:** a condition is active when the low 3 bits of its word are non-zero. The game takes the first hit from IDs 1 to 5, which sit at paralysis `0x20`, sleep `0x24`, freeze `0x28`, burn `0x2C`, poison `0x30`. [S, offsets I]
- **Badly poisoned:** `(conditions[5] & 0x1FF) == 0x79`. [I]
- **Types:** Roost hides Flying. If `conditions[24]` (`0x7C`) is active, treat a Flying type as none. [S]
- **Ability:** with Gastro Acid active (`conditions[16]`, `0x5C`), the effective ability is 0. [S]
- **Transform:** species stays the original; types, stats and ability copy the target; each in-use move becomes the target's with 5 max PP. [S]
- **Illusion:** `0x0C` holds the real species, so showing it spoils Zoroark. To show what the player sees, decrypt the party entry at `0x04` when bit 6 of `0x1B` is set. [S]
- **Enemy party address:** pokebot's first foe works out to `base+0x540D0`, `0x560` past the tracker's. Avoid both by using the record's `0x00` pointer.
- **Which copy `P` is:** the battle module holds client and server copies, `0xE8` bytes each, and no source says which `P` points into. [I]
- **Other languages:** pokebot's static addresses shift from Black 2 English by JP -0x640, EN +0x40, FR +0x40, IT -0xE0, DE -0xA0, ES -0x20. Language byte is at `0x023FFE0F`. The tracker refuses non-English ROMs, so `0x02000024` and the `base+` offsets are unverified outside `IRDO`.
- **Unchecked:** link and rotation battles. Flag value 3 being rotation is a guess.

## Encodings

**Species:** 0 none, 1-649 national dex in Gen 5, 650 egg. Gen 4 is 1-493. Forms are a separate field. [S]

**Types, Gen 5** (17, no Fairy, 17 = typeless): Normal 0, Fighting 1, Flying 2, Poison 3, Ground 4, Rock 5, Bug 6, Ghost 7, Steel 8, Fire 9, Water 10, Grass 11, Electric 12, Psychic 13, Ice 14, Dragon 15, Dark 16. [S]

**Types, Gen 4:** Mystery is 9, so Fire through Dark shift up by one, to 10-17. [S] So type IDs need a per-generation map.

**Effectiveness:** Gen 4 and 5 share a chart, and Steel still resists Ghost and Dark. The record has no move type, so we need our own move → type table. Abilities and items that change effectiveness (Levitate and so on) need their own handling. [S]

**Status, Gen 5:** a plain ID, not bits. 0 none, 1 paralysis, 2 sleep, 3 freeze, 4 burn, 5 poison, 6 confusion, on up to 35 aqua ring. [S]

**Status, Gen 4:** bits. Sleep counter bits 0-2, poison `0x8`, burn `0x10`, freeze `0x20`, paralysis `0x40`, toxic `0x80`, toxic counter bits 8-11. [S]

## Party Pokémon encryption, Gen 5

Only needed for Illusion. [X] 220 bytes: PID u32 at 0, flags u16 at 4, checksum u16 at 6, four shuffled 32-byte blocks at `0x08`, battle stats at `0x88`.

Cipher, one u16 at a time: `key = key * 0x41C64E6D + 0x6073` (mod 2^32), then `word ^= key >> 16`. The key advances before the first word. Blocks start with key = checksum, the battle section with key = PID. Skip the XOR if flag bit 0 (party) or bit 1 (box) says it's already decrypted. [I for the skip]

Block order: `index = ((pid & 0x3E000) >> 13) % 24`, then by index:

| 0-5 | 6-11 | 12-17 | 18-23 |
|---|---|---|---|
| ABCD ABDC ACBD ACDB ADBC ADCB | BACD BADC BCAD BCDA BDAC BDCA | CABD CADB CBAD CBDA CDAB CDBA | DABC DACB DBAC DBCA DCAB DCBA |

Block A has species at +0, item +2, ability +0xD (u8). Block B has moves at +0, PP +8, form in bits 3-7 of +0x18. Block C has the nickname at +0 (11 u16).

## Other games

- **Black 2:** same table as White 2; only battle status differs. [S]
- **Black / White (`IRBO`, `IRAO`):** no base pointer. Black: party `0x022349B4`, count `0x022349B0`, battle status `0x021D0798`, `P` `0x02269838`, flag `0x022A62F8`, trainer ID `0x022697BE`. White: all of those +`0x20`. Same `P` layout. [S] The record layout probably matches. [I]
- **Gen 4:** `a = read32(GLOBAL) & 0xFFFFFF`, `b = read32(0x02000000 + a + 0x20) & 0xFFFFFF`, address = `0x02000000 + b + offset`. `GLOBAL` is `0x02000BA8` for HGSS and Platinum, `0x02000B70` for DP. Battle status: HGSS `0x02246F48`, Pt `0x0224A55A`, DP `0x0223BB38`. Player record = stat stage offset minus `0x18`, enemy +`0xC0`, then player 2 and enemy 2. Stat stage offsets: HGSS `0x49E2C`, Pt `0x475D0`, DP `0x48598`. [S, record position I]
- **Gen 4 record:** species 0, Atk 2, Def 4, Spe 6, SpA 8, SpD 0xA, moves 0xC, stages 0x18 (HP, Atk, Def, Spe, SpA, SpD, Acc, Eva), type 1 0x24, type 2 0x25, form 0x26, ability 0x27, PP 0x2C, level 0x34, HP s32 0x4C, max HP u32 0x50, PID 0x68, status 0x6C, item 0x78. [S, X on species and PID]

## Sources

- NDS-Ironmon-Tracker (commit `05a0be0d`, GPL-3.0): https://github.com/Brian0255/NDS-Ironmon-Tracker, `ironmon_tracker/constants/MemoryAddresses.lua`, `BattleHandlerGen5.lua`, `PokemonDataReader.lua`.
- pokebw2 decomp of the US/Europe White 2 ROM (commit `c1e5dfec`, GPL-3.0, still in progress, names can change): https://github.com/fuddlesworth/pokebw2, `include/battle/btl_pokeparam.h`, `btl_main.h`, `src/ov167/btl_pokeparam.c`, `btl_calc.c`.
- pokebot-nds (MIT): https://github.com/wyanido/pokebot-nds, `lua/methods/gen_v.lua`, `b2w2.lua`, `gen_iv.lua`.
- pret/pokeplatinum: https://github.com/pret/pokeplatinum, `include/battle/battle_mon.h`.
- PKHeX: https://github.com/kwsch/PKHeX, `PKHeX.Core/PKM/Util/PokeCrypto.cs`.

## Verified in game

Run on the English White 2 (`IRDO`) with the software renderer on an Android emulator, 2026-11-05. Covered: wild singles, trainer singles, trainer doubles, a save state load mid-battle. Where this section disagrees with the sections above, this section wins.

**Confirmed:** `base` was always `0x02204D04`, even at boot. Species, level, HP, max HP, types, current ability, in-use moves and PP, and `P` all matched the screen. Stages confirmed for Atk and Def only. Condition words confirmed for paralysis only (`conditions[1]` = 1). Active mons do come first in `P`, and a switch swaps pointers. A fainted mon stays in its slot with HP 0, and its stages reset to 6. In doubles, slot 0 is the upper HP box and slot 1 the lower, on both sides.

**Corrections:**
- **Don't use battle status, the `0x41` byte, or the "data ready" check** to decide a battle is live. Status was always `0x2100` in battle, and `0x2101` and `0x2800` never appeared. All three stay "in battle" for 5 to 26 frames after the data is freed on exit, and one garbage pointer then reads as species 29604. Use the chain below.
- **`P` is the client copy**, which matches what's on screen: `P = main + 0xCC`. The server copy at `main + 0x1B4` holds results before the turn plays out. Never read it: it spoils the turn.
- **Player party** is at `base+0x530B0` going by `rec+0` for the first mon, not `base+0x53610`. The enemy party address `base+0x53B70` is right. Use the `rec+0` pointer anyway.
- **Trainer ID and the double flag aren't safe.** Trainer ID reads 0 early and in the outro. The flag is garbage outside battle and sets late. Use `input+0x54` (0 wild, 1 trainer) and `input+0x50` (0 singles, 1 doubles, higher values are triples and rotation per the decomp, not run).

**Live battle chain** (from the pokebw2 decomp, confirmed by running it):

```
P      = base + 0x526A8
main   = read32(P - 4)        -- main + 0xCC must equal P
core   = read32(main + 4)     -- read32(core) must equal main
scd    = read32(core + 0x1C4) -- scd+0xAC == main, scd+0xA8 == core
input  = read32(scd + 0xBC)
screen = read32(input + 0x58)
busy   = read32(input + 0x68) & 1
```

A battle is live when every pointer is in `0x02000000`-`0x02400000`, all the back-pointer tests pass, `read8(P+0x18)` is 1 to 6, and `screen <= 7`. Also check each record: species 1 to 650, level 1 to 100. `core` goes to 0 on the very first frame of an exit. Wait for the first `screen == 1` before drawing, so nothing shows during the intro. The final combined check wasn't run through an outro, only the chain.

**`screen` values:**

| Value | Meaning |
|---|---|
| 0 | Standby, messages, animations, bag, party list, L+A move info |
| 1 | Action menu, including the second mon in doubles |
| 2 | Move select |
| 3 | Target select |
| 4 | Yes/no (use next Pokémon or run) |

Move select is `screen == 2 and busy == 0`. `busy` is 1 for 9 to 14 frames while the buttons animate in. `read_s32(input+0x70) == -1` means no button has been pressed yet; it holds the pressed button for about 11 frames. Other fields: `scd+0x6C` is the record of the mon choosing, `input+0x300` is its slot (0 or 1). `input+0x2F0` is the 4 moves on screen, but it's stale outside `screen == 2`. Struggle goes 1 → 0 and never reaches 2.

**Other timing:** record HP drops at once while the on-screen bar animates, so it can lead the bar by a second or two. A save state load is fine in both directions, and the chain reads correctly in `on_state_loaded`. The script keeps running, so it must reset its own cached values there.

**Screen positions** (DS pixels, x, y, w, h):

| Item | Rect |
|---|---|
| Foe HP box, singles | 4, 30, 106, 22 |
| Your HP box, singles | 142, 112, 114, 28 |
| Foe HP boxes, doubles | upper 10, 18, 104, 23; lower 6, 41, 104, 22 |
| Your HP boxes, doubles | upper 142, 91, 114, 27; lower 146, 119, 110, 27 |
| Message band | 0, 148, 256, 41 |
| Move buttons, bottom screen | 2, 33, 124, 46; 130, 33, 122, 46; 2, 81, 124, 45; 130, 81, 122, 45 |

Inside a move button the name is centered at about y+11 to y+23, type and PP at about y+27 to y+37. "Helping Hand" spans about x+29 to x+94, leaving free slots at about x+6 to x+28 and x+95 to x+118. The camera pans constantly, so don't plan around sprites.

**Added while building the mod** (decomp, with the real values noted):
- `input+0x50` is a u32. Real values: 0 in singles, 1 in doubles.
- Mon IDs start at 0, 12, 6 and 18 per client, so your side is under 12. Real values: yours 0 and 1, foes 12 and 13. `P+0x18` client counts were 6,1,0,0 in singles and 6,2,0,0 in doubles.
- Other condition IDs: Foresight 17, Ingrain 21, Magnet Rise 30, Smack Down 31, Telekinesis 32. Miracle Eye shares 17: the word's parameter holds the type, 7 (Ghost) for Foresight and 16 (Dark) for Miracle Eye.
- Ingrain and Smack Down make Flying x1 for Ground per type (Steel/Flying takes x2, Water/Flying takes x1), and they beat Levitate, Magnet Rise and Telekinesis. Mold Breaker doesn't affect Magnet Rise.
- Roost on a pure Flying mon counts as Normal (`splitTypeCore`).
- The disguise's ability is in decrypted block A at +0xD.
- A faint clears the Illusion bit on the client copy with no break animation when the foe wasn't hit directly (`BattleClient_ScFaint`), so the real species can show after poison, burn or recoil. Don't show a fainted foe's species.
- In ROM (`IRDO`): move data is NARC `a/0/2/1`, with the sound flag at bit 8 of the flag word at `0x20`. Personal data, including form types, is `a/0/1/6`.

**Not run:** freeze, burn, poison, badly poisoned and confusion from a real battle (sleep has been seen); other stat stages; a foe sending out its next mon; Transform and a real Illusion (bit 6 of `0x1B` matches the decomp but no real Zoroark has been run); triples, rotation, multi and link battles; capture, level-up and evolution screens; other languages; Black 2. Encore and disabled moves are inferred to still open `screen == 2`.
