/** See BrassStart.hpp. */
#include "BrassStart.hpp"
#include "BrassJudge.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace px {


/** What nine in ten of the recordings' starts have at most, dB (make brassref ARGS=starts: blips median 0.1,
nine in ten under 1.3; blats median 2.2, nine in ten under 5.3): see brassStartClean. */
static const float BRASS_START_MOST_BLIP = 1.3f, BRASS_START_MOST_BLAT = 5.3f;


static double rmsDb(const double* x, size_t a, size_t b) {
	double s = 0.0;
	for (size_t i = a; i < b; i++)
		s += x[i] * x[i];
	return 10.0 * std::log10(s / (double) std::max<size_t>(1, b - a) + 1e-30);
}


/** THE SPECTRAL CENTROID of x[a] to x[a + w], hertz, to 8 kHz: a Hann window and a radix-2 transform. */
static double centroidOf(const double* x, size_t a, size_t w, double rate) {
	size_t m = 1;
	while (m < 2 * w)
		m *= 2;
	std::vector<std::complex<double>> f(m, 0.0);
	for (size_t i = 0; i < w; i++)
		f[i] = x[a + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) w));
	for (size_t i = 1, j = 0; i < m; i++) {
		size_t bit = m >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(f[i], f[j]);
	}
	for (size_t len = 2; len <= m; len <<= 1) {
		const std::complex<double> wl = std::polar(1.0, -2.0 * M_PI / (double) len);
		for (size_t i = 0; i < m; i += len) {
			std::complex<double> turn = 1.0;
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<double> u = f[i + k], v = f[i + k + len / 2] * turn;
				f[i + k] = u + v;
				f[i + k + len / 2] = u - v;
				turn *= wl;
			}
		}
	}
	double sum = 0.0, sumF = 0.0;
	for (size_t k = 1; k < m / 2; k++) {
		const double hz = (double) k * rate / (double) m;
		if (hz > 8000.0)
			break;
		const double mag = std::abs(f[k]);
		sum += mag;
		sumF += mag * hz;
	}
	return sum > 0.0 ? sumF / sum : 0.0;
}


BrassStartShape brassStartShape(const double* x, size_t n, size_t from, double rate, double hz) {
	BrassStartShape s;
	const size_t w2 = (size_t) (0.002 * rate), w5 = (size_t) (0.005 * rate), w10 = (size_t) (0.01 * rate);
	if (n < from + (size_t) (0.5 * rate))
		return s;
	// Where it is loudest, over 50 ms, as a first guess at where it settles.
	const size_t w50 = (size_t) (0.05 * rate);
	double loud = -1e9;
	for (size_t i = from; i + w50 < n; i += w5)
		loud = std::max(loud, rmsDb(x, i, i + w50));
	size_t onset = 0;
	for (size_t i = from; i + w2 < n; i += w2 / 2)
		if (rmsDb(x, i, i + w2) > loud - 30.0) {
			onset = i;
			break;
		}
	const size_t settleFrom = onset + (size_t) (0.4 * rate), settleTo = onset + (size_t) (0.45 * rate);
	const size_t most = (size_t) std::ceil(rate / (0.55 * hz));
	if (onset == 0 || settleTo + most + w10 > n)
		return s;
	const double settled = rmsDb(x, settleFrom, settleTo);
	if (rmsDb(x, onset, onset + w2) < settled - 40.0)
		return s;
	// The settled pitch and brightness.
	double sureSettled = 0.0;
	const double settledHz = brassPitchOf(x + settleFrom, (int) w10, rate, 0.55 * hz, 1.7 * hz, &sureSettled);
	const double settledBright = centroidOf(x, settleFrom, w10, rate);
	for (int k = 0; k < BrassStartShape::FRAMES; k++) {
		const size_t at = onset + (size_t) (k * 0.005 * rate);
		s.level[k] = (float) (rmsDb(x, at, at + w5) - settled);
		double sure = 0.0;
		const double f = brassPitchOf(x + at, (int) w10, rate, 0.55 * hz, 1.7 * hz, &sure);
		s.cents[k] = (float) (1200.0 * std::log2(f / settledHz));
		s.sure[k] = (float) std::fmax(0.0, std::fmin(1.0, sure));
		const double c = centroidOf(x, at, w10, rate);
		s.bright[k] = (float) (c > 0.0 && settledBright > 0.0 ? std::log2(c / settledBright) : 0.0);
	}
	s.valid = sureSettled > 0.8;
	s.onset = onset;
	// THE BLIP AND THE BLAT over the first 60 ms, every 5 ms against the 30 ms after; only where they are heard,
	// within 12 dB of the settled level, as further down every start's level flickers; and the level over two and
	// a half periods or 10 ms, whichever is longer: over 5 ms, shorter than one period of B flat 2, a peaky wave
	// read loud in one window and quiet in the next, a blip of 15 dB where there was none.
	{
		const size_t wb = std::max(w10, (size_t) (2.5 * rate / hz));
		float smooth[19];
		for (int k = 0; k < 19; k++) {
			const size_t at = onset + (size_t) (k * 0.005 * rate);
			smooth[k] = (float) (at + wb < n ? rmsDb(x, at, at + wb) - settled : -100.0);
		}
		for (int k = 0; k < 12; k++) {
			float after = 1e9f;
			for (int j = k + 1; j <= k + 6 && j < 19; j++)
				after = std::fmin(after, smooth[j]);
			if (smooth[k] > -12.f)
				s.blip = std::fmax(s.blip, smooth[k] - after);
			if (smooth[k + 1] > -12.f)
				s.blat = std::fmax(s.blat, smooth[k + 1] - smooth[k]);
		}
	}
	return s;
}


bool brassStartClean(const BrassStartShape& s) {
	return s.valid && s.blip <= BRASS_START_MOST_BLIP && s.blat <= BRASS_START_MOST_BLAT;
}


int brassStartRegister(double hz) {
	const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
	return midi < 47.5 ? 0 : midi < 59.5 ? 1 : 2;
}


int brassStartDynamic(double level) {
	// pp is a seventh up the ladder, mf four sevenths, ff six.
	const double step = 7.0 * level;
	return step < 2.5 ? 0 : step < 5.0 ? 1 : 2;
}


double brassStartDistance(const BrassStartShape& s, const BrassStartTemplate& t, double* parts) {
	if (!s.valid)
		return 1e9;
	double sum = 0.0;
	int counted = 0;
	double each[4] = {0, 0, 0, 0};
	int eachCount[4] = {0, 0, 0, 0};
	auto part = [&](float v, float m, float spread, double least) {
		const double z = std::fabs((double) v - (double) m) / std::fmax(least, (double) spread);
		const double held = std::fmin(3.0, z);
		return held * held;
	};
	for (int k = 0; k < BrassStartShape::FRAMES; k++) {
		const double l = part(s.level[k], t.level[k], t.levelSpread[k], 1.0);
		sum += l;
		each[0] += l;
		eachCount[0]++;
		// The pitch, how surely it repeats and the brightness only where the recordings are loud enough to
		// say: within 30 dB of settled.
		if (t.level[k] > -30.0) {
			const double c = part(s.cents[k], t.cents[k], t.centsSpread[k], 5.0), r = part(s.sure[k], t.sure[k], t.sureSpread[k], 0.05),
				b = part(s.bright[k], t.bright[k], t.brightSpread[k], 0.05);
			sum += c + r + b;
			each[1] += c;
			each[2] += r;
			each[3] += b;
			eachCount[1]++;
			eachCount[2]++;
			eachCount[3]++;
			counted += 4;
		}
		else
			counted++;
	}
	if (parts)
		for (int i = 0; i < 4; i++)
			parts[i] = eachCount[i] ? each[i] / eachCount[i] : 0.0;
	return counted ? sum / counted : 1e9;
}


} // namespace px
