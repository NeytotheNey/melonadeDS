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
     * melon.game.state(): the scene, read now if the last frame didn't, and kept updated for the rest of the game; emulator thread
     */
    int getSceneForScript(melonDS::NDS* nds);

    static const char* getSceneName(int scene);

private:
    int readScene(melonDS::NDS* nds, const PkNdsFieldGame* game);

    // Frames the update keeps running after the last poll; generous, since fast-forward runs several frames per presented one
    static constexpr int kPollFrames = 240;

    std::atomic<const PkNdsFieldGame*> game = nullptr;
    std::atomic<int> scene = Unknown;
    std::atomic<int> framesSincePoll = kPollFrames;
    std::atomic_bool isResetPending = false;
    // Emulator thread only
    bool isScriptWanted = false;
};

}

#endif //GAMESTATE_H
