/** mpxPhrase — a rhythm that phrases, read from the chart on the cable.

It places notes against the chart's bars, chord changes, cadences and phrases, and sends them on
one MPX cable with a level and a duration. The pitch is whatever NOTE says, so the cable can go
straight to fromMPX and be heard; an mpxVoice in between replaces the pitch with a melody.

WHY IT EXISTS. mpxEuclid repeats a fixed number of steps and knows nothing of bars, changes or
phrases, so a melody driven by it drifts through the form and never arrives anywhere. This is the
rhythm source a melody can phrase with. See docs/phrase.md for the design and the two corpora the
figures inside it came from.

THE DECIDING IS ELSEWHERE. Phrasing.cpp holds it, with no Rack in it, so it can be measured over
thousands of real phrases from a command line — make phrasetest. This file does what a module
does: reads the cable, watches the transport, hands the controls over, and sends the notes at
their times.

NO CLOCK INPUT. Every onset is placed by the beat the chart publishes, so this cannot drift from
the harmony; it stops, rewinds and loops exactly when the chart does. The tempo is measured from
how fast that beat advances, because a group's length is set in seconds — across a fourfold rise
in tempo, real phrases keep their length in seconds and take more beats.

THE FEEL IS NOT SET HERE. Swing is a control on mpxChart and travels on the cable, because it is
how the beat is divided and everything playing against that chart has to divide it the same way.
What is here is TRIPLETS: the middle unit of the triplet, which swing skips, and which belongs to
a line rather than to the music.

EVERY CONTROL IS BUILT. They were all declared from the start, including the ones that came
later, because Rack saves a parameter by its number, so adding one afterwards would move every
number after it and load a saved patch wrong — and because the panel is arranged once.
*/
#include "plugin.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Phrasing.hpp"
#include "PhraseParams.hpp"
#include "ChartLayout.hpp"
#include "RecordRing.hpp"

#include <ctime>

#include <algorithm>

using namespace px;


/** A pitch shown as a musician writes it, C4 being middle C. The same reading as mpxVoice's, so
the two panels never disagree about what a number means. */
struct PhraseNoteQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
			"F#", "G", "G#", "A", "A#", "B"};
		const int note = (int) std::round(getValue());
		const int pc = ((note % 12) + 12) % 12;
		return std::string(NAMES[pc]) + std::to_string((int) std::floor(note / 12.0) - 1);
	}
};


/** A PAUSE IN HALF BEATS. A breath that ends part-way through a beat leaves the next line to
start at a place nothing in the music marks, so the knob steps by half a beat and shows beats. */
struct HalfBeatQuantity : ParamQuantity {
	void setValue(float value) override {
		ParamQuantity::setValue(std::round(value * 2.f) / 2.f);
	}
	std::string getDisplayValueString() override {
		const float v = std::max(getMinValue(), std::min(getMaxValue(),
			std::round(getValue() * 2.f) / 2.f));
		return string::f(v == std::floor(v) ? "%.0f" : "%.1f", v);
	}
};

/** THE PAUSES AS THE GENERATOR TAKES THEM: in half beats, and no longer than a breath inside a
phrase or a bar between phrases. Applied to the knob's value as well as by the knob, so a patch
saved before the steps and the limits plays by them too. */
static float halfBeats(float v, float most) {
	return std::max(0.f, std::min(most, std::round(v * 2.f) / 2.f));
}


/** ONE LINE OF THE LOG: the settings, a phrase as it is decided, or a note as it is sent. */
struct PhraseRecord {
	enum Kind : uint8_t { SETTINGS, PHRASE, NOTE };
	Kind kind = NOTE;
	double seconds = 0.0;

	// SETTINGS
	float params[PHP_LEN] = {};
	uint32_t seed = 0;
	bool seedAlone = false;
	float beatsPerSecond = 0.f;

	// PHRASE
	int global = 0, position = 0, phrase = 0, phrasesPerPass = 0;
	float beats = 0.f;
	int cadence = 0;
	char section = 0;
	int appearance = 0, inSection = 0;
	int groups = 0;
	float starts[8] = {}, soundsTo[8] = {};
	bool silent = false;
	int restated = 0;

	// NOTE
	int group = 0;
	float offset = 0.f, beat = 0.f, durationBeats = 0.f, level = 0.f;
	uint8_t flags = 0;
	bool triplet = false, legato = false;
};


struct PhraseModule : Module, NoteSource, NoteSink {
	// The parameters are in PhraseParams.hpp, which the preset generator reads too.
	enum InputId { I_CHART, I_DENSITY, INPUTS_LEN };
	enum OutputId { O_MPX, OUTPUTS_LEN };
	enum LightId { L_NOTE, L_PHRASE, LIGHTS_LEN };

	int slot = -1;
	uint32_t generation = 0;
	BusReader chart;

	/** THE RECORDING: what the audio thread has decided, queued for the panel to write. */
	std::atomic<bool> recording{false};
	std::atomic<bool> snapshotWanted{false};
	RecordRing<PhraseRecord> records;
	double recordClock = 0.0;
	float lastRecorded[PHP_LEN] = {};
	uint32_t seedInUse = 0;
	bool seedAloneInUse = false;

	PhraseModule() {
		config(PHP_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// THE DEFAULTS COME FROM THE HEADER, not from numbers typed beside each control: they are
		// the SONG style, and the SONG preset that ships with the plugin is generated from the
		// same place. A module whose defaults were typed here would drift from its own preset.
		float d[PHP_LEN];
		phraseParamDefaults(d);

		// NAMED AS FRACTIONS, because the plate is in a narrow column and these are what every
		// sequencer prints: a quarter, an eighth, a sixteenth, and an eighth triplet.
		configSwitch(PHP_SUBDIVISION, 0.f, (float) (NUM_SUBDIVISIONS - 1), d[PHP_SUBDIVISION],
			"Grid", {"1/4", "1/8", "1/16", "1/8T"});

		// IN SECONDS, NOT BARS: real phrases hold their length in seconds as the tempo changes.
		configParam(PHP_GROUP, 1.f, 8.f, d[PHP_GROUP], "Group", " seconds");
		configParam(PHP_VARY, 0.f, 1.f, d[PHP_VARY], "Vary", "%", 0.f, 100.f);
		configParam(PHP_START, -1.f, 1.f, d[PHP_START], "Start");
		configParam(PHP_ENDING, 0.f, 1.f, d[PHP_ENDING], "Ending");

		// A BREATH, NOT A REST. Two beats at most between the lines of a phrase, a bar between
		// phrases, in half beats: in the pop songs a breath between lines was a beat and a half in
		// the middle and seldom over three, and a longer silence inside a phrase is heard as the
		// line having stopped. The number is the whole of the silence — see Phrasing.cpp.
		configParam<HalfBeatQuantity>(PHP_PAUSE, 0.f, GROUP_PAUSE_MOST,
			halfBeats(d[PHP_PAUSE], GROUP_PAUSE_MOST), "Group pause", " beats");
		configParam<HalfBeatQuantity>(PHP_PHRASE_PAUSE, 0.f, PHRASE_PAUSE_MOST,
			halfBeats(d[PHP_PHRASE_PAUSE], PHRASE_PAUSE_MOST), "Phrase pause", " beats");
		configParam(PHP_HOLD, 0.f, 1.f, d[PHP_HOLD], "Hold", "%", 0.f, 100.f);
		configParam(PHP_SILENT, 0.f, 1.f, d[PHP_SILENT], "Silent phrases", "%", 0.f, 100.f);

		configParam(PHP_DENSITY, 0.f, 1.f, d[PHP_DENSITY], "Density", "%", 0.f, 100.f);
		configParam(PHP_SYNCOPATION, 0.f, 1.f, d[PHP_SYNCOPATION], "Syncopation", "%", 0.f, 100.f);
		configParam(PHP_ONCHANGES, 0.f, 1.f, d[PHP_ONCHANGES], "On changes", "%", 0.f, 100.f);
		configParam(PHP_LENGTH, 0.f, 1.f, d[PHP_LENGTH], "Length", "%", 0.f, 100.f);
		configParam(PHP_DYNAMICS, 0.f, 1.f, d[PHP_DYNAMICS], "Dynamics", "%", 0.f, 100.f);
		configParam(PHP_VARIATION, 0.f, 1.f, d[PHP_VARIATION], "Variation", "%", 0.f, 100.f);

		// THE PITCH THE NOTES LEAVE WITH, for when there is no melody downstream. An mpxVoice
		// replaces it, so this can never interfere with one.
		configParam<PhraseNoteQuantity>(PHP_NOTE, 24.f, 96.f, d[PHP_NOTE], "Default note to send");
		paramQuantities[PHP_NOTE]->snapEnabled = true;

		configParam(PHP_SEED, 0.f, 999.f, d[PHP_SEED], "Variation seed");
		paramQuantities[PHP_SEED]->snapEnabled = true;
		// LOCK SEED, not OWN. The knob beside it is a seed either way; what this says is whether
		// the chart's seed still moves it. Locked, this module's line stays exactly as it is
		// while every other random process in the patch follows the chart.
		configSwitch(PHP_OWN_SEED, 0.f, 1.f, d[PHP_OWN_SEED], "Lock seed",
			{"Follows the chart's seed", "Locked to the seed here"});

		configParam(PHP_REPEAT, 0.f, 1.f, d[PHP_REPEAT], "Repeat", "%", 0.f, 100.f);
		configParam(PHP_CYCLE, 1.f, 16.f, d[PHP_CYCLE], "Cycle", " phrases");
		paramQuantities[PHP_CYCLE]->snapEnabled = true;
		configParam(PHP_SECTIONS, 0.f, 1.f, d[PHP_SECTIONS], "Sections",
			"%", 0.f, 100.f);
		configParam(PHP_ELIDE, 0.f, 1.f, d[PHP_ELIDE], "Elide", "%", 0.f, 100.f);
		configSwitch(PHP_RECORD, 0.f, 1.f, d[PHP_RECORD], "Record notes to disk",
			{"Off", "Recording"});

		// A SHARE OF THE BEATS, NOT AN ABSTRACT AMOUNT, because the corpus gives the number
		// directly: a true triplet event — the middle unit of the triplet, the one swing skips —
		// falls on eight per cent of the beats real solos play. Thirty per cent at the top is
		// beyond anything measured and is there to be overdone deliberately.
		configParam(PHP_TRIPLETS, 0.f, 0.3f, d[PHP_TRIPLETS], "Triplets", "% of beats", 0.f, 100.f);
		configParam(PHP_SHAPE, 0.f, 1.f, d[PHP_SHAPE], "Loudness shape across a phrase", "%", 0.f,
			100.f);
		configParam(PHP_MOTIF, 0.f, 1.f, d[PHP_MOTIF], "Motif: groups restating an earlier group's rhythm",
			"%", 0.f, 100.f);

		configInput(I_CHART, "Chart in — MPX");
		configInput(I_DENSITY, "Density — CV");
		configOutput(O_MPX, "Notes out — MPX");

		slot = busClaim(&generation);
	}

	~PhraseModule() {
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
		return inputId == I_CHART;
	}

	/** A RECORDING NEVER STARTS BY ITSELF. This is the path a patch and a preset both load
	through, so opening either with the button down would begin writing a file unasked. */
	void fromJson(json_t* rootJ) override {
		Module::fromJson(rootJ);
		params[PHP_RECORD].setValue(0.f);
	}

	// ---- what the panel says about the cables --------------------------------------------------

	std::atomic<int> wantSlot{-1};
	std::atomic<uint32_t> wantGeneration{0};
	std::atomic<bool> relink{false};

	void link(int s, uint32_t g) {
		if (s == wantSlot.load() && g == wantGeneration.load())
			return;
		wantSlot.store(s);
		wantGeneration.store(g);
		relink.store(true);
	}

	// ---- the transport, as seen from the cable -------------------------------------------------

	/** Beats a second, measured rather than told: the harmony block carries the beat but not the
	tempo. Smoothed, because the beat advances by a hair each sample and a raw difference is all
	rounding error. */
	float beatsPerSecond = 2.f;
	double lastBeat = -1.0;
	uint32_t lastEpoch = 0;
	int lastPhrase = -1;
	float lastPhraseBeats = 0.f;

	/** The phrase being played, and where in it we are. */
	PhrasePattern pattern;
	/** THE PHRASE BEFORE IT, which REPEAT restates. Kept as it was decided rather than as it was
	played — the pattern carries both — so a phrase restated at another tempo swings by the tempo
	it is at. */
	PhrasePattern previous;
	bool havePrevious = false;
	int nextNote = 0;
	float phraseOffset = 0.f;
	bool havePattern = false;
	/** RECORD on the chart, as last seen, so only a change of it moves this module's switch. */
	bool chartRecordWas = false;
	/** STYLE on the chart, as last seen. The first sighting is only noted: a patch opening is
	not a change, and applying it then would undo knobs set since the style was chosen. */
	int chartStyleWas = -1;

	/** THE NEXT PHRASE, decided before it begins so that its pickup can sound in the breath at
	the end of this one. `upcomingNext` is its next note not yet sent: the pickup notes are sent
	from here, and at the bar line the pattern takes over from wherever they left off. */
	PhrasePattern upcoming;
	bool haveUpcoming = false;
	int upcomingPhrase = -1;
	uint32_t upcomingEpoch = 0;
	int upcomingNext = 0;

	/** A note sent and not yet ended. A line plays one note at a time. */
	int64_t sounding = 0;
	bool isSounding = false;
	int soundingLeft = 0;
	float lamp = 0.f, phraseLamp = 0.f;

	/** Controls as they were when the phrase was last decided, so a change can be noticed. */
	float settingsSeen[PHP_LEN] = {};
	bool settingsDirty = false;

	// ---- the controls -------------------------------------------------------------------------

	PhraseControls controls() {
		PhraseControls c;
		c.subdivision = (int) std::lround(params[PHP_SUBDIVISION].getValue());
		c.groupSeconds = params[PHP_GROUP].getValue();
		c.vary = params[PHP_VARY].getValue();
		c.start = params[PHP_START].getValue();
		c.ending = params[PHP_ENDING].getValue();
		c.pause = halfBeats(params[PHP_PAUSE].getValue(), GROUP_PAUSE_MOST);
		c.phrasePause = halfBeats(params[PHP_PHRASE_PAUSE].getValue(), PHRASE_PAUSE_MOST);
		c.hold = params[PHP_HOLD].getValue();
		c.silentPhrases = params[PHP_SILENT].getValue();
		c.density = params[PHP_DENSITY].getValue();
		if (inputs[I_DENSITY].isConnected())
			c.density = math::clamp(c.density + inputs[I_DENSITY].getVoltage() / 10.f, 0.f, 1.f);
		c.syncopation = params[PHP_SYNCOPATION].getValue();
		c.onChanges = params[PHP_ONCHANGES].getValue();
		c.length = params[PHP_LENGTH].getValue();
		c.dynamics = params[PHP_DYNAMICS].getValue();
		c.variation = params[PHP_VARIATION].getValue();
		c.triplets = params[PHP_TRIPLETS].getValue();
		c.shape = params[PHP_SHAPE].getValue();
		c.motif = params[PHP_MOTIF].getValue();
		c.repeat = params[PHP_REPEAT].getValue();
		c.cycle = (int) std::lround(params[PHP_CYCLE].getValue());
		c.sections = params[PHP_SECTIONS].getValue();
		c.elide = params[PHP_ELIDE].getValue();
		return c;
	}

	/** True when any control has moved since the phrase was decided. */
	bool settingsMoved() {
		bool moved = false;
		for (int i = 0; i < PHP_LEN; i++) {
			const float now = params[i].getValue();
			if (now != settingsSeen[i]) {
				settingsSeen[i] = now;
				moved = true;
			}
		}
		return moved;
	}

	// ---- deciding a phrase --------------------------------------------------------------------

	/** WHICH PHRASE IS BEING DECIDED: the one playing, or the one after it. The same questions
	either way — its length, its ending, its chords, its place in the form — answered from the
	current fields of the harmony block or from its `upcoming`. */
	struct Which {
		float beats = 0.f;
		int cadence = 0;
		const float* changes = NULL;
		int changeCount = 0;
		char section = 0;
		int appearance = 0;
		int inSection = 0;
		int phrase = 0;
		uint32_t epoch = 0;
	};

	static Which current(const Harmony& h, float phraseBeats) {
		Which w;
		w.beats = phraseBeats;
		w.cadence = h.phraseCadence;
		w.changes = h.phraseChangeCount > 0 ? h.phraseChanges : NULL;
		w.changeCount = h.phraseChangeCount;
		w.section = h.section;
		w.appearance = h.sectionAppearance;
		w.inSection = h.phraseInSection;
		w.phrase = h.phrase;
		w.epoch = h.epoch;
		return w;
	}

	static Which next(const Harmony& h) {
		Which w;
		w.beats = h.upcoming.beats;
		w.cadence = h.upcoming.cadence;
		w.changes = h.upcoming.changeCount > 0 ? h.upcoming.changes : NULL;
		w.changeCount = h.upcoming.changeCount;
		w.section = h.upcoming.section;
		w.appearance = h.upcoming.sectionAppearance;
		w.inSection = h.upcoming.phraseInSection;
		w.phrase = h.upcoming.phrase;
		w.epoch = h.upcoming.epoch;
		return w;
	}

	/** Decides the phrase `w` into `out`. `before` is the phrase before it, for REPEAT, or NULL;
	`pickupRoom` is how far before its bar line it may begin. */
	void decideInto(const Harmony& h, const Which& w, const PhrasePattern* before, float pickupRoom,
			PhrasePattern& out) {
		decideInto(h, w, before, pickupRoom, -1, 0.f, out);
	}

	/** And with how it starts already drawn, by the phrase before it: see PhraseAsk::leadKind. */
	void decideInto(const Harmony& h, const Which& w, const PhrasePattern* before, float pickupRoom,
			int leadKind, float leadBeats, PhrasePattern& out) {
		PhraseAsk ask;
		ask.phraseBeats = w.beats;
		ask.barBeats = h.barBeats > 0 ? (float) h.barBeats : 4.f;
		ask.beatsPerSecond = std::max(0.1f, beatsPerSecond);
		// THE FEEL COMES OFF THE CABLE, converted by the chart for the tempo it is running at.
		// Nothing here knows anything about how swing varies with tempo, which is the point of
		// the chart publishing ratios rather than an amount.
		ask.swingEighth = h.swingEighth > 0.f ? h.swingEighth : 1.f;
		ask.swingSixteenth = h.swingSixteenth > 0.f ? h.swingSixteenth : 1.f;
		ask.cadence = w.cadence;
		ask.changes = w.changes;
		ask.changeCount = w.changeCount;
		ask.pickupRoom = pickupRoom;
		ask.leadKind = leadKind;
		ask.leadBeats = leadBeats;
		// WHERE IT SITS IN THE FORM, for its loudness. A contrasting section is any lettered one
		// but A, which is what charts call their first.
		ask.phraseInSection = w.inSection;
		ask.contrasting = w.section != 0 && w.section != 'A';
		ask.intoForm = h.phrasesPerPass > 0 ? (float) w.phrase / (float) h.phrasesPerPass : 0.f;

		// THE SEED. Added to the chart's unless OWN SEED says otherwise, and used alone when the
		// cable carries none — see docs/phrase.md.
		const uint32_t own = (uint32_t) std::lround(params[PHP_SEED].getValue());
		const bool alone = params[PHP_OWN_SEED].getValue() > 0.5f || h.seed == 0;
		ask.seed = alone ? own : h.seed + own;
		seedInUse = ask.seed;
		seedAloneInUse = alone;

		// WHERE THIS PHRASE FALLS since the top of the form, folded by CYCLE: at four, the fifth
		// phrase decides exactly as the first did, which is what makes a line come round. The
		// count runs across passes, so a cycle that does not divide the form keeps moving against
		// it rather than locking to it.
		const PhraseControls c = controls();
		const int global = (int) (w.epoch * std::max<uint32_t>(1, h.phrasesPerPass) + w.phrase);
		const int cycle = std::max(1, c.cycle);
		int position = global % cycle;

		// A RETURNING SECTION PLAYS WHAT IT PLAYED, in proportion to SECTIONS. The decision is
		// keyed on the section letter and the phrase within it and on nothing else, so the second
		// time round draws exactly as the first did — no pattern has to be stored for a section
		// that may not come back for two minutes.
		bool sectionKeyed = false;
		if (c.sections > 0.f && w.section) {
			const uint32_t key = phraseHash(ask.seed * 2246822519u
				+ (uint32_t) w.section * 668265263u + (uint32_t) w.inSection * 374761393u);
			if ((float) (key & 0xffffu) / 65535.f < c.sections) {
				position = (int) (key >> 16) % 4096 + cycle;   // beyond any ordinary cycle position
				sectionKeyed = true;
			}
		}
		ask.cyclePosition = position;

		// REPEAT RESTATES THE PHRASE BEFORE, and the chain restarts at the head of each cycle —
		// otherwise the cycle could not come round, since every phrase would depend on a different
		// predecessor. A section-keyed phrase restates nothing either: what it is restating is its
		// own earlier appearance.
		ask.previous = (before && position != 0 && !sectionKeyed) ? before : NULL;

		phraseGenerate(ask, c, out);
		if (recording.load(std::memory_order_relaxed)) {
			PhraseRecord r;
			r.kind = PhraseRecord::PHRASE;
			r.seconds = recordClock;
			r.global = global;
			r.position = position;
			r.phrase = w.phrase;
			r.phrasesPerPass = h.phrasesPerPass;
			r.beats = w.beats;
			r.cadence = w.cadence;
			r.section = w.section;
			r.appearance = w.appearance;
			r.inSection = w.inSection;
			r.groups = out.groupCount;
			for (int g = 0; g < out.groupCount && g < 8; g++) {
				r.starts[g] = out.starts[g];
				r.soundsTo[g] = out.soundsTo[g];
			}
			r.silent = out.silent;
			r.restated = out.restated;
			records.push(r);
		}
	}

	/** Decides the phrase that is playing, from where the music is in it. */
	void decide(const Harmony& h, float phraseBeats, float offsetInPhrase) {
		decideInto(h, current(h, phraseBeats), havePrevious ? &previous : NULL, 0.f, pattern);
		havePattern = true;
		phraseOffset = offsetInPhrase;
		// SKIP WHAT HAS ALREADY GONE BY. A phrase decided part-way through — because a setting
		// moved, or because the patch was joined mid-phrase — plays from where the music is, not
		// from its beginning.
		nextNote = 0;
		while (nextNote < pattern.noteCount && pattern.notes[nextNote].offset < offsetInPhrase)
			nextNote++;
		settingsMoved();
		settingsDirty = false;
	}

	/** HOW LONG ONE NOTE OUTLASTS THE START OF THE NEXT, when the two are joined. Long enough
	that the next note is always there first and a polyphonic voice hears the two together; short
	enough that nobody hears two pitches at once. */
	static constexpr float LEGATO_OVERLAP = 0.02f;

	/** A NOTE THAT HAS BEEN HANDED OVER, still sounding under the one that took its place. */
	bool tailing = false;
	int64_t tailHandle = 0;
	int tailLeft = 0;

	void endTail() {
		if (!tailing)
			return;
		Event e;
		e.kind = Event::OFF;
		e.handle = tailHandle;
		busPush(slot, e);
		tailing = false;
	}

	void endSounding() {
		if (!isSounding)
			return;
		Event e;
		e.kind = Event::OFF;
		e.handle = sounding;
		busPush(slot, e);
		isSounding = false;
	}

	/** SENDS ONE NOTE of `from`, the phrase playing or the one decided ahead for its pickup. */
	void sendNote(const PhrasePattern& from, int index, uint8_t extraFlags, int phraseForRecord,
			const Harmony& h, const ProcessArgs& args) {
		const PhraseNote& n = from.notes[index];
		// LEGATO: THE NEW NOTE BEGINS BEFORE THE OLD ONE ENDS.
		//
		// Every note used to end the one before it and only then begin, so no two notes ever
		// overlapped by so much as a sample — and a downstream voice that joins overlapping
		// notes into one line, which is what fromMPX's glide mode is for, never once saw a
		// note arrive while another was sounding. Whatever LENGTH said, the line came out
		// detached. So a note still sounding when the next arrives is ended AFTER the new one
		// has begun: the order is the whole of what makes it legato.
		const bool handOver = isSounding;
		const int64_t leaving = sounding;
		const int leftWas = soundingLeft;
		isSounding = false;
		Event e;
		e.kind = Event::ON;
		e.handle = sounding = mintHandle();
		// THE PITCH NOTE SETS, in volts from middle C, so the cable can go straight to fromMPX
		// and be heard. An mpxVoice downstream replaces it.
		e.pitch = (params[PHP_NOTE].getValue() - 60.f) / 12.f;
		e.level = n.level;
		e.duration = n.duration / std::max(0.1f, beatsPerSecond);
		// A NOTE THAT REACHES THE NEXT ONSET IS HELD A LITTLE PAST IT. Its length in beats is
		// exactly the gap, so its end and the next start fell on the same sample, and which
		// arrived first decided whether the line was joined. Fifteen milliseconds past settles
		// it: long enough that the next note is always there first, short enough that nobody
		// hears two notes at once. On the note itself, because fromMPX ends a note when its
		// duration runs out whether or not an OFF has come.
		if (index + 1 < from.noteCount) {
			const PhraseNote& after = from.notes[index + 1];
			if (n.offset + n.duration >= after.offset - 0.01f && after.group == n.group)
				e.duration += LEGATO_OVERLAP;
		}
		e.pan = 0.f;
		e.bendRange = 2.f;
		e.flags = (n.arrival ? Event::ARRIVAL : 0) | (n.groupEnd ? Event::GROUP_END : 0)
			| (n.approach ? Event::APPROACH : 0) | (n.onChange ? Event::ON_CHANGE : 0) | extraFlags;
		e.echo = (uint8_t) std::max(0, std::min(255, n.echo));
		e.along = n.along;
		busPush(slot, e);
		// THE LEAVING NOTE OUTLASTS THE NEW ONE'S START, and ends on its own timer rather
		// than in the same instant. Ending it at once made the two notes touch rather than
		// overlap: a voice with two channels of polyphony saw one note stop exactly as the
		// next began, which is detached playing with no gap, not legato. A real overlap is
		// what lets ANY downstream set-up join the notes — two voices of polyphony, or a
		// mono voice in a legato mode — without this module knowing which it is.
		if (handOver) {
			endTail();
			tailing = true;
			tailHandle = leaving;
			tailLeft = std::max(1, std::min(leftWas, (int) (LEGATO_OVERLAP * args.sampleRate)));
		}
		isSounding = true;
		soundingLeft = std::max(1, (int) std::lround(e.duration * args.sampleRate));
		lamp = 1.f;
		if (recording.load(std::memory_order_relaxed)) {
			PhraseRecord r;
			r.kind = PhraseRecord::NOTE;
			r.seconds = recordClock;
			r.phrase = phraseForRecord;
			r.group = n.group;
			r.offset = n.offset;
			r.beat = (float) h.beat;
			r.durationBeats = n.duration;
			r.level = n.level;
			r.flags = e.flags;
			r.triplet = n.triplet;
			r.legato = handOver;
			records.push(r);
		}
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);

		// THE PEDALS from the chart input go straight through: this module writes rhythms, not
		// pedalling. See NoteBus.hpp.
		if (slot >= 0) {
			float sustain, soft;
			chart.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		// THE RECORDING. A settings line when a take starts and whenever a knob moves, so the log
		// says exactly when something changed rather than leaving it to be inferred.
		const bool rec = params[PHP_RECORD].getValue() > 0.5f;
		recording.store(rec, std::memory_order_relaxed);
		if (rec) {
			recordClock += args.sampleTime;
			bool changed = snapshotWanted.exchange(false);
			for (int i = 0; i < PHP_LEN && !changed; i++)
				changed = params[i].getValue() != lastRecorded[i];
			if (changed) {
				PhraseRecord r;
				r.kind = PhraseRecord::SETTINGS;
				r.seconds = recordClock;
				for (int i = 0; i < PHP_LEN; i++)
					r.params[i] = lastRecorded[i] = params[i].getValue();
				r.seed = seedInUse;
				r.seedAlone = seedAloneInUse;
				r.beatsPerSecond = beatsPerSecond;
				records.push(r);
			}
		}

		if (relink.exchange(false)) {
			chart.clear();
			const int s = wantSlot.load();
			if (s >= 0)
				chart.attach(s, wantGeneration.load());
		}

		Harmony h;
		const bool have = chart.harmony(h) && h.valid;
		// RECORD ON THE CHART starts and stops this module's log too: a change of it is followed,
		// so this module's own switch still works between changes.
		if (have && h.record != chartRecordWas) {
			params[PHP_RECORD].setValue(h.record ? 1.f : 0.f);
			chartRecordWas = h.record;
		}
		// STYLE ON THE CHART: a change of it sets this module's knobs to that style's rhythm.
		if (have && (int) h.style != chartStyleWas) {
			if (chartStyleWas >= 0 && h.style > 0 && h.style < NUM_PHRASE_STYLES) {
				PhraseControls c;
				phraseStyle(h.style, c);
				float v[PHP_LEN];
				for (int i = 0; i < PHP_LEN; i++)
					v[i] = params[i].getValue();
				phraseParamsFrom(c, v);
				for (int i = 0; i < PHP_LEN; i++)
					params[i].setValue(v[i]);
			}
			chartStyleWas = h.style;
		}
		if (!have) {
			endTail();
			// NOTHING IS DRAWN TO SAY SO. Neither lamp flashes and no note leaves, which is what a
			// module with no chart looks like; a notice across the panel said the same thing and
			// took the room a control could use.
			endSounding();
			havePattern = false;
			lastBeat = -1.0;
			lights[L_NOTE].setBrightness(0.f);
			lights[L_PHRASE].setBrightness(0.f);
			return;
		}

		// THE TEMPO, from how fast the beat advances. A rewind or a loop moves the beat backwards,
		// which says nothing about tempo, so those samples are skipped rather than averaged in.
		bool jumped = false;
		if (lastBeat >= 0.0) {
			const double moved = h.beat - lastBeat;
			const float instant = (float) (moved / args.sampleTime);
			if (moved > 0.0 && instant < 50.f) {
				// A PLAUSIBLE ADVANCE, smoothed over about a fortieth of a second.
				beatsPerSecond += (instant - beatsPerSecond) * 0.0005f;
			}
			else if (moved >= 1.0 || moved < 0.0) {
				// A REWIND, A LOOP OR A SECTION CHOSEN: the music is somewhere else.
				jumped = true;
			}
			// ANYTHING BETWEEN — a forward step too large to be one sample of playing and too
			// small to be a move — is the chart snapping its beat into step with an incoming clock
			// pulse. It says nothing about the tempo and it has not moved the music, so it is
			// neither averaged in nor treated as a jump. Averaged in, one such sample would
			// outweigh hundreds of ordinary ones, because what is averaged is the advance divided
			// by a single sample's time.
		}
		lastBeat = h.beat;

		// WHERE WE ARE IN THE PHRASE, from the countdown the chart publishes.
		//
		// AND WHAT TO DO WITHOUT ONE. A chart that publishes no phrasing sends nought for both,
		// which would leave this module at the beginning of a phrase of no length for ever. Four
		// bars is the same fallback the chart itself uses for a stretch it cannot phrase, and it
		// is measured off the beat rather than taken from the block.
		float phraseBeats = h.phraseBeats;
		float offset = std::max(0.f, h.phraseBeats - h.beatsToPhraseEnd);
		int phraseIndex = (int) h.phrase;
		if (!(phraseBeats > 0.f)) {
			phraseBeats = 4.f * (h.barBeats > 0 ? (float) h.barBeats : 4.f);
			const double passed = std::max(0.0, h.beat);
			phraseIndex = (int) (passed / phraseBeats);
			offset = (float) (passed - (double) phraseIndex * phraseBeats);
		}

		// A NEW PHRASE, OR A NEW PLACE IN THE MUSIC. Either way the pattern is decided again from
		// where the music actually is, rather than carried on from where it was — unless the
		// phrase was decided ahead, for its pickup, in which case it is already under way.
		const bool newPhrase = phraseIndex != lastPhrase || h.epoch != lastEpoch
			|| phraseBeats != lastPhraseBeats;
		if (jumped)
			haveUpcoming = false;
		if (newPhrase || jumped || !havePattern) {
			const bool ahead = newPhrase && !jumped && haveUpcoming
				&& upcomingPhrase == phraseIndex && upcomingEpoch == h.epoch;
			// A PICKUP STILL SOUNDING IS NOT CUT at the bar line: it is the start of this phrase,
			// and its next note takes over from it as any note in a line does.
			if ((newPhrase || jumped) && !ahead) {
				endSounding();
				endTail();
			}
			// A REWIND, A LOOP OR A SECTION CHANGE BREAKS THE REPEAT CHAIN. What a phrase restates
			// is the phrase immediately before it; after a jump there is no such phrase, and
			// restating the one from before the jump would carry a fragment of the last take into
			// this one.
			if (jumped)
				havePrevious = false;
			// THE PHRASE THAT IS ENDING BECOMES THE ONE TO RESTATE — but only at a phrase
			// boundary. A phrase decided again mid-way because a knob moved must not become its
			// own predecessor.
			if (newPhrase && havePattern) {
				previous = pattern;
				havePrevious = true;
			}
			lastPhrase = phraseIndex;
			lastEpoch = h.epoch;
			lastPhraseBeats = phraseBeats;
			if (ahead) {
				pattern = upcoming;
				havePattern = true;
				nextNote = upcomingNext;
				while (nextNote < pattern.noteCount && pattern.notes[nextNote].offset < -1e-4f)
					nextNote++;
				settingsMoved();
				settingsDirty = false;
			}
			else {
				decide(h, phraseBeats, offset);
			}
			haveUpcoming = false;
			if (newPhrase)
				phraseLamp = 1.f;
		}
		else if (settingsDirty && pattern.groupCount > 0) {
			// A SETTING MOVED. It takes effect at the start of the next group: waiting for the next
			// phrase would make a knob feel dead for bars at a time, and acting at once would cut a
			// breath group in half.
			for (int g = 0; g < pattern.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
				if (offset >= pattern.starts[g] - 0.001f && phraseOffset < pattern.starts[g] - 0.001f) {
					decide(h, phraseBeats, offset);
					break;
				}
			}
		}
		if (settingsMoved())
			settingsDirty = true;

		phraseOffset = offset;

		// THE NEXT PHRASE, DECIDED AHEAD, so it can begin before its bar line. Once a phrase is
		// within the longest pickup of its end, the one after it is decided, with as much room
		// for a pickup as this one has left silent at its end. Only where the chart says what
		// comes next: an unphrased chart has no next phrase to describe.
		if (!haveUpcoming && havePattern && h.upcoming.valid && h.phraseBeats > 0.f
				&& h.beatsToPhraseEnd <= PICKUP_MAX_BEATS + 1e-3f) {
			const float room = phrasePickupRoom(pattern, phraseBeats,
				(int) std::lround(params[PHP_SUBDIVISION].getValue()));
			decideInto(h, next(h), &pattern, room, pattern.nextLead, pattern.nextLeadBeats, upcoming);
			haveUpcoming = true;
			upcomingPhrase = h.upcoming.phrase;
			upcomingEpoch = h.upcoming.epoch;
			upcomingNext = 0;
		}

		// THE NOTES, SENT AT THEIR TIMES — and none while the chart holds, stopped or waiting for
		// its clock: the notes on the beat it shows belong to the moment it starts.
		while (!h.holding && nextNote < pattern.noteCount
				&& pattern.notes[nextNote].offset <= offset + 1e-4f) {
			const int index = nextNote++;
			sendNote(pattern, index, 0, lastPhrase, h, args);
		}
		// AND THE NEXT PHRASE'S PICKUP, in this one's breath, counted back from its bar line.
		if (haveUpcoming && !h.holding) {
			while (upcomingNext < upcoming.noteCount && upcoming.notes[upcomingNext].offset < -1e-4f
					&& offset >= phraseBeats + upcoming.notes[upcomingNext].offset - 1e-4f) {
				const int index = upcomingNext++;
				sendNote(upcoming, index, Event::PICKUP, upcomingPhrase, h, args);
			}
		}
		if (isSounding && --soundingLeft <= 0)
			endSounding();
		if (tailing && --tailLeft <= 0)
			endTail();

		lamp = std::max(0.f, lamp - args.sampleTime * 6.f);
		phraseLamp = std::max(0.f, phraseLamp - args.sampleTime * 2.f);
		lights[L_NOTE].setBrightness(lamp);
		lights[L_PHRASE].setBrightness(phraseLamp);
	}
};


// ---- the panel ------------------------------------------------------------------------------
//
// EVERY CONTROL PRESENT AND NAMED, in bands by what it decides, as a starting arrangement for the
// panel editor. Nothing here is a considered position: the editor is what that is for.


/** THE KNOBS ARE THE LARGE SIZE, 12.19 mm across rather than 9.6, and every measurement that
depends on that follows from this one number rather than being written out again.

A name sits clear of the knob's edge, or clear of the numbers round it where there are any: the
marks are drawn 2.6 mm past the metal and a mark is about 0.7 mm from its middle to its edge. */
static const float KNOB_MM = 12.19f;
static const float PORT_EDGE = 4.01f;
static const float GAP = 2.f;

static void pLabel(Layout& L, const std::string& key, float x, float y, const std::string& text,
		Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
		const std::string& owner = "") {
	Item i;
	i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
	i.align = align; i.heading = heading; i.size = size; i.owner = owner;
	L.items.push_back(i);
}

/** HOW WIDE A MARK ROUND A KNOB MAY BE, and why these are set smaller than the default.

A three-mark scale draws its outer two at 0.83 of a half turn each way, so their centres are only
2 * sin(149.4 degrees) * 7.4 mm apart — 7.5 mm, measured from the same numbers the panel draws
with. At the default six points a capital advances about 1.22 mm, so "PICKUP" beside "OFF BEAT"
needs 8.5 mm and the two words touch. At 5.4 a capital advances 1.1 mm, which buys back the
millimetre; the words below are then short enough to leave 1.5 mm between the closest pair. */
static const float MARK_SIZE = 6.5f;

/** ONE SIZE FOR EVERY NAME ON THE PANEL, and one larger size for the four headings.

A panel whose names are set at four sizes reads as four kinds of thing, and they are all the same
kind of thing: the name of a control. So a knob, a plate, a button, a lamp and a jack are all
named at NAME_SIZE, and a name too wide for its column is broken over two lines rather than set
smaller.

THE HEADINGS ARE LARGER THAN WHAT THEY HEAD, which they were not: at 6.5 against the names' 7.5 a
heading read as a caption on the knob below it. Eight is as large as REPETITION can be set before
it reaches past its column.

THE MARKS ROUND A KNOB ARE THE ONE EXCEPTION, and they have to be. The two outer marks of a
three-mark scale sit 8.85 mm apart on a knob this size, which is what a pair of words has to fit
between; at the name size the widest pair on this panel would need 8.4 mm and touch. They are
also not names — they are the ends of a sweep. */
static const float NAME_SIZE = 7.5f;
static const float HEAD_SIZE = 8.f;

/** WHERE A KNOB'S NAME SITS, worked out from what is actually drawn below the knob rather than
from a fixed offset.

WHAT IS BELOW A KNOB is not its edge. It is the scale: the ticks reach 1 mm past a radius 0.3 mm
outside the metal, and where there are words round the knob they are centred 2.6 mm outside it.
The lowest point of either is at the outer marks, which sit at 0.83 of a half turn from the top —
so 0.861 of the scale's radius below the centre.

AND A NAME IS SET ABOUT ITS MIDDLE, growing in both directions as lines are added. A second line
therefore pushes the first one UP, into the knob: that is why the two-line names looked cramped
against their knobs while the one-line names did not. So the name is placed by where its TOP line
must fall, and the point handed to the panel is worked back from that.

The result is that every name on the panel begins a millimetre below whatever the knob draws
under itself, whether it is one line or two, marked or plain. */
static const float NAME_GAP = 1.f;

static float knobNameY(float y, bool marked, const std::string& name) {
	// The lowest thing the scale draws, measured from the knob's centre.
	const float scaleR = marked ? KNOB_MM / 2.f + 2.6f          // the words are centred here
		: KNOB_MM / 2.f + 0.3f + 1.f;                            // a tick's outer end
	const float halfMark = marked ? FIGURE_CAP * MARK_SIZE / (2.f * 2.952756f) : 0.f;
	const float below = 0.861f * scaleR + halfMark;

	// The name's own metrics: half a capital's height, and the step between lines.
	const float halfCap = FIGURE_CAP * NAME_SIZE / (2.f * 2.952756f);
	const float step = panelLineStep(NAME_SIZE) / 2.952756f;
	int lines = 1;
	for (size_t i = 0; i < name.size(); i++)
		if (name[i] == '\n')
			lines++;

	// Where the top line must fall, worked back to the point the panel is given.
	return y + below + NAME_GAP + halfCap + step * (float) (lines - 1) / 2.f;
}

/** `nameDy` overrides the computed distance where the panel editor settled on another one. The
computation is what an unadjusted knob gets; these are the places a hand did better. */
static void pKnob(Layout& L, const std::string& key, float x, float y, int id,
		const std::string& name, const std::vector<std::string>& marks = {},
		float nameDy = 0.f) {
	Item i;
	i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
	i.diameter = KNOB_MM;
	i.ticks = 3; i.tickMarks = marks;
	if (!marks.empty())
		i.nameSize = MARK_SIZE;
	L.items.push_back(i);
	pLabel(L, key + ".label", x, nameDy > 0.f ? y + nameDy : knobNameY(y, !marks.empty(), name),
		name, Panel::CENTRE, true, NAME_SIZE, key);
}

static void pPlate(Layout& L, const std::string& key, float x, float y, int id, int chars,
		const std::string& name) {
	Item i;
	i.key = key; i.kind = Item::PARAM; i.id = id;
	i.x = x; i.y = y; i.style = "readout"; i.chars = chars; i.h = 3.4f;
	L.items.push_back(i);
	pLabel(L, key + ".label", x, y - 5.22f, name, Panel::CENTRE, true, NAME_SIZE, key);
}

static void pJack(Layout& L, const std::string& key, Item::Kind kind, float x, float y, int id,
		const std::string& name, NVGcolor colour) {
	Item i;
	i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = colour;
	L.items.push_back(i);
	pLabel(L, key + ".label", x, y - PORT_EDGE - GAP - 0.98f, name, Panel::CENTRE, true, NAME_SIZE,
		key);
}

static void pRule(Layout& L, const std::string& key, float x, float y, float len, bool across) {
	Item i;
	i.key = key; i.kind = Item::RULE; i.x = x; i.y = y; i.horizontal = across;
	if (across) i.w = len; else i.h = len;
	L.items.push_back(i);
}

/** THE PANEL, IN VERTICAL BANDS READ LEFT TO RIGHT, in the order the module works in: the phrase
is divided into groups, the pauses between them are set, the groups are filled with notes, and
what happens from one phrase to the next comes last.

WHY COLUMNS RATHER THAN ROWS. There are twenty-four controls, and a rack row is tall: six knobs
fit in a column with room for a two-line name under each, where a row of six needs the panel to
be wide. Columns also let the reading order be the working order, which a grid of rows cannot
say — nothing about a row above another row means it happens first.

THE LEFT COLUMN IS EVERYTHING THAT IS NOT A KNOB: the plates and the button that were across the
top, and every jack, including the output. It is narrow, so the four bands of knobs sit
together and the panel reads as knobs with a service column beside them rather than as a field
of controls.

ARRANGED ROUGHLY, ON PURPOSE. Every position here is arithmetic — even pitches, even columns —
and the panel editor is what turns that into something that reads properly. What matters at this
stage is that every control is present, named, and in the right band. */
/** THE PANEL, AT THE POSITIONS ARRIVED AT IN THE PANEL EDITOR and folded back in here so that
they are what the module ships with.

FOUR BANDS READ LEFT TO RIGHT, in the order the module works in and the order somebody thinks in:
what happens across phrases, how one phrase divides, the pauses inside it, and the notes inside a
group. A rack row is tall, so a column holds six knobs with room for a two-line name under each
where a row of six would need a much wider panel — and a column can say "these belong together"
in a way that rows above rows cannot.

THE LEFT COLUMN IS EVERYTHING THAT IS NOT A KNOB, and the two things that govern every draw in
the module rather than any one span of music: the grid, the seed, whether the seed is locked, how
far the draws may depart from the likeliest, the default pitch, and the two inputs.

RECORD IS NOT ON THE PANEL. It is in the right-click menu: it is set once when a file is wanted
and never touched again. The parameter still exists and keeps its number, because that is what a
patch and a preset save it by. */
static Layout phraseLayout() {
	Layout L;
	// TWENTY-TWO HP. Five knob columns at 19 mm and the service column beside them. It was
	// nineteen HP with four columns, one of which held six knobs — which is what stopped the
	// knobs being any larger, since six rows of a large knob with a two-line name under it do not
	// fit a rack row.
	L.hp = 22.f;
	L.title = "mpxPhrase";

	const float colX[5] = {26.5f, 45.5f, 64.5f, 83.5f, 102.4f};
	// THE FIRST RULE SITS FURTHER RIGHT than the others are apart, because the service column
	// carries the three longest names on the panel — VARIATION, VARIATION SEED and LOCK SEED —
	// and at the panel's one name size they need 13.7 mm.
	const float ruleX[5] = {17.f, 35.5f, 55.f, 74.f, 93.f};
	// FIVE POSITIONS, of which the fullest column uses all five, and 21.5 mm apart. A large knob
	// with numbers round it and a two-line name under it reaches 15 mm below its centre, and the
	// knob beneath begins 6.1 mm above its own, so anything closer than 21.1 has a name touching
	// metal.
	const float rowY[5] = {24.f, 45.5f, 67.f, 88.5f, 110.f};
	// THE HEADINGS SIT JUST ABOVE THE CROSSBAR rather than up against the title. A heading of one
	// line reaches about 0.95 mm below the point it is placed at; one of two lines reaches 2.22,
	// because a name grows in both directions as lines are added. So the two are placed at
	// different points to leave the same 1.2 mm above the line at 15.5.
	const float headY = 13.35f;
	const float headY2 = 12.1f;

	for (int c = 0; c < 5; c++)
		pRule(L, "p.col" + std::to_string(c), ruleX[c], 8.f, 118.f, false);

	// A LINE UNDER THE HEADINGS, across the whole panel. It runs on over the service column,
	// which has no heading of its own: a line stopping short of the left edge read as a mistake,
	// and carried all the way it says the same thing about everything below it — that what is
	// above the line names what is under it.
	pRule(L, "p.headrule", 3.5f, 15.5f, 104.5f, true);

	// ---- 1. from one phrase to the next -------------------------------------------------------
	//
	// BY WIDENING SPAN: how much one phrase repeats another, over how many phrases that comes
	// round, and how much a returning section returns to what it played before.
	//
	// "ACROSS PHRASES" RATHER THAN "REPETITION", on two lines. The word is sixteen millimetres
	// long at the heading size and the column is nineteen, so it reached past its own rule — and
	// the band holds more than repetition: a cycle length and a section setting are about how
	// phrases relate across the form.
	pLabel(L, "p.head.repeat", colX[0], headY2, "ACROSS\nPHRASES", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.repeat", colX[0], rowY[0], PHP_REPEAT, "REPEAT", {}, 9.32f);
	pPlate(L, "p.cycle", colX[0], rowY[1], PHP_CYCLE, 2, "PHRASES\nRECYCLE");
	pKnob(L, "p.sections", colX[0], rowY[2], PHP_SECTIONS, "SECTIONS");
	// THE LOUDNESS SHAPE, with the controls that work across a whole phrase. DYNAMICS, under EACH
	// NOTE, is how deep it goes.
	pKnob(L, "p.shape", colX[0], rowY[3], PHP_SHAPE, "LOUDNESS\nSHAPE", {"FALL", "", "ARCH"});

	// ---- 2. how the phrase is divided ---------------------------------------------------------
	//
	// HOW LONG, HOW ALIKE, HOW IT BEGINS, HOW IT ENDS.
	pLabel(L, "p.head.groups", colX[1], headY, "GROUPS", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.group", colX[1], rowY[0], PHP_GROUP, "GROUP\nLENGTH", {"1", "", "8"}, 10.f);
	pKnob(L, "p.vary", colX[1], rowY[1], PHP_VARY, "LENGTH\nVARIATION", {}, 9.f);
	pKnob(L, "p.start", colX[1], rowY[2], PHP_START, "START", {"PICKUP", "MIXED", "AFTER"}, 10.f);
	pKnob(L, "p.ending", colX[1], rowY[3], PHP_ENDING, "ENDING", {"WEAK", "", "STRONG"});
	// A GROUP RESTATING ANOTHER'S RHYTHM, so it is the groups' business.
	pKnob(L, "p.motif", colX[1], rowY[4], PHP_MOTIF, "MOTIF");

	// ---- 3. the pauses -------------------------------------------------------------------------
	//
	// THE THREE LENGTHS OF PAUSE, SHORTEST FIRST, then the two controls that act on them. A
	// dropped phrase is a pause the length of a phrase, so it belongs here rather than among the
	// group controls: the three are one scale — between groups, between phrases, and instead of
	// a phrase — and somebody wanting more space in the music finds all of it in one column.
	pLabel(L, "p.head.pauses", colX[2], headY, "PAUSES", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.pause", colX[2], rowY[0], PHP_PAUSE, "GROUP\nPAUSE", {"0", "1", "2"}, 10.f);
	pKnob(L, "p.phrasepause", colX[2], rowY[1], PHP_PHRASE_PAUSE, "PHRASE\nPAUSE",
		{"0", "2", "4"}, 10.f);
	pKnob(L, "p.silent", colX[2], rowY[2], PHP_SILENT, "SILENT\nPHRASES");
	pKnob(L, "p.hold", colX[2], rowY[3], PHP_HOLD, "HOLD", {"SILENT", "", "HELD"});
	pKnob(L, "p.elide", colX[2], rowY[4], PHP_ELIDE, "ELIDE");

	// ---- 4. which slots sound ------------------------------------------------------------------
	//
	// ALL FOUR DECIDE WHERE THE NOTES FALL. Density says how many of the grid's slots sound,
	// syncopation moves them off the strong slots, on changes pulls them onto a chord change, and
	// triplets adds the middle unit of the beat.
	pLabel(L, "p.head.placing", colX[3], headY, "PLACING", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.density", colX[3], rowY[0], PHP_DENSITY, "DENSITY", {}, 9.f);
	pKnob(L, "p.sync", colX[3], rowY[1], PHP_SYNCOPATION, "SYNCO-\nPATION");
	pKnob(L, "p.onchanges", colX[3], rowY[2], PHP_ONCHANGES, "ON\nCHANGES");
	pKnob(L, "p.triplets", colX[3], rowY[3], PHP_TRIPLETS, "TRIPLETS");

	// ---- 5. what each note is like ---------------------------------------------------------
	//
	// NEITHER OF THESE SAYS WHERE A NOTE FALLS. They describe a note that already has its place:
	// how long it lasts and how hard it is played. That is the seam the notes band was split on.
	pLabel(L, "p.head.each", colX[4], headY, "EACH NOTE", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.length", colX[4], rowY[0], PHP_LENGTH, "LENGTH", {}, 9.32f);
	pKnob(L, "p.dynamics", colX[4], rowY[1], PHP_DYNAMICS, "DYNAMICS");

	// ---- the service column -------------------------------------------------------------------
	const float sx = 9.f;

	// THE GRID AS A FRACTION, not a word. "Eighth triplets" is fifteen characters and a plate
	// wide enough for it is three quarters of this column; the fractions are what every sequencer
	// prints and what a musician reads without stopping.
	// JUST UNDER THE LINE, since this column has no heading above it: its name is the first thing
	// below the rule, a millimetre clear of it.
	pPlate(L, "p.sub", sx, 27.f, PHP_SUBDIVISION, 4, "NOTE\nGRID");
	L.items.back().y = 21.f;

	// THE SEED, WHAT IT SEEDS, AND HOW FAR IT MAY GO. The seed picks which line you get and
	// VARIATION sets how far from the likeliest that line may stray, so they are one group and
	// the seed is named for what it seeds. LOCK SEED between them says whether the chart's own
	// seed still moves this one.
	pPlate(L, "p.seed", sx, 41.82f, PHP_SEED, 3, "VARIATION\nSEED");
	L.items.back().y = 36.5f;
	{
		Item i;
		i.key = "p.own"; i.kind = Item::PARAM; i.id = PHP_OWN_SEED;
		i.x = sx; i.y = 50.f; i.style = "latch"; i.diameter = 6.6f;
		L.items.push_back(i);
		pLabel(L, "p.own.label", sx, 54.5f, "LOCK SEED", Panel::CENTRE, true, NAME_SIZE, "p.own");
	}
	pKnob(L, "p.variation", sx, rowY[2], PHP_VARIATION, "VARIATION\nAMOUNT", {}, 9.5f);

	// ONE-LINE NAMES, BELOW THE JACKS. A jack reaches 4.01 mm below its centre and a name set at
	// 5.4 stands about 1.4 mm tall about its own middle, so 6.7 mm below the centre clears the
	// jack by 2 mm. Two-line names do not fit: the second line lands on what is beneath.
	pJack(L, "p.in.density", Item::PORT_IN, sx, 100.f, PhraseModule::I_DENSITY, "DENSITY\nCV",
		SIG_CV);
	L.items.back().y = 107.5f;

	// THE CHART AT THE FOOT, level with the jack the notes leave by, so the two magenta rings
	// read as the two ends of one path across the panel.
	pJack(L, "p.in.chart", Item::PORT_IN, sx, 115.f, PhraseModule::I_CHART, "CHART IN",
		NOTE_CABLE);
	L.items.back().y = 122.f;

	// ---- what is happening, and what leaves ---------------------------------------------------
	//
	// THE LAMPS SAY WHEN SOMETHING BEGINS: a note, and a phrase. One above the other on the
	// column's centre line, with the jack they report on below them — what leaves there is the
	// note the upper lamp flashes for.
	{
		Item i;
		i.key = "p.lamp"; i.kind = Item::LIGHT; i.id = PhraseModule::L_NOTE;
		i.x = colX[4]; i.y = 75.5f;
		L.items.push_back(i);
		pLabel(L, "p.lamp.label", colX[4], 79.5f, "BEGIN\nNOTE", Panel::CENTRE, true, NAME_SIZE,
			"p.lamp");
	}
	{
		Item i;
		i.key = "p.phraselamp"; i.kind = Item::LIGHT; i.id = PhraseModule::L_PHRASE;
		i.x = colX[4]; i.y = 86.5f;
		L.items.push_back(i);
		pLabel(L, "p.phraselamp.label", colX[4], 90.5f, "BEGIN\nPHRASE", Panel::CENTRE, true,
			NAME_SIZE, "p.phraselamp");
	}
	// TWO LINES: at the panel's name size "NOTES OUT" is 13.7 mm and this column, narrowed by the
	// service column beside it, has 16.5.
	// THE PITCH THE NOTES LEAVE WITH, beside the jack they leave by: it matters only when nothing
	// downstream is choosing a note, and then this is what is heard. Named for what it does
	// rather than for what it is — a plate reading "NOTE" on a module that generates notes says
	// nothing.
	pPlate(L, "p.note", colX[4], 105.5f, PHP_NOTE, 3, "DEFAULT\nNOTE");
	L.items.back().y = 99.5f;

	pJack(L, "p.out", Item::PORT_OUT, colX[4], 115.f, PhraseModule::O_MPX, "NOTES\nOUT",
		NOTE_CABLE);
	L.items.back().y = 122.5f;

	L.bindOffsets();
	return L;
}


static const char* PARAM_KEYS[PHP_LEN] = {"grid", "groupSeconds", "varyLength", "start",
	"ending", "groupPause", "phrasePause", "hold", "silentPhrases", "density", "syncopation",
	"onChanges", "length", "dynamics", "variation", "note", "seed", "lockSeed", "repeat", "cycle",
	"sections", "elide", "record", "triplets"};

/** ONE LINE OF JSON FOR ONE RECORD. Named fields rather than positions, so the log can be read
by eye and by anything else without a key to it. */
static std::string phraseRecordLine(const PhraseRecord& r) {
	char buf[1024];
	std::string out;
	switch (r.kind) {
		case PhraseRecord::SETTINGS: {
			std::snprintf(buf, sizeof(buf), "{\"t\":%.3f,\"settings\":{", r.seconds);
			out = buf;
			for (int i = 0; i < PHP_LEN; i++) {
				std::snprintf(buf, sizeof(buf), "%s\"%s\":%g", i ? "," : "", PARAM_KEYS[i],
					r.params[i]);
				out += buf;
			}
			std::snprintf(buf, sizeof(buf), "},\"seedInUse\":%u,\"seedFrom\":\"%s\","
				"\"beatsPerMinute\":%.1f}", (unsigned) r.seed, r.seedAlone ? "this module" :
				"the chart plus this module", r.beatsPerSecond * 60.f);
			out += buf;
			return out;
		}
		case PhraseRecord::PHRASE: {
			std::snprintf(buf, sizeof(buf), "{\"t\":%.3f,\"phrase\":%d,\"ofPass\":%d,"
				"\"sinceTop\":%d,\"inCycle\":%d,\"beats\":%.2f,\"cadence\":\"%s\","
				"\"section\":\"%c\",\"appearance\":%d,\"phraseInSection\":%d,"
				"\"silent\":%s,\"restatedSlots\":%d,\"groups\":[",
				r.seconds, r.phrase + 1, r.phrasesPerPass, r.global + 1, r.position + 1, r.beats,
				chartCadenceName(r.cadence), r.section ? r.section : '-', r.appearance,
				r.inSection + 1, r.silent ? "true" : "false", r.restated);
			out = buf;
			for (int g = 0; g < r.groups && g < 8; g++) {
				std::snprintf(buf, sizeof(buf), "%s{\"from\":%.2f,\"soundsTo\":%.2f}",
					g ? "," : "", r.starts[g], r.soundsTo[g]);
				out += buf;
			}
			out += "]}";
			return out;
		}
		case PhraseRecord::NOTE:
		default: {
			std::snprintf(buf, sizeof(buf), "{\"t\":%.3f,\"note\":{\"phrase\":%d,"
				"\"group\":%d,\"at\":%.3f,\"beat\":%.3f,\"lengthBeats\":%.3f,"
				"\"level\":%.2f,\"arrival\":%s,\"approach\":%s,\"groupEnd\":%s,"
				"\"triplet\":%s,\"legato\":%s}}",
				r.seconds, r.phrase + 1, r.group + 1, r.offset, r.beat, r.durationBeats, r.level,
				(r.flags & Event::ARRIVAL) ? "true" : "false",
				(r.flags & Event::APPROACH) ? "true" : "false",
				(r.flags & Event::GROUP_END) ? "true" : "false",
				r.triplet ? "true" : "false", r.legato ? "true" : "false");
			return buf;
		}
	}
}


struct PhraseWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	/** THE OPEN LOG while recording is on: a new file each time it starts, named by the moment,
	in the same folder as mpxMelody's, so a take is never appended to the one before. */
	FILE* log = NULL;
	std::string logPath;
	uint32_t droppedSeen = 0;

	~PhraseWidget() {
		if (log)
			std::fclose(log);
	}

	void writeLog(PhraseModule* m) {
		const bool on = m->recording.load();
		if (on && !log) {
			const std::string folder = asset::user("DreamerMPX");
			system::createDirectories(folder);
			char stamp[32];
			std::time_t now = std::time(NULL);
			std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
			logPath = folder + "/phrase-log-" + stamp + ".jsonl";
			log = std::fopen(logPath.c_str(), "w");
			// Whatever was queued before the file existed belongs to no take.
			PhraseRecord discard;
			while (m->records.pop(discard)) {}
			m->recordClock = 0.0;
			droppedSeen = m->records.dropped.load();
			m->snapshotWanted.store(true);
		}
		else if (!on && log) {
			std::fclose(log);
			log = NULL;
		}
		if (!log)
			return;
		PhraseRecord r;
		while (m->records.pop(r)) {
			const std::string line = phraseRecordLine(r);
			std::fwrite(line.data(), 1, line.size(), log);
			std::fputc('\n', log);
		}
		const uint32_t dropped = m->records.dropped.load();
		if (dropped != droppedSeen) {
			std::fprintf(log, "{\"dropped\":%u}\n", (unsigned) (dropped - droppedSeen));
			droppedSeen = dropped;
		}
		// FLUSHED EVERY FRAME, so the file on disk is always what has been decided so far: the
		// point of it is to be read while the patch is still running.
		std::fflush(log);
	}

	PhraseWidget(PhraseModule* module) {
		setModule(module);
		layout = phraseLayout();
		layoutApplyUser("mpxPhrase", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPhrase");
		PhraseModule* m = dynamic_cast<PhraseModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Record notes to disk", "",
			[=]() { return m->params[PHP_RECORD].getValue() > 0.5f; },
			[=]() {
				Param& p = m->params[PHP_RECORD];
				p.setValue(p.getValue() > 0.5f ? 0.f : 1.f);
			}));
		// WHERE IT WENT, so the file can be found without anybody having to know the folder.
		menu->addChild(createMenuLabel(logPath.empty()
			? "writes to " + asset::user("DreamerMPX") : logPath));
	}

	void step() override {
		ModuleWidget::step();
		PhraseModule* m = dynamic_cast<PhraseModule*>(module);
		if (!m)
			return;
		writeLog(m);
		// WHICH MPX CABLE IS PATCHED, resolved here: the engine knows nothing of cables.
		int slot = -1;
		uint32_t generation = 0;
		if (PortWidget* port = getInput(PhraseModule::I_CHART)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int s = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (s < 0)
					continue;
				cw->color = NOTE_CABLE;
				slot = s;
				generation = g;
				break;
			}
		}
		m->link(slot, generation);
		if (PortWidget* out = getOutput(PhraseModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}

};


Model* modelMpxPhrase = createModel<PhraseModule, PhraseWidget>("mpxPhrase");
