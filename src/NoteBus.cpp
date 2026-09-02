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


void busPublishHarmony(int slot, const Harmony& h) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	// Odd while writing, even when settled, so a reader can tell it caught the middle of one.
	const uint32_t s = bus.hseq.load(std::memory_order_relaxed);
	bus.hseq.store(s + 1, std::memory_order_release);
	std::atomic_thread_fence(std::memory_order_release);
	bus.harmony = h;
	std::atomic_thread_fence(std::memory_order_release);
	bus.hseq.store(s + 2, std::memory_order_release);
}


bool busReadHarmony(int slot, Harmony& out) {
	if (slot < 0 || slot >= MAX_BUSES)
		return false;
	Bus& bus = gBuses[slot];
	if (!bus.claimed.load(std::memory_order_acquire))
		return false;
	// Bounded, because this runs on the audio thread and a retry loop with no end is a stall
	// rather than a correction. Three attempts is far more than a writer needs.
	for (int attempt = 0; attempt < 3; attempt++) {
		const uint32_t a = bus.hseq.load(std::memory_order_acquire);
		if (a & 1u)
			continue;
		std::atomic_thread_fence(std::memory_order_acquire);
		Harmony h = bus.harmony;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (bus.hseq.load(std::memory_order_acquire) == a) {
			out = h;
			return h.valid;
		}
	}
	return false;
}


void BusReader::add(int slot, uint32_t generation) {
	if (slot < 0 || slot >= MAX_BUSES || count >= MAX_UPSTREAM)
		return;
	Link& link = links[count++];
	link.slot = slot;
	link.generation = generation;
	link.read = gBuses[slot].write.load(std::memory_order_acquire);
}


void BusReader::attach(int slot, uint32_t generation) {
	clear();
	if (slot >= 0)
		add(slot, generation);
}


bool BusReader::sameAs(const int* slots, const uint32_t* generations, int n) const {
	if (n != count)
		return false;
	for (int i = 0; i < n; i++) {
		if (links[i].slot != slots[i] || links[i].generation != generations[i])
			return false;
	}
	return true;
}


bool BusReader::harmony(Harmony& out) const {
	// The first upstream that has one. Two charts feeding one chain is a conflict, and taking
	// the first is a defined answer rather than an arbitrary one.
	for (int i = 0; i < count; i++) {
		if (busReadHarmony(links[i].slot, out))
			return true;
	}
	return false;
}


bool BusReader::next(Event& e) {
	// INTERLEAVED, not concatenated: every upstream is offered in turn so one busy source
	// cannot starve another. Within a sample the order between them means nothing anyway.
	for (int i = 0; i < count; i++) {
		Link& link = links[i];
		if (link.slot < 0 || link.slot >= MAX_BUSES)
			continue;
		Bus& bus = gBuses[link.slot];
		// The source has gone, or the slot has been taken by a different module since this
		// reader attached. Either way there is nothing here that belongs to us.
		if (!bus.claimed.load(std::memory_order_acquire))
			continue;
		if (bus.generation.load(std::memory_order_acquire) != link.generation)
			continue;

		uint32_t w = bus.write.load(std::memory_order_acquire);
		if (link.read == w)
			continue;
		// Lapped: move up to the oldest event still held rather than reading overwritten
		// memory. This cannot happen while both ends run in the same callback; it is here so
		// that if it ever does, the result is dropped notes rather than nonsense.
		if (w - link.read > (uint32_t) BUS_RING)
			link.read = w - BUS_RING;

		e = bus.ring[link.read % BUS_RING];
		link.read++;
		return true;
	}
	return false;
}


} // namespace px
