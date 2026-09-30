/** Reading Guitar Pro files, from a command line.

WHY THERE IS A PROGRAM FOR THIS. The reader knows nothing about Rack, so it can be run against a
folder of real files and made right before anything is patched. A fault found here is a line and
a file name; the same fault found by dropping a file on a panel is silence.

    make gptest ARGS="~/Downloads/song.gp ~/Downloads/other.gpx"
*/
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
	for (size_t i = 0; i < song.tracks.size(); i++) {
		const GpTrack& t = song.tracks[i];
		int notes = 0;
		for (size_t b = 0; b < t.bars.size(); b++) {
			for (size_t e = 0; e < t.bars[b].beats.size(); e++)
				notes += (int) t.bars[b].beats[e].notes.size();
		}
		std::printf("%-56s    %-24s %4d bars %5d notes%s\n", "", t.name.c_str(),
			(int) t.bars.size(), notes, t.percussion ? "  (percussion)" : "");
	}
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: gptest FILE ...\n");
		return 1;
	}
	for (int i = 1; i < argc; i++)
		report(argv[i]);
	return 0;
}
