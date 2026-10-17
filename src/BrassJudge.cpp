/** See BrassJudge.hpp. */
#include "BrassJudge.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace px {

using cd = std::complex<double>;


/** A RADIX-2 TRANSFORM in place; `inverse` without the 1 / n. */
static void transform(std::vector<cd>& a, bool inverse) {
	const size_t n = a.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const cd w = std::polar(1.0, (inverse ? 2.0 : -2.0) * M_PI / (double) len);
		for (size_t i = 0; i < n; i += len) {
			cd turn = 1.0;
			for (size_t k = 0; k < len / 2; k++) {
				const cd u = a[i + k], v = a[i + k + len / 2] * turn;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				turn *= w;
			}
		}
	}
}


/** THE PITCH OF ONE STRETCH, by de Cheveigné and Kawahara's YIN (2002): the difference of the stretch from
itself moved by each lag, normalised by its running mean, and the first lag from `least` where it falls under
a tenth, or else its least; the period refined on a parabola. `sure` is one less that normalised difference:
one where the stretch repeats exactly. The stretch is x[0] to x[w], and x holds `most` samples more. */
static double yin(const double* x, int w, int least, int most, double* sure) {
	size_t m = 1;
	while (m < (size_t) (w + most))
		m *= 2;
	m *= 2;
	std::vector<cd> a(m, 0.0), b(m, 0.0);
	for (int j = 0; j < w; j++)
		a[(size_t) j] = x[j];
	for (int j = 0; j < w + most; j++)
		b[(size_t) j] = x[j];
	transform(a, false);
	transform(b, false);
	for (size_t k = 0; k < m; k++)
		a[k] = std::conj(a[k]) * b[k];
	transform(a, true);
	// The energy of the stretch, and of the stretch moved by each lag, by running sums.
	std::vector<double> sq((size_t) (w + most) + 1, 0.0);
	for (int j = 0; j < w + most; j++)
		sq[(size_t) j + 1] = sq[(size_t) j] + x[j] * x[j];
	const double e1 = sq[(size_t) w];
	std::vector<double> d((size_t) most + 1, 0.0);
	for (int t = 1; t <= most; t++) {
		const double r = a[(size_t) t].real() / (double) m;
		const double e2 = sq[(size_t) (t + w)] - sq[(size_t) t];
		d[(size_t) t] = std::fmax(0.0, e1 + e2 - 2.0 * r);
	}
	// Normalised by its running mean.
	std::vector<double> n((size_t) most + 1, 1.0);
	double sum = 0.0;
	for (int t = 1; t <= most; t++) {
		sum += d[(size_t) t];
		n[(size_t) t] = sum > 0.0 ? d[(size_t) t] * t / sum : 1.0;
	}
	const int from = std::max(2, least);
	int found = -1;
	for (int t = from; t < most; t++)
		if (n[(size_t) t] < 0.1) {
			while (t + 1 < most && n[(size_t) t + 1] < n[(size_t) t])
				t++;
			found = t;
			break;
		}
	if (found < 0) {
		found = from;
		for (int t = from; t < most; t++)
			if (n[(size_t) t] < n[(size_t) found])
				found = t;
	}
	*sure = 1.0 - n[(size_t) found];
	double period = (double) found;
	if (found > 1 && found < most) {
		const double p = n[(size_t) found - 1], q = n[(size_t) found], r = n[(size_t) found + 1];
		const double den = p - 2.0 * q + r;
		if (den > 1e-12)
			period += 0.5 * (p - r) / den;
	}
	return period;
}


double brassPitchOf(const double* x, int w, double rate, double lowHz, double highHz, double* sure) {
	const int least = (int) std::floor(rate / highHz), most = (int) std::ceil(rate / lowHz);
	return rate / yin(x, w, least, most, sure);
}


BrassJudged brassJudgeStart(const std::vector<double>& cup, const std::vector<double>& heard, size_t at, double rate, double hz) {
	BrassJudged j;
	j.dip = 0.0;
	const size_t b = std::min(cup.size(), heard.size());
	if (b < at + (size_t) (0.1 * rate))
		return j;
	auto level = [&](size_t i0, size_t i1) {
		double s2 = 0.0;
		for (size_t i = i0; i < i1; i++)
			s2 += heard[i] * heard[i];
		return 10.0 * std::log10(s2 / (double) std::max<size_t>(1, i1 - i0) + 1e-30);
	};
	const double settled = level(b - (size_t) (0.05 * rate), b);
	const int w = (int) (0.01 * rate);
	const int least = (int) std::floor(rate / (1.7 * hz)), most = (int) std::ceil(rate / (0.55 * hz));
	const size_t hop = (size_t) (0.0025 * rate);
	double held = -1.0, rise0 = -1.0, rise1 = -1.0, front = -1e9;
	for (size_t i = at; i + (size_t) (w + most) < b; i += hop) {
		const double ms = 1000.0 * ((double) i + 0.5 * w - (double) at) / rate, dur = 1000.0 * (double) hop / rate;
		const double here = level(i, i + (size_t) w);
		if (ms < 80.0)
			front = std::max(front, here);
		if (rise0 < 0.0 && here > settled - 30.0)
			rise0 = ms;
		if (rise1 < 0.0 && here > settled - 3.0)
			rise1 = ms;
		if (here <= settled - 30.0)
			continue;
		double sure = 0.0;
		const double f = rate / yin(&cup[i], w, least, most, &sure);
		const double off = std::fabs(1200.0 * std::log2(f / hz));
		if (off <= 20.0 && sure > 0.8) {
			if (held < 0.0)
				held = ms - 0.5 * dur;
			if (ms - held >= 40.0 && j.settle >= 1e8)
				j.settle = std::max(0.0, held);
		}
		else
			held = -1.0;
		if (j.settle < 1e8)
			continue;
		const double loud = std::fmin(1.0, std::pow(10.0, (here - settled) / 20.0));
		if (sure < 0.6)
			j.rough += dur * loud;
		else if (off > 100.0)
			j.squawk += dur;
		else if (off > 20.0)
			j.smear += dur * loud;
	}
	if (j.settle >= 1e8)
		j.settle = 1000.0 * (double) (b - at) / rate;
	if (rise0 >= 0.0 && rise1 >= rise0)
		j.gap = std::fmax(0.0, (rise1 - rise0) - 60.0) / 3.0;
	j.over = std::fmax(0.0, front - settled - 2.0);
	return j;
}


BrassJudged brassJudgeJoin(const std::vector<double>& cup, const std::vector<double>& heard, size_t at, double rate, double fromHz,
	double toHz) {
	BrassJudged j;
	const size_t b = std::min(cup.size(), heard.size());
	if (b < at + (size_t) (0.06 * rate))
		return j;
	auto level = [&](size_t i0, size_t i1) {
		double s2 = 0.0;
		for (size_t i = i0; i < i1; i++)
			s2 += heard[i] * heard[i];
		return 10.0 * std::log10(s2 / (double) std::max<size_t>(1, i1 - i0) + 1e-30);
	};
	const size_t back2 = (size_t) (0.2 * rate), back1 = (size_t) (0.1 * rate);
	const double before = at > back2 ? level(at - back2, at - back1) : level(0, at);
	const double after = level(b - (size_t) (0.05 * rate), b);
	const double lower = std::min(before, after);
	const double lo = std::min(fromHz, toHz), hi = std::max(fromHz, toHz);

	// THE PITCH EVERY 2.5 ms, by YIN over 10 ms, from 60 ms before the join to the end of what was played.
	const int w = (int) (0.01 * rate);
	const int least = (int) std::floor(rate / (1.7 * hi)), most = (int) std::ceil(rate / (0.55 * lo));
	const size_t hop = (size_t) (0.0025 * rate);
	const size_t first = at > (size_t) (0.06 * rate) ? at - (size_t) (0.06 * rate) : 0;
	double held = -1.0;
	for (size_t i = first; i + (size_t) (w + most) < b; i += hop) {
		double sure = 0.0;
		const double hz = rate / yin(&cup[i], w, least, most, &sure);
		// Each stretch's time is its middle.
		const double ms = 1000.0 * ((double) i + 0.5 * w - (double) at) / rate, dur = 1000.0 * (double) hop / rate;
		const double here = level(i, i + (size_t) w);
		const double toNew = std::fabs(1200.0 * std::log2(hz / toHz)), toOld = std::fabs(1200.0 * std::log2(hz / fromHz));
		if (toNew <= 20.0 && sure > 0.8) {
			if (held < 0.0)
				held = ms - 0.5 * dur;
			if (ms - held >= 40.0 && j.settle >= 1e8)
				j.settle = std::max(0.0, held);
		}
		else
			held = -1.0;
		if (here <= after - 20.0 || j.settle < 1e8)
			continue;
		const double loud = std::fmin(1.0, std::pow(10.0, (here - after) / 20.0));
		if (sure < 0.6)
			j.rough += dur * loud;
		else if (toNew > 30.0 && toOld > 30.0) {
			if (hz > hi * 1.06 || hz < lo / 1.06)
				j.squawk += dur;
			else
				j.smear += dur * loud;
		}
	}
	if (j.settle >= 1e8)
		j.settle = 1000.0 * (double) (b - at) / rate;
	const size_t w5 = (size_t) (0.005 * rate);
	double lowest = 1e9;
	const size_t g0 = at > (size_t) (0.15 * rate) ? at - (size_t) (0.15 * rate) : 0;
	for (size_t i = g0; i + w5 < std::min(b, at + (size_t) (0.15 * rate)); i += w5) {
		const double l = level(i, i + w5);
		lowest = std::min(lowest, l);
		if (l < lower - 20.0)
			j.gap += 5.0;
	}
	j.dip = lowest - lower;
	return j;
}


} // namespace px
