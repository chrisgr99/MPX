/** mpxGuitarVoice's envelopes and mix, from a command line.

WHY THERE IS A PROGRAM FOR THIS. An envelope that is a little too slow or never quite finishes is
heard as a vague wrongness and nothing more; measured, it is a number that is not the one asked
for. Each check here plays a string through GuitarVoices with a steady one-volt return and
measures what comes out.

    make voicetest
*/
#include "../src/GuitarVoice.hpp"

#include <cmath>
#include <cstdio>

using namespace px;


static const float RATE = 48000.f;
static const float DT = 1.f / RATE;
static int failures = 0;


static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-58s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}


/** The level of one string, read off the mix: the left side of a string panned hard left. */
static float run(GuitarVoices& g, int frames) {
	const float in = 1.f;
	float l = 0.f, r = 0.f;
	for (int i = 0; i < frames; i++)
		g.process(&in, 1, DT, &l, &r);
	return l;
}


/** Frames until the string's level falls below `target`, or -1. */
static int framesUntilBelow(GuitarVoices& g, float target, int limit) {
	const float in = 1.f;
	for (int i = 0; i < limit; i++) {
		float l = 0.f, r = 0.f;
		g.process(&in, 1, DT, &l, &r);
		if (l < target)
			return i;
	}
	return -1;
}


static GuitarVoices fresh(float attack, float decay, float release) {
	GuitarVoices g;
	g.times(attack, decay, release);
	g.pan[0] = -1.f;
	// No filter envelope unless a check asks for one: the rest measure the filter standing still.
	g.filter.envelopeDepth = 0.f;
	return g;
}


int main() {
	// ATTACK: full level at the end of it, not before.
	{
		GuitarVoices g = fresh(0.01f, 100.f, 0.1f);
		g.strike(0, 1.f);
		const float half = run(g, (int) (0.005f * RATE));
		check(std::fabs(half - 0.5f) < 0.01f, "attack: half way at half the attack time", half);
		const float full = run(g, (int) (0.005f * RATE) + 1);
		check(full > 0.99f, "attack: full at the end of the attack time", full);
	}

	// DECAY: sixty decibels down after the decay time, while the note is held.
	{
		GuitarVoices g = fresh(0.f, 1.f, 0.1f);
		g.strike(0, 1.f);
		run(g, 1);
		const float at = run(g, (int) RATE);
		check(std::fabs(20.f * std::log10(at) + 60.f) < 0.5f,
			"decay: -60 dB one decay time after the strike (dB)", 20.f * std::log10(at));
	}

	// RELEASE: sixty decibels down after the release time, from the level the note had.
	{
		GuitarVoices g = fresh(0.f, 100.f, 0.2f);
		g.strike(0, 1.f);
		const float before = run(g, 100);
		g.end(0);
		const float after = run(g, (int) (0.2f * RATE));
		check(std::fabs(20.f * std::log10(after / before) + 60.f) < 0.5f,
			"release: -60 dB one release time after the end (dB)",
			20.f * std::log10(after / before));
		const int gone = framesUntilBelow(g, 1e-6f, (int) RATE);
		check(gone >= 0, "release: the envelope finishes", gone);
	}

	// LEGATO: no strike, so a hammer-on carries on from where the string was.
	{
		GuitarVoices g = fresh(0.05f, 2.f, 0.1f);
		g.strike(0, 1.f);
		run(g, (int) (0.5f * RATE));
		const float before = run(g, 1);
		g.setLevel(0, 0.8f);                 // what a hammer-on does to the level
		const float after = run(g, 1);
		check(std::fabs(after - before * 0.8f) < 0.01f,
			"legato: a new level without a new attack", after);
	}

	// RESTRIKE: from where it was, so striking a ringing string does not click.
	{
		GuitarVoices g = fresh(0.01f, 2.f, 0.1f);
		g.strike(0, 1.f);
		const float before = run(g, (int) (0.3f * RATE));
		g.strike(0, 1.f);
		const float after = run(g, 1);
		check(after >= before && after - before < 0.01f,
			"restrike: rises from the level the string had", after - before);
	}

	// LEVEL: the strike's level scales the string.
	{
		GuitarVoices g = fresh(0.f, 100.f, 0.1f);
		g.strike(0, 0.25f);
		const float v = run(g, 10);
		check(std::fabs(v - 0.25f) < 0.01f, "level: a string struck at a quarter", v);
	}

	// PAN: equal power, so a centred string is 0.707 on each side.
	{
		GuitarVoices g = fresh(0.f, 100.f, 0.1f);
		g.pan[0] = 0.f;
		g.strike(0, 1.f);
		const float in = 1.f;
		float l = 0.f, r = 0.f;
		for (int i = 0; i < 10; i++)
			g.process(&in, 1, DT, &l, &r);
		check(std::fabs(l - 0.7071f) < 0.01f && std::fabs(r - 0.7071f) < 0.01f,
			"pan: a centred string at 0.707 on each side", l);
	}

	// CHANNELS: a polyphonic return gives each string its own channel.
	{
		GuitarVoices g = fresh(0.f, 100.f, 0.1f);
		g.pan[0] = -1.f;
		g.pan[1] = 1.f;
		g.strike(0, 1.f);
		g.strike(1, 1.f);
		const float in[2] = {0.3f, 0.6f};
		float l = 0.f, r = 0.f;
		for (int i = 0; i < 10; i++)
			g.process(in, 2, DT, &l, &r);
		check(std::fabs(l - 0.3f) < 0.01f && std::fabs(r - 0.6f) < 0.01f,
			"channels: string one's audio left, string two's right", r);
	}

	// ---- the filter ----

	// KEY TRACKING: an octave of pitch is an octave of cutoff at one, nothing at nought.
	{
		GuitarVoices g;
		g.filter.cutoff = 1000.f;
		g.pitch[0] = 1.f;
		g.filter.keyTracking = 1.f;
		check(std::fabs(g.cutoffOf(0) - 2000.f) < 1.f, "key tracking: a volt up doubles the cutoff",
			g.cutoffOf(0));
		g.filter.keyTracking = 0.f;
		check(std::fabs(g.cutoffOf(0) - 1000.f) < 1.f, "key tracking: off leaves it where it is",
			g.cutoffOf(0));
	}

	// TIMBRE: a palm mute's brightness is an octave and a half darker than an ordinary note.
	{
		GuitarVoices g;
		g.filter.cutoff = 1000.f;
		g.timbre[0] = 0.25f;
		const double octaves = std::log2(g.cutoffOf(0) / 1000.f);
		check(std::fabs(octaves + 1.4) < 0.01, "timbre: a palm mute moves the cutoff (octaves)",
			octaves);
	}

	// SLOPE: two octaves above the cutoff, 12 dB an octave is about 24 dB down, 24 about 48.
	for (int steep = 0; steep < 2; steep++) {
		const float cutoff = 500.f, tone = 2000.f;
		double rms[2] = {};
		for (int pass = 0; pass < 2; pass++) {
			GuitarVoices g = fresh(0.f, 1000.f, 0.1f);
			g.filter.cutoff = pass ? cutoff : 20000.f;
			g.filter.keyTracking = 0.f;
			g.filter.steep = steep != 0;
			g.strike(0, 1.f);
			double sum = 0.0;
			int n = 0;
			for (int i = 0; i < (int) RATE; i++) {
				const float in = std::sin(2.f * 3.14159265f * tone * i / RATE);
				float l = 0.f, r = 0.f;
				g.process(&in, 1, DT, &l, &r);
				if (i > (int) (RATE / 2)) {
					sum += (double) l * l;
					n++;
				}
			}
			rms[pass] = std::sqrt(sum / n);
		}
		const double db = 20.0 * std::log10(rms[1] / rms[0]);
		const double want = steep ? -48.0 : -24.0;
		check(std::fabs(db - want) < 3.0,
			steep ? "slope 24: two octaves above the cutoff (dB)"
				: "slope 12: two octaves above the cutoff (dB)", db);
	}

	// RESONANCE: a peak at the cutoff, and still stable.
	{
		const float cutoff = 1000.f;
		double rms[2] = {};
		for (int pass = 0; pass < 2; pass++) {
			GuitarVoices g = fresh(0.f, 1000.f, 0.1f);
			g.filter.cutoff = cutoff;
			g.filter.keyTracking = 0.f;
			g.filter.resonance = pass ? 1.f : 0.f;
			g.strike(0, 1.f);
			double sum = 0.0;
			int n = 0;
			for (int i = 0; i < (int) RATE; i++) {
				const float in = std::sin(2.f * 3.14159265f * cutoff * i / RATE);
				float l = 0.f, r = 0.f;
				g.process(&in, 1, DT, &l, &r);
				if (i > (int) (RATE / 2)) {
					sum += (double) l * l;
					n++;
				}
			}
			rms[pass] = std::sqrt(sum / n);
		}
		const double db = 20.0 * std::log10(rms[1] / rms[0]);
		check(db > 20.0 && std::isfinite(db), "resonance: full is a peak at the cutoff (dB)", db);
	}

	// FILTER ENVELOPE: the depth at its peak, back to the cutoff after its decay, and not
	// struck again by a hammer-on.
	{
		GuitarVoices g = fresh(0.f, 100.f, 0.1f);
		g.filter.cutoff = 1000.f;
		g.filter.keyTracking = 0.f;
		g.filter.envelopeDepth = 2.f;
		g.filterTimes(0.01f, 0.5f);
		g.strike(0, 1.f);
		run(g, (int) (0.01f * RATE) + 1);
		const double peak = std::log2(g.cutoffOf(0) / 1000.f);
		check(std::fabs(peak - 2.0) < 0.02, "filter envelope: depth at the peak (octaves)", peak);
		run(g, (int) (0.5f * RATE));
		const double after = std::log2(g.cutoffOf(0) / 1000.f);
		check(after < 0.01, "filter envelope: back down after its decay (octaves)", after);
		g.setLevel(0, 0.8f);
		run(g, 10);
		const double legato = std::log2(g.cutoffOf(0) / 1000.f);
		check(legato < 0.01, "filter envelope: a hammer-on does not strike it", legato);
	}

	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
