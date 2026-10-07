#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace px;
int main(int argc, char** argv) {
	std::ifstream f(argv[1], std::ios::binary); std::stringstream all; all << f.rdbuf();
	const std::string t = all.str(); const std::vector<uint8_t> b(t.begin(), t.end());
	GpSong s; std::string why;
	if (!mxReadBytes(b, s, &why)) { std::printf("%s\n", why.c_str()); return 1; }
	std::printf("master bars: %d\n", (int) s.masterBars.size());
	for (size_t i = 0; i < s.masterBars.size() && i < 5; i++)
		std::printf("  master %d: number=%d quarters=%.2f  %d/%d\n", (int) i,
			s.masterBars[i].number, s.masterBars[i].quarters,
			s.masterBars[i].beatsPerBar, s.masterBars[i].beatUnit);
	GpTimeline line;
	if (!gpBuildTimeline(s, line, &why)) { std::printf("%s\n", why.c_str()); return 1; }
	for (size_t i = 0; i < line.bars.size() && i < 5; i++)
		std::printf("  played %d: written %d, starts %.2fq, %.2fq long, %.3fs\n", (int) i,
			line.bars[i].written, line.bars[i].startQuarters, line.bars[i].quarters,
			line.bars[i].startSeconds);
	for (size_t t2 = 0; t2 < line.tracks.size(); t2++)
		for (size_t n = 0; n < line.tracks[t2].size() && n < 6; n++)
			std::printf("  note %d: %.3fq long %.3fq midi %d\n", (int) n,
				line.tracks[t2][n].startQuarters, line.tracks[t2][n].lengthQuarters,
				line.tracks[t2][n].note.midi);
	return 0;
}
