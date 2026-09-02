#pragma once
#include <rack.hpp>
#include "Chord.hpp"
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

/** THE HARMONY THE NOTES ARE PLAYED AGAINST, carried by the same cable.

STATE, NOT EVENTS. A stream of chord changes would leave a module that starts listening between
two of them knowing nothing until the next one. Rack's own model is the guide: a cable carries a
value readable at any sample, not a stream that must not be missed. So this is a block anyone
can read whenever they like, and forwarding it costs a copy.

AND IT DESCRIBES THE FUTURE. Notes arrive as they happen; this says what is coming. That is why
a harmony processor can insert a chord before a dominant it can already see, where every note
processor has to work around having no lookahead. */
struct Harmony {
	bool valid = false;
	Key key;
	Chord current, next, after;
	/** Beats until the current chord gives way. */
	float beatsToNext = 0.f;
	/** Where we are, in beats from the start of the cycle, and how long the cycle is. */
	double beat = 0.0;
	float cycleBeats = 0.f;
	/** The time signature, so a module can work in bars without being told what one is. */
	uint8_t barBeats = 4;
	uint8_t barUnit = 4;
	int bar = 0;
	float beatInBar = 0.f;
};

struct Bus {
	std::atomic<bool> claimed{false};
	std::atomic<uint32_t> generation{0};
	std::atomic<uint32_t> write{0};
	Event ring[BUS_RING];

	/** A seqlock, because the harmony is larger than a word and Rack may run several engine
	threads: the writer raises the count before and after, and a reader that sees it change
	underneath reads again. Written a few times a second and read every sample, which is
	exactly the traffic a seqlock suits. */
	std::atomic<uint32_t> hseq{0};
	Harmony harmony;
};

extern Bus gBuses[MAX_BUSES];

/** Takes a free bus, or -1 if all are in use. Main thread, at module construction. */
int busClaim(uint32_t* generation);
/** Gives one back. Main thread, at module destruction. */
void busRelease(int slot);

/** Appends an event. Audio thread, one writer per bus. */
void busPush(int slot, const Event& e);

/** Publishes the harmony on this bus. Audio thread, one writer per bus. */
void busPublishHarmony(int slot, const Harmony& h);
/** Reads it. False if the slot is empty or nothing has published one. */
bool busReadHarmony(int slot, Harmony& out);

/** How many cables one input can be fed by. Rack has allowed several into one input since
version 2.5, and interleaving their events is the note-domain equivalent of the voltage summing
it does for everything else — which is what makes parallel chains work. */
static const int MAX_UPSTREAM = 4;

/** A reader's cursors. One per cable feeding the input. */
struct BusReader {
	struct Link {
		int slot = -1;
		uint32_t generation = 0;
		uint32_t read = 0;
	};
	Link links[MAX_UPSTREAM];
	int count = 0;

	/** Rebuilt each time the patching changes. Starts at each bus's present, so connecting a
	cable does not replay every note the source has ever sent. */
	void clear() { count = 0; }
	void add(int slot, uint32_t generation);
	/** The single-cable case, which is most of them. */
	void attach(int slot, uint32_t generation);
	void detach() { count = 0; }
	bool attached() const { return count > 0; }
	/** Whether this is the same set of upstreams, so an unchanged frame does not reset the
	cursors and replay nothing. */
	bool sameAs(const int* slots, const uint32_t* generations, int n) const;

	/** Takes the next event from any upstream. Audio thread. */
	bool next(Event& e);
	/** The harmony from the first upstream that has one. */
	bool harmony(Harmony& out) const;
};

/** A unique name for a note. */
int64_t mintHandle();

/** ANYTHING THAT PUTS NOTES ON A CABLE. Implemented by toMPX and by every native source after
it, so the far end asks a question about a capability rather than about a class: a sequencer
that speaks MPX plugs straight into an unbundler with no adapter and nothing to whitelist.

This is what the design has always claimed — that the link is established by capability — and
until there was a second source it was only true on paper. */
struct NoteSource {
	virtual ~NoteSource() {}
	/** The bus this output writes to, or -1 for a port that is not an MPX one. */
	virtual int busSlotFor(int outputId, uint32_t* generation) = 0;
};


} // namespace px
