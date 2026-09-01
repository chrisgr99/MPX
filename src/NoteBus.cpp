#include "NoteBus.hpp"

namespace px {


Bus gBuses[MAX_BUSES];

static std::atomic<int64_t> gNextHandle{1};


int64_t mintHandle() {
	return gNextHandle.fetch_add(1, std::memory_order_relaxed);
}


int busClaim(uint32_t* generation) {
	for (int i = 0; i < MAX_BUSES; i++) {
		bool expected = false;
		if (gBuses[i].claimed.compare_exchange_strong(expected, true)) {
			// Bumped on the way IN rather than on the way out, so a reader that attached to
			// the previous occupant of this slot no longer matches.
			uint32_t g = gBuses[i].generation.fetch_add(1, std::memory_order_release) + 1;
			if (generation)
				*generation = g;
			return i;
		}
	}
	return -1;
}


void busRelease(int slot) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	gBuses[slot].claimed.store(false, std::memory_order_release);
}


void busPush(int slot, const Event& e) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	uint32_t w = bus.write.load(std::memory_order_relaxed);
	bus.ring[w % BUS_RING] = e;
	// Released after the event is written, so a reader that sees the new index sees the event.
	bus.write.store(w + 1, std::memory_order_release);
}


void BusReader::attach(int newSlot, uint32_t newGeneration) {
	slot = newSlot;
	generation = newGeneration;
	read = (newSlot >= 0) ? gBuses[newSlot].write.load(std::memory_order_acquire) : 0;
}


bool BusReader::next(Event& e) {
	if (slot < 0 || slot >= MAX_BUSES)
		return false;
	Bus& bus = gBuses[slot];
	// The source has gone, or the slot has been taken by a different module since this reader
	// attached. Either way there is nothing here that belongs to this voice.
	if (!bus.claimed.load(std::memory_order_acquire))
		return false;
	if (bus.generation.load(std::memory_order_acquire) != generation)
		return false;

	uint32_t w = bus.write.load(std::memory_order_acquire);
	if (read == w)
		return false;
	// Lapped: move up to the oldest event still held rather than reading overwritten memory.
	// This cannot happen while both ends run in the same callback; it is here so that if it
	// ever does, the result is dropped notes rather than nonsense.
	if (w - read > (uint32_t) BUS_RING)
		read = w - BUS_RING;

	e = bus.ring[read % BUS_RING];
	read++;
	return true;
}


} // namespace px
