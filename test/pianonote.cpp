/** ONE NOTE OF THE MODELLED PIANO TO A FILE, so it can be set beside the recording of the same
note on a real one.

	make pianonote ARGS="60 0.6 4 build/c4.wav"      key, level, seconds, where

WHY A TOOL FOR ONE NOTE. A render of a piece says whether the instrument is pleasant; a single note
beside a single recording says in what way it is not, and a note at a time is the only way to find
out whether a fault belongs to the bass, the treble, the attack or the decay. */
#include "../src/PianoKeys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace px;
static const double SR = 48000.0;
static const double OUT_SCALE = 100.0;

int main(int argc, char** argv) {
	if (argc < 5) {
		std::printf("usage: pianonote <midi> <level 0..1> <seconds> <out.wav>\n");
		return 1;
	}
	const int midi = std::atoi(argv[1]);
	const double level = std::atof(argv[2]);
	const double seconds = std::atof(argv[3]);
	const std::string out = argv[4];

	PianoBoard board;
	board.setSampleRate((float) SR);
	board.silence();
	PianoKeys keys;
	keys.setSampleRate((float) SR);
	// THE KEY STAYS DOWN, as it does when a sample is recorded: a recording of a note is a note
	// allowed to ring, not a note released.
	keys.down(board, 1, (float) (midi - 60) / 12.f, (float) level, 0.f);

	std::vector<float> x((size_t) (seconds * SR));
	double peak = 0.0;
	for (size_t i = 0; i < x.size(); i++) {
		keys.step(board);
		const double v = (double) board.process() * OUT_SCALE;
		x[i] = (float) v;
		peak = std::max(peak, std::fabs(v));
	}

	FILE* f = std::fopen(out.c_str(), "wb");
	if (!f) {
		std::printf("could not write %s\n", out.c_str());
		return 1;
	}
	const uint32_t frames = (uint32_t) x.size(), dataBytes = frames * 2u;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + dataBytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32((uint32_t) SR); u32((uint32_t) SR * 2u); u16(2); u16(16);
	std::fwrite("data", 1, 4, f);
	u32(dataBytes);
	// NOT NORMALISED. The level is the model's own, because whether it is loud enough is one of
	// the things being asked.
	for (float v : x) {
		const double c = (v > 10.f) ? 10.f : ((v < -10.f) ? -10.f : v);
		u16((uint16_t) (int16_t) std::lround(c * 3276.7));
	}
	std::fclose(f);
	std::printf("%s: midi %d at %.2f, peak %.3f V\n", out.c_str(), midi, level, peak);
	return 0;
}
