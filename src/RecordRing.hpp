#pragma once
/** A QUEUE FROM THE AUDIO THREAD TO THE PANEL, for anything a module writes to disk as it plays.

One writer, one reader, fixed size, no locks and no allocation: the audio thread may never wait
for a file. When the panel falls behind, what does not fit is counted and dropped rather than
blocking the engine, and the count is written into the log so a gap in it is visible rather than
silent.
*/
#include <atomic>
#include <cstdint>

namespace px {

template <typename T, uint32_t SIZE = 512>
struct RecordRing {
	T ring[SIZE];
	std::atomic<uint32_t> head{0}, tail{0};
	std::atomic<uint32_t> dropped{0};

	void push(const T& r) {
		const uint32_t h = head.load(std::memory_order_relaxed);
		const uint32_t next = (h + 1) % SIZE;
		if (next == tail.load(std::memory_order_acquire)) {
			dropped++;
			return;
		}
		ring[h] = r;
		head.store(next, std::memory_order_release);
	}

	bool pop(T& out) {
		const uint32_t t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_acquire))
			return false;
		out = ring[t];
		tail.store((t + 1) % SIZE, std::memory_order_release);
		return true;
	}
};

} // namespace px
