package me.magnum.melonds.impl

import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MapThrottledTest {

    @Test
    fun aBurstCostsAtMostTwoRebuildsAndEndsOnTheLatest() {
        var transforms = 0

        val results = runBlocking {
            (1..100).asFlow().mapThrottled(intervalMs = 50) {
                transforms++
                it
            }.toList()
        }

        assertTrue("$transforms transforms", transforms <= 2)
        assertEquals(100, results.last())
    }

    @Test
    fun aSingleValueIsHandled() {
        assertEquals(listOf(1), runBlocking { listOf(1).asFlow().mapThrottled(intervalMs = 1) { it }.toList() })
    }
}
