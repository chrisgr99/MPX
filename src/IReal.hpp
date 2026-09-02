#pragma once
/** iReal Pro charts: reading a playlist export, and turning one song into timed chord spans.

NO RACK IN HERE. Nothing in this file knows what a module or a cable is, so it can be built into
a small command-line program and run against the two thousand charts in a real export — which is
how it gets to be right, rather than by patching cables and listening for something wrong.

The pipeline, which follows the public de-obfuscation and the ireal-reader reference parser:

  1. find the irealb:// link in the HTML and decode it
  2. split the playlist on "===" — the last piece is the playlist's name
  3. split each song on runs of "=" into title, composer, style, key, body
  4. drop the body's fixed prefix and unscramble it in fifty-character blocks
  5. tokenise into cells, KEEPING repeats, endings and sections rather than flattening
  6. expand those cells into spans with a start and end beat, on demand

Step five is the one worth stating twice: the folded form is what the chart IS, and the expanded
form is a view of it. Flattening at parse time would throw away the structure a chart viewer and
a phrase finder both need.
*/
#include "Chord.hpp"

#include <string>
#include <vector>

namespace px {


/** One entry of a chart, in the order it is written. Structure is preserved, not unwrapped. */
struct Cell {
	enum Kind {
		CHORD,
		BAR,            /**< A barline, closing the bar before it. */
		SECTION,        /**< A rehearsal letter opening a section. */
		REPEAT_OPEN,
		REPEAT_CLOSE,
		ENDING,         /**< A first- or second-time bar. */
		END,            /**< The final barline. */
		TIME,           /**< A time signature, which may change mid-chart. */
		REPEAT_BAR,     /**< Play the previous bar again. */
		REPEAT_TWO,     /**< Play the previous two bars again. */
		REPEAT_HELD,    /**< Hold what is sounding through this bar. */
		EMPTY,          /**< A blank cell, which still takes its share of the bar. */
		DIVIDER,        /**< A beat divider within a bar. */
		SEGNO,
		CODA,
	};
	Kind kind = CHORD;
	/** CHORD. Invalid for a No Chord cell, which still occupies its time. */
	Chord chord;
	bool noChord = false;
	/** SECTION. */
	char label = 0;
	/** ENDING. */
	int ending = 0;
	/** TIME. */
	int beats = 4, unit = 4;
	/** CHORD, kept for diagnostics: the symbol as it was written. */
	std::string raw;
};


struct Song {
	std::string title;
	std::string composer;
	std::string style;
	Key key;
	int beats = 4, unit = 4;
	std::vector<Cell> cells;
	/** False when the chart uses something this reader cannot honour. A chart that cannot be
	played correctly says so rather than being played wrongly. */
	bool supported = true;
	std::string why;
};


/** A chord sounding over a stretch of time, in beats from the start of the expanded chart. */
struct Span {
	Chord chord;
	bool noChord = false;
	float startBeat = 0.f;
	float endBeat = 0.f;
	/** The section this bar opens, or nought. */
	char section = 0;
};


struct Expansion {
	std::vector<Span> spans;
	float totalBeats = 0.f;
	int bars = 0;
};


/** Finds the irealb link in an HTML export and decodes it. Empty if there is none. */
std::string irealPayloadFromHtml(const std::string& html);

/** Undoes the fifty-character block scramble. */
std::string irealUnscramble(const std::string& body);

/** Splits a decoded payload into songs. The playlist's name is written to `name` when given. */
std::vector<Song> irealParsePlaylist(const std::string& payload, std::string* name = NULL);

/** Parses one song chunk — the part between two "===" separators. */
Song irealParseSong(const std::string& chunk);

/** Parses an iReal chord symbol against a key. False if it is not a chord at all. */
bool irealParseChord(const std::string& symbol, const Key& key, Chord& out);

/** Reads iReal's key field: "C", "A-" for A minor. */
Key irealParseKey(const std::string& field);

/** Writes the repeats, endings and similes out flat, and gives every chord a start and an end
in beats. This is the view a player uses; the cells remain the chart. */
Expansion irealExpand(const Song& song);


} // namespace px
