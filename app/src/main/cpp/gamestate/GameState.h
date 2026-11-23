#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <atomic>
#include "types.h"

namespace melonDS { class NDS; }
struct PkNdsFieldGame;

namespace MelonDSAndroid
{

/**
 * What a Pokemon game PokeDaisy's field read knows is doing this frame (the field, a text box, a battle, a menu screen, a fade), for the
 * WideMelon camera's auto zoom and melon.game.state(). The read is PokeDaisy's pk_nds_field.cpp, copied as it is next to this file; this is
 * its shim: RAM through MainRAM, the cart found at ROM load, and one update per emulated frame, only for a known cart that something asked
 * about lately, so other games pay nothing.
 */
class GameState
{
public:
    /**
     * The scenes, numbered like PokeDaisy's WideCamera.SCENE_* (WideCamera.kt's GameScene has the same numbers). PokeDaisy's DS build calls a
     * battle SCENE_OFF; it's BATTLE here (PokeDaisy's SCENE_BATTLE_START number) so scripts can tell it apart
     */
    enum Scene : int
    {
        Unknown = -1,
        Field = 0,
        Text = 1,
        Battle = 3,
        Off = 4,
        Fade = 5,
        Menu = 6,
    };

    /**
     * The cart just loaded ([rom] null for none): looks it up in the field read's tables, and forgets what scripts asked for. Before the
     * emulator thread runs the game
     */
    void setCart(const melonDS::u8* rom, melonDS::u32 romLength);
    /**
     * Forgets the scene (a reset or a state load); any thread
     */
    void reset();
    /**
     * Reads the scene after a frame ran, if the cart is known and the camera polled lately or a script asked; emulator thread
     */
    void update(melonDS::NDS* nds);
    /**
     * The scene of the last update, Unknown for an unknown cart or while nothing wanted it. Polling counts as wanting it for the next
     * kPollFrames frames, so the camera keeps it updated just by asking; any thread
     */
    int pollScene();
    /**
     * melon.game.state(): the scene, read now if the last frame didn't, and kept updated for the rest of the game; emulator thread. Unlike
     * pollScene's, a field or text box whose top screen doesn't show its 3D (isTop3DCovered) is a fade
     */
    int getSceneForScript(melonDS::NDS* nds);

    /**
     * Whether the cart is a Gen 4 game whose battles WideMelon draws wide (PokeDaisy's pk_nds_battle_margins: HG/SS, Platinum and Diamond
     * battles are 2D, with only the Pokemon in 3D; Gen 5's are all 3D and get the 3D wings like the field); any thread
     */
    bool hasWideBattles() const;
    /**
     * WideMelon's Gen 4 battles, before each frame: reads the last frame's scene (polling it, so it stays updated while this wants it) for
     * applyWideBattle, and returns whether the GPU 2D shadow should be in its wide battle mode (3) for this frame: from the first battle frame
     * until kWideBattleHoldFrames frames after the last, so a scene that drops out of a battle for a frame doesn't start the recorder over,
     * and never outside that, so the rest of the game runs as without the shadow. [isWideView] is whether the top screen is wider than 256;
     * emulator thread
     */
    bool updateWideBattle(bool isWideView);
    /**
     * PokeDaisy's EXPAND battle margins on the GPU 2D shadow: unit A's wide modes for this frame, from what updateWideBattle read. In a battle
     * the background (BG3) goes on past the screen with the sprites behind the Pokemon (platforms, trainers), and the text box's rows repeat
     * the screen's edge, and the HP boxes at the sides are carried a few pixels past it; anywhere else nothing does. After the shadow's mode
     * is set, as the shadow drops them while it's off, and before the renderer's SetOutputNeed, whose output cap follows them; emulator thread
     */
    void applyWideBattle(melonDS::NDS* nds) const;

    static const char* getSceneName(int scene);

private:
    int readScene(melonDS::NDS* nds, const PkNdsFieldGame* game);
    // Whether engine A is on top and shows no 3D of its own: its 3D layer is off, the display isn't the layers, or a direct-color bitmap BG
    // (a captured frame) is up. Gen 5's doors zoom the old map's last frame with the 3D off, then cross-fade it into the new map's 3D, which
    // the depth hands over as 2D meanwhile, while the field read already says field
    static bool isTop3DCovered(melonDS::NDS* nds);

    // Frames the update keeps running after the last poll; generous, since fast-forward runs several frames per presented one
    static constexpr int kPollFrames = 240;
    // Frames the shadow's wide battle mode stays on after the last battle frame (updateWideBattle). Only the recorder runs meanwhile: with
    // no wide modes it draws nothing and the output cap is back (GLCompositor::OutputNeed)
    static constexpr int kWideBattleHoldFrames = 16;

    std::atomic<const PkNdsFieldGame*> game = nullptr;
    std::atomic<int> scene = Unknown;
    std::atomic<int> framesSincePoll = kPollFrames;
    std::atomic_bool isResetPending = false;
    // Emulator thread only
    bool isScriptWanted = false;
    // updateWideBattle's: whether the last frame's scene was a battle, and the frames the wide battle mode has left after it
    bool isWideBattle = false;
    int wideBattleHold = 0;
};

}

#endif //GAMESTATE_H
