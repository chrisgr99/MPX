#pragma once
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
