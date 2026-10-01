#pragma once
/** Guitar Pro files: reading one into a song this plugin can play.

NO RACK IN HERE, and no dependencies either. Nothing in this file knows what a module or a cable
is, so it builds into a command-line program and runs against a folder of real files — which is
how it gets to be right, rather than by dropping files on a panel and listening for something
wrong. The same arrangement as IReal.hpp, for the same reason.

FOUR FORMATS, ONE SONG. What comes out is the same whichever went in:

    .gp          Guitar Pro 7 and 8: a zip holding Content/score.gpif, which is XML
    .gpx         Guitar Pro 6: a BCFZ container holding the same kind of XML
    .gp5 .gp4    a sequential binary format, differing in the middle between versions
    .gp3

EVERY COUNT IN THE FILE IS A NUMBER SOMEBODY ELSE WROTE. Each one is checked against what is left
before it is used and every string length is bounded, so a truncated or unusual file ends the
read with a reason rather than taking the host down with it.

WHAT IS NOT READ IS NOT ANNOUNCED. A song that plays is useful even where a bend or an unusual
tuplet did not survive. What was skipped is counted in `skipped` for whoever is debugging, and
nothing is put in front of the listener.

See docs/guitar-pro.md.
*/
#include <cstdint>
#include <string>
#include <vector>

namespace px {


/** One note of a beat. Pitch is MIDI; the string and fret are kept because a variation may want
them — the same pitch played on two strings is two different sounds on a guitar. */
struct GpNote {
	int midi = 60;
	int string = 0;          /**< 1 is the highest string; nought when the file did not say. */
	int fret = -1;
	bool tied = false;       /**< Continues the note before it rather than striking again. */
	bool dead = false;       /**< A muted click with no pitch. */
	bool ghost = false;
	bool accent = false;
	bool palmMute = false;
	bool letRing = false;
	bool harmonic = false;
	bool artificialHarmonic = false;
	bool slide = false;      /**< Slides into or out of another note, as slideFlags says. */
	/** WHICH SLIDE, as the file's own bits: 1 shifts to the next note, 2 slides to it without
	striking it, 4 slides out downwards, 8 out upwards, 16 in from below, 32 in from above, and
	the top two bits are pick slides. */
	uint8_t slideFlags = 0;
	bool hammer = false;     /**< Sounded by the left hand from the note before it. Which of a
	                         hammer-on and a pull-off it is depends on the direction, which the
	                         file does not say and the timeline works out. */
	bool tapped = false;
	bool staccato = false;
	bool heavyAccent = false;
	bool tremolo = false;    /**< Picked repeatedly for the note's length. */
	/** A GRACE NOTE LEADING INTO THIS ONE: nought for none, 1 played before the beat, 2 taking
	time from it. `graceFret` is where it is fingered, on this note's own string. */
	uint8_t grace = 0;
	int graceFret = -1;
	bool vibrato = false;
	/** A BEND, as points through the note's length: position from nought to one, height in
	CENTS. The two families of file count both of those in units of their own, and neither is
	cents; they are converted on the way in, so everything downstream reads one unit. Empty for
	a note that is not bent. */
	std::vector<std::pair<float, float> > bend;
};


/** One beat: a rhythm and the notes struck on it. A rest has no notes. */
struct GpBeat {
	/** The beat's length in quarter notes, dots and tuplets already worked in. */
	float quarters = 1.f;
	/** Where it starts in the bar, in quarter notes from the bar's first beat. */
	float start = 0.f;
	/** Nought to one, from the file's own marks: ppp through fff. */
	float dynamic = 0.6f;
	bool rest = false;
	/** Struck as a chord, and which way the hand went: -1 up, 1 down, 0 not brushed. */
	int brush = 0;
	/** How long the hand took over the brush, in milliseconds, or nought where the file does not
	say and the player decides. */
	int brushMs = 0;
	/** GRACE NOTES: nought for an ordinary beat, 1 for one played before the beat, 2 for one
	taking time from it. The beat's own notes are the grace notes. */
	uint8_t grace = 0;
	/** WHAT THE RIGHT HAND DID, where it is not a plain pick: tapping, slapping the string with
	the thumb, or popping it with a finger. A property of the beat and not of one note, because
	the hand does one thing at a time. */
	bool tapped = false;
	bool slapped = false;
	bool popped = false;
	/** THE WHAMMY BAR over this beat, as a bend is: points of (position nought to one, cents).
	It moves every string of the beat at once, which is what a bar does. */
	std::vector<std::pair<float, float> > whammy;
	std::vector<GpNote> notes;
};


/** One bar of one track. */
struct GpBar {
	std::vector<GpBeat> beats;
};


/** One track: an instrument, and its bars in the order the file writes them. */
struct GpTrack {
	std::string name;         /**< "Jazz Guitar", "Bass", "Drums" — what goes on the panel. */
	std::string shortName;
	/** The General MIDI instrument the file asks for, nought to 127. */
	int midiProgram = 0;
	int midiChannel = 0;
	/** WHERE THE FILE PLACES IT, from -1 at the left to 1 at the right. */
	float pan = 0.f;
	bool percussion = false;
	/** THE WHOLE PART LETS RING, which some transcriptions say once for the track rather than on
	every note. Three of the nineteen tracks of the Oasis file do. */
	bool letRingThroughout = false;
	int capo = 0;
	std::vector<int> tuning;  /**< MIDI note of each open string, highest first. */
	std::vector<GpBar> bars;
};


/** What a bar says about time, key and structure. One of these per bar of the song, shared by
every track: Guitar Pro keeps them once rather than per track, and so does this. */
struct GpMasterBar {
	int beatsPerBar = 4;
	int beatUnit = 4;         /**< The lower number of the time signature. */
	int keyAccidentals = 0;   /**< Sharps positive, flats negative. */
	bool minor = false;
	bool repeatOpen = false;
	/** HOW MANY TIMES THE SECTION CLOSING HERE IS PLAYED, or nought where no repeat closes on
	this bar. Both readers give times played, not times gone back: a plain repeat is two. */
	int repeatCount = 0;
	std::vector<int> endings; /**< Which passes this bar is played on; empty for all of them. */
	std::string section;      /**< "Intro", "Chorus" — empty where the bar opens no section. */
	std::string sectionLetter;
	bool doubleBar = false;
	/** NAVIGATION MARKS ON THIS BAR, as the file names them: a place to come back to — "Coda",
	"DoubleCoda", "Segno", "SegnoSegno", "Fine" — or an instruction to jump — "DaCapo",
	"DaSegnoAlCoda", "DaCoda" and the rest of them. Both readers give the same names. */
	std::vector<std::string> marks;
};


/** A TEMPO IN FORCE FROM A POINT IN THE SONG. The first is the song's own tempo at its first bar,
so the list is never empty in a song that read. */
struct GpTempo {
	int bar = 0;
	float position = 0.f;     /**< Quarter notes into the bar. */
	float bpm = 120.f;
	/** THE NOTE THE COUNT IS OF, as the file's own code: 1 an eighth, 2 a quarter, 3 a dotted
	quarter, 4 a half, 5 a dotted half. Two is what almost every file writes, and it is the
	ordinary reading of a tempo — so many beats a minute, a beat being a quarter note. */
	int unit = 2;
};


/** A chord symbol the file carries, and where it applies. Many transcriptions have none, and
then the harmony is worked out from the notes instead — see docs/guitar-pro.md. */
struct GpChordMark {
	int bar = 0;
	float start = 0.f;        /**< Quarter notes from the bar's first beat. */
	std::string name;         /**< As written: "Fm7", "Bb", "C#dim". */
};


struct GpSong {
	std::string title, artist, album, words, music, copyright, tabber;
	float tempo = 120.f;      /**< The number written on the score, in its own unit. */
	std::vector<GpTempo> tempos;
	std::vector<GpMasterBar> masterBars;
	std::vector<GpTrack> tracks;
	std::vector<GpChordMark> chords;
	std::vector<std::string> lyrics;

	/** WHAT WAS NOT READ, counted rather than announced. For a log line, never for the panel. */
	int skipped = 0;
};


/** Reads a file of any of the four kinds, chosen by what is in it rather than by its name — a
file downloaded from the internet is as likely as not to carry the wrong extension.

Returns false and fills `why` when the file cannot be read at all. A file that is readable but
imperfect returns true with `skipped` set. */
bool gpRead(const std::string& path, GpSong& out, std::string* why);

/** The same, for a file already in memory. */
bool gpReadBytes(const std::vector<uint8_t>& bytes, GpSong& out, std::string* why);

/** Raw DEFLATE, as a zip holds it. Public because the test program checks it on its own: a fault
here looks exactly like a fault in the XML that follows it. Returns false on a malformed stream
rather than reading past the end of anything. */
bool gpInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);


} // namespace px
