package me.magnum.melonds.ui.common

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CarriedOverKeyFilterTest {

    private val confirm = 23
    private val filter = CarriedOverKeyFilter()

    @Test
    fun aPressThatBeganOnThePreviousScreenIsIgnoredThroughItsRelease() {
        assertTrue(filter.shouldIgnore(confirm, isDown = true, repeatCount = 5))
        assertTrue(filter.shouldIgnore(confirm, isDown = true, repeatCount = 6))
        assertTrue(filter.shouldIgnore(confirm, isDown = false, repeatCount = 0))
    }

    @Test
    fun theNextPressAfterItWorks() {
        filter.shouldIgnore(confirm, isDown = true, repeatCount = 5)
        filter.shouldIgnore(confirm, isDown = false, repeatCount = 0)

        assertFalse(filter.shouldIgnore(confirm, isDown = true, repeatCount = 0))
        assertFalse(filter.shouldIgnore(confirm, isDown = false, repeatCount = 0))
    }

    @Test
    fun holdingAKeyPressedHereStaysUntouched() {
        assertFalse(filter.shouldIgnore(confirm, isDown = true, repeatCount = 0))
        assertFalse(filter.shouldIgnore(confirm, isDown = true, repeatCount = 1))
        assertFalse(filter.shouldIgnore(confirm, isDown = false, repeatCount = 0))
    }

    @Test
    fun otherKeysAreNotAffected() {
        filter.shouldIgnore(confirm, isDown = true, repeatCount = 5)

        assertFalse(filter.shouldIgnore(20, isDown = true, repeatCount = 0))
        assertFalse(filter.shouldIgnore(20, isDown = false, repeatCount = 0))
    }

    @Test
    fun aReleaseWithNothingBeforeItIsLeftAlone() {
        assertFalse(filter.shouldIgnore(confirm, isDown = false, repeatCount = 0))
    }
}
