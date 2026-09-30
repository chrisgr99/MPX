#pragma once
/** A Guitar Pro song as it is played, rather than as it is written.

NO RACK IN HERE EITHER, for the same reason as GuitarPro.hpp: this builds into a command-line
program and is checked against real files before anything is patched.

WHAT THIS DOES. A file is written the way a player reads it — bars once each, with repeat marks,
first and second endings, and signs telling you to go back. What a sequencer needs is the bars in
the order they sound, each with a time in seconds. So the written bars are walked the way a player
walks them, the tempo map is applied, tied notes are joined into one, and every note comes out with
the moment it starts and the moment it stops.

ONE PASS, THEN IT IS FIXED. The timeline is built when a song is loaded and read from afterwards,
so nothing here runs while audio does.

See docs/guitar-pro.md.
*/
#include "GuitarPro.hpp"

namespace px {


/** One bar as it is played. The same written bar appears several times in a song with repeats,
each with its own `pass`, its own place and its own time. */
struct GpPlayedBar {
	int written = 0;          /**< Which bar of the score it is, counting from nought. */
	int pass = 1;             /**< Which time through, counting from one. */
	double startQuarters = 0.0;
	double quarters = 0.0;    /**< Its length, from the time signature. */
	double startSeconds = 0.0;
	double seconds = 0.0;
	float bpm = 120.f;        /**< In quarter notes a minute, at the bar's start. */
};


/** One note as it is played: where it starts, how long it sounds, and everything the file said
about how it is played. */
struct GpPlayedNote {
	int track = 0;
	int bar = 0;              /**< Which played bar it belongs to, into `GpTimeline::bars`. */
	double startQuarters = 0.0;
	double lengthQuarters = 0.0;
	double startSeconds = 0.0;
	double lengthSeconds = 0.0;
	float dynamic = 0.6f;     /**< The beat's level, nought to one. */
	int brush = 0;            /**< -1 up, 1 down, nought for a note that was not brushed. */
	int brushMs = 0;
	uint8_t grace = 0;        /**< From the beat: 1 before it, 2 taking time from it. */
	/** WHERE THIS NOTE CAME FROM on its own string, as a MIDI note, or -1 for the first note on
	that string. A hammer-on and a pull-off are the same mark in the file — which of them it is
	depends on whether the hand went up or down, and only the note before it knows. A slide needs
	the same thing, to know where the pitch starts. */
	int fromMidi = -1;
	/** How many notes the stroke this note belongs to has, and which of them this is, counted
	from the first string the hand reaches. Nought and one for a note struck on its own. */
	uint8_t strumIndex = 0;
	uint8_t strumCount = 0;
	GpNote note;              /**< Pitch, string, fret and the articulations, as read. */
};


/** THE TEMPO AS A LIST OF SEGMENTS over the played timeline. Each begins where the one before
it ends, so turning a position into a time is a search rather than a walk, and a change in the
middle of a bar is no harder than one at its start. */
struct GpSegment {
	double quarters = 0.0;
	double seconds = 0.0;
	double secondsPerQuarter = 0.5;
};


struct GpTimeline {
	std::vector<GpPlayedBar> bars;
	std::vector<GpSegment> clock;
	/** One list per track of the song, in the song's own track order, each in time order. */
	std::vector<std::vector<GpPlayedNote> > tracks;
	double quarters = 0.0;    /**< The whole length. */
	double seconds = 0.0;
	/** WHAT DID NOT SURVIVE, counted rather than announced — a jump whose sign is missing, or a
	tie continuing a note that is not there. */
	int skipped = 0;
};


/** Builds the played order of a song. Returns false only when there is nothing to play. */
bool gpBuildTimeline(const GpSong& song, GpTimeline& out, std::string* why);

/** The second a position falls on, and how long a quarter note lasts there. Both are a search
over the tempo segments and neither allocates, so either may be called from the audio thread. */
double gpSecondsAt(const GpTimeline& line, double quarters);
double gpSecondsPerQuarter(const GpTimeline& line, double quarters);


} // namespace px
