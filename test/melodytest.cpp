/** Plays the melodic step over a chord sequence and reports what kind of line comes out.

  make melodytest

WHY A CENSUS RATHER THAN AN EAR. A weighting either produces music or it does not, and the only
honest way to find out is to listen — but a weighting can be plainly WRONG in ways that are
measurable before anybody listens: a line that leaps constantly, that never lands on a chord
tone, that walks to one end of its register and stays there, or that plays the same note for
ever. Those are the faults this catches, and every one of them was worth catching early.

The figures below are what a musician would say about a line if they could only count. A melody
line should move mostly by step, land on the chord on strong beats far more often than between
them, and use most of the register it was given.
*/
#include "../src/Melodic.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace px;

/** A ii-V-I in C, two beats a chord, which is the sequence every one of these decisions was
argued about. */
struct Step {
	int degree;
	bool strong;
	float beatsToNext;
};

int main() {
	Key key;
	key.tonic = 0;
	key.minor = false;

	struct Role {
		const char* name;
		float smooth, lock;
		int centre, span;
		float lead, rootPull;
		int scale;
	};
	static const Role roles[] = {
		{"bass",   0.55f, 0.85f, 45, 19, 2.5f, 4.f, SCALE_KEY},
		{"melody", 0.75f, 0.55f, 72, 24, 1.8f, 1.f, SCALE_KEY},
		{"lead",   0.50f, 0.40f, 76, 24, 1.4f, 1.f, SCALE_MINOR_PENT},
	};

	for (const Role& role : roles) {
		MelodyProfile profile = melodyProfile(role.smooth, role.lock,
			role.centre - role.span / 2, role.span);
		profile.lead = role.lead;
		profile.rootPull = role.rootPull;

		int scale[7];
		const int scaleCount = melodyScalePitchClasses(key, role.scale, scale);

		// Sixteen bars of ii-V-I, two notes a bar, and a draw that walks steadily through its
		// range — a plain ramp, which is the least musical driver there is and therefore the
		// one a weighting has to survive.
		static const int DEGREES[4] = {2, 5, 1, 1};
		int previous = -1;
		// A THIRD IS NOT A LEAP. Counting everything over a tone as a leap made a singable
		// line look as though it jumped about a third of the time — a figure that would have
		// sent me tuning weights that were already right. Steps, thirds and leaps are three
		// different things to anybody who plays.
		int steps = 0, thirds = 0, leaps = 0, repeats = 0;
		int strongNotes = 0, strongOnChord = 0, weakNotes = 0, weakOnChord = 0;
		int lowest = 999, highest = -999;
		std::map<int, int> intervals;

		for (int bar = 0; bar < 64; bar++) {
			const int degree = DEGREES[bar % 4];
			const int nextDegree = DEGREES[(bar + 1) % 4];
			Chord chord, next;
			chord.valid = true; chord.degree = (int8_t) degree;
			chord.quality = (degree == 5) ? Q_DOM7 : (degree == 2 ? Q_MIN7 : Q_MAJ7);
			next.valid = true; next.degree = (int8_t) nextDegree;
			next.quality = (nextDegree == 5) ? Q_DOM7 : (nextDegree == 2 ? Q_MIN7 : Q_MAJ7);

			int chordPcs[MAX_CHORD_TONES], nextPcs[MAX_CHORD_TONES];
			const int chordCount = chordPitchClasses(chord, key, chordPcs);
			const int nextCount = chordPitchClasses(next, key, nextPcs);

			for (int half = 0; half < 2; half++) {
				MelodyAsk ask;
				ask.previous = previous;
				ask.scale = scale;
				ask.scaleCount = scaleCount;
				ask.chord = chordPcs;
				ask.chordCount = chordCount;
				ask.rootPc = chordRootPitchClass(chord, key);
				ask.nextChord = nextPcs;
				ask.nextChordCount = nextCount;
				ask.beatsToNext = half == 0 ? 2.f : 1.f;
				ask.strong = (half == 0);
				// A RAMP, not noise: it exercises the whole weighting rather than sampling it,
				// and any bias in the candidate ordering shows up as a drift.
				ask.dice = (float) ((bar * 2 + half) % 17) / 17.f;

				const int note = melodicStep(ask, profile);
				if (previous >= 0) {
					const int d = std::abs(note - previous);
					intervals[d]++;
					if (d == 0) repeats++;
					else if (d <= 2) steps++;
					else if (d <= 4) thirds++;
					else leaps++;
				}
				const int pc = ((note % 12) + 12) % 12;
				bool onChord = false;
				for (int i = 0; i < chordCount; i++)
					onChord = onChord || (((chordPcs[i] % 12) + 12) % 12) == pc;
				if (ask.strong) { strongNotes++; strongOnChord += onChord ? 1 : 0; }
				else { weakNotes++; weakOnChord += onChord ? 1 : 0; }
				lowest = std::min(lowest, note);
				highest = std::max(highest, note);
				previous = note;
			}
		}

		const int moves = steps + thirds + leaps + repeats;
		std::printf("%s\n", role.name);
		std::printf("  by step %3d%%   by a third %3d%%   by a leap %3d%%   repeated %3d%%\n",
			100 * steps / std::max(1, moves), 100 * thirds / std::max(1, moves),
			100 * leaps / std::max(1, moves), 100 * repeats / std::max(1, moves));
		std::printf("  on a chord tone: strong beats %3d%%   weak beats %3d%%\n",
			100 * strongOnChord / std::max(1, strongNotes),
			100 * weakOnChord / std::max(1, weakNotes));
		std::printf("  register asked %d to %d, used %d to %d\n",
			profile.low, profile.high, lowest, highest);
		std::printf("  intervals:");
		for (std::map<int, int>::const_iterator it = intervals.begin();
			it != intervals.end(); ++it) {
			std::printf(" %d:%d", it->first, it->second);
		}
		std::printf("\n\n");
	}
	return 0;
}
