#include "NoteBus.hpp"

#include <cmath>

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
	// The pedals come up with the module that held them, or the next occupant of this slot would
	// start with somebody else's foot down.
	gBuses[slot].sustain.store(0.f, std::memory_order_relaxed);
	gBuses[slot].soft.store(0.f, std::memory_order_relaxed);
	gBuses[slot].pedalDecided.store(false, std::memory_order_relaxed);
	gBuses[slot].claimed.store(false, std::memory_order_release);
}


void busPublishPedals(int slot, float sustain, float soft, bool decided) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	gBuses[slot].sustain.store(sustain, std::memory_order_relaxed);
	gBuses[slot].soft.store(soft, std::memory_order_relaxed);
	gBuses[slot].pedalDecided.store(decided, std::memory_order_relaxed);
}


void BusReader::pedals(float& sustain, float& soft, bool* decided) const {
	sustain = 0.f;
	soft = 0.f;
	if (decided)
		*decided = false;
	for (int i = 0; i < count; i++) {
		const Link& link = links[i];
		if (link.slot < 0 || link.slot >= MAX_BUSES)
			continue;
		const Bus& bus = gBuses[link.slot];
		if (!bus.claimed.load(std::memory_order_acquire))
			continue;
		if (bus.generation.load(std::memory_order_acquire) != link.generation)
			continue;
		sustain = std::fmax(sustain, bus.sustain.load(std::memory_order_relaxed));
		soft = std::fmax(soft, bus.soft.load(std::memory_order_relaxed));
		if (decided && bus.pedalDecided.load(std::memory_order_relaxed))
			*decided = true;
	}
}


void busPush(int slot, const Event& e) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	uint32_t w = bus.write.load(std::memory_order_relaxed);
	bus.ring[w % BUS_RING] = e;
	// Released after the event is written, so a reader that sees the new index sees the event.
	bus.write.store(w + 1, std::memory_order_release);
	if (e.kind == Event::ON)
		bus.lastOn.store(APP->engine->getFrame(), std::memory_order_relaxed);
}


void busAnnounce(int slot, const Coming& c) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	const uint32_t w = bus.comingWrite.load(std::memory_order_relaxed);
	bus.coming[w % COMING_RING] = c;
	bus.comingWrite.store(w + 1, std::memory_order_release);
}


void busWithdraw(int slot) {
	Coming c;
	c.kind = Coming::WITHDRAW;
	busAnnounce(slot, c);
}


float busFlashVolts(int slot) {
	if (slot < 0 || slot >= MAX_BUSES)
		return 0.f;
	const int64_t now = APP->engine->getFrame();
	const float rate = APP->engine->getSampleRate();
	// A READER THAT CANNOT PLAY THE PART, said within the last half second: blinking, a quarter of a
	// second on and a quarter off, over any note's pulse.
	const int64_t said = now - gBuses[slot].cannotPlayAt.load(std::memory_order_relaxed);
	if (said >= 0 && said < (int64_t) (0.5f * rate))
		return ((now / (int64_t) (0.25f * rate)) % 2 == 0) ? 10.f : 0.f;
	const int64_t since = now - gBuses[slot].lastOn.load(std::memory_order_relaxed);
	const int64_t length = (int64_t) (rate * 0.005f);
	return (since >= 0 && since < length) ? 10.f : 0.f;
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


void busPublishVoicing(int slot, const ChordVoicing& v) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	const uint32_t s = bus.vseq.load(std::memory_order_relaxed);
	bus.vseq.store(s + 1, std::memory_order_release);
	std::atomic_thread_fence(std::memory_order_release);
	bus.voicing = v;
	std::atomic_thread_fence(std::memory_order_release);
	bus.vseq.store(s + 2, std::memory_order_release);
}


void busPublishInstrument(int slot, const Instrument& p) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	const uint32_t s = bus.pseq.load(std::memory_order_relaxed);
	bus.pseq.store(s + 1, std::memory_order_release);
	std::atomic_thread_fence(std::memory_order_release);
	bus.instrument = p;
	std::atomic_thread_fence(std::memory_order_release);
	bus.pseq.store(s + 2, std::memory_order_release);
}


bool busReadInstrument(int slot, Instrument& out) {
	if (slot < 0 || slot >= MAX_BUSES)
		return false;
	Bus& bus = gBuses[slot];
	if (!bus.claimed.load(std::memory_order_acquire))
		return false;
	for (int attempt = 0; attempt < 3; attempt++) {
		const uint32_t a = bus.pseq.load(std::memory_order_acquire);
		if (a & 1u)
			continue;
		std::atomic_thread_fence(std::memory_order_acquire);
		Instrument p = bus.instrument;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (bus.pseq.load(std::memory_order_acquire) == a) {
			out = p;
			return p.valid;
		}
	}
	return false;
}


void busPublishMotion(int slot, const PartMotion& v) {
	if (slot < 0 || slot >= MAX_BUSES)
		return;
	Bus& bus = gBuses[slot];
	const uint32_t s = bus.sseq.load(std::memory_order_relaxed);
	bus.sseq.store(s + 1, std::memory_order_release);
	std::atomic_thread_fence(std::memory_order_release);
	bus.motion = v;
	std::atomic_thread_fence(std::memory_order_release);
	bus.sseq.store(s + 2, std::memory_order_release);
}


bool busReadMotion(int slot, PartMotion& out) {
	if (slot < 0 || slot >= MAX_BUSES)
		return false;
	Bus& bus = gBuses[slot];
	if (!bus.claimed.load(std::memory_order_acquire))
		return false;
	for (int attempt = 0; attempt < 3; attempt++) {
		const uint32_t a = bus.sseq.load(std::memory_order_acquire);
		if (a & 1u)
			continue;
		std::atomic_thread_fence(std::memory_order_acquire);
		PartMotion v = bus.motion;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (bus.sseq.load(std::memory_order_acquire) == a) {
			out = v;
			return v.valid;
		}
	}
	return false;
}


bool busReadVoicing(int slot, ChordVoicing& out) {
	if (slot < 0 || slot >= MAX_BUSES)
		return false;
	Bus& bus = gBuses[slot];
	if (!bus.claimed.load(std::memory_order_acquire))
		return false;
	for (int attempt = 0; attempt < 3; attempt++) {
		const uint32_t a = bus.vseq.load(std::memory_order_acquire);
		if (a & 1u)
			continue;
		std::atomic_thread_fence(std::memory_order_acquire);
		ChordVoicing v = bus.voicing;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (bus.vseq.load(std::memory_order_acquire) == a) {
			out = v;
			return v.valid;
		}
	}
	return false;
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
	link.comingRead = gBuses[slot].comingWrite.load(std::memory_order_acquire);
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


bool BusReader::beat(Harmony& out) const {
	for (int i = 0; i < count; i++) {
		Harmony h;
		// busReadHarmony fills `h` whenever it read one, and answers for the chords alone.
		const bool chords = busReadHarmony(links[i].slot, h);
		if (chords || h.timingValid) {
			out = h;
			return true;
		}
	}
	return false;
}


bool BusReader::published(Harmony& out) const {
	for (int i = 0; i < count; i++) {
		Harmony h;
		const bool chords = busReadHarmony(links[i].slot, h);
		if (chords || h.timingValid || h.keyValid || h.pitchesValid) {
			out = h;
			return true;
		}
	}
	return false;
}


bool BusReader::voicing(ChordVoicing& out) const {
	for (int i = 0; i < count; i++) {
		if (busReadVoicing(links[i].slot, out))
			return true;
	}
	return false;
}


bool BusReader::motion(PartMotion& out) const {
	for (int i = 0; i < count; i++) {
		if (busReadMotion(links[i].slot, out))
			return true;
	}
	return false;
}


void BusReader::cannotPlay() const {
	const int64_t now = APP->engine->getFrame();
	for (int i = 0; i < count; i++) {
		const int slot = links[i].slot;
		if (slot < 0 || slot >= MAX_BUSES)
			continue;
		Bus& bus = gBuses[slot];
		// Only to the source this reader attached to, as everything it reads.
		if (!bus.claimed.load(std::memory_order_acquire)
				|| bus.generation.load(std::memory_order_acquire) != links[i].generation)
			continue;
		bus.cannotPlayAt.store(now, std::memory_order_relaxed);
	}
}


bool BusReader::instrument(Instrument& out) const {
	for (int i = 0; i < count; i++) {
		if (busReadInstrument(links[i].slot, out))
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


bool BusReader::nextComing(Coming& c) {
	for (int i = 0; i < count; i++) {
		Link& link = links[i];
		if (link.slot < 0 || link.slot >= MAX_BUSES)
			continue;
		Bus& bus = gBuses[link.slot];
		if (!bus.claimed.load(std::memory_order_acquire))
			continue;
		if (bus.generation.load(std::memory_order_acquire) != link.generation)
			continue;
		const uint32_t w = bus.comingWrite.load(std::memory_order_acquire);
		if (link.comingRead == w)
			continue;
		if (w - link.comingRead > (uint32_t) COMING_RING)
			link.comingRead = w - COMING_RING;
		c = bus.coming[link.comingRead % COMING_RING];
		link.comingRead++;
		return true;
	}
	return false;
}


} // namespace px
