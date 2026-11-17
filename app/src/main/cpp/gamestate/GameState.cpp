#include "GameState.h"

#include "NDS.h"
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

void GameState::setCart(const melonDS::u8* rom, melonDS::u32 romLength)
{
    game.store(rom ? pk_nds_field_game_rom(rom, romLength) : nullptr, std::memory_order_relaxed);
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
    // A field task alone (a warp, a door) isn't a text box: the text box's BG has to show tiles
    if ((bits & PK_NDS_FIELD_TEXT) && !pk_nds_bg3_tiles(nds, currentGame->textBg ? currentGame->textBg : 3))
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
