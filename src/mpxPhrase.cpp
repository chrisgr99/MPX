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

WHAT IS NOT BUILT YET. REPEAT, CYCLE, SECTIONS and ELIDE are on the panel and do nothing; they
are milestone five. TRIPLETS is milestone 4a, the feel, and RECORD is milestone seven. They are
declared now because Rack saves a parameter by its number, so adding them later would move every
number after them and load a saved patch wrong — and because the panel is arranged once.
*/
#include "plugin.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Phrasing.hpp"
#include "PhraseParams.hpp"
#include "ChartLayout.hpp"

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


struct PhraseModule : Module, NoteSource, NoteSink {
	// The parameters are in PhraseParams.hpp, which the preset generator reads too.
	enum InputId { I_CHART, I_DENSITY, INPUTS_LEN };
	enum OutputId { O_MPX, OUTPUTS_LEN };
	enum LightId { L_NOTE, L_PHRASE, LIGHTS_LEN };

	int slot = -1;
	uint32_t generation = 0;
	BusReader chart;

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

		configParam(PHP_PAUSE, 0.f, 8.f, d[PHP_PAUSE], "Pause", " beats");
		configParam(PHP_PHRASE_PAUSE, 0.f, 8.f, d[PHP_PHRASE_PAUSE], "Phrase pause", " beats");
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

		configParam(PHP_REPEAT, 0.f, 1.f, d[PHP_REPEAT], "Repeat (not built yet)", "%", 0.f, 100.f);
		configParam(PHP_CYCLE, 1.f, 16.f, d[PHP_CYCLE], "Cycle (not built yet)", " phrases");
		paramQuantities[PHP_CYCLE]->snapEnabled = true;
		configParam(PHP_SECTIONS, 0.f, 1.f, d[PHP_SECTIONS], "Sections (not built yet)",
			"%", 0.f, 100.f);
		configParam(PHP_ELIDE, 0.f, 1.f, d[PHP_ELIDE], "Elide (not built yet)", "%", 0.f, 100.f);
		configSwitch(PHP_RECORD, 0.f, 1.f, d[PHP_RECORD], "Record notes to disk (not built yet)",
			{"Off", "Recording"});

		// A SHARE OF THE BEATS, NOT AN ABSTRACT AMOUNT, because the corpus gives the number
		// directly: a true triplet event — the middle unit of the triplet, the one swing skips —
		// falls on eight per cent of the beats real solos play. Thirty per cent at the top is
		// beyond anything measured and is there to be overdone deliberately.
		configParam(PHP_TRIPLETS, 0.f, 0.3f, d[PHP_TRIPLETS], "Triplets (not built yet)",
			"% of beats", 0.f, 100.f);

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
	int nextNote = 0;
	float phraseOffset = 0.f;
	bool havePattern = false;

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
		c.pause = params[PHP_PAUSE].getValue();
		c.phrasePause = params[PHP_PHRASE_PAUSE].getValue();
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

	void decide(const Harmony& h, float phraseBeats, float offsetInPhrase) {
		PhraseAsk ask;
		ask.phraseBeats = phraseBeats;
		ask.barBeats = h.barBeats > 0 ? (float) h.barBeats : 4.f;
		ask.beatsPerSecond = std::max(0.1f, beatsPerSecond);
		ask.cadence = h.phraseCadence;
		ask.changes = h.phraseChangeCount > 0 ? h.phraseChanges : NULL;
		ask.changeCount = h.phraseChangeCount;

		// THE SEED. Added to the chart's unless OWN SEED says otherwise, and used alone when the
		// cable carries none — see docs/phrase.md.
		const uint32_t own = (uint32_t) std::lround(params[PHP_SEED].getValue());
		const bool alone = params[PHP_OWN_SEED].getValue() > 0.5f || h.seed == 0;
		ask.seed = alone ? own : h.seed + own;

		// WHERE THIS PHRASE FALLS since the top of the form. CYCLE will fold this; until then each
		// phrase decides differently, which is what a fresh line every phrase means.
		ask.cyclePosition = (int) (h.epoch * std::max<uint32_t>(1, h.phrasesPerPass) + h.phrase);

		phraseGenerate(ask, controls(), pattern);
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

	void endSounding() {
		if (!isSounding)
			return;
		Event e;
		e.kind = Event::OFF;
		e.handle = sounding;
		busPush(slot, e);
		isSounding = false;
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);

		if (relink.exchange(false)) {
			chart.clear();
			const int s = wantSlot.load();
			if (s >= 0)
				chart.attach(s, wantGeneration.load());
		}

		Harmony h;
		const bool have = chart.harmony(h) && h.valid;
		if (!have) {
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
		// where the music actually is, rather than carried on from where it was.
		const bool newPhrase = phraseIndex != lastPhrase || h.epoch != lastEpoch
			|| phraseBeats != lastPhraseBeats;
		if (newPhrase || jumped || !havePattern) {
			if (newPhrase || jumped)
				endSounding();
			lastPhrase = phraseIndex;
			lastEpoch = h.epoch;
			lastPhraseBeats = phraseBeats;
			decide(h, phraseBeats, offset);
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

		// THE NOTES, SENT AT THEIR TIMES.
		while (nextNote < pattern.noteCount && pattern.notes[nextNote].offset <= offset + 1e-4f) {
			const PhraseNote& n = pattern.notes[nextNote++];
			endSounding();
			Event e;
			e.kind = Event::ON;
			e.handle = sounding = mintHandle();
			// THE PITCH NOTE SETS, in volts from middle C, so the cable can go straight to fromMPX
			// and be heard. An mpxVoice downstream replaces it.
			e.pitch = (params[PHP_NOTE].getValue() - 60.f) / 12.f;
			e.level = n.level;
			e.duration = n.duration / std::max(0.1f, beatsPerSecond);
			e.pan = 0.f;
			e.bendRange = 2.f;
			busPush(slot, e);
			isSounding = true;
			soundingLeft = std::max(1, (int) std::lround(e.duration * args.sampleRate));
			lamp = 1.f;
		}
		if (isSounding && --soundingLeft <= 0)
			endSounding();

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

static const float NAME_HALF = 1.22f;

/** THE KNOBS ARE THE LARGE SIZE, 12.19 mm across rather than 9.6, and every measurement that
depends on that follows from this one number rather than being written out again.

A name sits clear of the knob's edge, or clear of the numbers round it where there are any: the
marks are drawn 2.6 mm past the metal and a mark is about 0.7 mm from its middle to its edge. */
static const float KNOB_MM = 12.19f;
static const float KNOB_EDGE = KNOB_MM / 2.f;
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

	// A LINE UNDER THE HEADINGS, across the five named columns and not the service column, which
	// has no heading to separate. It says that the word above it names the band below rather than
	// the knob nearest it.
	pRule(L, "p.headrule", ruleX[0], 15.5f, 91.f, true);

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
	pPlate(L, "p.cycle", colX[0], rowY[1], PHP_CYCLE, 2, "CYCLE\nPHRASES");
	pKnob(L, "p.sections", colX[0], 58.5f, PHP_SECTIONS, "SECTIONS");

	// ---- 2. how the phrase is divided ---------------------------------------------------------
	//
	// HOW LONG, HOW ALIKE, HOW IT BEGINS, HOW IT ENDS.
	pLabel(L, "p.head.groups", colX[1], headY, "GROUPS", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.group", colX[1], rowY[0], PHP_GROUP, "GROUP\nLENGTH", {"1", "", "8"}, 10.f);
	pKnob(L, "p.vary", colX[1], rowY[1], PHP_VARY, "LENGTH\nVARIATION", {}, 9.f);
	pKnob(L, "p.start", colX[1], rowY[2], PHP_START, "START", {"PICKUP", "MIXED", "AFTER"}, 10.f);
	// START'S NAME IS SET LARGER AND ITS MARKS SMALLER than the panel's standard, from the editor.
	if (Item* i = L.find("p.start"))
		i->nameSize = 5.f;
	if (Item* i = L.find("p.start.label"))
		i->size = 8.f;
	pKnob(L, "p.ending", colX[1], rowY[3], PHP_ENDING, "ENDING", {"WEAK", "", "STRONG"});

	// ---- 3. the pauses -------------------------------------------------------------------------
	//
	// THE THREE LENGTHS OF PAUSE, SHORTEST FIRST, then the two controls that act on them. A
	// dropped phrase is a pause the length of a phrase, so it belongs here rather than among the
	// group controls: the three are one scale — between groups, between phrases, and instead of
	// a phrase — and somebody wanting more space in the music finds all of it in one column.
	pLabel(L, "p.head.pauses", colX[2], headY, "PAUSES", Panel::CENTRE, true, HEAD_SIZE);
	pKnob(L, "p.pause", colX[2], rowY[0], PHP_PAUSE, "GROUP\nPAUSE", {"0", "", "8"}, 10.f);
	pKnob(L, "p.phrasepause", colX[2], rowY[1], PHP_PHRASE_PAUSE, "PHRASE\nPAUSE",
		{"0", "", "8"}, 10.f);
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
	pPlate(L, "p.sub", sx, 18.f, PHP_SUBDIVISION, 4, "GRID");
	L.items.back().y = 13.5f;

	// THE SEED, WHAT IT SEEDS, AND HOW FAR IT MAY GO. The seed picks which line you get and
	// VARIATION sets how far from the likeliest that line may stray, so they are one group and
	// the seed is named for what it seeds. LOCK SEED between them says whether the chart's own
	// seed still moves this one.
	pPlate(L, "p.seed", sx, 33.32f, PHP_SEED, 3, "VARIATION\nSEED");
	L.items.back().y = 28.f;
	{
		Item i;
		i.key = "p.own"; i.kind = Item::PARAM; i.id = PHP_OWN_SEED;
		i.x = sx; i.y = 41.5f; i.style = "latch"; i.diameter = 6.6f;
		L.items.push_back(i);
		pLabel(L, "p.own.label", sx, 46.f, "LOCK SEED", Panel::CENTRE, true, NAME_SIZE, "p.own");
	}
	pKnob(L, "p.variation", sx, 58.5f, PHP_VARIATION, "VARIATION\nAMOUNT", {}, 9.5f);

	// THE PITCH THE NOTES ARE SENT AT, which matters only when nothing downstream is choosing
	// one. Named for what it does rather than for what it is: a plate reading "NOTE" on a module
	// that generates notes says nothing.
	pPlate(L, "p.note", sx, 88.f, PHP_NOTE, 3, "DEFAULT\nNOTE");
	L.items.back().y = 82.f;

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
		i.x = colX[0]; i.y = 90.f;
		L.items.push_back(i);
		pLabel(L, "p.lamp.label", colX[0], 94.f, "BEGIN\nNOTE", Panel::CENTRE, true, NAME_SIZE,
			"p.lamp");
	}
	{
		Item i;
		i.key = "p.phraselamp"; i.kind = Item::LIGHT; i.id = PhraseModule::L_PHRASE;
		i.x = colX[0]; i.y = 101.f;
		L.items.push_back(i);
		pLabel(L, "p.phraselamp.label", colX[0], 105.f, "BEGIN\nPHRASE", Panel::CENTRE, true,
			NAME_SIZE, "p.phraselamp");
	}
	// TWO LINES: at the panel's name size "NOTES OUT" is 13.7 mm and this column, narrowed by the
	// service column beside it, has 16.5.
	pJack(L, "p.out", Item::PORT_OUT, colX[0], 115.f, PhraseModule::O_MPX, "NOTES\nOUT",
		NOTE_CABLE);
	L.items.back().y = 122.5f;

	L.bindOffsets();
	return L;
}


struct PhraseWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

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
		menu->addChild(createCheckMenuItem("Record notes to disk (not built yet)", "",
			[=]() { return m->params[PHP_RECORD].getValue() > 0.5f; },
			[=]() {
				Param& p = m->params[PHP_RECORD];
				p.setValue(p.getValue() > 0.5f ? 0.f : 1.f);
			}));
	}

	void step() override {
		ModuleWidget::step();
		PhraseModule* m = dynamic_cast<PhraseModule*>(module);
		if (!m)
			return;
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
