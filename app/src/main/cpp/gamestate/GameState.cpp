#include "GameState.h"

#include <cstring>

#include "NDS.h"
#include "GPU_Shadow2D.h"
#include "pk_nds_field.h"

namespace MelonDSAndroid
{

// The field read's callbacks take no context, so the NDS being read is set around each read (emulator thread only, like PokeDaisy's ds)
static melonDS::NDS* readNds = nullptr;

// PokeDaisy's pk_nds.cpp mainRead32: main RAM only, through MainRAM like RetroAchievementsManager::PeekMemory, 0 elsewhere
static uint32_t mainRead32(uint32_t addr)
{
    if ((addr & 0xFF000000u) != 0x02000000u)
        return 0;

    const melonDS::u8* m = readNds->MainRAM;
    melonDS::u32 mask = readNds->MainRAMMask;
    return (uint32_t) m[addr & mask] | ((uint32_t) m[(addr + 1) & mask] << 8) | ((uint32_t) m[(addr + 2) & mask] << 16) |
        ((uint32_t) m[(addr + 3) & mask] << 24);
}

// melonadeDS's own carts, not PokeDaisy's (its tables stay verbatim in pk_nds_field.cpp; these are looked up before them).
//
// Diamond (ADAE rev 5, the US cart, and rev 13, a European cart): from pret/pokediamond, whose US Diamond build these carts' ARM9 and
// every overlay match byte for byte (module SHA-1s against arm9/pokediamond.us.sha1), so its address-named symbols are the carts' own.
// The FieldSystem is Platinum's ancestor: the process manager { parent, child, pause, kill } at 0, the field task at 0x10, the player
// avatar at 0x38 (its state at 0x18, a word before Platinum's), the field map's running flag at 0x64 (field_system.h, sub_020373AC,
// set by the field map's init), the location at 0x1C. The battle's child process runs UNK_020F2D94 (its main the battle overlay's
// state machine) and its data is the battle system, the battle context at 0x30 and its command at 8 as in Platinum (ov11_0222DF4C,
// ov11_0223D238). The field text box is on BG3 (sub_020545B8), Platinum's. Not mapped: the wild encounter rates and the sound (unused
// here: nothing writes RAM and nothing waits on the music). Pearl's ARM9 and field / battle overlays differ, so it isn't covered.
static const PkNdsFieldGame DIAMOND = {
    "Diamond (USA rev 5 / Europe rev 13)",
    0x021C5A08,   // the FieldSystem pointer (UNK_021C5A08)
    0x020F2D94,   // the battle's OverlayManagerTemplate (UNK_020F2D94)
    0x00, 0x10, 0x38, 0x64,
    0x18,
    0, 0, 0,      // no wild encounter rates. pk_nds_field_state doesn't treat a 0 here as unmapped: with a write32 and the
                  // WILD ENCOUNTERS option it would write into the process manager. Map these before passing a write32.
    0x1C,
    0x021D767D,   // the field map's main (ov05_021F64C8's)
    0, 0,         // no sound
    0,
    0,            // no pk_nds_call functions
    0x30,
    0x08,
};

// pk_nds_bg3_tiles for Diamond's field text box on BG3: the tiles in its map below the map name banner, which takes the top
// kDiamondBannerRows rows (sub_020545B8's text box is in rows 18-23, the banner slides in over rows 0-2)
static constexpr int kDiamondBannerRows = 3;

static int diamondTextBoxTiles(melonDS::NDS* nds)
{
    uint32_t disp = nds->GPU.GPU2D_A.DispCnt;
    if (!(disp & (1 << 11)) || (disp & 7) > 1)
        return 0;   // BG3 off, or not a text BG in this mode
    uint16_t cnt = nds->GPU.GPU2D_A.BGCnt[3];
    uint32_t base = 0x06000000u + ((disp >> 27) & 7) * 0x10000 + ((cnt >> 8) & 0x1F) * 0x800;
    int n = 0;
    for (uint32_t i = kDiamondBannerRows * 32; i < 32 * 24; i++)
        if (nds->ARM9Read16(base + i * 2) & 0x3FF)
            n++;
    return n;
}

// The cart's field read: ours first, then PokeDaisy's
static const PkNdsFieldGame* findGame(const melonDS::u8* rom, melonDS::u32 romLength)
{
    if (!rom)
        return nullptr;
    if (romLength >= 0x20)
    {
        int revision = rom[0x1E];
        if (std::memcmp(rom + 0xC, "ADAE", 4) == 0 && (revision == 5 || revision == 13))
            return &DIAMOND;
        // US Platinum rev 0 is rev 1's code: pret/pokeplatinum builds both, and its checksums (platinum.us/sbins_*.sha1) share the ARM9
        // and every overlay but the GTS one, which only changes an argument (checked against the cart: ARM9 and all 122 overlays match).
        // So it's PokeDaisy's rev 1 table as it is, touch rules too
        if (std::memcmp(rom + 0xC, "CPUE", 4) == 0 && revision == 0)
            return pk_nds_field_game("CPUE", 1);
    }
    return pk_nds_field_game_rom(rom, romLength);
}

void GameState::setCart(const melonDS::u8* rom, melonDS::u32 romLength)
{
    game.store(findGame(rom, romLength), std::memory_order_relaxed);
    isScriptWanted = false;
    framesSincePoll.store(kPollFrames, std::memory_order_relaxed);
    reset();
}

void GameState::reset()
{
    scene.store(Unknown, std::memory_order_relaxed);
    isResetPending.store(true, std::memory_order_relaxed);
}

void GameState::update(melonDS::NDS* nds)
{
    const PkNdsFieldGame* currentGame = game.load(std::memory_order_relaxed);
    if (!currentGame) [[likely]]
        return;

    int sincePoll = framesSincePoll.load(std::memory_order_relaxed);
    bool isWanted = isScriptWanted || sincePoll < kPollFrames;
    if (sincePoll < kPollFrames)
        framesSincePoll.store(sincePoll + 1, std::memory_order_relaxed);
    scene.store(isWanted ? readScene(nds, currentGame) : Unknown, std::memory_order_relaxed);
}

int GameState::pollScene()
{
    framesSincePoll.store(0, std::memory_order_relaxed);
    return scene.load(std::memory_order_relaxed);
}

int GameState::getSceneForScript(melonDS::NDS* nds)
{
    const PkNdsFieldGame* currentGame = game.load(std::memory_order_relaxed);
    if (!currentGame)
        return Unknown;

    isScriptWanted = true;
    int current = scene.load(std::memory_order_relaxed);
    if (current == Unknown)
    {
        // The first call, or the first since a reset or a state load: read it from the RAM as it is now
        current = readScene(nds, currentGame);
        scene.store(current, std::memory_order_relaxed);
    }
    return current;
}

int GameState::readScene(melonDS::NDS* nds, const PkNdsFieldGame* currentGame)
{
    // pk_nds_field_reset_wild also forgets the Gen 5 battle prompt the touch rules follow; nothing here writes RAM (write32 is null), so the
    // WILD ENCOUNTERS hold it's named for never starts
    if (isResetPending.exchange(false, std::memory_order_relaxed))
        pk_nds_field_reset_wild();

    // PokeDaisy's pk_ndsm_field_bits, the parts the scene needs
    readNds = nds;
    int bits = pk_nds_field_state(currentGame, mainRead32, nullptr, 0);
    readNds = nullptr;
    // A field task alone (a warp, a door) isn't a text box: the text box's BG has to show tiles. Diamond's map name banner is on that BG
    // too, in its top rows, and stays up while the player walks out of a door (a field task), which read as a text box for 40-70 frames
    // after the fade, longer than the camera's after-fade window once the frame rate drops (WideCamera.AFTER_FADE_TEXT_SECONDS). So in
    // Diamond only the rows under the banner count
    if ((bits & PK_NDS_FIELD_TEXT) && !(currentGame == &DIAMOND ? diamondTextBoxTiles(nds)
                                                                 : pk_nds_bg3_tiles(nds, currentGame->textBg ? currentGame->textBg : 3)))
        bits &= ~PK_NDS_FIELD_TEXT;

    // PokeDaisy's pk_ndsm_wide_scene
    if (bits & PK_NDS_FIELD_BATTLE)
        return Battle;
    // The top screen faded all the way to black or white (master brightness mode 1 or 2 at 16): between two maps (a door, a warp; the field
    // process stops meanwhile) or two screens. Decides nothing: the zoom holds through it. PokeDaisy reads engine A, which is the top screen in
    // these games' field; its comment means the top screen, so this reads whichever engine POWCNT1 bit 15 puts there (GPU::ScreenSwap, set
    // when engine A is on top)
    const melonDS::GPU2D::Unit& topEngine = nds->GPU.ScreenSwap ? nds->GPU.GPU2D_A : nds->GPU.GPU2D_B;
    melonDS::u16 masterBrightness = topEngine.MasterBrightness;
    int brightnessMode = masterBrightness >> 14;
    if ((brightnessMode == 1 || brightnessMode == 2) && (masterBrightness & 0x1F) >= 16)
        return Fade;
    if (bits & PK_NDS_FIELD_MENU)
        return Menu;
    if (!(bits & PK_NDS_FIELD_SCREEN))
        return Off;
    return (bits & PK_NDS_FIELD_TEXT) ? Text : Field;
}

bool GameState::hasWideBattles() const
{
    const PkNdsFieldGame* currentGame = game.load(std::memory_order_relaxed);
    return currentGame && pk_nds_battle_margins(currentGame);
}

bool GameState::updateWideBattle(bool isWideView)
{
    // The scene read after the last frame ran, so the frame after a battle's last one still has its modes (and the output at the battle's
    // scale). Left as it is, as it doesn't show where we've seen a battle end (running from a SoulSilver wild battle): the scene stays a battle
    // until the top screen has faded to black, so that frame is black either way, as are the frames the shadow and the cap switch on
    isWideBattle = isWideView && hasWideBattles() && pollScene() == Battle;
    if (isWideBattle)
        wideBattleHold = kWideBattleHoldFrames;
    else if (wideBattleHold > 0)
        wideBattleHold--;
    return wideBattleHold > 0;
}

void GameState::applyWideBattle(melonDS::NDS* nds) const
{
    using namespace melonDS::Shadow2D;
    // PokeDaisy's battle compose (pk_nds_wide.cpp, pk_nds_wide_frame): BG3 is the battle's background, drawn on with its scroll, BLDCNT
    // fades and the master brightness (the shadow's composite has all three); a sprite behind the 3D is a platform or a trainer, drawn on from
    // its tiles; a BG over BG3 showing at the screen's edge (the text box's dark frame) repeats the edge on its rows; and the HP boxes at the
    // sides are carried a few pixels past it and closed there
    static constexpr melonDS::u32 kBattleModes = WideModes(Wide_Off, Wide_Repeat, Wide_Repeat, Wide_Draw, Wide_Behind3D) | Wide_CarryBoxes;
    static constexpr melonDS::u32 kBattleExtents = 0xFFu << 24;

    nds->GPU.SetShadow2DWide(isWideBattle ? kBattleModes : 0, isWideBattle ? kBattleExtents : 0, isWideBattle ? kBattleExtents : 0);
}

const char* GameState::getSceneName(int scene)
{
    switch (scene)
    {
        case Field: return "field";
        case Text: return "text";
        case Battle: return "battle";
        case Off: return "off";
        case Fade: return "fade";
        case Menu: return "menu";
        default: return nullptr;
    }
}

}
