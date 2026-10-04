/** See GpMusicXML.hpp. */
#include "GpMusicXML.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace px {

namespace {


/** HOW FINELY A QUARTER NOTE IS DIVIDED in what is written out.

Nine hundred and sixty, because it has to divide by every tuplet anybody writes as well as by
every ordinary note: three for a triplet, five, seven and nine for the rest, and sixteen for a
sixty-fourth. A tuplet whose length does not come out whole is a rounding error that accumulates
across a bar until the bar does not add up, and a bar that does not add up is one Verovio draws
with a gap in it. */
const int DIVISIONS = 960;


/** THE NOTES, LONGEST FIRST, with the names MusicXML uses for them. */
struct Written {
	const char* type;
	float quarters;
};
const Written WRITTEN[] = {
	{"breve", 8.f}, {"whole", 4.f}, {"half", 2.f}, {"quarter", 1.f}, {"eighth", 0.5f},
	{"16th", 0.25f}, {"32nd", 0.125f}, {"64th", 0.0625f}, {"128th", 0.03125f},
};
const int WRITTEN_COUNT = (int) (sizeof(WRITTEN) / sizeof(WRITTEN[0]));

/** THE TUPLETS WORTH LOOKING FOR, as actual over normal.

Three in the time of two is a triplet and is most of them. The rest are here because transcribers
use them and because leaving one out does not produce nothing — it produces a note of the wrong
length, which is worse. Ordered so that the simplest explanation of a length wins. */
struct Tuplet {
	int actual, normal;
};
const Tuplet TUPLETS[] = {
	{1, 1}, {3, 2}, {5, 4}, {6, 4}, {7, 4}, {7, 8}, {9, 8}, {11, 8}, {13, 8}, {5, 2}, {2, 3}, {4, 3},
};
const int TUPLET_COUNT = (int) (sizeof(TUPLETS) / sizeof(TUPLETS[0]));


/** THE PITCH SPELLINGS, sharp and flat.

WHICH ONE IS NOT A DETAIL. A guitar part in E flat written in sharps is a part nobody can read:
every accidental fights the key signature. The key tells us which family to spell in, and that is
as far as this goes — a proper engraver asks what the chord is doing, and we have no chord. */
const char* const SHARP_STEP[12] = {"C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
const int SHARP_ALTER[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
const char* const FLAT_STEP[12] = {"C", "D", "D", "E", "E", "F", "G", "G", "A", "A", "B", "B"};
const int FLAT_ALTER[12] = {0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0};


/** XML's five, and nothing else needs escaping in a document written as UTF-8. */
std::string esc(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		switch (in[i]) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default: out += in[i];
		}
	}
	return out;
}


/** EVERY VOICE OF A BAR, TAKEN APART AGAIN.

The reader lays Guitar Pro's voices onto one timeline because what it is for is what SOUNDS, and
two voices played by one instrument sound as one line — see GuitarPro.cpp. An engraver needs them
back: two voices written as one are a bar with twice as many beats in it as it has room for.

They come apart because each voice was laid down in order from the start of the bar, and within
one voice every beat begins strictly AFTER the one before it — a beat has a length, so the next
one cannot begin at the same moment. A beat that does not begin later than the last is therefore
the first beat of the next voice. Not merely one that begins EARLIER: a second voice that starts
at the top of the bar, as a second voice usually does, begins at exactly the same moment as the
first one did, and testing for earlier misses it entirely.

A grace note is not counted. It hangs off the beat after it and carries the same position, so it
would otherwise look like the start of a voice of its own. */
std::vector<std::vector<const GpBeat*> > splitVoices(const GpBar& bar) {
	std::vector<std::vector<const GpBeat*> > voices;
	float last = -1.f;
	bool any = false;
	for (size_t i = 0; i < bar.beats.size(); i++) {
		const GpBeat& beat = bar.beats[i];
		const bool grace = (beat.grace != 0);
		if (voices.empty() || (!grace && any && beat.start <= last + 1e-4f))
			voices.push_back(std::vector<const GpBeat*>());
		voices.back().push_back(&beat);
		if (!grace) {
			last = beat.start;
			any = true;
		}
	}
	return voices;
}


/** Whether this track is written on a bass clef, a treble clef an octave down, or drums. */
int lowestString(const GpTrack& track) {
	int low = 127;
	for (size_t i = 0; i < track.tuning.size(); i++)
		low = std::min(low, track.tuning[i]);
	return (track.tuning.empty()) ? 55 : low;
}


} // namespace


/** WHAT A LENGTH IS WRITTEN AS.

The model carries a length in quarter notes with the dots and the tuplet already worked into it,
because that is what sounds. Writing it down needs them back out: two thirds of a quarter note is
a triplet quarter, and a half plus a quarter is a dotted half, and nothing in the number says
which. So every ordinary note is tried against every dot count and every tuplet anybody writes,
and the simplest that matches is the answer — which is also how a transcriber would read it.

A length that matches nothing is given the nearest note and marked. That happens with a bar whose
contents do not add up in the file either, and the alternative is a note of no length at all. */
GpFigure gpNoteFigure(float quarters) {
	GpFigure best;
	best.type = "quarter";
	best.exact = false;
	float bestError = 1e9f;

	if (quarters <= 0.f)
		return best;

	for (int t = 0; t < TUPLET_COUNT; t++) {
		for (int w = 0; w < WRITTEN_COUNT; w++) {
			float dotted = WRITTEN[w].quarters;
			for (int dots = 0; dots <= 3; dots++) {
				if (dots > 0)
					dotted += WRITTEN[w].quarters / (float) (1 << dots);
				// A tuplet of `actual` in the time of `normal` makes each note shorter by that
				// ratio, which is the same way round the file counts it.
				const float length =
					dotted * (float) TUPLETS[t].normal / (float) TUPLETS[t].actual;
				const float error = std::fabs(length - quarters);
				if (error < bestError - 1e-6f) {
					bestError = error;
					best.type = WRITTEN[w].type;
					best.dots = dots;
					best.actual = TUPLETS[t].actual;
					best.normal = TUPLETS[t].normal;
					best.exact = (error < 1e-4f);
				}
				if (best.exact && bestError < 1e-6f)
					return best;
			}
		}
	}
	return best;
}


namespace {


/** One note as it will be written, with what the engraver needs that the model does not say. */
struct Figured {
	const GpBeat* beat = NULL;
	GpFigure figure;
	int duration = 0;        /**< In divisions. */
	bool tupletStart = false, tupletStop = false;
	/** WHAT JOINS EACH NOTE TO ITS NEIGHBOUR. See markTechniques. */
	struct Joined {
		bool slideStart = false, slideStop = false;
		bool hammerStart = false, hammerStop = false;
		bool pullStart = false, pullStop = false;
	};
	std::vector<Joined> joined;
	/** What each beam level does on this note: "begin", "continue", "end", "forward hook",
	"backward hook", or nothing where this note has no beam at that level. See markBeams. */
	std::vector<std::string> beams;
	/** Which of this beat's notes a tie LEAVES, and which it ARRIVES at. Both ends are worked
	out together, because a tie with only one end is not a tie. See markTies. */
	std::vector<bool> tieStart;
	std::vector<bool> tieStop;
};


/** WHICH NOTES BEGIN A TIE.

The model marks the note that CONTINUES one — that is what a Guitar Pro file says, and it is all
the player needs, since a continued note is simply not struck again. Written music marks both
ends: a tie is a curve from one note head to another, and a curve needs both.

So each note is asked whether the beat after it continues it. On the same string where both say
which string they are on, because the same pitch on two strings is two notes on a guitar and
tying between them would draw a curve across the staff; on the same pitch otherwise. */
void markTies(std::vector<Figured>& notes, const GpBeat* intoNextBar,
		std::vector<int>* stopsInNextBar) {
	for (size_t i = 0; i < notes.size(); i++)
		notes[i].tieStop.assign(notes[i].beat->notes.size(), false);

	for (size_t i = 0; i < notes.size(); i++) {
		const GpBeat& here = *notes[i].beat;
		notes[i].tieStart.assign(here.notes.size(), false);
		// THE LAST NOTE OF A BAR IS ANSWERED BY THE FIRST NOTE OF THE NEXT ONE.
		//
		// A tie across a barline is the commonest tie there is — it is how a note longer than
		// the bar is written at all — and looking only within the bar found the END of one and
		// never the beginning. What came out was a tie arriving from nowhere, which Verovio
		// reports as a tie left open and draws as a curve hanging off the barline.
		const GpBeat* nextBeat =
			(i + 1 < notes.size()) ? notes[i + 1].beat : intoNextBar;
		if (!nextBeat)
			continue;
		const GpBeat& next = *nextBeat;
		for (size_t a = 0; a < here.notes.size(); a++) {
			for (size_t b = 0; b < next.notes.size(); b++) {
				if (!next.notes[b].tied)
					continue;
				// THE SAME PITCH, ALWAYS. A tie means one sound held on, so the two notes it
				// joins are the same note — and an engraver pairs them by pitch. Matching on the
				// string alone pairs a fret with a different fret on that string, which is not a
				// tie at all; the engraver then cannot pair what it was given and leaves the
				// curve hanging.
				//
				// The string is still required where both notes name one, because the same pitch
				// on two strings is two different sounds on a guitar.
				const bool samePitch = next.notes[b].midi == here.notes[a].midi;
				const bool sameString = here.notes[a].string <= 0
					|| next.notes[b].string <= 0
					|| next.notes[b].string == here.notes[a].string;
				if (samePitch && sameString) {
					notes[i].tieStart[a] = true;
					// MARKED AT BOTH ENDS, HERE AND ONLY HERE.
					//
					// The model says a note CONTINUES one before it, and that is all it says —
					// it does not say which. So a note marked as a continuation whose other end
					// cannot be found is written as an ordinary note rather than as half a tie:
					// a curve that starts nowhere is worse than a note struck once too often,
					// and it is what the engraver reports as a tie left open.
					if (i + 1 < notes.size())
						notes[i + 1].tieStop[b] = true;
					else if (stopsInNextBar)
						stopsInNextBar->push_back((int) b);
					break;
				}
			}
		}
	}
}


/** THE TUPLETS MARKED, by walking the voice and bracketing runs of the same ratio.

A tuplet is not a property of one note: three triplet eighths are one tuplet, and MusicXML wants
the first and the last of them marked so the bracket and the figure can be drawn once. Any run of
notes sharing a ratio is taken as one, which is right for everything a transcription writes. */
void markTuplets(std::vector<Figured>& notes) {
	size_t i = 0;
	while (i < notes.size()) {
		if (notes[i].figure.actual == notes[i].figure.normal) {
			i++;
			continue;
		}
		size_t j = i;
		while (j + 1 < notes.size()
				&& notes[j + 1].figure.actual == notes[i].figure.actual
				&& notes[j + 1].figure.normal == notes[i].figure.normal) {
			j++;
		}
		notes[i].tupletStart = true;
		notes[j].tupletStop = true;
		i = j + 1;
	}
}


/** WHICH NOTE OF A BEAT ANSWERS WHICH, for a technique that joins two notes.

On the same string, because a guitar's technique belongs to a string: a slide is one finger moving
along one string, and a hammer-on is a finger landing on the string already sounding. Where a file
does not say which string, the same pitch is taken instead. Returns -1 when there is no answer. */
int matching(const GpBeat& other, const GpNote& note) {
	for (size_t i = 0; i < other.notes.size(); i++) {
		if (note.string > 0 && other.notes[i].string == note.string)
			return (int) i;
	}
	if (note.string == 0) {
		for (size_t i = 0; i < other.notes.size(); i++) {
			if (other.notes[i].midi == note.midi)
				return (int) i;
		}
	}
	return -1;
}


/** THE TECHNIQUES THAT JOIN TWO NOTES, GIVEN BOTH ENDS.

A SPANNER NEEDS SOMEWHERE TO LAND. MusicXML draws a slide or a hammer-on as a line from one note
to another, started on the first and stopped on the second — and one started and never stopped is
not merely untidy, it is a line the engraver cannot place, which it says so about and then draws
somewhere unhelpful.

The model marks one end only, because one end is all a player needs. A slide is marked on the note
it leaves FROM; a hammer-on is marked on the note the hand lands ON. So each is given its other
end here, and anything whose other end does not exist — a slide out of the last note of a part,
into nothing — is dropped rather than left hanging.

WHICH OF A HAMMER-ON AND A PULL-OFF it is, the file does not say: both are one finger doing the
work instead of the pick, and the difference is only which way the pitch went. Up is a hammer-on
and down is a pull-off, which is what the names mean. */
void markTechniques(std::vector<Figured>& notes) {
	for (size_t i = 0; i < notes.size(); i++)
		notes[i].joined.assign(notes[i].beat->notes.size(), Figured::Joined());

	for (size_t i = 0; i < notes.size(); i++) {
		const GpBeat& here = *notes[i].beat;
		for (size_t n = 0; n < here.notes.size(); n++) {
			const GpNote& note = here.notes[n];

			// A SLIDE THAT GOES SOMEWHERE. The low two bits are the slides that arrive at the
			// next note — one struck, one not. The others slide off into nothing, which is a
			// different mark and not a line between two notes.
			if (note.slide && (note.slideFlags & 0x03) != 0 && i + 1 < notes.size()) {
				const int to = matching(*notes[i + 1].beat, note);
				if (to >= 0) {
					notes[i].joined[n].slideStart = true;
					notes[i + 1].joined[(size_t) to].slideStop = true;
				}
			}

			// A HAMMER-ON OR A PULL-OFF, marked here on the note the hand lands on.
			if (note.hammer && i > 0) {
				const int from = matching(*notes[i - 1].beat, note);
				if (from >= 0) {
					const bool up = note.midi > notes[i - 1].beat->notes[(size_t) from].midi;
					if (up) {
						notes[i - 1].joined[(size_t) from].hammerStart = true;
						notes[i].joined[n].hammerStop = true;
					}
					else {
						notes[i - 1].joined[(size_t) from].pullStart = true;
						notes[i].joined[n].pullStop = true;
					}
				}
			}
		}
	}
}


/** HOW MANY BEAMS A NOTE CARRIES. An eighth has one, a sixteenth two, and so on down. */
int beamLevels(const std::string& type) {
	if (type == "eighth") return 1;
	if (type == "16th") return 2;
	if (type == "32nd") return 3;
	if (type == "64th") return 4;
	if (type == "128th") return 5;
	return 0;
}


/** HOW LONG ONE BEAT OF THE BAR IS, in quarter notes, which is what notes are beamed within.

Beaming shows the beat. Six eighths in 6/8 are beamed as two groups of three because that is how
6/8 is counted, and the same six in 3/4 are beamed as three groups of two for the same reason —
the notes are identical and the beams are what tell a reader which piece they are looking at.

A lower number of eight that divides by three is a compound time, counted in dotted quarters.
Everything else is counted in whatever its lower number names. */
float beatLength(int beatsPerBar, int beatUnit) {
	const float unit = 4.f / (float) std::max(1, beatUnit);
	if (beatUnit >= 8 && beatsPerBar % 3 == 0 && beatsPerBar > 3)
		return unit * 3.f;
	// A BAR COUNTED IN HALVES IS STILL READ IN QUARTERS. Cut time beamed in half-note groups
	// puts eight eighths under one beam, which nobody reads.
	if (unit > 1.f)
		return 1.f;
	return unit;
}


/** WHICH NOTES ARE JOINED BY BEAMS, AND HOW.

Verovio draws the beams the document asks for and no others — it does not work them out, and a
part with none is a page of separate flagged notes, which is markedly harder to read than the
same music beamed.

A run is broken by anything that cannot be beamed — a rest, a quarter note or longer — and by the
end of a beat, so that the beams show the count. Within a run, each level of beam is begun, carried
and ended on its own, because a sixteenth beside an eighth carries a second beam that the eighth
does not: that second beam ends in a HOOK on the sixteenth rather than running to its neighbour. */
void markBeams(std::vector<Figured>& notes, float beat) {
	std::vector<int> levels(notes.size(), 0);
	std::vector<bool> breaks(notes.size(), false);
	float at = 0.f;
	for (size_t i = 0; i < notes.size(); i++) {
		const GpBeat& b = *notes[i].beat;
		const bool rest = b.rest || b.notes.empty();
		levels[i] = (rest || b.grace) ? 0 : beamLevels(notes[i].figure.type);
		// A RUN ENDS WHERE A BEAT ENDS. Measured from the start of each note: a note that begins
		// in one beat and runs into the next does not join what follows it.
		if (i > 0 && beat > 0.f) {
			const float here = std::floor(at / beat + 1e-4f);
			const float was = std::floor((at - notes[i - 1].beat->quarters) / beat + 1e-4f);
			if (here != was)
				breaks[i] = true;
		}
		if (!b.grace)
			at += b.quarters;
	}

	size_t i = 0;
	while (i < notes.size()) {
		if (levels[i] == 0) {
			i++;
			continue;
		}
		size_t j = i;
		while (j + 1 < notes.size() && levels[j + 1] > 0 && !breaks[j + 1])
			j++;
		// A RUN OF ONE IS NOT A BEAM. One eighth on its own keeps its flag.
		if (j > i) {
			for (size_t k = i; k <= j; k++) {
				notes[k].beams.assign((size_t) levels[k], "");
				for (int L = 1; L <= levels[k]; L++) {
					const bool before = (k > i) && levels[k - 1] >= L;
					const bool after = (k < j) && levels[k + 1] >= L;
					std::string& said = notes[k].beams[(size_t) L - 1];
					if (before && after)
						said = "continue";
					else if (!before && after)
						said = "begin";
					else if (before && !after)
						said = "end";
					else
						said = (k < j) ? "forward hook" : "backward hook";
				}
			}
		}
		i = j + 1;
	}
}


/** The step, alter and octave of a MIDI pitch, spelled for the key in force. */
void spell(int midi, int accidentals, std::string& step, int& alter, int& octave) {
	midi = std::max(0, std::min(127, midi));
	const int pc = midi % 12;
	octave = midi / 12 - 1;
	if (accidentals < 0) {
		step = FLAT_STEP[pc];
		alter = FLAT_ALTER[pc];
	}
	else {
		step = SHARP_STEP[pc];
		alter = SHARP_ALTER[pc];
	}
}


} // namespace


std::string gpToMusicXML(const GpSong& song, int trackIndex, GpStaves wanted,
		std::string* trouble) {
	if (trackIndex < 0 || trackIndex >= (int) song.tracks.size())
		return "";
	const GpTrack& track = song.tracks[trackIndex];
	int inexact = 0;

	// TABLATURE NEEDS STRINGS TO PUT THE FINGERS ON. A track with no tuning — a drum kit, or a
	// transcription that never said — has nothing to write tab against, so it is written as
	// notation whatever was asked for.
	GpStaves staves = wanted;
	if (track.tuning.empty() || track.percussion)
		staves = GP_NOTATION;
	const int staffCount = (staves == GP_BOTH) ? 2 : 1;
	const bool tabStaff = (staves == GP_TAB || staves == GP_BOTH);

	std::ostringstream x;
	x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	  << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\""
	     " \"http://www.musicxml.org/dtds/partwise.dtd\">\n"
	  << "<score-partwise version=\"4.0\">\n";

	x << "  <work><work-title>" << esc(song.title) << "</work-title></work>\n";
	x << "  <identification>\n";
	if (!song.artist.empty())
		x << "    <creator type=\"composer\">" << esc(song.artist) << "</creator>\n";
	if (!song.words.empty())
		x << "    <creator type=\"lyricist\">" << esc(song.words) << "</creator>\n";
	x << "  </identification>\n";

	x << "  <part-list>\n    <score-part id=\"P1\">\n"
	  << "      <part-name>" << esc(track.name.empty() ? "Guitar" : track.name)
	  << "</part-name>\n";
	// THE SOUND IT ASKS FOR, so that a reader opening this elsewhere gets the right instrument.
	x << "      <midi-instrument id=\"P1-I1\"><midi-program>"
	  << (track.midiProgram + 1) << "</midi-program></midi-instrument>\n";
	x << "    </score-part>\n  </part-list>\n";

	x << "  <part id=\"P1\">\n";

	// WHAT IS IN FORCE, so that an attribute is written only where it changes — a key and a time
	// signature repeated on every bar is a score with a signature on every bar.
	int beatsNow = -1, unitNow = -1, keyNow = -999;
	// TIES WAITING TO LAND, one list per voice: which notes of the next bar's first beat a tie
	// from this bar arrives at. See markTies.
	std::vector<std::vector<int> > carried;
	const int bars = (int) std::min(track.bars.size(), song.masterBars.size());

	for (int b = 0; b < bars; b++) {
		const GpBar& bar = track.bars[(size_t) b];
		const GpMasterBar& master = song.masterBars[(size_t) b];

		x << "    <measure number=\"" << (b + 1) << "\">\n";

		const bool first = (b == 0);
		const bool timeChanged = (master.beatsPerBar != beatsNow || master.beatUnit != unitNow);
		const bool keyChanged = (master.keyAccidentals != keyNow);
		if (first || timeChanged || keyChanged) {
			x << "      <attributes>\n";
			if (first)
				x << "        <divisions>" << DIVISIONS << "</divisions>\n";
			if (first || keyChanged) {
				x << "        <key><fifths>" << master.keyAccidentals << "</fifths>"
				  << "<mode>" << (master.minor ? "minor" : "major") << "</mode></key>\n";
			}
			if (first || timeChanged) {
				x << "        <time><beats>" << master.beatsPerBar << "</beats>"
				  << "<beat-type>" << master.beatUnit << "</beat-type></time>\n";
			}
			if (first) {
				if (staffCount > 1)
					x << "        <staves>2</staves>\n";

				// THE CLEF THE INSTRUMENT IS READ IN. Guitar music is written an octave above
				// where it sounds, which is what the little eight under the treble clef says; a
				// bass is read on a bass clef, also an octave up; drums have no pitch to read.
				//
				// A clef is NUMBERED when a part has more than one staff, and unnumbered when it
				// has one — so the same writing serves both.
				const std::string which = (staffCount > 1) ? " number=\"1\"" : "";
				if (staves != GP_TAB) {
					if (track.percussion) {
						x << "        <clef" << which
						  << "><sign>percussion</sign><line>2</line></clef>\n";
					}
					// A BASS, NOT A GUITAR. A guitar's lowest string is E2, MIDI forty; a bass's
					// is E1, MIDI twenty-eight, and a five-string bass goes to B0 at twenty-three.
					// The line between them is anything below the bottom of a guitar — thirty-four
					// sits in the gap with room on both sides.
					else if (lowestString(track) < 34) {
						x << "        <clef" << which << "><sign>F</sign><line>4</line>"
						     "<clef-octave-change>-1</clef-octave-change></clef>\n";
					}
					else {
						x << "        <clef" << which << "><sign>G</sign><line>2</line>"
						     "<clef-octave-change>-1</clef-octave-change></clef>\n";
					}
				}

				// THE TABLATURE STAFF: a TAB clef, one line per string, and what each string is
				// tuned to. The tuning is not decoration — it is how the reader knows a 5 on the
				// third line is a D, and it is what makes Verovio treat the staff as tablature
				// at all rather than as a staff with a strange clef on it.
				//
				// LINE ONE IS THE BOTTOM LINE, which on guitar tablature is the LOWEST string.
				// Our tuning runs the other way, highest string first, so the lines are counted
				// back through it.
				if (tabStaff) {
					const std::string tabWhich =
						(staffCount > 1) ? " number=\"2\"" : "";
					const int strings = (int) track.tuning.size();
					x << "        <staff-details" << tabWhich << "><staff-lines>" << strings
					  << "</staff-lines>\n";
					for (int line = 1; line <= strings; line++) {
						std::string step;
						int alter = 0, octave = 0;
						spell(track.tuning[(size_t) (strings - line)], 0, step, alter, octave);
						x << "          <staff-tuning line=\"" << line << "\">"
						  << "<tuning-step>" << step << "</tuning-step>";
						if (alter)
							x << "<tuning-alter>" << alter << "</tuning-alter>";
						x << "<tuning-octave>" << octave << "</tuning-octave></staff-tuning>\n";
					}
					x << "        </staff-details>\n";
					x << "        <clef" << tabWhich << "><sign>TAB</sign><line>5</line></clef>\n";
				}
			}
			x << "      </attributes>\n";
			beatsNow = master.beatsPerBar;
			unitNow = master.beatUnit;
			keyNow = master.keyAccidentals;
		}

		// THE TEMPO, WHERE IT CHANGES. Written as a direction with a metronome mark, which is
		// what a reader expects to see over the bar it takes effect on.
		for (size_t t = 0; t < song.tempos.size(); t++) {
			if (song.tempos[t].bar != b)
				continue;
			x << "      <direction placement=\"above\"><direction-type><metronome>"
			  << "<beat-unit>quarter</beat-unit><per-minute>"
			  << (int) std::lround(song.tempos[t].bpm)
			  << "</per-minute></metronome></direction-type>"
			  << "<sound tempo=\"" << (int) std::lround(song.tempos[t].bpm) << "\"/>"
			     "</direction>\n";
		}

		// THE MARKS THAT SEND A READER SOMEWHERE ELSE. A sign to come back to is drawn as its own
		// symbol; an instruction to go is written in words, the way it is printed on a page. The
		// `sound` element carries the same thing for anything playing the file, which is not us —
		// our own timeline already works the jumps out. See GpTimeline.
		for (size_t m = 0; m < master.marks.size(); m++) {
			const std::string& mark = master.marks[m];
			const char* symbol = NULL;
			const char* words = NULL;
			const char* sound = NULL;
			if (mark == "Segno" || mark == "SegnoSegno") {
				symbol = "<segno/>";
				sound = "segno=\"segno\"";
			}
			else if (mark == "Coda" || mark == "DoubleCoda") {
				symbol = "<coda/>";
				sound = "coda=\"coda\"";
			}
			else if (mark == "Fine") {
				words = "Fine";
				sound = "fine=\"yes\"";
			}
			else if (mark == "DaCapo") {
				words = "D.C.";
				sound = "dacapo=\"yes\"";
			}
			else if (mark == "DaCapoAlFine") {
				words = "D.C. al Fine";
				sound = "dacapo=\"yes\"";
			}
			else if (mark == "DaCapoAlCoda" || mark == "DaCapoAlDoubleCoda") {
				words = "D.C. al Coda";
				sound = "dacapo=\"yes\"";
			}
			else if (mark == "DaSegno" || mark == "DaSegnoSegno") {
				words = "D.S.";
				sound = "dalsegno=\"segno\"";
			}
			else if (mark == "DaSegnoAlFine" || mark == "DaSegnoSegnoAlFine") {
				words = "D.S. al Fine";
				sound = "dalsegno=\"segno\"";
			}
			else if (mark == "DaSegnoAlCoda" || mark == "DaSegnoSegnoAlCoda"
					|| mark == "DaSegnoAlDoubleCoda") {
				words = "D.S. al Coda";
				sound = "dalsegno=\"segno\"";
			}
			else if (mark == "DaCoda" || mark == "DaDoubleCoda") {
				words = "To Coda";
				sound = "tocoda=\"coda\"";
			}
			if (!symbol && !words)
				continue;
			x << "      <direction placement=\"above\"><direction-type>";
			if (symbol)
				x << symbol;
			else
				x << "<words>" << words << "</words>";
			x << "</direction-type>";
			if (sound)
				x << "<sound " << sound << "/>";
			x << "</direction>\n";
		}

		// THE SECTION'S NAME, as a rehearsal mark: "Intro", "Chorus". What the chart module
		// shows as a letter, a score shows in a box over the barline.
		if (!master.section.empty() || !master.sectionLetter.empty()) {
			const std::string& said =
				master.sectionLetter.empty() ? master.section : master.sectionLetter;
			x << "      <direction placement=\"above\"><direction-type><rehearsal>"
			  << esc(said) << "</rehearsal></direction-type></direction>\n";
		}

		// WHICH PASSES THIS BAR IS PLAYED ON, written out as MusicXML wants it: "1" or "1,2".
		std::string endingNumbers;
		for (size_t e = 0; e < master.endings.size(); e++) {
			if (e)
				endingNumbers += ",";
			endingNumbers += std::to_string(master.endings[e]);
		}

		// AN ENDING IS A SPAN, NOT A BAR. Guitar Pro marks every bar of a first-time ending with
		// the pass it belongs to; written music draws one bracket over the whole run, open at the
		// bar where it starts and closed at the bar where it ends. So the bracket opens where
		// these numbers differ from the bar before and closes where they differ from the bar
		// after — which for a one-bar ending is the same bar, as it should be.
		const std::vector<int> before =
			(b > 0) ? song.masterBars[(size_t) b - 1].endings : std::vector<int>();
		const std::vector<int> after =
			(b + 1 < (int) song.masterBars.size())
				? song.masterBars[(size_t) b + 1].endings : std::vector<int>();
		const bool endingOpens = !master.endings.empty() && master.endings != before;
		const bool endingCloses = !master.endings.empty() && master.endings != after;

		// A REPEAT OPENING, AND AN ENDING STARTING, both of which belong on the left barline.
		if (master.repeatOpen || endingOpens) {
			x << "      <barline location=\"left\">\n";
			if (endingOpens)
				x << "        <ending number=\"" << endingNumbers << "\" type=\"start\"/>\n";
			if (master.repeatOpen)
				x << "        <repeat direction=\"forward\"/>\n";
			x << "      </barline>\n";
		}

		// THE MUSIC. Each voice in turn, with the clock wound back between them.
		const std::vector<std::vector<const GpBeat*> > voices = splitVoices(bar);
		const std::vector<std::vector<int> > carriedIn = carried;
		carried.assign(voices.size(), std::vector<int>());

		// A BAR WITH NOTHING IN IT IS STILL A BAR, and a bar of silence is written as a whole
		// rest, not as blank staff. Some transcriptions store a silent bar as a rest and some
		// store nothing at all; both have to come out as the same thing on the page.
		if (voices.empty()) {
			const int full = (int) std::lround(
				(float) master.beatsPerBar * 4.f / (float) master.beatUnit * (float) DIVISIONS);
			for (int staff = 1; staff <= staffCount; staff++) {
				if (staff > 1)
					x << "      <backup><duration>" << full << "</duration></backup>\n";
				x << "      <note><rest measure=\"yes\"/><duration>" << full
				  << "</duration><voice>" << ((staff == 1) ? 1 : 5) << "</voice>";
				if (staffCount > 1)
					x << "<staff>" << staff << "</staff>";
				x << "</note>\n";
			}
		}

		// THE SAME MUSIC ON EACH STAFF. Notation and tablature are not two different parts: they
		// are one part written down twice, which is what MusicXML means by a part with two
		// staves — so the bar is walked once per staff, winding the clock back in between.
		int lastVoiceLen = 0;
		bool wroteAny = false;
		for (int staff = 1; staff <= staffCount; staff++) {
		for (size_t v = 0; v < voices.size(); v++) {
			if (wroteAny) {
				// BACK TO THE START OF THE BAR. MusicXML walks a bar forwards, and `backup` is
				// the only way to write two things happening at once — a second voice, or the
				// same music again on a second staff.
				if (lastVoiceLen > 0) {
					x << "      <backup><duration>" << lastVoiceLen
					  << "</duration></backup>\n";
				}
			}
			// VOICES ARE NUMBERED PER STAFF, the way every engraver numbers them: one to four on
			// the upper staff and five upwards on the lower, so that a voice number says which
			// staff it belongs to.
			const int voiceNo = (staff == 1) ? (int) v + 1 : (int) v + 5;
			lastVoiceLen = 0;
			for (size_t i = 0; i < voices[v].size(); i++) {
				if (!voices[v][i]->grace)
					lastVoiceLen += (int) std::lround(voices[v][i]->quarters * (float) DIVISIONS);
			}
			wroteAny = true;

			// A BAR'S REST IS A WHOLE REST, whatever the metre.
			//
			// A transcription writes a silent bar as one whole rest — in 6/8, in 5/4, in
			// anything — because that is what a whole rest means: not four quarter notes, but
			// this bar. Guitar Pro stores it with a whole note's length, so a silent bar of 6/8
			// arrives holding four quarters of rest in a bar with room for three.
			//
			// Written as a measure rest, which is the same thing said properly: the engraver
			// draws it centred in the bar and gives it the bar's own length.
			const bool wholeBarRest = (voices.size() == 1 && voices[v].size() == 1
				&& (voices[v][0]->rest || voices[v][0]->notes.empty()));
			if (wholeBarRest) {
				const int full = (int) std::lround(
					(float) master.beatsPerBar * 4.f / (float) master.beatUnit
					* (float) DIVISIONS);
				x << "      <note><rest measure=\"yes\"/><duration>" << full
				  << "</duration><voice>" << voiceNo << "</voice>";
				if (staffCount > 1)
					x << "<staff>" << staff << "</staff>";
				x << "</note>\n";
				lastVoiceLen = full;
				continue;
			}

			std::vector<Figured> figured;
			for (size_t i = 0; i < voices[v].size(); i++) {
				Figured f;
				f.beat = voices[v][i];
				f.figure = gpNoteFigure(f.beat->quarters);
				f.duration = (int) std::lround(f.beat->quarters * (float) DIVISIONS);
				if (!f.figure.exact)
					inexact++;
				figured.push_back(f);
			}
			markTuplets(figured);
			// The first beat of the same voice in the next bar, for a tie that crosses the line.
			const GpBeat* intoNext = NULL;
			if (b + 1 < bars) {
				const std::vector<std::vector<const GpBeat*> > after =
					splitVoices(track.bars[(size_t) b + 1]);
				if (v < after.size() && !after[v].empty())
					intoNext = after[v][0];
			}
			std::vector<int> intoNextStops;
			markTies(figured, intoNext, &intoNextStops);
			if (!intoNextStops.empty())
				carried[(size_t) v] = intoNextStops;

			// A TIE THAT ARRIVED FROM THE BAR BEFORE lands on the first beat of this voice. The
			// bar that started it worked out which note it lands on; it could not mark it there,
			// because that note had not been written down yet.
			if (!figured.empty() && v < carriedIn.size()) {
				for (size_t k = 0; k < carriedIn[v].size(); k++) {
					const int which = carriedIn[v][k];
					if (which >= 0 && which < (int) figured[0].tieStop.size())
						figured[0].tieStop[(size_t) which] = true;
				}
			}
			markBeams(figured, beatLength(master.beatsPerBar, master.beatUnit));
			markTechniques(figured);

			for (size_t i = 0; i < figured.size(); i++) {
				const Figured& f = figured[i];
				const GpBeat& beat = *f.beat;

				if (beat.rest || beat.notes.empty()) {
					x << "      <note><rest/><duration>" << f.duration << "</duration>"
					  << "<voice>" << voiceNo << "</voice>"
					  << "<type>" << f.figure.type << "</type>";
					for (int d = 0; d < f.figure.dots; d++)
						x << "<dot/>";
					if (staffCount > 1)
						x << "<staff>" << staff << "</staff>";
					x << "</note>\n";
					continue;
				}

				// WHETHER THIS COPY OF THE MUSIC IS THE TABLATURE ONE. With both staves the tab
				// is the lower; with tablature alone it is the only one.
				const bool onTab = tabStaff && (staff == staffCount);

				for (size_t n = 0; n < beat.notes.size(); n++) {
					const GpNote& note = beat.notes[n];
					x << "      <note>";
					if (beat.grace)
						x << "<grace slash=\"" << (beat.grace == 1 ? "yes" : "no") << "\"/>";
					if (n > 0)
						x << "<chord/>";

					if (track.percussion) {
						// A DRUM HAS NO PITCH, only a place on the staff. MusicXML says where by
						// naming a step and an octave anyway, which is what `unpitched` is.
						std::string step;
						int alter = 0, octave = 0;
						spell(note.midi, 0, step, alter, octave);
						x << "<unpitched><display-step>" << step << "</display-step>"
						  << "<display-octave>" << octave << "</display-octave></unpitched>";
					}
					else {
						std::string step;
						int alter = 0, octave = 0;
						spell(note.midi, master.keyAccidentals, step, alter, octave);
						x << "<pitch><step>" << step << "</step>";
						if (alter)
							x << "<alter>" << alter << "</alter>";
						x << "<octave>" << octave << "</octave></pitch>";
					}

					if (!beat.grace)
						x << "<duration>" << f.duration << "</duration>";
					// A TIED NOTE CONTINUES THE ONE BEFORE IT. The model marks the note that
					// CONTINUES; the one that started it is marked here as well, which the
					// writer below does by looking back.
					const bool startsTie = (n < f.tieStart.size()) && f.tieStart[n];
					const bool stopsTie = (n < f.tieStop.size()) && f.tieStop[n];
					if (stopsTie)
						x << "<tie type=\"stop\"/>";
					if (startsTie)
						x << "<tie type=\"start\"/>";
					x << "<voice>" << voiceNo << "</voice>";
					x << "<type>" << f.figure.type << "</type>";
					for (int d = 0; d < f.figure.dots; d++)
						x << "<dot/>";
					if (f.figure.actual != f.figure.normal) {
						x << "<time-modification><actual-notes>" << f.figure.actual
						  << "</actual-notes><normal-notes>" << f.figure.normal
						  << "</normal-notes></time-modification>";
					}
					// A DEAD NOTE IS A CROSS and a harmonic is a diamond — the two notehead
					// shapes guitar music uses, and the only way either can be said that Verovio
					// will draw. A harmonic written as MusicXML's own `harmonic` element is not
					// read at all; see the note on `technical` below.
					if (note.dead)
						x << "<notehead>x</notehead>";
					else if (note.harmonic || note.artificialHarmonic)
						x << "<notehead>diamond</notehead>";
					// WHICH STAFF IT IS ON, which MusicXML wants before the beams.
					if (staffCount > 1)
						x << "<staff>" << staff << "</staff>";
					// THE BEAMS GO ON THE FIRST NOTE OF A CHORD ONLY, because a chord has one
					// stem and one set of beams however many heads hang off it.
					if (n == 0) {
						for (size_t L = 0; L < f.beams.size(); L++) {
							if (!f.beams[L].empty()) {
								x << "<beam number=\"" << (L + 1) << "\">"
								  << f.beams[L] << "</beam>";
							}
						}
					}

					// WHAT IS DRAWN ON OR AROUND THE NOTE.
					std::ostringstream marks;
					if (stopsTie)
						marks << "<tied type=\"stop\"/>";
					if (startsTie)
						marks << "<tied type=\"start\"/>";
					if (f.tupletStart)
						marks << "<tuplet type=\"start\"/>";
					if (f.tupletStop)
						marks << "<tuplet type=\"stop\"/>";

					// NOT ON THE TABLATURE STAFF. An accent is a mark about how a note is
					// played, and tablature is about where it is played — printed tab does not
					// carry them, Verovio's own model has nowhere to hang one on a tab group,
					// and asking it to put one there crashes its layout outright.
					std::ostringstream arts;
					if (!onTab) {
						if (note.staccato)
							arts << "<staccato/>";
						if (note.accent)
							arts << "<accent/>";
						if (note.heavyAccent)
							arts << "<strong-accent/>";
					}
					if (arts.tellp() > 0)
						marks << "<articulations>" << arts.str() << "</articulations>";

					// THE STRING AND THE FRET, AND NOTHING ELSE IN HERE.
					//
					// Verovio reads four things from a `technical` element — a fingering, a thumb
					// position, a string and a fret — and turns EVERY OTHER CHILD into an
					// articulation. So a hammer-on written where MusicXML says to write it is not
					// drawn as a hammer-on: it becomes an accent with no value, which draws
					// nothing on an ordinary staff and crashes the layout outright on a tablature
					// one, because tablature has nowhere to hang an articulation.
					//
					// What the techniques below do instead is say the same thing in a way an
					// engraver already draws.
					std::ostringstream tech;
					if (note.string > 0)
						tech << "<string>" << note.string << "</string>";
					if (note.fret >= 0)
						tech << "<fret>" << note.fret << "</fret>";
					if (tech.tellp() > 0)
						marks << "<technical>" << tech.str() << "</technical>";

					// A HAMMER-ON AND A PULL-OFF ARE DRAWN AS A SLUR, which is how guitar music
					// has always shown them: a curve between two notes, one struck and one not.
					// Not on the tablature staff, where the fret numbers carry it.
					const Figured::Joined join =
						(n < f.joined.size()) ? f.joined[n] : Figured::Joined();
					if (!onTab) {
						if (join.hammerStart || join.pullStart)
							marks << "<slur type=\"start\" number=\"1\"/>";
						if (join.hammerStop || join.pullStop)
							marks << "<slur type=\"stop\" number=\"1\"/>";
					}

					std::ostringstream orn;
					if (note.tremolo)
						orn << "<tremolo type=\"single\">3</tremolo>";
					// VIBRATO IS NOT WRITTEN HERE. It is a wavy line over however long it lasts,
					// and the model says only which notes carry it — a line started on every one
					// of them and ended on none is a line the engraver cannot place. It belongs
					// with the other spanners, once there is something saying where it stops.
					if (orn.tellp() > 0)
						marks << "<ornaments>" << orn.str() << "</ornaments>";

					if (join.slideStart)
						marks << "<slide type=\"start\" number=\"1\"/>";
					if (join.slideStop)
						marks << "<slide type=\"stop\" number=\"1\"/>";

					if (marks.tellp() > 0)
						x << "<notations>" << marks.str() << "</notations>";
					x << "</note>\n";
				}
			}
		}

		}   // every staff

		// THE RIGHT-HAND BARLINE: a repeat closing, an ending finishing, or a double bar.
		if (master.repeatCount > 0 || endingCloses || master.doubleBar) {
			x << "      <barline location=\"right\">\n";
			if (master.repeatCount > 0 || master.doubleBar) {
				x << "        <bar-style>"
				  << (master.repeatCount > 0 ? "light-heavy" : "light-light")
				  << "</bar-style>\n";
			}
			if (endingCloses) {
				// STOPPED WHERE THE MUSIC GOES BACK, and merely DISCONTINUED where it does not:
				// a last-time ending has no hook on its right-hand end, because nothing returns
				// from it.
				x << "        <ending number=\"" << endingNumbers << "\" type=\""
				  << (master.repeatCount > 0 ? "stop" : "discontinue") << "\"/>\n";
			}
			if (master.repeatCount > 0) {
				x << "        <repeat direction=\"backward\" times=\""
				  << master.repeatCount << "\"/>\n";
			}
			x << "      </barline>\n";
		}

		x << "    </measure>\n";
	}

	x << "  </part>\n</score-partwise>\n";

	if (trouble && inexact > 0) {
		char said[160];
		std::snprintf(said, sizeof(said),
			"%d of the lengths in this part are not any ordinary note and were written as the "
			"nearest one", inexact);
		*trouble = said;
	}
	return x.str();
}


} // namespace px
