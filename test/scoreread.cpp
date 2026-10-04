/** WHAT COMES IN WHEN A SCORE IS LOADED, by either reader, chosen the way the module chooses it:
by what is in the file rather than by its name. Reports the parts, the bars and the notes, then
builds the played timeline so repeats and jumps are exercised too.

	make scoreread ARGS="a-score.mxl"
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

int main(int argc, char** argv) {
	if (argc < 2) { std::printf("usage: scoreread <file>\n"); return 1; }
	std::ifstream file(argv[1], std::ios::binary);
	if (!file) { std::printf("could not open it\n"); return 1; }
	std::stringstream all;
	all << file.rdbuf();
	const std::string text = all.str();
	const std::vector<uint8_t> bytes(text.begin(), text.end());

	GpSong song;
	std::string why;
	const std::string head = text.substr(0, 2048 < text.size() ? 2048 : text.size());
	bool ok = false;
	const bool looksXML = mxLooksLikeMusicXML(head);
	if (looksXML)
		ok = mxReadBytes(bytes, song, &why);
	if (!ok)
		ok = gpReadBytes(bytes, song, looksXML ? NULL : &why);
	if (!ok) { std::printf("could not read it: %s\n", why.c_str()); return 1; }

	std::printf("%s", song.title.empty() ? "(no title)" : song.title.c_str());
	if (!song.artist.empty()) std::printf(" — %s", song.artist.c_str());
	std::printf("\n   read as %s, %d bars, %d parts, opening tempo %.0f\n",
		looksXML ? "MusicXML" : "Guitar Pro", (int) song.masterBars.size(),
		(int) song.tracks.size(), song.tempo);

	for (size_t t = 0; t < song.tracks.size(); t++) {
		const GpTrack& k = song.tracks[t];
		long notes = 0;
		for (size_t b = 0; b < k.bars.size(); b++)
			for (size_t i = 0; i < k.bars[b].beats.size(); i++)
				notes += (long) k.bars[b].beats[i].notes.size();
		std::printf("   part %zu  %-28s %6ld notes, %s\n", t,
			k.name.empty() ? "(unnamed)" : k.name.c_str(), notes,
			k.tuning.empty() ? "no fingerboard" : "fretted");
	}

	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) { std::printf("   no timeline: %s\n", why.c_str()); return 1; }
	long played = 0;
	for (size_t t = 0; t < line.tracks.size(); t++) played += (long) line.tracks[t].size();
	std::printf("   played: %d bars, %.0f seconds, %ld notes (%d passed over)\n",
		(int) line.bars.size(), line.seconds, played, line.skipped);
	return 0;
}
