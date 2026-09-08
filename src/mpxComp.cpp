/** mpxComp — chordal accompaniment.

WHAT IT IS FOR. A chord is a set of notes; it is not yet anything a player would play. Every
chord out of mpxChart is voiced upward from its own root, so consecutive chords move in parallel
and all four voices leap together — which is why the chart through a polyphonic oscillator sounds
like a chord machine rather than like hands. This module is the part that decides WHERE the notes
go: which of the chord's tones are worth playing, and where each voice puts its own.

THE RULE, and it is the whole trick: on a chord change the voices move as little as they can.
Common tones do not move at all, the others step, and the same progression through the same
oscillator stops lurching. It is the rule that fixed the chart's bass output, applied to several
voices rather than one — and, because several voices compete for the same tones, done as a search
for the cheapest whole voicing rather than one voice at a time.

ONE WAY IN, and it is an MPX cable rather than a polyphonic one. A polyphonic chord is a set of
pitch classes: it does not say which note is the third and which is the fifth, and every decision
worth making here needs to know. Three voices under a thirteenth chord means keeping the third,
the seventh and the thirteenth and dropping the root and the fifth, and there is no way to do
that from twelve anonymous semitones. The MPX cable carries the chord as a degree of the key and
a quality, so the tones arrive already knowing what they are — along with what is coming, which
is what the rhythm will need.

ONE WAY OUT, and it is an MPX cable too. A note on one carries its pitch, its level and its
duration as a single event, so the three parallel cables this had at first — pitch, gate and
level — were saying in triplicate what one event says once, and fromMPX is the one place where
any MPX cable becomes ordinary Rack signals.

AND THAT IS WHAT MAKES A TIE POSSIBLE, which is the whole point of the module. A voice that does
not move is a note that is simply never sent again: one event carrying on, the way a player's
finger stays down. Three cables could not say it — a gate cable has no way to mark one channel as
carrying on — so every voice was struck afresh at every chord change, which is exactly the fault
the voicing exists to avoid.

NO CLOCK EITHER. The module is dead without an MPX cable and an MPX cable already carries the
beat, the bar and the time signature, so a clock jack could only ever have been a second timebase
fighting the first.

THE RHYTHM IS OFF BY DEFAULT, and off is not an absence. With it off the chord is held and tied,
which is the module's other half and what a pad wants; turning it on trades the tie for a figure.
A step falls at a division of the beat, the pattern says who plays on it, the accent says how hard
from where the step sits in the bar, and the strum spreads the voices apart by a few milliseconds.
None of it needs a clock: the metre is on the cable. See comp.md.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "Voicing.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const int MAX_VOICES = 6;

/** WHAT AN UNACCENTED NOTE COMES OUT AT, and there is no knob for it.

A level that is the same for every note is a gain stage, and a gain stage belongs in the
amplifier the patch already has. What this output is FOR is variation — the accent, the
humanising, and the balance that puts the top voice above its own harmony — so it emits a
deviation from a reference rather than an absolute, and the reference is here.

Eight volts rather than ten leaves headroom above for an accent and room below for the rest. It
does mean an amplifier set for a ten-volt envelope is a little quiet until it is turned up; the
alternative was ten with accents cutting downward, which needs no convention but makes an accent
an absence rather than a stress. */
static const float LEVEL_NOMINAL = 8.f;

/** The voicings, in the order the knob walks through them. */
enum Spread {
	SPREAD_CLOSE,   /**< every voice in the smallest space that holds them */
	SPREAD_DROP2,   /**< the lowest voice an octave under the rest: the pianist's default */
	SPREAD_OPEN,    /**< every other voice an octave up, so the gaps are fifths and sixths */
	NUM_SPREADS,
};

static const char* SPREAD_NAMES[NUM_SPREADS] = {"Close", "Drop two", "Open"};

/** HOW MUCH OF THE CHORD TO PLAY, which is most of what separates one style of accompaniment
from another as far as the voicing is concerned. A country band's dominant is four notes and a
jazz pianist's is six, and both are playing the chord the chart wrote. */
enum Colour {
	COLOUR_TRIAD,       /**< root, third and fifth, and the sus tones that stand in for a third */
	COLOUR_SEVENTHS,    /**< the four-note chord: sevenths and sixths, no extensions */
	COLOUR_EXTENSIONS,  /**< the chord as written, ninths and thirteenths included */
	NUM_COLOURS,
};

static const char* COLOUR_NAMES[NUM_COLOURS] = {"Triad", "Sevenths", "Extensions"};

/** WHICH VOICES SOUND ON EACH STEP. A pattern is nothing more than that: the rhythm decides
when a step falls and the pattern decides who plays on it. */
static const char* PATTERN_NAMES[] = {"Block", "Broken up", "Broken down", "Alberti", "Waltz"};
static const int NUM_PATTERNS = (int) (sizeof(PATTERN_NAMES) / sizeof(PATTERN_NAMES[0]));

static const char* ACCENT_NAMES[] = {"Even", "Metric", "Downbeat", "Backbeat", "Offbeat", "Push"};
static const int NUM_ACCENTS = (int) (sizeof(ACCENT_NAMES) / sizeof(ACCENT_NAMES[0]));

/** HOW OFTEN A STEP FALLS, as a division of the beat. A pattern says who plays; this says when.
Without it a figure has no relation to the tempo at all, and Alberti at one speed is the only
Alberti there is. */
static const char* RATE_NAMES[] = {"1 a bar", "1 in 2 beats", "1 a beat", "2 a beat",
	"3 a beat", "4 a beat"};
static const int NUM_RATES = (int) (sizeof(RATE_NAMES) / sizeof(RATE_NAMES[0]));
/** How many beats one step lasts. A nought means a whole bar, whatever the metre says one is —
which cannot be a number here, because a chart may change metre while it plays. */
static const float RATE_BEATS[NUM_RATES] = {0.f, 2.f, 1.f, 0.5f, 1.f / 3.f, 0.25f};

/** The longest a strum takes to cross all its voices, in seconds. */
static const float STRUM_MAX = 0.09f;
/** How far an accent and the balance may lift a note, and humanising drop it, in volts. */
static const float ACCENT_LIFT = 2.f;
static const float BALANCE_LIFT = 1.5f;
static const float HUMAN_DROP = 2.f;


struct CompModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_VOICES,
		P_BASS,
		P_CENTRE,
		P_SPAN,
		P_SPREAD,
		P_COLOUR,
		P_LEAD,
		P_PATTERN,
		P_GATE,
		P_ACCENT,
		P_AMOUNT,
		P_HUMAN,
		// APPENDED, NEVER INSERTED. A patch stores a parameter by its POSITION in this list, so
		// putting a new one in the middle hands every value after it to the wrong control when
		// an old patch is opened — a saved Pattern arriving as the Rhythm switch, and so on. New
		// controls go on the end however untidy that reads.
		P_STRUM,
		P_BALANCE,
		P_RHYTHM,
		P_RATE,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		NUM_OUTPUTS
	};
	enum LightId {
		L_CHANGE,
		NUM_LIGHTS
	};

	CompModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		rhythmReset();

		// COUNTED FROM NOUGHT, so that the value is which position of the control is chosen
		// rather than the number itself. Every place that wants the count adds one.
		configSwitch(P_VOICES, 0.f, (float) (MAX_VOICES - 1), 3.f, "Voices",
			{"1", "2", "3", "4", "5", "6"});
		// SEMITONES FROM MIDDLE C, because that is how a musician says where a part sits, and
		// because the number then reads the same whatever the chart's key is.
		configParam(P_CENTRE, -24.f, 24.f, 0.f, "Register", " semitones from middle C");
		paramQuantities[P_CENTRE]->snapEnabled = true;
		configSwitch(P_SPAN, 0.f, 2.f, 1.f, "Span", {"1 octave", "2 octaves", "3 octaves"});
		// WHETHER ANYTHING ELSE IS PLAYING THE BOTTOM. It is not a matter of range: with a bass
		// on its own instrument the root is the tone this part can most afford to leave out, and
		// without one the root has to be here and has to be underneath. The chart's own bass
		// output is the ordinary case, so that is the default.
		configSwitch(P_BASS, 0.f, 1.f, 0.f, "Root",
			{"Rootless — a separate instrument has it",
			"Chord has the root — this part plays it, underneath"});
		configSwitch(P_SPREAD, 0.f, (float) (NUM_SPREADS - 1), 0.f, "Spread",
			{SPREAD_NAMES[0], SPREAD_NAMES[1], SPREAD_NAMES[2]});
		configSwitch(P_COLOUR, 0.f, (float) (NUM_COLOURS - 1), (float) COLOUR_EXTENSIONS,
			"Chord tones", {COLOUR_NAMES[0], COLOUR_NAMES[1], COLOUR_NAMES[2]});
		// NOUGHT IS WHAT A CHORD SOURCE ALREADY DOES: rebuild every chord from its root. One is
		// a voice taking the nearest tone of the new chord. Between them the movement is
		// allowed but limited, which is a musical control rather than a switch.
		configParam(P_LEAD, 0.f, 1.f, 1.f, "Voice leading", "%", 0.f, 100.f);

		// OFF IS THE DEFAULT AND IT IS NOT AN ABSENCE. With the rhythm off the chord is held and
		// tied — a voice that does not move is never re-struck — which is the module's other
		// half and what somebody feeding a pad wants. Turning the rhythm on trades the tie for
		// a figure, and that is a musical choice rather than a completeness.
		configSwitch(P_RHYTHM, 0.f, 1.f, 0.f, "Rhythm", {"Off — one held chord", "On"});
		configSwitch(P_RATE, 0.f, (float) (NUM_RATES - 1), 2.f, "Rate",
			{RATE_NAMES[0], RATE_NAMES[1], RATE_NAMES[2], RATE_NAMES[3], RATE_NAMES[4],
			RATE_NAMES[5]});
		configSwitch(P_PATTERN, 0.f, (float) (NUM_PATTERNS - 1), 0.f, "Pattern",
			{PATTERN_NAMES[0], PATTERN_NAMES[1], PATTERN_NAMES[2], PATTERN_NAMES[3],
			PATTERN_NAMES[4]});
		configParam(P_GATE, 0.05f, 1.f, 0.9f, "Gate length", "%", 0.f, 100.f);
		configSwitch(P_ACCENT, 0.f, (float) (NUM_ACCENTS - 1), 1.f, "Accent",
			{ACCENT_NAMES[0], ACCENT_NAMES[1], ACCENT_NAMES[2], ACCENT_NAMES[3],
			ACCENT_NAMES[4], ACCENT_NAMES[5]});
		configParam(P_AMOUNT, 0.f, 1.f, 0.3f, "Accent amount", "%", 0.f, 100.f);
		configParam(P_HUMAN, 0.f, 1.f, 0.f, "Humanise", "%", 0.f, 100.f);
		// A TIME OFFSET RATHER THAN A PATTERN, so it combines with whatever figure is playing —
		// a rolled waltz is a thing, and it could not be if strum were an entry in the list.
		configParam(P_STRUM, 0.f, 1.f, 0.f, "Strum", " ms", 0.f, STRUM_MAX * 1000.f);
		// THE ONE CONTROL THAT VARIES LEVEL ACROSS THE NOTES OF ONE CHORD. A pianist plays the
		// top voice louder so the melody sits above its own harmony; accent varies the beats and
		// cannot do this, because in a block chord every voice of it is accented alike.
		configParam(P_BALANCE, 0.f, 1.f, 0.3f, "Balance", "%", 0.f, 100.f);

		configInput(I_MPX, "MPX note in \u2014 takes an MPX output only");
		configOutput(O_MPX, "MPX note out \u2014 goes to an MPX input only");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~CompModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_MPX;
	}

	// ---- what the widget tells us about the cables ----

	/** The bus this module publishes on. Claimed for the whole life of the module rather than
	when a cable appears, because a slot is cheap and a claim that comes and goes is a cursor
	that has to be rebuilt downstream. */
	int slot = -1;
	uint32_t generation = 0;
	BusReader reader;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	/** Set on the main thread when the patching changes, acted on by the audio thread, so the
	reader is only ever touched by the thread that reads it. */
	std::atomic<bool> relink{false};

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++) {
			same = (slots[i] == wantSlots[i].load())
				&& (generations[i] == wantGenerations[i].load());
		}
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store(std::min(n, MAX_UPSTREAM));
		relink.store(true);
	}

	// ---- state ----

	/** Where each voice sits, in volts. Kept between chords: this is the memory that makes a
	line rather than a series of chords. */
	float voice[MAX_VOICES];
	int voices = 0;
	/** The chord last voiced, as pitch classes, so a change can be recognised. */
	int hadClasses[MAX_CHORD_TONES];
	int hadCount = -1;
	/** The settings the sounding chord was voiced under, so that moving any of them voices it
	again. One number rather than a copy of each, because all that is ever asked is whether
	anything moved. */
	float hadSig = 0.f;
	float lightFade = 0.f;

	/** THE NOTE EACH VOICE IS SOUNDING, or nought for a voice that is silent.

	This is what makes a tie possible, and a tie is the whole point of the module. A voice that
	does not move is a note that is simply never sent again — one event that carries on — rather
	than a gate dipped and struck afresh alongside the voices that did move. Three parallel
	cables could not say that: a gate cable has no way to mark one channel as carrying on. */
	int64_t sounding[MAX_VOICES] = {0};
	/** What pitch each sounding note was sent at, so that a note can be left alone when the step
	that follows it would only ask for the same pitch again. */
	float soundingPitch[MAX_VOICES] = {0.f};

	/** HOW FAST THE BEAT IS GOING, in beats a second, measured rather than told.

	A note carries its own duration so that the far end can finish it even if the note-off is
	lost, and a duration is in seconds while the harmony counts in beats. Nothing on the cable
	says the tempo, but the beat itself advances in real time, so watching how far it gets in a
	known number of samples is the tempo. */
	double lastBeat = 0.0;
	int beatWindow = 0;
	float beatsPerSecond = 0.f;

	/** The chord on the MPX cable, written out as a player reads it: every tone the quality
	implies, each knowing which degree it is and how badly it is wanted. Returns how many. */
	int gatherChord(ChordTone* out) {
		Harmony h;
		if (!reader.harmony(h) || !h.valid)
			return 0;
		return chordVoicingTones(h.current, h.key, out);
	}

	/** A NOTE, AND THE END OF ONE.

	The duration is a safety net rather than the thing that ends the note: this module sends its
	own note-off when a voice moves, and the duration is what lets the far end finish a note
	whose off was lost. It is set to the rest of the chord with a little over, so a note is never
	cut short by it. */
	void sendOn(int v, float level, float duration) {
		if (slot < 0)
			return;
		Event e;
		e.kind = Event::ON;
		e.handle = sounding[v] = mintHandle();
		e.pitch = soundingPitch[v] = voice[v];
		e.level = math::clamp(level, 0.f, 1.f);
		e.duration = math::clamp(duration, 0.005f, 30.f);
		busPush(slot, e);
	}

	/** A HELD NOTE, which is what a chord with no rhythm is. The duration is the rest of the
	chord with a little over: this module sends its own note-off when a voice moves, and the
	duration is only what lets the far end finish a note whose off was lost. */
	void sendHeld(int v) {
		float duration = 30.f;
		if (beatsPerSecond > 0.01f) {
			Harmony h;
			if (reader.harmony(h) && h.valid && h.beatsToNext > 0.f)
				duration = h.beatsToNext / beatsPerSecond * 1.2f;
		}
		sendOn(v, LEVEL_NOMINAL / 10.f, duration);
	}

	void sendOff(int v) {
		if (sounding[v] == 0)
			return;
		if (slot >= 0) {
			Event e;
			e.kind = Event::OFF;
			e.handle = sounding[v];
			busPush(slot, e);
		}
		sounding[v] = 0;
	}

	/** HOW FAST THE BEAT IS GOING. Measured over a window rather than a sample, because a beat
	that advances a few millionths per sample is all rounding error at that scale. */
	void measureTempo(const ProcessArgs& args) {
		Harmony h;
		if (!reader.harmony(h) || !h.valid) {
			beatsPerSecond = 0.f;
			beatWindow = 0;
			return;
		}
		const int window = (int) (args.sampleRate / 20.f);   // twenty times a second
		if (++beatWindow < window)
			return;
		const double moved = h.beat - lastBeat;
		lastBeat = h.beat;
		beatWindow = 0;
		if (moved <= 0.0) {
			beatsPerSecond = 0.f;
			return;
		}
		const float now = (float) moved / ((float) window / args.sampleRate);
		// Smoothed, so a chart that nudges its beat does not shorten every note in the bar.
		beatsPerSecond = (beatsPerSecond <= 0.f) ? now : beatsPerSecond * 0.8f + now * 0.2f;
	}

	/** WHAT GOES ON DOWN THE CHAIN.

	The harmony, forwarded whole, so that a chart into this module into whatever comes next needs
	one cable rather than a second run back to the chart for the beat. The notes too: a lane this
	module has never heard of travels through it untouched, and a chain that swallowed the
	chart's own notes would be a trap.

	The voiced notes are NOT on it yet. Once this module plays a rhythm it is emitting notes, and
	a note on an MPX cable carries its level and its duration — an accented downbeat is one event
	rather than three simultaneous cables the far end has to reassemble. Until then the notes it
	would emit are held chords, which the polyphonic output already carries. */
	void forwardBus() {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		if (slot < 0)
			return;
		Event e;
		while (reader.next(e))
			busPush(slot, e);
		Harmony h;
		if (reader.harmony(h))
			busPublishHarmony(slot, h);
	}

	/** WHICH TONES GET PLAYED, when there are fewer voices than the chord has tones — which is
	the ordinary case rather than the awkward one.

	Taking the lowest ranks is the whole rule, because the ranks were written to be taken in
	order: the tone that says which quality this is, then the seventh, then the colour, then the
	root, and the plain fifth last of all. Three voices under a thirteenth chord therefore play
	the third, the seventh and the thirteenth, which is what a pianist plays and what no amount
	of arithmetic on a polyphonic cable could have worked out.

	MORE VOICES THAN TONES doubles from the same order, so the sixth voice of a triad doubles the
	third rather than whatever happened to be first in the list.

	Writes the pitch classes IN STACK ORDER — the root first and the rest as they rise above it,
	which is what the voicing wants — and returns how many. */
	int chooseTones(const ChordTone* tones, int count, int want, bool ownBass, int colour,
			int* pcs) {
		if (count <= 0)
			return 0;
		int rootPc = tones[0].pc;
		for (int i = 0; i < count; i++)
			if (tones[i].degree == 1)
				rootPc = tones[i].pc;

		// HOW MUCH OF THE CHORD TO PLAY. A triad is the degrees a triad has, which includes the
		// fourth and the second a sus chord puts where its third would be; sevenths adds the
		// seventh and the sixth; extensions is everything the quality implies.
		int keep[MAX_CHORD_TONES];
		int n = 0;
		for (int i = 0; i < count; i++) {
			const int d = tones[i].degree;
			bool take = true;
			if (colour == COLOUR_TRIAD)
				take = (d == 1 || d == 3 || d == 4 || d == 5 || (d == 9 && tones[i].rank == 0));
			else if (colour == COLOUR_SEVENTHS)
				take = (d != 11 && d != 13 && !(d == 9 && tones[i].rank != 0));
			if (take)
				keep[n++] = i;
		}
		// A chord can be left with nothing to play — a fifth chord asked for a third. Whatever
		// the quality does have is better than silence.
		if (n == 0) {
			for (int i = 0; i < count; i++)
				keep[n++] = i;
		}

		int above[MAX_CHORD_TONES];
		int rank[MAX_CHORD_TONES];
		// THE ELEVENTH IS WHY THE THIRD WAS RANKED LAST. Take the eleventh away and the third
		// is an ordinary third again, so the table's ranking has to be undone here rather than
		// leaving an eleventh chord played as a rootless fifth.
		bool hasEleven = false;
		for (int k = 0; k < n; k++)
			hasEleven = hasEleven || (tones[keep[k]].degree == 11);
		for (int k = 0; k < n; k++) {
			const ChordTone& t = tones[keep[k]];
			above[k] = ((t.pc - rootPc) % 12 + 12) % 12;
			// PLAYING ITS OWN BOTTOM MAKES THE ROOT ESSENTIAL. The table ranks it fourth
			// because a bass usually has it; when nothing else does, it is the first tone
			// kept and the first tone doubled.
			if (ownBass && t.degree == 1)
				rank[k] = -1;
			else if (!hasEleven && t.degree == 3 && t.rank > 4)
				rank[k] = 0;
			else
				rank[k] = t.rank;
		}
		count = n;

		// The keep order: rank first, and the lower tone first where two are ranked alike.
		int order[MAX_CHORD_TONES];
		for (int i = 0; i < count; i++)
			order[i] = i;
		std::sort(order, order + count, [&](int a, int b) {
			if (rank[a] != rank[b])
				return rank[a] < rank[b];
			return above[a] < above[b];
		});

		// Take that many, doubling round the same order when there are more voices than tones,
		// then put them back into stack order for the voicing.
		int chosen[MAX_VOICES];
		const int take = std::min(want, MAX_VOICES);
		for (int v = 0; v < take; v++)
			chosen[v] = order[v % count];
		std::sort(chosen, chosen + take, [&](int a, int b) { return above[a] < above[b]; });
		for (int v = 0; v < take; v++)
			pcs[v] = tones[keep[chosen[v]]].pc;
		return take;
	}

	/** THE VOICING. The search itself is in Voicing.cpp, which is arithmetic and can be tested
	without a running Rack; this is what the panel means, handed to it. */
	void revoice(const ChordTone* tones, int count, float lead) {
		const int want = (int) std::round(params[P_VOICES].getValue()) + 1;
		const float centre = params[P_CENTRE].getValue() / 12.f;
		const float span = std::round(params[P_SPAN].getValue()) + 1.f;

		const bool ownBass = params[P_BASS].getValue() > 0.5f;
		int pcs[MAX_VOICES];
		const int colour = (int) std::round(params[P_COLOUR].getValue());
		const int n = chooseTones(tones, count, want, ownBass, colour, pcs);
		if (n <= 0)
			return;

		VoicingRequest req;
		req.pcs = pcs;
		req.count = n;
		req.held = voice;
		req.heldCount = voices;
		req.lo = centre - span / 2.f;
		req.hi = centre + span / 2.f;
		req.lead = lead;
		req.spread = (int) std::round(params[P_SPREAD].getValue());
		// The root is first in stack order, so where this part carries its own bottom the
		// lowest voice is pinned to tone nought.
		req.bassTone = ownBass ? 0 : -1;

		voices = voicePlace(req, voice);
	}

	// ---- the rhythm ---------------------------------------------------------------------

	/** Which step of the figure was last started, counted from the start of the cycle so that
	two of these modules on the same chart agree without being told. */
	int64_t lastStep = -1;
	/** Seconds until a voice's note starts, for a strum; negative for nothing waiting. */
	float startIn[MAX_VOICES];
	float startLevel[MAX_VOICES];
	/** Seconds until a sounding note is ended, which is what gate length means. */
	float endIn[MAX_VOICES];
	/** Whether the rhythm was running last sample, so that switching it either way can clear up
	after itself. */
	bool rhythmWas = false;

	/** A cheap generator for humanising. On the audio thread, so nothing that allocates or
	locks: this is the whole of it. */
	uint32_t noise = 0x9e3779b9u;
	float dice() {
		noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
		return (float) (noise >> 8) / 16777216.f;
	}

	void rhythmReset() {
		lastStep = -1;
		for (int v = 0; v < MAX_VOICES; v++) {
			startIn[v] = -1.f;
			endIn[v] = -1.f;
		}
	}

	/** WHO PLAYS ON THIS STEP. The whole of what a pattern is.

	Alberti is low, high, middle, high — the figure Mozart wrote under half his left hands — and
	a waltz is the bass on the first beat with the rest of the chord on the others, which is the
	oom-pah-pah every accordion has ever played. Both are named after what they do rather than
	invented here. */
	bool playsOn(int pattern, int step, int voice, int n) const {
		if (n <= 0)
			return false;
		switch (pattern) {
			case 1:  return voice == (step % n);                    // broken, upward
			case 2:  return voice == (n - 1 - (step % n));          // broken, downward
			case 3: {                                               // Alberti
				const int seq[4] = {0, n - 1, n / 2, n - 1};
				return voice == seq[((step % 4) + 4) % 4];
			}
			case 4:                                                 // waltz
				return (step % 3 == 0) ? (voice == 0) : (voice != 0);
			default: return true;                                   // block
		}
	}

	/** HOW HARD THIS STEP IS STRUCK, before the balance and the humanising.

	METRE RATHER THAN CHOICE. The harmony carries the time signature, so strong-weak-medium-weak
	is a lookup and stays right through a chart that changes metre — which is the whole reason
	the beat travels on the cable rather than on a clock jack. Four four is strong, weak, medium,
	weak; three four is strong, weak, weak; six eight is two groups of three. */
	float accentOf(int accent, float beatInBar, int barBeats, float beatsToNext,
			float stepBeats) const {
		const float frac = beatInBar - std::floor(beatInBar);
		const int beat = (int) std::floor(beatInBar);
		switch (accent) {
			case 1: {   // metric
				if (beat == 0 && frac < 0.01f)
					return 1.f;
				const bool compound = (barBeats % 3 == 0 && barBeats > 3);
				const int middle = compound ? barBeats / 2 : barBeats / 2;
				if (barBeats > 2 && beat == middle && frac < 0.01f)
					return 0.5f;
				return 0.f;
			}
			case 2:  return (beat == 0 && frac < 0.01f) ? 1.f : 0.f;            // downbeat
			case 3:  return (beat % 2 == 1 && frac < 0.01f) ? 1.f : 0.f;        // backbeat
			case 4:  return (frac >= 0.4f) ? 1.f : 0.f;                         // offbeat
			// PUSH ANTICIPATES THE CHANGE: the last step before the chord turns over is the one
			// that leans. Only the harmony knows when that is, which is why it needs no clock.
			case 5:  return (beatsToNext > 0.f && beatsToNext <= stepBeats * 1.01f) ? 1.f : 0.f;
			default: return 0.f;                                                // even
		}
	}

	/** A STEP FALLS. Whoever is sounding and playing again is ended first; whoever is starting is
	queued, immediately or a strum's distance apart. */
	void startStep(int64_t step, const Harmony& h, float stepBeats, float stepSeconds) {
		const int pattern = (int) std::round(params[P_PATTERN].getValue());
		const int accent = (int) std::round(params[P_ACCENT].getValue());
		const float amount = params[P_AMOUNT].getValue();
		const float human = params[P_HUMAN].getValue();
		const float balance = params[P_BALANCE].getValue();
		const float gate = params[P_GATE].getValue();
		const float strum = params[P_STRUM].getValue() * STRUM_MAX;

		const double beatOfStep = (double) step * (double) stepBeats;
		const int barBeats = std::max(1, (int) h.barBeats);
		float beatInBar = (float) std::fmod(beatOfStep, (double) barBeats);
		if (beatInBar < 0.f)
			beatInBar += (float) barBeats;
		const float weight = accentOf(accent, beatInBar, barBeats, h.beatsToNext, stepBeats);

		// A VOICE THIS STEP DOES NOT ASK FOR IS ENDED, whether or not it was being held. Without
		// this a tied voice from a block chord would go on sounding under a broken figure that
		// never mentions it again.
		for (int v = 0; v < MAX_VOICES; v++) {
			if (v >= voices || !playsOn(pattern, (int) step, v, voices)) {
				if (endIn[v] < 0.f && sounding[v] != 0 && startIn[v] < 0.f)
					endIn[v] = 0.0001f;
			}
		}

		int order = 0;
		for (int v = 0; v < voices; v++) {
			if (!playsOn(pattern, (int) step, v, voices))
				continue;
			// The top voice is the melody, so it carries a little more of the level than the
			// ones underneath it — which is the one thing an accent cannot do, since an accent
			// falls on every voice of a block chord alike.
			const float top = (voices > 1) ? (float) v / (float) (voices - 1) : 1.f;
			float level = LEVEL_NOMINAL
				+ weight * amount * ACCENT_LIFT
				+ (top - 0.5f) * balance * BALANCE_LIFT
				- dice() * human * HUMAN_DROP;
			startIn[v] = (float) order * strum;
			startLevel[v] = math::clamp(level, 0.f, 10.f);
			endIn[v] = -1.f;
			order++;
		}
		// Everything that is sounding and is not starting again is left to its own gate.
		stepSecondsNow = std::fmax(0.005f, stepSeconds);
		gateSeconds = std::fmax(0.005f, gate * stepSeconds);
		// FULL GATE MEANS TIE, not "a note exactly one step long". A note that ends precisely
		// when the next one begins is re-struck every step, so at the top of the range there is
		// no way to hold a chord across two beats at all — which is what the control was reached
		// for. At the top it holds instead: a voice asked for the same pitch again is left
		// sounding, and only a voice that moves, stops or changes chord is struck afresh.
		tieing = (gate > 0.99f);
	}

	/** How long a note started now is held. Set when the step starts, spent when it fires. */
	float gateSeconds = 0.1f;
	/** Whether this step's notes hold rather than end. */
	bool tieing = false;
	/** How long this step lasts, which is how long its notes mean to sound. The gate is what
	cuts them shorter than that. */
	float stepSecondsNow = 0.25f;

	/** Called every sample while the rhythm is on: what has come due. */
	void rhythmTick(float dt) {
		for (int v = 0; v < MAX_VOICES; v++) {
			if (startIn[v] >= 0.f) {
				startIn[v] -= dt;
				if (startIn[v] <= 0.f) {
					startIn[v] = -1.f;
					if (v < voices) {
						// Already sounding the very note this step asks for, and holding: leave
						// it alone. That is the tie, and it is the same rule the module uses
						// with no rhythm at all.
						if (tieing && sounding[v] != 0
							&& std::fabs(soundingPitch[v] - voice[v]) < 1e-4f) {
							endIn[v] = -1.f;
							continue;
						}
						if (sounding[v] != 0)
							sendOff(v);
						// THE DURATION IS THE NOTE'S OWN LENGTH, not the gate's. A note carries
						// how long it means to last; the gate is what interrupts it. Sending the
						// gate's length as the duration made the two the same number and the
						// distinction vanished — nothing was ever cut short, because nothing
						// intended to last longer than it did.
						//
						// It matters as soon as anything but this module can end a note: a
						// receiving module that loses an off can still finish the note properly,
						// and one that steals a voice knows what it is interrupting.
						sendOn(v, startLevel[v] / 10.f, tieing ? 30.f : stepSecondsNow);
						endIn[v] = tieing ? -1.f : gateSeconds;
					}
				}
			}
			if (endIn[v] >= 0.f) {
				endIn[v] -= dt;
				if (endIn[v] <= 0.f) {
					endIn[v] = -1.f;
					sendOff(v);
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		forwardBus();
		measureTempo(args);

		ChordTone tones[MAX_CHORD_TONES];
		const int count = gatherChord(tones);

		bool chordChanged = (count != hadCount);
		for (int i = 0; !chordChanged && i < count; i++)
			chordChanged = (tones[i].pc != hadClasses[i]);

		// A KNOB HAS TO DO SOMETHING WHEN IT IS TURNED. Every setting here decides the voicing,
		// so a chord already sounding is voiced again the moment one of them moves — otherwise
		// turning Register does nothing at all until the bar turns over, which reads as a
		// broken control rather than as a patient one.
		const float sig = params[P_VOICES].getValue()
			+ 8.f * params[P_BASS].getValue()
			+ 64.f * params[P_CENTRE].getValue()
			+ 4096.f * params[P_SPAN].getValue()
			+ 32768.f * params[P_SPREAD].getValue()
			+ 262144.f * params[P_COLOUR].getValue()
			+ 2097152.f * params[P_LEAD].getValue();
		const bool settingsChanged = (sig != hadSig);
		hadSig = sig;

		const bool rhythmOn = params[P_RHYTHM].getValue() > 0.5f;

		if (count > 0 && (chordChanged || settingsChanged)) {
			// KEPT, so that what each voice was playing can be compared with what it is to play
			// now. A voice whose pitch has not changed is not sent again, and that is the tie.
			float was[MAX_VOICES];
			const int wasVoices = voices;
			for (int v = 0; v < wasVoices; v++)
				was[v] = voice[v];

			revoice(tones, count, params[P_LEAD].getValue());
			hadCount = count;
			for (int i = 0; i < count; i++)
				hadClasses[i] = tones[i].pc;

			// WITH A RHYTHM RUNNING THE CHORD CHANGE SENDS NOTHING. The next step will play the
			// new voicing when it falls, and striking the chord here as well would put an extra
			// note in front of the beat every time the harmony turned over.
			if (!rhythmOn) {
				for (int v = 0; v < MAX_VOICES; v++) {
					const bool had = (v < wasVoices);
					const bool has = (v < voices);
					// A HELD TONE IS LEFT ALONE. Nothing is sent for it at all: the note it is
					// already sounding carries on, which is what a player's finger does.
					if (had && has && sounding[v] != 0
						&& std::fabs(voice[v] - was[v]) < 1e-4f)
						continue;
					if (sounding[v] != 0)
						sendOff(v);
					if (has)
						sendHeld(v);
				}
			}
			if (chordChanged)
				lightFade = 1.f;
		}
		// ---- the rhythm, or the held chord it replaces ----
		//
		// SWITCHING EITHER WAY CLEARS UP AFTER ITSELF. Turning it on with a chord held would
		// otherwise leave the voices that the figure never touches sounding for ever; turning it
		// off mid-figure would leave the part silent until the next chord change.
		if (rhythmOn != rhythmWas) {
			rhythmWas = rhythmOn;
			for (int v = 0; v < MAX_VOICES; v++)
				sendOff(v);
			rhythmReset();
			if (!rhythmOn) {
				for (int v = 0; v < voices; v++)
					sendHeld(v);
			}
		}

		if (rhythmOn && count > 0) {
			Harmony h;
			if (reader.harmony(h) && h.valid) {
				// A WHOLE BAR IS THE SLOWEST STEP, and it has to be asked of the harmony rather
				// than written down: the metre can change while the chart plays.
				const int rate = (int) std::round(params[P_RATE].getValue());
				const float stepBeats = (RATE_BEATS[rate] > 0.f)
					? RATE_BEATS[rate] : (float) std::max(1, (int) h.barBeats);
				const double pos = h.beat / (double) stepBeats;
				const int64_t step = (int64_t) std::floor(pos);
				if (step != lastStep) {
					// A jump — the chart rewound, or a step was missed under load — starts from
					// where we are rather than trying to catch up through every step between.
					lastStep = step;
					const float stepSeconds = (beatsPerSecond > 0.01f)
						? stepBeats / beatsPerSecond : 0.25f;
					startStep(step, h, stepBeats, stepSeconds);
				}
			}
			rhythmTick(args.sampleTime);
		}

		if (count == 0 && voices != 0) {
			// Nothing to play. Every note is ended rather than left hanging on a far end that
			// has no way of knowing the chart stopped.
			for (int v = 0; v < MAX_VOICES; v++)
				sendOff(v);
			rhythmReset();
			voices = 0;
			hadCount = -1;
		}

		if (lightFade > 0.f)
			lightFade = std::fmax(0.f, lightFade - args.sampleTime * 4.f);
		lights[L_CHANGE].setBrightness(lightFade);
	}
};


static Layout compLayout() {
	Layout L;
	L.hp = 20.f;
	L.title = "mpxComp";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	// TWO MILLIMETRES FROM THE EDGE OF WHAT IT NAMES, and the edge is the VISIBLE one rather
	// than the widget's centre. A RoundBlackKnob is 9.6 mm across, so its edge is 4.8 mm out;
	// its ticks reach 1.3 further; a name set at ten points stands 1.22 either side of where it
	// is placed. Add those and the offset falls out. Guessing instead of adding them is what
	// made the knob names look pressed against the knob and the jack names float away from the
	// jack, since a knob and a jack are not the same size.
	static const float NAME_HALF = 1.22f;    /**< Half the height of a ten-point name. */
	static const float KNOB_EDGE = 4.8f;     /**< RoundBlackKnob, from its SVG. */
	static const float PORT_EDGE = 4.01f;    /**< PJ301M, from its SVG. */
	static const float GAP = 2.f;

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			int ticks = 2, const std::vector<std::string>& marks = {}) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
		i.ticks = ticks; i.tickMarks = marks;
		L.items.push_back(i);
		// FROM THE EDGE OF THE KNOB, not from the ends of its ticks. The ticks are hairlines and
		// the knob is what the eye takes for the control, so a name measured from the ticks
		// reads as a millimetre and a half too far.
		//
		// NUMBERS ARE NOT HAIRLINES, though. Where a knob carries them the sweep ends pointing
		// down and to either side, so the lowest of them reach 7.1 mm below the middle — further
		// than the metal does — and that is the edge the name has to clear.
		const float edge = marks.empty() ? KNOB_EDGE : 7.1f;
		label(key + ".label", x, y + edge + GAP + NAME_HALF, name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		// A jack's name clears the jack, which is smaller than a knob — hence its own number.
		label(key + ".label", x, y + PORT_EDGE + GAP + 0.98f, name, Panel::CENTRE,
			size > 0.f, size, key);
	};

	// A CHOICE BETWEEN NAMED THINGS IS NOT A KNOB. Voices, Span, Spread, Pattern and Accent each
	// pick one of a short list, and a knob with a pointer at eleven o'clock says nothing about
	// which. They are lamp columns instead: one lamp a choice, its name beside it, and the group
	// named above. A column is placed by its TOP LEFT CORNER, not its centre, because it is a
	// list; its width is the longest name and its height is the pitch times the lamps.
	auto radio = [&](const std::string& key, float x, float y, int id, const std::string& group,
			const std::vector<std::string>& names, float pitch) {
		// A lamp column's top edge is its corner, since it is placed by the corner.
		label(key + ".group", x, y - GAP - NAME_HALF, group, Panel::LEFT, true, 0.f, key);
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "lamps";
		i.names = names; i.horizontal = false; i.pitch = pitch;
		i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
	};

	// TWENTY HP IS 101.6 MM, and it is twenty rather than sixteen because of the rule above.
	// Names two millimetres clear of what they name, and a Voices knob wearing a number at every
	// detent, need the height; four narrower columns hold it where three wider ones could not.
	//
	// Every position here was checked rather than eyed: each control's visible extent worked out
	// from the component sizes, and no two of them closer than a millimetre.
	//
	// VOICES IS A KNOB WITH SIX DETENTS rather than a column of lamps: it is a count, and a
	// count reads round a dial the way a number reads. It carries a number at every detent, so
	// the count can be set by looking at it.
	knob("p.voices", 15.f, 30.f, CompModule::P_VOICES, "VOICES", 6,
		{"1", "2", "3", "4", "5", "6"});
	knob("p.centre", 15.f, 49.f, CompModule::P_CENTRE, "REGISTER", 3);
	radio("p.bass", 6.f, 65.2f, CompModule::P_BASS, "ROOT",
		{"ROOTLESS", "CHORD HAS\nTHE ROOT"}, 6.5f);

	radio("p.span", 32.f, 24.f, CompModule::P_SPAN, "SPAN",
		{"1 OCT", "2 OCT", "3 OCT"}, 5.5f);
	radio("p.spread", 32.f, 45.f, CompModule::P_SPREAD, "SPREAD",
		{"CLOSE", "DROP 2", "OPEN"}, 5.5f);

	knob("p.lead", 72.f, 28.f, CompModule::P_LEAD, "VOICE LEADING", 2);

	// HOW MUCH OF THE CHORD, under the leading knob, because it is the other half of what a
	// style of accompaniment is as far as the notes are concerned.
	radio("p.colour", 56.f, 45.f, CompModule::P_COLOUR, "CHORD",
		{"TRIAD", "SEVENTHS", "EXTENSIONS"}, 5.5f);

	label("h.rhythm", 45.f, 76.3f, "RHYTHM", Panel::CENTRE, false, 7.f);

	// THE FOUR THE RHYTHM NEEDED THAT THE PANEL DID NOT YET HAVE. Placed where there was room
	// rather than where they belong — the arrangement is a thing to do by eye in the editor.
	radio("p.rhythm", 52.f, 64.f, CompModule::P_RHYTHM, "RHYTHM", {"OFF", "ON"}, 5.5f);
	radio("p.rate", 68.f, 64.f, CompModule::P_RATE, "PER BEAT", {"1", "2", "3", "4"}, 5.f);
	knob("p.strum", 88.f, 66.f, CompModule::P_STRUM, "STRUM", 2);
	knob("p.balance", 88.f, 100.f, CompModule::P_BALANCE, "BALANCE", 2);

	// UP AND DOWN RATHER THAN BROKEN UP AND BROKEN DOWN. The group is called Pattern and the
	// entry above them is Block, which says what kind of thing they are; the longer wording made
	// the column wide enough to reach the one beside it.
	radio("p.pattern", 6.f, 83.f, CompModule::P_PATTERN, "PATTERN",
		{"BLOCK", "UP", "DOWN", "ALBERTI", "WALTZ"}, 4.2f);
	radio("p.accent", 30.f, 83.f, CompModule::P_ACCENT, "ACCENT",
		{"EVEN", "METRIC", "DOWNBEAT", "BACKBEAT", "OFFBEAT", "PUSH"}, 4.2f);

	knob("p.gate", 60.f, 86.f, CompModule::P_GATE, "GATE", 2);
	knob("p.amount", 78.f, 86.f, CompModule::P_AMOUNT, "AMOUNT", 2);
	knob("p.human", 94.f, 86.f, CompModule::P_HUMAN, "HUMAN", 2);

	// TWO JACKS, one in and one out, at opposite ends of the row so that a chain reads left to
	// right. Everything the module is handed and everything it plays travels on those two.
	jack("in.mpx", Item::PORT_IN, 12.f, 114.f, CompModule::I_MPX, "mpx\nIN", NOTE_CABLE, 7.f);
	jack("out.mpx", Item::PORT_OUT, 89.f, 114.f, CompModule::O_MPX, "mpx\nOUT", NOTE_CABLE, 7.f);

	Item lamp;
	lamp.key = "lamp.change"; lamp.kind = Item::LIGHT; lamp.id = CompModule::L_CHANGE;
	lamp.x = 30.f; lamp.y = 114.f;
	L.items.push_back(lamp);


	L.bindOffsets();
	return L;
}


struct CompWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	CompWidget(CompModule* module) {
		setModule(module);
		layout = compLayout();
		layoutApplyUser("mpxComp", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxComp");
	}

	void step() override {
		ModuleWidget::step();
		CompModule* comp = dynamic_cast<CompModule*>(module);
		if (!comp)
			return;
		// WHICH MPX CABLES ARE PATCHED, resolved here rather than in the audio thread: the
		// engine knows nothing of cables, and the widget is walked once a frame anyway.
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(CompModule::I_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				if (n >= MAX_UPSTREAM)
					break;
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int s = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (s < 0)
					continue;
				cw->color = NOTE_CABLE;
				slots[n] = s;
				generations[n] = g;
				n++;
			}
		}
		comp->link(slots, generations, n);

		if (PortWidget* out = getOutput(CompModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMpxComp = createModel<px::CompModule, px::CompWidget>("mpxComp");
