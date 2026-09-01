#pragma once
#include <rack.hpp>
#include <atomic>
#include <cstdint>

/** The note transport — see docs/design.md.

A note cable is a real Rack cable. Rack owns it, draws it, saves it with the patch, undoes it
and removes it when either module goes. What it does NOT do is carry the notes: Rack cables
carry a float per channel per sample, and a note is an event with a name, so the events travel
through this table instead and the cable is what says who is joined to whom.

WHY NOT VOLTAGES. Nine lanes on a sixteen-channel polyphonic cable would carry one note. The
whole point of the bundle is that one cable is one instrument, not one note, and an instrument
plays several at once. Events have no such ceiling.

WHY NOT AN EXPANDER. Rack's message-passing between modules reaches the module physically next
door and no further. A voice belongs among the modules that implement it, wherever in the rack
that is.

SLOTS RATHER THAN POINTERS. A voice holding a pointer to its source would be holding a dangling
one for the frame between the source's deletion and the next scan. The buses are static, so a
voice reads a slot that always exists and finds it unclaimed. The generation number closes the
remaining gap: a slot freed and immediately reclaimed by a different module does not silently
inherit the old link.

ONE PRODUCER, SEVERAL CONSUMERS. The source writes; every voice patched to it reads, each with
its own cursor, so one Note feeds as many Voices as you care to patch. A consumer that has been
lapped is moved up rather than replaying: it is a live signal, not a recording.
*/

namespace px {


/** How many voice cables can be sourced at once. An toMPX claims one per output, so
this is sixteen of them. */
static const int MAX_BUSES = 64;
/** Events a bus holds before the oldest are overwritten. Producer and consumer both run in the
same audio callback and the consumer drains completely every sample, so it cannot fall behind
by more than one sample's worth of events. This is headroom, not a buffer. */
static const int BUS_RING = 256;

/** The continuing values, which arrive as updates naming the note they belong to. */
enum Lane {
	LANE_BEND,
	LANE_PRESSURE,
	LANE_TIMBRE,
	NUM_LANES,
};

struct Event {
	enum Kind : uint8_t { ON, OFF, UPDATE };
	uint8_t kind = ON;
	uint8_t lane = 0;
	/** Names one sounding note, so a later message can reach it. Unique for the session. */
	int64_t handle = 0;
	/** Set at note-on and unchanging for the note's life. */
	float pitch = 0.f;
	float level = 0.f;
	float duration = 0.f;
	float pan = 0.f;
	/** How many semitones full bend deflection is worth, so the far end can produce the
	control voltage without knowing the source's knob. */
	float bendRange = 2.f;
	/** UPDATE only. */
	float value = 0.f;
};

struct Bus {
	std::atomic<bool> claimed{false};
	std::atomic<uint32_t> generation{0};
	std::atomic<uint32_t> write{0};
	Event ring[BUS_RING];
};

extern Bus gBuses[MAX_BUSES];

/** Takes a free bus, or -1 if all are in use. Main thread, at module construction. */
int busClaim(uint32_t* generation);
/** Gives one back. Main thread, at module destruction. */
void busRelease(int slot);

/** Appends an event. Audio thread, one writer per bus. */
void busPush(int slot, const Event& e);

/** A voice's cursor into a bus. */
struct BusReader {
	int slot = -1;
	uint32_t generation = 0;
	uint32_t read = 0;

	/** Points at a bus and starts at its present, so connecting a cable does not replay
	every note the source has ever sent. */
	void attach(int newSlot, uint32_t newGeneration);
	void detach() { slot = -1; }
	bool attached() const { return slot >= 0; }
	/** Takes the next event, if there is one. Audio thread. */
	bool next(Event& e);
};

/** A unique name for a note. */
int64_t mintHandle();


} // namespace px
