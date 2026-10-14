/** See BrassRadiation.hpp. */
#include "BrassRadiation.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <mutex>
#include <tuple>

namespace px {

using cd = std::complex<double>;


double brassJ1(double x) {
	const double ax = std::fabs(x);
	if (ax <= 3.0) {
		const double y = (x / 3.0) * (x / 3.0);
		const double over = 0.5 + y * (-0.56249985 + y * (0.21093573 + y * (-0.03954289 + y * (0.00443319 + y * (-0.00031761 + y * 0.00001109)))));
		return x * over;
	}
	const double z = 3.0 / ax;
	const double f = 0.79788456 + z * (0.00000156 + z * (0.01659667 + z * (0.00017105 + z * (-0.00249511 + z * (0.00113653 + z * -0.00020033)))));
	const double t = ax - 2.35619449 + z * (0.12499612 + z * (0.00005650 + z * (-0.00637879 + z * (0.00074348 + z * (0.00079824 + z * -0.00029166)))));
	const double v = f * std::cos(t) / std::sqrt(ax);
	return x < 0.0 ? -v : v;
}


double brassY1(double x) {
	if (x <= 3.0) {
		const double y = (x / 3.0) * (x / 3.0);
		const double poly = -0.6366198 + y * (0.2212091 + y * (2.1682709 + y * (-1.3164827 + y * (0.3123951 + y * (-0.0400976 + y * 0.0027873)))));
		return (2.0 / M_PI) * std::log(0.5 * x) * brassJ1(x) + poly / x;
	}
	const double z = 3.0 / x;
	const double f = 0.79788456 + z * (0.00000156 + z * (0.01659667 + z * (0.00017105 + z * (-0.00249511 + z * (0.00113653 + z * -0.00020033)))));
	const double t = x - 2.35619449 + z * (0.12499612 + z * (0.00005650 + z * (-0.00637879 + z * (0.00074348 + z * (0.00079824 + z * -0.00029166)))));
	return f * std::sin(t) / std::sqrt(x);
}


/** tan^-1(-J1 / N1), continuous from nought at nought to pi at J1's first zero. */
static double delta(double x) { return x <= 0.0 ? 0.0 : std::atan2(brassJ1(x), -brassY1(x)); }

/** Points for the integrals over phi, x = ka sin phi, which takes the square root at x = ka away. */
static const int POINTS = 400;


double brassUnflangedReflection(double ka) {
	if (ka <= 0.0)
		return 1.0;
	// int_0^ka delta(x) / (x sqrt(ka^2 - x^2)) dx = int_0^(pi/2) delta(x) / x dphi; the midpoint rule.
	double sum = 0.0;
	for (int i = 0; i < POINTS; i++) {
		const double x = ka * std::sin(0.5 * M_PI * ((double) i + 0.5) / POINTS);
		sum += delta(x) / x;
	}
	sum *= 0.5 * M_PI / POINTS;
	return std::exp(-2.0 * ka / M_PI * sum);
}


double brassUnflangedGain(double ka, double theta) {
	if (ka < 1e-4)
		return 1.0;
	const double r = brassUnflangedReflection(ka);
	const double sine = std::fabs(std::sin(theta)), cosine = std::cos(theta);
	const double s = ka * sine;
	// The first factor, 4 / (pi sin^2) J1(s) / sqrt(J1^2 + N1^2), whose limit at the axis is ka^2.
	double first;
	if (s < 1e-6)
		first = ka * ka;
	else {
		const double j = brassJ1(s), y = brassY1(s);
		first = 4.0 / (M_PI * sine * sine) * j / std::sqrt(j * j + y * y);
	}
	// The principal value, int_0^(pi/2) (h(x) - h(s)) / (x^2 - s^2) dphi with h(x) = x delta(x): the part
	// taken off integrates to nought over the whole range.
	const double hs = s * delta(s);
	double sum = 0.0;
	for (int i = 0; i < POINTS; i++) {
		const double x = ka * std::sin(0.5 * M_PI * ((double) i + 0.5) / POINTS);
		const double d = x * x - s * s;
		if (std::fabs(d) < 1e-9 * ka * ka) {
			// The limit, h'(s) / (2 s), by a small difference.
			const double e = 1e-5 * ka;
			sum += ((s + e) * delta(s + e) - (s - e) * delta(s - e)) / (2.0 * e) / (2.0 * std::max(s, 1e-12));
		}
		else
			sum += (x * delta(x) - hs) / d;
	}
	sum *= 0.5 * M_PI / POINTS;
	return first * r / (1.0 - r * r) * std::exp(2.0 * ka * cosine / M_PI * sum);
}


/** A PISTON'S PATTERN WITH KIRCHHOFF'S OBLIQUITY, unscaled. */
static double piston(double ka, double theta) {
	const double s = ka * std::sin(theta);
	const double d = s < 1e-8 ? 1.0 : 2.0 * brassJ1(s) / s;
	const double v = 0.5 * (1.0 + std::cos(theta)) * d;
	return v * v;
}


/** The piston's pattern scaled to radiate all the power: half the integral over theta of it times sin theta
is one. */
static double pistonGain(double ka, double theta) {
	const int n = std::max(400, (int) (40.0 * ka));
	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		const double t = M_PI * ((double) i + 0.5) / n;
		sum += piston(ka, t) * std::sin(t);
	}
	sum *= 0.5 * M_PI / n;
	return piston(ka, theta) / sum;
}


static const double MIX_FROM = 3.4, MIX_TO = 3.83;

static double mixing(double ka) {
	const double w = std::min(1.0, std::max(0.0, (ka - MIX_FROM) / (MIX_TO - MIX_FROM)));
	return w * w * (3.0 - 2.0 * w);
}


static double bellGainAt(double ka, double theta) {
	if (ka <= MIX_FROM)
		return brassUnflangedGain(ka, theta);
	if (ka >= MIX_TO)
		return pistonGain(ka, theta);
	const double w = mixing(ka);
	return (1.0 - w) * brassUnflangedGain(ka, theta) + w * pistonGain(ka, theta);
}


double brassBellGain(double ka, double theta, double spread) {
	if (spread <= 0.0)
		return bellGainAt(ka, theta);
	// The mean over the angles within `spread` of theta, each weighted by the share of the sphere around it.
	const int n = 9;
	double sum = 0.0, weights = 0.0;
	for (int i = 0; i < n; i++) {
		const double t = theta + spread * (2.0 * ((double) i + 0.5) / n - 1.0);
		const double w = std::fabs(std::sin(t)) + 1e-3;
		sum += w * bellGainAt(ka, std::fabs(t));
		weights += w;
	}
	return sum / weights;
}


double brassBellResistance(double ka) {
	if (ka <= 0.0)
		return 0.0;
	const double kaThere = std::min(ka, MIX_TO);
	const double r = brassUnflangedReflection(kaThere);
	// Silva et al.'s end correction over the radius.
	const double sn = std::sin(2.0 * kaThere);
	const double endOverA = 0.6133 * (1.0 + 0.044 * kaThere * kaThere) / (1.0 + 0.19 * kaThere * kaThere) - 0.02 * sn * sn;
	const double c2 = std::cos(2.0 * kaThere * endOverA);
	const double unflanged = (1.0 - r * r) / (1.0 + 2.0 * r * c2 + r * r);
	const double w = mixing(ka);
	return (1.0 - w) * unflanged + w;
}


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


void BrassListenerFilter::design(const BrassGrid::Setup& bell, const BrassListener& where) {
	rate = bell.rate;
	// ONE DESIGN FOR EVERY VOICE ALIKE: the same bell, rate and listener give the same filter, and designing
	// it takes most of a second, so each is kept once made.
	double length = 0.0;
	for (const BrassSection& sec : bell.bore.sections)
		length += sec.length;
	const auto key = std::make_tuple(bell.rate, bell.air.c, bell.bore.mouthRadius(), length, (int) bell.bore.sections.size(), where.angle,
		where.distance, where.spread, where.smallSource);
	static std::mutex lock;
	static std::map<decltype(key), std::vector<double>> made;
	{
		std::lock_guard<std::mutex> hold(lock);
		const auto found = made.find(key);
		if (found != made.end()) {
			h = found->second;
			history.assign(2 * h.size(), 0.0);
			at = 0;
			return;
		}
	}
	designAnew(bell, where);
	std::lock_guard<std::mutex> hold(lock);
	made[key] = h;
}


void BrassListenerFilter::designAnew(const BrassGrid::Setup& bell, const BrassListener& where) {
	const BrassAir& air = bell.air;
	const double a = bell.bore.mouthRadius();
	// THE BELL'S OWN RESPONSE: an impulse into its port, the flow at its mouth. The port takes away what the
	// bell sends back, so the response is short.
	size_t n = 1;
	while ((double) n < 0.25 * rate)
		n *= 2;
	const size_t m = 2 * n;
	BrassGrid grid;
	grid.setup(bell);
	std::vector<cd> flow(m, 0.0);
	for (size_t i = 0; i < n; i++) {
		grid.arrive(i == 0 ? 1.0 : 0.0, 0.0);
		grid.step(0.0);
		flow[i] = grid.radiatedFlow();
	}
	transform(flow, false);
	// THE RADIATION, flow at the mouth to pressure at the listener: rho / (4 pi r) times the flow's rate of
	// change — taken as the difference from one step to the next, which keeps the response short — times
	// what the bell does besides, sqrt(G Re(Z_rad) / (rho c k^2 / (4 pi))): one while ka is small, smooth and
	// never nought, given the least phase its magnitude allows by folding its cepstrum. The difference's own
	// fall towards half the rate is taken out of it.
	const double zc = air.rho * air.c / (M_PI * a * a);
	std::vector<double> besides(m / 2 + 1, 1.0);
	const size_t every = 4;
	auto exact = [&](size_t k) {
		const double hz = (double) k * rate / (double) m, omega = 2.0 * M_PI * hz;
		const double difference = 2.0 * std::sin(0.5 * omega / rate) * rate;
		const double droop = k == 0 ? 1.0 : omega / difference;
		if (where.smallSource || k == 0)
			return droop;
		const double ka = omega / air.c * a;
		const double g = brassBellGain(ka, where.angle, where.spread), res = brassBellResistance(ka);
		const double small = air.rho * air.c * omega * omega / (air.c * air.c) / (4.0 * M_PI);
		return std::sqrt(g * res * zc / small) * droop;
	};
	for (size_t k = 0; k <= m / 2; k += every)
		besides[k] = exact(k);
	besides[m / 2] = exact(m / 2);
	for (size_t k = 0; k < m / 2; k++)
		if (k % every) {
			const size_t k0 = k - k % every, k1 = std::min(m / 2, k0 + every);
			const double u = (double) (k - k0) / (double) (k1 - k0);
			besides[k] = besides[k0] + u * (besides[k1] - besides[k0]);
		}
	double top = 0.0;
	for (double v : besides)
		top = std::max(top, v);
	std::vector<cd> c(m, 0.0);
	for (size_t k = 0; k <= m / 2; k++) {
		c[k] = std::log(std::max(besides[k], 1e-4 * top));
		if (k > 0 && k < m / 2)
			c[m - k] = c[k];
	}
	transform(c, true);
	for (size_t i = 0; i < m; i++)
		c[i] /= (double) m;
	for (size_t i = 1; i < m / 2; i++)
		c[i] *= 2.0;
	for (size_t i = m / 2 + 1; i < m; i++)
		c[i] = 0.0;
	transform(c, false);
	// THE THREE TOGETHER, back to time.
	std::vector<cd> total(m);
	const double scale = air.rho / (4.0 * M_PI * where.distance) * rate;
	for (size_t i = 0; i < m; i++) {
		const cd difference = 1.0 - std::polar(1.0, -2.0 * M_PI * (double) i / (double) m);
		total[i] = flow[i] * difference * scale * std::exp(c[i]);
	}
	transform(total, true);
	std::vector<double> response(m);
	double energy = 0.0;
	for (size_t i = 0; i < m; i++) {
		response[i] = total[i].real() / (double) m;
		energy += response[i] * response[i];
	}
	// AS SHORT AS LEAVES UNDER 60 dB OF ITS ENERGY BEHIND, the last eighth faded.
	size_t length = m;
	double tail = 0.0;
	while (length > 64 && tail + response[length - 1] * response[length - 1] < 1e-6 * energy) {
		tail += response[length - 1] * response[length - 1];
		length--;
	}
	length = std::min(length, (size_t) 4096);
	h.assign(response.begin(), response.begin() + (long) length);
	const size_t fade = length / 8;
	for (size_t i = 0; i < fade; i++)
		h[length - 1 - i] *= 0.5 - 0.5 * std::cos(M_PI * (double) i / (double) fade);
	// The taps reversed, oldest first, to meet the history in the order it is kept.
	std::reverse(h.begin(), h.end());
	history.assign(2 * length, 0.0);
	at = 0;
}


double BrassListenerFilter::process(double x) {
	// EACH SAMPLE IS KEPT TWICE, n apart, so that the last n are always in one run: history[at + 1] to
	// history[at + n], oldest first.
	const int n = (int) h.size();
	at = at + 1 == n ? 0 : at + 1;
	history[(size_t) at] = x;
	history[(size_t) (at + n)] = x;
	const double* past = &history[(size_t) (at + 1)];
	const double* taps = h.data();
	double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
	int i = 0;
	for (; i + 4 <= n; i += 4) {
		s0 += taps[i] * past[i];
		s1 += taps[i + 1] * past[i + 1];
		s2 += taps[i + 2] * past[i + 2];
		s3 += taps[i + 3] * past[i + 3];
	}
	for (; i < n; i++)
		s0 += taps[i] * past[i];
	return (s0 + s1) + (s2 + s3);
}


double BrassListenerFilter::magnitude(double hz) const {
	// h is kept oldest first: its last tap is the newest sample's.
	cd sum = 0.0;
	const size_t n = h.size();
	for (size_t i = 0; i < n; i++)
		sum += h[n - 1 - i] * std::polar(1.0, -2.0 * M_PI * hz / rate * (double) i);
	return std::abs(sum);
}


} // namespace px
