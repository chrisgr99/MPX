/** mpxArp — one note at a time, from the chord on the cable.

WHAT IT IS. An arpeggiator: the chord arrives on an MPX cable, this plays its tones one after
another in time with the chart's own beat, and sends them out as notes.

AND WHAT IT IS ALSO FOR, which is why it was built when it was. mpxComp is several decisions
stacked on one another — voicing, tone selection, voice leading, pattern, accent, humanising —
and it plays them all at once, so when the sound is wrong there is no way to hear WHICH of them
was wrong. This plays one note, at a known moment, at a known pitch. A click is a click on one
note; a release that gets cut off is one release; a voice that never comes back is one voice.
That makes it the instrument to test the rest of the chain with, and it is a module worth having
on its own account, which is why it is not a test rig.

ONE KNOB DOES GATE AND OVERLAP, and that is the control that matters here. LENGTH is how long a
note lasts as a fraction of the step it started on. Under a hundred it is staccato; at a hundred
each note ends as the next begins; ABOVE a hundred the notes overlap, deliberately, and that is
the only way a single line can exercise voice allocation, voice stealing and the returning
envelope at all. Turn it past the mark and raise the voice count on fromMPX and you can hear
directly whether a release is being cut short.

CHORD IS A DIRECTION. The fifth position of the direction control strikes every tone together
instead of one after another, so the same panel tests notes arriving at the same instant — which
is the one thing a line cannot show — without any of comp's machinery in the way.

NO CLOCK, for the same reason as comp: the beat, the bar and the metre are already on the cable,
and a clock jack would be a second timebase fighting the first.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <algorithm>
#include <cmath>

namespace px {


/** How many notes may be sounding at once. Only overlap ever needs more than one, and overlap is
bounded by LENGTH_MAX steps, so this is generous rather than calculated. */
static const int MAX_SOUNDING = 16;

/** WHAT A NOTE IS STRUCK AT WHEN NOTHING IS SHAPING IT. Eight volts, which is what mpxComp emits,
so the two modules drive an amplifier to the same place and swapping one for the other does not
change the gain of the patch. */
static const float LEVEL_SAME = 0.8f;

static const char* RATE_NAMES[] = {"1 a bar", "1 in 2 beats", "1 a beat", "2 a beat",
	"3 a beat", "4 a beat"};
static const int NUM_RATES = (int) (sizeof(RATE_NAMES) / sizeof(RATE_NAMES[0]));
/** How many beats one step lasts. Nought means a whole bar, whatever the metre says one is —
which cannot be written down here, because a chart may change metre while it plays. */
static const float RATE_BEATS[NUM_RATES] = {0.f, 2.f, 1.f, 0.5f, 1.f / 3.f, 0.25f};

enum Direction {
	DIR_UP,
	DIR_DOWN,
	DIR_UPDOWN,
	DIR_RANDOM,
	/** Every tone at once, which is the case a line cannot show. */
	DIR_CHORD,
	NUM_DIRECTIONS,
};

static const char* DIRECTION_NAMES[NUM_DIRECTIONS] =
	{"Up", "Down", "Up and down", "Random", "Chord — all at once"};

/** HOW MUCH OF THE CHORD TO PLAY. The same three as mpxComp, and the same words, because they
mean the same thing and a person moving between the two modules should not have to learn them
twice. */
static const char* TONES_NAMES[] = {"Triad", "Sevenths", "Extensions"};
static const int NUM_TONES = (int) (sizeof(TONES_NAMES) / sizeof(TONES_NAMES[0]));
/** How many tones each of those takes. Nought means every tone the chord has. */
static const int TONES_COUNT[NUM_TONES] = {3, 4, 0};

/** WHAT EACH NOTE IS STRUCK AT. A level that never varies tells you nothing about whether the
level lane is working at all, so the shapes are here to make it audible. */
static const char* VELOCITY_NAMES[] = {"Same", "Rising", "Falling", "Random", "First loudest"};
static const int NUM_VELOCITIES = (int) (sizeof(VELOCITY_NAMES) / sizeof(VELOCITY_NAMES[0]));
/** THE RANGE A SHAPE COVERS, and it is nearly all of it.

It was a small deviation from the reference at first — eight volts give or take three — which is
mpxComp's arithmetic, and there it is right: comp emits an accent, an accent is a stress rather
than a different note, and the room above the reference is deliberately left for it.

That is the wrong scale for a module whose job is to show you whether the level lane is working.
Five volts to ten is six decibels, and six decibels of note-to-note difference disappears
underneath an envelope with a percussive shape — which is what it did. A shape now runs from
almost nothing to full, better than twenty-five decibels, so it is not a question of listening
carefully. */
static const float VELOCITY_QUIET = 0.08f;
static const float VELOCITY_LOUD = 1.f;
/** What the notes that are NOT the accented one drop to, for the shape that stresses one. */
static const float VELOCITY_UNDER = 0.25f;

/** The range of the LENGTH knob, as a fraction of one step. The top is three steps, so that
overlap can be got well past the point where it merely joins one note to the next. */
static const float LENGTH_MIN = 0.05f, LENGTH_MAX = 3.f;


struct ArpModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_RATE,
		P_DIRECTION,
		P_OCTAVES,
		P_TONES,
		P_CENTRE,
		P_LENGTH,
		P_VELOCITY,
		P_BEND,
		// APPENDED, NEVER INSERTED. A patch stores a parameter by its POSITION in this list, so
		// a new one in the middle hands every value after it to the wrong control when an old
		// patch is opened. New controls go on the end however untidy that reads.
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
		L_STEP,
		NUM_LIGHTS
	};

	ArpModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configSwitch(P_RATE, 0.f, (float) (NUM_RATES - 1), 2.f, "Rate",
			{RATE_NAMES[0], RATE_NAMES[1], RATE_NAMES[2], RATE_NAMES[3], RATE_NAMES[4],
			RATE_NAMES[5]});
		configSwitch(P_DIRECTION, 0.f, (float) (NUM_DIRECTIONS - 1), 0.f, "Direction",
			{DIRECTION_NAMES[0], DIRECTION_NAMES[1], DIRECTION_NAMES[2], DIRECTION_NAMES[3],
			DIRECTION_NAMES[4]});
		// COUNTED FROM NOUGHT, so the value is which position of the control is chosen rather
		// than the number itself. Every place that wants the count adds one.
		configSwitch(P_OCTAVES, 0.f, 2.f, 0.f, "Octaves", {"1", "2", "3"});
		configSwitch(P_TONES, 0.f, (float) (NUM_TONES - 1), 1.f, "Chord tones",
			{TONES_NAMES[0], TONES_NAMES[1], TONES_NAMES[2]});
		// SEMITONES FROM MIDDLE C, as on mpxComp, because that is how a musician says where a
		// part sits and the number reads the same whatever key the chart is in.
		configParam(P_CENTRE, -24.f, 24.f, 0.f, "Register", " semitones from middle C");
		paramQuantities[P_CENTRE]->snapEnabled = true;
		// PER CENT OF A STEP, and the hundred mark is the one that matters: below it the notes
		// are separated, at it they meet, above it they overlap.
		configParam(P_LENGTH, LENGTH_MIN, LENGTH_MAX, 0.9f, "Length", "% of a step", 0.f, 100.f);
		configSwitch(P_VELOCITY, 0.f, (float) (NUM_VELOCITIES - 1), 0.f, "Velocity",
			{VELOCITY_NAMES[0], VELOCITY_NAMES[1], VELOCITY_NAMES[2], VELOCITY_NAMES[3],
			VELOCITY_NAMES[4]});
		// A BEND THAT HAPPENS AFTER THE NOTE HAS STARTED, which is the only way to test that
		// bend reaches the far end at all: a note that is simply played at the bent pitch would
		// look identical. Nought is off and it is the default, since an arpeggio that slides is
		// not what most people want first.
		configParam(P_BEND, -2.f, 2.f, 0.f, "Bend over the note", " semitones");

		configInput(I_MPX, "MPX in");
		configOutput(O_MPX, "MPX out");

		slot = busClaim(&generation);
		for (int i = 0; i < MAX_SOUNDING; i++)
			sounding[i] = Sounding();
	}

	~ArpModule() {
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

	int slot = -1;
	uint32_t generation = 0;
	BusReader reader;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
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

	// ---- what is sounding ----

	/** ONE NOTE THAT HAS BEEN SENT AND NOT YET ENDED.

	A pool rather than a voice per line, because with overlap there is no fixed number: at three
	steps of length there are three notes alive at once whatever the direction says. Each one
	carries its own handle, which is what lets it be ended on its own rather than by position. */
	struct Sounding {
		int64_t handle = 0;
		/** Seconds until this note is ended. */
		float endIn = 0.f;
		/** Where the bend has reached, in volts, and where it is going. */
		float bendTo = 0.f;
		float bendSent = 0.f;
		/** How long the note was given, so the bend knows how far through it is. */
		float span = 0.1f;
		float lived = 0.f;
	};
	Sounding sounding[MAX_SOUNDING];

	int64_t lastStep = -1;
	/** Where in the figure we are, which is not the step number: a chart that jumps should not
	jump the arpeggio to a different note of the chord. */
	int cursor = 0;
	float lightFade = 0.f;
	/** How often bend updates go out, in seconds. Every sample would be thousands of events a
	second saying almost the same thing; fifty a second is finer than any ear and finer than the
	ramp the far end applies to them anyway. */
	float bendClock = 0.f;

	/** A cheap generator for the random direction and the random velocity. On the audio thread,
	so nothing that allocates or locks: this is the whole of it. */
	uint32_t noise = 0x2545f491u;
	float dice() {
		noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
		return (float) (noise >> 8) / 16777216.f;
	}

	double lastBeat = 0.0;
	int beatWindow = 0;
	float beatsPerSecond = 0.f;

	/** WHICH TIME ROUND THE FORM WE ARE, as the chart counts it. When it changes the music has
	gone back to the top, so the figure starts again from its first note and the generator is put
	back to the seed — which is what makes a random direction, or a random velocity, play the
	same way on every pass instead of merely sounding similar. */
	uint32_t lastEpoch = 0;
	bool haveEpoch = false;

	void backToTheTop(const Harmony& h) {
		cursor = 0;
		lastStep = -1;
		// Mixed rather than taken plainly, so seeds one apart do not give sequences one apart.
		uint32_t x = h.seed * 0x9e3779b9u + 0x85ebca6bu;
		x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
		noise = x ? x : 0x2545f491u;
	}

	/** HOW FAST THE BEAT IS GOING, measured over a window rather than a sample, because a beat
	that advances a few millionths per sample is all rounding error at that scale. */
	void measureTempo(const ProcessArgs& args) {
		Harmony h;
		if (!reader.harmony(h) || !h.valid) {
			beatsPerSecond = 0.f;
			beatWindow = 0;
			return;
		}
		const int window = (int) (args.sampleRate / 20.f);
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
		beatsPerSecond = (beatsPerSecond <= 0.f) ? now : beatsPerSecond * 0.8f + now * 0.2f;
	}

	/** Everything arriving goes on down the chain, the harmony with it, so a chart into this
	module into whatever comes next needs one cable rather than a second run back for the beat. */
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

	// ---- the notes themselves ----

	void sendOff(int i) {
		Sounding& s = sounding[i];
		if (s.handle == 0)
			return;
		if (slot >= 0) {
			Event e;
			e.kind = Event::OFF;
			e.handle = s.handle;
			busPush(slot, e);
		}
		s.handle = 0;
	}

	void allOff() {
		for (int i = 0; i < MAX_SOUNDING; i++)
			sendOff(i);
	}

	/** A free place for a note, or the one that has least of its life left — which is the right
	one to take, since it is the note nearest to ending anyway. */
	int placeFor() {
		for (int i = 0; i < MAX_SOUNDING; i++) {
			if (sounding[i].handle == 0)
				return i;
		}
		int take = 0;
		for (int i = 1; i < MAX_SOUNDING; i++) {
			if (sounding[i].endIn < sounding[take].endIn)
				take = i;
		}
		sendOff(take);
		return take;
	}

	void strike(float pitch, float level, float seconds, float bendTo) {
		if (slot < 0)
			return;
		const int i = placeFor();
		Sounding& s = sounding[i];
		Event e;
		e.kind = Event::ON;
		e.handle = s.handle = mintHandle();
		e.pitch = pitch;
		e.level = math::clamp(level, 0.f, 1.f);
		// THE DURATION IS WHAT WE MEAN, not a safety net. This module sends its own note-off at
		// the end of the length, and the duration says the same thing, so a far end that lost the
		// off still finishes the note where it was meant to finish.
		e.duration = math::clamp(seconds, 0.005f, 30.f);
		e.bendRange = 2.f;
		busPush(slot, e);
		s.endIn = seconds;
		s.span = std::fmax(0.01f, seconds);
		s.lived = 0.f;
		s.bendTo = bendTo;
		s.bendSent = 0.f;
	}

	/** Ends what has come due, and moves the bend of what has not. */
	void tick(float dt) {
		bendClock += dt;
		const bool update = (bendClock >= 0.02f);
		if (update)
			bendClock = 0.f;

		for (int i = 0; i < MAX_SOUNDING; i++) {
			Sounding& s = sounding[i];
			if (s.handle == 0)
				continue;
			s.lived += dt;
			s.endIn -= dt;
			if (s.endIn <= 0.f) {
				sendOff(i);
				continue;
			}
			// A STRAIGHT RAMP ACROSS THE NOTE. Anything shapelier would be a guess about what a
			// player does; a ramp is unmistakable, which is what a thing built to be checked
			// against should be.
			if (update && s.bendTo != 0.f) {
				const float want = s.bendTo * math::clamp(s.lived / s.span, 0.f, 1.f);
				if (std::fabs(want - s.bendSent) > 1e-4f) {
					s.bendSent = want;
					Event e;
					e.kind = Event::UPDATE;
					e.lane = LANE_BEND;
					e.handle = s.handle;
					// IN VOLTS, RAW, which is the convention toMPX already sends on: the far end
					// adds it to the held pitch, and the bend range only decides what the separate
					// control voltage calls full deflection.
					e.value = want;
					busPush(slot, e);
				}
			}
		}
	}

	// ---- the figure ----

	/** The tones to play, as semitones above the chord's root, ascending and starting at nought.
	Returns how many, and writes the root's own pitch class. */
	int gatherSteps(int* offsets, int* rootPc) {
		Harmony h;
		if (!reader.harmony(h) || !h.valid)
			return 0;
		ChordTone tones[MAX_CHORD_TONES];
		const int count = chordVoicingTones(h.current, h.key, tones);
		if (count <= 0)
			return 0;

		// The root is what everything else is measured from, and it is the tone that says it is
		// the first degree rather than whichever one happens to be listed first.
		int root = tones[0].pc;
		for (int i = 0; i < count; i++) {
			if (tones[i].degree == 1) {
				root = tones[i].pc;
				break;
			}
		}
		*rootPc = root;

		// HOW MANY, taken by rank. The ranks were written to be taken in order — the tone that
		// says which quality this is, then the seventh, then the colour, then the root, then the
		// plain fifth — so four tones of a thirteenth chord is what a player would choose rather
		// than the first four in the list.
		const int which = (int) std::round(params[P_TONES].getValue());
		int want = TONES_COUNT[math::clamp(which, 0, NUM_TONES - 1)];
		if (want <= 0 || want > count)
			want = count;

		bool taken[MAX_CHORD_TONES] = {false};
		int n = 0;
		for (int rank = 0; rank <= 5 && n < want; rank++) {
			for (int i = 0; i < count && n < want; i++) {
				if (taken[i] || tones[i].rank != rank)
					continue;
				taken[i] = true;
				offsets[n++] = ((tones[i].pc - root) % 12 + 12) % 12;
			}
		}
		// Anything the ranks did not reach, so a chord is never short of the tones it was asked
		// for merely because a rank was missing.
		for (int i = 0; i < count && n < want; i++) {
			if (!taken[i])
				offsets[n++] = ((tones[i].pc - root) % 12 + 12) % 12;
		}

		// ASCENDING, which is what makes it an arpeggio rather than a list. Chosen by rank and
		// then sorted, so which tones are played and the order they are played in are two
		// separate decisions.
		std::sort(offsets, offsets + n);
		return n;
	}

	/** Which place in the figure the given step lands on. */
	int placeOf(int direction, int step, int n) {
		if (n <= 1)
			return 0;
		switch (direction) {
			case DIR_DOWN:
				return n - 1 - (step % n);
			case DIR_UPDOWN: {
				// Neither end repeated, so the turn sounds like a turn rather than a stumble.
				const int period = 2 * n - 2;
				const int at = ((step % period) + period) % period;
				return (at < n) ? at : period - at;
			}
			case DIR_RANDOM:
				return (int) (dice() * n) % n;
			default:
				return step % n;
		}
	}

	/** What a note in this place is struck at, as a fraction of full scale.

	WRITTEN AS THE LEVEL ITSELF rather than as a deviation from a reference. A deviation has to be
	clamped at both ends, and the clamp was eating the loud end of every rising shape — eleven
	volts and thirteen are both ten — so a third of the control did nothing. Naming the two ends
	and travelling between them cannot do that. */
	float levelFor(int shape, int place, int n) {
		const float across = (n > 1) ? (float) place / (float) (n - 1) : 0.5f;
		const float span = VELOCITY_LOUD - VELOCITY_QUIET;
		switch (shape) {
			case 1: return VELOCITY_QUIET + across * span;
			case 2: return VELOCITY_LOUD - across * span;
			case 3: return VELOCITY_QUIET + dice() * span;
			case 4: return (place == 0) ? VELOCITY_LOUD : VELOCITY_UNDER;
			default: return LEVEL_SAME;
		}
	}

	void startStep(float stepSeconds) {
		int offsets[MAX_CHORD_TONES];
		int rootPc = 0;
		const int tones = gatherSteps(offsets, &rootPc);
		if (tones <= 0)
			return;

		const int octaves = (int) std::round(params[P_OCTAVES].getValue()) + 1;
		const int n = tones * octaves;
		const int direction = (int) std::round(params[P_DIRECTION].getValue());
		const int shape = (int) std::round(params[P_VELOCITY].getValue());
		const float centre = params[P_CENTRE].getValue();
		const float seconds = std::fmax(0.005f, params[P_LENGTH].getValue() * stepSeconds);
		const float bend = params[P_BEND].getValue() / 12.f;

		// THE ROOT SITS IN THE OCTAVE ABOVE THE REGISTER, so that moving Register moves the whole
		// figure by the amount it says and the chord's own root decides where inside that octave
		// the figure begins.
		const float base = (centre + (float) rootPc) / 12.f;

		if (direction == DIR_CHORD) {
			// EVERY TONE AT THE SAME INSTANT, which is the case one note at a time cannot show.
			// The octaves come too, so a chord over three octaves is a real test of how many
			// notes the far end can take at once.
			for (int k = 0; k < n; k++) {
				const int tone = k % tones;
				const int oct = k / tones;
				strike(base + (offsets[tone] + 12 * oct) / 12.f,
					levelFor(shape, tone, tones), seconds, bend);
			}
		}
		else {
			const int place = placeOf(direction, cursor, n);
			const int tone = place % tones;
			const int oct = place / tones;
			strike(base + (offsets[tone] + 12 * oct) / 12.f,
				levelFor(shape, tone, tones), seconds, bend);
		}
		cursor++;
		lightFade = 1.f;
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

		Harmony h;
		const bool have = reader.harmony(h) && h.valid;
		if (have && (!haveEpoch || h.epoch != lastEpoch)) {
			haveEpoch = true;
			lastEpoch = h.epoch;
			backToTheTop(h);
		}
		if (have) {
			// A WHOLE BAR IS THE SLOWEST STEP, and it has to be asked of the harmony rather than
			// written down: the metre can change while the chart plays.
			const int rate = (int) std::round(params[P_RATE].getValue());
			const float stepBeats = (RATE_BEATS[rate] > 0.f)
				? RATE_BEATS[rate] : (float) std::max(1, (int) h.barBeats);
			const double pos = h.beat / (double) stepBeats;
			const int64_t step = (int64_t) std::floor(pos);
			if (step != lastStep) {
				// A jump — the chart rewound, or a step was missed under load — starts from where
				// we are rather than catching up through every step between.
				lastStep = step;
				const float stepSeconds = (beatsPerSecond > 0.01f)
					? stepBeats / beatsPerSecond : 0.25f;
				startStep(stepSeconds);
			}
		}
		else if (lastStep != -1) {
			// The chart stopped. Every note is ended rather than left hanging on a far end that
			// has no way of knowing.
			allOff();
			lastStep = -1;
			cursor = 0;
		}

		tick(args.sampleTime);

		if (lightFade > 0.f)
			lightFade = std::fmax(0.f, lightFade - args.sampleTime * 6.f);
		lights[L_STEP].setBrightness(lightFade);
	}
};


static Layout arpLayout() {
	Layout L;
	L.hp = 14.f;
	L.title = "mpxArp";
	L.titleAbove = "DREAMER DEVELOPMENT";

	static const float NAME_HALF = 1.22f;
	static const float KNOB_EDGE = 4.8f;
	static const float PORT_EDGE = 4.01f;
	static const float GAP = 2.f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			int ticks = 2, const std::vector<std::string>& marks = {}) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
		i.ticks = ticks; i.tickMarks = marks;
		L.items.push_back(i);
		const float edge = marks.empty() ? KNOB_EDGE : 7.1f;
		label(key + ".label", x, y + edge + GAP + NAME_HALF, name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + PORT_EDGE + GAP + 0.98f, name, Panel::CENTRE,
			size > 0.f, size, key);
	};
	auto radio = [&](const std::string& key, float x, float y, int id, const std::string& group,
			const std::vector<std::string>& names, float pitch) {
		label(key + ".group", x, y - GAP - NAME_HALF, group, Panel::LEFT, true, 0.f, key);
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "lamps";
		i.names = names; i.horizontal = false; i.pitch = pitch;
		i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
	};

	// FOURTEEN HP IS 71.12 MM. Every position here is a starting point rather than a considered
	// arrangement — the panel editor is what it is for — but nothing overlaps and the two columns
	// of choices sit clear of each other.
	radio("p.dir", 5.f, 26.f, ArpModule::P_DIRECTION, "DIRECTION",
		{"UP", "DOWN", "UP/DOWN", "RANDOM", "CHORD"}, 5.f);
	radio("p.rate", 40.f, 26.f, ArpModule::P_RATE, "RATE",
		{"1 A BAR", "1 IN 2", "1 A BEAT", "2 A BEAT", "3 A BEAT", "4 A BEAT"}, 5.f);

	radio("p.tones", 5.f, 63.f, ArpModule::P_TONES, "CHORD",
		{"TRIAD", "SEVENTHS", "EXTENSIONS"}, 5.f);
	radio("p.oct", 40.f, 63.f, ArpModule::P_OCTAVES, "OCTAVES", {"1", "2", "3"}, 5.f);

	radio("p.vel", 5.f, 85.f, ArpModule::P_VELOCITY, "VELOCITY",
		{"SAME", "RISING", "FALLING", "RANDOM", "FIRST"}, 4.6f);

	knob("p.centre", 46.f, 88.f, ArpModule::P_CENTRE, "REGISTER", 3);
	// THE MARK IN THE MIDDLE IS WHERE THE NOTES MEET. Below it they are separated, above it they
	// overlap, and the number is the only way to find that point without listening for it.
	knob("p.length", 46.f, 108.f, ArpModule::P_LENGTH, "LENGTH", 3, {"5", "100", "300"});

	knob("p.bend", 24.f, 108.f, ArpModule::P_BEND, "BEND", 3, {"-2", "0", "+2"});

	jack("in.mpx", Item::PORT_IN, 8.f, 118.f, ArpModule::I_MPX, "mpx\nIN", NOTE_CABLE, 7.f);
	jack("out.mpx", Item::PORT_OUT, 63.f, 118.f, ArpModule::O_MPX, "mpx\nOUT", NOTE_CABLE, 7.f);

	Item lamp;
	lamp.key = "lamp.step"; lamp.kind = Item::LIGHT; lamp.id = ArpModule::L_STEP;
	lamp.x = 35.f; lamp.y = 118.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}


struct ArpWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ArpWidget(ArpModule* module) {
		setModule(module);
		layout = arpLayout();
		layoutApplyUser("mpxArp", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxArp");
	}

	void step() override {
		ModuleWidget::step();
		ArpModule* arp = dynamic_cast<ArpModule*>(module);
		if (!arp)
			return;
		// WHICH MPX CABLES ARE PATCHED, resolved here rather than in the audio thread: the engine
		// knows nothing of cables, and the widget is walked once a frame anyway.
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(ArpModule::I_MPX)) {
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
		arp->link(slots, generations, n);

		if (PortWidget* out = getOutput(ArpModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMpxArp = createModel<px::ArpModule, px::ArpWidget>("mpxArp");
