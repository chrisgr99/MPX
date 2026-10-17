#pragma once
/** WHAT THE TROMBONE'S BENCHES SHARE (test/brasssound.cpp, test/brasstables.cpp): the trombone and its lips
stepped together, a held note at a pressure with the lips tuned to sound it 1.1 per cent over its resonance
as players play it (B2), and the measures test/brassref.cpp takes of the recordings. */
#include "../src/BrassBore.hpp"
#include "../src/BrassLips.hpp"
#include "../src/BrassRadiation.hpp"
#include "../src/BrassWaveguide.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

using namespace px;

/** The rate the benches run at; each sets its own before playing. */
inline double RATE = 44100.0;
/** The tuning slide's pull, metres: 33 mm matches the Conn 8H's resonances (B1); a player tunes further. */
inline double PULL = 0.033;
/** The slide's positions, metres out from first (docs/brass.md, phase 2). */
inline const double POSITIONS[7] = {0.0, 0.083, 0.170, 0.262, 0.358, 0.458, 0.564};

inline double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

/** THE BORE'S RESONANCES with the slide `extension` out. */
inline std::vector<double> resonances(double extension) {
	BrassBore b = BrassBore::tromboneTimed(extension, PULL);
	b.radiation = BrassRadiation::PARALLEL_RL;
	const BrassAir air(20.0);
	std::vector<double> f, m, hz, mag;
	for (double x = 15.0; x <= 1100.0; x += 0.1) {
		f.push_back(x);
		m.push_back(std::abs(b.impedance(x, air)));
	}
	brassPeaks(f, m, &hz, &mag);
	return hz;
}

/** THE TROMBONE AND ITS LIPS, stepped together; what enters the bell is kept for listening from anywhere. */
struct Player {
	BrassAir air = BrassAir(20.0);
	BrassWaveguide column;
	BrassHingedLips lips;
	void setup(double extension) {
		BrassWaveguide::Setup s;
		s.air = air;
		s.rate = RATE;
		s.tuningPull = PULL;
		s.extension = extension;
		column.setup(s);
		lips.setup(BrassHingedLips::Shape(), air, RATE);
	}
	double step(double* intoBell, double* heard) {
		double without = 0.0, perFlow = 0.0;
		column.begin(&without, &perFlow);
		const double cup = column.end(lips.step(column.cupPressure(), without, perFlow));
		*intoBell = column.bellInput();
		*heard = column.listenerPressure();
		return cup;
	}
};

/** THE PITCH of the cup's pressure over its last `n` samples: rising crossings of its mean. */
inline double pitchOf(const std::vector<double>& x, size_t n) {
	const size_t from = x.size() - n;
	double mean = 0.0;
	for (size_t i = from; i < x.size(); i++)
		mean += x[i];
	mean /= (double) n;
	double first = -1.0, last = -1.0;
	int count = 0;
	for (size_t i = from + 1; i < x.size(); i++) {
		const double a = x[i - 1] - mean, b = x[i] - mean;
		if (a < 0.0 && b >= 0.0) {
			const double at = (double) (i - 1) + a / (a - b);
			if (first < 0.0)
				first = at;
			last = at;
			count++;
		}
	}
	return count >= 3 ? RATE * (count - 1) / (last - first) : 0.0;
}

/** ONE HELD NOTE: 1.2 s at `mouth` pascals after a 20 ms rise, the lips at `lipHz`. */
struct Take {
	bool sounds = false;
	double f0 = 0.0, level = -300.0;
	std::vector<double> intoBell, heard, cup;
};

inline Take play(double extension, double resonance, double lipHz, double mouth, double seconds = 1.2) {
	Player p;
	p.setup(extension);
	p.lips.setResonance(lipHz);
	const size_t n = (size_t) (seconds * RATE);
	std::vector<double> cup(n);
	Take t;
	t.intoBell.resize(n);
	t.heard.resize(n);
	double power = 0.0, lo = 1e18, hi = -1e18;
	const size_t last = (size_t) (0.4 * RATE);
	for (size_t i = 0; i < n; i++) {
		p.lips.setMouthPressure(mouth * std::fmin(1.0, (double) i / (0.02 * RATE)));
		double heard = 0.0;
		cup[i] = p.step(&t.intoBell[i], &heard);
		t.heard[i] = heard;
		if (i >= n - last) {
			power += heard * heard;
			lo = std::min(lo, cup[i]);
			hi = std::max(hi, cup[i]);
		}
	}
	t.f0 = pitchOf(cup, last);
	t.cup = cup;
	t.level = 10.0 * std::log10(power / (double) last / (20e-6 * 20e-6) + 1e-30);
	t.sounds = t.f0 > 0.0 && std::fabs(cents(t.f0, resonance)) < 150.0 && hi - lo > 100.0;
	return t;
}

/** THE LIPS THAT SOUND THE NOTE 1.1 PER CENT OVER ITS RESONANCE at `mouth`, as a share of the resonance. */
inline double tunedLips(double extension, double resonance, double mouth, double over = 0.011) {
	double lo = 0.70, hi = 1.06;
	for (int i = 0; i < 9; i++) {
		const double ratio = 0.5 * (lo + hi);
		const Take t = play(extension, resonance, ratio * resonance, mouth, 0.8);
		const bool high = t.sounds ? t.f0 > (1.0 + over) * resonance : ratio > 0.9;
		(high ? hi : lo) = ratio;
	}
	return 0.5 * (lo + hi);
}

/** A NOTE AT ONE PRESSURE, the lips tuned. */
struct Played {
	double mouth = 0.0, lips = 0.0;
	Take take;
};

inline Played at(double extension, double resonance, double mouth) {
	Played p;
	p.mouth = mouth;
	p.lips = tunedLips(extension, resonance, mouth);
	p.take = play(extension, resonance, p.lips * resonance, mouth);
	return p;
}

/** THE MEASURES OF test/brassref.cpp, from a note's last 0.37 s heard through `filter`. */
struct Measures {
	double centroidOverF0 = 0.0, slopeBelow = NAN, slopeAbove = NAN, slopeAbove40 = NAN, aboveDb = 0.0;
};

inline void fft(std::vector<std::complex<double>>& a) {
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
		const std::complex<double> w = std::polar(1.0, -2.0 * M_PI / (double) len);
		for (size_t i = 0; i < n; i += len) {
			std::complex<double> turn = 1.0;
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * turn;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				turn *= w;
			}
		}
	}
}

/** The slope of level against octave over the harmonics in [lo, hi) at or above `floor` dB, by least
squares; NAN where fewer than three. */
inline double slopeOf(const std::vector<double>& h, double f0, double lo, double hi, double floor) {
	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	int n = 0;
	for (size_t k = 0; k < h.size(); k++) {
		const double f = f0 * (double) (k + 1);
		if (f < lo || f >= hi || h[k] < floor)
			continue;
		const double x = std::log2(f);
		sx += x; sy += h[k]; sxx += x * x; sxy += x * h[k];
		n++;
	}
	return n < 3 ? NAN : (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

inline Measures measure(const std::vector<double>& x, double f0) {
	const size_t n = 16384;
	std::vector<std::complex<double>> spec(2 * n, 0.0);
	const size_t from = x.size() - n;
	for (size_t i = 0; i < n; i++)
		spec[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n));
	fft(spec);
	const double binHz = RATE / (double) spec.size();
	std::vector<double> h;
	double strongest = -1e9;
	for (int k = 1; (double) k * f0 < 10000.0; k++) {
		const double want = (double) k * f0;
		const size_t lo = (size_t) std::floor(want * 0.97 / binHz), hi = (size_t) std::ceil(want * 1.03 / binHz);
		size_t best = lo;
		for (size_t i = lo; i <= hi && i + 1 < spec.size() / 2; i++)
			if (std::abs(spec[i]) > std::abs(spec[best]))
				best = i;
		h.push_back(20.0 * std::log10(std::abs(spec[best]) + 1e-12));
		strongest = std::max(strongest, h.back());
	}
	Measures m;
	double sumA = 0.0, sumFA = 0.0, all = 0.0, above = 0.0;
	for (size_t k = 0; k < h.size(); k++) {
		h[k] -= strongest;
		const double amp = std::pow(10.0, h[k] / 20.0), f = f0 * (double) (k + 1);
		sumA += amp;
		sumFA += f * amp;
		all += amp * amp;
		if (f >= 1000.0)
			above += amp * amp;
	}
	m.centroidOverF0 = sumFA / sumA / f0;
	m.aboveDb = 10.0 * std::log10((above + 1e-12) / (all + 1e-12));
	m.slopeBelow = slopeOf(h, f0, 0.0, 1000.0, -1e9);
	m.slopeAbove = slopeOf(h, f0, 1000.0, 5000.0, -1e9);
	m.slopeAbove40 = slopeOf(h, f0, 1000.0, 5000.0, -40.0);
	return m;
}

/** THE SLOPES OF THE RECORDINGS' OWN HARMONICS within 40 dB of the strongest, from the table's columns. */
struct IowaRow {
	int midi = 0;
	std::string dynamic;
	double f0 = 0.0;
	Measures m;
};

inline std::vector<IowaRow> readIowa(const char* path) {
	std::vector<IowaRow> rows;
	std::ifstream in(path);
	std::string line;
	std::getline(in, line);
	while (std::getline(in, line)) {
		std::vector<std::string> cells;
		std::stringstream ss(line);
		std::string cell;
		while (std::getline(ss, cell, '\t'))
			cells.push_back(cell);
		if (cells.size() < 13)
			continue;
		IowaRow r;
		r.midi = std::atoi(cells[1].c_str());
		r.dynamic = cells[2];
		r.f0 = std::atof(cells[3].c_str());
		r.m.centroidOverF0 = std::atof(cells[9].c_str());
		r.m.slopeBelow = cells[10].empty() ? NAN : std::atof(cells[10].c_str());
		r.m.slopeAbove = cells[11].empty() ? NAN : std::atof(cells[11].c_str());
		r.m.aboveDb = std::atof(cells[12].c_str());
		std::vector<double> h;
		for (size_t i = 13; i < cells.size(); i++)
			h.push_back(cells[i].empty() ? -300.0 : std::atof(cells[i].c_str()));
		r.m.slopeAbove40 = slopeOf(h, r.f0, 1000.0, 5000.0, -40.0);
		rows.push_back(r);
	}
	return rows;
}

inline double median(std::vector<double> v) {
	v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return std::isnan(x); }), v.end());
	if (v.empty())
		return NAN;
	std::sort(v.begin(), v.end());
	return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

inline const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline std::string noteName(int midi) { return std::string(NAMES[midi % 12]) + std::to_string(midi / 12 - 1); }

} // namespace bench
