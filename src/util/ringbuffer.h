//
// util/ringbuffer.h
//
// Lockfreie SPSC-Ringbuffer für VelvetKeys Multicore.
// Single-Producer-Single-Consumer, keine Locks, kein Warten.
//
// Design-Ziele:
// - DMA-IRQ (Core 0) darf NIEMALS blockieren
// - Core 1 (Audio-Producer) darf niemals auf Core 0 warten
// - Lockfrei via C++11 atomics + memory barriers
//

#ifndef _VELVETKEYS_RINGBUFFER_H
#define _VELVETKEYS_RINGBUFFER_H

#include <atomic>
#include <cassert>

// ═══════════════════════════════════════════════════════════════
// 1. Generischer SPSC-Ringbuffer (für UI-Messages, Deferred Work)
// ═══════════════════════════════════════════════════════════════

template<typename T, unsigned N>
class LockfreeSPSCQueue
{
    static_assert(N > 0 && (N & (N-1)) == 0, "N must be power of 2");

public:
    LockfreeSPSCQueue() = default;

    // Producer (nur ein Core) – returns false if full
    bool Push(const T& item)
    {
        unsigned h = m_head.load(std::memory_order_relaxed);
        unsigned next = (h + 1) & (N - 1);

        if (next == m_tail.load(std::memory_order_acquire))
            return false; // voll

        m_buffer[h] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (nur ein Core) – returns false if empty
    bool Pop(T& item)
    {
        unsigned t = m_tail.load(std::memory_order_relaxed);

        if (t == m_head.load(std::memory_order_acquire))
            return false; // leer

        item = m_buffer[t];
        m_tail.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    unsigned Count() const
    {
        unsigned h = m_head.load(std::memory_order_acquire);
        unsigned t = m_tail.load(std::memory_order_acquire);
        return (h - t) & (N - 1);
    }

    bool Empty() const
    {
        return m_head.load(std::memory_order_acquire) ==
               m_tail.load(std::memory_order_acquire);
    }

private:
    T                     m_buffer[N];
    std::atomic<unsigned> m_head{0};
    std::atomic<unsigned> m_tail{0};
};


// ═══════════════════════════════════════════════════════════════
// 2. Audio Double-Buffer (für Core 1 -> Core 0 DMA)
//
//    Jeder Slot hat drei Zustände:
//      Empty      = 0  – Slot ist frei, darf von Producer beschrieben werden
//      Ready      = 1  – Slot enthält fertige Audio-Daten
//      Consuming  = 2  – Slot wird gerade vom DMA-IRQ gelesen
//
//    Producer (Core 1): sucht Empty-Slot, füllt ihn, markiert Ready.
//    Consumer (Core 0/DMA): sucht Ready-Slot, markiert Consuming,
//                             kopiert in Hardware-Buffer, markiert Empty.
//
//    Wenn kein Ready-Slot verfügbar: Consumer gibt den letzten Block
//    erneut aus (Repeat-Last) oder Stille.
// ═══════════════════════════════════════════════════════════════

enum class AudioSlotStatus : unsigned
{
    Empty     = 0,
    Ready     = 1,
    Consuming = 2,
    Filling   = 3
};

template<unsigned NSlots, unsigned MaxFrames>
struct AudioBufferSlot
{
    float outL[MaxFrames];
    float outR[MaxFrames];
    unsigned nFrames = 0;
    std::atomic<unsigned> status{static_cast<unsigned>(AudioSlotStatus::Empty)};
};

template<unsigned NSlots, unsigned MaxFrames>
class LockfreeAudioBuffer
{
    static_assert(NSlots > 0 && (NSlots & (NSlots - 1)) == 0,
                  "NSlots must be power of 2");

public:
    LockfreeAudioBuffer() = default;

    // ── Producer (Core 1) ──────────────────────────────────────

    // Sucht einen Empty-Slot und markiert ihn als Filling.
    // Der Slot wird NICHT als Ready markiert — der Caller muss
    // CommitSlot() aufrufen, nachdem die Daten geschrieben wurden.
    // Gibt nullptr zurück, wenn alle Slots Ready/Consuming sind.
    //
    // WICHTIG: Wir überschreiben NIE einen Ready-Slot. Sonst läuft der
    // Producer (Core 1, in Mikrosekunden pro Chunk) im Steady-State
    // dauerhaft durch alle Slots, während der Consumer (DMA, 10.67 ms
    // pro Chunk) nur alle ~10 ms einen Slot leert. Die in den Slots
    // liegenden Audio-Phasen wären dann zwischen den Slots
    // diskontinuierlich → hörbares Stottern/Knacken.
    AudioBufferSlot<NSlots, MaxFrames>* AcquireEmptySlot()
    {
        unsigned idx = m_writeIdx.load(std::memory_order_relaxed);

        for (unsigned i = 0; i < NSlots; ++i)
        {
            unsigned slotIdx = (idx + i) & (NSlots - 1);
            unsigned expected = static_cast<unsigned>(AudioSlotStatus::Empty);

            if (m_slots[slotIdx].status.compare_exchange_strong(
                    expected,
                    static_cast<unsigned>(AudioSlotStatus::Filling),
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                m_writeIdx.store((slotIdx + 1) & (NSlots - 1),
                                  std::memory_order_relaxed);
                return &m_slots[slotIdx];
            }
        }

        // Alle Slots Ready/Consuming → Producer ist schneller als
        // Consumer. Caller (FillAudioSlot) wartet kurz und versucht
        // es erneut. KEIN Overwrite mehr.
        return nullptr;
    }

    // Markiert einen Filling-Slot als Ready (nach Daten geschrieben wurden).
    // memory_order_release stellt sicher, dass alle vorherigen Writes
    // (slot->outL/outR, nFrames) sichtbar sind, bevor der Status Ready wird.
    void CommitSlot(AudioBufferSlot<NSlots, MaxFrames>* slot)
    {
        if (slot)
        {
            slot->status.store(static_cast<unsigned>(AudioSlotStatus::Ready),
                               std::memory_order_release);
        }
    }

    // ── Consumer (Core 0 / DMA-IRQ) ──────────────────────────

    // Sucht einen Ready-Slot und markiert ihn als Consuming.
    // Gibt nullptr zurück, wenn kein Ready-Slot verfügbar.
    AudioBufferSlot<NSlots, MaxFrames>* AcquireReadySlot()
    {
        unsigned idx = m_readIdx.load(std::memory_order_relaxed);

        for (unsigned i = 0; i < NSlots; ++i)
        {
            unsigned slotIdx = (idx + i) & (NSlots - 1);
            unsigned expected = static_cast<unsigned>(AudioSlotStatus::Ready);

            if (m_slots[slotIdx].status.compare_exchange_strong(
                    expected,
                    static_cast<unsigned>(AudioSlotStatus::Consuming),
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                m_readIdx.store((slotIdx + 1) & (NSlots - 1),
                                std::memory_order_relaxed);
                return &m_slots[slotIdx];
            }
        }

        return nullptr; // Kein Ready-Slot
    }

    // Gibt einen Consuming-Slot zurück (markiert als Empty).
    void ReleaseSlot(AudioBufferSlot<NSlots, MaxFrames>* slot)
    {
        if (slot)
        {
            slot->status.store(static_cast<unsigned>(AudioSlotStatus::Empty),
                               std::memory_order_release);
        }
    }

    // Debug: Anzahl Ready-Slots
    unsigned ReadyCount() const
    {
        unsigned count = 0;
        for (unsigned i = 0; i < NSlots; ++i)
        {
            if (m_slots[i].status.load(std::memory_order_acquire) ==
                static_cast<unsigned>(AudioSlotStatus::Ready))
            {
                ++count;
            }
        }
        return count;
    }

private:
    AudioBufferSlot<NSlots, MaxFrames> m_slots[NSlots];
    std::atomic<unsigned>              m_writeIdx{0};
    std::atomic<unsigned>              m_readIdx{0};
};


// ═══════════════════════════════════════════════════════════════
// 3. Einfacher atomarer Flag-Wrapper (für Cross-Core-Signale)
// ═══════════════════════════════════════════════════════════════

class AtomicSignal
{
public:
    void Set()   { m_flag.store(true,  std::memory_order_release); }
    void Clear() { m_flag.store(false, std::memory_order_release); }
    bool TestAndClear()
    {
        return m_flag.exchange(false, std::memory_order_acq_rel);
    }
    bool Test() const
    {
        return m_flag.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> m_flag{false};
};

#endif // _VELVETKEYS_RINGBUFFER_H
