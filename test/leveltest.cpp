/** The built-in band's level against mpxGuitar's, from a command line.

WHY THERE IS A PROGRAM FOR THIS. Two modules can play the same Guitar Chart part side by side: the
band built into mpxGuitarChart6, through FluidSynth, and mpxGuitar, through its modelled strings.
They were twenty decibels apart, and nothing but a listener would have said so. This plays the
same note through each, scaled as each module scales it, and holds them within a few decibels.

    make leveltest ARGS="path/to/bank.sf2"
*/
#include "../src/FluidEngine.hpp"
#include "../src/GuitarModel.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace px;


/** AS SoundCore.hpp AND mpxGuitar.cpp HAVE THEM, kept here by hand since those files need Rack.
Change them together: a scale changed there and not here leaves this measuring the old one. */
static const float SOUND_VOLTS = 65.f;
static const float GUITAR_VOLTS = 4.f;
static const float RATE = 48000.f;


static double peakOf(const std::vector<float>& v) {
	double peak = 0.0;
	for (float x : v)
		peak = std::fmax(peak, std::fabs(x));
	return peak;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("give the path of a SoundFont\n");
		return 1;
	}
	const float level = 0.7f;
	int failures = 0;
	for (int nylon = 1; nylon >= 0; nylon--) {
		FluidEngine e;
		if (!e.start(RATE, 32, 256, 2) || !e.loadBank(argv[1])) {
			std::printf("the bank could not be read\n");
			return 1;
		}
		e.effects(false, false);
		e.program(0, 0, nylon ? 24 : 25);
		e.noteOn(0, 57, (int) std::lround(level * 127.f));
		static float b[4][64], fx[4][64];
		float* out[4] = {b[0], b[1], b[2], b[3]};
		float* f[4] = {fx[0], fx[1], fx[2], fx[3]};
		std::vector<float> band;
		for (int k = 0; k < 750; k++) {
			e.renderGroups(out, f, 64);
			for (int i = 0; i < 64; i++)
				band.push_back((b[0][i] + b[2][i]) * SOUND_VOLTS);
		}

		GuitarModel g;
		g.setSampleRate(RATE);
		const int tuning[6] = {64, 59, 55, 50, 45, 40};
		for (int s = 0; s < 6; s++) {
			g.open[s] = (tuning[s] - 60) / 12.f;
			g.openKnown[s] = true;
			g.setPitch(s, g.open[s]);
		}
		g.settings.nylon = nylon != 0;
		g.settings.sympathy = 0.f;
		g.setPitch(2, (57 - 60) / 12.f);
		g.strike(2, level, 0, 2);
		std::vector<float> guitar;
		for (int i = 0; i < (int) RATE; i++) {
			float l = 0.f, r = 0.f;
			g.process(&l, &r);
			guitar.push_back(l * GUITAR_VOLTS);
		}

		const double d = 20.0 * std::log10(peakOf(band) / peakOf(guitar));
		const bool ok = std::fabs(d) < 4.0;
		std::printf("%s  %s: the band's note against mpxGuitar's, at their peaks (dB)  %g\n",
			ok ? "ok  " : "FAIL", nylon ? "nylon" : "steel", d);
		if (!ok)
			failures++;
	}
	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
