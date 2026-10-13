/** See BowedRehearsal.hpp. */
#include "BowedRehearsal.hpp"
#include "Crew.hpp"

#include <algorithm>

namespace px {


Attack attackFromRest(BowedString& s, float force, float accel, double hz, int periodsAfter,
                      std::vector<float>* bridge, float topSpeed, double regular, size_t firstRun) {
	Attack r;
	const double rate = s.sampleRate();
	const double period = rate / hz;
	std::vector<size_t> slips;
	std::vector<double> slipMu;            // mean friction coefficient, slip by slip
	double muSum = 0.0;
	int muN = 0;
	bool was = false;
	const size_t cap = (size_t) (3.0 * rate);
	size_t end = cap;
	SlipWatch watch;
	for (size_t i = 0; i < end; i++) {
		const float t = (float) i / (float) rate;
		s.setBow(force, std::fmin(accel * t, topSpeed));
		const float out = s.process();
		if (bridge)
			bridge->push_back(out);
		const bool flyback = watch.update(s, force, std::fmin(accel * t, topSpeed));
		const bool stuck = (s.bristling() || s.heating()) ? !flyback : s.sticking();
		// A slip starts where the string comes off the hair.
		if (i > 0 && was && !stuck) {
			if (!slips.empty())
				slipMu.push_back(muN ? muSum / muN : 0.0);
			slips.push_back(i);
			muSum = 0.0;
			muN = 0;
			if (slips.size() == 1) {
				end = std::min(cap, i + (size_t) (periodsAfter * period));
				r.slipSpeed = s.velocityAtBow() - std::fmin(accel * t, topSpeed);
			}
		}
		if (!stuck && !slips.empty()) {
			muSum += std::fabs(s.frictionForce()) / force;
			muN++;
		}
		was = stuck;
	}
	if (slips.empty())
		return r;
	r.firstSlip = (double) slips[0] / rate;
	if (!slipMu.empty())
		r.muFirst = slipMu[0];
	// The later slips: those before the first one's reflection from the nut returns.
	{
		const double n = 1.0 / std::fmax(1e-3, (double) s.contact());
		double sum = 0.0;
		int k = 0;
		for (size_t j = 1; j < slipMu.size() && (double) j < n - 1.0; j++, k++)
			sum += slipMu[j];
		r.muLater = k ? sum / k : r.muFirst;
	}
	// REGULAR FROM SLIP k: every gap from there to the end within a tenth of a period, and at
	// least three of them, or the end of the run is not telling us anything.
	size_t k = slips.size();
	for (size_t j = slips.size(); j-- > 1; ) {
		const double gap = (double) (slips[j] - slips[j - 1]);
		if (std::fabs(gap - period) > regular * period)
			break;
		k = j - 1;
	}
	if (k < slips.size() && slips.size() - k < 4)
		k = slips.size();
	// OR, IF ASKED, the first slip from which five periods running are regular.
	if (firstRun > 0) {
		k = slips.size();
		for (size_t j = 0; j + firstRun < slips.size(); j++) {
			bool run = true;
			for (size_t q = j + 1; q <= j + firstRun && run; q++)
				run = std::fabs((double) (slips[q] - slips[q - 1]) - period) <= regular * period;
			if (run) { k = j; break; }
		}
	}
	if (k < slips.size()) {
		r.periodsBefore = (int) std::lround((double) (slips[k] - slips[0]) / period);
		r.seconds = (double) (slips[k] - slips[0]) / rate;
	}
	// What went wrong first.
	const size_t upTo = (k < slips.size()) ? k : slips.size() - 1;
	for (size_t j = 1; j <= upTo; j++) {
		const double gap = (double) (slips[j] - slips[j - 1]);
		if (gap > 1.1 * period) { r.prolonged = true; break; }
		if (gap < 0.9 * period) { r.prolonged = false; break; }
	}
	if (k >= slips.size()) {
		// Never regular: what dominated, gaps too long or too short?
		int longer = 0, shorter = 0;
		for (size_t j = 1; j < slips.size(); j++) {
			const double gap = (double) (slips[j] - slips[j - 1]);
			if (gap > 1.1 * period) longer++;
			else if (gap < 0.9 * period) shorter++;
		}
		r.prolonged = longer >= shorter || slips.size() < 2;
		r.seconds = (double) (end - slips[0]) / rate;
	}
	return r;
}


double startRoughness(const std::vector<float>& x, double firstSlip, double rate) {
	auto biquad = [rate](double hz, bool high, double* b, double* a) {
		const double w = 2.0 * M_PI * hz / rate, cw = std::cos(w), alpha = std::sin(w) / (2.0 * 0.7071);
		const double a0 = 1.0 + alpha;
		b[0] = (high ? (1.0 + cw) / 2.0 : (1.0 - cw) / 2.0) / a0;
		b[1] = (high ? -(1.0 + cw) : (1.0 - cw)) / a0;
		b[2] = b[0];
		a[0] = -2.0 * cw / a0;
		a[1] = (1.0 - alpha) / a0;
	};
	double hb[3], ha[2], lb[3], la[2];
	biquad(2000.0, true, hb, ha);
	biquad(1000.0, false, lb, la);
	double hx1 = 0, hx2 = 0, hy1 = 0, hy2 = 0, lx1 = 0, lx2 = 0, ly1 = 0, ly2 = 0, high = 0, low = 0;
	const size_t from = (size_t) (firstSlip * rate), to = std::min(x.size(), from + (size_t) (0.08 * rate));
	for (size_t i = 0; i < to; i++) {
		const double v = x[i];
		const double h = hb[0] * v + hb[1] * hx1 + hb[2] * hx2 - ha[0] * hy1 - ha[1] * hy2;
		hx2 = hx1; hx1 = v; hy2 = hy1; hy1 = h;
		const double l = lb[0] * v + lb[1] * lx1 + lb[2] * lx2 - la[0] * ly1 - la[1] * ly2;
		lx2 = lx1; lx1 = v; ly2 = ly1; ly1 = l;
		if (i >= from) {
			high += h * h;
			low += l * l;
		}
	}
	return low > 0.0 ? high / low : 0.0;
}


double squeakOf(const std::vector<float>& x, double rate, double hz, double tolerance) {
	// Down to near 48 kHz by averaging blocks, which is lowpass enough below 10 kHz for this.
	const int block = std::max(1, (int) std::lround(rate / 48000.0));
	const double r = rate / block;
	std::vector<double> y;
	for (size_t i = 0; i + (size_t) block <= x.size(); i += (size_t) block) {
		double sum = 0.0;
		for (int k = 0; k < block; k++)
			sum += x[i + (size_t) k];
		y.push_back(sum / block);
	}
	if (y.size() < 64)
		return -99.0;
	size_t n = 1;
	while (n < 2 * y.size() || n < 4096)
		n <<= 1;
	std::vector<double> re(n, 0.0), im(n, 0.0);
	for (size_t i = 0; i < y.size(); i++)
		re[i] = y[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) (y.size() - 1)));
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const double ang = -2.0 * M_PI / (double) len;
		for (size_t i = 0; i < n; i += len)
			for (size_t k = 0; k < len / 2; k++) {
				const double c = std::cos(ang * k), s = std::sin(ang * k);
				const size_t a = i + k, b = i + k + len / 2;
				const double tr = re[b] * c - im[b] * s, ti = re[b] * s + im[b] * c;
				re[b] = re[a] - tr;
				im[b] = im[a] - ti;
				re[a] += tr;
				im[a] += ti;
			}
	}
	double on = 0.0, off = 0.0;
	for (size_t bin = 1; bin < n / 2; bin++) {
		const double f = (double) bin * r / (double) n;
		if (f < 3000.0 || f > 10000.0)
			continue;
		const double p = re[bin] * re[bin] + im[bin] * im[bin];
		const double k = std::round(f / hz);
		if (k >= 1.0 && std::fabs(f / (k * hz) - 1.0) <= tolerance)
			on += p;
		else
			off += p;
	}
	return 10.0 * std::log10(std::fmax(1e-30, off) / std::fmax(1e-30, on));
}


std::vector<Rehearsed> rehearseShortlist(const BowedString& model, double hz, float beta,
                                         float force, float speed, Crew* crew) {
	const double rate = model.sampleRate();
	const double z = model.impedance();
	const double zBow = z * 3.27 / 4.27, T = 1.0 / hz;
	std::vector<Rehearsed> first, second;
	int soonest = -1;
	// EACH TRIAL ON ITS OWN COPY OF THE STRING, shared out over the crew where there is one, and
	// gathered in the order they were listed, so the ranking below sees what one thread would.
	struct Trial {
		float beta;
		double bite;
		float accel;
	};
	auto tryAll = [&](const std::vector<Trial>& trials, std::vector<Rehearsed>* into) {
		std::vector<Rehearsed> found(trials.size());
		std::vector<char> started(trials.size(), 0);
		auto one = [&](int i) {
			const Trial& t = trials[(size_t) i];
			BowedString s = model;
			s.setContact(t.beta);
			std::vector<float> bridge;
			const Attack r = attackFromRest(s, (float) (force * t.bite), t.accel, hz, 20, &bridge, speed);
			if (r.periodsBefore < 0)
				return;
			Rehearsed& c = found[(size_t) i];
			c.beta = t.beta;
			c.bite = t.bite;
			c.accel = t.accel;
			c.periods = r.periodsBefore;
			c.roughness = startRoughness(bridge, r.firstSlip, rate);
			started[(size_t) i] = 1;
		};
		if (crew)
			crew->each((int) trials.size(), one);
		else
			for (int i = 0; i < (int) trials.size(); i++)
				one(i);
		for (size_t i = 0; i < trials.size(); i++)
			if (started[i])
				into->push_back(found[i]);
	};
	{
		std::vector<Trial> trials;
		for (int b = 0; b < 7; b++)
			for (int k = 0; k < 24; k++) {
				const double bite = std::pow(4.0, b / 6.0);
				const double kk = 0.004 * std::pow(30.0, k / 23.0);
				trials.push_back({beta, bite, (float) (kk * beta * (1.0 - beta) * force * bite / (T * zBow))});
			}
		tryAll(trials, &first);
	}
	for (const Rehearsed& c : first)
		if (soonest < 0 || c.periods < soonest)
			soonest = c.periods;
	// Within a limit, smoothest first; beyond it, soonest first.
	auto rank = [](std::vector<Rehearsed>& v, int limit) {
		std::stable_sort(v.begin(), v.end(), [limit](const Rehearsed& x, const Rehearsed& y) {
			const bool xi = x.periods <= limit, yi = y.periods <= limit;
			if (xi != yi)
				return xi;
			if (xi)
				return x.roughness < y.roughness;
			return x.periods != y.periods ? x.periods < y.periods : x.roughness < y.roughness;
		});
	};
	rank(first, 5);
	std::vector<Rehearsed> out;
	if (!first.empty() && first.front().periods <= 3)
		out = first;
	else {
		std::vector<Trial> trials;
		for (float shift : {0.85f, 1.2f, 0.7f, 1.4f}) {
			const float b2 = beta * shift;
			for (int b = 0; b < 13; b++)
				for (int k = 0; k < 40; k++) {
					const double bite = std::pow(8.0, b / 12.0);
					const double kk = 0.002 * std::pow(100.0, k / 39.0);
					trials.push_back({b2, bite, (float) (kk * b2 * (1.0 - b2) * force * bite / (T * zBow))});
				}
		}
		tryAll(trials, &second);
		rank(second, 3);
		for (const Rehearsed& c : second)
			if (c.periods <= 3)
				out.push_back(c);
		for (const Rehearsed& c : first)
			out.push_back(c);
		for (const Rehearsed& c : second)
			if (c.periods > 3)
				out.push_back(c);
		rank(out, 3);
	}
	for (Rehearsed& c : out)
		c.soonest = soonest;
	return out;
}


double roughnessOf(const std::vector<float>& x, double rate, double hz, size_t from) {
	// DOWN TO NEAR 48 kHz by averaging blocks, which is all the measure needs and an eighth of the work at
	// an oversampled string's rate — but never below a hundred samples a period, for a high note.
	const double wanted = std::max(48000.0, 100.0 * hz);
	const int block = std::max(1, (int) std::floor(rate / wanted));
	std::vector<double> y;
	y.reserve((x.size() > from ? x.size() - from : 0) / (size_t) block + 1);
	for (size_t i = from; i + (size_t) block <= x.size(); i += (size_t) block) {
		double sum = 0.0;
		for (int k = 0; k < block; k++)
			sum += x[i + (size_t) k];
		y.push_back(sum / block);
	}
	const double period = rate / (double) block / hz;
	const int span = (int) (4.0 * period);
	const int lo = (int) std::floor(period * 0.97), hi = (int) std::ceil(period * 1.03);
	// THE CORRELATION AT A LAG BETWEEN SAMPLES, the later stretch read by straight lines.
	auto correlation = [&](size_t a, double lag) {
		const int L = (int) std::floor(lag);
		const double f = lag - L;
		double xy = 0.0, xx = 0.0, yy = 0.0;
		for (int i = 0; i < span; i++) {
			const double p = y[a + (size_t) i];
			const double q = (1.0 - f) * y[a + (size_t) (i + L)] + f * y[a + (size_t) (i + L + 1)];
			xy += p * q;
			xx += p * p;
			yy += q * q;
		}
		return xy / std::sqrt(xx * yy + 1e-30);
	};
	double total = 0.0;
	int n = 0;
	for (size_t a = 0; a + (size_t) (span + hi + 3) < y.size(); a += (size_t) std::max(1.0, 2.0 * period)) {
		// TO THE SAMPLE, then between samples by golden section on the two either side.
		double best = -2.0;
		int at = lo;
		for (int lag = lo; lag <= hi; lag++) {
			const double c = correlation(a, (double) lag);
			if (c > best) {
				best = c;
				at = lag;
			}
		}
		double l = std::max((double) lo, at - 1.0), r = std::min((double) hi, at + 1.0);
		const double g = 0.6180339887498949;
		double m1 = r - g * (r - l), m2 = l + g * (r - l);
		double c1 = correlation(a, m1), c2 = correlation(a, m2);
		for (int k = 0; k < 24; k++) {
			if (c1 < c2) {
				l = m1;
				m1 = m2;
				c1 = c2;
				m2 = l + g * (r - l);
				c2 = correlation(a, m2);
			}
			else {
				r = m2;
				m2 = m1;
				c2 = c1;
				m1 = r - g * (r - l);
				c1 = correlation(a, m1);
			}
		}
		best = std::max(best, std::max(c1, c2));
		total += 1.0 - std::fmin(1.0, best);
		n++;
	}
	return n ? 10.0 * std::log10(total / n + 1e-12) : 0.0;
}


} // namespace px
