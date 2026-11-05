package me.magnum.melonds.impl.retroachievements

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RomMetadataSyncGateTest {

    private val gate = RomMetadataSyncGate()

    @Test
    fun withNoGameRunningOnlyAKeyMatters() {
        assertTrue(gate.canEnqueue(hasKey = true))
        assertFalse(gate.canEnqueue(hasKey = false))
    }

    @Test
    fun startingAGameCancelsTheSyncIfThereCanBeOne() {
        assertTrue(RomMetadataSyncGate().onGameStarted(hasKey = true))
        assertFalse(RomMetadataSyncGate().onGameStarted(hasKey = false))
    }

    @Test
    fun nothingIsEnqueuedWhileAGameRuns() {
        gate.onGameStarted(hasKey = true)

        // A scan finishing late, or a key saved from the in-game settings
        assertFalse(gate.canEnqueue(hasKey = true))
    }

    @Test
    fun leavingTheGameEnqueuesASyncIfAKeyIsSaved() {
        gate.onGameStarted(hasKey = true)

        assertTrue(gate.onGameEnded(hasKey = true))
        assertTrue(gate.canEnqueue(hasKey = true))
    }

    @Test
    fun leavingTheGameEnqueuesNothingWithoutAKey() {
        gate.onGameStarted(hasKey = false)

        assertFalse(gate.onGameEnded(hasKey = false))
    }

    @Test
    fun aKeySavedDuringTheGameIsPickedUpOnLeaving() {
        gate.onGameStarted(hasKey = false)

        assertFalse(gate.canEnqueue(hasKey = true))
        assertTrue(gate.onGameEnded(hasKey = true))
    }

    @Test
    fun aKeyRemovedDuringTheGameIsRespectedOnLeaving() {
        gate.onGameStarted(hasKey = true)

        assertFalse(gate.onGameEnded(hasKey = false))
    }

    @Test
    fun theOldGamesEndDoesNotLetTheSyncIntoTheNextGame() {
        gate.onGameStarted(hasKey = true)
        gate.onGameStarted(hasKey = true)

        assertFalse(gate.onGameEnded(hasKey = true))
        assertFalse(gate.canEnqueue(hasKey = true))

        assertTrue(gate.onGameEnded(hasKey = true))
    }

    @Test
    fun anExtraEndCannotCancelOutTheNextStart() {
        gate.onGameEnded(hasKey = true)

        gate.onGameStarted(hasKey = true)

        assertFalse(gate.canEnqueue(hasKey = true))
    }
}
