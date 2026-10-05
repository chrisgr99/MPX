/** WHAT THE DERIVED HARMONY ACTUALLY SAYS, bar by bar.

A score carries no chord symbols, so what it offers a generative module is the pitches sounding
and how much each matters — long, on the beat and loud being what separates a chord tone from a
passing note. That weighting is the kind of thing that reads reasonably and is wrong in practice,
and the only way to tell is to run it over music somebody has heard and read the answer out.

So this does what the module does: walks the played timeline, adds each note's weight to its pitch
class, lets the whole set fade over about a beat, and prints what the set says at the end of every
bar. Against a piece you know, you can see whether it is finding the harmony or the surface.

	make harmony ARGS="a-score.musicxml"
	make harmony ARGS="a-song.gp5 2"     one part of it
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

static const char* PC_NAMES[12] =
	{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};


int main(int argc, char** argv) {
	if (argc < 2) { std::printf("usage: harmony <score> [part]\n"); return 1; }
	std::ifstream file(argv[1], std::ios::binary);
	if (!file) { std::printf("could not open it\n"); return 1; }
	std::stringstream all;
	all << file.rdbuf();
	const std::string text = all.str();
	const std::vector<uint8_t> bytes(text.begin(), text.end());

	GpSong song;
	std::string why;
	const std::string head = text.substr(0, text.size() < 2048 ? text.size() : 2048);
	bool ok = false;
	if (mxLooksLikeMusicXML(head))
		ok = mxReadBytes(bytes, song, &why);
	if (!ok)
		ok = gpReadBytes(bytes, song, &why);
	if (!ok) { std::printf("could not read it: %s\n", why.c_str()); return 1; }

	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) { std::printf("no timeline: %s\n", why.c_str()); return 1; }

	// EVERY PART AT ONCE unless one is named: the harmony of a quartet is what the four of them
	// are doing together, not what any one of them is doing.
	const int only = (argc > 2) ? std::atoi(argv[2]) : -1;
	std::vector<const GpPlayedNote*> notes;
	for (size_t t = 0; t < line.tracks.size(); t++) {
		if (only >= 0 && (int) t != only)
			continue;
		if (t < song.tracks.size() && song.tracks[t].percussion)
			continue;     // A drum has no pitch to contribute.
		for (size_t i = 0; i < line.tracks[t].size(); i++)
			notes.push_back(&line.tracks[t][i]);
	}
	if (notes.empty()) { std::printf("nothing to play\n"); return 1; }
	std::sort(notes.begin(), notes.end(),
		[](const GpPlayedNote* a, const GpPlayedNote* b) {
			return a->startQuarters < b->startQuarters;
		});

	std::printf("%s — %d bars, %d parts%s\n",
		song.title.empty() ? "(no title)" : song.title.c_str(),
		(int) line.bars.size(), (int) song.tracks.size(),
		(only >= 0) ? " (one part)" : "");
	std::printf("bar  key      strongest pitches                    bass\n");

	float weight[12] = {0.f};
	int bassMidi = -1;
	double bassWhen = -1e9;
	size_t at = 0;
	int keyWas = -999;

	for (size_t b = 0; b < line.bars.size(); b++) {
		const double from = line.bars[b].startQuarters;
		const double to = from + line.bars[b].quarters;

		// The notes of this bar, each fading from where it starts to the end of the bar.
		while (at < notes.size() && notes[at]->startQuarters < to) {
			const GpPlayedNote& n = *notes[at++];
			if (n.startQuarters < from)
				continue;
			const int pc = ((n.note.midi % 12) + 12) % 12;
			// FADED TO THE END OF THE BAR, over about a beat, which is what the module's own
			// decay does sample by sample.
			const double age = to - n.startQuarters;
			weight[pc] += gpHarmonyWeight(line, song, n) * (float) std::exp(-age);
			if (bassMidi < 0 || n.note.midi <= bassMidi || n.startQuarters - bassWhen > 1.0) {
				bassMidi = n.note.midi;
				bassWhen = n.startQuarters;
			}
		}

		// WHAT THE SET SAYS AT THE BAR'S END, as the module would publish it.
		float most = 0.f;
		for (int pc = 0; pc < 12; pc++)
			most = std::max(most, weight[pc]);

		std::string said;
		if (most > 0.01f) {
			std::pair<int, int> order[12];
			int found = 0;
			for (int pc = 0; pc < 12; pc++) {
				const int w = (int) std::lround(weight[pc] / most * 255.f);
				if (w > 0)
					order[found++] = std::make_pair(-w, pc);
			}
			std::sort(order, order + found);
			for (int k = 0; k < found && k < 5; k++) {
				// The same threshold the sprite module uses: a quarter of the strongest, with
				// at least three kept whatever they weigh.
				if (-order[k].first < 64 && k >= 3)
					break;
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%s(%d) ",
					PC_NAMES[order[k].second], -order[k].first);
				said += buf;
			}
		}

		const size_t written = (size_t) line.bars[b].written;
		std::string key = "  ";
		if (written < song.masterBars.size()) {
			const GpMasterBar& m = song.masterBars[written];
			if (m.keyAccidentals != keyWas) {
				keyWas = m.keyAccidentals;
				const int tonic = m.minor ? ((keyWas * 7 + 9) % 12 + 12) % 12
					: ((keyWas * 7) % 12 + 12) % 12;
				key = PC_NAMES[tonic];
				key += m.minor ? "m" : "";
			}
		}

		std::printf("%3d  %-7s  %-36s %s\n", (int) line.bars[b].written + 1, key.c_str(),
			said.c_str(), (bassMidi >= 0) ? PC_NAMES[((bassMidi % 12) + 12) % 12] : "-");

		// Carry the set into the next bar, faded by a bar's worth.
		for (int pc = 0; pc < 12; pc++)
			weight[pc] *= 0.25f;
	}
	return 0;
}
