/** The SoundFont engine, from a command line.

WHY THERE IS A PROGRAM FOR THIS. Everything mpxFluidSynth does to make a sound happens here as well,
and here it can be looked at: the bank is loaded, a part is given a sound, notes are played on a
channel per string, a bend is drawn through one of them, and the result is written as a WAV that
can be opened and measured. A fault found here is a line and a number; the same fault found by
patching is a module that makes no sound and says nothing.

    make soundtest ARGS="path/to/bank.sf2"
*/
#include "../src/FluidEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace px;


static const double RATE = 48000.0;


/** A plain 16-bit stereo WAV, so anything can open it. */
static void writeWav(const std::string& path, const std::vector<float>& left,
		const std::vector<float>& right) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const int frames = (int) left.size();
	const int bytes = frames * 4;
	const int rate = (int) RATE;
	auto u32 = [&](unsigned int v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32((unsigned int) (36 + bytes));
	std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2);
	u32((unsigned int) rate); u32((unsigned int) (rate * 4)); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32((unsigned int) bytes);
	for (int i = 0; i < frames; i++) {
		const float l = (left[(size_t) i] > 1.f) ? 1.f : (left[(size_t) i] < -1.f ? -1.f : left[(size_t) i]);
		const float r = (right[(size_t) i] > 1.f) ? 1.f : (right[(size_t) i] < -1.f ? -1.f : right[(size_t) i]);
		u16((unsigned short) (short) (l * 32000.f));
		u16((unsigned short) (short) (r * 32000.f));
	}
	std::fclose(f);
}


/** The pitch of a stretch of sound, by autocorrelation. Rough, and enough to tell whether a bend
went where it was asked to go. */
static double pitchOf(const std::vector<float>& x, size_t from, size_t count) {
	double best = 0.0;
	size_t bestLag = 0;
	for (size_t lag = (size_t) (RATE / 600); lag < (size_t) (RATE / 60); lag++) {
		double sum = 0.0;
		for (size_t i = from; i + lag < from + count; i += 3)
			sum += (double) x[i] * x[i + lag];
		if (sum > best) {
			best = sum;
			bestLag = lag;
		}
	}
	return bestLag ? RATE / (double) bestLag : 0.0;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: soundtest BANK.sf2\n");
		return 1;
	}

	FluidEngine engine;
	if (!engine.start(RATE, 64, 256)) {
		std::printf("NO: %s\n", engine.reason().c_str());
		return 1;
	}
	if (!engine.loadBank(argv[1])) {
		std::printf("NO: %s\n", engine.reason().c_str());
		return 1;
	}
	std::printf("%s: %d sounds\n", engine.bankName().c_str(), (int) engine.presets().size());

	// What a band would be given. Named rather than numbered, so it is obvious when a bank does
	// not hold what was asked for.
	const int WANT[][2] = {{0, 29}, {0, 33}, {128, 0}};
	for (size_t i = 0; i < sizeof(WANT) / sizeof(WANT[0]); i++) {
		std::string name = "(not in this bank)";
		for (size_t k = 0; k < engine.presets().size(); k++) {
			if (engine.presets()[k].bank == WANT[i][0]
					&& engine.presets()[k].program == WANT[i][1])
				name = engine.presets()[k].name;
		}
		std::printf("   bank %3d program %3d  %s\n", WANT[i][0], WANT[i][1], name.c_str());
	}

	// A CHANNEL PER STRING, which is the whole point: six channels on one sound, a chord across
	// four of them, and a whole-tone bend on one alone.
	for (int c = 0; c < 6; c++) {
		engine.program(c, 0, 29);
		engine.bendRange(c, 12);
	}
	const int CHORD[4] = {40, 47, 52, 56};       // E, B, E, G sharp: an E major shape.

	std::vector<float> left, right;
	float l[64], r[64];
	const int blocks = (int) (RATE * 2.0 / 64);
	for (int b = 0; b < blocks; b++) {
		const double t = b * 64.0 / RATE;
		if (b == 0) {
			for (int i = 0; i < 4; i++)
				engine.noteOn(i, CHORD[i], 100);
		}
		// The bend on the fourth string only, a whole tone over half a second from one second in.
		if (t >= 1.0 && t < 1.5)
			engine.bend(3, (float) ((t - 1.0) / 0.5 * 200.0));
		std::memset(l, 0, sizeof(l));
		std::memset(r, 0, sizeof(r));
		engine.render(l, r, 64);
		for (int i = 0; i < 64; i++) {
			left.push_back(l[i]);
			right.push_back(r[i]);
		}
	}

	double peak = 0.0;
	for (size_t i = 0; i < left.size(); i++)
		peak = (std::fabs(left[i]) > peak) ? std::fabs(left[i]) : peak;
	std::printf("rendered %.2f seconds, peak %.4f\n", (double) left.size() / RATE, peak);

	// The bent string on its own, to see where it went. Played alone after the chord, so the
	// reading is of one note rather than of four.
	engine.allOff();
	for (int c = 0; c < 6; c++) {
		engine.program(c, 0, 29);
		engine.bendRange(c, 12);
	}
	std::vector<float> one, oneR;
	engine.noteOn(3, 52, 100);                   // E three, 164.8 Hz.
	for (int b = 0; b < blocks; b++) {
		const double t = b * 64.0 / RATE;
		if (t >= 0.3 && t < 0.8)
			engine.bend(3, (float) ((t - 0.3) / 0.5 * 200.0));
		else if (t >= 0.8)
			engine.bend(3, 200.f);
		std::memset(l, 0, sizeof(l));
		std::memset(r, 0, sizeof(r));
		engine.render(l, r, 64);
		for (int i = 0; i < 64; i++) {
			one.push_back(l[i] + r[i]);
			oneR.push_back(r[i]);
		}
	}
	const double before = pitchOf(one, (size_t) (RATE * 0.1), (size_t) (RATE * 0.1));
	const double after = pitchOf(one, (size_t) (RATE * 1.2), (size_t) (RATE * 0.1));
	std::printf("one string: %.1f Hz, then %.1f Hz after a whole-tone bend "
		"(164.8 and 185.0 are the notes)\n", before, after);

	writeWav("build/soundtest.wav", left, right);
	std::printf("wrote build/soundtest.wav\n");
	return 0;
}
