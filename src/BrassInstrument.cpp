/** See BrassInstrument.hpp. */
#include "BrassInstrument.hpp"

#include <cmath>
#include <cstdlib>

namespace px {


void BrassInstrument::setup(const Design& d) {
	made = d;
	made.oversampling = std::max(1, d.oversampling);
	const double innerRate = d.rate * made.oversampling;
	BrassWaveguide::Setup s;
	s.air = d.air;
	s.rate = innerRate;
	s.tuningPull = d.tuningPull;
	s.listener = d.listener;
	s.steepening = d.steepening;
	s.extension = 0.0;
	column.setup(s);
	lips.setup(d.lips, d.air, innerRate);
	lips.setResonance(lipsSet);
	slideNow = slideTold = 0.0;
	sinceTold = 0;
	tellEvery = std::max(1, (int) std::lround(innerRate / 8000.0));
	slideLengthTold = 0.0;
	breath = 0.0;
	tongue = 1.0;
	noiseState = 0x9e3779b97f4a7c15ull;
	noiseNow = 0.0;
	noisePole = 1.0 - std::exp(-2.0 * M_PI * d.breathNoiseHz / innerRate);
	designLowPass();
}


/** A WINDOWED SINC, 48 taps at the outer rate's worth of inner samples, Blackman's window, cut at 20 kHz or
0.42 of the outer rate, whichever is lower: from 20 kHz to 26 at 48 kHz, where what is above 24 would fold
down, it falls past 70 dB. */
void BrassInstrument::designLowPass() {
	const int n = made.oversampling;
	inner.assign(n > 1 ? (size_t) (2 * 48 * n) : 0, 0.0);
	innerAt = 0;
	if (n <= 1) {
		lowPass.clear();
		return;
	}
	const int taps = 48 * n;
	const double innerRate = made.rate * n;
	const double cut = std::fmin(20000.0, 0.42 * made.rate) / innerRate;
	lowPass.assign((size_t) taps, 0.0);
	double sum = 0.0;
	for (int i = 0; i < taps; i++) {
		const double t = (double) i - 0.5 * (double) (taps - 1);
		const double sinc = std::fabs(t) < 1e-12 ? 2.0 * cut : std::sin(2.0 * M_PI * cut * t) / (M_PI * t);
		const double u = (double) i / (double) (taps - 1);
		const double window = 0.42 - 0.5 * std::cos(2.0 * M_PI * u) + 0.08 * std::cos(4.0 * M_PI * u);
		lowPass[(size_t) i] = sinc * window;
		sum += lowPass[(size_t) i];
	}
	for (double& h : lowPass)
		h /= sum;
}


void BrassInstrument::setSlide(double metres) {
	slideNow = std::fmax(0.0, metres);
}


double BrassInstrument::step() {
	const int n = made.oversampling;
	double out = 0.0;
	for (int k = 0; k < n; k++) {
		// The slide, its length told to the column every step while it moves — told in steps of an eighth of
		// a millisecond, its delays jumped at 8 kHz and buzzed in the highs — and its losses every eighth.
		if (slideNow != slideTold && ++sinceTold >= tellEvery) {
			column.setExtension(slideNow);
			slideTold = slideLengthTold = slideNow;
			sinceTold = 0;
		}
		else if (slideNow != slideLengthTold) {
			column.setExtension(slideNow, false);
			slideLengthTold = slideNow;
		}
		lips.setMouthPressure(breath * std::fmin(1.0, std::fmax(0.0, tongue)));
		lips.setTongue(tongue);
		double without = 0.0, perFlow = 0.0;
		column.begin(&without, &perFlow);
		double flow = lips.step(column.cupPressure(), without, perFlow);
		if (made.breathNoise > 0.0) {
			// Xorshift's next number, as a value from minus one to one, low passed.
			noiseState ^= noiseState << 13;
			noiseState ^= noiseState >> 7;
			noiseState ^= noiseState << 17;
			const double white = (double) (noiseState >> 11) * (2.0 / 9007199254740992.0) - 1.0;
			noiseNow += noisePole * (white - noiseNow);
			const double mouth = breath * std::fmin(1.0, std::fmax(0.0, tongue)) / made.breathNoiseFull;
			const double share = made.breathNoise * std::fmin(1.5, mouth * mouth);
			flow += share * std::fabs(flow) * noiseNow;
		}
		column.end(flow);
		const double heard = column.listenerPressure();
		if (n <= 1) {
			out = heard;
			continue;
		}
		const size_t size = inner.size() / 2;
		inner[(size_t) innerAt] = inner[(size_t) innerAt + size] = heard;
		innerAt = (innerAt + 1) % (int) size;
	}
	if (n > 1) {
		// The newest inner sample is at innerAt - 1; the taps run from the oldest of the last `taps`.
		const size_t size = inner.size() / 2, taps = lowPass.size();
		const size_t first = (size_t) innerAt + size - taps;
		double sum = 0.0;
		for (size_t i = 0; i < taps; i++)
			sum += lowPass[i] * inner[first + i];
		out = sum;
	}
	if (!std::isfinite(out) || !std::isfinite(column.cupPressure())) {
		// BLOWN UP: from rest again, the controls kept.
		const double b = breath, t = tongue, l = lipsSet, x = slideNow;
		setup(made);
		setLips(l);
		breath = b;
		tongue = t;
		slideNow = x;
		return 0.0;
	}
	return out;
}


} // namespace px
