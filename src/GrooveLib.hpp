#pragma once
/** THE GROOVE LIBRARY: rhythms as files rather than as code.

A groove is a set of strokes over one or more bars — where each falls, how hard, and which drum
plays it. Written as data, a groove is a file: the library grows without a build, a bad one is
fixed by editing a line, and anybody can add their own without touching the plugin.

WHERE THEY COME FROM. The plugin's own `grooves` folder ships with it. A folder of the same name
in the Rack user folder is read afterwards, so a file of yours with the same name as one of ours
replaces it, and a new name adds to the library.

THE FORMAT. One file holds an array of grooves:

  [{ "name": "Medium swing", "genre": "Jazz", "bars": 2, "beats": 4, "swing": 0.62,
     "hits": [{"bar": 0, "beat": 0, "part": "ride", "weight": 1.0}, ...],
     "fill": [{"beat": 3, "part": "snare", "weight": 0.8}, ...] }]

`beat` counts from nought at the start of the bar and may be fractional. `weight` is nought to
one, and is what Density thins from the light end. `swing` is what the groove wants rather than
what it gets: the knob overrides it. `fill` replaces the last bar of a phrase when the phrase-end
setting asks for one.
*/
#include "plugin.hpp"

#include <string>
#include <vector>

namespace px {


/** The drums a groove may name, and the General MIDI note each is sent as. */
enum GroovePart {
	GP_KICK, GP_SNARE, GP_STICK, GP_CLAP, GP_HAT, GP_OPEN, GP_TOM_LO, GP_TOM_HI, GP_RIDE,
	GP_CRASH, GP_PARTS
};

/** In volts, middle C at nought — kick 36, snare 38, closed hat 42, ride 51. */
extern const float GROOVE_PITCH[GP_PARTS];

/** The word each part is written as in a groove file. */
const char* groovePartName(int part);
/** The part a word means, or -1. */
int groovePartFor(const std::string& word);


struct GrooveHit {
	int bar = 0;
	float beat = 0.f;
	int part = GP_KICK;
	float weight = 1.f;
};


struct Groove {
	std::string name;
	std::string genre;
	int bars = 1;
	int beats = 4;
	/** What the groove asks for, which the panel's knob overrides. Below nought means it has no
	opinion. */
	float swing = -1.f;
	std::vector<GrooveHit> hits;
	std::vector<GrooveHit> fill;

	std::string label() const { return genre.empty() ? name : (genre + ": " + name); }
};


/** EVERY GROOVE THAT WAS FOUND, in the order genre then name, loaded once when first asked for.
Never empty: a plain four-four beat is built in, so a missing folder is a thin library rather than
a silent module. */
const std::vector<Groove>& grooveLibrary();

/** How many there are, which is what the panel's control is ranged over. */
int grooveCount();
/** One of them, clamped, so a patch saved with a library that has since shrunk still plays. */
const Groove& grooveAt(int index);


} // namespace px
