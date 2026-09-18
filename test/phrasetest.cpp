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
		{"jazz, 120 bpm", STYLE_JAZZ, 2.f},
		{"jazz, 240 bpm", STYLE_JAZZ, 4.f},
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
	PhrasePattern p1, p2;
	ask.seed = 1; phraseGenerate(ask, c, p1);
	ask.seed = 999; phraseGenerate(ask, c, p2);
	bool alike = p1.noteCount == p2.noteCount;
	for (int i = 0; alike && i < p1.noteCount; i++)
		alike = std::fabs(p1.notes[i].offset - p2.notes[i].offset) < 1e-6f;
	std::printf("variation at nought ignores the seed: %s\n", alike ? "yes" : "NO");
	failures += alike ? 0 : 1;

	// A phrase decided at one tempo and again at another: the groups should hold their length in
	// seconds, not in beats. This is the finding the whole design rests on.
	phraseStyle(STYLE_SONG, c);
	float secs[3], beats[3];
	const float rates[3] = {1.f, 2.f, 4.f};
	for (int i = 0; i < 3; i++) {
		ask.beatsPerSecond = rates[i];
		ask.phraseBeats = 8.f * rates[i];     // eight seconds of music at each tempo
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

	int faults = 0;
	for (int k = 0; k < NUM_CASES; k++)
		faults += stats[k].outside + stats[k].overlapping + stats[k].pauseMissing;
	std::printf("notes inside their phrase, not overlapping, every group with a pause: %s"
		"  (%d faults)\n", faults ? "NO" : "yes", faults);
	failures += faults ? 1 : 0;

	std::printf("\n%s\n", failures ? "EXACT CHECKS FAILED" : "exact checks pass");
	return failures ? 1 : 0;
}
