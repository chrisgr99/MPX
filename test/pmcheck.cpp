/** What a file says about palm muting, let ring and strums, per track and per string. */
#include "../src/GuitarPro.hpp"
#include <cstdio>
using namespace px;
int main(int argc, char** argv) {
	GpSong song; std::string why;
	if (!gpRead(argv[1], song, &why)) { std::printf("no: %s\n", why.c_str()); return 1; }
	std::printf("%s — %s\n", song.title.c_str(), song.artist.c_str());
	for (size_t t = 0; t < song.tracks.size(); t++) {
		const GpTrack& k = song.tracks[t];
		long notes = 0, pm = 0, lr = 0, dead = 0, brushed = 0, beats = 0;
		long pmByString[9] = {0}, notesByString[9] = {0};
		for (size_t b = 0; b < k.bars.size(); b++)
			for (size_t i = 0; i < k.bars[b].beats.size(); i++) {
				const GpBeat& e = k.bars[b].beats[i];
				beats++;
				if (e.brush != 0) brushed++;
				for (size_t n = 0; n < e.notes.size(); n++) {
					const GpNote& g = e.notes[n];
					notes++;
					const int s = (g.string >= 1 && g.string <= 8) ? g.string : 0;
					notesByString[s]++;
					if (g.palmMute) { pm++; pmByString[s]++; }
					if (g.letRing) lr++;
					if (g.dead) dead++;
				}
			}
		if (!notes) continue;
		std::printf("\ntrack %zu  %-22s  letRingThroughout=%d\n", t, k.name.c_str(),
			(int) k.letRingThroughout);
		std::printf("   %ld beats (%ld strummed), %ld notes: %ld palm muted, %ld let ring, %ld dead\n",
			beats, brushed, notes, pm, lr, dead);
		std::printf("   by string (1 = highest):");
		for (int s = 1; s <= 6; s++)
			std::printf("  %d: %ld/%ld", s, pmByString[s], notesByString[s]);
		std::printf("\n");
	}
	return 0;
}
