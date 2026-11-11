package me.magnum.melonds.impl.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Test

class SystemSamplerParsingTest {

    private fun bytes(text: String) = text.toByteArray(Charsets.US_ASCII)

    @Test
    fun statTicksAreUtimePlusStime() {
        val line = bytes("1234 (EmuThread) S 1 1234 0 0 -1 4194368 100 0 0 0 250 75 0 0 20 0 30 0 12345 0 0\n")
        assertEquals(325L, SystemSampler.parseStatCpuTicks(line, line.size))
        val fields = LongArray(2)
        assertEquals(true, SystemSampler.parseStatFields(line, line.size, fields))
        assertEquals(325L, fields[0])
        // Field 22, used to tell a reused thread ID from the thread that had it before
        assertEquals(12345L, fields[1])
    }

    @Test
    fun statNameWithSpacesAndParenthesesIsSkipped() {
        val line = bytes("42 (a) b (c)) R 1 42 0 0 -1 0 0 0 0 0 7 3 0 0 20 0 1 0 1 0 0")
        assertEquals(10L, SystemSampler.parseStatCpuTicks(line, line.size))
        val range = SystemSampler.findStatName(line, line.size)
        val start = (range ushr 32).toInt()
        val end = (range and 0xFFFFFFFFL).toInt()
        assertEquals("a) b (c)", String(line, start, end - start, Charsets.US_ASCII))
    }

    @Test
    fun truncatedStatLineIsRejected() {
        val line = bytes("1 (x) S 1 2 3")
        assertEquals(-1L, SystemSampler.parseStatCpuTicks(line, line.size))
    }

    @Test
    fun numberListsParse() {
        val out = LongArray(8)
        val text = bytes("  120 340\n")
        assertEquals(2, SystemSampler.parseLongs(text, text.size, out))
        assertEquals(120L, out[0])
        assertEquals(340L, out[1])

        val clockStats = bytes("0 0 15 2000 30000 0")
        assertEquals(6, SystemSampler.parseLongs(clockStats, clockStats.size, out))
        assertEquals(30000L, out[4])
    }
}
