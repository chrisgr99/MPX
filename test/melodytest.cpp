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
#include "../src/MelodyVoice.hpp"
#include "../src/ChartLayout.hpp"

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

	int failures = 0;

	// ---- DOES A LINE COME ROUND? ------------------------------------------------------------
	//
	// EXACT, NOT STATISTICAL. The whole value of a recurring line is that a listener recognises
	// it, so "similar" is no answer. With CYCLE at four, the fifth phrase must draw exactly as
	// the first did — and the draw is what the line is made of.
	{
		bool same = true, differs = false;
		for (int step = 0; step < 32; step++) {
			const float into = (float) step * 0.5f;
			const float first = melodyDraw(58, 7, false, 4, 0, 8, 0, into);
			const float fifth = melodyDraw(58, 7, false, 4, 0, 8, 4, into);
			const float second = melodyDraw(58, 7, false, 4, 0, 8, 1, into);
			if (std::fabs(first - fifth) > 1e-9f)
				same = false;
			if (std::fabs(first - second) > 1e-9f)
				differs = true;
		}
		std::printf("a cycle of four brings the same line round at phrase five: %s\n",
			same ? "yes" : "NO");
		std::printf("and the phrases inside a cycle differ from one another: %s\n",
			differs ? "yes" : "NO");
		failures += same ? 0 : 1;
		failures += differs ? 0 : 1;

		// ACROSS PASSES TOO: the pass counter used to be in here, which is precisely why nothing
		// ever repeated. The fifth phrase of the second pass draws as the first phrase did.
		bool acrossPasses = true;
		for (int step = 0; step < 32; step++) {
			const float into = (float) step * 0.5f;
			if (std::fabs(melodyDraw(58, 7, false, 4, 0, 8, 0, into)
				- melodyDraw(58, 7, false, 4, 1, 8, 4, into)) > 1e-9f)
				acrossPasses = false;
		}
		std::printf("and it comes round across passes of the form: %s\n",
			acrossPasses ? "yes" : "NO");
		failures += acrossPasses ? 0 : 1;

		// A LOCKED SEED IGNORES THE CHART'S. Two charts with different seeds, one locked line.
		const bool locked = std::fabs(melodyDraw(11, 7, true, 4, 0, 8, 0, 2.f)
			- melodyDraw(999, 7, true, 4, 0, 8, 0, 2.f)) < 1e-9f;
		std::printf("a locked seed ignores the chart's: %s\n", locked ? "yes" : "NO");
		failures += locked ? 0 : 1;
	}

	// ---- DOES A PHRASE ARRIVE? --------------------------------------------------------------
	//
	// A closing cadence means the ear is waiting for the tonic. The anchor is what puts it there,
	// and this is the figure that says whether it does: how often the note chosen at a phrase end
	// is the tonic, with the anchor and without it.
	{
		MelodyProfile profile = melodyProfile(0.75f, 0.55f, 60, 24);
		int scale[7];
		const int scaleCount = melodyScalePitchClasses(key, SCALE_KEY, scale);
		int chordPcs[MAX_CHORD_TONES];
		Chord tonicChord;
		tonicChord.valid = true;
		tonicChord.degree = 1;
		tonicChord.quality = Q_MAJOR;
		const int chordCount = chordPitchClasses(tonicChord, key, chordPcs);

		int onTonic[2] = {0, 0}, total[2] = {0, 0};
		for (int anchored = 0; anchored < 2; anchored++) {
			int previous = 64;
			for (int i = 0; i < 2000; i++) {
				MelodyAsk ask;
				ask.previous = previous;
				ask.scale = scale;
				ask.scaleCount = scaleCount;
				ask.chord = chordPcs;
				ask.chordCount = chordCount;
				ask.rootPc = 0;
				ask.beatsToNext = 2.f;
				ask.strong = true;
				ask.dice = melodyDraw(58, 7, false, 4, 0, 8, (uint32_t) i, 0.f);
				const int tonic = 0;
				if (anchored) {
					ask.anchor = &tonic;
					ask.anchorCount = 1;
					ask.anchorStrength = 4.f;      // what the module uses on the arrival itself
				}
				const int note = melodicStep(ask, profile);
				if ((((note % 12) + 12) % 12) == 0)
					onTonic[anchored]++;
				total[anchored]++;
				previous = note;
			}
		}
		const float without = 100.f * onTonic[0] / std::max(1, total[0]);
		const float with = 100.f * onTonic[1] / std::max(1, total[1]);
		const bool ok = with > without * 1.6f && with > 30.f;
		std::printf("\nthe anchor lands a phrase on the tonic: %s"
			"  (%.0f%% of notes with it, %.0f%% without)\n", ok ? "yes" : "NO", with, without);
		failures += ok ? 0 : 1;
	}

	// ---- WHAT THE VOICE DOES TO A NOTE ONCE IT HAS A PITCH -----------------------------------
	//
	// These four knobs were on the panel and did nothing for as long as the voice existed; the
	// checks below are what they now promise, each exactly.
	{
		auto chordOf = [](int degree, int quality) {
			Chord c;
			c.valid = true;
			c.degree = (int8_t) degree;
			c.quality = (uint8_t) quality;
			return c;
		};
		Harmony h;
		h.valid = true;
		h.key = key;
		h.current = chordOf(1, Q_MAJOR);            // C
		h.next = chordOf(4, Q_MAJOR);               // F, two beats away
		h.beatsToNext = 2.f;
		h.phraseBeats = 16.f;
		h.beatsToPhraseEnd = 8.f;
		VoiceShapeSettings vs;
		bool ok;

		vs.articulation = -1.f;
		VoiceShape a = voiceShape(h, vs, 64, 1.f, 0.6f, true, 0, 0.5f);
		vs.articulation = 1.f;
		VoiceShape b = voiceShape(h, vs, 64, 1.f, 0.6f, true, 0, 0.5f);
		vs.articulation = 0.f;
		VoiceShape c0 = voiceShape(h, vs, 64, 1.f, 0.6f, true, 0, 0.5f);
		ok = std::fabs(a.beats - 0.3f) < 1e-4f && std::fabs(b.beats - 2.f) < 1e-4f && b.slur
			&& std::fabs(c0.beats - 1.f) < 1e-4f && !c0.slur;
		std::printf("\narticulation shortens, keeps and slurs: %s  (%.2f, %.2f, %.2f beats)\n",
			ok ? "yes" : "NO", a.beats, c0.beats, b.beats);
		failures += ok ? 0 : 1;

		// BREATH, for a rhythm that does not phrase: a note starting inside the last two beats
		// is dropped, one running into them is cut; a rhythm that marks its notes is left alone.
		vs = VoiceShapeSettings();
		vs.breath = 2.f;
		Harmony end = h;
		end.beatsToPhraseEnd = 1.5f;
		end.next.valid = false;
		VoiceShape dropped = voiceShape(end, vs, 64, 1.f, 0.6f, true, 0, 0.5f);
		Harmony near = h;
		near.beatsToPhraseEnd = 3.f;
		near.next.valid = false;
		VoiceShape cut = voiceShape(near, vs, 64, 2.5f, 0.6f, true, 0, 0.5f);
		VoiceShape marked = voiceShape(end, vs, 64, 1.f, 0.6f, true, Event::GROUP_END, 0.5f);
		ok = dropped.drop && !cut.drop && std::fabs(cut.beats - 1.f) < 1e-4f && !marked.drop;
		std::printf("breath rests before a phrase end, and leaves a phrased rhythm alone: %s\n",
			ok ? "yes" : "NO");
		failures += ok ? 0 : 1;

		// END NOTES AT CHANGES: B held from C into F is a semitone from F's C and is not a tone of
		// F, so it is always cut at the change; C is a tone of F, so it is held unless asked.
		vs = VoiceShapeSettings();
		VoiceShape clash = voiceShape(h, vs, 71, 3.f, 0.6f, true, 0, 0.9f);   // B, over F
		VoiceShape held = voiceShape(h, vs, 72, 3.f, 0.6f, true, 0, 0.9f);    // C, in F
		vs.endAtChanges = 1.f;
		VoiceShape asked = voiceShape(h, vs, 72, 3.f, 0.6f, true, 0, 0.9f);
		ok = std::fabs(clash.beats - 2.f) < 1e-4f && std::fabs(held.beats - 3.f) < 1e-4f
			&& std::fabs(asked.beats - 2.f) < 1e-4f;
		std::printf("a clash is ended at the change, a held tone kept unless asked: %s\n",
			ok ? "yes" : "NO");
		failures += ok ? 0 : 1;

		// ACCENT: evened toward the middle, or leaning on strong beats and chord tones.
		vs = VoiceShapeSettings();
		vs.accent = -1.f;
		VoiceShape even1 = voiceShape(h, vs, 64, 1.f, 0.2f, false, 0, 0.5f);
		VoiceShape even2 = voiceShape(h, vs, 64, 1.f, 1.f, true, 0, 0.5f);
		vs.accent = 1.f;
		VoiceShape loud = voiceShape(h, vs, 64, 1.f, 0.6f, true, 0, 0.5f);    // E: a chord tone
		VoiceShape soft = voiceShape(h, vs, 62, 1.f, 0.6f, false, 0, 0.5f);   // D: not one, weak
		ok = std::fabs(even1.level - 0.75f) < 1e-4f && std::fabs(even2.level - 0.75f) < 1e-4f
			&& loud.level > 0.6f && soft.level < 0.6f;
		std::printf("accent evens, or leans on strong beats and chord tones: %s  (%.2f against %.2f)\n",
			ok ? "yes" : "NO", loud.level, soft.level);
		failures += ok ? 0 : 1;

		// FROM THE CHORD: over E7 in A minor the palette has G sharp and not G.
		Key am;
		am.tonic = 9;
		am.minor = true;
		Harmony m;
		m.valid = true;
		m.key = am;
		m.current = chordOf(5, Q_DOM7);
		m.next = chordOf(1, Q_MINOR);
		m.beatsToNext = 1.f;
		VoiceSettings vset;
		vset.scale = SCALE_FROM_CHORD;
		vset.centre = 67;
		vset.span = 14;
		MelodyReport rep;
		voiceNoteFor(m, vset, 67, 0.5f, NULL, 0, 0.f, &rep);
		bool sharp = false, natural = false;
		for (int i = 0; i < rep.count; i++) {
			const int pc = ((rep.notes[i] % 12) + 12) % 12;
			sharp = sharp || pc == 8;
			natural = natural || pc == 7;
		}
		ok = sharp && !natural;
		std::printf("the scale from the chord takes G sharp over E7, and drops G: %s\n",
			ok ? "yes" : "NO");
		failures += ok ? 0 : 1;
	}

	// ---- ENDINGS ON STABLE NOTES ----------------------------------------------------------------
	//
	// A half-cadence arrival over G7 in C lands on G or D, never B (the leading tone) or F (the
	// seventh); a breath note over G7 lands on G, B or D, never F — whatever the draw and however
	// low the chord lock.
	{
		Key c;
		c.tonic = 0;
		Chord g7;
		g7.valid = true;
		g7.degree = 5;
		g7.quality = (uint8_t) Q_DOM7;
		VoiceSettings vset;
		vset.lock = 0.2f;
		vset.smooth = 0.4f;
		vset.centre = 67;
		vset.span = 17;
		bool halfOk = true, breathOk = true;
		for (int i = 0; i < 400; i++) {
			Harmony h;
			h.valid = true;
			h.key = c;
			h.current = g7;
			h.phraseCadence = CADENCE_HALF;
			h.phraseBeats = 16.f;
			h.beatsToPhraseEnd = 1.f;
			const int prev = 60 + i % 14;
			const float dice = (float) i / 400.f;
			const int a = voiceNoteFor(h, vset, prev, dice, NULL, 0, 0.f, NULL, Event::ARRIVAL, -1);
			const int apc = ((a % 12) + 12) % 12;
			halfOk = halfOk && apc == 7;
			h.phraseCadence = CADENCE_AUTHENTIC;
			h.beatsToPhraseEnd = 8.f;
			const int b = voiceNoteFor(h, vset, prev, dice, NULL, 0, 0.f, NULL, Event::GROUP_END, -1);
			const int bpc = ((b % 12) + 12) % 12;
			breathOk = breathOk && (bpc == 7 || bpc == 11 || bpc == 2);
		}
		std::printf("\na half cadence ends on the dominant's root: %s\n", halfOk ? "yes" : "NO");
		std::printf("a breath is held on a note of the triad: %s\n", breathOk ? "yes" : "NO");
		failures += halfOk ? 0 : 1;
		failures += breathOk ? 0 : 1;
	}

	// ---- REPEATED NOTES ------------------------------------------------------------------------
	//
	// The share of intervals that are a repeated note, across the REPEATED NOTES knob, on a line of
	// eighth notes over I IV I V at the patch's own settings. This is the calibration: the jazz
	// solos repeat on about one interval in twenty.
	{
		Key c;
		c.tonic = 0;
		const int degrees[4] = {1, 4, 1, 5};
		auto chordOf = [](int degree) {
			Chord ch;
			ch.valid = true;
			ch.degree = (int8_t) degree;
			ch.quality = (uint8_t) Q_MAJOR;
			return ch;
		};
		auto shareAt = [&](float repeats) {
			VoiceSettings vset;
			vset.smooth = 0.75f;
			vset.lock = 0.6f;
			vset.centre = 67;
			vset.span = 14;
			vset.leading = 0.8f;
			vset.repeats = repeats;
			int previous = -1, before = -1, same = 0, moves = 0;
			for (int i = 0; i < 4000; i++) {
				const int bar = (i / 8) % 4;
				Harmony h;
				h.valid = true;
				h.key = c;
				h.current = chordOf(degrees[bar]);
				h.next = chordOf(degrees[(bar + 1) % 4]);
				h.beatInBar = (float) (i % 8) * 0.5f;
				h.barBeats = 4;
				h.beatsToNext = 4.f - h.beatInBar;
				const float dice = melodyDraw(58, 7, false, 4, 0, 8, (uint32_t) (i / 32),
					(float) (i % 32) * 0.5f);
				const int note = voiceNoteFor(h, vset, previous, dice, NULL, 0, 0.f, NULL, 0, before);
				if (previous >= 0) {
					moves++;
					same += note == previous ? 1 : 0;
				}
				before = previous;
				previous = note;
			}
			return 100.f * same / std::max(1, moves);
		};
		std::printf("\nrepeated notes, share of intervals: without the knob %.0f%%;", shareAt(-1.f));
		for (int k = 0; k <= 10; k += 2)
			std::printf("  %.1f: %.0f%%", k / 10.f, shareAt(k / 10.f));
		std::printf("\n");
		const bool ok = shareAt(0.f) < shareAt(0.5f) && shareAt(0.5f) < shareAt(1.f);
		std::printf("the knob raises the share of repeated notes: %s\n", ok ? "yes" : "NO");
		failures += ok ? 0 : 1;
	}

	// MOTIF: a note restating another takes its pitch when the pitch fits — a tone of the chord,
	// in reach — nearly always at full, and at nought only as often as the draw happens to.
	{
		Key c;
		c.tonic = 0;
		Chord cmaj;
		cmaj.valid = true;
		cmaj.degree = 1;
		cmaj.quality = (uint8_t) Q_MAJOR;
		Harmony h;
		h.valid = true;
		h.key = c;
		h.current = cmaj;
		h.phraseBeats = 16.f;
		h.beatsToPhraseEnd = 8.f;
		VoiceSettings vset;
		vset.centre = 67;
		vset.span = 14;
		vset.scale = SCALE_FROM_CHORD;
		auto takes = [&](float motif) {
			vset.motif = motif;
			int took = 0;
			for (int i = 0; i < 200; i++) {
				VoiceLine line;
				line.echoPitch = 60;
				const int n = voiceNoteFor(h, vset, 64, (i + 0.5f) / 200.f, NULL, 0, 0.f, NULL, 0, 62,
					0.5f, &line);
				took += n == 60;
			}
			return took / 200.f;
		};
		const float off = takes(0.f), full = takes(1.f);
		const bool ok = full > 0.9f && off < 0.5f;
		std::printf("a restated note takes the pitch it restates: %s  (%.0f%% at nought, %.0f%% at full)\n",
			ok ? "yes" : "NO", 100.f * off, 100.f * full);
		failures += ok ? 0 : 1;

		// CONTOUR: a line rises to its high point early and falls to its end. Nine notes of a
		// group, over many draws: at full, the note a quarter of the way through is the highest
		// on average and the last is below the first; at nought, neither need be.
		auto shape = [&](float contour, float* quarter, float* end) {
			vset.motif = 0.f;
			vset.contour = contour;
			float q = 0.f, e = 0.f;
			const int runs = 300;
			for (int r = 0; r < runs; r++) {
				VoiceMemory mem;
				int prev = -1, before = -1, start = -1;
				for (int k = 0; k < 9; k++) {
					const float along = k / 8.f;
					const VoiceLine line = mem.lineFor(0, along);
					const float dice = (float) ((r * 7919 + k * 104729) % 1000) / 1000.f;
					const int n = voiceNoteFor(h, vset, prev, dice, NULL, 0, 0.f, NULL, 0, before,
						0.5f, &line);
					mem.remember(n, along);
					before = prev;
					prev = n;
					if (k == 0)
						start = n;
					if (k == 2)
						q += (float) (n - start);
					if (k == 8)
						e += (float) (n - start);
				}
			}
			*quarter = q / runs;
			*end = e / runs;
		};
		float q0, e0, q1, e1;
		shape(0.f, &q0, &e0);
		shape(1.f, &q1, &e1);
		const bool shaped = q1 > q0 + 1.f && q1 - e1 > 3.f;
		std::printf("contour lifts a line early and brings it down to its end: %s  (at a quarter "
			"%+.1f and at the end %+.1f semitones from the start, against %+.1f and %+.1f at nought)\n",
			shaped ? "yes" : "NO", q1, e1, q0, e0);
		failures += shaped ? 0 : 1;
	}

	std::printf("\n%s\n", failures ? "EXACT CHECKS FAILED" : "exact checks pass");
	return failures ? 1 : 0;
}
