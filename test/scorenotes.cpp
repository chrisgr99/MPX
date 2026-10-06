/** WHAT A SCORE SENDS DOWN THE CABLE, as plain text.

	make scorenotes ARGS="a-piece.mxl 20" > notes.txt

One line per note: when it starts, which key, how hard, how long. The same readers the modules use,
so these are the notes mpxScore would actually send — including the LENGTH, which is the thing it
sends instead of a note-off and which neither piano read until it was found by ear.

WHAT IT IS FOR. test/salamander-release.py plays these through the sampled piano's release engine
outside Rack, which is the only way to check a claim about its damping without a round trip through
somebody listening. Three successive changes to that damping were reported as working when they
were not, and none of them was visible to the compiler. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: scorenotes <file> [seconds]\n");
		return 1;
	}
	const double seconds = (argc > 2) ? std::atof(argv[2]) : 20.0;

	std::ifstream file(argv[1], std::ios::binary);
	if (!file) {
		std::fprintf(stderr, "could not open %s\n", argv[1]);
		return 1;
	}
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
	if (!ok) {
		std::fprintf(stderr, "could not read it: %s\n", why.c_str());
		return 1;
	}

	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) {
		std::fprintf(stderr, "could not lay it out in time: %s\n", why.c_str());
		return 1;
	}

	// EVERY PART TOGETHER, in time order: a piano piece is one instrument however many staves it
	// is written on.
	struct Hit {
		double at, lasts;
		int midi;
		float level;
	};
	std::vector<Hit> hits;
	for (size_t t = 0; t < line.tracks.size(); t++) {
		for (const GpPlayedNote& n : line.tracks[t]) {
			if (n.startSeconds > seconds)
				break;
			// A TIED NOTE CONTINUES THE ONE BEFORE IT rather than being struck again, which is the
			// difference between a held minim and two crotchets hammered twice.
			if (n.note.tied)
				continue;
			hits.push_back(Hit{n.startSeconds, (n.lengthSeconds > 0.05) ? n.lengthSeconds : 0.05,
				n.note.midi, n.dynamic});
		}
	}
	std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });

	std::fprintf(stderr, "%s: %d notes in the first %.0f seconds of %.0f\n",
		song.title.empty() ? argv[1] : song.title.c_str(), (int) hits.size(), seconds,
		line.seconds);
	for (const Hit& h : hits)
		std::printf("%.6f %d %.4f %.6f\n", h.at, h.midi, h.level, h.lasts);
	return 0;
}
