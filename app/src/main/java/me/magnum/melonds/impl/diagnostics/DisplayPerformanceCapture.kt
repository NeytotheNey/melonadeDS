package me.magnum.melonds.impl.diagnostics

import java.util.concurrent.atomic.AtomicLong

/**
 * Collects one sample per presented display frame for [PerformanceLogger]. Samples are recorded by the frame render thread (single producer) and
 * drained by the logger (single consumer) through a preallocated lock-free ring, so recording never allocates or blocks.
 *
 * Each sample takes [FIELDS] entries: presentation timestamp (ns, [System.nanoTime]), render CPU duration (ns), flags ([FLAG_VALID],
 * [FLAG_NEW_FRAME], [FLAG_EMULATION_PAUSED], [FLAG_DS_SLEEP], [FLAG_FAST_FORWARD]), the number of emulator frames that were skipped (produced
 * but never presented) before this one and the display's vsync period when it was rendered (ns, 0 if unknown).
 */
object DisplayPerformanceCapture {
    const val FIELDS = 5
    const val FLAG_VALID = 1L
    const val FLAG_NEW_FRAME = 2L
    const val FLAG_EMULATION_PAUSED = 4L
    const val FLAG_DS_SLEEP = 8L
    const val FLAG_FAST_FORWARD = 16L

    // Enough for several seconds of samples even at 144Hz
    private const val CAPACITY = 1024

    private val data = LongArray(CAPACITY * FIELDS)
    private val writeIndex = AtomicLong(0)
    private val readIndex = AtomicLong(0)
    private val droppedSamples = AtomicLong(0)

    @Volatile
    var isEnabled = false
        private set

    // Set by the consumer when capture starts so that the producer forgets the last frame of a previous capture
    @Volatile private var resetFrameSequence = true
    // Only accessed by the producer
    private var lastFrameSequence = 0L

    /**
     * Must be called from the consumer. Enabling discards pending samples.
     */
    fun setEnabled(enabled: Boolean) {
        if (enabled) {
            readIndex.set(writeIndex.get())
            droppedSamples.set(0)
            resetFrameSequence = true
        }
        isEnabled = enabled
    }

    /**
     * Must be called from the render thread.
     */
    fun record(timestampNs: Long, renderDurationNs: Long, isValidFrame: Boolean, frameSequence: Long, isEmulationPaused: Boolean, isDsSleeping: Boolean, isFastForward: Boolean, refreshPeriodNs: Long) {
        if (resetFrameSequence) {
            resetFrameSequence = false
            lastFrameSequence = 0L
        }

        val isNewFrame = isValidFrame && frameSequence != lastFrameSequence
        // The sequence restarts when a new emulator instance is created, so only count skipped frames when it moves forward
        val skippedFrames = if (isNewFrame && lastFrameSequence != 0L && frameSequence > lastFrameSequence) {
            frameSequence - lastFrameSequence - 1
        } else {
            0L
        }
        if (isValidFrame) {
            lastFrameSequence = frameSequence
        }

        val write = writeIndex.get()
        if (write - readIndex.get() >= CAPACITY) {
            droppedSamples.incrementAndGet()
            return
        }

        val offset = (write % CAPACITY).toInt() * FIELDS
        data[offset] = timestampNs
        data[offset + 1] = renderDurationNs
        data[offset + 2] = (if (isValidFrame) FLAG_VALID else 0L) or
                (if (isNewFrame) FLAG_NEW_FRAME else 0L) or
                (if (isEmulationPaused) FLAG_EMULATION_PAUSED else 0L) or
                (if (isDsSleeping) FLAG_DS_SLEEP else 0L) or
                (if (isFastForward) FLAG_FAST_FORWARD else 0L)
        data[offset + 3] = skippedFrames
        data[offset + 4] = refreshPeriodNs
        // Publishes the sample to the consumer
        writeIndex.lazySet(write + 1)
    }

    /**
     * Must be called from the consumer. Copies pending samples into [buffer] and returns the number of samples copied.
     */
    fun drain(buffer: LongArray): Int {
        val read = readIndex.get()
        val write = writeIndex.get()
        val count = minOf(write - read, (buffer.size / FIELDS).toLong()).toInt()

        for (i in 0 until count) {
            val offset = ((read + i) % CAPACITY).toInt() * FIELDS
            System.arraycopy(data, offset, buffer, i * FIELDS, FIELDS)
        }

        // Lets the producer reuse the slots
        readIndex.lazySet(read + count)
        return count
    }

    fun getDroppedSampleCount(): Long {
        return droppedSamples.get()
    }
}
