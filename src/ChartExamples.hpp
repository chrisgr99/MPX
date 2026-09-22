#pragma once
/** THE EXAMPLES: short progressions built into the plugin, for hearing what the generators do
with music simple enough to judge.

WHY THEY EXIST. A real chart mixes phrase lengths from two bars to eight and cadences of every
kind, so when a line does not sound musical there is no telling whether the phrase generator, the
chart's phrasing or the melody is at fault. Each of these changes one thing, and every one of them
is phrased by the chart exactly as a musician reads it — which is checked, in `make charttest`, so
an example that stops phrasing correctly is caught rather than silently testing the wrong thing.

WRITTEN AS PLAIN CHORD TEXT, in iReal Pro's own notation, and turned into the stored form an
imported song uses. So the chart module and everything downstream treat them exactly as any other
song, and there is no second chart format to keep in step.

ALL IN C BUT ONE, which is in A minor, so a log or a printout reads without transposing.
*/
#include "IReal.hpp"

#include <string>
#include <vector>

namespace px {

struct ChartExample {
	const char* title;
	/** iReal Pro's key field: a letter, with "-" for minor. */
	const char* key;
	/** The bars, in iReal Pro's notation: "|" between bars, "Z" at the end. */
	const char* body;
	/** How the chart should phrase it, as a musician would: each phrase's bars and how it ends. */
	const char* expect;
};

/** The playlist every examples list is shown under. */
static const char* const CHART_EXAMPLES_PLAYLIST = "Examples";

inline const std::vector<ChartExample>& chartExamples() {
	static const std::vector<ChartExample> examples = {
		// A QUESTION AND AN ANSWER: four bars stopping on the dominant, four arriving home. The
		// simplest thing a melody can be asked to do, and the one that tests whether a half
		// cadence is heard as a half cadence.
		{"1 Period, I IV I V / I IV V I", "C",
			"{*AT44C   |F   |C   |G   |C   |F   |G7  |C   Z",
			"1-4 half, 5-8 authentic"},
		// THE COMMONEST LOOP IN POP, arriving only at the end.
		{"2 Pop loop, I vi IV V", "C",
			"{*AT44C   |A-  |F   |G   |C   |A-  |F G7|C   Z",
			"1-4 half, 5-8 authentic"},
		// THE JAZZ CADENCE WITH NOTHING AROUND IT, each four bars arriving in its third bar and
		// holding the tonic for the fourth.
		{"3 Two five one, twice", "C",
			"{*AT44D-7 |G7  |C^7 |C^7 |D-7 |G7  |C^7 |C^7 Z",
			"1-4 authentic, 5-8 authentic"},
		// THE SAME SHAPE WITH THE HARMONY TWICE AS FAST, which separates what chord changes do to
		// a line from what phrase length does to it.
		{"4 Two chords a bar", "C",
			"{*AT44C F |G C |C F |G7 C|C F |G C |F G7|C   Z",
			"1-4 authentic, 5-8 authentic"},
		// A MINOR KEY, so an ending can be checked for landing on a minor tonic.
		{"5 Minor period, i iv i V / i iv V i", "A-",
			"{*AT44A-  |D-  |A-  |E7  |A-  |D-  |E7  |A-  Z",
			"1-4 half, 5-8 authentic"},
		// ONE PHRASE ONLY: shorter than a group length leaves room for, which is where a phrase
		// generator is most tempted to play a stub.
		{"6 Four bars, I IV V I", "C",
			"{*AT44C   |F   |G7  |C   Z",
			"1-4 authentic"},
		// THE PERIOD TWICE, so a repeat and a cycle have something to come round to.
		{"7 Sixteen bars, the period twice", "C",
			"{*AT44C   |F   |C   |G   |C   |F   |G7  |C   |C   |F   |C   |G   |C   |F   |G7  |C   Z",
			"1-4 half, 5-8 authentic, 9-12 half, 13-16 authentic"},
	};
	return examples;
}

/** THE STORED FORM an imported song has: iReal Pro's record, with its bars scrambled the way
the format scrambles them. The scrambling swaps characters in pairs, so the same function that
reads it also writes it. */
inline std::string chartExampleChunk(const ChartExample& e) {
	return std::string(e.title) + "=" + CHART_EXAMPLES_PLAYLIST + "==Pop=" + e.key
		+ "==1r34LbKcu7" + irealUnscramble(e.body) + "=Pop=100=1";
}

} // namespace px
