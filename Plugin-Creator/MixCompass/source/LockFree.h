#pragma once
// Triple buffer lock-free, un solo scrittore e un solo lettore: lo scrittore non aspetta mai, il
// lettore prende sempre l'ultimo valore completo. Serve a passare lo Snapshot (struct grande)
// dal thread di analisi alla UI senza lock (CLAUDE.md: UI ← DSP solo via atomic).
#include <atomic>

template <typename T>
class TripleBuffer
{
public:
    // --- lato scrittore ---
    T& back() noexcept { return buffers[backIndex]; }
    void publish() noexcept
    {
        const int old = state.exchange (backIndex | dirtyBit, std::memory_order_acq_rel);
        backIndex = old & indexMask;
    }

    // --- lato lettore: nullptr se non c'è niente di nuovo dall'ultima lettura ---
    const T* readLatest() noexcept
    {
        if ((state.load (std::memory_order_acquire) & dirtyBit) == 0)
            return nullptr;
        const int old = state.exchange (frontIndex, std::memory_order_acq_rel);
        frontIndex = old & indexMask;
        return &buffers[frontIndex];
    }

private:
    static constexpr int dirtyBit = 4, indexMask = 3;
    T buffers[3] {};
    std::atomic<int> state { 1 };   // indice del buffer "di mezzo" + bit "nuovo"
    int backIndex = 0, frontIndex = 2;
};
