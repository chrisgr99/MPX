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
	{
		int own = 0, longer = 0;
		for (size_t i = 0; i < s.masterBars.size(); i++) {
			if (s.masterBars[i].quarters <= 0.f)
				continue;
			own++;
			const float metre = (float) s.masterBars[i].beatsPerBar * 4.f
				/ (float) ((s.masterBars[i].beatUnit > 0) ? s.masterBars[i].beatUnit : 4);
			if (s.masterBars[i].quarters > metre)
				longer++;
		}
		std::printf("  bars with a length of their own: %d (%d of them longer than the metre)\n",
			own, longer);
	}
	for (size_t i = 0; i < s.masterBars.size() && i < 5; i++)
		std::printf("  master %d: number=%d quarters=%.2f  %d/%d\n", (int) i,
			s.masterBars[i].number, s.masterBars[i].quarters,
			s.masterBars[i].beatsPerBar, s.masterBars[i].beatUnit);
	int trills = 0, tremolos = 0, graces = 0;
	for (size_t t = 0; t < s.tracks.size(); t++)
		for (size_t b = 0; b < s.tracks[t].bars.size(); b++)
			for (size_t e = 0; e < s.tracks[t].bars[b].beats.size(); e++) {
				const GpBeat& beat = s.tracks[t].bars[b].beats[e];
				if (beat.grace) graces++;
				for (size_t n = 0; n < beat.notes.size(); n++) {
					if (beat.notes[n].trill) trills++;
					if (beat.notes[n].tremolo) tremolos++;
				}
			}
	std::printf("  the reader found: %d trills, %d tremolos, %d grace beats\n",
		trills, tremolos, graces);

	// WHAT THE READER MADE OF ONE BAR, given as a third argument (the number the page shows).
	if (argc > 2) {
		const int want = std::atoi(argv[2]);
		for (size_t t = 0; t < s.tracks.size(); t++) {
			for (size_t b = 0; b < s.tracks[t].bars.size(); b++) {
				if ((int) b >= (int) s.masterBars.size())
					break;
				const int number = (s.masterBars[b].number >= 0) ? s.masterBars[b].number
					: (int) b + 1;
				if (number != want)
					continue;
				int notes = 0;
				for (size_t e = 0; e < s.tracks[t].bars[b].beats.size(); e++)
					notes += (int) s.tracks[t].bars[b].beats[e].notes.size();
				std::printf("  track %d bar %d: %d beats, %d notes\n", (int) t, number,
					(int) s.tracks[t].bars[b].beats.size(), notes);
				for (size_t e = 0; e < s.tracks[t].bars[b].beats.size() && e < 12; e++) {
					const GpBeat& beat = s.tracks[t].bars[b].beats[e];
					std::printf("    beat at %6.3fq lasting %5.3fq: %d notes\n",
						beat.start, beat.quarters, (int) beat.notes.size());
				}
			}
		}
	}

	// WHERE THE GRACE NOTES ARE, and what the timeline made of them.
	{
		std::printf("  grace notes in the song model:\n");
		for (size_t t = 0; t < s.tracks.size(); t++)
			for (size_t b = 0; b < s.tracks[t].bars.size(); b++)
				for (size_t e = 0; e < s.tracks[t].bars[b].beats.size(); e++) {
					const GpBeat& beat = s.tracks[t].bars[b].beats[e];
					if (!beat.grace)
						continue;
					const int number = (b < s.masterBars.size() && s.masterBars[b].number >= 0)
						? s.masterBars[b].number : (int) b + 1;
					std::printf("    bar %3d at %6.3fq lasting %5.3fq, %d notes, level %.2f\n",
						number, beat.start, beat.quarters, (int) beat.notes.size(),
						beat.dynamic);
				}
	}

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
