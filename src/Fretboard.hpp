#pragma once
/** WHAT A FRETTING HAND CAN DO, and the search for ways of doing it. See docs/finger-picker.md.

NO RACK IN HERE, as with the performer and the string models: it builds into a command-line test
that checks the shapes it finds against the ones every guitarist knows.

SHARED BY EVERY MODULE THAT FRETS. The finger picker, and the strumming and lead modules after it,
each want something different of the left hand — a full shape across six strings, only the strings
a pattern plays, single notes moving along the neck — but what a hand CAN do is the same for all
of them, and easy to get subtly wrong. So it is here once: the tuning, the hand's limits, and an
exhaustive search through every way of fretting the strings within them. Each module brings its
own requirements and its own idea of what is good; this decides only what is possible.

STRINGS ARE NUMBERED FROM THE TOP, as the performer and the cable number them: string nought is
the highest, the first string, and the last is the lowest.
*/
#include <cstdint>
#include <functional>
#include <string>

namespace px {


struct Tuning {
	static const int MAX_STRINGS = 8;
	int strings = 6;
	/** The MIDI note of each open string, highest first. */
	int open[MAX_STRINGS] = {64, 59, 55, 50, 45, 40};

	static Tuning standard() { return Tuning(); }
};


/** A way of holding the strings: for each, muted, open, or stopped at a fret. */
struct Shape {
	static const int MUTED = -1;
	int fret[Tuning::MAX_STRINGS];

	Shape() {
		for (int s = 0; s < Tuning::MAX_STRINGS; s++)
			fret[s] = MUTED;
	}

	bool sounds(int s) const { return fret[s] != MUTED; }
	/** The MIDI note a string sounds, or -1. */
	int note(const Tuning& t, int s) const { return sounds(s) ? t.open[s] + fret[s] : -1; }
	/** The lowest sounding string, highest-numbered, or -1 if none sounds. */
	int bassString(const Tuning& t) const;
	/** As a guitarist writes a shape, lowest string first: "x32010". */
	std::string written(const Tuning& t) const;
	static Shape parse(const std::string& text, const Tuning& t);
};


/** THE HAND'S LIMITS. A stretch of four frets for the stopped notes, four fingers, and a barre —
one finger flat across several strings at the lowest fret — counting as one. Open strings cost
nothing and may sound anywhere. */
struct Hand {
	/** The most frets the stopped notes may span, counting both ends: four is frets 1 to 4. */
	int stretch = 4;
	int fingers = 4;
	/** The highest fret the search goes to. */
	int highestFret = 15;
	/** WHETHER A BARRE MAY LIE ACROSS A MUTED STRING. False for a strum, where a muted string
	must stay silent and a finger lying on it would sound it. True where muted means only that the
	string is not played, as in finger picking: a string sounds only when it is picked, so a barre
	across one that is not costs nothing. */
	bool barreOverMuted = false;
};


/** WHAT HOLDING A SHAPE TAKES, worked out once and given to the scoring. */
struct Grip {
	bool playable = false;
	/** Fingers needed, a barre counted once. */
	int fingers = 0;
	/** The lowest and highest stopped frets, nought and nought with nothing stopped. */
	int lowFret = 0, highFret = 0;
	bool barre = false;
	/** The lowest fret stopped on strings apart, with no barre able to take them, and a higher
	fret stopped too: the index finger lies at the lowest fret, and a second finger there has to
	crowd back beside it. */
	bool splitIndex = false;
	int sounding = 0;
	int open = 0;
};

Grip gripOf(const Shape& shape, const Tuning& t, const Hand& hand);


/** EVERY PLAYABLE SHAPE in which each string is one of the choices `allowed` gives it, offered to
`visit` with its grip. `allowed(string, fret)` is asked for every fret from nought up and for the
muted string, which is fret -1. The search is exhaustive, window by window up the neck; a shape
with nothing stopped, all open or muted, is offered once. */
void searchShapes(const Tuning& t, const Hand& hand,
	const std::function<bool(int string, int fret)>& allowed,
	const std::function<void(const Shape&, const Grip&)>& visit);


/** A CHORD AS THE FRETTING HAND NEEDS IT: pitch classes, which of them are essential, and the
bass. Built from the chord library's ranked tones by the modules; plain here so the library needs
nothing else. */
struct ChordNotes {
	static const int MAX = 8;
	int count = 0;
	int pc[MAX] = {};
	bool essential[MAX] = {};
	/** Which degree each tone is — 1, 3, 5, 7, or 6, 9, 11 or 13 for a colour — or nought where
	the chord was built without saying. */
	int8_t degree[MAX] = {};
	int bass = 0;

	bool contains(int pitchClass) const;
	bool isEssential(int pitchClass) const;
};


/** THE BEST FULL SHAPE for a chord: every sounding string a chord tone, no muted string between
sounding ones, the lowest sounding string the bass, every essential tone present, at least four
strings sounding — what a strum needs, and what the familiar chord shapes are. Among those, open
strings, more strings, fewer fingers and a low position are preferred, and `nearFret` adds a
preference for that part of the neck when it is above nought. Returns false if none is playable. */
bool bestFullShape(const ChordNotes& chord, const Tuning& t, const Hand& hand, Shape* out,
	int nearFret = 0);


} // namespace px
