/** Repeats, endings and jumps: the played order, against songs written here rather than read.

WHY NOT A REAL FILE. Forty-seven transcriptions downloaded from the internet hold seventeen
closing repeats and nine sets of alternate endings between them, and not one navigation mark —
no D.S., no D.C., no Fine, one Coda with nothing sending you to it. A mark somebody writes into a
score by hand is common in printed music and rare in tablature, so waiting for a file to test
this with is waiting for ever.

So the songs are written here, a few bars each with nothing in them but their structure, and what
is checked is the order the bars come out in. Nothing about a file is being tested; the walk is.

    make navtest
*/
#include "../src/GpTimeline.hpp"

#include <cstdio>
#include <string>

using namespace px;


static int failures = 0;


/** A song of `bars` empty 4/4 bars, with one track so the timeline has something to walk. */
static GpSong songOf(int bars) {
	GpSong song;
	for (int i = 0; i < bars; i++)
		song.masterBars.push_back(GpMasterBar());
	GpTrack track;
	track.name = "test";
	track.bars.resize((size_t) bars);
	song.tracks.push_back(track);
	GpTempo t;
	song.tempos.push_back(t);
	return song;
}


/** The played order as "1 2 3", so a wrong answer reads as one. */
static std::string orderOf(const GpSong& song, int* skipped) {
	GpTimeline line;
	std::string why;
	if (!gpBuildTimeline(song, line, &why))
		return "(nothing: " + why + ")";
	std::string out;
	for (size_t i = 0; i < line.bars.size(); i++) {
		if (!out.empty())
			out += " ";
		out += std::to_string(line.bars[i].written + 1);
	}
	if (skipped)
		*skipped = line.skipped;
	return out;
}


static void check(const char* what, const GpSong& song, const char* want, int wantSkipped = 0) {
	int skipped = 0;
	const std::string got = orderOf(song, &skipped);
	const bool ok = (got == want) && (skipped == wantSkipped);
	if (!ok)
		failures++;
	std::printf("%-44s %s\n", what, ok ? "yes" : "NO");
	if (!ok) {
		std::printf("%-44s   wanted  %s%s\n", "", want,
			wantSkipped ? "   (with something skipped)" : "");
		std::printf("%-44s   got     %s%s\n", "", got.c_str(),
			skipped ? "   (with something skipped)" : "");
	}
}


int main() {
	// Nothing at all: eight bars once through.
	check("plain", songOf(8), "1 2 3 4 5 6 7 8");

	// A repeat closing on bar four, played twice. The opening repeat is on bar one, which is
	// also where a song with no opening repeat starts from.
	{
		GpSong s = songOf(6);
		s.masterBars[0].repeatOpen = true;
		s.masterBars[3].repeatCount = 2;
		check("a repeat", s, "1 2 3 4 1 2 3 4 5 6");
	}

	// Three times through.
	{
		GpSong s = songOf(5);
		s.masterBars[1].repeatOpen = true;
		s.masterBars[3].repeatCount = 3;
		check("a repeat played three times", s, "1 2 3 4 2 3 4 2 3 4 5");
	}

	// First and second endings: bar four is played the first time round, bar five the second.
	// The repeat closes on the first ending, which is where it is written.
	{
		GpSong s = songOf(6);
		s.masterBars[1].repeatOpen = true;
		s.masterBars[3].endings.push_back(1);
		s.masterBars[3].repeatCount = 2;
		s.masterBars[4].endings.push_back(2);
		check("first and second endings", s, "1 2 3 4 2 3 5 6");
	}

	// Da Capo: back to the top once, and then on to the end. Repeats are not taken again after
	// a jump, which is the usual reading of one.
	{
		GpSong s = songOf(6);
		s.masterBars[3].marks.push_back("DaCapo");
		check("da capo", s, "1 2 3 4 1 2 3 4 5 6");
	}

	// Da Capo al Fine: back to the top, and stop where it says Fine.
	{
		GpSong s = songOf(8);
		s.masterBars[2].marks.push_back("Fine");
		s.masterBars[5].marks.push_back("DaCapoAlFine");
		check("da capo al fine", s, "1 2 3 4 5 6 1 2 3");
	}

	// Dal Segno: back to the sign.
	{
		GpSong s = songOf(8);
		s.masterBars[2].marks.push_back("Segno");
		s.masterBars[5].marks.push_back("DaSegno");
		check("dal segno", s, "1 2 3 4 5 6 3 4 5 6 7 8");
	}

	// D.S. al Coda: back to the sign, on to the "to coda" mark, then the coda to the end. The
	// bar carrying the coda is not played on the way past, because the jump has not happened yet.
	{
		GpSong s = songOf(10);
		s.masterBars[2].marks.push_back("Segno");
		s.masterBars[4].marks.push_back("DaCoda");
		s.masterBars[6].marks.push_back("DaSegnoAlCoda");
		s.masterBars[7].marks.push_back("Coda");
		check("dal segno al coda", s, "1 2 3 4 5 6 7 3 4 5 8 9 10");
	}

	// A jump to a sign the song does not carry: counted, and the song plays on rather than
	// stopping or looping.
	{
		GpSong s = songOf(5);
		s.masterBars[2].marks.push_back("DaSegno");
		check("a jump to a missing sign", s, "1 2 3 4 5", 1);
	}

	// A song that is nothing but a repeat: it must end.
	{
		GpSong s = songOf(2);
		s.masterBars[0].repeatOpen = true;
		s.masterBars[1].repeatCount = 2;
		check("a song that is only a repeat", s, "1 2 1 2");
	}

	std::printf("\n%s\n", failures ? "SOMETHING IS WRONG" : "all of them");
	return failures ? 1 : 0;
}
