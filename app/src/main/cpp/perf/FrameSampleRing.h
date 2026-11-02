#ifndef FRAMESAMPLERING_H
#define FRAMESAMPLERING_H

#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * Lock-free single-producer, single-consumer ring of fixed-size int64 samples that drops (and counts) samples when full, allocated up front so
 * a push never allocates, blocks or does I/O.
 */
template<std::size_t Capacity, std::size_t Fields>
class FrameSampleRing
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    static constexpr std::size_t FIELDS = Fields;

    /**
     * Producer side: returns false if the sample was dropped because the buffer is full.
     */
    inline bool push(const int64_t (&sample)[Fields])
    {
        uint64_t write = writeIndex.load(std::memory_order_relaxed);
        uint64_t read = readIndex.load(std::memory_order_acquire);
        if (write - read >= Capacity)
        {
            droppedSamples.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        int64_t* slot = &data[(write & (Capacity - 1)) * Fields];
        for (std::size_t i = 0; i < Fields; i++)
            slot[i] = sample[i];

        writeIndex.store(write + 1, std::memory_order_release);
        return true;
    }

    /**
     * Consumer side: hands up to [maxSamples] samples to [copyRegion] (source pointer, destination offset in samples, count) in at most two
     * contiguous regions and returns how many it consumed.
     */
    template<typename CopyFunction>
    std::size_t drain(std::size_t maxSamples, CopyFunction copyRegion)
    {
        uint64_t read = readIndex.load(std::memory_order_relaxed);
        uint64_t write = writeIndex.load(std::memory_order_acquire);
        std::size_t available = (std::size_t) (write - read);
        std::size_t count = available < maxSamples ? available : maxSamples;
        if (count == 0)
            return 0;

        std::size_t start = (std::size_t) (read & (Capacity - 1));
        std::size_t firstChunk = Capacity - start;
        if (firstChunk > count)
            firstChunk = count;

        copyRegion(&data[start * Fields], (std::size_t) 0, firstChunk);
        if (count > firstChunk)
            copyRegion(&data[0], firstChunk, count - firstChunk);

        readIndex.store(read + count, std::memory_order_release);
        return count;
    }

    /**
     * Consumer side: discards all pending samples and resets the dropped sample counter.
     */
    void discardAll()
    {
        readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release);
        droppedSamples.store(0, std::memory_order_relaxed);
    }

    uint64_t getDroppedSampleCount() const
    {
        return droppedSamples.load(std::memory_order_relaxed);
    }

private:
    int64_t data[Capacity * Fields]{};
    std::atomic<uint64_t> writeIndex{0};
    std::atomic<uint64_t> readIndex{0};
    std::atomic<uint64_t> droppedSamples{0};
};

#endif //FRAMESAMPLERING_H
