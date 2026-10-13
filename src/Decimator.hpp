#pragma once
/** BACK DOWN TO THE ENGINE'S RATE. A string running at several times the sample rate (see
BowedString::oversampling) has to be filtered before it is thinned out, or everything above half the
engine's rate folds back down into the audible band. A windowed-sinc lowpass at nine-tenths of the
engine's Nyquist frequency, Blackman-Harris windowed, twenty-four taps a factor each side, computed
only for the samples that are kept. No Rack in here. */
#include <cmath>
#include <vector>

namespace px {


struct Decimator {
	/** By `factor`, one sample out for every `factor` in. One is a plain pass-through. */
	void setFactor(int factor) {
		n = factor < 1 ? 1 : factor;
		taps.clear();
		history.assign(1, 0.f);
		pos = 0;
		count = 0;
		if (n == 1)
			return;
		const int half = 24 * n;
		const int len = 2 * half + 1;
		const double cutoff = 0.45 / n;   // of the fast rate
		taps.resize((size_t) len);
		double sum = 0.0;
		for (int i = 0; i < len; i++) {
			const double x = i - half;
			const double sinc = (x == 0.0) ? 2.0 * cutoff : std::sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
			const double t = (double) i / (len - 1);
			const double window = 0.35875 - 0.48829 * std::cos(2.0 * M_PI * t) + 0.14128 * std::cos(4.0 * M_PI * t)
				- 0.01168 * std::cos(6.0 * M_PI * t);
			taps[(size_t) i] = (float) (sinc * window);
			sum += taps[(size_t) i];
		}
		for (float& t : taps)
			t = (float) (t / sum);
		// TWICE OVER: each sample is written at its place and again a whole length on, so the window
		// of the last `taps` samples is always one unbroken run, read straight through.
		history.assign(2 * taps.size(), 0.f);
	}
	int factor() const { return n; }

	/** One fast sample in. True when a slow sample is ready, in `*out`. */
	bool push(float x, float* out) {
		if (n == 1) {
			*out = x;
			return true;
		}
		const int len = (int) taps.size();
		history[(size_t) pos] = x;
		history[(size_t) (pos + len)] = x;
		pos = (pos + 1) % len;
		if (++count < n)
			return false;
		count = 0;
		double y = 0.0;
		const float* window = &history[(size_t) pos];
		for (int i = 0; i < len; i++)
			y += taps[(size_t) i] * window[i];
		*out = (float) y;
		return true;
	}

private:
	int n = 1, pos = 0, count = 0;
	std::vector<float> taps;
	std::vector<float> history;
};


} // namespace px
