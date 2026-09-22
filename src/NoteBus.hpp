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


/** How many voice cables can be sourced at once. An mpxIn claims one per output, so
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

	/** WHAT THIS NOTE IS TO ITS PHRASE, from a rhythm source that knows. A melody cannot tell
	which note is the last of a phrase — it sees notes one at a time, and the phrase's breath
	comes after its last note, so a countdown to the phrase's end misses the note that matters.
	The source that placed the notes knows, and says so here.

	ARRIVAL: the last note of the phrase, which a melody lands on the tonic at a full close and on
	an open degree at a half cadence. GROUP_END: the last note before a breath inside the phrase,
	which a melody takes on a tone of the chord sounding. APPROACH: the note before the arrival,
	which a melody takes on a neighbour of the note it is about to land on, so the ending is
	arrived at rather than jumped to. A source that sets none of these — a Euclidean
	rhythm, a keyboard — leaves the melody to its countdown.

	PICKUP: a note leading into the NEXT phrase, sounding in the breath before it begins. It
	belongs to the phrase it leads into and not to the one it sounds in, so a melody neither pulls
	it toward the ending phrase's arrival nor draws it as part of that phrase.

	ON_CHANGE: the note falls where the chord changes, which a melody arrives at by step. */
	enum Flag : uint8_t { ARRIVAL = 1, GROUP_END = 2, APPROACH = 4, PICKUP = 8, ON_CHANGE = 16 };
	uint8_t flags = 0;

	/** MOTIF: this note restates the rhythm of the note sent this many notes before it, as part
	of a figure coming back; nought when it restates nothing. A melody can then bring back that
	note's pitch as well, and the figure returns whole. See applyMotif in Phrasing.cpp. */
	uint8_t echo = 0;
	/** HOW FAR THROUGH ITS BREATH GROUP THE NOTE FALLS, nought at the group's first note and one
	at its last; below nought from a source that does not know. A melody shapes its line by it:
	see CONTOUR on mpxVoice. */
	float along = -1.f;
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

	/** WHERE THE PHRASE ENDS, AND WHICH PHRASE IT IS.

	A PHRASE IS NOT A BAR AND NOT THE FORM. It is the unit a singer breathes between and a
	writer shapes toward — four bars, or eight, ending where the harmony arrives somewhere. A
	melody that does not know where its phrases end cannot breathe at their ends, cannot slur
	within one, and cannot lean toward a cadence it cannot see coming.

	ONLY THE CHART CAN WORK THIS OUT. The three chords of lookahead here are enough to lead into
	a change and nowhere near enough to know that the change is two bars from the end of an
	eight-bar phrase. Sections, cadences and the four-and-eight-bar shape are properties of the
	whole progression, which only the module holding it can see. So it is computed once where
	the chart is read and carried here like everything else.

	`beatsToPhraseEnd` counts down to the end of the current phrase; `phraseBeats` is how long
	that phrase is, so a module can tell how far through it is; `phrase` numbers the phrases
	within one pass of the form, so one can be told from the next. Nought and nought mean
	nothing is known — an unphrased chart, or a module that has not been told. */
	float beatsToPhraseEnd = 0.f;
	float phraseBeats = 0.f;
	uint16_t phrase = 0;
	/** HOW THE CURRENT PHRASE ENDS, as a ChartCadence. A full close, a half cadence expecting an
	answer, or none — a section end or a stretch with no cadence in it. What a line does at the
	end of a phrase depends on which. */
	uint8_t phraseCadence = 0;

	/** HOW MANY PHRASES ONE PASS OF THE FORM HOLDS. With the pass counter and the phrase number,
	this counts phrases from the top of the form across passes, which a cycle measured in phrases
	depends on. */
	uint16_t phrasesPerPass = 0;

	/** WHERE THE PHRASE SITS IN THE FORM: the section letter, or nought where the chart names
	none; which time that section has begun in this pass, from one; and which phrase of the
	section this is, from nought. A returning section can reuse what was made for its first
	appearance by matching the last two. */
	char section = 0;
	uint8_t sectionAppearance = 0;
	uint8_t phraseInSection = 0;

	/** EVERY CHORD CHANGE INSIDE THE CURRENT PHRASE, in beats from its start, so a rhythm can be
	decided for the whole phrase when it begins. At most MAX_PHRASE_CHANGES; a phrase with more is
	rare, and `phraseChangesAll` says how many there really were so the loss is visible. */
	static constexpr int MAX_PHRASE_CHANGES = 16;
	uint8_t phraseChangeCount = 0;
	uint8_t phraseChangesAll = 0;
	float phraseChanges[MAX_PHRASE_CHANGES] = {};

	/** THE PHRASE AFTER THIS ONE, as the fields above describe the current one.

	WHY THE FUTURE IS NEEDED. Most sung phrases do not begin on their first bar line: in thirty
	pop songs only one in six did, and more than a quarter began with a pickup in the bar before.
	A pickup sounds while the previous phrase is still the current one, so a rhythm generator has
	to decide the next phrase before it begins — which it can only do if it knows how long that
	phrase is, how it ends and where its chords change.

	`epoch` is the pass the next phrase falls in: one more than this pass's when the current phrase
	is the last of the form, since the chart loops. `valid` is false where nothing is known. */
	struct Upcoming {
		bool valid = false;
		uint16_t phrase = 0;
		uint32_t epoch = 0;
		float beats = 0.f;
		uint8_t cadence = 0;
		char section = 0;
		uint8_t sectionAppearance = 0;
		uint8_t phraseInSection = 0;
		uint8_t changeCount = 0;
		float changes[MAX_PHRASE_CHANGES] = {};
	};
	Upcoming upcoming;

	/** HOW HARD THE MUSIC SWINGS, and by how much a note off the beat is therefore moved.

	ON THE CHART BECAUSE EVERYTHING HAS TO AGREE. Swing is not a rhythm and does not belong to a
	rhythm module: it is how the beat is divided, which is the chart's business already — as the
	beat, the bar, the metre and the phrase boundaries are. Two rhythm modules are peers with no
	cable between them, so a swing control on each would let them disagree with nothing to say
	why, and a swung line over a straight drum part is always wrong.

	THE CHART DOES THE CONVERTING. `swing` is the amount, nought to one, as set on the panel; the
	two ratios are what that amount means at the tempo the chart is running, taken from the curve
	measured in 456 jazz solos — see Swing.hpp. A module looks up the ratio for whichever division
	it is working at and knows nothing of tempo curves, so two modules cannot convert the same
	amount differently.

	A ratio is the length of the first half of a divided beat against the second: one is even, two
	is a full triplet feel. */
	float swing = 0.f;
	float swingEighth = 1.f;
	float swingSixteenth = 1.f;

	/** THE NUMBER EVERY RANDOM PROCESS DOWNSTREAM STARTS FROM.

	A patch full of scatter and chance is unrepeatable unless everything in it agrees where its
	randomness came from. This is that agreement: one number, set on the chart, carried by the
	same cable as the beat, so a module has to be told nothing and asked nothing. */
	uint32_t seed = 0;

	/** HOW MANY TIMES THE MUSIC HAS GONE BACK TO THE TOP.

	Counted rather than signalled, because this is state and not an event: a module that starts
	listening halfway through a chorus can still tell which time round it is, where a pulse it
	was not there to hear would have told it nothing. It changes on a rewind and on the chart
	looping, and a module that wants each pass to differ folds it into the seed. */
	uint32_t epoch = 0;

	/** RECORD, PRESSED ON THE CHART: every module downstream that keeps a log starts and stops
	its log with it, so one press records a whole take — the rhythm, the pitches — as one. Each
	module follows a change of it and can still be switched on its own. */
	bool record = false;

	/** THE STYLE CHOSEN ON THE CHART, as a PhraseStyle: nought for none. When it changes, every
	module downstream that has a part of a style — mpxPhrase its rhythm, each melody voice its line
	— sets its own knobs to that style, so one choice sets the whole chain. Only a change is
	followed: the knobs stay yours afterwards, and a patch opening is not a change. */
	uint8_t style = 0;

	/** THE CHART IS NOT MOVING: stopped, or started and waiting for its clock's next pulse to
	begin on. Nothing is to be played on the strength of the beat it shows — it shows the beat it
	will start from, and a note there belongs to the moment it starts, not to the moment it was
	rewound or PLAY was pressed. */
	bool holding = false;
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

	/** THE PEDALS, as state rather than events, for the reason the harmony is: a module that
	starts listening while the pedal is already down must know that at once, not when the pedal
	next moves. Nought is up and one fully down; between is half-pedalling. Two independent words,
	so no seqlock — a reader that sees one updated before the other sees a pedal half a sample
	early, which nothing can hear. */
	std::atomic<float> sustain{0.f};
	std::atomic<float> soft{0.f};
};

extern Bus gBuses[MAX_BUSES];

/** Takes a free bus, or -1 if all are in use. Main thread, at module construction. */
int busClaim(uint32_t* generation);
/** Gives one back. Main thread, at module destruction. */
void busRelease(int slot);

/** Appends an event. Audio thread, one writer per bus. */
void busPush(int slot, const Event& e);

/** Publishes the pedals on this bus. Audio thread, one writer per bus. A module that reads an
upstream and publishes its own bus forwards them, exactly as it forwards the harmony: a processor
that dropped them would leave everything after it unpedalled. */
void busPublishPedals(int slot, float sustain, float soft);

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
	/** The pedals, the most pressed of all the upstreams: two sources merged into one input are
	one player's hands, and either one holding the pedal holds it. Nought and nought with
	nothing attached. */
	void pedals(float& sustain, float& soft) const;
};

/** A unique name for a note. */
int64_t mintHandle();

/** ANYTHING THAT PUTS NOTES ON A CABLE. Implemented by mpxIn and by every native source after
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
