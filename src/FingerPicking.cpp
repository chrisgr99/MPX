#include "FingerPicking.hpp"
#include <algorithm>
#include <initializer_list>
#include <cstdlib>

namespace px {


/** THE FOLK PATTERNS, an eighth note a step, a step with no fingers a rest.

FORWARD ROLL. In a bar of four, the thumb on the first and third beats and the fingers rolling up
the treble strings and back between: thumb, index, middle, ring, thumb, ring, middle, index. In a
bar of three, the roll as it is usually written: thumb, index, middle, ring, middle, index.

PINCH AND ROLL. The thumb and the ring finger together on the first and third beats, then index,
middle, index. In a bar of three, the pinch on the first beat, then index, middle, index, middle,
index.

THUMB AND PINCH. The bass alone on each beat, and the middle and ring fingers together on the
first and second strings between.

BROKEN ARPEGGIO. The bass, then each treble string upward once, a beat each: thumb, index, middle,
ring on the four beats of a bar of four. In a bar of three, the bass on the first beat and the
three fingers in eighths after it.

ARPEGGIO IN EIGHTHS. The same upward figure an eighth note a step: thumb, index, middle, ring
twice in a bar of four, the thumb on the first and third beats. It goes straight up each time,
where the forward roll comes back down. In a bar of three, up and back once: thumb, index, middle,
ring, middle, index. */
const PickPattern PICK_PATTERNS[] = {
	{"Forward roll",
		8, {F_T, F_I, F_M, F_R, F_T, F_R, F_M, F_I},
		6, {F_T, F_I, F_M, F_R, F_M, F_I}},
	{"Pinch and roll",
		8, {F_T | F_R, F_I, F_M, F_I, F_T | F_R, F_I, F_M, F_I},
		6, {F_T | F_R, F_I, F_M, F_I, F_M, F_I}},
	{"Thumb and pinch",
		8, {F_T, F_M | F_R, F_T, F_M | F_R, F_T, F_M | F_R, F_T, F_M | F_R},
		6, {F_T, F_M | F_R, F_T, F_M | F_R, F_T, F_M | F_R}},
	{"Broken arpeggio",
		8, {F_T, 0, F_I, 0, F_M, 0, F_R, 0},
		6, {F_T, 0, F_I, F_M, F_R, 0}},
	{"Arpeggio in eighths",
		8, {F_T, F_I, F_M, F_R, F_T, F_I, F_M, F_R},
		6, {F_T, F_I, F_M, F_R, F_M, F_I}},
};
const int NUM_PICK_PATTERNS = (int) (sizeof(PICK_PATTERNS) / sizeof(PICK_PATTERNS[0]));


static int pitchClassOf(int note) {
	return ((note % 12) + 12) % 12;
}


/** WHERE THE JAZZ HAND GOES, and what it wants on the treble strings, at the Style knob's top:
the fifth fret, unless the Position knob asks for higher; a colour tone, the seventh, the plain
fifth and the root, each counted for or against; a doubled note against; and each semitone a
treble string moves from the shape before, against. */
static const float JAZZ_POSITION = 5.f;
static const float JAZZ_COLOUR_TONE = 2.f, JAZZ_SEVENTH_TONE = 1.f, JAZZ_FIFTH_TONE = -1.5f,
	JAZZ_ROOT_TONE = -1.f, JAZZ_DOUBLED = -1.5f, JAZZ_MOVE = -0.4f, JAZZ_OPEN = -2.f;
/** From where on the Style knob a triad gains its seventh, and a seventh its colours. */
static const float JAZZ_SEVENTH = 0.25f, JAZZ_COLOURS = 0.6f;


static int degreeOf(const ChordNotes& c, int pc) {
	for (int i = 0; i < c.count; i++)
		if (c.pc[i] == pc)
			return c.degree[i];
	return 0;
}


/** HOW GOOD A PICKING SHAPE IS, higher better. The same terms as a full shape's, without the
preference for more strings, since the strings are fixed; and with two of its own: a deeper bass
string, and how little the hand moves from the shape it holds. An open string counts for a shape
in the open position and neither for nor against one further up: picked open strings ringing among
stopped notes high on the neck are a sound of finger-style guitar, where a strummed chord would
only be muddied by them. Toward jazz the open strings count against, the hand is drawn up the neck,
and the treble strings' tones, doublings and movement are weighed as JAZZ_ says. */
static float pickingScore(const Shape& shape, const Grip& g, int bassString, int nearFret,
	const Shape* previous, const Tuning& t, const ChordNotes& chord, float jazz) {
	const float openWeight = (g.highFret <= 3 ? 3.f : 0.f) * (1.f - jazz) + JAZZ_OPEN * jazz;
	float score = openWeight * g.open - 2.f * g.fingers;
	if (g.splitIndex)
		score -= 6.f;
	if (g.highFret - g.lowFret >= 3)
		score -= 3.f;
	const float target = std::fmax((float) nearFret, JAZZ_POSITION * jazz);
	score -= 4.f * std::fabs((float) g.lowFret - target);
	score += 1.f * (bassString - BASS_STRING_FIRST);
	if (jazz > 0.f) {
		int seen = 0;
		for (int s = 0; s < t.strings; s++) {
			if (!shape.sounds(s))
				continue;
			const int pc = ((shape.note(t, s) % 12) + 12) % 12;
			if (seen & (1 << pc))
				score += JAZZ_DOUBLED * jazz;
			seen |= 1 << pc;
			if (s >= BASS_STRING_FIRST)
				continue;
			const int d = degreeOf(chord, pc);
			if (d == 6 || d == 9 || d == 11 || d == 13)
				score += JAZZ_COLOUR_TONE * jazz;
			else if (d == 7)
				score += JAZZ_SEVENTH_TONE * jazz;
			else if (d == 5)
				score += JAZZ_FIFTH_TONE * jazz;
			else if (d == 1)
				score += JAZZ_ROOT_TONE * jazz;
			if (previous && previous->sounds(s))
				score += JAZZ_MOVE * jazz * std::abs(shape.note(t, s) - previous->note(t, s));
		}
	}
	if (previous) {
		// Each played string whose fret changes is a finger lifted or put down.
		for (int s = 0; s < t.strings; s++)
			if (shape.sounds(s) && previous->sounds(s) && shape.fret[s] != previous->fret[s])
				score -= 1.f;
		int low = 0;
		for (int s = 0; s < t.strings; s++)
			if (previous->fret[s] > 0 && (low == 0 || previous->fret[s] < low))
				low = previous->fret[s];
		if (low > 0 && g.lowFret > 0)
			score -= 1.f * std::abs(g.lowFret - low);
	}
	return score;
}


int pickingShapes(const ChordNotes& chord, const Tuning& t, const Hand& hand, int nearFret,
	const Shape* previous, Shape* out, float* scores, int max, float jazz) {
	int n = 0;
	searchShapes(t, hand,
		[&](int s, int f) {
			const bool bassString = s >= BASS_STRING_FIRST;
			if (f == Shape::MUTED)
				return bassString;
			const int pc = pitchClassOf(t.open[s] + f);
			return bassString ? pc == chord.bass : chord.contains(pc);
		},
		[&](const Shape& shape, const Grip& g) {
			// One bass string, the thumb's.
			int bass = -1;
			for (int s = BASS_STRING_FIRST; s < t.strings; s++) {
				if (!shape.sounds(s))
					continue;
				if (bass >= 0)
					return;
				bass = s;
			}
			if (bass < 0)
				return;
			const int bassNote = shape.note(t, bass);
			for (int s = 0; s < BASS_STRING_FIRST; s++)
				if (shape.note(t, s) <= bassNote)
					return;
			for (int i = 0; i < chord.count; i++) {
				if (!chord.essential[i])
					continue;
				bool present = false;
				for (int s = 0; s < t.strings; s++)
					if (shape.sounds(s) && pitchClassOf(shape.note(t, s)) == chord.pc[i])
						present = true;
				if (!present)
					return;
			}
			const float score = pickingScore(shape, g, bass, nearFret, previous, t, chord, jazz);
			// Kept best first: in at its place, the worst falling off the end when full.
			if (n == max && score <= scores[n - 1])
				return;
			int at = n < max ? n++ : n - 1;
			while (at > 0 && scores[at - 1] < score) {
				out[at] = out[at - 1];
				scores[at] = scores[at - 1];
				at--;
			}
			out[at] = shape;
			scores[at] = score;
		});
	return n;
}


bool bestPickingShape(const ChordNotes& chord, const Tuning& t, const Hand& hand, int nearFret,
	const Shape* previous, Shape* out) {
	float score;
	return pickingShapes(chord, t, hand, nearFret, previous, out, &score, 1) > 0;
}


ChordNotes jazzTones(const ChordNotes& in, float jazz, bool dominant) {
	ChordNotes c = in;
	if (jazz < JAZZ_SEVENTH || c.count == 0)
		return c;
	int root = c.pc[0];
	for (int i = 0; i < c.count; i++)
		if (c.degree[i] == 1)
			root = c.pc[i];
	auto has = [&](int interval) { return c.contains((root + interval) % 12); };
	auto add = [&](int interval, int degree, bool essential) {
		if (has(interval) || c.count >= ChordNotes::MAX)
			return;
		c.pc[c.count] = (root + interval) % 12;
		c.degree[c.count] = (int8_t) degree;
		c.essential[c.count] = essential;
		c.count++;
	};
	const bool major = has(4), minor = has(3) && !major, fifth = has(7);
	const bool seventh = has(10) || has(11) || (has(9) && degreeOf(c, (root + 9) % 12) == 6);
	if (!fifth)
		return c;      // diminished, augmented or suspended without a fifth: left as written
	if (!seventh) {
		if (major)
			add(dominant ? 10 : 11, 7, true);
		else if (minor)
			add(10, 7, true);
	}
	if (jazz >= JAZZ_COLOURS && (major || minor)) {
		// The ninth, where no altered ninth is there already; a dominant's thirteenth too.
		if (!has(1) && !(major && has(3)))
			add(2, 9, false);
		if (major && has(10))
			add(9, 13, false);
	}
	return c;
}


float pickChance(uint32_t seed, int64_t clock, uint32_t salt) {
	uint64_t x = (uint64_t) seed * 0x9E3779B97F4A7C15ull
		^ (uint64_t) clock * 0xC2B2AE3D27D4EB4Full
		^ ((uint64_t) salt + 1) * 0x165667B19E3779F9ull;
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;
	return (float) (x >> 40) / 16777216.f;
}


// ---- the settings' weights ---------------------------------------------------------------------

/** How far below the best shape's score another may be and still be played, at Voicing's bottom
and added at its top: a little further up the neck, or a little more awkward, the more variety is
asked for. */
static const float VOICING_MARGIN_MIN = 6.f;
static const float VOICING_MARGIN_SPAN = 18.f;
/** At Voicing's top, how often a returning chord is held another way, and how often the same
chord into a new bar is. */
static const float VOICING_RETURN = 0.8f;
static const float VOICING_BAR = 0.35f;
/** At Picking's top, how often a step is changed. */
static const float PICKING_RATE = 0.35f;
/** At Ornament's top, how often a single treble note is hammered or pulled into, and how often a
change of bass is approached by a run. */
static const float ORNAMENT_RATE = 0.3f;
static const float RUN_RATE = 0.7f;
/** THE FILL IN A PHRASE'S LAST BAR: steps the phrase's figure leaves alone are changed there as
often as this much of the figure's own chance, on top of the figure. */
static const float FILL_RATE = 0.6f;
/** A bass run is likelier into the phrase's last change, where a player leads into what follows. */
static const float RUN_PHRASE_END_WEIGHT = 1.6f;

enum : uint32_t { SALT_PICK = 1, SALT_PICK_KIND, SALT_ORNAMENT, SALT_ORNAMENT_KIND, SALT_RUN,
	SALT_PALM, SALT_PICK_FILL, SALT_PICK_FILL_KIND, SALT_ORNAMENT_FILL, SALT_ORNAMENT_FILL_KIND };


/** A CHANCE FOR THIS STEP OF THE BAR IN THIS PHRASE: the same in each of the phrase's bars. */
static float motifChance(const PickContext& at, uint32_t salt) {
	return pickChance(at.seed ^ (at.motif * 0x9E3779B1u + 0x7F4A7C15u), at.step, salt);
}

static bool lastBarOfPhrase(const PickContext& at) {
	return at.phraseStepsLeft >= 0 && at.phraseStepsLeft <= at.barBeats * 2;
}

/** WHETHER THIS STEP IS VARIED, and the chance that says how: by the phrase's figure, or else by
the fill in its last bar. */
static bool variedStep(const PickContext& at, float rate, uint32_t salt, uint32_t kindSalt,
	uint32_t fillSalt, uint32_t fillKindSalt, float* kind) {
	if (rate <= 0.f)
		return false;
	if (motifChance(at, salt) < rate) {
		*kind = motifChance(at, kindSalt);
		return true;
	}
	if (lastBarOfPhrase(at) && motifChance(at, fillSalt) < rate * FILL_RATE) {
		*kind = motifChance(at, fillKindSalt);
		return true;
	}
	return false;
}


static bool sameShape(const Shape& a, const Shape& b, const Tuning& t) {
	for (int s = 0; s < t.strings; s++)
		if (a.fret[s] != b.fret[s])
			return false;
	return true;
}

static uint32_t chordKey(const ChordNotes& c) {
	uint32_t mask = 0;
	for (int i = 0; i < c.count; i++)
		mask |= 1u << c.pc[i];
	return mask | ((uint32_t) c.bass << 12) | (1u << 20);
}

/** ONE OF THE NEAR-BEST SHAPES OTHER THAN `avoid`, chosen by `u`, nought to one. */
static bool otherShape(const Shape* shapes, const float* scores, int n, const Shape& avoid,
	float u, float margin, const Tuning& t, Shape* out) {
	int choices[16];
	int count = 0;
	for (int i = 0; i < n && count < 16; i++)
		if (scores[i] >= scores[0] - margin && !sameShape(shapes[i], avoid, t))
			choices[count++] = i;
	if (count == 0)
		return false;
	*out = shapes[choices[std::min(count - 1, (int) (u * (float) count))]];
	return true;
}


void FingerPicker::remember(const ChordNotes& chord, const Shape& shape) {
	const uint32_t key = chordKey(chord);
	for (int i = 0; i < remembered; i++)
		if (memory[i].chord == key) {
			memory[i].shape = shape;
			return;
		}
	// Full: the oldest goes.
	if (remembered == MEMORY) {
		for (int i = 1; i < MEMORY; i++)
			memory[i - 1] = memory[i];
		remembered--;
	}
	memory[remembered].chord = key;
	memory[remembered].shape = shape;
	remembered++;
}


static const int CANDIDATES = 12;

float FingerPicker::margin() const {
	return VOICING_MARGIN_MIN + VOICING_MARGIN_SPAN * variation.voicing;
}

bool FingerPicker::choose(const ChordNotes& chord, float chance, Shape* out) const {
	Shape shapes[CANDIDATES];
	float scores[CANDIDATES];
	const int n = pickingShapes(chord, tuning, hand, position, have ? &held : nullptr, shapes,
		scores, CANDIDATES, variation.jazz);
	if (n == 0)
		return false;
	Shape choice = shapes[0];
	// A CHORD HEARD BEFORE, held another way from the last time.
	const float threshold = variation.voicing * VOICING_RETURN;
	if (chance < threshold) {
		const uint32_t key = chordKey(chord);
		for (int i = 0; i < remembered; i++)
			if (memory[i].chord == key) {
				otherShape(shapes, scores, n, memory[i].shape, chance / threshold, margin(),
					tuning, &choice);
				break;
			}
	}
	*out = choice;
	return true;
}


bool FingerPicker::preview(const ChordNotes& chord, float chance, Shape* out) const {
	return choose(chord, chance, out);
}


bool FingerPicker::setChord(const ChordNotes& chord, float chance) {
	Shape choice;
	if (!choose(chord, chance, &choice)) {
		have = false;
		return false;
	}
	held = choice;
	notes = chord;
	have = true;
	remember(chord, held);
	return true;
}


bool FingerPicker::revoice(float chance) {
	const float threshold = variation.voicing * VOICING_BAR;
	if (!have || chance >= threshold)
		return false;
	Shape shapes[CANDIDATES];
	float scores[CANDIDATES];
	const int n = pickingShapes(notes, tuning, hand, position, &held, shapes, scores, CANDIDATES,
		variation.jazz);
	Shape choice;
	if (!otherShape(shapes, scores, n, held, chance / threshold, margin(), tuning, &choice))
		return false;
	held = choice;
	remember(notes, held);
	return true;
}


/** HOW HARD A STEP IS STRUCK. At Accent's middle: the thumb on the first beat of the bar at 0.95,
the thumb elsewhere at 0.8, a finger on a beat at 0.65 and off it at 0.55. Accent scales every
level's distance from 0.72, so nought plays them all alike and one doubles the contrast. A new
chord is leaned on a little, and the last beat of a phrase eased off. */
float FingerPicker::levelFor(const PickContext& at, bool thumb) const {
	const float target = thumb ? (at.step == 0 ? 0.95f : 0.8f) : (at.step % 2 == 0 ? 0.65f : 0.55f);
	const float a = 2.f * variation.accent;
	float level = 0.72f + (target - 0.72f) * a;
	if (at.chordStart)
		level += 0.05f * a;
	if (at.phraseStepsLeft >= 0 && at.phraseStepsLeft <= 2)
		level *= 1.f - 0.1f * a;
	return std::max(0.15f, std::min(1.f, level));
}


static bool inScale(int note, int scaleMask) {
	return scaleMask == 0 || ((scaleMask >> pitchClassOf(note)) & 1) != 0;
}


bool FingerPicker::alternateBass(PickedNote* out) const {
	const int bs = held.bassString(tuning);
	if (bs < 0)
		return false;
	int lowestTreble = 999;
	for (int s = 0; s < BASS_STRING_FIRST; s++)
		if (held.sounds(s))
			lowestTreble = std::min(lowestTreble, held.note(tuning, s));
	const Grip g = gripOf(held, tuning, hand);
	const int low = g.lowFret > 0 ? g.lowFret : 1;
	const int top = low + hand.stretch - 1;
	// The fifth first, as an alternating bass has it, and the root where there is no fifth.
	int targets[2];
	int count = 0;
	if (notes.contains((notes.bass + 7) % 12))
		targets[count++] = (notes.bass + 7) % 12;
	if (notes.count > 0 && notes.pc[0] != notes.bass)
		targets[count++] = notes.pc[0];
	for (int k = 0; k < count; k++)
		for (int d = 1; d < tuning.strings; d++)
			for (int sign = -1; sign <= 1; sign += 2) {
				const int s = bs + sign * d;
				if (s < BASS_STRING_FIRST || s >= tuning.strings)
					continue;
				for (int f = 0; f <= top; f++) {
					if (f > 0 && f < low)
						continue;
					const int note = tuning.open[s] + f;
					if (pitchClassOf(note) != targets[k] || note >= lowestTreble)
						continue;
					Shape trial = held;
					trial.fret[s] = f;
					if (!gripOf(trial, tuning, hand).playable)
						continue;
					out->string = s;
					out->fret = f;
					out->note = note;
					return true;
				}
			}
	return false;
}


bool FingerPicker::placeBass(int note, PickedNote* out) const {
	const Grip g = gripOf(held, tuning, hand);
	const int low = g.lowFret > 0 ? g.lowFret : 1;
	const int top = low + hand.stretch - 1;
	int best = -1, bestFret = 0, bestCost = 999;
	for (int s = BASS_STRING_FIRST; s < tuning.strings; s++) {
		const int f = note - tuning.open[s];
		if (f < 0 || f > hand.highestFret)
			continue;
		if (f != 0 && (f < low - 1 || f > top))
			continue;
		const int cost = f == 0 ? 0 : 1 + std::abs(f - low);
		if (cost < bestCost) {
			best = s;
			bestFret = f;
			bestCost = cost;
		}
	}
	if (best < 0)
		return false;
	out->string = best;
	out->fret = bestFret;
	out->note = note;
	return true;
}


/** A scale step from `note` in direction `dir`, a semitone or two. */
static int scaleStep(int note, int dir, int scaleMask) {
	for (int k = 1; k <= 2; k++)
		if (scaleMask != 0 && inScale(note + dir * k, scaleMask))
			return note + dir * k;
	return note + dir * 2;
}


/** A BASS RUN INTO THE NEXT CHORD: on the last two steps before a change of bass, the thumb walks
to the new bass a scale step at a time, from the side the old one is on — G, A, B into C; C, B,
A into G. Decided once for each change, so both steps agree. */
int FingerPicker::bassRun(const PickContext& at, PickedNote* out) const {
	if (variation.ornament <= 0.f || at.stepsToChange < 1 || at.stepsToChange > 2
			|| at.nextBass < 0 || at.nextBass == notes.bass)
		return 0;
	const float weight = at.phraseStepsLeft >= 0
		&& at.phraseStepsLeft <= at.barBeats * 2 + at.stepsToChange ? RUN_PHRASE_END_WEIGHT : 1.f;
	if (pickChance(at.seed, at.clock + at.stepsToChange, SALT_RUN)
			>= variation.ornament * RUN_RATE * weight)
		return 0;
	const int bs = held.bassString(tuning);
	if (bs < 0)
		return 0;
	const int bassNote = held.note(tuning, bs);
	int d = ((at.nextBass - pitchClassOf(bassNote)) % 12 + 12) % 12;
	if (d > 6)
		d -= 12;
	int target = bassNote + d;
	while (target < tuning.open[tuning.strings - 1])
		target += 12;
	const int dir = target > bassNote ? -1 : 1;
	const int one = scaleStep(target, dir, at.scaleMask);
	const int note = at.stepsToChange == 1 ? one : scaleStep(one, dir, at.scaleMask);
	if (note == bassNote)
		return 0;
	PickedNote& n = out[0];
	n = PickedNote();
	if (!placeBass(note, &n))
		return 0;
	n.finger = THUMB;
	n.level = levelFor(at, true);
	return 1;
}


int FingerPicker::ornament(const PickedNote& target, float chance, int scaleMask,
	PickedNote* out) const {
	const PickedNote aim = target;
	const int s = aim.string, f = aim.fret;
	const Grip g = gripOf(held, tuning, hand);
	const int low = g.lowFret > 0 ? g.lowFret : 1;
	const int top = low + hand.stretch - 1;
	auto usable = [&](int note) {
		return inScale(note, scaleMask) || notes.contains(pitchClassOf(note));
	};
	// HAMMERED ON from the open string to a note in the first four frets, or from two frets below
	// further up; PULLED OFF from a scale step above, within the hand's reach.
	int from = -1;
	if (f >= 1 && f <= 4 && usable(tuning.open[s]))
		from = 0;
	else if (f > 4 && f - 2 >= low - 1 && usable(tuning.open[s] + f - 2))
		from = f - 2;
	int above = -1;
	for (int k = 2; k >= 1; k--)
		if (f + k <= top && usable(tuning.open[s] + f + k) && above < 0)
			above = f + k;
	// AS OFTEN UP AS DOWN. A note on an open string can only be pulled off to, never hammered
	// on to, so where only a pull-off will do it is played half the time, and where both will
	// the hammer-on is the likelier; the ornaments then rise about as often as they fall.
	const bool hammer = from >= 0 && (above < 0 || chance < 0.65f);
	const bool pull = !hammer && above >= 0 && (from >= 0 || chance < 0.5f);
	out[0] = aim;
	if (!hammer && !pull)
		return 1;
	out[0].fret = hammer ? from : above;
	out[0].note = tuning.open[s] + out[0].fret;
	out[0].technique = 0;
	out[0].delay = 0.f;
	out[1] = aim;
	out[1].technique = hammer ? PT_HAMMER_ON : PT_PULL_OFF;
	// A FULL STEP LATER, where the next step falls: the first note is heard as a note. Half a
	// step, a sixteenth, made it a clipped grace note.
	out[1].delay = 1.f;
	return 2;
}


void FingerPicker::palmMute(const PickContext& at, PickedNote* out, int n) const {
	if (variation.palm <= 0.f)
		return;
	// A FIGURE, as the other variations are: the same steps muted in every bar of the phrase, so
	// the muting is a way of playing and not a note choked here and there. The first beat of the
	// bar is the last to be muted: only in the setting's upper half.
	const float chance = motifChance(at, SALT_PALM);
	const float threshold = at.step == 0 ? 2.f * variation.palm - 1.f : variation.palm;
	if (chance >= threshold)
		return;
	for (int i = 0; i < n; i++)
		if (out[i].finger == THUMB)
			out[i].technique |= PT_PALM_MUTE;
}


uint8_t FingerPicker::writtenAt(int step, int barBeats) const {
	const PickPattern& p = PICK_PATTERNS[(pattern % NUM_PICK_PATTERNS + NUM_PICK_PATTERNS)
		% NUM_PICK_PATTERNS];
	const bool triple = barBeats > 0 && barBeats % 3 == 0;
	const int steps = triple ? p.tripleSteps : p.evenSteps;
	return (triple ? p.triple : p.even)[((step % steps) + steps) % steps];
}


/** THE FINGERS ONE STEP PLAYS: the pattern's, changed by the phrase's figure.

NEVER A STRING PLAYED A MOMENT BEFORE OR AFTER. A finger the figure adds or moves goes only to a
string that neither the step before, as played, nor the step after, as written, plays: a pinch
whose second note is the very note picked again half a beat later sounds like the first one cut
short. Where no finger is free the step is played as written. The first step of a bar or of a chord
keeps its bass note. */
uint8_t FingerPicker::figureAt(const PickContext& at, uint8_t before, bool* altBass) const {
	*altBass = false;
	uint8_t fingers = writtenAt(at.step, at.barBeats);
	float kind = 0.f;
	if (!variedStep(at, variation.picking * PICKING_RATE, SALT_PICK, SALT_PICK_KIND, SALT_PICK_FILL,
			SALT_PICK_FILL_KIND, &kind))
		return fingers;
	const uint8_t busy = (uint8_t) (before | writtenAt(at.step + 1, at.barBeats));
	// The first of `order` that is free and not already playing, or nought.
	auto freeOf = [&](std::initializer_list<int> order, uint8_t playing) -> uint8_t {
		for (int f : order)
			if (!((busy | playing) & (1 << f)))
				return (uint8_t) (1 << f);
		return 0;
	};
	const bool anchor = at.chordStart || at.step == 0;
	if (fingers & F_T) {
		// The thumb to the other bass note, or a finger joining it in a pinch.
		if (!anchor && kind < 0.5f)
			*altBass = true;
		else
			fingers |= kind < 0.75f ? freeOf({RING, MIDDLE, INDEX}, fingers)
				: freeOf({MIDDLE, RING, INDEX}, fingers);
	}
	else if (fingers) {
		if (kind < 0.3f) {
			// Left out, the strings already picked ringing through.
			fingers = 0;
		}
		else if (kind < 0.65f) {
			// Another finger, on the next string, where one is free.
			uint8_t moved = 0;
			for (int f = INDEX; f <= RING; f++) {
				if (!(fingers & (1 << f)))
					continue;
				uint8_t to = 0;
				if (f == INDEX || f == RING)
					to = freeOf({MIDDLE}, moved);
				else
					to = kind < 0.47f ? freeOf({INDEX, RING}, moved) : freeOf({RING, INDEX}, moved);
				moved |= to ? to : (uint8_t) (1 << f);
			}
			fingers = moved;
		}
		else {
			// A pinch: the thumb with it on a beat, a finger beside it off one.
			if (at.step % 2 == 0 && !(busy & F_T))
				fingers |= F_T;
			else
				fingers |= (fingers & F_R) ? freeOf({MIDDLE, INDEX}, fingers)
					: (fingers & F_M) ? freeOf({RING, INDEX}, fingers)
					: freeOf({MIDDLE, RING}, fingers);
		}
	}
	else {
		// A rest in the pattern filled.
		fingers = kind < 0.5f ? freeOf({INDEX, MIDDLE, RING}, 0) : freeOf({MIDDLE, INDEX, RING}, 0);
	}
	return fingers;
}


int FingerPicker::notesAt(const PickContext& at, PickedNote* out) const {
	if (!have)
		return 0;

	const int run = bassRun(at, out);
	if (run > 0) {
		palmMute(at, out, run);
		return run;
	}

	// THE PICKING HAND'S CHANGES, the phrase's figure, worked out from the start of the bar so
	// each step knows what the step before it played.
	uint8_t before = 0;
	for (int k = 0; k < at.step; k++) {
		PickContext earlier = at;
		earlier.step = k;
		earlier.clock = at.clock - (at.step - k);
		earlier.chordStart = false;
		if (at.phraseStepsLeft >= 0)
			earlier.phraseStepsLeft = at.phraseStepsLeft + (at.step - k);
		bool unused;
		before = figureAt(earlier, before, &unused);
	}
	bool altBass = false;
	const uint8_t fingers = figureAt(at, before, &altBass);

	const float level = levelFor(at, (fingers & F_T) != 0);
	int n = 0;
	for (int f = 0; f < NUM_FINGERS; f++) {
		if (!(fingers & (1 << f)))
			continue;
		PickedNote note;
		note.finger = f;
		note.level = level;
		if (f == THUMB && altBass && alternateBass(&note)) {
			out[n++] = note;
			continue;
		}
		const int s = f == THUMB ? held.bassString(tuning) : FINGER_STRING[f];
		if (s < 0 || !held.sounds(s))
			continue;
		note.string = s;
		note.fret = held.fret[s];
		note.note = held.note(tuning, s);
		out[n++] = note;
	}

	// THE LAST EIGHTH BEFORE A CHANGE: a treble string the next shape frets differently is played
	// as the next shape has it, the hand already moving, so the note is not picked only to be cut
	// a moment later as the finger lifts. It then rings on into the new chord.
	bool anticipated = false;
	if (at.nextShape && at.stepsToChange == 1)
		for (int i = 0; i < n; i++) {
			PickedNote& p = out[i];
			if (p.finger == THUMB || p.string >= BASS_STRING_FIRST)
				continue;
			const int f = at.nextShape->fret[p.string];
			if (f < 0 || f == held.fret[p.string])
				continue;
			p.fret = f;
			p.note = tuning.open[p.string] + f;
			anticipated = true;
		}

	// THE ORNAMENT, on a step with one treble note.
	float how = 0.f;
	// Not where the pattern's next step picks the same string: the hammer-on or pull-off lands
	// on that step, and the string cannot be sounded twice there. Nor on the step before a
	// change, where it would land as the hand moves to the next shape.
	if (n == 1 && out[0].finger != THUMB && !anticipated && at.stepsToChange != 1
			&& !(writtenAt(at.step + 1, at.barBeats) & (1 << out[0].finger))
			&& variedStep(at, variation.ornament * ORNAMENT_RATE, SALT_ORNAMENT, SALT_ORNAMENT_KIND,
				SALT_ORNAMENT_FILL, SALT_ORNAMENT_FILL_KIND, &how))
		n = ornament(out[0], how, at.scaleMask, out);
	palmMute(at, out, n);
	return n;
}


int FingerPicker::notesAt(int step, int barBeats, PickedNote* out) const {
	PickContext at;
	at.step = step;
	at.barBeats = barBeats;
	FingerPicker plain = *this;
	plain.variation.picking = plain.variation.ornament = plain.variation.palm = 0.f;
	return plain.notesAt(at, out);
}


} // namespace px
