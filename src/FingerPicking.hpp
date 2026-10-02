#pragma once
/** THE TWO HANDS OF A FINGER-STYLE PLAYER, with no Rack in them. See docs/finger-picker.md.

mpxFingerPicker reads the chart off its cable and works out the beat; everything about what a
player does with the chord is here, so a command-line test can play a progression through it and
check every note.

THE PICKING HAND plays a pattern: a cycle of steps an eighth note apart, each naming the fingers
that play on it. The thumb plays the bass, on whichever low string the fretting hand has put the
chord's bass note; the index, middle and ring fingers play the third, second and first strings.

THE FRETTING HAND frets only the strings the picking hand plays: the three treble strings and the
one bass string. The others are left alone, neither fretted nor sounded.

EXPRESSION AND VARIATION. How hard each note is struck follows the beat and the phrase, as far as
the Accent setting says. Three settings vary what is played, each in its own dimension: Voicing,
how often a chord is held a different way; Picking, how often a step of the pattern is changed;
Ornament, how often notes the pattern does not contain are added.

A FIGURE, NOT SCATTERED CHANGES. Which steps the picking and ornament variations change, and how,
is decided for the phrase and played in every bar of it, through its chord changes, as a player
settles into a varied figure; the phrase's last bar adds to it, where a player fills. Everything is
drawn from the chart's seed and pass counter, so a patch plays the same way every time it is played
from the top, and a phrase that returns in the form returns with its figure.

STRINGS ARE NUMBERED FROM THE TOP, as in Fretboard.hpp: nought is the first string.
*/
#include "Fretboard.hpp"

namespace px {


enum Finger { THUMB, INDEX, MIDDLE, RING, NUM_FINGERS };

/** One step of a pattern: the fingers that play on it, a bit for each. */
enum : uint8_t { F_T = 1 << THUMB, F_I = 1 << INDEX, F_M = 1 << MIDDLE, F_R = 1 << RING };

/** HOW A NOTE IS SOUNDED, in the same bits as the cable's Event::Technique, so the module passes
them on unchanged. Nought is an ordinary picked note. */
enum : uint32_t { PT_HAMMER_ON = 1u << 0, PT_PULL_OFF = 1u << 1, PT_PALM_MUTE = 1u << 9 };

/** A PICKING PATTERN, as data: the steps of one bar, an eighth note each. There are two forms, one
for a bar of an even number of beats and one for a bar of three, since a pattern is shaped to the
bar it is played in; a bar of any other length plays the even form, cut off or repeated. */
struct PickPattern {
	const char* name;
	int evenSteps;
	uint8_t even[16];
	int tripleSteps;
	uint8_t triple[16];
};

/** The patterns, in the order the panel lists them. */
extern const PickPattern PICK_PATTERNS[];
extern const int NUM_PICK_PATTERNS;

/** The treble string each finger plays. The thumb's string is the shape's bass string. */
static const int FINGER_STRING[NUM_FINGERS] = {-1, 2, 1, 0};
/** The strings the thumb may play the bass on: the fourth to the sixth. */
static const int BASS_STRING_FIRST = 3;


/** THE SETTINGS, each from nought to one and a half: one is the most a player would do, and the
half beyond it exaggerates, so the module's knobs make plain what each does. Accent at nought
strikes every note alike; at a half, the levels the patterns were written for; at one and a half,
three times that contrast. The three variations at nought play the pattern exactly as written,
with the best shape for each chord. */
struct PickVariation {
	float accent = 0.5f;
	float voicing = 0.f;
	float picking = 0.f;
	float ornament = 0.f;
	/** THE STYLE, nought pop to one jazz. Toward jazz a chord written plain gains its seventh, and
	further its ninth and on a dominant its thirteenth; the hand leaves the open strings, sits
	higher on the neck, puts colour tones rather than the root or the plain fifth on the treble
	strings, avoids doubling a note, and moves each treble string as little as it can from one chord
	to the next. At nought, as the folk patterns were written. */
	float jazz = 0.f;
	/** HOW MANY OF THE THUMB'S NOTES ARE PALM-MUTED, nought to one: the side of the picking
	hand resting on the bass strings, so the bass thumps under ringing treble. Each thumb note is
	muted as often as this says, the first beat of the bar only in the upper half. */
	float palm = 0.f;
};


/** WHERE A STEP FALLS, as the cable says it. */
struct PickContext {
	/** The step, an eighth note, counted from the start of the bar, and the bar's length. */
	int step = 0;
	int barBeats = 4;
	/** The step counted from the start of the chart, and the seed with the pass mixed in: what
	every variation is drawn from. */
	int64_t clock = 0;
	uint32_t seed = 0;
	/** The first step of a new chord. */
	bool chordStart = false;
	/** Steps until the next chord, and its bass as a pitch class; -1 where nothing is known. */
	int stepsToChange = -1;
	int nextBass = -1;
	/** Steps until the end of the phrase; -1 where the chart has no phrases. */
	int phraseStepsLeft = -1;
	/** THE SHAPE THE HAND WILL TAKE AT THE NEXT CHANGE, from FingerPicker::preview, or null. On
	the last step before the change, a treble string it frets differently is played as it has it. */
	const Shape* nextShape = nullptr;
	/** WHICH PHRASE THIS IS, as a number the picking and ornament variations are drawn from in
	place of the step: the same in every bar of the phrase, so a varied figure is played in each
	of its bars, and the same for a phrase that returns in the form, so it returns with it. */
	uint32_t motif = 0;
	/** The key's scale, a bit for each pitch class; nought where it is not known. */
	int scaleMask = 0;
};


/** THE BEST SHAPES FOR FINGER PICKING: a chord tone on each of the three treble strings, the
chord's bass on one of the three bass strings and lower than all of them, the other bass strings
left out, and every essential tone of the chord somewhere among the four.

Preferred, in order of score: the lowest stopped fret near `nearFret`, open strings in the open
position, fewer fingers, a deeper bass string, and — with `previous` — a shape the hand need hardly
move to reach. Writes up to `max` of them, best first, with their scores, and returns how many. */
int pickingShapes(const ChordNotes& chord, const Tuning& t, const Hand& hand, int nearFret,
	const Shape* previous, Shape* out, float* scores, int max, float jazz = 0.f);

/** THE CHORD AS A JAZZ PLAYER HEARS IT, as far as `jazz` says. From a quarter of the way, a triad
gains its seventh — a major seventh, or on a dominant, `dominant`, a flat one; a minor seventh on a
minor triad — and the seventh is required. From 0.6, a chord with a seventh gains its ninth, and a
dominant its thirteenth, as tones the hand may use but need not. A chord the chart already colours
keeps what it has; a diminished or augmented chord is left alone. */
ChordNotes jazzTones(const ChordNotes& chord, float jazz, bool dominant);

/** The best of them. Returns false if none is playable. */
bool bestPickingShape(const ChordNotes& chord, const Tuning& t, const Hand& hand, int nearFret,
	const Shape* previous, Shape* out);


/** A NOTE THE PICKER PLAYS: the string, the fret, the MIDI note they make, the finger, how hard,
how it is sounded, and how far into its step it falls, as a fraction of the step. */
struct PickedNote {
	int string = 0;
	int fret = 0;
	int note = 0;
	int finger = THUMB;
	float level = 0.7f;
	uint32_t technique = 0;
	float delay = 0.f;
};

/** The most notes one step can give: a pinch of all four fingers, or an ornament's two. */
static const int MAX_STEP_NOTES = 8;


/** BOTH HANDS TOGETHER: a chord in, a shape held, and the notes of each step out. */
struct FingerPicker {
	Tuning tuning;
	Hand hand;
	/** The fret the hand prefers to play near; nought is the open position. */
	int position = 0;
	int pattern = 0;
	PickVariation variation;

	FingerPicker() { hand.barreOverMuted = true; }

	/** A NEW CHORD, or the same one with the position changed: the hand moves to the best shape
	for it, staying near the shape it holds. A chord heard before may be held a different way
	from the last time, as often as Voicing says, drawn from `chance`, nought to one. Returns
	false, and holds nothing, if no shape will do. */
	bool setChord(const ChordNotes& chord, float chance = 1.f);
	/** THE SHAPE `setChord` WOULD TAKE for this chord with this chance, without taking it: the
	same answer it will give, asked a step early. */
	bool preview(const ChordNotes& chord, float chance, Shape* out) const;
	/** THE SAME CHORD INTO A NEW BAR: the hand may take another shape for it, less often than for
	a returning chord. Returns true if it moved. */
	bool revoice(float chance);

	bool holding() const { return have; }
	const Shape& shape() const { return held; }
	const ChordNotes& chord() const { return notes; }

	/** The notes of one step. Returns how many, at most MAX_STEP_NOTES. */
	int notesAt(const PickContext& at, PickedNote* out) const;
	/** The pattern as written, at the bar's step: no context, no variation. */
	int notesAt(int step, int barBeats, PickedNote* out) const;

private:
	bool have = false;
	Shape held;
	ChordNotes notes;

	/** THE LAST SHAPE EACH CHORD WAS HELD IN, so a returning chord can be held differently. */
	static const int MEMORY = 16;
	struct Remembered {
		uint32_t chord = 0;
		Shape shape;
	};
	Remembered memory[MEMORY];
	int remembered = 0;

	void remember(const ChordNotes& chord, const Shape& shape);
	bool choose(const ChordNotes& chord, float chance, Shape* out) const;
	/** How far below the best a different shape may score, by the Voicing setting. */
	float margin() const;

	float levelFor(const PickContext& at, bool thumb) const;
	/** The pattern's fingers at a step of the bar, as written. */
	uint8_t writtenAt(int step, int barBeats) const;
	/** The fingers a step plays, the phrase's figure applied, given what the step before played;
	`altBass` is set where the thumb is to take the other bass note. */
	uint8_t figureAt(const PickContext& at, uint8_t before, bool* altBass) const;
	/** A note for the thumb on another bass string than the shape's: the chord's fifth, or else its
	root, within the hand's reach. False if there is none. */
	bool alternateBass(PickedNote* out) const;
	/** Palm-mutes the thumb's notes among `n`, as often as the setting says. */
	void palmMute(const PickContext& at, PickedNote* out, int n) const;
	/** Where to play a passing note of the bass run: a bass string and fret near the hand. */
	bool placeBass(int note, PickedNote* out) const;
	int bassRun(const PickContext& at, PickedNote* out) const;
	/** A hammer-on or a pull-off into the note `target`, written as two notes into `out`. Returns
	how many: two, or one, the target as it was, where neither will do. */
	int ornament(const PickedNote& target, float chance, int scaleMask, PickedNote* out) const;
};


/** A NUMBER FROM NOUGHT TO ONE, the same every time it is asked for the same seed, clock and
purpose. `salt` keeps one purpose's chances apart from another's. */
float pickChance(uint32_t seed, int64_t clock, uint32_t salt);


} // namespace px
