package me.magnum.melonds.impl.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Test

class DebugSwitchesTest {

    @Test
    fun emptyOrBlankTextHasNoSwitches() {
        assertEquals(DebugSwitches.Result(emptyList(), emptyList()), DebugSwitches.parse(null))
        assertEquals(DebugSwitches.Result(emptyList(), emptyList()), DebugSwitches.parse(""))
        assertEquals(DebugSwitches.Result(emptyList(), emptyList()), DebugSwitches.parse("  \n , \n"))
    }

    @Test
    fun commasAndNewLinesSeparatePairs() {
        val result = DebugSwitches.parse("hash3d=1, gltimers=2\nscenestats=1\r\ncomputedepth=1")

        assertEquals(listOf("hash3d" to "1", "gltimers" to "2", "scenestats" to "1", "computedepth" to "1"), result.switches)
        assertEquals(emptyList<String>(), result.errors)
    }

    @Test
    fun bitmaskSwitchesKeepTheirValue() {
        val result = DebugSwitches.parse("debug.melonade.gltrans=3, scenestats=1, hash3d=1")

        assertEquals(listOf("gltrans" to "3", "scenestats" to "1", "hash3d" to "1"), result.switches)
        assertEquals("gltrans=3\nscenestats=1\nhash3d=1", result.toNativeString())
    }

    @Test
    fun prefixIsOptionalAndNamesAreLowercased() {
        val result = DebugSwitches.parse("debug.melonade.hash3d=1,DEBUG.MELONADE.GLTimers=1, ComputeTimers = 1")

        assertEquals(listOf("hash3d" to "1", "gltimers" to "1", "computetimers" to "1"), result.switches)
    }

    @Test
    fun valuesKeepTheirCase() {
        assertEquals(listOf("hirescap" to "log", "gpu2d" to "All"), DebugSwitches.parse("hirescap=log, gpu2d=All").switches)
    }

    @Test
    fun commaPiecesWithoutEqualsContinueTheValueBefore() {
        val result = DebugSwitches.parse("cpusemu=4,5, 6,7, hash3d=1\ncpus2d=1,2")

        assertEquals(listOf("cpusemu" to "4,5,6,7", "hash3d" to "1", "cpus2d" to "1,2"), result.switches)
        assertEquals(emptyList<String>(), result.errors)
    }

    @Test
    fun aContinuationDoesNotCrossLines() {
        val result = DebugSwitches.parse("cpusemu=4\n5")

        assertEquals(listOf("cpusemu" to "4"), result.switches)
        assertEquals(listOf("5"), result.errors)
    }

    @Test
    fun theLastValueOfARepeatedNameWins() {
        assertEquals(listOf("gltimers" to "1", "hash3d" to "0"), DebugSwitches.parse("hash3d=1, gltimers=1, hash3d=0").switches)
    }

    @Test
    fun commentLinesAreSkipped() {
        assertEquals(listOf("hash3d" to "1"), DebugSwitches.parse("# baseline run\nhash3d=1\n  # gltimers=2").switches)
    }

    @Test
    fun badPiecesAreReportedAndLeftOut() {
        val result = DebugSwitches.parse("=1, hash3d=, bad.name=1, weird-name=2, a=b=c, gltimers=1, 4")

        // "4" follows a valid pair, so it continues gltimers' value
        assertEquals(listOf("gltimers" to "1,4"), result.switches)
        assertEquals(listOf("=1", "hash3d=", "bad.name=1", "weird-name=2", "a=b=c"), result.errors)
    }

    @Test
    fun aPieceAfterABadPairIsNotGluedToIt() {
        val result = DebugSwitches.parse("bad-name=1,2")

        assertEquals(emptyList<Pair<String, String>>(), result.switches)
        assertEquals(listOf("bad-name=1", "2"), result.errors)
    }

    @Test
    fun namesAndValuesOverAndroidsPropertyLimitsAreLeftOut() {
        val longName = "a".repeat(17)
        val longValue = "1".repeat(92)
        val result = DebugSwitches.parse("$longName=1, ${"b".repeat(16)}=1, hash3d=$longValue, gltimers=${"2".repeat(91)}")

        assertEquals(listOf("b".repeat(16) to "1", "gltimers" to "2".repeat(91)), result.switches)
        assertEquals(listOf("$longName=1", "hash3d=$longValue"), result.errors)
    }

    @Test
    fun spacesSeparatePairsLikeCommas() {
        val result = DebugSwitches.parse("hash3d=1 gltimers=1\tscenestats=2,  computedepth=1")

        assertEquals(listOf("hash3d" to "1", "gltimers" to "1", "scenestats" to "2", "computedepth" to "1"), result.switches)
        assertEquals(emptyList<String>(), result.errors)
    }

    @Test
    fun describedSwitchesParseBackToTheSame() {
        val result = DebugSwitches.parse("debug.melonade.hash3d=1, cpusemu=4,5,6, gltimers = 2")

        assertEquals(result, DebugSwitches.parse(result.describe()))
        assertEquals(listOf("hash3d" to "1", "cpusemu" to "4,5,6", "gltimers" to "2"), result.switches)
    }

    @Test
    fun onlyACommaContinuesAValue() {
        val result = DebugSwitches.parse("cpusemu=4,5 6 hash3d=1")

        assertEquals(listOf("cpusemu" to "4,5", "hash3d" to "1"), result.switches)
        assertEquals(listOf("6"), result.errors)
    }

    @Test
    fun spacedOutPairsStillWork() {
        val result = DebugSwitches.parse("bad name=1")

        // a space separates, so this is a stray word and a pair
        assertEquals(listOf("name" to "1"), result.switches)
        assertEquals(listOf("bad"), result.errors)
    }

    @Test
    fun nativeStringIsOnePairPerLine() {
        val result = DebugSwitches.parse("debug.melonade.hash3d=1, cpusemu=4,5")

        assertEquals("hash3d=1\ncpusemu=4,5", result.toNativeString())
        assertEquals("hash3d=1 cpusemu=4,5", result.describe())
    }
}
