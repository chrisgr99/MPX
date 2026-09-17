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

	for (int n = low; n <= high && count < MAX_CANDIDATES; n++) {
		const int pc = pitchClass(n);
		if (!holds(ask.scale, ask.scaleCount, pc))
			continue;
		// WITHIN REACH OF THE LAST NOTE. This is what makes a line rather than a sequence of
		// unrelated notes, and it is skipped on the first note because there is nothing to be
		// within reach of.
		if (ask.previous >= 0 && std::abs(n - ask.previous) > profile.window)
			continue;

		float w = 1.f;
		if (ask.previous >= 0) {
			w *= std::pow(intervalWeight(n - ask.previous), profile.leapAversion);
			if (n < ask.previous)
				w *= profile.descendBias;
		}

		const bool isChordTone = holds(ask.chord, ask.chordCount, pc);
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
