/** See BowedBody.hpp. */
#include "BowedBody.hpp"

#include <cmath>

namespace px {


static const double PI = 3.14159265358979323846;


void BowedBody::setSampleRate(float hz) {
	if (hz > 0.f && (double) hz != rate) {
		rate = hz;
		build();
	}
}


void BowedBody::setDesign(const Design& d) {
	wanted = d;
	build();
}


/** A small, repeatable random number generator, so a body is the same body every time. */
static double uniform(uint32_t* state) {
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return (double) *state / 4294967296.0;
}


/** A CURVE'S LEVEL AT A FREQUENCY, as a plain ratio: decibels between points along a logarithmic
frequency scale, flat beyond the ends. Nought where there is no curve. */
static double curveAt(const std::vector<BowedBody::Point>& curve, double hz) {
	if (curve.empty())
		return 0.0;
	double db = curve.front().db;
	if (hz >= curve.back().hz)
		db = curve.back().db;
	else
		for (size_t i = 1; i < curve.size(); i++)
			if (hz < curve[i].hz) {
				const BowedBody::Point& a = curve[i - 1];
				const BowedBody::Point& b = curve[i];
				const double t = hz <= a.hz ? 0.0 : std::log(hz / a.hz) / std::log((double) b.hz / a.hz);
				db = a.db + (b.db - a.db) * t;
				break;
			}
	return std::pow(10.0, db / 20.0);
}


/** BUILDING THE RESONATORS.

The signature modes as given. The dense ones from their statistics: evenly spread at the given
density, each moved off its place on the grid by up to a third of the spacing so the body is not a
comb; their strengths spread as measured modal strengths are, the square root of an exponentially
distributed power, so a few are strong and most are weak; and their signs at random, since a mode
radiates from its own pattern of antinodes and adds to its neighbours or takes from them.

THE LEVEL OF A DENSE MODE is set so that the average over a band comes out at the level asked for.
Each resonator's response at its peak is one, so its power summed over frequency is pi/2 times its
bandwidth, f/Q; with `perHz` of them a hertz, the band's average power is perHz pi/2 f loss times a
mode's own squared strength, and that is solved for the strength. */
void BowedBody::build() {
	built.clear();
	for (const Mode& m : wanted.modes) {
		Resonator r;
		r.hz = m.hz;
		r.q = m.q;
		r.mobility = m.mobility;
		r.radiativity = m.radiativity;
		built.push_back(r);
	}
	const Dense& d = wanted.dense;
	if (d.perHz > 0.f && d.toHz > d.fromHz && d.loss > 0.f) {
		uint32_t seed = d.seed ? d.seed : 1u;
		const int n = (int) std::floor((d.toHz - d.fromHz) * d.perHz);
		const double spacing = 1.0 / d.perHz;
		for (int i = 0; i < n; i++) {
			Resonator r;
			r.hz = d.fromHz + (i + 0.5 + (uniform(&seed) - 0.5) * 0.66) * spacing;
			r.q = 1.0 / d.loss;
			const double level = curveAt(d.radiativity, r.hz);
			const double mobility = curveAt(d.mobility, r.hz);
			const double perMode = 1.0 / std::sqrt(d.perHz * PI / 2.0 * r.hz * d.loss);
			const double spread = std::sqrt(-std::log(std::fmax(1e-9, uniform(&seed))));
			const double sign = uniform(&seed) < 0.5 ? -1.0 : 1.0;
			r.radiativity = sign * level * perMode * spread;
			// A driving point's modes all give way the same way: mobility is never negative.
			r.mobility = mobility * perMode * spread;
			built.push_back(r);
		}
	}
	// The resonators' coefficients: poles at the mode's frequency and bandwidth, zeros at nought
	// and at half the sample rate, and the gain that makes the peak one.
	for (Resonator& r : built) {
		const double hz = std::fmin(r.hz, 0.45 * rate);
		const double w = 2.0 * PI * hz / rate;
		const double radius = std::exp(-PI * (hz / std::fmax(0.5, r.q)) / rate);
		r.a1 = 2.0 * radius * std::cos(w);
		r.a2 = -radius * radius;
		r.b = 1.0;
		double re, im;
		resonance(r, hz, &re, &im);
		const double peak = std::sqrt(re * re + im * im);
		r.b = (peak > 0.0) ? 1.0 / peak : 0.0;
		r.y1 = r.y2 = 0.0;
	}
	// THE BRIDGE: a second-order lowpass with its resonance, by the bilinear transform with the
	// frequency prewarped, so the peak lands where it is asked to.
	const Bridge& br = wanted.bridge;
	if (br.hz > 0.f && br.hz < 0.45 * rate) {
		const double w0 = 2.0 * PI * br.hz / rate;
		const double k = std::tan(w0 / 2.0);
		const double q = std::fmax(0.3, (double) br.q);
		const double norm = 1.0 / (1.0 + k / q + k * k);
		bb0 = k * k * norm;
		bb1 = 2.0 * bb0;
		bb2 = bb0;
		ba1 = 2.0 * (k * k - 1.0) * norm;
		ba2 = (1.0 - k / q + k * k) * norm;
	}
	else {
		bb0 = 1.0;
		bb1 = bb2 = ba1 = ba2 = 0.0;
	}
	silence();
}


void BowedBody::silence() {
	bx1 = bx2 = by1 = by2 = 0.0;
	f1 = f2 = 0.0;
	velocity = 0.0;
	for (Resonator& r : built)
		r.y1 = r.y2 = 0.0;
}


float BowedBody::process(float bridgeForce) {
	// Through the bridge.
	const double x = bridgeForce;
	const double f = bb0 * x + bb1 * bx1 + bb2 * bx2 - ba1 * by1 - ba2 * by2;
	bx2 = bx1;
	bx1 = x;
	by2 = by1;
	by1 = f;
	// Into the modes, every one driven by the same force.
	double sound = 0.0, v = 0.0;
	const double drive = f - f2;
	for (Resonator& r : built) {
		const double y = r.b * drive + r.a1 * r.y1 + r.a2 * r.y2;
		r.y2 = r.y1;
		r.y1 = y;
		sound += r.radiativity * y;
		v += r.mobility * y;
	}
	f2 = f1;
	f1 = f;
	velocity = v;
	return (float) sound;
}


void BowedBody::resonance(const Resonator& r, double hz, double* re, double* im) const {
	const double w = 2.0 * PI * hz / rate;
	// H = b (1 - z^-2) / (1 - a1 z^-1 - a2 z^-2), z = e^jw.
	const double nr = r.b * (1.0 - std::cos(2.0 * w)), ni = r.b * std::sin(2.0 * w);
	const double dr = 1.0 - r.a1 * std::cos(w) - r.a2 * std::cos(2.0 * w);
	const double di = r.a1 * std::sin(w) + r.a2 * std::sin(2.0 * w);
	const double den = dr * dr + di * di;
	*re = (nr * dr + ni * di) / den;
	*im = (ni * dr - nr * di) / den;
}


double BowedBody::bridgeGainAt(double hz) const {
	const double w = 2.0 * PI * hz / rate;
	const double nr = bb0 + bb1 * std::cos(w) + bb2 * std::cos(2.0 * w);
	const double ni = -(bb1 * std::sin(w) + bb2 * std::sin(2.0 * w));
	const double dr = 1.0 + ba1 * std::cos(w) + ba2 * std::cos(2.0 * w);
	const double di = -(ba1 * std::sin(w) + ba2 * std::sin(2.0 * w));
	return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}


float BowedBody::radiativityAt(float hz) const {
	double re = 0.0, im = 0.0;
	for (const Resonator& r : built) {
		double a, b;
		resonance(r, hz, &a, &b);
		re += r.radiativity * a;
		im += r.radiativity * b;
	}
	return (float) (bridgeGainAt(hz) * std::sqrt(re * re + im * im));
}


float BowedBody::mobilityAt(float hz) const {
	double re = 0.0, im = 0.0;
	for (const Resonator& r : built) {
		double a, b;
		resonance(r, hz, &a, &b);
		re += r.mobility * a;
		im += r.mobility * b;
	}
	return (float) (bridgeGainAt(hz) * std::sqrt(re * re + im * im));
}


} // namespace px
