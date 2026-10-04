#pragma once
/** MUSICXML READ INTO THE SONG MODEL.

The other direction from GpMusicXML.hpp, and the reason the model is the same one: everything
downstream of a song already works. The timeline unfolds its repeats, endings and jumps; the chart
module plays it; the performer shapes it; the note bus carries it; mpxPiano and the rest sound it.
A reader that produces a GpSong acquires all of that without asking for any of it.

NOT GUITAR MUSIC. This is for a fugue or a string quartet — music with no fingerboard. A note
whose string is nought is exactly that case, and the bus and the performer have always said so:
"the source has no fretboard, and then a renderer gives the note a voice of its own". So no
fingering is worked out and none is wanted.

ONE PART IS ONE TRACK, which is one row on the module and one cable. A quartet is four parts and
four cables. A keyboard fugue is ONE part of two staves carrying three or four voices, which go
down one cable together and overlap as a fugue must — the model holds voices laid on one timeline,
which is what Guitar Pro does with its own two and what the chart already plays.

WHAT IS READ AND WHAT IS NOT. Notes, rests, chords, ties, voices, the divisions they are counted
in, key and time and their changes, repeats, first and second time bars, the jump marks, tempo
marks that carry a number, dynamic marks, and staccato and accent. Not yet: hairpins, written
tempo changes that carry no number, ornaments, and anything to do with a fingerboard.
*/
#include "GuitarPro.hpp"

#include <string>
#include <vector>

namespace px {


/** Reads a MusicXML document into the song model. Returns false and fills `why` when the document
is not MusicXML this can read at all; a document that is readable but imperfect returns true with
`skipped` counting what was passed over. */
bool mxRead(const std::string& xml, GpSong& out, std::string* why);

/** The same from a file, compressed or not: a .mxl is a zip holding the document. */
bool mxReadFile(const std::string& path, GpSong& out, std::string* why);

/** The same from bytes already in hand, which is what a module has: a patch carries the file it
loaded, so the bytes are what comes back when the patch is opened. Compressed or not. */
bool mxReadBytes(const std::vector<uint8_t>& bytes, GpSong& out, std::string* why);

/** Whether this looks like MusicXML rather than a Guitar Pro file, so one chooser can offer both
and the right reader be used without the name being trusted. */
bool mxLooksLikeMusicXML(const std::string& head);


} // namespace px
