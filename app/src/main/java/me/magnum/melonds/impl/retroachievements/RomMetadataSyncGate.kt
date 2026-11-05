package me.magnum.melonds.impl.retroachievements

/**
 * Decides when the metadata sync may run: only with a key, and never while a game is running, because of the frame budget.
 *
 * Games are counted rather than flagged. When a game is closed and another one opened right away, the new game's start can come before the old
 * one's end, and that end mustn't let the sync back in. Not thread safe, so callers have to synchronize.
 */
class RomMetadataSyncGate {

    private var runningGames = 0

    fun canEnqueue(hasKey: Boolean): Boolean = hasKey && runningGames == 0

    /**
     * @return True if a sync may be there to cancel. Removing the key cancels it too, so without a key there's nothing to do
     */
    fun onGameStarted(hasKey: Boolean): Boolean {
        runningGames++
        return hasKey
    }

    /**
     * @return True if a sync should be enqueued now
     */
    fun onGameEnded(hasKey: Boolean): Boolean {
        runningGames = (runningGames - 1).coerceAtLeast(0)
        return canEnqueue(hasKey)
    }
}
