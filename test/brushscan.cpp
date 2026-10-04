#include "../src/GuitarPro.hpp"
#include <cstdio>
using namespace px;
int main(int argc, char** argv) {
	long files = 0, tracks = 0, beats = 0, brushed = 0, notes = 0, pm = 0, chords = 0, chordsBrushed = 0;
	for (int a = 1; a < argc; a++) {
		GpSong song; std::string why;
		if (!gpRead(argv[a], song, &why)) continue;
		files++;
		for (size_t t = 0; t < song.tracks.size(); t++) {
			const GpTrack& k = song.tracks[t];
			if (k.percussion) continue;
			tracks++;
			for (size_t b = 0; b < k.bars.size(); b++)
				for (size_t i = 0; i < k.bars[b].beats.size(); i++) {
					const GpBeat& e = k.bars[b].beats[i];
					beats++;
					if (e.brush != 0) brushed++;
					if (e.notes.size() >= 3) { chords++; if (e.brush != 0) chordsBrushed++; }
					notes += (long) e.notes.size();
					for (size_t n = 0; n < e.notes.size(); n++)
						if (e.notes[n].palmMute) pm++;
				}
		}
	}
	std::printf("%ld files, %ld pitched tracks, %ld beats, %ld notes\n", files, tracks, beats, notes);
	std::printf("beats marked as a strum: %ld (%.1f%%)\n", brushed, 100.0 * brushed / (beats ? beats : 1));
	std::printf("chords of 3+ notes: %ld, of which marked as a strum: %ld (%.1f%%)\n",
		chords, chordsBrushed, 100.0 * chordsBrushed / (chords ? chords : 1));
	std::printf("notes palm muted: %ld (%.1f%%)\n", pm, 100.0 * pm / (notes ? notes : 1));
	return 0;
}
