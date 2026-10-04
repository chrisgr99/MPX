/** What the timeline now makes of a song's chords: how many are strummed, which way, and how
wide the stroke comes out. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include <cstdio>
#include <map>
using namespace px;
int main(int argc, char** argv) {
	GpSong song; std::string why;
	if (!gpRead(argv[1], song, &why)) { std::printf("no: %s\n", why.c_str()); return 1; }
	GpTimeline line;
	gpBuildTimeline(song, line, NULL);
	const int track = (argc > 2) ? std::atoi(argv[2]) : 0;
	long down = 0, up = 0, plain = 0;
	std::map<int,long> widths;
	std::map<double,long> downAt, upAt;
	if (track < 0 || track >= (int) line.tracks.size()) { std::printf("no such track\n"); return 1; }
	const std::vector<GpPlayedNote>& notes = line.tracks[track];
	for (size_t i = 0; i < notes.size(); i++) {
		const GpPlayedNote& p = notes[i];
		if (p.brush > 0) { down++; if (p.strumIndex == 0) widths[p.strumCount]++; }
		else if (p.brush < 0) { up++; }
		else plain++;
	}
	std::printf("track %d: %ld notes in down-strokes, %ld in up-strokes, %ld not strummed\n",
		track, down, up, plain);
	std::printf("down-strokes by how many strings the hand crossed:");
	for (std::map<int,long>::iterator it = widths.begin(); it != widths.end(); ++it)
		std::printf("  %d strings: %ld", it->first, it->second);
	std::printf("\n");
	return 0;
}
