/** mpxFingerPicker — the chords on an mpxChart cable, played finger-style. See
docs/finger-picker.md.

The two hands are in FingerPicking.hpp, with no Rack in them. This module reads the chart off its
cable, turns each chord into what the hands need, steps through the pattern on the chart's beat,
and sends the notes on with their strings and frets, let ring, for mpxGuitar to play.

THE CHART'S BEAT, NOT A CLOCK. The beat, the bar, the metre and the swing are on the cable already.
A step is an eighth note; the second eighth of each beat comes where the chart's swing puts it.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "FingerPicking.hpp"

#include <algorithm>
#include <cmath>

namespace px {


/** A let-ring note's length: long enough that it is always ended by its string being played
again, or by the hand moving, before this. */
static const float RING_SECONDS = 8.f;
/** How close to the change, in beats, a step takes the coming chord rather than the one
sounding: the step and the change fall on the same beat, and the chart may publish the change a
sample after the beat the step is counted from. */
static const float CHANGE_EARLY_BEATS = 0.02f;
/** HOW FAR PAST WHAT A PLAYER WOULD DO the expression knobs go: their tops are half again
beyond the settings' ordinary tops, so turning one up makes plain what it does, and it can then be
brought back to taste. Each starts at the same value as before, now two thirds as far round. */
static const float EXAGGERATE = 1.5f;
/** What the voicing chances are drawn for, kept apart from the picker's own. */
static const uint32_t SALT_VOICE = 100, SALT_REVOICE = 101;


/** THE CHORD AS THE HANDS NEED IT. Every tone the chord library ranks as part of the chord, a
tone ranked wrong for it left out; the root and the tones that say what the chord is — the third,
the seventh, an altered fifth, a sixth — essential; the bass the slash bass or the root. */
static ChordNotes chordNotesOf(const Chord& chord, const Key& key, float jazz) {
	ChordNotes c;
	ChordTone tones[MAX_CHORD_TONES];
	const int n = chordVoicingTones(chord, key, tones);
	for (int i = 0; i < n && c.count < ChordNotes::MAX; i++) {
		if (tones[i].rank >= 5)
			continue;
		c.pc[c.count] = tones[i].pc;
		c.essential[c.count] = tones[i].degree == 1 || tones[i].rank <= 1;
		c.degree[c.count] = tones[i].degree;
		c.count++;
	}
	c.bass = chordBassPitchClass(chord, key);
	// TOWARD JAZZ, the chord as a jazz player hears it: a major triad on the fifth degree is a
	// dominant, and gains a flat seventh rather than a major one.
	const bool dominant = chord.degree == 5 && chord.accidental == 0 && chord.quality == Q_MAJOR;
	return jazzTones(c, jazz, dominant);
}

static bool sameChord(const Chord& a, const Chord& b) {
	return a.valid == b.valid && a.degree == b.degree && a.accidental == b.accidental
		&& a.quality == b.quality && a.bassDegree == b.bassDegree
		&& a.bassAccidental == b.bassAccidental;
}


//?module Plays the chords on an mpxChart cable finger-style, an eighth note a step on the chart's
//? beat: the thumb plays each chord's bass on the fourth, fifth or sixth string, and the index,
//? middle and ring fingers play the third, second and first strings, fretted to tones of the
//? chord. Each note is sent let ring with its string and fret, for mpxGuitar to play.
//?note The fretting hand frets only the four strings the pattern plays. At a chord change, a
//? fretted string whose fret changes stops, and one whose fret stays the same rings on until it
//? is picked again. An open treble string rings on unless the new shape frets it. On the last
//? step before a change, a treble string the new shape frets differently is already played as
//? the new shape has it.
//?note The picking and ornament variations are chosen for a phrase and played in every bar of it,
//? through its chord changes, with more added in its last bar; a section that returns in the form
//? brings back its figures. Every variation is drawn from the chart's seed and its pass counter,
//? so a patch plays the same way each time it is played from the top, and each pass differs.
//?note The display shows the chord and the shape the hand holds, lowest string first, with an x
//? for each string that is not played.
//?note The second eighth of each beat falls where the chart's swing puts it. Nothing plays while
//? the chart is stopped, and every string stops when it stops.
//?note Notes arriving on the input are not passed on. The chart's harmony and pedals are, with an
//? instrument of six strings in standard tuning.
struct FingerPickerModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_POSITION,
		P_PATTERN,
		P_ACCENT,
		P_VOICING,
		P_PICKING,
		P_ORNAMENT,
		P_PALM,
		P_STYLE,
		// APPENDED, NEVER INSERTED: a patch stores a parameter by its position in this list.
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
		NUM_LIGHTS
	};

	FingerPickerModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_POSITION, 0.f, 12.f, 0.f, "Position", " fret");
		//? The fret the fretting hand plays near, from the open position at 0 to the 12th fret.
		//? A preference, not a limit: a chord with no shape near it is played where it has one.
		//? A change takes effect on the next step.
		paramQuantities[P_POSITION]->snapEnabled = true;
		{
			std::vector<std::string> names;
			for (int i = 0; i < NUM_PICK_PATTERNS; i++)
				names.push_back(PICK_PATTERNS[i].name);
			configSwitch(P_PATTERN, 0.f, (float) (NUM_PICK_PATTERNS - 1), 0.f, "Pattern", names);
			//? Forward roll: thumb, index, middle, ring, thumb, ring, middle, index. Pinch and
			//? roll: thumb and ring together on beats 1 and 3, then index, middle, index. Thumb
			//? and pinch: the bass on each beat, the first two strings together between. Arpeggio
			//? 1/4: thumb, index, middle, ring, a beat each. Arpeggio 1/8: thumb, index, middle,
			//? ring, twice, in eighths. A bar of three has its own form of each.
		}
		configParam(P_ACCENT, 0.f, 1.f, 0.5f / EXAGGERATE, "Accent", "%", 0.f, 100.f);
		//? How much stronger the accented notes are than the rest. At a third of the way, the
		//? thumb on the first beat of the bar is strongest, the thumb on other beats next, then
		//? fingers on a beat, then fingers between beats; a new chord is struck a little harder
		//? and the last beat of a phrase a little softer. At 0 every note is struck alike; at the
		//? top the contrast is tripled.
		configParam(P_VOICING, 0.f, 1.f, 0.3f / EXAGGERATE, "Voicing variation", "%", 0.f, 100.f);
		//? How often the fretting hand holds a chord another way: a chord that returns, often,
		//? and the same chord into a new bar, less often. The higher the setting, the further
		//? from the best shape the other may be, a different top note, a different bass string
		//? or higher on the neck. At 0, a chord is always held the same way; at the top, every
		//? returning chord is held another way.
		configParam(P_PICKING, 0.f, 1.f, 0.25f / EXAGGERATE, "Picking variation", "%", 0.f, 100.f);
		//? How often a step of the pattern is changed: a pinch in place of a single note, a note
		//? left out, another finger on the next string, a rest filled, or the thumb on the
		//? chord's fifth. The same steps change in every bar of a phrase. The thumb on the first
		//? beat of each bar and each chord is never changed. At 0, the pattern as written; at the
		//? top, about half the steps changed.
		configParam(P_ORNAMENT, 0.f, 1.f, 0.25f / EXAGGERATE, "Ornaments", "%", 0.f, 100.f);
		//? How often notes are added that the pattern does not contain: a hammer-on or pull-off
		//? into a treble note from a scale note on its string, and a bass run into the next
		//? chord, a scale step at a time on the two steps before a change of bass. The same steps
		//? are ornamented in every bar of a phrase. At 0, none; at the top, about half the single
		//? treble notes, and a run into nearly every change of bass.
		configParam(P_PALM, 0.f, 1.f, 0.f, "Palm mute", "%", 0.f, 100.f);
		//? How many of the thumb's bass notes are palm-muted, the side of the picking hand
		//? resting on the bass strings: muffled, its ring gone in under a second, under treble
		//? that rings. The same
		//? steps are muted in every bar of a phrase; the first beat of the bar only in the upper
		//? half. At 0 none; at 100% all.
		configParam(P_STYLE, 0.f, 1.f, 0.f, "Style, pop to jazz", "%", 0.f, 100.f);
		//? From pop at 0 to jazz at 100%. Toward jazz a triad gains its seventh from a quarter of
		//? the way, and its ninth from 60%; the hand leaves the open strings for the middle of the
		//? neck, puts sevenths and colour tones on the treble strings rather than the root or
		//? fifth, avoids doubled notes, and moves each treble string as little as it can.
		configInput(I_MPX, "MPX from mpxChart");
		//? mpxChart's cable: the key, the chords with their bass notes, and the beat, bar, metre
		//? and swing the pattern follows.
		configOutput(O_MPX, "MPX notes, with strings and frets");
		//? The notes, each with its string and fret and let ring, and the chart's harmony and
		//? pedals. For mpxGuitar, or any MPX module.
		slot = busClaim(&generation);
	}

	~FingerPickerModule() {
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

	// ---- the cables, as the widget finds them ----

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

	// ---- the hands ----

	FingerPicker picker;
	Chord chordHeld;
	Key keyHeld;
	int positionHeld = -1;
	float styleHeld = -1.f;
	int64_t lastStep = -1;

	/** The note ringing on each string and the fret it is at, so the hand can stop it. */
	int64_t ringing[Tuning::MAX_STRINGS] = {};
	int ringingFret[Tuning::MAX_STRINGS] = {};

	/** THE SECOND NOTE OF AN ORNAMENT, waiting for its moment: a hammer-on or a pull-off falls
	partway through its step. Held as the beat it is due at, so a chart that stops or jumps can
	drop it. */
	struct Pending {
		double beat = 0.0;
		PickedNote note;
	};
	static const int MAX_PENDING = 8;
	Pending pending[MAX_PENDING];
	int pendingCount = 0;

	/** For the panel: the chord and the shape held, packed so the drawing thread can read them
	without a lock. The shape is a byte a string, the fret plus one, nought for a string left out. */
	std::atomic<uint64_t> shownShape{0};
	std::atomic<int32_t> shownChord{-1};
	std::atomic<int32_t> shownKey{0};

	uint32_t instrumentChange = 1;
	bool instrumentSent = false;

	void stopString(int s) {
		if (ringing[s] == 0)
			return;
		if (slot >= 0) {
			Event e;
			e.kind = Event::OFF;
			e.handle = ringing[s];
			busPush(slot, e);
		}
		ringing[s] = 0;
	}

	void stopAll() {
		for (int s = 0; s < Tuning::MAX_STRINGS; s++)
			stopString(s);
		pendingCount = 0;
	}

	/** THE HAND HAS MOVED: what it lets ring and what it stops. A string fretted where the new
	shape frets it rings on; a stopped string anywhere else is stopped, a finger lifted or put down.
	An open treble string rings on unless the new shape puts a finger on it: the fretting hand does
	not touch it as it moves. An open bass string rings on if it is a tone of the new chord, and is
	stopped if not, as the thumb and palm keep the bass clean. */
	void handMoved() {
		const Shape& now = picker.shape();
		const ChordNotes& c = picker.chord();
		for (int s = 0; s < picker.tuning.strings; s++) {
			if (ringing[s] == 0)
				continue;
			if (picker.holding() && now.fret[s] == ringingFret[s])
				continue;
			if (ringingFret[s] == 0 && s < BASS_STRING_FIRST && now.fret[s] <= 0)
				continue;
			if (ringingFret[s] == 0 && picker.holding()
					&& (c.contains(picker.tuning.open[s] % 12) || c.bass == picker.tuning.open[s] % 12)
					&& (!now.sounds(s) || now.fret[s] == 0))
				continue;
			stopString(s);
		}
		show();
	}

	void show() {
		const Shape& now = picker.shape();
		uint64_t packed = 0;
		if (picker.holding())
			for (int s = 0; s < picker.tuning.strings; s++)
				packed |= (uint64_t) (uint8_t) (now.fret[s] + 1) << (8 * s);
		shownShape.store(packed);
		const Chord& chord = chordHeld;
		shownChord.store((chord.degree & 0xff) | ((chord.accidental + 1) << 8)
			| (chord.quality << 16) | ((chord.bassDegree & 0x7) << 24)
			| ((chord.bassAccidental + 1) << 27));
		shownKey.store((keyHeld.tonic & 0xff) | (keyHeld.minor ? 0x100 : 0));
	}

	/** A NEW CHORD: the hand moves to a shape for it. */
	void moveTo(const Chord& chord, const Key& key, float chance) {
		chordHeld = chord;
		keyHeld = key;
		// NO CHORD: the hand comes off the strings.
		if (!chord.valid) {
			stopAll();
			shownChord.store(-1);
			return;
		}
		picker.position = positionHeld;
		picker.setChord(chordNotesOf(chord, key, picker.variation.jazz), chance);
		handMoved();
	}

	void send(const PickedNote& p) {
		// THE THUMB LEAVING THE SHAPE'S BASS STRING STOPS THE ONE BEFORE: a run note or an
		// alternate bass would otherwise ring on top of the bass it moved from, G and A together
		// on the way into C. Coming back to the shape's bass lets the other ring.
		if (p.finger == THUMB && p.string != picker.shape().bassString(picker.tuning))
			for (int s = BASS_STRING_FIRST; s < picker.tuning.strings; s++)
				if (s != p.string)
					stopString(s);
		Event e;
		e.kind = Event::ON;
		e.handle = mintHandle();
		e.pitch = (float) (p.note - 60) / 12.f;
		e.level = p.level;
		e.duration = RING_SECONDS;
		e.technique = Event::LET_RING | p.technique;
		e.string = (int8_t) (p.string + 1);
		e.fret = (int8_t) p.fret;
		busPush(slot, e);
		ringing[p.string] = e.handle;
		ringingFret[p.string] = p.fret;
	}

	/** The step's notes: those at its start now, the rest when the beat reaches them. */
	void pick(const PickContext& at, double stepBeat, double stepLength) {
		PickedNote notes[MAX_STEP_NOTES];
		const int n = picker.notesAt(at, notes);
		for (int i = 0; i < n; i++) {
			if (notes[i].delay <= 0.f) {
				send(notes[i]);
				continue;
			}
			if (pendingCount < MAX_PENDING) {
				pending[pendingCount].beat = stepBeat + notes[i].delay * stepLength;
				pending[pendingCount].note = notes[i];
				pendingCount++;
			}
		}
	}

	void firePending(double beat) {
		int kept = 0;
		for (int i = 0; i < pendingCount; i++) {
			if (beat >= pending[i].beat)
				send(pending[i].note);
			else
				pending[kept++] = pending[i];
		}
		pendingCount = kept;
	}

	/** WHAT THE CABLE OUT CARRIES BESIDES THE NOTES: the chart's harmony and pedals, passed on,
	and the instrument, which is this module's own — six strings in standard tuning. */
	void publish() {
		Harmony h;
		if (reader.harmony(h))
			busPublishHarmony(slot, h);
		float sustain, soft;
		reader.pedals(sustain, soft);
		busPublishPedals(slot, sustain, soft);
		if (!instrumentSent) {
			Instrument p;
			p.valid = true;
			p.program = 25;
			p.stringCount = (uint8_t) picker.tuning.strings;
			for (int s = 0; s < picker.tuning.strings; s++)
				p.tuning[s] = (uint8_t) picker.tuning.open[s];
			p.setName("Finger-picked guitar");
			p.change = instrumentChange;
			busPublishInstrument(slot, p);
			instrumentSent = true;
		}
	}

	void onReset() override {
		instrumentChange++;
		instrumentSent = false;
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(busFlashVolts(slot));
		if (slot < 0)
			return;

		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		// The notes of whatever comes in are not this module's to pass on: it plays the chart.
		{
			Event e;
			while (reader.next(e)) {}
		}
		publish();

		Harmony h;
		const bool playing = reader.harmony(h) && h.valid && !h.holding;
		if (!playing) {
			if (lastStep != -1) {
				stopAll();
				lastStep = -1;
			}
			return;
		}

		// THE STEP: two to a beat, the second where the swing puts it.
		const double beatFloor = std::floor(h.beat);
		const float within = (float) (h.beat - beatFloor);
		const float ratio = std::max(1.f, h.swingEighth);
		const float firstHalf = ratio / (1.f + ratio);
		const int half = within >= firstHalf ? 1 : 0;
		const int64_t step = (int64_t) beatFloor * 2 + half;
		// A jump backwards, a rewind, drops what was waiting.
		if (step < lastStep)
			pendingCount = 0;
		firePending(h.beat);
		if (step == lastStep)
			return;
		lastStep = step;

		picker.variation.accent = EXAGGERATE * params[P_ACCENT].getValue();
		picker.variation.voicing = EXAGGERATE * params[P_VOICING].getValue();
		picker.variation.picking = EXAGGERATE * params[P_PICKING].getValue();
		picker.variation.ornament = EXAGGERATE * params[P_ORNAMENT].getValue();
		picker.variation.palm = params[P_PALM].getValue();
		picker.variation.jazz = params[P_STYLE].getValue();
		picker.pattern = (int) std::lround(params[P_PATTERN].getValue());
		// THE SEED WITH THE PASS MIXED IN, so each pass of the form varies differently and the
		// same pass is the same every time.
		const uint32_t seed = h.seed * 0x9e3779b9u ^ (h.epoch + 1u) * 0x85ebca6bu;

		// THE CHORD, the coming one when the step falls on its change.
		const bool early = h.next.valid && h.beatsToNext <= CHANGE_EARLY_BEATS;
		const Chord chord = early ? h.next : h.current;
		const int barBeats = std::max(1, (int) h.barBeats);
		const int stepInBar = (int) std::floor(h.beatInBar + 1e-4f) * 2 + half;
		const int position = (int) std::lround(params[P_POSITION].getValue());
		bool chordStart = false;
		if (!sameChord(chord, chordHeld) || h.key.tonic != keyHeld.tonic
				|| h.key.minor != keyHeld.minor || position != positionHeld
				|| std::fabs(picker.variation.jazz - styleHeld) > 0.02f) {
			styleHeld = picker.variation.jazz;
			chordStart = !sameChord(chord, chordHeld);
			positionHeld = position;
			moveTo(chord, h.key, pickChance(seed, step, SALT_VOICE));
		}
		else if (stepInBar == 0 && picker.revoice(pickChance(seed, step, SALT_REVOICE)))
			handMoved();
		if (!chord.valid)
			return;

		PickContext at;
		at.step = stepInBar;
		at.barBeats = barBeats;
		at.clock = step;
		at.seed = seed;
		at.chordStart = chordStart;
		// What comes next, unknown when the step has already taken the coming chord.
		if (!early && h.next.valid && h.beatsToNext > 0.f) {
			at.stepsToChange = (int) std::lround(h.beatsToNext * 2.f);
			at.nextBass = chordBassPitchClass(h.next, h.key);
		}
		if (h.phraseBeats > 0.f)
			at.phraseStepsLeft = (int) std::lround(h.beatsToPhraseEnd * 2.f);
		// WHICH PHRASE, for its figure: a section's phrase by the section's letter and its place
		// in it, so the A section's second phrase plays the same figure every time the A comes
		// round in a pass; a phrase of an unsectioned chart by its number; and where the chart
		// has no phrases, four bars at a time.
		if (h.phraseBeats > 0.f)
			at.motif = h.section != 0
				? (((uint32_t) (uint8_t) h.section << 8) | h.phraseInSection) | (1u << 20)
				: (uint32_t) h.phrase + 1u;
		else
			at.motif = (uint32_t) (step / (int64_t) (barBeats * 2 * 4)) | (1u << 21);
		int scale[7];
		scalePitchClasses(h.key, scale);
		for (int i = 0; i < 7; i++)
			at.scaleMask |= 1 << (((scale[i] % 12) + 12) % 12);

		// THE SHAPE TO COME, a step before it comes, for the treble strings to move early.
		Shape coming;
		if (at.stepsToChange == 1 && h.next.valid && picker.preview(chordNotesOf(h.next, h.key,
				picker.variation.jazz),
				pickChance(seed, step + 1, SALT_VOICE), &coming))
			at.nextShape = &coming;

		const double stepBeat = beatFloor + (half ? firstHalf : 0.f);
		const double stepLength = half ? 1.f - firstHalf : firstHalf;
		pick(at, stepBeat, stepLength);
		firePending(h.beat);
	}
};


/** The chord and the shape the hand holds, as a guitarist writes it, lowest string first. */
struct PickerDisplay : widget::Widget {
	FingerPickerModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0 || !module)
			return;
		std::string text = "—";
		const int32_t packed = module->shownChord.load();
		if (packed >= 0) {
			Chord chord;
			chord.valid = true;
			chord.degree = (int8_t) (packed & 0xff);
			chord.accidental = (int8_t) (((packed >> 8) & 0xff) - 1);
			chord.quality = (uint8_t) ((packed >> 16) & 0xff);
			chord.bassDegree = (int8_t) ((packed >> 24) & 0x7);
			chord.bassAccidental = (int8_t) (((packed >> 27) & 0x3) - 1);
			const int32_t k = module->shownKey.load();
			Key key;
			key.tonic = (int8_t) (k & 0xff);
			key.minor = (k & 0x100) != 0;
			text = chordLetter(chord, key) + "  ";
			const uint64_t shape = module->shownShape.load();
			if (shape == 0)
				text += "—";
			for (int s = module->picker.tuning.strings - 1; shape && s >= 0; s--) {
				const int fret = (int) ((shape >> (8 * s)) & 0xff) - 1;
				text += fret < 0 ? std::string("x") : std::to_string(fret);
				if (fret >= 10 && s > 0)
					text += " ";
			}
		}
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


/** TEN HP, 50.8 MM: the chord and the shape, the patterns, the position, accent and style knobs,
the three variations, and the two cables with palm mute between them. */
static const float PICKER_W = 50.8f;
/** The outer columns of knobs, the middle one at the centre. */
static const float COLUMN_1 = 10.f, COLUMN_3 = PICKER_W - 10.f;

static Layout pickerLayout() {
	Layout L;
	L.hp = PICKER_W / 5.08f;
	L.title = "mpxFingerPicker";

	auto label = [&](const char* key, float x, float y, const char* text, const char* owner,
			bool heading, float size) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = Panel::CENTRE; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, key, false, 0.f);
	};

	Item what;
	what.key = "d.shape"; what.kind = Item::DISPLAY;
	what.x = 3.f; what.y = 20.f; what.w = PICKER_W - 6.f; what.h = 6.f;
	L.items.push_back(what);

	// THE PATTERNS, a lamp group reading down, each named on its right, placed by its top left
	// corner, under a heading.
	{
		Item h;
		h.key = "p.pattern.group"; h.kind = Item::LABEL; h.x = 4.f; h.y = 31.f;
		h.text = h.defaultText = "PATTERN"; h.align = Panel::LEFT; h.heading = true;
		h.owner = "p.pattern";
		L.items.push_back(h);
	}
	{
		Item i;
		i.key = "p.pattern"; i.kind = Item::PARAM; i.id = FingerPickerModule::P_PATTERN;
		i.style = "lamps"; i.names = {"FORWARD ROLL", "PINCH & ROLL", "THUMB & PINCH", "ARPEGGIO 1/4",
			"ARPEGGIO 1/8"};
		i.horizontal = false; i.pitch = 5.5f; i.labelSide = Panel::RIGHT;
		i.x = 4.f; i.y = 35.f;
		L.items.push_back(i);
	}

	// WHERE ON THE NECK, HOW HARD, AND IN WHAT STYLE: the three large knobs.
	{
		Item i;
		i.key = "p.position"; i.kind = Item::PARAM; i.id = FingerPickerModule::P_POSITION;
		i.style = "knob"; i.x = COLUMN_1; i.y = 70.f;
		i.ticks = 3; i.tickMarks = {"0", "6", "12"};
		L.items.push_back(i);
		label("p.position.label", COLUMN_1, 82.f, "POSITION", "p.position", true, 8.f);
	}
	{
		Item i;
		i.key = "p.accent"; i.kind = Item::PARAM; i.id = FingerPickerModule::P_ACCENT;
		i.style = "knob"; i.x = PICKER_W / 2.f; i.y = 70.f;
		L.items.push_back(i);
		label("p.accent.label", PICKER_W / 2.f, 82.f, "ACCENT", "p.accent", true, 8.f);
	}
	{
		Item i;
		i.key = "p.style"; i.kind = Item::PARAM; i.id = FingerPickerModule::P_STYLE;
		i.style = "knob"; i.x = COLUMN_3; i.y = 70.f;
		i.ticks = 2; i.tickMarks = {"POP", "JAZZ"};
		L.items.push_back(i);
		label("p.style.label", COLUMN_3, 82.f, "STYLE", "p.style", true, 8.f);
	}

	// THE THREE VARIATIONS, on small knobs in a row: the fretting hand, the picking hand, and
	// the notes added.
	auto small = [&](const char* key, int id, float x, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob.small"; i.x = x; i.y = 94.f;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, 101.f, name, key, true, 7.f);
	};
	small("p.voicing", FingerPickerModule::P_VOICING, COLUMN_1, "VOICING");
	small("p.picking", FingerPickerModule::P_PICKING, PICKER_W / 2.f, "PICKING");
	small("p.ornament", FingerPickerModule::P_ORNAMENT, COLUMN_3, "ORNAMENT");

	// PALM MUTE between the two cables: the thumb's hand, set for a style rather than a song.
	{
		Item i;
		i.key = "p.palm"; i.kind = Item::PARAM; i.id = FingerPickerModule::P_PALM;
		i.style = "knob.small"; i.x = PICKER_W / 2.f; i.y = 114.f;
		L.items.push_back(i);
		label("p.palm.label", PICKER_W / 2.f, 120.5f, "PALM\nMUTE", "p.palm", true, 7.f);
	}

	jack("in.mpx", Item::PORT_IN, 10.f, 114.f, FingerPickerModule::I_MPX, "mpx IN");
	jack("out.mpx", Item::PORT_OUT, PICKER_W - 10.f, 114.f, FingerPickerModule::O_MPX, "mpx OUT");

	L.bindOffsets();
	return L;
}


struct FingerPickerWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	FingerPickerWidget(FingerPickerModule* module) {
		setModule(module);
		layout = pickerLayout();
		layoutApplyUser("mpxFingerPicker", layout);
		panel = new Panel;
		addChild(panel);
		PickerDisplay* what = new PickerDisplay;
		what->module = module;
		layoutPlaceDisplay(this, layout, "d.shape", what);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxFingerPicker");
	}

	void step() override {
		ModuleWidget::step();
		FingerPickerModule* m = dynamic_cast<FingerPickerModule*>(module);
		if (!m)
			return;
		// WHICH MPX CABLES ARE PATCHED, resolved here: the engine knows nothing of cables.
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(FingerPickerModule::I_MPX)) {
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
		m->link(slots, generations, n);

		if (PortWidget* out = getOutput(FingerPickerModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMpxFingerPicker = createModel<px::FingerPickerModule, px::FingerPickerWidget>(
	"mpxFingerPicker");
