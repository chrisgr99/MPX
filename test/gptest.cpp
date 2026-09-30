/** Reading Guitar Pro files, from a command line.

WHY THERE IS A PROGRAM FOR THIS. The reader knows nothing about Rack, so it can be run against a
folder of real files and made right before anything is patched. A fault found here is a line and
a file name; the same fault found by dropping a file on a panel is silence.

    make gptest ARGS="~/Downloads/song.gp ~/Downloads/other.gpx"
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"

#include <cstdio>
#include <string>

using namespace px;


static void report(const std::string& path) {
	GpSong song;
	std::string why;
	const size_t slash = path.rfind('/');
	const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

	if (!gpRead(path, song, &why)) {
		std::printf("%-56s  NO: %s\n", name.c_str(), why.c_str());
		return;
	}

	std::printf("%-56s  %s — %s\n", name.c_str(),
		song.title.empty() ? "(untitled)" : song.title.c_str(),
		song.artist.empty() ? "(no artist)" : song.artist.c_str());
	std::printf("%-56s  %g bpm, %d bars, %d tracks, %d chord marks, %d skipped\n", "",
		song.tempo, (int) song.masterBars.size(), (int) song.tracks.size(),
		(int) song.chords.size(), song.skipped);
	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) {
		std::printf("%-56s  NO TIMELINE: %s\n", "", why.c_str());
		return;
	}
	const int minutes = (int) (line.seconds / 60.0);
	std::printf("%-56s  plays %d bars, %d:%04.1f, %d skipped\n", "",
		(int) line.bars.size(), minutes, line.seconds - minutes * 60.0, line.skipped);

	for (size_t i = 0; i < song.tracks.size(); i++) {
		const GpTrack& t = song.tracks[i];
		int notes = 0;
		for (size_t b = 0; b < t.bars.size(); b++) {
			for (size_t e = 0; e < t.bars[b].beats.size(); e++)
				notes += (int) t.bars[b].beats[e].notes.size();
		}
		std::printf("%-56s    %-24s %4d bars %5d notes  %5d played%s\n", "", t.name.c_str(),
			(int) t.bars.size(), notes, (int) line.tracks[i].size(),
			t.percussion ? "  (percussion)" : "");
	}
}


/** One bar of one track, as it is played: for reading against the score by eye. */
static void showBar(const std::string& path, int track, int bar) {
	GpSong song;
	GpTimeline line;
	std::string why;
	if (!gpRead(path, song, &why) || !gpBuildTimeline(song, line, &why)) {
		std::printf("%s\n", why.c_str());
		return;
	}
	if (track < 0 || track >= (int) line.tracks.size() || bar < 1
			|| bar > (int) line.bars.size()) {
		std::printf("that song has %d tracks and plays %d bars\n",
			(int) line.tracks.size(), (int) line.bars.size());
		return;
	}
	const GpPlayedBar& b = line.bars[(size_t) bar - 1];
	std::printf("%s, bar %d of the playing: written bar %d, pass %d, %g quarters, %.3fs at %g\n",
		song.tracks[(size_t) track].name.c_str(), bar, b.written + 1, b.pass, b.quarters,
		b.startSeconds, b.bpm);
	const std::vector<GpPlayedNote>& notes = line.tracks[(size_t) track];
	for (size_t i = 0; i < notes.size(); i++) {
		if (notes[i].bar != bar - 1)
			continue;
		const GpPlayedNote& n = notes[i];
		std::printf("    %7.3f  %6.3fs  midi %3d  string %d fret %2d  level %.2f%s%s%s%s%s\n",
			n.startQuarters - b.startQuarters, n.lengthSeconds, n.note.midi, n.note.string,
			n.note.fret, n.dynamic,
			n.note.dead ? " dead" : "", n.note.letRing ? " ring" : "",
			n.note.palmMute ? " palm" : "", n.note.hammer ? " hammer" : "",
			n.note.slide ? " slide" : "");
	}
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: gptest FILE ...          what is in each file\n");
		std::printf("       gptest FILE TRACK BAR    one played bar, counting from one\n");
		return 1;
	}
	if (argc == 4) {
		showBar(argv[1], std::atoi(argv[2]) - 1, std::atoi(argv[3]));
		return 0;
	}
	for (int i = 1; i < argc; i++)
		report(argv[i]);
	return 0;
}
