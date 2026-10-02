/** One modelled guitar string, from a command line.

WHY THERE IS A PROGRAM FOR THIS. A string that is a few cents flat, rings a little short or puts
its pick in the wrong place is heard as "not quite a guitar" and nothing more specific. Measured,
each of those is a number against the one asked for. So every check plucks a GuitarString and
measures what comes out: the pitch by autocorrelation, the level of a single partial with a
Goertzel filter. Then the same string plays a few things into build/, to be listened to.

    make stringtest
*/
#include "../src/GuitarString.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


static const float RATE = 48000.f;
static int failures = 0;


static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-62s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}


static GuitarString string(float hz, float decay = 3.f, float damping = 0.3f) {
	GuitarString s;
	s.setSampleRate(RATE);
	s.setFrequency(hz);
	s.setDecay(decay);
	s.setDamping(damping);
	return s;
}


static std::vector<float> play(GuitarString& s, float seconds) {
	std::vector<float> out((size_t) (seconds * RATE));
	for (float& x : out)
		x = s.process();
	return out;
}


/** The pitch over a stretch of the sound, from the lag at which it best matches itself, with
the peak found between samples by a parabola. Searched within a fifth of the expected period. */
static double pitchOf(const std::vector<float>& x, double from, double to, double expect) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	const double period = RATE / expect;
	const int lo = (int) (period * 0.8), hi = (int) (period * 1.2) + 2;
	std::vector<double> r(hi + 2, 0.0);
	for (int lag = lo - 1; lag <= hi + 1; lag++) {
		double sum = 0.0;
		for (size_t i = a; i + lag < b; i++)
			sum += (double) x[i] * x[i + lag];
		r[lag] = sum;
	}
	int best = lo;
	for (int lag = lo; lag <= hi; lag++)
		if (r[lag] > r[best])
			best = lag;
	const double y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
	const double shift = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
	return RATE / (best + shift);
}


/** How strong one frequency is over a stretch, by a Goertzel filter. */
static double levelAt(const std::vector<float>& x, double from, double to, double hz) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	const double w = 2.0 * M_PI * hz / RATE, c = 2.0 * std::cos(w);
	double s1 = 0.0, s2 = 0.0;
	for (size_t i = a; i < b; i++) {
		const double s = x[i] + c * s1 - s2;
		s2 = s1;
		s1 = s;
	}
	const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
	return std::sqrt(std::fmax(power, 0.0)) / (double) (b - a);
}


static double db(double ratio) {
	return 20.0 * std::log10(std::fmax(ratio, 1e-12));
}


static double cents(double got, double want) {
	return 1200.0 * std::log2(got / want);
}


static void writeWav(const std::string& path, const std::vector<float>& left,
		const std::vector<float>& right) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t frames = (uint32_t) left.size(), bytes = frames * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32((uint32_t) RATE); u32((uint32_t) RATE * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (uint32_t i = 0; i < frames; i++) {
		const int16_t l = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, left[i])) * 32000.f);
		const int16_t r = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, right[i])) * 32000.f);
		std::fwrite(&l, 2, 1, f);
		std::fwrite(&r, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}


int main() {
	// TUNING: the six open strings of a guitar and a high fretted note, each within a cent.
	const double notes[] = {82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26, 1318.51};
	for (double hz : notes) {
		GuitarString s = string((float) hz);
		s.pluck(1.f, 0.7f, 0.2f);
		const std::vector<float> x = play(s, 1.f);
		const double c = cents(pitchOf(x, 0.3, 0.8, hz), hz);
		char what[96];
		std::snprintf(what, sizeof what, "tuning: %.2f Hz (cents)", hz);
		check(std::fabs(c) < 1.0, what, c);
	}

	// DECAY: the fundamental falls by 60 dB over the decay time, at the bottom and the top of
	// the neck alike. Measured as the fall over one second, which a three-second decay makes
	// twenty decibels.
	for (double hz : {82.41, 659.26}) {
		GuitarString s = string((float) hz, 3.f);
		s.pluck(1.f, 0.7f, 0.2f);
		const std::vector<float> x = play(s, 2.f);
		const double span = 8.0 / hz;
		const double fall = db(levelAt(x, 1.5, 1.5 + span, hz) / levelAt(x, 0.5, 0.5 + span, hz));
		char what[96];
		std::snprintf(what, sizeof what, "decay: %.0f Hz falls 20 dB a second (dB)", hz);
		check(std::fabs(fall + 20.0) < 1.5, what, fall);
	}

	// DAMPING: the upper partials die faster than the fundamental, and faster still with more.
	{
		double drop[2];
		for (int i = 0; i < 2; i++) {
			GuitarString s = string(110.f, 3.f, i ? 0.9f : 0.1f);
			s.pluck(1.f, 1.f, 0.13f);
			const std::vector<float> x = play(s, 1.f);
			drop[i] = db(levelAt(x, 0.6, 0.8, 1100.0) / levelAt(x, 0.02, 0.22, 1100.0));
		}
		check(drop[1] < drop[0] - 10.0, "damping: the tenth partial dies faster with more (dB)",
			drop[1] - drop[0]);
	}

	// PICK POSITION: plucked in the middle, a string has no even partials; near the bridge,
	// it has.
	{
		double second[2];
		for (int i = 0; i < 2; i++) {
			GuitarString s = string(110.f);
			s.pluck(1.f, 1.f, i ? 0.5f : 0.1f);
			const std::vector<float> x = play(s, 0.5f);
			second[i] = db(levelAt(x, 0.05, 0.25, 220.0) / levelAt(x, 0.05, 0.25, 110.0));
		}
		check(second[1] < -25.0, "pick: in the middle, the second partial is gone (dB)",
			second[1]);
		check(second[0] > -15.0, "pick: near the bridge, the second partial is there (dB)",
			second[0]);
	}

	// HARDNESS: a plectrum puts far more into the upper partials than a fingertip.
	{
		double top[2];
		for (int i = 0; i < 2; i++) {
			GuitarString s = string(110.f);
			s.pluck(1.f, i ? 1.f : 0.1f, 0.13f);
			const std::vector<float> x = play(s, 0.5f);
			top[i] = db(levelAt(x, 0.02, 0.12, 1210.0) / levelAt(x, 0.02, 0.12, 110.0));
		}
		check(top[1] > top[0] + 10.0, "hardness: the eleventh partial, plectrum over finger (dB)",
			top[1] - top[0]);
	}

	// THE START OF A NOTE: from silence smoothly, a fingertip's pluck with no jump bigger than a
	// small part of its own height — a step at the start is a click with every frequency in it.
	{
		GuitarString s = string(196.f, 5.f, 0.6f);
		s.pluck(0.7f, 0.2f, 0.24f);
		const std::vector<float> x = play(s, 0.1f);
		float peak = 0.f, jump = 0.f;
		for (size_t i = 1; i < x.size(); i++) {
			peak = std::fmax(peak, std::fabs(x[i]));
			jump = std::fmax(jump, std::fabs(x[i] - x[i - 1]));
		}
		// Begun at its corner it jumped 0.68 of its height in one sample; from its zero crossing
		// what is left is the waveform's own slope.
		check(jump / peak < 0.1, "start: a fingertip's pluck, largest jump against its peak",
			jump / peak);
	}

	// LEVEL: half as hard is half as loud.
	{
		double rms[2];
		for (int i = 0; i < 2; i++) {
			GuitarString s = string(110.f);
			s.pluck(i ? 0.5f : 1.f, 0.7f, 0.2f);
			const std::vector<float> x = play(s, 0.5f);
			rms[i] = levelAt(x, 0.1, 0.4, 110.0);
		}
		check(std::fabs(db(rms[1] / rms[0]) + 6.02) < 0.5, "level: half as hard (dB)",
			db(rms[1] / rms[0]));
	}

	// BEND: a whole tone up while the string rings, and in tune when it gets there.
	{
		GuitarString s = string(220.f);
		s.pluck(1.f, 0.7f, 0.2f);
		std::vector<float> x = play(s, 0.5f);
		s.setFrequency(220.f * std::pow(2.f, 2.f / 12.f));
		const std::vector<float> y = play(s, 1.f);
		x.insert(x.end(), y.begin(), y.end());
		const double want = 220.0 * std::pow(2.0, 2.0 / 12.0);
		const double c = cents(pitchOf(x, 0.8, 1.3, want), want);
		check(std::fabs(c) < 2.0, "bend: a whole tone up, in tune (cents)", c);
	}

	// RESTRIKE AND STABILITY: struck again while ringing, nothing runs away, and ten seconds on
	// a three-second decay is near silence.
	{
		GuitarString s = string(82.41f);
		s.pluck(1.f, 1.f, 0.2f);
		play(s, 0.3f);
		s.pluck(1.f, 1.f, 0.2f);
		const std::vector<float> x = play(s, 10.f);
		float peak = 0.f;
		bool finite = true;
		for (float v : x) {
			peak = std::fmax(peak, std::fabs(v));
			finite = finite && std::isfinite(v);
		}
		check(finite && peak < 3.f, "restrike: struck again while ringing (peak)", peak);
		double tail = 0.0;
		for (size_t i = x.size() - 4800; i < x.size(); i++)
			tail = std::fmax(tail, std::fabs(x[i]));
		check(tail < 0.002, "stability: near silence ten seconds later (peak)", tail);
	}

	// A RESTRIKE AT ANY POINT OF THE STRING'S CYCLE sounds as a fresh pluck does: caught first,
	// the old vibration cannot cancel the new note.
	{
		auto level = [&](GuitarString& g, float seconds) {
			double sum = 0.0;
			const int n = (int) (seconds * RATE);
			for (int i = 0; i < n; i++) {
				const float x = g.process();
				sum += (double) x * x;
			}
			return std::sqrt(sum / n);
		};
		const float hz = 261.63f;
		GuitarString fresh = string(hz, 6.f);
		fresh.pluck(0.6f, 0.6f, 0.3f);
		const double plain = level(fresh, 0.03f);
		double low = 99.0, high = -99.0;
		for (int k = 0; k < 40; k++) {
			GuitarString g = string(hz, 6.f);
			g.pluck(0.6f, 0.6f, 0.3f);
			level(g, 0.24f + (float) k / 40.f / hz);
			g.pluck(0.6f, 0.6f, 0.3f);
			const double d = 20.0 * std::log10(level(g, 0.03f) / plain);
			low = std::fmin(low, d);
			high = std::fmax(high, d);
		}
		check(low > -2.0, "restrike: at its weakest point in the cycle, against a fresh pluck (dB)", low);
		check(high < 2.0, "restrike: at its strongest point in the cycle, against a fresh pluck (dB)", high);
	}


	// SMOOTH SLIDES: an octave's slide is as smooth as the note held before it. Measured as the
	// largest jump in the signal's slope, which a click is and a glide is not.
	{
		GuitarString s = string(110.f, 4.f);
		s.pluck(1.f, 0.7f, 0.2f);
		std::vector<float> x;
		for (int i = 0; i < (int) (1.f * RATE); i++) {
			const float t = i / RATE;
			const float semis = (t < 0.4f) ? 0.f : (t < 0.7f ? 12.f * (t - 0.4f) / 0.3f : 12.f);
			s.setFrequency(110.f * std::pow(2.f, semis / 12.f));
			x.push_back(s.process());
		}
		auto roughest = [&](double from, double to) {
			double worst = 0.0, level = 0.0;
			for (size_t i = (size_t) (from * RATE) + 2; i < (size_t) (to * RATE); i++) {
				worst = std::fmax(worst, std::fabs(x[i] - 2.f * x[i - 1] + x[i - 2]));
				level = std::fmax(level, std::fabs(x[i]));
			}
			return worst / level;
		};
		const double ratio = roughest(0.4, 0.7) / roughest(0.2, 0.4);
		check(ratio < 3.0, "slide: an octave, as smooth as the held note (ratio)", ratio);
	}

	// STIFFNESS: the eighth partial of the low E sits sharp of eight times the fundamental, and
	// the fundamental itself stays in tune.
	{
		auto partialCents = [&](float stiff, int n) {
			GuitarString s = string(82.41f, 6.f, 0.1f);
			s.setStiffness(stiff);
			s.pluck(1.f, 1.f, 0.13f);
			const std::vector<float> x = play(s, 1.2f);
			double best = -1.0, bestCents = 0.0;
			for (double c = -40.0; c <= 40.0; c += 0.25) {
				const double hz = 82.41 * n * std::pow(2.0, c / 1200.0);
				const double l = levelAt(x, 0.2, 1.2, hz);
				if (l > best) {
					best = l;
					bestCents = c;
				}
			}
			return bestCents;
		};
		const double plain = partialCents(0.f, 8), stiff = partialCents(1.f, 8);
		check(std::fabs(plain) < 1.0, "stiffness: none, the 8th partial in tune (cents)", plain);
		check(stiff > 2.0 && stiff < 30.0, "stiffness: full, the 8th partial sharp (cents)", stiff);
		// The fundamental's own frequency, not the waveform's period: with the upper partials
		// sharp, a waveform no longer repeats exactly, and matching it against itself is pulled
		// off by them.
		const double c = partialCents(1.f, 1);
		check(std::fabs(c) < 1.0, "stiffness: full, the fundamental in tune (cents)", c);
	}

	// ---- to listen to ----
	{
		// An open E chord strummed down, the strings a hand's width apart in time.
		const double chord[] = {82.41, 123.47, 164.81, 207.65, 246.94, 329.63};
		std::vector<float> l((size_t) (5 * RATE), 0.f), r = l;
		for (int k = 0; k < 6; k++) {
			GuitarString s = string((float) chord[k], 4.f);
			const size_t at = (size_t) (k * 0.012 * RATE);
			s.pluck(0.25f, 0.7f, 0.18f);
			for (size_t i = at; i < l.size(); i++) {
				const float v = s.process();
				l[i] += v * (1.f - k / 10.f);
				r[i] += v * (0.5f + k / 10.f);
			}
		}
		writeWav("build/string-chord.wav", l, r);
	}
	{
		// The same note picked at the bridge, then a quarter of the way, then in the middle.
		std::vector<float> l;
		for (float pos : {0.06f, 0.25f, 0.5f}) {
			GuitarString s = string(110.f, 3.f);
			s.pluck(0.8f, 0.8f, pos);
			const std::vector<float> x = play(s, 2.f);
			l.insert(l.end(), x.begin(), x.end());
		}
		writeWav("build/string-pick-positions.wav", l, l);
	}
	{
		// A fingertip, then a plectrum.
		std::vector<float> l;
		for (float hard : {0.1f, 1.f}) {
			GuitarString s = string(146.83f, 3.f);
			s.pluck(0.8f, hard, 0.15f);
			const std::vector<float> x = play(s, 2.f);
			l.insert(l.end(), x.begin(), x.end());
		}
		writeWav("build/string-hardness.wav", l, l);
	}
	{
		// A note bent up a whole tone and back, as the performer will.
		GuitarString s = string(246.94f, 4.f);
		s.pluck(0.8f, 0.8f, 0.15f);
		std::vector<float> l;
		for (int i = 0; i < (int) (2.5f * RATE); i++) {
			const float t = i / RATE;
			float semis = 0.f;
			if (t > 0.4f && t < 0.7f) semis = 2.f * (t - 0.4f) / 0.3f;
			else if (t >= 0.7f && t < 1.5f) semis = 2.f;
			else if (t >= 1.5f && t < 1.8f) semis = 2.f * (1.8f - t) / 0.3f;
			s.setFrequency(246.94f * std::pow(2.f, semis / 12.f));
			l.push_back(s.process());
		}
		writeWav("build/string-bend.wav", l, l);
	}

	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
