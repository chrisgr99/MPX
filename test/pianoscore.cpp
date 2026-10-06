/** PLAYS A REAL SCORE THROUGH THE MODELLED PIANO AND WRITES IT TO A FILE.

	make pianoscore ARGS="~/Downloads/some-piece.mxl"
	make pianoscore ARGS="a-piece.mxl 20 build/first-twenty-seconds.wav"

WHY. Short made-up pieces show whether a mechanism works; real music shows whether an instrument
is worth playing, and it shows it far faster. A bass that is too short or a note that will not stop
is obvious in two bars of Chopin and invisible in a measurement.

AND IT PLAYS WHAT THE MODULE PLAYS. The same reader the module uses, the same timeline, the same
keys — mpxScore turns a score into notes with `PianoKeys`-shaped information and so does this, so a
fault heard here is a fault there. The one thing it does not share is the module's own panel: there
is no volume knob, so the level is the module's default.

ALL PARTS AT ONCE, because a piano piece is one instrument however many staves it is written on.
A score with other instruments in it will have them played on the piano too, which for a quartet
is not what anybody wants but is at least honest about what it is doing. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"
#include "../src/PianoKeys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

static const double SR = 48000.0;
/** The module's own scaling: the board's surface velocity in metres per second to volts. */
static const double OUT_SCALE = 100.0;


static bool writeWav(const std::string& path, const std::vector<float>& x) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	const uint32_t frames = (uint32_t) x.size();
	const uint32_t dataBytes = frames * 2u * 2u;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + dataBytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);
	u16(2);
	u32((uint32_t) SR);
	u32((uint32_t) SR * 2u * 2u);
	u16(4);
	u16(16);
	std::fwrite("data", 1, 4, f);
	u32(dataBytes);
	for (float v : x) {
		const double c = (v > 10.f) ? 10.f : ((v < -10.f) ? -10.f : v);
		const int16_t s = (int16_t) std::lround(c * 3276.7);
		u16((uint16_t) s);
		u16((uint16_t) s);
	}
	std::fclose(f);
	return true;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: pianoscore <file> [seconds] [out.wav]\n");
		std::printf("       pianoscore <file> [seconds] --notes   writes the notes as text, for\n");
		std::printf("                                             a reference renderer to play\n");
		return 1;
	}
	const double seconds = (argc > 2) ? std::atof(argv[2]) : 20.0;
	const std::string out = (argc > 3) ? argv[3] : "build/score.wav";

	std::ifstream file(argv[1], std::ios::binary);
	if (!file) {
		std::printf("could not open %s\n", argv[1]);
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
		std::printf("could not read it: %s\n", why.c_str());
		return 1;
	}

	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) {
		std::printf("could not lay it out in time: %s\n", why.c_str());
		return 1;
	}

	std::printf("%s", song.title.empty() ? "(no title)" : song.title.c_str());
	if (!song.artist.empty())
		std::printf(" — %s", song.artist.c_str());
	std::printf("\n   %d bars, %d parts, %.0f seconds in all; playing the first %.0f\n",
		(int) song.masterBars.size(), (int) song.tracks.size(), line.seconds, seconds);

	// Every part's notes together, in time order: a piano piece is one instrument.
	struct Hit {
		double at;
		double lasts;
		int midi;
		float level;
	};
	std::vector<Hit> hits;
	for (size_t t = 0; t < line.tracks.size(); t++) {
		for (const GpPlayedNote& n : line.tracks[t]) {
			if (n.startSeconds > seconds)
				break;
			// A TIED NOTE CONTINUES THE ONE BEFORE IT rather than being struck again, which is
			// the difference between a held minim and two crotchets hammered twice.
			if (n.note.tied)
				continue;
			hits.push_back(Hit{n.startSeconds, std::max(0.05, n.lengthSeconds), n.note.midi,
				n.dynamic});
		}
	}
	std::sort(hits.begin(), hits.end(),
		[](const Hit& a, const Hit& b) { return a.at < b.at; });
	if (hits.empty()) {
		std::printf("   nothing to play in that stretch\n");
		return 1;
	}

	// THE NOTES AS TEXT, so that something else can play the same ones. The point of a reference
	// render is that it differs from ours only in the instrument.
	if (out == "--notes") {
		for (const Hit& h : hits)
			std::printf("%.6f %d %.4f %.6f\n", h.at, h.midi, h.level, h.lasts);
		return 0;
	}

	PianoBoard board;
	board.setSampleRate((float) SR);
	board.silence();
	PianoKeys keys;
	keys.setSampleRate((float) SR);

	// A TAIL, so the last notes are allowed to finish rather than being cut off mid-decay.
	std::vector<float> wave((size_t) ((seconds + 4.0) * SR));
	size_t next = 0;
	int64_t tag = 1;
	double peak = 0.0;
	int lowest = 127, highest = 0;
	for (size_t i = 0; i < wave.size(); i++) {
		const double now = (double) i / SR;
		keys.step(board);
		while (next < hits.size() && hits[next].at <= now) {
			const Hit& h = hits[next++];
			if (h.midi < 21 || h.midi > 108)
				continue;
			lowest = std::min(lowest, h.midi);
			highest = std::max(highest, h.midi);
			keys.down(board, tag++, (float) (h.midi - 60) / 12.f, h.level, (float) h.lasts);
		}
		const double v = (double) board.process() * OUT_SCALE;
		wave[i] = (float) v;
		peak = std::max(peak, std::fabs(v));
	}

	double lastSecond = 0.0;
	for (size_t i = wave.size() - (size_t) SR; i < wave.size(); i++)
		lastSecond = std::max(lastSecond, (double) std::fabs(wave[i]));

	std::printf("   %d notes, from MIDI %d to %d; peak %.2f V, last second %.0f dB below it,"
		" %d keys still down\n", (int) hits.size(), lowest, highest, peak,
		20.0 * std::log10(std::max(lastSecond, 1e-12) / std::max(peak, 1e-12)), keys.holding());
	if (!writeWav(out, wave)) {
		std::printf("   could not write %s\n", out.c_str());
		return 1;
	}
	std::printf("   wrote %s\n", out.c_str());
	return 0;
}
