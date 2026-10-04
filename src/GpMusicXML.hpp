#pragma once
/** ONE TRACK OF A GUITAR PRO SONG, WRITTEN AS MUSICXML.

Verovio engraves MusicXML and nothing else we have. The song model holds what the file said — see
GuitarPro.hpp — and this turns one track of it into a document Verovio can set.

ONE TRACK, NOT THE SONG. A score of seventeen staves puts three bars on a page, so the window
shows one instrument at a time and this writes one instrument at a time. It also means the cut
that Score::onePart has to make for a file from somewhere else is never needed for our own.

WHAT SOUNDS AND WHAT IS WRITTEN ARE NOT THE SAME THING, and this is where they part company. The
rest of the plugin cares what a note sounds like: a pitch, a length in quarter notes, a string and
a fret. An engraver cares what it is WRITTEN as — a dotted eighth inside a triplet, tied over a
barline, with a slur to the note after it. A length of a third of a quarter note does not say
whether it is a triplet eighth or something stranger, so the written form has to be recovered from
the sound, which is what noteFigure below does.

See docs/notation.md.
*/
#include "GuitarPro.hpp"

#include <string>

namespace px {


/** WHICH STAVES A PART IS WRITTEN ON.

Guitar music is read two ways. Notation says what the music IS — the pitches, the rhythm, the
phrasing — and tablature says where to put your fingers, which is a different question and often
the more useful one. Printed guitar books carry both, one above the other, and so does every
website that publishes them.

BOTH IS TWO STAVES OF THE SAME MUSIC, not two different things: the notes are written out twice,
once on each staff, which is what MusicXML means by a part with two staves. */
enum GpStaves {
	GP_NOTATION,   /**< One staff, pitches. */
	GP_TAB,        /**< One staff, string and fret. */
	GP_BOTH,       /**< Notation above, tablature below, the same music on each. */
};


/** The song's one track as a MusicXML document. Empty when the track does not exist.

`trouble`, when given, collects what could not be written exactly — a length that is not any
ordinary note, a tuplet nobody has a name for. The document is still returned: a bar that is a
little wrong is worth more than no score at all, and these are the things worth knowing about
rather than things worth refusing over. */
std::string gpToMusicXML(const GpSong& song, int trackIndex, GpStaves staves = GP_NOTATION,
	std::string* trouble = NULL);


/** HOW A LENGTH IS WRITTEN: the note it is drawn as, how many dots it carries, and what tuplet it
sits in. Public because the test program checks it directly — every length in every file we have,
against what it came back as.

`type` is MusicXML's own name: "whole", "half", "quarter", "eighth", "16th" and so on down.
`actual` over `normal` is the tuplet, 3 over 2 for a triplet, and 1 over 1 for no tuplet. */
struct GpFigure {
	std::string type;
	int dots = 0;
	int actual = 1, normal = 1;
	bool exact = false;   /**< False when nothing in the table matched and the nearest was taken. */
};

GpFigure gpNoteFigure(float quarters);


} // namespace px
