/** See Melodic.hpp. */
#include "Melodic.hpp"

#include <algorithm>
#include <cmath>

namespace px {

static int pitchClass(int note) {
	return ((note % 12) + 12) % 12;
}

static bool holds(const int* set, int count, int pc) {
	for (int i = 0; i < count; i++) {
		if (pitchClass(set[i]) == pc)
			return true;
	}
	return false;
}

static float intervalWeight(int distance) {
	if (distance < 0)
		distance = -distance;
	return distance < 13 ? INTERVAL_WEIGHTS[distance] : 0.f;
}

MelodyProfile melodyProfile(float smoothness, float chordLock, int low, int span) {
	const float sm = std::min(1.f, std::max(0.f, smoothness));
	const float cl = std::min(1.f, std::max(0.f, chordLock));
	MelodyProfile p;
	p.low = low;
	p.high = low + std::max(1, span);
	// SEVEN SEMITONES WHEN SMOOTH, FOURTEEN WHEN LEAPY. A window of a fifth keeps a line
	// singable; one of a ninth lets it jump about without letting it leave its register.
	p.window = (int) std::lround(7.f + (1.f - sm) * 7.f);
	p.leapAversion = 0.5f + sm * 1.5f;
	p.chordPull = 1.f + cl * 3.f;
	p.strongChordPull = 1.f + cl * 2.f;
	// AND THE OTHER SIDE OF THE SAME KNOB. Locking onto the chord means allowing less else;
	// at full lock a passing note is under a third as likely as it was.
	p.passing = 1.f - cl * 0.7f;
	return p;
}

int melodyScalePitchClasses(const Key& key, int which, int* out) {
	// INTERVALS FROM THE TONIC, which is what makes every one of these follow a transposition
	// of the chart without anything being recalculated.
	static const int MAJOR_PENT[5] = {0, 2, 4, 7, 9};
	static const int MINOR_PENT[5] = {0, 3, 5, 7, 10};
	static const int BLUES[6] = {0, 3, 5, 6, 7, 10};

	const int tonic = ((key.tonic % 12) + 12) % 12;
	const int* from = NULL;
	int count = 0;
	switch (which) {
		case SCALE_MAJOR_PENT: from = MAJOR_PENT; count = 5; break;
		case SCALE_MINOR_PENT: from = MINOR_PENT; count = 5; break;
		case SCALE_BLUES:      from = BLUES;      count = 6; break;
		default:
			// THE KEY'S OWN, major or minor, which the chart already knows how to give. FROM
			// CHORD lands here too: it is on the panel and in the saved patch and is not built,
			// and behaving as the key is the honest thing for it to do until it is.
			scalePitchClasses(key, out);
			return 7;
	}
	for (int i = 0; i < count; i++)
		out[i] = (tonic + from[i]) % 12;
	return count;
}

/** The nearest note of the palette to a starting point, searched outward. The answer when the
weighting has nothing to offer — a register with no scale note in it, or a window that excludes
everything — because a line that stops is worse than a line that repeats. */
static int nearestScaleNote(int from, const int* scale, int count, int low, int high) {
	const int start = std::min(high, std::max(low, from));
	for (int r = 0; r <= high - low; r++) {
		const int tries[2] = {start - r, start + r};
		for (int i = 0; i < 2; i++) {
			const int n = tries[i];
			if (n < low || n > high)
				continue;
			if (holds(scale, count, pitchClass(n)))
				return n;
		}
	}
	return start;
}

float melodyDraw(uint32_t chartSeed, uint32_t ownSeed, bool alone, int cycle,
		uint32_t epoch, uint32_t phrasesPerPass, uint32_t phrase, float intoPhrase) {
	const int n = cycle > 0 ? cycle : 1;
	const uint32_t position = (uint32_t) (((int) (epoch * (phrasesPerPass > 0 ? phrasesPerPass : 1)
		+ phrase)) % n);
	// To a ninety-sixth of a beat: finer than any grid a rhythm generator uses, and coarse enough
	// that a hair of jitter in when a note arrives does not change the note chosen.
	uint32_t x = (alone ? ownSeed * 7919u : chartSeed + ownSeed * 7919u)
		+ position * 104729u
		+ (uint32_t) (int32_t) std::lround(intoPhrase * 96.f) * 2654435761u;
	// A cheap integer hash: three rounds of shift and multiply, which is enough to turn a counter
	// into something that does not look like one.
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	x ^= x >> 16;
	return (float) (x >> 8) / 16777216.f;
}


/** HOW STRONGLY A LINE KEEPS ITS DIRECTION, calibrated against 456 jazz solos — see the comment
in melodicStep and `python3 test/contour.py`, which measures both. */
static const float CONTINUE_AFTER_STEP = 2.2f;
static const float TURN_AFTER_LEAP = 1.8f;
static const float RETURN_WEIGHT = 0.3f;
static const float CONTINUE_THROUGH_CHORD = 2.f;
/** A NEW CHORD ARRIVED AT BY STEP: its tones a step from the note before, twice and a half as
likely. */
static const float ARRIVE_BY_STEP = 2.5f;
/** A NOTE AND ITS OWN ALTERATION IN A ROW — A then A flat — a twelfth as likely. */
static const float CROSS_RELATION = 0.08f;
/** ANY NOTE OUTSIDE THE KEY, a third as likely: available when a borrowed chord calls for it, but
not the line's first choice, so the melody stays in its key while the harmony borrows. */
static const float OUTSIDE_KEY = 0.33f;
/** A NOTE TO STEER AWAY FROM, a seventh as likely. */
static const float AVOID_WEIGHT = 0.15f;

int melodicStep(const MelodyAsk& ask, const MelodyProfile& profile) {
	const int low = profile.low, high = profile.high;
	const float centre = (float) (low + high) / 2.f;
	const float halfSpan = std::max(1.f, (float) (high - low) / 2.f);
	const bool leadOn = ask.beatsToNext <= profile.leadWindow && ask.nextChordCount > 0;

	// A REGISTER HOLDS AT MOST A HANDFUL OF SCALE NOTES, and a very wide one still holds fewer
	// than this. Fixed rather than allocated, because this runs in the audio thread.
	static const int MAX_CANDIDATES = 128;
	int notes[MAX_CANDIDATES];
	float weights[MAX_CANDIDATES];
	int count = 0;
	float total = 0.f;

	// WHETHER ANY ANCHOR NOTE IS WITHIN REACH at all, so that an anchor-only choice never leaves
	// the line with nothing to play.
	bool anchorOnly = false;
	if (ask.anchorOnly && ask.anchorCount > 0) {
		for (int n = low; n <= high; n++) {
			if (ask.previous >= 0 && std::abs(n - ask.previous) > profile.window)
				continue;
			if (holds(ask.anchor, ask.anchorCount, pitchClass(n))) {
				anchorOnly = true;
				break;
			}
		}
	}

	for (int n = low; n <= high && count < MAX_CANDIDATES; n++) {
		const int pc = pitchClass(n);
		if (anchorOnly && !holds(ask.anchor, ask.anchorCount, pc))
			continue;
		// AN ANCHOR NEED NOT BE IN THE SCALE: the dominant's raised seventh in a minor key is not
		// in the natural minor the line is drawing from, and it is the note a half cadence wants.
		if (!anchorOnly && !holds(ask.scale, ask.scaleCount, pc))
			continue;
		// WITHIN REACH OF THE LAST NOTE. This is what makes a line rather than a sequence of
		// unrelated notes, and it is skipped on the first note because there is nothing to be
		// within reach of.
		if (ask.previous >= 0 && std::abs(n - ask.previous) > profile.window)
			continue;

		const bool isChordTone = holds(ask.chord, ask.chordCount, pc);
		float w = 1.f;
		if (ask.previous >= 0) {
			if (n == ask.previous && profile.unison >= 0.f)
				w *= profile.unison;
			else
				w *= std::pow(intervalWeight(n - ask.previous), profile.leapAversion);
			if (n < ask.previous)
				w *= profile.descendBias;
		}

		// WHICH WAY THE LINE IS GOING. With only the last note to go on, a line cannot tell
		// rising from falling, and with smoothness high the likeliest move is a step — either way
		// — so it rocked between neighbours: G F G F G. It turned round on 60 per cent of its
		// moves and went straight back to the note before on 39 per cent. In 456 transcribed jazz
		// solos those figures are 39 and 11, because real lines do three things this now does too:
		//
		// A STEP CARRIES ON the way it started — 64 per cent of the time after a step.
		// A LEAP TURNS BACK — 55 per cent of the time after a leap — filling in the gap it left.
		// AND A LINE SELDOM GOES STRAIGHT BACK to the note it has just left, which is a trill, not
		// a melody.
		if (ask.previous >= 0 && ask.beforePrevious >= 0) {
			const int last = ask.previous - ask.beforePrevious;
			const int move = n - ask.previous;
			if (last != 0 && move != 0) {
				const bool same = (last > 0) == (move > 0);
				if (std::abs(last) <= 2) {
					if (same)
						w *= CONTINUE_AFTER_STEP;
				}
				else if (std::abs(last) <= 4) {
					// A THIRD CARRIES ON THROUGH THE CHORD. A skip of a third to a chord tone is a
					// line moving through the harmony — F, A flat, B over D diminished in Michelle
					// — and it carries on the way it started, to the next chord tone or by step.
					// Counted as a leap, it was made to turn back, and a line could only ever touch
					// a chord's tones and retreat from them.
					if (same && std::abs(move) <= 4 && (isChordTone || std::abs(move) <= 2))
						w *= CONTINUE_THROUGH_CHORD;
				}
				else if (!same) {
					w *= TURN_AFTER_LEAP;
				}
			}
			if (n == ask.beforePrevious && n != ask.previous)
				w *= RETURN_WEIGHT;
		}
		// A NOTE AND ITS OWN ALTERATION IN A ROW, A to A flat. In Michelle a line in F moved from A
		// to A flat as the harmony moved to B flat 7, and to the ear the melody had slipped out of
		// tune: both notes are right for their chords, and the pair is what a singer avoids.
		// The note before that as well: A, G, then A flat is the same slip heard a moment later.
		if (ask.naturalOf && ask.previous >= 0) {
			const int a = pitchClass(ask.previous);
			if (ask.naturalOf[pc] == a || ask.naturalOf[a] == pc)
				w *= CROSS_RELATION;
			else if (ask.beforePrevious >= 0) {
				const int b = pitchClass(ask.beforePrevious);
				if (ask.naturalOf[pc] == b || ask.naturalOf[b] == pc)
					w *= CROSS_RELATION;
			}
		}
		// A NEW CHORD IS ARRIVED AT BY STEP. On the note that falls on a chord change, the new
		// chord's tones a step from the note before are favoured: A to G onto C, A flat to G onto
		// C, as Michelle arrives at each chord. A line that leapt onto every change never sounded as
		// though it was going anywhere.
		if (ask.arriveByStep && ask.previous >= 0 && isChordTone) {
			const int d = std::abs(n - ask.previous);
			if (d >= 1 && d <= 2)
				w *= ARRIVE_BY_STEP;
		}
		// AN ENDING MOVES, AND THE NOTE BEFORE IT DOES NOT LAND EARLY. A step into a held ending
		// on the pitch the ending then takes is one note struck twice, not a step.
		if (ask.moveOn && ask.previous >= 0 && n == ask.previous)
			w *= AVOID_WEIGHT;
		if (ask.avoidCount > 0 && holds(ask.avoid, ask.avoidCount, pc)
				&& !(ask.anchorCount > 0 && holds(ask.anchor, ask.anchorCount, pc)))
			w *= AVOID_WEIGHT;
		// A MELODY STAYS IN ITS KEY WHILE THE HARMONY BORROWS. A line over B flat 7 in F took A flat
		// as readily as any other note, and against the A natural of the bar before the melody
		// sounded out of tune. An anchor is exempt: an ending that needs the note gets it.
		// A BORROWED CHORD'S OWN TONES ARE NOT OUTSIDE ANYTHING: over D diminished in F, A flat and
		// B are the chord, and steering away from them sent the line to E, G and B flat instead —
		// in the key, and against the chord.
		if (ask.naturalOf && ask.naturalOf[pc] >= 0 && !isChordTone
				&& !(ask.anchorCount > 0 && holds(ask.anchor, ask.anchorCount, pc)))
			w *= OUTSIDE_KEY;

		w *= isChordTone ? profile.chordPull : profile.passing;
		if (ask.strong && isChordTone)
			w *= profile.strongChordPull;
		if (ask.strong && ask.rootPc >= 0 && pc == pitchClass(ask.rootPc))
			w *= profile.rootPull;

		// A STEP FROM A TONE OF THE CHORD THAT IS COMING resolves into it, which is what voice
		// leading is. Only while the change is close: leading into a chord four bars away is
		// not leading, it is wandering toward something.
		if (leadOn) {
			for (int i = 0; i < ask.nextChordCount; i++) {
				const int npc = pitchClass(ask.nextChord[i]);
				const int up = ((pc - npc) % 12 + 12) % 12;
				const int down = ((npc - pc) % 12 + 12) % 12;
				const int d = std::min(up, down);
				if (d >= 1 && d <= 2) {
					w *= profile.lead;
					break;
				}
			}
		}

		// GRAVITY TOWARD THE MIDDLE OF THE REGISTER. Without it a line drifts to one end and
		// stays there, because nothing else in the weighting cares where it is, only where it
		// is going.
		w *= 1.f - profile.gravity * (std::fabs((float) n - centre) / halfSpan);

		// A FIGURE COMING BACK: the note it restates, or failing that the same step from here.
		if (ask.echo >= 0 && n == ask.echo)
			w *= ask.echoWeight;
		else if (ask.echoStep >= 0 && n == ask.echoStep)
			w *= ask.echoStepWeight;
		else if (ask.hasEchoMove && ask.previous >= 0) {
			const int move = n - ask.previous;
			if ((move > 0) == (ask.echoMove > 0) && (move < 0) == (ask.echoMove < 0)
					&& std::abs(move - ask.echoMove) <= 2)
				w *= ask.echoShapeWeight;
		}
		// THE LINE'S SHAPE: a pull toward where the contour puts this note, three semitones away
		// being as far as a full pull lets through at a seventh of its weight.
		if (ask.aimStrength > 0.f && ask.aim >= 0.f) {
			const float d = ((float) n - ask.aim) / 3.f;
			w *= std::exp(-2.f * ask.aimStrength * d * d);
		}

		// AT A PHRASE BOUNDARY, the tones that sound like arriving somewhere.
		if (ask.anchorCount > 0 && holds(ask.anchor, ask.anchorCount, pc))
			w *= ask.anchorStrength;

		// WHAT ANOTHER VOICE HAS JUST TAKEN. A weight, never a ban: if every candidate collides
		// — two voices in the same few semitones — the penalty applies to all of them equally
		// and cancels out in the normalising, so the line plays instead of falling silent.
		// Separation does as much as the registers leave room for and gives up quietly.
		if (ask.separation > 0.f && ask.takenCount > 0) {
			for (int i = 0; i < ask.takenCount; i++) {
				if (pitchClass(ask.taken[i]) != pc)
					continue;
				// At full separation a doubled pitch class is a twentieth as likely.
				w *= 1.f - 0.95f * std::min(1.f, ask.separation);
				break;
			}
		}

		if (!(w > 0.f))
			continue;
		notes[count] = n;
		weights[count] = w;
		count++;
		total += w;
	}

	if (ask.report) {
		MelodyReport& r = *ask.report;
		r.count = 0;
		r.fellBack = (count == 0 || total <= 0.f);
		for (int i = 0; i < count && r.count < MelodyReport::MAX; i++) {
			r.notes[r.count] = notes[i];
			r.share[r.count] = total > 0.f ? weights[i] / total : 0.f;
			r.count++;
		}
	}

	if (count == 0 || total <= 0.f) {
		return nearestScaleNote(ask.previous >= 0 ? ask.previous : (int) std::lround(centre),
			ask.scale, ask.scaleCount, low, high);
	}

	// THE DRAW. The weights are a running total and the voltage says how far along it to stop,
	// so a candidate twice as heavy is twice as likely and the same voltage against the same
	// weighting always gives the same note. That determinism is the whole reason a melody can
	// be driven by a cable and still be a piece of music rather than a stream of accidents.
	const float d = std::min(0.999999f, std::max(0.f, ask.dice));
	const float target = d * total;
	float running = 0.f;
	for (int i = 0; i < count; i++) {
		running += weights[i];
		if (running > target)
			return notes[i];
	}
	return notes[count - 1];
}

} // namespace px
