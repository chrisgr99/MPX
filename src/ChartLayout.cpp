#include "ChartLayout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace px {


// ---- comments, which is where iReal keeps the navigation ------------------------------------

/** Lower case, with the marks that are not part of the words taken out. "*3" is a rehearsal
number and "XyQ" is a stray empty-cell token; neither is language. */
static std::string normaliseComment(const std::string& text) {
	std::string s;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '*' && i + 1 < text.size() && std::isdigit((unsigned char) text[i + 1])) {
			// ALL the digits, not one. "*624x" is a rehearsal number followed by a stray x, and
			// leaving "624x" behind read it as a repeat count of six hundred and twenty-four —
			// which "Chameleon" duly played.
			while (i + 1 < text.size() && std::isdigit((unsigned char) text[i + 1]))
				i++;
			s += ' ';
			continue;
		}
		if (text.compare(i, 3, "XyQ") == 0) {
			i += 2;
			s += ' ';
			continue;
		}
		s += (char) std::tolower((unsigned char) text[i]);
	}
	// One space between words, none at either end.
	std::string out;
	bool space = false;
	for (char c : s) {
		if (std::isspace((unsigned char) c)) {
			space = !out.empty();
			continue;
		}
		if (space)
			out += ' ';
		space = false;
		out += c;
	}
	return out;
}

/** Whether `needle` appears in `hay`. */
static bool has(const std::string& hay, const char* needle) {
	return hay.find(needle) != std::string::npos;
}

/** "d.c." written any of the ways people write it: dc, d.c, d. c., D.C. */
static bool hasAbbrev(const std::string& s, char a, char b) {
	for (size_t i = 0; i + 1 < s.size(); i++) {
		if (s[i] != a)
			continue;
		// AT THE START OF A WORD. Without this, "Vamp and solo till cue" contains a d followed
		// by an s and was read as a D.S. — which, having no segno to return to, cancelled the
		// real D.C. al Fine further down the same chart. "Jogral", and it would have been a
		// hard thing to find by ear.
		if (i > 0 && std::isalnum((unsigned char) s[i - 1]))
			continue;
		size_t j = i + 1;
		if (j < s.size() && s[j] == '.')
			j++;
		while (j < s.size() && s[j] == ' ')
			j++;
		if (j < s.size() && s[j] == b)
			return true;
	}
	return false;
}

NavComment chartClassifyNav(const std::string& text) {
	NavComment out;
	const std::string s = normaliseComment(text);
	if (s.empty())
		return out;

	// A repeat count on its own: "3x", "7 x".
	{
		size_t i = 0;
		while (i < s.size() && std::isdigit((unsigned char) s[i]))
			i++;
		size_t j = i;
		while (j < s.size() && s[j] == ' ')
			j++;
		if (i > 0 && j + 1 == s.size() && s[j] == 'x') {
			out.kind = NavComment::REPEAT;
			out.times = std::atoi(s.c_str());
			return out;
		}
	}

	const bool dc = hasAbbrev(s, 'd', 'c');
	const bool ds = hasAbbrev(s, 'd', 's');
	if (dc || ds) {
		out.kind = NavComment::JUMP;
		out.jump.from = ds ? ChartNav::NAV_DS : ChartNav::NAV_DC;
		out.jump.target = ChartNav::TO_END;
		if (has(s, "al coda"))
			out.jump.target = ChartNav::TO_CODA;
		else if (has(s, "al fine"))
			out.jump.target = ChartNav::TO_FINE;
		else {
			// "al 2nd ending", "al 2 end".
			const size_t at = s.find("al ");
			if (at != std::string::npos) {
				size_t i = at + 3;
				while (i < s.size() && s[i] == ' ')
					i++;
				size_t digits = i;
				while (digits < s.size() && std::isdigit((unsigned char) s[digits]))
					digits++;
				if (digits > i && has(s.substr(digits), "end")) {
					out.jump.target = ChartNav::TO_ENDING;
					out.jump.ending = std::atoi(s.c_str() + i);
				}
			}
		}
		return out;
	}

	// A bare Fine, which is where an al-Fine jump stops. Guarded to the START of the comment so
	// the "fine" inside "d.c. al fine" is not read as one; that case is answered above.
	if (s.compare(0, 4, "fine") == 0
		&& (s.size() == 4 || !std::isalpha((unsigned char) s[4]))) {
		out.kind = NavComment::FINE;
		return out;
	}
	return out;
}


// ---- the layout ------------------------------------------------------------------------------

/** iReal pads before a second ending so that the two line up in ITS layout. We lay the endings
out ourselves, so the padding is a run of blank bars in the middle of the chart that belongs to
nothing. Dropped here, before anything counts a bar — the display and the audio have to agree on
how many bars there are. */
static std::vector<Cell> stripEndingSpacers(const std::vector<Cell>& cells) {
	std::vector<Cell> out;
	for (size_t i = 0; i < cells.size(); i++) {
		out.push_back(cells[i]);
		if (cells[i].kind != Cell::REPEAT_CLOSE)
			continue;
		size_t j = i + 1;
		while (j < cells.size() && (cells[j].kind == Cell::EMPTY
			|| cells[j].kind == Cell::DIVIDER || cells[j].kind == Cell::BAR)) {
			j++;
		}
		// That run is padding only when an ending follows it directly.
		if (j > i + 1 && j < cells.size() && cells[j].kind == Cell::ENDING)
			i = j - 1;
	}
	return out;
}

static int beatsOf(int beats) {
	return beats > 0 ? beats : 4;
}


std::vector<ChartBar> chartLayout(const Song& song) {
	const std::vector<Cell> cells = stripEndingSpacers(song.cells);
	std::vector<ChartBar> bars;

	int beatsPerBar = beatsOf(song.beats);
	float beatCursor = 0.f;
	int barIndex = 0;

	// Decorations waiting for the next bar that gets content.
	ChartBar pending;
	bool havePending = false;
	// Which bars opened the repeats that are still open, so a count written inside one attaches
	// to the bar that opened it.
	std::vector<int> openRepeats;
	bool closeRepeatPending = false;
	bool endPending = false;
	bool doubleRightPending = false;

	ChartBar current;
	bool haveCurrent = false;

	auto startBar = [&]() {
		current = ChartBar();
		current.index = barIndex;
		current.beatStart = beatCursor;
		current.beats = beatsPerBar;
		if (havePending) {
			current.section = pending.section;
			current.repeatOpen = pending.repeatOpen;
			current.ending = pending.ending;
			current.timeBeats = pending.timeBeats;
			current.timeUnit = pending.timeUnit;
			current.segno = pending.segno;
			current.coda = pending.coda;
			current.fine = pending.fine;
			current.nav = pending.nav;
			// A count seen before the bar's first content belongs to the repeat it opens.
			if (pending.passes > 0 && current.repeatOpen)
				current.passes = pending.passes;
			pending = ChartBar();
			havePending = false;
		}
		// A repeat opened by this bar counts as open from NOW, not from when the bar closes: a
		// count written inside the repeat's own first bar belongs to it. The place it will land
		// in the list is not known yet, so it is marked and filled in on the way out.
		if (current.repeatOpen)
			openRepeats.push_back(-1);
		haveCurrent = true;
	};

	auto closeBar = [&]() {
		if (!haveCurrent)
			return;
		// A bar that got no content and carries no decoration is not a bar — a barline before
		// the first chord, say. One that carries a decoration is kept even when empty, because
		// the decoration is structure and dropping it loses the shape.
		const bool decorated = current.section != 0 || current.repeatOpen
			|| current.ending > 0 || current.timeBeats > 0;
		if (current.slots.empty() && !decorated
			&& !closeRepeatPending && !endPending && !doubleRightPending) {
			doubleRightPending = false;
			haveCurrent = false;
			return;
		}
		// (A dropped bar can never have opened a repeat: repeatOpen counts as a decoration.)
		if (current.repeatOpen && !openRepeats.empty() && openRepeats.back() < 0)
			openRepeats.back() = (int) bars.size();
		if (closeRepeatPending) {
			current.repeatClose = true;
			closeRepeatPending = false;
			if (!openRepeats.empty())
				openRepeats.pop_back();
		}
		if (endPending) {
			current.end = true;
			endPending = false;
		}
		if (doubleRightPending) {
			current.doubleRight = true;
			doubleRightPending = false;
		}
		bars.push_back(current);
		beatCursor += current.beats;
		barIndex++;
		haveCurrent = false;
	};

	auto ensureBar = [&]() {
		if (!haveCurrent)
			startBar();
	};

	/** A simile stands for a whole bar of its own. iReal commonly writes "C-7 XyQ Kcl" with no
	barline before the mark, meaning bar one is C minor seven and bar two repeats it — so if the
	open bar already holds a real chord, close it first or the mark is swallowed into that bar
	and a measure disappears from the chart. */
	auto breakBeforeSimile = [&]() {
		if (!haveCurrent)
			return;
		for (const ChartSlot& slot : current.slots) {
			if (!slot.empty) {
				closeBar();
				return;
			}
		}
	};

	for (const Cell& cell : cells) {
		switch (cell.kind) {
			case Cell::CHORD: {
				ensureBar();
				ChartSlot slot;
				slot.noChord = cell.noChord;
				slot.chord = cell.chord;
				slot.raw = cell.raw;
				slot.plain = !cell.noChord && !cell.chord.valid;
				current.slots.push_back(slot);
				break;
			}

			case Cell::EMPTY: {
				ensureBar();
				ChartSlot slot;
				slot.empty = true;
				current.slots.push_back(slot);
				break;
			}

			case Cell::REPEAT_BAR:
			case Cell::REPEAT_TWO:
			case Cell::REPEAT_HELD: {
				breakBeforeSimile();
				ensureBar();
				ChartSlot slot;
				slot.simile = (cell.kind == Cell::REPEAT_BAR) ? ChartSlot::SIMILE_ONE
					: (cell.kind == Cell::REPEAT_TWO) ? ChartSlot::SIMILE_TWO
					: ChartSlot::SIMILE_LAST;
				current.slots.push_back(slot);
				break;
			}

			case Cell::DIVIDER:
				// Separates two chords inside one bar. They are already in the same bar, so
				// there is nothing to do here.
				break;

			case Cell::SECTION: {
				// A section opens the NEXT bar, so the bar in progress is closed first and the
				// one that ENDED the outgoing section gets a double barline. That is either the
				// bar still open, capped through the flag, or — if a barline already closed it —
				// the last one pushed, capped directly. Never the leading section, which ends
				// nothing.
				const bool hadCurrent = haveCurrent;
				if (hadCurrent)
					doubleRightPending = true;
				closeBar();
				if (!hadCurrent && !bars.empty()
					&& !bars.back().end && !bars.back().repeatClose) {
					bars.back().doubleRight = true;
				}
				pending.section = cell.label;
				havePending = true;
				break;
			}

			case Cell::REPEAT_OPEN:
				closeBar();
				pending.repeatOpen = true;
				havePending = true;
				break;

			case Cell::ENDING:
				closeBar();
				pending.ending = cell.ending;
				havePending = true;
				break;

			case Cell::TIME:
				closeBar();
				beatsPerBar = beatsOf(cell.beats);
				pending.timeBeats = cell.beats;
				pending.timeUnit = cell.unit;
				havePending = true;
				break;

			case Cell::REPEAT_CLOSE:
				if (!haveCurrent) {
					// A close after an empty tail. There is no open bar to cap, so it closes on
					// the previous bar of content — which is what the audio does too. Holding it
					// for the NEXT bar would fold that bar into the repeat.
					if (!bars.empty()) {
						bars.back().repeatClose = true;
						if (!openRepeats.empty())
							openRepeats.pop_back();
					}
				}
				else {
					closeRepeatPending = true;
					closeBar();
				}
				break;

			case Cell::BAR:
				closeBar();
				break;

			case Cell::END:
				endPending = true;
				closeBar();
				break;

			case Cell::SEGNO:
				if (haveCurrent)
					current.segno = true;
				else {
					pending.segno = true;
					havePending = true;
				}
				break;

			case Cell::CODA:
				// On a bar in progress the sign sits AFTER its chords, so an al-Coda return
				// plays that bar and then jumps. With no bar open it sits at the next bar's
				// start and the jump happens before that bar sounds.
				if (haveCurrent) {
					current.coda = true;
					current.codaAfter = true;
				}
				else {
					pending.coda = true;
					havePending = true;
				}
				break;

			case Cell::COMMENT: {
				const NavComment nav = chartClassifyNav(cell.raw);
				if (nav.kind == NavComment::JUMP) {
					if (haveCurrent)
						current.nav = nav.jump;
					else {
						pending.nav = nav.jump;
						havePending = true;
					}
				}
				else if (nav.kind == NavComment::FINE) {
					if (haveCurrent)
						current.fine = true;
					else {
						pending.fine = true;
						havePending = true;
					}
				}
				else if (nav.kind == NavComment::REPEAT) {
					if (!openRepeats.empty() && openRepeats.back() < 0)
						current.passes = nav.times;      // the repeat opened by the bar we are in
					else if (!openRepeats.empty())
						bars[openRepeats.back()].passes = nav.times;
					else {
						pending.passes = nav.times;
						havePending = true;
					}
				}
				break;
			}

			default:
				break;
		}
	}

	closeBar();
	return bars;
}


// ---- rows --------------------------------------------------------------------------------

std::vector<std::vector<RowCell> > chartRows(const std::vector<ChartBar>& bars, int barsPerRow) {
	const int cols = barsPerRow > 0 ? barsPerRow : 4;
	std::vector<std::vector<RowCell> > rows;
	std::vector<RowCell> row;

	// The column the FIRST ending of the current group sits in, so a later alternative can be
	// indented to the same one. Cleared when a section starts, not when an undecorated bar goes
	// by: the tail of a first ending carries no ending number of its own, and clearing on those
	// would lose the column before the second ending arrived.
	int firstEndingCol = -1;
	int firstEndingNumber = -1;

	auto flush = [&]() {
		if (!row.empty())
			rows.push_back(row);
		row.clear();
	};

	for (size_t i = 0; i < bars.size(); i++) {
		const ChartBar& bar = bars[i];
		const bool opensSection = bar.section != 0;
		const bool opensEnding = bar.ending > 0;

		if (opensEnding) {
			if (firstEndingCol < 0) {
				// The first of a stacked group carries on where it is, and records where its
				// first bar landed so the others can be put under it.
				//
				// AFTER THE WRAP, not before it. A first ending that opens on a full row goes
				// to the next row's first column, and recording the column it would have had
				// on the old row put the second ending a whole row further right — which,
				// being wider than the row, became a blank row followed by an ending back at
				// column one, aligned with nothing. GXW has this fault too; this is where it
				// is fixed rather than reproduced.
				firstEndingCol = ((int) row.size() >= cols) ? 1 : (int) row.size() + 1;
				firstEndingNumber = bar.ending;
			}
			else if (bar.ending != firstEndingNumber) {
				// A second or later ending starts a new row, padded on the left so its first
				// bar lands directly beneath the first ending's.
				flush();
				for (int k = 1; k < firstEndingCol; k++)
					row.push_back(RowCell());
				firstEndingNumber = bar.ending;
			}
			// A continuation bar of the SAME ending just packs on below.
		}
		else if (opensSection) {
			firstEndingCol = -1;
			firstEndingNumber = -1;
			flush();
		}

		if ((int) row.size() >= cols)
			flush();

		RowCell cell;
		cell.empty = false;
		cell.bar = (int) i;
		row.push_back(cell);
	}

	flush();
	return rows;
}


// ---- the played order --------------------------------------------------------------------

/** The highest ending number in the group that starts at `from`. The group runs on to the next
repeat opening, which is a fresh block, or to the end. */
static int maxEndingFrom(const std::vector<ChartBar>& bars, size_t from) {
	int max = bars[from].ending > 0 ? bars[from].ending : 1;
	for (size_t j = from + 1; j < bars.size(); j++) {
		if (bars[j].repeatOpen)
			break;
		if (bars[j].ending > max)
			max = bars[j].ending;
	}
	return max;
}

/** Where the ending block opening at `s` stops, and whether a repeat closes on its last bar. */
static void endingBlockEnd(const std::vector<ChartBar>& bars, size_t s,
	size_t& endExclusive, bool& hadClose) {

	size_t j = s;
	hadClose = false;
	while (j < bars.size()) {
		if (j > s && bars[j].ending > 0)
			break;
		if (bars[j].repeatClose) {
			hadClose = true;
			j++;
			break;
		}
		j++;
	}
	endExclusive = j;
}


ChartPlayback chartPlayback(const std::vector<ChartBar>& bars) {
	ChartPlayback out;
	const int n = (int) bars.size();
	if (n == 0)
		return out;

	// The jump and its anchors, settled before the walk starts. There is at most one: a chart
	// with two D.C.s is not a chart anybody could play either.
	int navIndex = -1;
	for (int k = 0; k < n; k++) {
		if (bars[k].nav.from != ChartNav::NAV_NONE) {
			navIndex = k;
			break;
		}
	}
	int segnoIndex = -1;
	for (int k = 0; k < n; k++) {
		if (bars[k].segno) {
			segnoIndex = k;
			break;
		}
	}

	int toCodaIndex = -1, codaStartIndex = -1;
	if (navIndex >= 0) {
		const ChartNav& nav = bars[navIndex].nav;
		if (nav.from == ChartNav::NAV_DS && segnoIndex < 0) {
			out.notes.push_back("D.S. with no segno; navigation ignored");
			navIndex = -1;
		}
		else if (nav.target == ChartNav::TO_CODA) {
			for (int k = navIndex; k >= 0; k--) {
				if (bars[k].coda) {
					toCodaIndex = k;
					break;
				}
			}
			for (int k = navIndex + 1; k < n; k++) {
				if (bars[k].coda) {
					codaStartIndex = k;
					break;
				}
			}
			if (toCodaIndex < 0 || codaStartIndex < 0) {
				out.notes.push_back("al Coda: coda signs not found; navigation ignored");
				navIndex = -1;
			}
		}
	}
	const ChartNav navTarget = navIndex >= 0 ? bars[navIndex].nav : ChartNav();

	struct Frame {
		int openIndex;
		int passes;
		int pass;
	};
	std::vector<Frame> stack;
	int lastClosedPass = 1;
	bool returning = false;   /**< On the way back after a D.C. or D.S. */
	bool navFired = false;
	int i = 0;
	long guard = 0;

	std::vector<int> order;
	while (i < n) {
		if (++guard > 1000000) {
			out.notes.push_back("sequencer guard tripped");
			break;
		}
		const ChartBar& bar = bars[i];

		// The to-Coda jump when the sign sits at the bar's START: on the way back, leap before
		// this bar sounds. The commoner case, the sign at the bar's end, is below.
		if (returning && navTarget.target == ChartNav::TO_CODA
			&& i == toCodaIndex && !bar.codaAfter) {
			i = codaStartIndex;
			continue;
		}

		// A repeat frame opens the first time its mark is reached, never on the way back round
		// — the frame already covers this bar — and never on the return trip, which does not
		// take repeats at all.
		if (!returning && bar.repeatOpen
			&& (stack.empty() || stack.back().openIndex != i)) {
			Frame frame;
			frame.openIndex = i;
			frame.passes = bar.passes > 0 ? bar.passes : 2;
			frame.pass = 1;
			stack.push_back(frame);
		}

		// Which ending to take.
		if (bar.ending > 0) {
			bool play;
			if (returning) {
				// On the way back, the named ending if one was named, and otherwise the first —
				// which is the path that does not repeat.
				play = (navTarget.target == ChartNav::TO_ENDING)
					? (bar.ending == navTarget.ending) : (bar.ending == 1);
			}
			else {
				const int pass = stack.empty() ? lastClosedPass : stack.back().pass;
				// Ending k plays on pass k, and the last ending also covers any pass beyond it,
				// for a block repeated more times than it has endings.
				play = (bar.ending == pass)
					|| (bar.ending == maxEndingFrom(bars, i) && pass > bar.ending);
			}
			if (!play) {
				size_t endExclusive = 0;
				bool hadClose = false;
				endingBlockEnd(bars, (size_t) i, endExclusive, hadClose);
				if (hadClose && !returning && !stack.empty()) {
					lastClosedPass = stack.back().pass;
					stack.pop_back();
				}
				i = (int) endExclusive;
				continue;
			}
		}

		order.push_back(i);

		// The to-Coda jump when the sign sits at the bar's END, which is how iReal writes it:
		// the bar is PLAYED and only then does the music leap. Jumping first drops the bar and
		// puts the whole form a measure out.
		if (returning && navTarget.target == ChartNav::TO_CODA
			&& i == toCodaIndex && bar.codaAfter) {
			i = codaStartIndex;
			continue;
		}

		// On the way back, a Fine is where it stops.
		if (returning && navTarget.target == ChartNav::TO_FINE && bar.fine)
			break;

		// A repeat closes: go round again while passes remain, else retire the frame.
		if (!returning && bar.repeatClose) {
			if (!stack.empty() && stack.back().pass < stack.back().passes) {
				stack.back().pass++;
				i = stack.back().openIndex;
				continue;
			}
			if (!stack.empty()) {
				lastClosedPass = stack.back().pass;
				stack.pop_back();
			}
		}

		// The jump fires only once this bar's repeats have finished with it.
		if (!returning && !navFired && navIndex >= 0 && i == navIndex) {
			navFired = true;
			returning = true;
			if (!stack.empty()) {
				out.notes.push_back("D.C./D.S. fired inside an open repeat");
				stack.clear();
			}
			i = (navTarget.from == ChartNav::NAV_DC) ? 0 : segnoIndex;
			continue;
		}

		i++;
	}

	float beat = 0.f;
	for (size_t k = 0; k < order.size(); k++) {
		const ChartBar& bar = bars[order[k]];
		// A two-bar simile is ONE written bar standing for TWO of music, and the sound plays
		// both — so it holds the cursor for twice as long, which keeps the light in step with
		// the ear.
		bool twoBars = false;
		for (const ChartSlot& slot : bar.slots)
			twoBars = twoBars || slot.simile == ChartSlot::SIMILE_TWO;
		const float span = (float) bar.beats * (twoBars ? 2.f : 1.f);
		PlayedBar played;
		played.bar = order[k];
		played.startBeat = beat;
		played.endBeat = beat + span;
		out.timeline.push_back(played);
		beat += span;
	}
	out.totalBeats = beat;
	return out;
}


// ---- sections ----------------------------------------------------------------------------

std::vector<ChartSection> chartRangesForLabel(const std::vector<ChartBar>& bars, char label);

/** A bar holding no chord: an iReal spacer, or padding. */
static bool isBlankBar(const ChartBar& bar) {
	if (bar.slots.empty())
		return true;
	for (const ChartSlot& slot : bar.slots) {
		if (!slot.empty)
			return false;
	}
	return true;
}

/** Where a section that opens at `start` ends.

It runs to the bar before the next STRUCTURAL boundary — another section, or a fresh repeat
block opened after this section's own repeat has closed, which is how a trailing tag or a
repeat-and-fade coda is written — or to the end of the chart. Trailing blanks are trimmed.

So clicking a letter selects that section and not a tag that happens to follow it. A section's
OWN internal repeat does not end it; only a new block opened after its content has closed. */
static bool sectionRange(const std::vector<ChartBar>& bars, int start, int& last) {
	const int n = (int) bars.size();
	if (n == 0)
		return false;
	if (start < 0)
		start = 0;
	if (start > n - 1)
		start = n - 1;
	int end = n - 1;
	bool closed = false;
	for (int i = start + 1; i < n; i++) {
		if (bars[i].section != 0) {
			end = i - 1;
			break;
		}
		if (bars[i].repeatOpen && closed) {
			end = i - 1;
			break;
		}
		if (bars[i].repeatClose)
			closed = true;
	}
	while (end > start && isBlankBar(bars[end]))
		end--;
	last = end;
	return true;
}


std::vector<ChartSection> chartSections(const std::vector<ChartBar>& bars) {
	std::vector<ChartSection> out;
	for (size_t i = 0; i < bars.size(); i++) {
		if (bars[i].section == 0)
			continue;
		int last = 0;
		if (!sectionRange(bars, (int) i, last))
			continue;
		ChartSection sec;
		sec.label = bars[i].section;
		sec.first = (int) i;
		sec.last = last;
		out.push_back(sec);
	}
	return out;
}


ChartPlayback chartPlaybackForLabel(const std::vector<ChartBar>& bars,
	const ChartPlayback& whole, char label) {

	ChartPlayback out;
	if (label == 0)
		return whole;
	const std::vector<ChartSection> ranges = chartRangesForLabel(bars, label);
	if (ranges.empty())
		return whole;

	float beat = 0.f;
	for (size_t i = 0; i < whole.timeline.size(); i++) {
		const int bar = whole.timeline[i].bar;
		bool keep = false;
		for (size_t r = 0; r < ranges.size() && !keep; r++)
			keep = (bar >= ranges[r].first && bar <= ranges[r].last);
		if (!keep)
			continue;
		PlayedBar played;
		played.bar = bar;
		played.startBeat = beat;
		// The bar keeps its own length, which matters where the meter changes inside a chart.
		played.endBeat = beat + (whole.timeline[i].endBeat - whole.timeline[i].startBeat);
		beat = played.endBeat;
		out.timeline.push_back(played);
	}
	out.totalBeats = beat;
	if (!out.timeline.empty())
		return out;

	// NOTHING KEPT, which happens more often than it sounds as though it should. A coda, a tag,
	// or a section after a D.C. al Fine is written in the chart but never reached by the walk
	// through the whole form — the music stops before it. Thirty-four charts in two thousand.
	//
	// Choosing such a section still has to play it. Asking for the coda means "play me the
	// coda", not "play me nothing", so the bars are taken as written, once through, in the
	// order they appear.
	beat = 0.f;
	for (size_t r = 0; r < ranges.size(); r++) {
		for (int b = ranges[r].first; b <= ranges[r].last && b < (int) bars.size(); b++) {
			PlayedBar played;
			played.bar = b;
			played.startBeat = beat;
			played.endBeat = beat + (float) bars[b].beats;
			beat = played.endBeat;
			out.timeline.push_back(played);
		}
	}
	out.totalBeats = beat;
	if (out.timeline.empty())
		return whole;
	return out;
}


std::vector<ChartSection> chartRangesForLabel(const std::vector<ChartBar>& bars, char label) {
	std::vector<ChartSection> out;
	for (const ChartSection& sec : chartSections(bars)) {
		if (sec.label == label)
			out.push_back(sec);
	}
	return out;
}


} // namespace px
