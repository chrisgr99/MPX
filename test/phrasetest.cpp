/** What kind of rhythm the phrase generator makes, measured over real charts.

  make phrasetest

WHY A CENSUS RATHER THAN AN EAR. The same reason as melodytest: a generator can be plainly wrong
in ways that are measurable before anybody listens — phrases that never breathe, pauses that
never arrive, a line that changes when it should recur. The figures here are compared with the
two corpora in docs/phrase.md: 22 lead-sheet melodies and 456 jazz solos.

THE EXACT CHECKS AT THE END MUST NEVER FAIL. They are not about taste: the same seed giving a
different phrase, or VARIATION at nought giving different phrases, would mean nothing about this
module could be reproduced.
*/
#include "../src/ChartLayout.hpp"
#include "../src/IReal.hpp"
#include "../src/Phrasing.hpp"
#include "../src/Swing.hpp"
#include "../src/ChartExamples.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

static std::string readFile(const char* path) {
	std::ifstream file(path);
	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

struct Stats {
	int phrases = 0, groups = 0, notes = 0, silentPhrases = 0, full = 0;
	std::vector<float> groupSeconds, groupBeats, pauseBeats, noteGapBeats;
	std::map<int, int> startKinds, endKinds;
	int lastLongest = 0, groupsMeasured = 0;
	int pauseMissing = 0;
	int outside = 0, overlapping = 0;
	int onChange = 0, changeSlots = 0, onChangeCovered = 0;
};

static void measure(const PhraseAsk& ask, const PhraseControls& c, const PhrasePattern& p,
		Stats& s) {
	s.phrases++;
	if (p.silent) { s.silentPhrases++; return; }
	if (p.full) s.full++;
	s.groups += p.groupCount;
	s.notes += p.noteCount;

	for (int g = 0; g < p.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
		const float beats = p.soundsTo[g] - p.starts[g];
		s.groupBeats.push_back(beats);
		s.groupSeconds.push_back(beats / ask.beatsPerSecond);
		s.pauseBeats.push_back(p.pauses[g]);
		if (p.pauses[g] <= 0.01f)
			s.pauseMissing++;
		s.startKinds[p.startKind[g]]++;
		s.endKinds[p.endKind[g]]++;

		// Is the group's last note the longest in it?
		float longest = 0.f, lastDur = 0.f;
		int count = 0;
		for (int i = 0; i < p.noteCount; i++) {
			if (p.notes[i].group != g) continue;
			count++;
			longest = std::max(longest, p.notes[i].duration);
			lastDur = p.notes[i].duration;
		}
		if (count >= 3) {
			s.groupsMeasured++;
			if (lastDur >= longest - 1e-4f)
				s.lastLongest++;
		}
	}

	for (int i = 0; i < p.noteCount; i++) {
		const PhraseNote& n = p.notes[i];
		if (n.offset < -0.001f || n.offset + n.duration > ask.phraseBeats + 0.001f)
			s.outside++;
		if (i + 1 < p.noteCount && n.offset + n.duration > p.notes[i + 1].offset + 0.001f
			&& n.group == p.notes[i + 1].group)
			s.overlapping++;
		if (i + 1 < p.noteCount)
			s.noteGapBeats.push_back(p.notes[i + 1].offset - n.offset);
		if (n.onChange)
			s.onChange++;
	}
	// HOW MANY OF THE PHRASE'S CHORD CHANGES GOT A NOTE — which is what ON CHANGES is for, and
	// the only way to see whether the control does anything.
	for (int k = 0; k < ask.changeCount; k++) {
		s.changeSlots++;
		for (int i = 0; i < p.noteCount; i++) {
			if (std::fabs(p.notes[i].offset - ask.changes[k]) < 0.26f) {
				s.onChangeCovered++;
				break;
			}
		}
	}
	(void) c;
}

static float median(std::vector<float>& v) {
	if (v.empty()) return 0.f;
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

static float share(const std::vector<float>& v, float lo, float hi) {
	int n = 0;
	for (float x : v) if (x >= lo && x < hi) n++;
	return v.empty() ? 0.f : 100.f * (float) n / (float) v.size();
}

static void report(const char* title, Stats& s) {
	std::printf("\n%s: %d phrases, %d groups, %d notes", title, s.phrases, s.groups, s.notes);
	if (s.silentPhrases)
		std::printf(", %d phrases silent", s.silentPhrases);
	std::printf("\n");
	std::printf("  group length: median %.1f seconds, %.1f beats"
		"   (2 seconds or less %.0f%%, over 5 seconds %.0f%%)\n",
		median(s.groupSeconds), median(s.groupBeats),
		share(s.groupSeconds, 0.f, 2.f), share(s.groupSeconds, 5.f, 1e9f));
	std::printf("  pause after a group: median %.1f beats, two bars or more %.0f%%"
		"   groups with no pause: %d\n",
		median(s.pauseBeats), share(s.pauseBeats, 8.f, 1e9f), s.pauseMissing);
	static const char* STARTS[] = {"pickup", "downbeat", "after the beat"};
	static const char* ENDS[] = {"strong beat", "off the beat"};
	std::printf("  starts:");
	for (int i = 0; i < NUM_STARTS; i++)
		std::printf("  %s %.0f%%", STARTS[i], 100.f * (float) s.startKinds[i] / std::max(1, s.groups));
	std::printf("\n  endings:");
	for (int i = 0; i < NUM_ENDS; i++)
		std::printf("  %s %.0f%%", ENDS[i], 100.f * (float) s.endKinds[i] / std::max(1, s.groups));
	std::printf("\n  last note the longest in its group: %.0f%% of %d groups\n",
		100.f * (float) s.lastLongest / std::max(1, s.groupsMeasured), s.groupsMeasured);
	std::printf("  chord changes that got a note: %.0f%% of %d\n",
		100.f * (float) s.onChangeCovered / std::max(1, s.changeSlots), s.changeSlots);
	if (s.outside || s.overlapping || s.full)
		std::printf("  FAULTS: %d notes outside the phrase, %d overlapping, %d phrases over the "
			"note limit\n", s.outside, s.overlapping, s.full);
}

int main(int argc, char** argv) {
	std::vector<std::string> files;
	for (int i = 1; i < argc; i++)
		files.push_back(argv[i]);

	// Real phrases from real charts, at four tempos, in both styles.
	struct Case { const char* name; int style; float bps; };
	static const Case cases[] = {
		{"song, 60 bpm", STYLE_SONG, 1.f},
		{"song, 120 bpm", STYLE_SONG, 2.f},
		{"song, 240 bpm", STYLE_SONG, 4.f},
		{"jazz, 120 bpm", STYLE_JAZZ2, 2.f},
		{"jazz, 240 bpm", STYLE_JAZZ2, 4.f},
	};
	static const int NUM_CASES = sizeof(cases) / sizeof(cases[0]);
	Stats stats[NUM_CASES];

	int chartsUsed = 0;
	uint32_t seed = 1;
	for (const std::string& file : files) {
		const std::string payload = irealPayloadFromHtml(readFile(file.c_str()));
		if (payload.empty())
			continue;
		for (const Song& song : irealParsePlaylist(payload)) {
			const std::vector<ChartBar> bars = chartLayout(song);
			if (bars.empty())
				continue;
			const ChartPlayback pb = chartPlayback(bars);
			const std::vector<ChartPhrase> phrases = chartPhrases(bars, pb);
			if (phrases.empty())
				continue;
			chartsUsed++;
			// Every twentieth chart, to keep the run brief and the sample wide.
			if (chartsUsed % 20 != 0)
				continue;
			for (const ChartPhrase& ph : phrases) {
				const int b = ph.startBar;
				const float barBeats = (float) bars[pb.timeline[b].bar].beats;
				for (int k = 0; k < NUM_CASES; k++) {
					PhraseControls c;
					phraseStyle(cases[k].style, c);
					PhraseAsk ask;
					ask.phraseBeats = ph.endBeat - ph.startBeat;
					ask.barBeats = barBeats;
					ask.beatsPerSecond = cases[k].bps;
					ask.cadence = ph.cadence;
					ask.changes = ph.changes.empty() ? NULL : &ph.changes[0];
					ask.changeCount = (int) ph.changes.size();
					ask.seed = seed;
					ask.cyclePosition = (int) (&ph - &phrases[0]) % 4;
					PhrasePattern p;
					phraseGenerate(ask, c, p);
					measure(ask, c, p, stats[k]);
				}
				seed = seed * 1664525u + 1013904223u;
			}
		}
	}

	std::printf("%d charts read, every twentieth used\n", chartsUsed);
	for (int k = 0; k < NUM_CASES; k++)
		report(cases[k].name, stats[k]);

	std::printf("\nFor comparison, from docs/phrase.md:\n");
	std::printf("  lead-sheet melodies: 77%% of breath groups two bars or shorter; starts pickup "
		"37%%, downbeat 37%%, after 22%%;\n    endings on a strong beat 62%%; last note longest "
		"71%%\n");
	std::printf("  jazz solos: median phrase 2.7 seconds, middle half 1.5 to 4.4; starts off the "
		"beat 55%%, downbeat 10%%;\n    endings off the beat 62%%; last note longest 56%%; "
		"median silence between phrases 1.9 beats\n");

	// ---- the exact checks ----
	int failures = 0;
	PhraseAsk ask;
	ask.phraseBeats = 16.f;
	ask.barBeats = 4.f;
	ask.beatsPerSecond = 2.f;
	ask.seed = 4242;
	ask.cyclePosition = 2;
	PhraseControls c;
	phraseStyle(STYLE_SONG, c);

	PhrasePattern a, b;
	phraseGenerate(ask, c, a);
	phraseGenerate(ask, c, b);
	bool same = a.noteCount == b.noteCount;
	for (int i = 0; same && i < a.noteCount; i++)
		same = std::fabs(a.notes[i].offset - b.notes[i].offset) < 1e-6f
			&& std::fabs(a.notes[i].duration - b.notes[i].duration) < 1e-6f;
	std::printf("\nthe same seed gives the same phrase: %s\n", same ? "yes" : "NO");
	failures += same ? 0 : 1;

	c.variation = 0.f;
	// WHAT VARIATION AT NOUGHT NOW MEANS. It used to mean the seed made no difference at all,
	// which turned every yes-or-no draw into a threshold: density at a half sounded every slot
	// above the middle and none below, so the module made a metronome and every other control's
	// meaning moved with this one knob. A probability is already a proportion. So at nought what
	// goes is the JITTER — the wander in a pause's length, the wander in where a group starts —
	// and the pattern still belongs to the seed.
	PhraseControls z = c;
	z.variation = 0.f;
	z.pause = 2.f;
	z.phrasePause = 2.f;
	bool exact = true, differs = false;
	int measured = 0;
	PhrasePattern shape;
	ask.seed = 1;
	phraseGenerate(ask, z, shape);
	for (uint32_t seed = 1; seed <= 12; seed++) {
		ask.seed = seed;
		PhrasePattern p;
		phraseGenerate(ask, z, p);
		for (int g = 0; g + 1 < p.groupCount; g++) {
			// THE PAUSE, not the gap to the next group's start: that gap also holds the room left
			// for the next group's pickup, which comes after the breath and is drawn, not jittered.
			const float silence = p.pauses[g];
			// WHAT IS ASKED FOR, OR THE CAP, WHICHEVER IS SMALLER. A breath may take at most a
			// third of its group, so a short group's pause is shorter than the setting — a limit
			// rather than a wander, and it is the wander this is checking for.
			const float span = p.starts[g + 1] - p.starts[g];
			float want = std::min(z.pause, span * 0.34f);
			want = std::floor(want * 2.f + 0.5f) / 2.f;    // in half beats, as a breath is
			if (std::fabs(silence - want) > 0.01f)
				exact = false;
			measured++;
		}
		if (p.noteCount != shape.noteCount)
			differs = true;
	}
	std::printf("variation at nought takes the wander out of the pauses: %s"
		"  (%d group ends, all exactly %.1f beats)\n", exact ? "yes" : "NO", measured, z.pause);
	failures += exact ? 0 : 1;
	std::printf("and the seed still decides which slots sound: %s\n", differs ? "yes" : "NO");
	failures += differs ? 0 : 1;

	// AND DENSITY MEANS WHAT IT SAYS AT EITHER END OF VARIATION. Half the slots at a half, at
	// nought and at one alike — this is the check that would have caught the metronome.
	{
		float share[2] = {0.f, 0.f};
		for (int k = 0; k < 2; k++) {
			PhraseControls dc = c;
			dc.variation = k ? 1.f : 0.f;
			dc.density = 0.5f;
			dc.subdivision = SUB_EIGHTH;
			dc.triplets = 0.f;
			int notes = 0, slots = 0;
			for (uint32_t seed = 1; seed <= 30; seed++) {
				ask.seed = seed;
				PhrasePattern p;
				phraseGenerate(ask, dc, p);
				notes += p.noteCount;
				for (int g = 0; g < p.groupCount; g++)
					slots += (int) std::floor((p.soundsTo[g] - p.starts[g]) * 2.f + 0.5f);
			}
			share[k] = slots ? (float) notes / slots : 0.f;
		}
		const bool ok = share[0] > 0.3f && share[0] < 0.75f && share[1] > 0.3f && share[1] < 0.75f;
		std::printf("density at a half sounds about half the slots at either variation: %s"
			"  (%.0f%% at nought, %.0f%% at one)\n", ok ? "yes" : "NO",
			share[0] * 100.f, share[1] * 100.f);
		failures += ok ? 0 : 1;
	}

	// A phrase decided at one tempo and again at another: the groups should hold their length in
	// seconds, not in beats. This is the finding the whole design rests on.
	phraseStyle(STYLE_SONG, c);
	float secs[3], beats[3];
	const float rates[3] = {1.f, 2.f, 4.f};
	for (int i = 0; i < 3; i++) {
		ask.beatsPerSecond = rates[i];
		// SIXTEEN SECONDS OF MUSIC AT EACH TEMPO. It was eight, which at one beat a second cannot
		// hold two three-second groups and the breaths between them — so the check passed only
		// because the second group was a one-second stub, which is the fault it should have
		// been catching. A phrase with room in it tests the property; one without tests the tail.
		ask.phraseBeats = 16.f * rates[i];
		ask.seed = 7;
		PhrasePattern p;
		phraseGenerate(ask, c, p);
		secs[i] = (p.soundsTo[0] - p.starts[0]) / rates[i];
		beats[i] = p.soundsTo[0] - p.starts[0];
	}
	const bool level = std::fabs(secs[0] - secs[2]) < 1.f && beats[2] > beats[0] * 1.8f;
	std::printf("a group holds its length in seconds as the tempo changes: %s"
		"  (%.1f, %.1f, %.1f seconds; %.0f, %.0f, %.0f beats)\n", level ? "yes" : "NO",
		secs[0], secs[1], secs[2], beats[0], beats[1], beats[2]);
	failures += level ? 0 : 1;

	// THE CURVE ITSELF, against the medians measured in the solos. A curve that drifted from the
	// corpus would make every figure below meaningless while every check still passed.
	{
		struct Point { float bpm, want; } pts[] = {
			{100.f, 1.25f}, {140.f, 1.43f}, {180.f, 1.43f}, {220.f, 1.36f},
			{260.f, 1.27f}, {300.f, 1.15f},
		};
		bool ok = true;
		for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
			float e = 0.f, sx = 0.f;
			swingRatios(1.f, pts[i].bpm, e, sx);
			if (std::fabs(e - pts[i].want) > 0.02f)
				ok = false;
		}
		float e0 = 0.f, s0 = 0.f;
		swingRatios(0.f, 160.f, e0, s0);
		ok = ok && std::fabs(e0 - 1.f) < 1e-6f && std::fabs(s0 - 1.f) < 1e-6f;
		std::printf("the swing curve matches the corpus, and nought is even: %s\n",
			ok ? "yes" : "NO");
		failures += ok ? 0 : 1;
	}

	// ---- THE FEEL: swing, and triplets ---------------------------------------------------
	//
	// SWING IS A DEFORMATION OF TIME, so what is checked is the time: the ratio between the two
	// halves of each divided beat, measured from the notes that came out, against the ratio the
	// chart said. A census of statistics would not catch a mapping applied to onsets but not to
	// the notes' ends, which is the fault worth fearing here.
	{
		phraseStyle(STYLE_JAZZ2, c);
		c.triplets = 0.f;
		c.density = 1.f;                      // every slot, so every beat has both halves
		c.subdivision = SUB_EIGHTH;
		ask.beatsPerSecond = 3.f;
		ask.phraseBeats = 32.f;
		ask.swingEighth = 1.5f;

		double got = 0.0;
		int pairs = 0, offGrid = 0;
		for (uint32_t seed = 1; seed <= 40; seed++) {
			ask.seed = seed;
			PhrasePattern p;
			phraseGenerate(ask, c, p);
			for (int i = 0; i + 1 < p.noteCount; i++) {
				const float a = p.notes[i].offset, b = p.notes[i + 1].offset;
				const float beat = std::floor(a + 0.001f);
				// The two halves of one beat: a note on the beat and the one after it, inside it.
				if (std::fabs(a - beat) > 0.01f || b >= beat + 0.999f)
					continue;
				const float firstHalf = b - a, secondHalf = beat + 1.f - b;
				if (firstHalf <= 0.f || secondHalf <= 0.f)
					continue;
				got += firstHalf / secondHalf;
				pairs++;
			}
		}
		const double ratio = pairs ? got / pairs : 0.0;
		const bool ok = pairs > 100 && std::fabs(ratio - 1.5) < 0.06;
		std::printf("swing reproduces the ratio the chart published: %s"
			"  (asked 1.50, measured %.2f over %d pairs)\n", ok ? "yes" : "NO", ratio, pairs);
		failures += ok ? 0 : 1;

		// AND WITH NO SWING, EVERY ONSET IS STILL ON ITS SLOT. A deformation that leaked in at
		// nought would be invisible in any statistic and obvious to the ear.
		ask.swingEighth = 1.f;
		offGrid = 0;
		int counted = 0;
		for (uint32_t seed = 1; seed <= 40; seed++) {
			ask.seed = seed;
			PhrasePattern p;
			phraseGenerate(ask, c, p);
			for (int i = 0; i < p.noteCount; i++) {
				const float slots = p.notes[i].offset * 2.f;    // eighths
				if (std::fabs(slots - std::round(slots)) > 0.001f)
					offGrid++;
				counted++;
			}
		}
		std::printf("with swing at nought every onset stays on its slot: %s  (%d of %d off)\n",
			offGrid ? "NO" : "yes", offGrid, counted);
		failures += offGrid ? 1 : 0;

		// TRIPLETS, as a share of the beats that carry notes, against what was asked for.
		ask.swingEighth = 1.4f;
		c.triplets = 0.15f;
		int tripletBeats = 0, playedBeats = 0, runs = 0, longRuns = 0;
		for (uint32_t seed = 1; seed <= 60; seed++) {
			ask.seed = seed;
			PhrasePattern p;
			phraseGenerate(ask, c, p);
			bool beatHas[64] = {}, beatTrip[64] = {};
			for (int i = 0; i < p.noteCount; i++) {
				const int beat = (int) std::floor(p.notes[i].offset + 0.001f);
				if (beat < 0 || beat >= 64)
					continue;
				beatHas[beat] = true;
				if (p.notes[i].triplet)
					beatTrip[beat] = true;
			}
			int run = 0;
			for (int b = 0; b < (int) ask.phraseBeats && b < 64; b++) {
				if (beatHas[b])
					playedBeats++;
				if (beatTrip[b]) {
					tripletBeats++;
					run++;
				}
				else if (run) {
					runs++;
					if (run > 3)
						longRuns++;
					run = 0;
				}
			}
			if (run) {
				runs++;
				if (run > 3)
					longRuns++;
			}
		}
		const float share = playedBeats ? (float) tripletBeats / playedBeats : 0.f;
		const bool tok = std::fabs(share - 0.15f) < 0.05f && longRuns * 20 < runs;
		std::printf("triplets land on about the share asked for: %s"
			"  (asked 15%%, measured %.0f%%; %d runs, %d longer than three beats)\n",
			tok ? "yes" : "NO", share * 100.f, runs, longRuns);
		failures += tok ? 0 : 1;
	}

	// ---- REPETITION -----------------------------------------------------------------------
	//
	// EXACT CHECKS, not statistics. Repetition is the one thing here whose whole value is that it
	// is recognisable, so "about the same" is no answer: either the onsets come back or they do
	// not.
	{
		phraseStyle(STYLE_SONG, c);
		c.triplets = 0.f;
		c.variation = 0.6f;
		ask.swingEighth = 1.f;
		ask.swingSixteenth = 1.f;
		ask.beatsPerSecond = 2.f;
		ask.phraseBeats = 16.f;
		ask.seed = 42;
		ask.previous = NULL;

		// CYCLE: with the caller folding the phrase count by N, phrase N plus one decides exactly
		// as phrase one did. This is what makes a line come round.
		const int cycle = 5;
		PhrasePattern first, again;
		ask.cyclePosition = 0;
		phraseGenerate(ask, c, first);
		ask.cyclePosition = cycle % cycle;      // the same position, one cycle later
		phraseGenerate(ask, c, again);
		bool same = first.noteCount == again.noteCount;
		for (int i = 0; same && i < first.noteCount; i++)
			same = std::fabs(first.notes[i].offset - again.notes[i].offset) < 1e-6f;
		std::printf("a cycle comes round to the same phrase: %s  (%d notes)\n",
			same ? "yes" : "NO", first.noteCount);
		failures += same ? 0 : 1;

		// REPEAT AT ONE: every onset of the previous phrase comes back, except in the last group,
		// which is always the new phrase's own.
		c.repeat = 1.f;
		PhrasePattern prev, next;
		ask.cyclePosition = 0;
		ask.previous = NULL;
		phraseGenerate(ask, c, prev);
		ask.cyclePosition = 1;
		ask.previous = &prev;
		phraseGenerate(ask, c, next);
		const float keepFrom = next.groupCount > 0 ? next.starts[next.groupCount - 1] : 0.f;
		int copied = 0, missing = 0;
		for (int i = 0; i < prev.rawCount; i++) {
			if (prev.raw[i].offset >= keepFrom - 0.001f)
				continue;
			// NOT INTO A BREATH. Where this phrase breathes is its own, so an earlier note that
			// falls in one of its pauses is left out on purpose.
			bool inBreath = false;
			for (int g = 0; g < next.groupCount; g++) {
				const float to = (g + 1 < next.groupCount) ? next.starts[g + 1] : ask.phraseBeats;
				if (prev.raw[i].offset >= next.soundsTo[g] - 0.001f
					&& prev.raw[i].offset < to - 0.001f)
					inBreath = true;
			}
			// NOR INTO A LINE'S ENDING: from a group's own last note to where the next group
			// starts sounding is protected, so a held ending is not cut by a copied note.
			for (int j = 0; j + 1 < next.noteCount && !inBreath; j++) {
				if (next.notes[j].group == next.notes[j + 1].group)
					continue;
				if (prev.raw[i].offset >= next.notes[j].offset - 0.001f
					&& prev.raw[i].offset < next.notes[j + 1].offset - 0.001f)
					inBreath = true;
			}
			if (inBreath)
				continue;
			bool found = false;
			for (int j = 0; j < next.noteCount && !found; j++)
				found = std::fabs(next.notes[j].offset - prev.raw[i].offset) < 1e-6f;
			if (found)
				copied++;
			else
				missing++;
		}
		const bool restated = missing == 0 && copied > 0;
		std::printf("repeat at one restates every onset before the last group: %s"
			"  (%d restated, %d lost)\n", restated ? "yes" : "NO", copied, missing);
		failures += restated ? 0 : 1;

		// REPEAT AT NOUGHT changes nothing: the phrase is what it would have been with no
		// previous phrase at all. A repeat that leaked in at nought would be a rhythm nobody
		// asked for and nothing to point at.
		c.repeat = 0.f;
		const float motifWas = c.motif;
		c.motif = 0.f;
		PhrasePattern alone, withPrev;
		ask.cyclePosition = 1;
		ask.previous = NULL;
		phraseGenerate(ask, c, alone);
		ask.previous = &prev;
		phraseGenerate(ask, c, withPrev);
		bool untouched = alone.noteCount == withPrev.noteCount;
		for (int i = 0; untouched && i < alone.noteCount; i++)
			untouched = std::fabs(alone.notes[i].offset - withPrev.notes[i].offset) < 1e-6f;
		std::printf("repeat at nought ignores the phrase before: %s\n", untouched ? "yes" : "NO");
		c.motif = motifWas;
		failures += untouched ? 0 : 1;

		// ELIDE: with a closing cadence and elide at one, the last group runs to the phrase end
		// rather than breathing at it.
		c.repeat = 0.f;
		c.elide = 1.f;
		c.phrasePause = 4.f;
		ask.previous = NULL;
		ask.cadence = CADENCE_AUTHENTIC;
		float latest = 0.f;
		for (uint32_t seed = 1; seed <= 20; seed++) {
			ask.seed = seed;
			PhrasePattern p;
			phraseGenerate(ask, c, p);
			if (p.groupCount > 0)
				latest = std::max(latest, p.soundsTo[p.groupCount - 1]);
		}
		const bool runs = latest > ask.phraseBeats - 1.f;
		std::printf("elide lets a phrase run into the next: %s  (sounds to %.1f of %.0f beats)\n",
			runs ? "yes" : "NO", latest, ask.phraseBeats);
		failures += runs ? 0 : 1;

		c.elide = 0.f;
		ask.cadence = CADENCE_NONE;
		ask.previous = NULL;

		// A PICKUP: a phrase given room before its bar line leads in, as often as START asks and
		// never further back than the room; one given none begins on or after its bar line.
		c.start = -1.f;
		int led = 0, tooFar = 0, without = 0;
		for (uint32_t seed = 1; seed <= 200; seed++) {
			ask.seed = seed;
			PhrasePattern p;
			ask.pickupRoom = 1.5f;
			phraseGenerate(ask, c, p);
			if (p.noteCount > 0 && p.notes[0].offset < -1e-4f)
				led++;
			if (p.noteCount > 0 && p.notes[0].offset < -1.5f - 1e-4f)
				tooFar++;
			ask.pickupRoom = 0.f;
			phraseGenerate(ask, c, p);
			if (p.noteCount > 0 && p.notes[0].offset < -1e-4f)
				without++;
		}
		const bool pickups = led > 100 && tooFar == 0 && without == 0;
		std::printf("a phrase leads in within the room it is given, and not without it: %s"
			"  (%d of 200 with a beat and a half, %d past it, %d with none)\n",
			pickups ? "yes" : "NO", led, tooFar, without);
		failures += pickups ? 0 : 1;
		ask.pickupRoom = 0.f;
		c.start = 0.f;

		// THE LOUDNESS SHAPE: an arch is loudest in the middle of a phrase, a fall at its start,
		// and with DYNAMICS at nought every note is the same level.
		auto thirds = [&](float shape, float dynamics, float* out3) {
			c.shape = shape;
			c.dynamics = dynamics;
			float sum[3] = {0.f, 0.f, 0.f};
			int n[3] = {0, 0, 0};
			for (uint32_t seed = 1; seed <= 100; seed++) {
				ask.seed = seed;
				PhrasePattern p;
				phraseGenerate(ask, c, p);
				for (int i = 0; i < p.noteCount; i++) {
					const int k = std::min(2, 3 * i / std::max(1, p.noteCount));
					sum[k] += 20.f * std::log10(p.notes[i].level / 0.5f);
					n[k]++;
				}
			}
			for (int k = 0; k < 3; k++)
				out3[k] = sum[k] / std::max(1, n[k]);
		};
		float arch[3], fall[3], flat[3];
		thirds(1.f, 1.f, arch);
		thirds(0.f, 1.f, fall);
		thirds(1.f, 0.f, flat);
		const bool shaped = arch[1] > arch[0] + 1.f && arch[1] > arch[2] + 1.f
			&& fall[0] > fall[1] + 1.f && fall[1] > fall[2]
			&& std::fabs(flat[0] - flat[2]) < 0.01f && std::fabs(flat[1] - flat[2]) < 0.01f;
		std::printf("the loudness arches, falls, or stays level as asked: %s"
			"  (arch %+.1f %+.1f %+.1f dB, fall %+.1f %+.1f %+.1f)\n", shaped ? "yes" : "NO",
			arch[0], arch[1], arch[2], fall[0], fall[1], fall[2]);
		failures += shaped ? 0 : 1;
		c.shape = 0.7f;
		c.dynamics = 0.5f;
	}

	// ---- MOTIF -------------------------------------------------------------------------------
	//
	// A group restating an earlier line's rhythm: at a pop setting about a third of the lines do,
	// as a third do in the thirty pop songs; every echo names a note that is really there, in the
	// same place in the beat; and nothing overlaps.
	{
		PhraseControls c;
		phraseStyle(STYLE_POP2, c);
		float ch[4] = {0.f, 4.f, 8.f, 12.f};
		PhrasePattern keep;
		bool have = false;
		int lines = 0, restated = 0, echoes = 0, wrong = 0, overlaps = 0;
		for (uint32_t seed = 1; seed <= 400; seed++) {
			PhraseAsk a;
			a.phraseBeats = 16.f; a.barBeats = 4.f; a.beatsPerSecond = 2.f;
			a.seed = seed; a.cyclePosition = (int) (seed % 5);
			a.changes = ch; a.changeCount = 4;
			a.previous = have ? &keep : NULL;
			PhrasePattern p;
			phraseGenerate(a, c, p);
			lines += p.groupCount;
			int lastGroup = -1;
			for (int i = 0; i < p.noteCount; i++) {
				const PhraseNote& n = p.notes[i];
				if (i > 0 && n.offset < p.notes[i - 1].offset + 0.01f)
					overlaps++;
				if (n.echo <= 0)
					continue;
				echoes++;
				if (n.group != lastGroup) {
					restated++;
					lastGroup = n.group;
				}
				const int b = n.echo;
				const PhraseNote* src = b <= i ? &p.notes[i - b]
					: (have && b - i <= keep.noteCount ? &keep.notes[keep.noteCount - (b - i)] : NULL);
				const float fa = n.offset - std::floor(n.offset);
				if (!src || src->id != n.echoOf
						|| (!n.triplet && !src->triplet
							&& std::fabs(fa - (src->offset - std::floor(src->offset))) > 0.01f))
					wrong++;
			}
			keep = p;
			have = true;
		}
		const float share = (float) restated / (float) std::max(1, lines);
		const bool ok = share > 0.25f && share < 0.45f && echoes > 0 && wrong == 0 && overlaps == 0;
		std::printf("a line restates an earlier line's rhythm about a third of the time: %s"
			"  (%.0f%% of %d lines, %d echoed notes, %d misnamed, %d overlapping)\n", ok ? "yes" : "NO",
			100.f * share, lines, echoes, wrong, overlaps);
		failures += ok ? 0 : 1;
	}

	// ---- THE EXAMPLES PHRASE AS A MUSICIAN READS THEM ------------------------------------
	//
	// They are what everything is judged against, so if the chart ever phrases one differently
	// from how it was meant, every listening test after that is testing the wrong thing without
	// anybody knowing. Checked exactly, bar by bar and cadence by cadence.
	{
		int wrong = 0;
		for (const ChartExample& x : chartExamples()) {
			Song song = irealParseSong(chartExampleChunk(x));
			std::vector<ChartBar> bars = chartLayout(song);
			ChartPlayback pb = chartPlayback(bars);
			std::vector<ChartPhrase> ph = chartPhrases(bars, pb);
			std::string got;
			for (size_t i = 0; i < ph.size(); i++) {
				char buf[64];
				std::snprintf(buf, sizeof(buf), "%s%d-%d %s", i ? ", " : "", ph[i].startBar + 1,
					ph[i].endBar, chartCadenceName(ph[i].cadence));
				got += buf;
			}
			if (got != x.expect) {
				wrong++;
				std::printf("   %s: phrased \"%s\", meant \"%s\"\n", x.title, got.c_str(),
					x.expect);
			}
		}
		std::printf("every example phrases as a musician reads it: %s  (%d examples)\n",
			wrong ? "NO" : "yes", (int) chartExamples().size());
		failures += wrong ? 1 : 0;
	}

	// A LOOP IS THE SONG'S OWN PHRASING, HEARD THROUGH A WINDOW. Example 7, the period twice:
	// looping bars 5 to 8 gives the song's second phrase whole; looping 3 to 6 gives the second
	// half of the first phrase and the first half of the second, each still the whole phrase.
	{
		const ChartExample& x = chartExamples()[6];
		const Song song = irealParseSong(chartExampleChunk(x));
		const std::vector<ChartBar> bars = chartLayout(song);
		const ChartPlayback whole = chartPlayback(bars);
		const std::vector<ChartPhrase> all = chartPhrases(bars, whole);
		int from = -1;
		ChartPlayback loop = chartPlaybackForBars(whole, 4, 7, &from);
		std::vector<ChartPhrase> in = chartPhrasesInLoop(all, whole, loop, from);
		bool ok = loop.timeline.size() == 4 && in.size() == 1 && in[0].number == 1
			&& in[0].cadence == CADENCE_AUTHENTIC && std::fabs(in[0].startBeat) < 1e-4f
			&& std::fabs(in[0].endBeat - 16.f) < 1e-3f && !in[0].cut;
		loop = chartPlaybackForBars(whole, 2, 5, &from);
		in = chartPhrasesInLoop(all, whole, loop, from);
		// Bars 3 to 6: the end of the first phrase and the start of the second, each the WHOLE
		// phrase as the song has it — its length, its cadence — heard only where the loop is.
		ok = ok && in.size() == 2 && in[0].number == 0 && in[0].cadence == CADENCE_HALF
			&& in[0].cut && std::fabs(in[0].fullStart + 8.f) < 1e-3f
			&& std::fabs(in[0].fullEnd - 8.f) < 1e-3f
			&& in[1].number == 1 && in[1].cadence == CADENCE_AUTHENTIC && in[1].cut
			&& std::fabs(in[1].startBeat - 8.f) < 1e-3f && std::fabs(in[1].endBeat - 16.f) < 1e-3f
			&& std::fabs(in[1].fullEnd - 24.f) < 1e-3f;
		std::printf("a loop keeps the song's own phrases, cut to its bars: %s\n", ok ? "yes" : "NO");
		failures += ok ? 0 : 1;
	}

	int faults = 0;
	for (int k = 0; k < NUM_CASES; k++)
		faults += stats[k].outside + stats[k].overlapping + stats[k].pauseMissing;
	std::printf("notes inside their phrase, not overlapping, every group with a pause: %s"
		"  (%d faults)\n", faults ? "NO" : "yes", faults);
	failures += faults ? 1 : 0;

	std::printf("\n%s\n", failures ? "EXACT CHECKS FAILED" : "exact checks pass");
	return failures ? 1 : 0;
}
