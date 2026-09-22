#pragma once
#include <cstdint>
/** Laying a chart out as it is WRITTEN, rather than as it is played.

Two different things are wanted from one chart. Playing it needs every repeat taken and every
ending chosen, which is what irealExpand gives — a flat run of chords with beat numbers.
Reading it needs the opposite: the chart as the copyist wrote it, four bars to a line, with the
repeat marks and endings still standing, because those marks ARE the structure and a chart with
them written out is three times longer and says nothing about the form.

So this is the second view. It turns the cells into BARS carrying their decorations, and groups
those bars into ROWS. It computes no pixels and knows nothing about drawing; what it produces is
a description that a display renders and that other things — section ranges, a now-playing
cursor — can be asked questions about.

PORTED FROM GXW, whose harmonyChartLayout.js does this correctly and has been read against real
charts for a long time. The rules below are its rules, and the comments that explain WHY a rule
is the way it is are worth more than the code, so they came across too.

NO RACK IN HERE, like IReal.hpp, so the whole thing can be run over a couple of thousand real
charts from a command line and read.
*/
#include "IReal.hpp"

#include <string>
#include <vector>

namespace px {


/** One chord position inside a bar. A bar usually holds one; a split bar holds two. */
struct ChartSlot {
	enum Simile {
		SIMILE_NONE,
		SIMILE_ONE,     /**< % — play the previous bar again. */
		SIMILE_TWO,     /**< %% — play the previous TWO bars again. */
		SIMILE_LAST,    /**< the held-bar mark. */
	};
	Chord chord;
	bool noChord = false;
	bool empty = false;
	Simile simile = SIMILE_NONE;
	/** The symbol as written, kept for anything the parser could not read. */
	std::string raw;
	/** True when nothing but `raw` can be shown: an unparseable chord. */
	bool plain = false;
};


/** Where a D.C. or a D.S. sends the music. */
struct ChartNav {
	enum From { NAV_NONE, NAV_DC, NAV_DS };
	enum Target { TO_END, TO_CODA, TO_FINE, TO_ENDING };
	From from = NAV_NONE;
	Target target = TO_END;
	int ending = 0;    /**< TO_ENDING only. */
};


/** One bar of the chart as written, with everything that decorates it. */
struct ChartBar {
	int index = 0;          /**< Position in the whole chart, counted from nought. */
	float beatStart = 0.f;  /**< Beats before this bar, as written rather than as played. */
	int beats = 4;          /**< Beats in this bar, from the running time signature. */
	std::vector<ChartSlot> slots;

	char section = 0;       /**< A rehearsal letter opening here. */
	bool repeatOpen = false;
	bool repeatClose = false;
	int ending = 0;         /**< An ending bracket opening here: 1, 2, … */
	bool end = false;       /**< The final barline. */
	bool doubleRight = false; /**< A double barline: a section boundary that is not the end. */
	int timeBeats = 0, timeUnit = 0;  /**< A meter change announced here. */
	bool segno = false;
	bool coda = false;
	bool codaAfter = false; /**< The coda sign sits at this bar's END, so the bar plays first. */
	bool fine = false;
	int passes = 0;         /**< A repeat count written as "3x" on the repeat opened here. */
	ChartNav nav;           /**< A jump that fires after this bar. */
};


/** A cell of a laid-out row: a bar, or a blank column used to indent an ending. */
struct RowCell {
	bool empty = true;
	int bar = -1;   /**< Index into the bar list. */
};


/** What a comment turned out to be. iReal writes the navigation in comments. */
struct NavComment {
	enum Kind { NONE, JUMP, FINE, REPEAT };
	Kind kind = NONE;
	ChartNav jump;
	int times = 0;   /**< REPEAT only. */
};

/** Reads one comment as a navigation instruction, a Fine, a repeat count, or nothing. */
NavComment chartClassifyNav(const std::string& text);


/** The chart as written: cells into bars, with their decorations. */
std::vector<ChartBar> chartLayout(const Song& song);

/** The bars grouped into rows for a grid of `barsPerRow` equal columns.

Every row uses the same number of columns, so the barlines run straight down the page — which is
what makes a chart readable at a glance and is the whole reason for laying it out rather than
letting it wrap. */
std::vector<std::vector<RowCell> > chartRows(const std::vector<ChartBar>& bars, int barsPerRow);


/** One bar as PLAYED: which written bar is sounding, and over which beats.

`bar` points back into the laid-out chart, so the same written bar appears here once for each
time it is played — which is exactly what a cursor needs in order to light the right measure on
the second pass through a repeat. */
struct PlayedBar {
	int bar = 0;
	float startBeat = 0.f;
	float endBeat = 0.f;
};

struct ChartPlayback {
	std::vector<PlayedBar> timeline;
	float totalBeats = 0.f;
	/** Anything the sequencer could not make sense of. It never refuses to play. */
	std::vector<std::string> notes;
};

/** Walks the written chart into the order it is played: repeats taken the right number of times,
the right ending chosen on each pass, and the segno, coda, D.C., D.S. and Fine obeyed.

ONE ENGINE FOR BOTH the sound and the cursor. Two walks would drift apart at the first unusual
chart and the cursor would light a bar the ear was not hearing, which is worse than no cursor. */
ChartPlayback chartPlayback(const std::vector<ChartBar>& bars);

/** HOW A PHRASE ENDS. Published on the cable, because the kind of ending decides what a line does
there: a full close thins and holds, a half cadence pauses and expects an answer, an evaded close
runs on. */
enum ChartCadence : uint8_t {
	CADENCE_NONE,           /**< A boundary with no cadence at it: a section end, or the fallback. */
	CADENCE_AUTHENTIC,      /**< The dominant to the tonic. */
	CADENCE_PLAGAL,         /**< The four chord to the tonic. */
	CADENCE_BACKDOOR,       /**< The flat seven dominant to the tonic. */
	CADENCE_TRITONE,        /**< The flat two dominant to the tonic. */
	CADENCE_HALF,           /**< Ending on the dominant, held. */
	CADENCE_DECEPTIVE,      /**< The dominant to the six chord. Never a phrase end: it runs on. */
	NUM_CADENCES,
};

/** A short name for a cadence type, for a display or a log. */
const char* chartCadenceName(int cadence);

/** ONE CHORD CHANGE AS PLAYED: where it falls and what it changes to.

Resolved the way the module resolves harmony — a blank slot holds the chord before it, and a
simile repeats the bar before — so that what is phrased is exactly what is heard. */
struct ChartChange {
	float beat = 0.f;       /**< From the start of the played cycle. */
	int playedBar = 0;      /**< Index into the playback timeline. */
	Chord chord;
};

/** THE CHORD SOUNDING at a place in the played cycle, the way the chart module plays it.

`at` is an index into the playback timeline and `within` is beats into that bar. A blank slot
holds the chord before it within the bar; a simile, or a bar with no slots, asks the bar before
it at the same place. Returns false where nothing is sounding, and `toNext` is beats until this
slot gives way.

SHARED ON PURPOSE. The module plays through this function and the census checks through it, so
a check that the chart publishes the changes it plays is a check of the code that plays them,
not of a second copy that happens to agree. */
bool chartChordAt(const std::vector<ChartBar>& bars, const ChartPlayback& playback, int at,
	float within, Chord& out, float& toNext);

/** Every chord change in played order. */
std::vector<ChartChange> chartChanges(const std::vector<ChartBar>& bars,
	const ChartPlayback& playback);

/** What kind of cadence a change from `from` to `to` makes, given how long `to` then sounds. */
ChartCadence chartCadenceOf(const Chord& from, const Chord& to, float heldBeats, float barBeats);

/** A PHRASE: from one boundary to the next, on bar lines, and how it ends. */
struct ChartPhrase {
	float startBeat = 0.f, endBeat = 0.f;
	int startBar = 0, endBar = 0;           /**< Played bars; endBar is one past the last. */
	ChartCadence cadence = CADENCE_NONE;
	/** ITS NUMBER IN THE WHOLE FORM, where it has been cut out of it by a loop, or -1 for its
	place in the list. A looped passage is the song's own phrases, so it keeps the song's numbers
	and draws exactly what the song draws there. */
	int number = -1;
	/** WHERE THE WHOLE PHRASE LIES, in beats of the played walk, when a loop has cut it: it may
	begin before the loop does and end after. A rhythm is decided for the whole phrase and only
	the part inside the loop is heard, so the loop plays what the song plays there. Equal to the
	start and end when the phrase is whole. `changes` and `cadence` are the whole phrase's. */
	float fullStart = 0.f, fullEnd = 0.f;
	bool cut = false;

	/** EVERY CHORD CHANGE INSIDE THE PHRASE, in beats from its start, so that a rhythm can be
	generated for the whole phrase at once rather than discovering each change as it arrives. */
	std::vector<float> changes;

	/** WHERE THE PHRASE SITS IN THE FORM. The section letter, or nought before the chart names
	one; which time that section has begun in this pass, counting from one, so the last A of an
	A A B A form is the third; and which phrase of that section this is, counting from nought. A
	returning section's phrases can be matched to its first appearance's by the last two. */
	char section = 0;
	int sectionAppearance = 0;
	int phraseInSection = 0;
};

/** WHERE THE PHRASES FALL: four-bar units laid out from the start of each section, contiguous,
covering the whole played cycle.

THE FORM FIRST, THE HARMONY SECOND. A sung phrase is four bars far more often than not, and in a
pop song it is four bars whatever the chords do: the chords only colour how it ends. So the units
are laid out first and each takes as its cadence the last one arriving in its final two bars, or
none. Two readings this replaced — cadence to cadence, with four bars as a preference — split
songs at every dominant held for a bar and at every bar of a repeated vamp.

A section whose length is not a multiple of four gives the leftover bars to its last phrase; one
shorter than four bars is a single phrase.

A PHRASE IS NOT A BREATH. Four bars is often longer than a singer's breath; the breaths inside a
phrase are mpxPhrase's groups, set in seconds.

A HALF CADENCE is a dominant held for a bar or more. It is a melodic fact that chords can only
suggest, and this is the suggestion. A DECEPTIVE CADENCE never ends a phrase.

SECTIONS ARE HARD BOUNDARIES. A phrase never crosses the start of a section. */
std::vector<ChartPhrase> chartPhrases(const std::vector<ChartBar>& bars,
	const ChartPlayback& playback);

/** The length of a phrase, in bars, before a section's leftover is added to its last. */
static const int PHRASE_BARS = 4;


/** THE LOOP: the played walk from written bar `first` to written bar `last`, both included,
taken from the first place in the whole walk where `first` is played and running on until `last`
has been — the bars as the song plays them, once. Renumbered from nought. `from` receives the
index in the whole walk where it begins. An empty walk when the range is never played. */
ChartPlayback chartPlaybackForBars(const ChartPlayback& whole, int first, int last, int* from);

/** THE SONG'S OWN PHRASES, CUT TO A LOOP. Each phrase of `whole` that overlaps the loop — which
begins at played index `from` of `wholePlayback` and runs as `loop` does — is kept, clipped to it
and moved to its beats, with its number in the song. A phrase cut short at the loop's end loses
its cadence, since the loop ends before the cadence does. So a looped passage is phrased exactly
as it is in the song. */
std::vector<ChartPhrase> chartPhrasesInLoop(const std::vector<ChartPhrase>& whole,
	const ChartPlayback& wholePlayback, const ChartPlayback& loop, int from);


/** The same walk, keeping only the bars that belong to one section's label.

THE FORM BECOMES SHORTER, not gapped. A section chosen out of a chart is played as though the
rest of the chart were not there — the kept bars are renumbered so they run without a break —
because a form with silence where the other sections were is not a form anybody can play to.

A label owns every occurrence of itself: an AABA chart with A chosen plays all three A's in the
order they come, which is what a musician means by "just the A section". */
ChartPlayback chartPlaybackForLabel(const std::vector<ChartBar>& bars,
	const ChartPlayback& whole, char label);


/** One section: its letter and the bars it covers, both ends included. */
struct ChartSection {
	char label = 0;
	int first = 0;
	int last = 0;
};

/** Every labelled section, in the order written. */
std::vector<ChartSection> chartSections(const std::vector<ChartBar>& bars);

/** Every section carrying this letter. A letter recurs in an AABA form, and choosing it means
choosing all of them. */
std::vector<ChartSection> chartRangesForLabel(const std::vector<ChartBar>& bars, char label);


} // namespace px
