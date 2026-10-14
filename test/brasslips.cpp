/** THE LIPS ON THE AIR COLUMN, from a command line: phases 3 and 4 of docs/brass.md, measured against
research/brass/trombone-benchmark.md, B2 to B6.

    make brasslips                     thresholds, held notes, bending and the pedal
    make brasslips ARGS=sweep          the lips' shape swept against B2 and B4
    make brasslips ARGS="corners corner=0.0001 mass=0.9 breadth=0.014"   one shape of closure against B2 and B4
    make brasslips ARGS=listen         held notes, a slur and the slide moving, to listen to, into build/
    make brasslips ARGS=regular        where the notes stay regular, over the lips and the pressure
    make brasslips ARGS=brass          brassiness against B5 and B6, steepening on and off ("brass 96000": at 96 kHz)
    make brasslips ARGS=aliasing       the power between the harmonics ("aliasing 192000": at 192 kHz)
    make brasslips ARGS=crescendo      a crescendo with and without brassiness, and one note at two rates, into build/

The trombone is BrassWaveguide at 48 kHz, first position, the tuning slide pulled 33 mm, the air at 20 C;
its resonances are BrassBore's calculation for the same geometry and radiation. Each step the column says
what the cup's pressure will be as a straight line in the flow (BrassWaveguide::begin), the lips give the
flow (BrassLips::step), and the column finishes the step.
*/
#include "../src/BrassBore.hpp"
#include "../src/BrassLips.hpp"
#include "../src/BrassWaveguide.hpp"

#include <algorithm>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

using namespace px;

static double RATE = 48000.0;   // 96 kHz with ARGS="brass 96000", to see what aliases
static const double PULL = 0.033;

static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

/** WHICH LIPS: the hinged block (BrassHingedLips) unless asked for the one mass (BrassLips). */
static bool oneMass = false;
static BrassHingedLips::Shape hingedNow;
static bool blendDamping = true;

/** A TROMBONE AND ITS PLAYER'S LIPS, stepped together. */
struct Player {
	BrassAir air = BrassAir(20.0);
	BrassWaveguide column;
	BrassLips lips;
	BrassHingedLips hinged;
	BrassLips::Shape shape;
	void setResonance(double hz) {
		lips.setResonance(hz);
		hinged.setResonance(hz);
	}
	void setMouthPressure(double pa) {
		lips.setMouthPressure(pa);
		hinged.setMouthPressure(pa);
	}
	bool steepening = true;
	BrassListener listener;
	void setup(double extension = 0.0) {
		BrassWaveguide::Setup s;
		s.steepening = steepening;
		s.listener = listener;
		s.air = air;
		s.rate = RATE;
		s.tuningPull = PULL;
		s.extension = extension;
		column.setup(s);
		lips.setup(shape, air, RATE);
		hinged.setup(hingedNow, air, RATE);
		hinged.blendDamping = blendDamping;
	}
	/** One step: the cup's pressure after it; `heard`, the pressure a metre in front of the bell. */
	double step(double* heard = nullptr) {
		double without = 0.0, perFlow = 0.0;
		column.begin(&without, &perFlow);
		const double flow = oneMass ? lips.step(column.cupPressure(), without, perFlow)
			: hinged.step(column.cupPressure(), without, perFlow);
		const double cup = column.end(flow);
		if (heard)
			*heard = column.listenerPressure();
		return cup;
	}
};

/** THE BORE'S RESONANCES for the same geometry and the radiation the column carries. */
static std::vector<double> resonances(double extension) {
	BrassBore b = BrassBore::tromboneTimed(extension, PULL);
	b.radiation = BrassRadiation::PARALLEL_RL;
	const BrassAir air(20.0);
	std::vector<double> f, m, hz, mag;
	for (double x = 15.0; x <= 1000.0; x += 0.05) {
		f.push_back(x);
		m.push_back(std::abs(b.impedance(x, air)));
	}
	brassPeaks(f, m, &hz, &mag);
	return hz;
}

/** THE PITCH of `x` (the cup's pressure) from `from` for `n` samples: rising crossings of its mean, the
mean period between the first and the last. Nought where there are fewer than three. */
static double pitchOf(const std::vector<double>& x, size_t from, size_t n) {
	if (from + n > x.size() || n < 4)
		return 0.0;
	double mean = 0.0;
	for (size_t i = from; i < from + n; i++)
		mean += x[i];
	mean /= (double) n;
	double first = -1.0, last = -1.0;
	int count = 0;
	for (size_t i = from + 1; i < from + n; i++) {
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

/** HOW MUCH THE CUP'S PRESSURE SWINGS from `from` for `n` samples: half its range. */
static double swingOf(const std::vector<double>& x, size_t from, size_t n) {
	double lo = 1e18, hi = -1e18;
	for (size_t i = from; i < from + n && i < x.size(); i++) {
		lo = std::min(lo, x[i]);
		hi = std::max(hi, x[i]);
	}
	return 0.5 * (hi - lo);
}

/** A NOTE STARTED BY A SLOW RISE OF THE MOUTH'S PRESSURE, the lips at `lipHz`: the mouth's pressure at
which the cup first swings more than 50 Pa for 30 ms, and the pitch 0.3 s after; negative where it never
does by `top` pascals. */
static BrassLips::Shape shapeNow;

static double threshold(double lipHz, double top, double* pitch) {
	Player p;
	p.shape = shapeNow;
	p.setup();
	p.setResonance(lipHz);
	const double seconds = top / 2000.0;   // 2 kPa a second
	const size_t n = (size_t) (seconds * RATE);
	std::vector<double> cup(n);
	for (size_t i = 0; i < n; i++) {
		p.setMouthPressure(top * (double) i / (double) n);
		cup[i] = p.step();
	}
	const size_t window = (size_t) (0.03 * RATE);
	for (size_t i = window; i + window < n; i += window / 3) {
		if (swingOf(cup, i - window, window) > 50.0 && swingOf(cup, i, window) > 50.0) {
			const size_t at = i + (size_t) (0.3 * RATE);
			*pitch = at + window * 4 < n ? pitchOf(cup, at, window * 4) : pitchOf(cup, n - window * 4, window * 4);
			return top * (double) (i - window) / (double) n;
		}
	}
	*pitch = 0.0;
	return -1.0;
}

/** A NOTE HELD at `mouth` pascals with the lips at `lipHz`, a second and a half: its pitch, the cup's
swing and the level a metre away over the last half second, decibels of 20 micropascals. */
static void held(double lipHz, double mouth, double* pitch, double* swing, double* level, double extension = 0.0) {
	Player p;
	p.shape = shapeNow;
	p.setup(extension);
	p.setResonance(lipHz);
	const size_t n = (size_t) (1.5 * RATE), last = (size_t) (0.5 * RATE);
	std::vector<double> cup(n);
	double power = 0.0;
	for (size_t i = 0; i < n; i++) {
		p.setMouthPressure(mouth * std::fmin(1.0, (double) i / (0.02 * RATE)));
		double heard = 0.0;
		cup[i] = p.step(&heard);
		if (i >= n - last)
			power += heard * heard;
	}
	*pitch = pitchOf(cup, n - last, last);
	*swing = swingOf(cup, n - last, last);
	*level = 10.0 * std::log10(power / (double) last / (20e-6 * 20e-6) + 1e-30);
}

/** THE LIPS' SHAPE SWEPT: for each, the threshold and where the note sounds on the third and fourth
resonances (F3, B flat 3), the lips' resonance from 0.7 to 1.3 of each. */
static void sweep() {
	const std::vector<double> peaks = resonances(0.0);
	std::printf("quality  mass x  width mm  rest mm   |  F3: threshold Pa, over the resonance %%   |  B flat 3: threshold Pa, over %%\n");
	for (double quality : {1.5, 3.0, 5.0})
		for (double massTimes : {0.5, 1.0, 2.0, 4.0})
			for (double width : {0.010, 0.014})
				for (double rest : {0.29e-3, 0.5e-3}) {
					shapeNow = BrassLips::Shape();
					shapeNow.quality = quality;
					shapeNow.massAtReference *= massTimes;
					shapeNow.width = width;
					shapeNow.restOpening = rest;
					std::printf("%7.1f %7.1f %9.0f %8.2f   |", quality, massTimes, 1000.0 * width, 1000.0 * rest);
					for (size_t r : {2, 3}) {
						double lowest = 1e9, sounds = 0.0;
						for (double ratio = 0.70; ratio <= 1.301; ratio += 0.04) {
							double pitch = 0.0;
							const double t = threshold(ratio * peaks[r], 5000.0, &pitch);
							if (t > 0.0 && pitch > 0.0 && std::fabs(cents(pitch, peaks[r])) < 150.0 && t < lowest) {
								lowest = t;
								sounds = pitch;
							}
						}
						if (lowest < 1e9)
							std::printf("  %6.0f Pa  %+5.2f%%            |", lowest, 100.0 * (sounds / peaks[r] - 1.0));
						else
							std::printf("  none below 5 kPa                |");
					}
					std::printf("\n");
					std::fflush(stdout);
				}
}

/** A WAV, 16 bits, its peak at 1 dB under full scale, or scaled by `gain` where it is given. */
static void writeWav(const char* path, const std::vector<double>& x, double gain = 0.0) {
	double peak = 1e-12;
	for (double v : x)
		peak = std::fmax(peak, std::fabs(v));
	if (gain > 0.0)
		peak = 0.891 / gain;
	FILE* f = std::fopen(path, "wb");
	if (!f)
		return;
	const uint32_t n = (uint32_t) x.size(), bytes = n * 2, rate = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	const double g = 0.891 * 32767.0 / peak;
	for (double v : x) {
		const int16_t sample = (int16_t) std::lround(v * g);
		std::fwrite(&sample, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path);
}

/** THE LIPS TO LISTEN TO, heard a metre in front of the bell: held notes on resonances 2 to 6, each a
second and a half at one and a half times its threshold with the lips at 0.98 of the resonance (0.9 for the
second), started and stopped over 30 ms; a slur up resonances 3, 4, 5 and 6 at 2 kPa, a second each, the
lips gliding to the next over 80 ms; and the slide moving from first to fourth position and back on
resonance 4, the lips following it. */
static void listen() {
	const std::vector<double> peaks = resonances(0.0);
	std::vector<double> out;
	auto play = [&](Player& p, double lipHz, double mouth, double seconds, double rampIn, double rampOut,
	                const std::function<void(Player&, double)>& each) {
		const size_t n = (size_t) (seconds * RATE);
		for (size_t i = 0; i < n; i++) {
			const double t = (double) i / RATE;
			const double env = std::fmin(1.0, std::fmin(rampIn > 0 ? t / rampIn : 1.0, rampOut > 0 ? (seconds - t) / rampOut : 1.0));
			p.setResonance(lipHz);
			p.setMouthPressure(mouth * std::fmax(0.0, env));
			if (each)
				each(p, t);
			double heard = 0.0;
			p.step(&heard);
			out.push_back(heard);
		}
	};
	// HELD NOTES.
	for (size_t r = 1; r <= 5; r++) {
		const double lip = (r == 1 ? 0.9 : 0.98) * peaks[r];
		double pitch = 0.0;
		double t = threshold(lip, 8000.0, &pitch);
		if (t < 0.0)
			t = 3000.0;
		Player p;
		p.setup();
		play(p, lip, 1.5 * t, 1.5, 0.03, 0.03, nullptr);
		for (int i = 0; i < (int) (0.3 * RATE); i++)
			out.push_back(0.0);
	}
	writeWav("build/Trombone lips, held notes on resonances 2 to 6.wav", out);
	// A SLUR at one pressure, the lips gliding over 80 ms from each resonance's lips to the next's.
	out.clear();
	{
		Player p;
		p.setup();
		const double mouth = 2000.0;
		const double seconds = 4.5;
		play(p, 0.98 * peaks[2], mouth, seconds, 0.03, 0.03, [&](Player& q, double t) {
			const double place = std::fmin(3.0, std::fmax(0.0, t - 1.0));   // a new resonance each second from 1 s
			const int from = (int) std::floor(place);
			const double along = std::fmin(1.0, (place - from) / 0.08);
			const double a = 0.98 * peaks[(size_t) (2 + from)], b = 0.98 * peaks[(size_t) std::min(5, 3 + from)];
			q.setResonance(a * std::pow(b / a, std::fmin(1.0, along)));
		});
	}
	writeWav("build/Trombone lips, slurred up resonances 3 to 6.wav", out);
	// THE SLIDE out to fourth position and back on resonance 4, the lips following the resonance.
	out.clear();
	{
		Player p;
		p.setup();
		const std::vector<double> fourth = resonances(0.262);
		const double mouth = 3000.0, seconds = 5.0;
		play(p, 0.98 * peaks[3], mouth, seconds, 0.03, 0.03, [&](Player& q, double t) {
			double e = 0.0;
			if (t > 1.0 && t <= 2.0)
				e = 0.262 * (0.5 - 0.5 * std::cos(M_PI * (t - 1.0)));
			else if (t > 2.0 && t <= 3.0)
				e = 0.262;
			else if (t > 3.0 && t <= 4.0)
				e = 0.262 * (0.5 + 0.5 * std::cos(M_PI * (t - 3.0)));
			if (((int) (t * RATE)) % 32 == 0)
				q.column.setExtension(e);
			const double u = e / 0.262;
			q.setResonance(0.98 * peaks[3] * std::pow(fourth[3] / peaks[3], u));
		});
	}
	writeWav("build/Trombone lips, slide out to fourth position and back.wav", out);
}

/** A HELD NOTE'S SPECTRUM: the amplitudes of its harmonics up to 10 kHz, decibels, from a Hann-windowed
DFT of `x` over its last half second at each multiple of `f0`. */
static std::vector<double> harmonicsOf(const std::vector<double>& x, double f0) {
	const size_t n = (size_t) (0.5 * RATE), from = x.size() - n;
	std::vector<double> out;
	for (int k = 1; k * f0 < 10000.0; k++) {
		const double w = 2.0 * M_PI * k * f0 / RATE;
		double re = 0.0, im = 0.0;
		for (size_t i = 0; i < n; i++) {
			const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
			re += x[from + i] * win * std::cos(w * (double) i);
			im -= x[from + i] * win * std::sin(w * (double) i);
		}
		// The window's mean is a half, and a real sine's amplitude is shared between two bins.
		out.push_back(10.0 * std::log10((re * re + im * im) * 16.0 / ((double) n * (double) n) + 1e-30));
	}
	return out;
}

/** THE MEASURES OF test/brassref.cpp: centroid over the pitch, share over 1 kHz in decibels, slope over
1 kHz in decibels an octave. */
static void measures(const std::vector<double>& h, double f0, double* centroidRatio, double* above, double* slopeAbove) {
	double sumA = 0.0, sumFA = 0.0, all = 0.0, high = 0.0;
	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	int count = 0;
	for (size_t k = 0; k < h.size(); k++) {
		const double f = f0 * (double) (k + 1), amp = std::pow(10.0, h[k] / 20.0);
		sumA += amp;
		sumFA += f * amp;
		all += amp * amp;
		if (f >= 1000.0)
			high += amp * amp;
		if (f >= 1000.0 && f < 5000.0) {
			const double lx = std::log2(f);
			sx += lx; sy += h[k]; sxx += lx * lx; sxy += lx * h[k];
			count++;
		}
	}
	*centroidRatio = sumFA / sumA / f0;
	*above = 10.0 * std::log10(high / all + 1e-30);
	*slopeAbove = count >= 3 ? (count * sxy - sx * sy) / (count * sxx - sx * sx) : 0.0;
}

/** WHAT IS NOT A HARMONIC: of the power of `x`'s last half second below 20 kHz, the share in decibels more
than a tenth of `f0` from every harmonic — aliases of what the steepened wave carries above half the rate
fall between the harmonics, since the period is not a whole number of samples. A Hann window and a radix-2
transform of the half second zero-padded. */
static double offHarmonic(const std::vector<double>& x, double f0, double bottom = 0.0, double top = 20000.0) {
	const size_t n = (size_t) (0.5 * RATE), from = x.size() - n;
	size_t size = 1;
	while (size < n)
		size *= 2;
	std::vector<std::complex<double>> a(size, 0.0);
	for (size_t i = 0; i < n; i++)
		a[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n));
	for (size_t i = 1, j = 0; i < size; i++) {
		size_t bit = size >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= size; len *= 2) {
		const std::complex<double> w = std::polar(1.0, -2.0 * M_PI / (double) len);
		for (size_t i = 0; i < size; i += len) {
			std::complex<double> turn = 1.0;
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * turn;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				turn *= w;
			}
		}
	}
	double on = 0.0, off = 0.0;
	for (size_t k = 1; k < size / 2; k++) {
		const double f = (double) k * RATE / (double) size;
		if (f > top)
			break;
		if (f < bottom)
			continue;
		const double h = f / f0, near = std::fabs(h - std::round(h)) * f0;
		(near > 0.1 * f0 ? off : on) += std::norm(a[k]);
	}
	return 10.0 * std::log10(off / (on + off) + 1e-30);
}

/** HOW REGULAR the cup's pressure is over its last half second: the highest correlation of it with itself
moved by a period between 0.8 and 1.25 of `periodNear` samples, and that period. One for a note that
repeats exactly. */
static double regularity(const std::vector<double>& cup, double periodNear, double* period) {
	const size_t n = (size_t) (0.5 * RATE), from = cup.size() - n;
	double mean = 0.0;
	for (size_t i = from; i < cup.size(); i++)
		mean += cup[i];
	mean /= (double) n;
	double best = -1.0;
	for (int lag = (int) (0.8 * periodNear); lag <= (int) (1.25 * periodNear); lag++) {
		double s = 0.0, p = 0.0, q = 0.0;
		for (size_t i = from; i + (size_t) lag < cup.size(); i++) {
			const double a = cup[i] - mean, b = cup[i + (size_t) lag] - mean;
			s += a * b; p += a * a; q += b * b;
		}
		const double c = s / std::sqrt(p * q + 1e-30);
		if (c > best) {
			best = c;
			*period = lag;
		}
	}
	return best;
}

/** WHERE THE NOTES STAY REGULAR: for resonances 3 to 5, the lips from 0.86 to 1.10 of the resonance and the
mouth's pressure from 800 Pa to 6.4 kPa, where the note sounds, per cent over the resonance, starred where it
is irregular (regularity under 0.95), or a dot where no note on that resonance sounds. */
static void regular() {
	const std::vector<double> peaks = resonances(0.0);
	for (size_t r = 2; r <= 4; r++) {
		std::printf("\nresonance %zu, %.1f Hz: rows the lips over the resonance, columns the mouth's pressure in Pa\n      ", r + 1, peaks[r]);
		for (double mouth = 800.0; mouth <= 6500.0; mouth *= std::sqrt(2.0))
			std::printf("%7.0f", mouth);
		std::printf("\n");
		for (double ratio = 0.86; ratio <= 1.101; ratio += 0.02) {
			std::printf("  %.2f", ratio);
			for (double mouth = 800.0; mouth <= 6500.0; mouth *= std::sqrt(2.0)) {
				Player p;
				p.setup();
				p.setResonance(ratio * peaks[r]);
				const size_t n = (size_t) (1.2 * RATE);
				std::vector<double> cup(n);
				for (size_t i = 0; i < n; i++) {
					p.setMouthPressure(mouth * std::fmin(1.0, (double) i / (0.02 * RATE)));
					cup[i] = p.step();
				}
				const double f0 = pitchOf(cup, n - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE));
				if (!(f0 > 0.0) || std::fabs(cents(f0, peaks[r])) > 150.0 || swingOf(cup, n - 4800, 4800) < 50.0) {
					std::printf("      .");
					continue;
				}
				double period = 0.0;
				const double c = regularity(cup, RATE / peaks[r], &period);
				std::printf(" %+5.1f%s", 100.0 * (f0 / peaks[r] - 1.0), c < 0.95 ? "*" : " ");
			}
			std::printf("\n");
		}
	}
}

/** THE LIPS A PLAYER WOULD USE: for resonance `r` at `mouth` pascals, the lips' resonance, as a share of
the bore's, at which the note sounds 1.1 per cent over the bore's resonance, as trombonists play (B2) —
found by halving between 0.70 and 1.02 of it, the steepening off. Sounding no note on the resonance counts
as too high at the top of the range and too low at the bottom. */
static double tunedLips(const std::vector<double>& peaks, size_t r, double mouth) {
	double lo = 0.70, hi = 1.02;
	for (int i = 0; i < 9; i++) {
		const double ratio = 0.5 * (lo + hi);
		Player p;
		p.steepening = false;
		p.setup();
		p.setResonance(ratio * peaks[r]);
		const size_t n = (size_t) (1.0 * RATE);
		std::vector<double> cup(n);
		for (size_t k = 0; k < n; k++) {
			p.setMouthPressure(mouth * std::fmin(1.0, (double) k / (0.02 * RATE)));
			cup[k] = p.step();
		}
		const double f0 = pitchOf(cup, n - (size_t) (0.4 * RATE), (size_t) (0.4 * RATE));
		const bool sounds = f0 > 0.0 && std::fabs(cents(f0, peaks[r])) < 150.0 && swingOf(cup, n - 4800, 4800) > 50.0;
		const bool high = sounds ? f0 > 1.011 * peaks[r] : ratio > 0.9;
		(high ? hi : lo) = ratio;
	}
	return 0.5 * (lo + hi);
}

/** BRASSINESS TO LISTEN TO. A crescendo on B flat 3, the mouth's pressure rising from 800 Pa to 5.4 kPa over
six seconds and the lips following the tuning at each pressure (tunedLips), with the steepening and
without, at one gain, so that the two are as loud as the physics makes them. Then a loud B flat 3, 3.8 kPa
for two seconds with the steepening, computed at 48 kHz and at 192 kHz and set to one loudness: what the
first has and the second has not is aliasing. */
static void crescendo() {
	const std::vector<double> peaks = resonances(0.0);
	const size_t r = 3;
	std::vector<double> pressures, lips;
	for (double mouth = 673.0; mouth <= 6500.0; mouth *= std::pow(2.0, 0.25)) {
		pressures.push_back(mouth);
		lips.push_back(tunedLips(peaks, r, mouth));
	}
	auto lipsAt = [&](double mouth) {
		if (mouth <= pressures.front())
			return lips.front();
		for (size_t i = 0; i + 1 < pressures.size(); i++)
			if (mouth <= pressures[i + 1]) {
				const double u = std::log(mouth / pressures[i]) / std::log(pressures[i + 1] / pressures[i]);
				return lips[i] + u * (lips[i + 1] - lips[i]);
			}
		return lips.back();
	};
	std::vector<double> takes[2];
	double loudest = 1e-12;
	for (int steep : {1, 0}) {
		Player p;
		p.steepening = steep == 1;
		p.setup();
		const double seconds = 6.5;
		const size_t n = (size_t) (seconds * RATE);
		for (size_t i = 0; i < n; i++) {
			const double t = (double) i / RATE;
			const double mouth = 800.0 * std::pow(5382.0 / 800.0, std::fmin(1.0, t / 6.0));
			const double env = std::fmin(1.0, std::fmin(t / 0.03, (seconds - t) / 0.03));
			p.setResonance(lipsAt(mouth) * peaks[r]);
			p.setMouthPressure(mouth * std::fmax(0.0, env));
			double heard = 0.0;
			p.step(&heard);
			takes[steep].push_back(heard);
			loudest = std::fmax(loudest, std::fabs(heard));
		}
	}
	writeWav("build/Trombone crescendo on B flat 3, with brassiness.wav", takes[1], 0.891 / loudest);
	writeWav("build/Trombone crescendo on B flat 3, without brassiness.wav", takes[0], 0.891 / loudest);
	// THE SAME LOUD NOTE AT TWO RATES, set to one loudness by the power of their harmonics under 10 kHz (the
	// sharper fronts at 192 kHz carry power above 24 kHz that no one hears), the louder peak 1 dB under full scale.
	std::vector<double> note[2];
	double rms[2] = {0.0, 0.0}, peak[2] = {0.0, 0.0};
	for (int k = 0; k < 2; k++) {
		RATE = k == 0 ? 48000.0 : 192000.0;
		const std::vector<double> at = resonances(0.0);
		const double lip = tunedLips(at, r, 3805.0);
		Player p;
		p.setup();
		p.setResonance(lip * at[r]);
		const double seconds = 2.0;
		const size_t n = (size_t) (seconds * RATE);
		note[k].resize(n);
		std::vector<double> cup(n);
		for (size_t i = 0; i < n; i++) {
			const double t = (double) i / RATE;
			p.setMouthPressure(3805.0 * std::fmax(0.0, std::fmin(1.0, std::fmin(t / 0.03, (seconds - t) / 0.03))));
			cup[i] = p.step(&note[k][i]);
			peak[k] = std::fmax(peak[k], std::fabs(note[k][i]));
		}
		// The harmonics of the half second before the release.
		const size_t held = n - (size_t) (0.1 * RATE);
		const std::vector<double> part(note[k].begin(), note[k].begin() + (long) held);
		for (double level : harmonicsOf(part, pitchOf(cup, held - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE))))
			rms[k] += 0.5 * std::pow(10.0, level / 10.0);
		rms[k] = std::sqrt(rms[k]);
		std::printf("  at %.0f Hz: pitch %.2f Hz, harmonics under 10 kHz %.1f dB, peak %.1f dB\n", RATE,
			pitchOf(cup, held - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE)), 20.0 * std::log10(rms[k]), 20.0 * std::log10(peak[k]));
	}
	const double target = 0.891 / std::fmax(peak[0] / rms[0], peak[1] / rms[1]);
	RATE = 48000.0;
	writeWav("build/Trombone loud B flat 3, computed at 48 kHz.wav", note[0], target / rms[0]);
	RATE = 192000.0;
	writeWav("build/Trombone loud B flat 3, computed at 192 kHz.wav", note[1], target / rms[1]);
	RATE = 48000.0;
}

/** ALIASING: F3 and B flat 3 at 1.3, 2.7 and 5.4 kPa, the lips tuned, with the steepening and without,
and for each the share of the power off the harmonics in four bands. Run at 48 and at 96 kHz: what is there
at 48 and not at 96 is folded down from above 24 kHz. */
static void aliasing() {
	const std::vector<double> peaks = resonances(0.0);
	std::printf("at %.0f Hz, corners %.2f mm: off the harmonics, dB of each band's power\n", RATE, 1000.0 * hingedNow.cornerHeight);
	std::printf("  note   mouth Pa   steepening   under 2 kHz   2 to 5 kHz   5 to 10 kHz   10 to 20 kHz   lips / resonance   period, samples   regularity   widest mm2   shut\n");
	for (size_t r : {(size_t) 2, (size_t) 3})
		for (double mouth : {1345.0, 2691.0, 5382.0}) {
			const double lip = tunedLips(peaks, r, mouth);
			for (int steep : {0, 1}) {
				Player p;
				p.steepening = steep == 1;
				p.setup();
				p.setResonance(lip * peaks[r]);
				const size_t n = (size_t) (1.5 * RATE);
				std::vector<double> heard(n), cup(n);
				double widest = 0.0, shut = 0.0;
				for (size_t i = 0; i < n; i++) {
					p.setMouthPressure(mouth * std::fmin(1.0, (double) i / (0.02 * RATE)));
					cup[i] = p.step(&heard[i]);
					if (i >= n - (size_t) (0.5 * RATE)) {
						widest = std::fmax(widest, p.hinged.opening());
						shut += p.hinged.opening() <= 0.0 ? 1.0 : 0.0;
					}
				}
				const double f0 = pitchOf(cup, n - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE));
				double period = 0.0;
				const double regular = regularity(cup, RATE / f0, &period);
				std::printf("  %4s %10.0f %12s %13.1f %12.1f %13.1f %14.1f %18.3f %17.2f %12.4f %12.1f %6.2f\n", r == 2 ? "F3" : "Bb3", mouth,
					steep ? "on" : "off", offHarmonic(heard, f0, 0.0, 2000.0), offHarmonic(heard, f0, 2000.0, 5000.0),
					offHarmonic(heard, f0, 5000.0, 10000.0), offHarmonic(heard, f0, 10000.0, 20000.0), lip, RATE / f0, regular, 1e6 * widest,
					shut / (0.5 * RATE));
			}
		}
}

/** BRASSINESS, B5 and B6: F3, B flat 3 and D4 held at mouth pressures rising a quarter octave at a time
from 400 Pa, with the wave to the bell steepening and without. For each, its level a metre in front of the
bell, its centroid over its pitch, its share and slope over 1 kHz (test/brassref.cpp's measures), the
bell's harmonics against the mouthpiece's under 800 Hz and over 1.5 kHz, and the share of its power off
the harmonics. Then the dynamics: mf the pressure nearest the trombonists' 1.3 kPa (B4), pp the note
nearest 13 dB under it and ff the nearest 8 dB over it, as the recordings step; and what the brightness
and the transfer do over those steps against the recordings and Beauchamp. */
static void brass() {
	const std::vector<double> peaks = resonances(0.0);
	const char* names[3] = {"F3", "B flat 3", "D4"};
	double seconds[2] = {0.0, 0.0}, played[2] = {0.0, 0.0};
	std::printf("at %.0f Hz\n", RATE);
	for (size_t note = 0; note < 3; note++) {
		const size_t r = note + 2;
		std::vector<double> pressures, lips;
		for (double mouth = 400.0; mouth <= 8000.0; mouth *= std::pow(2.0, 0.25)) {
			pressures.push_back(mouth);
			lips.push_back(tunedLips(peaks, r, mouth));
		}
		std::printf("\n%s (resonance %zu, %.1f Hz), the lips at each pressure tuned to sound 1.1 per cent over it\n", names[note], r + 1, peaks[r]);
		std::printf("  steepening   mouth Pa   lips / resonance   sounds, per cent over   regularity   level dB   centroid / f0   over 1 kHz dB   slope over 1 kHz   transfer under 800 Hz dB   over 1.5 kHz dB   off the harmonics dB\n");
		struct Row { double mouth, level, centroid, above, slope, under, over; };
		for (int steep : {0, 1}) {
			std::vector<Row> rows;
			for (size_t at = 0; at < pressures.size(); at++) {
				const double mouth = pressures[at];
				Player p;
				p.steepening = steep == 1;
				p.setup();
				p.setResonance(lips[at] * peaks[r]);
				const size_t n = (size_t) (1.5 * RATE);
				std::vector<double> heard(n), cup(n);
				double power = 0.0;
				const auto start = std::chrono::steady_clock::now();
				for (size_t i = 0; i < n; i++) {
					p.setMouthPressure(mouth * std::fmin(1.0, (double) i / (0.02 * RATE)));
					cup[i] = p.step(&heard[i]);
					if (i >= n - (size_t) (0.5 * RATE))
						power += heard[i] * heard[i];
				}
				seconds[steep] += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
				played[steep] += 1.5;
				const double f0 = pitchOf(cup, n - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE));
				if (!(f0 > 0.0) || std::fabs(cents(f0, peaks[r])) > 150.0)
					continue;
				const double level = 10.0 * std::log10(power / (0.5 * RATE) / (20e-6 * 20e-6) + 1e-30);
				const std::vector<double> hb = harmonicsOf(heard, f0), hc = harmonicsOf(cup, f0);
				double period = 0.0;
				const double regular = regularity(cup, RATE / peaks[r], &period);
				Row row{mouth, level, 0, 0, 0, 0, 0};
				measures(hb, f0, &row.centroid, &row.above, &row.slope);
				int under = 0, over = 0;
				for (size_t k = 0; k < hb.size() && k < hc.size(); k++) {
					const double f = f0 * (double) (k + 1);
					if (f < 800.0) {
						row.under += hb[k] - hc[k];
						under++;
					}
					if (f >= 1500.0) {
						row.over += hb[k] - hc[k];
						over++;
					}
				}
				row.under /= std::max(1, under);
				row.over /= std::max(1, over);
				std::printf("  %10s %10.0f %18.3f %23.1f %12.3f %10.1f %15.2f %15.1f %18.1f %26.1f %17.1f %22.1f\n", steep ? "on" : "off", mouth,
					lips[at], 100.0 * (f0 / peaks[r] - 1.0), regular, level, row.centroid, row.above, row.slope, row.under, row.over,
					offHarmonic(heard, f0));
				rows.push_back(row);
			}
			if (rows.size() < 3)
				continue;
			auto nearest = [&](const std::function<double(const Row&)>& distance) {
				size_t best = 0;
				for (size_t i = 0; i < rows.size(); i++)
					if (distance(rows[i]) < distance(rows[best]))
						best = i;
				return rows[best];
			};
			const Row mf = nearest([](const Row& w) { return std::fabs(std::log(w.mouth / 1300.0)); });
			const Row pp = nearest([&](const Row& w) { return std::fabs(w.level - (mf.level - 13.0)); });
			const Row ff = nearest([&](const Row& w) { return std::fabs(w.level - (mf.level + 8.0)); });
			std::printf("  %s, steepening %s: pp %.0f Pa, mf %.0f, ff %.0f; levels %+.1f and %+.1f dB (recordings +13, +8)\n", names[note],
				steep ? "on" : "off", pp.mouth, mf.mouth, ff.mouth, mf.level - pp.level, ff.level - mf.level);
			std::printf("    centroid over the pitch %.2f, %.2f, %.2f: x%.2f and x%.2f (recordings x1.55 and x1.85)\n", pp.centroid, mf.centroid,
				ff.centroid, mf.centroid / pp.centroid, ff.centroid / mf.centroid);
			std::printf("    slope over 1 kHz %.1f, %.1f, %.1f dB an octave (recordings about -25, -25, -11)\n", pp.slope, mf.slope, ff.slope);
			std::printf("    share over 1 kHz %.1f, %.1f, %.1f dB: %+.1f and %+.1f (recordings +9.0 and +3.6)\n", pp.above, mf.above, ff.above,
				mf.above - pp.above, ff.above - mf.above);
			std::printf("    transfer mf to ff: under 800 Hz %+.1f dB (B5: within 3), over 1.5 kHz %+.1f dB (B5: 15 to 25)\n", ff.under - mf.under,
				ff.over - mf.over);
		}
	}
	std::printf("\nCOST: %.1f per cent of one core without the steepening, %.1f with (lips and column together)\n", 100.0 * seconds[0] / played[0],
		100.0 * seconds[1] / played[1]);
}

/** THE HINGED LIPS' SHAPE SWEPT, as sweep does for the one mass. */
static void sweepHinged() {
	const std::vector<double> peaks = resonances(0.0);
	std::printf("mass   rest y mm   breadth mm   |  F3: threshold Pa, over the resonance %%   |  B flat 3: threshold Pa, over %%\n");
	for (double mass : {0.75, 1.0, 1.25, 1.5})
		for (double rest : {0.0, 0.02e-3})
			for (double breadth : {0.0105, 0.014}) {
				hingedNow = BrassHingedLips::Shape();
				hingedNow.mass = mass;
				hingedNow.restY = rest;
				hingedNow.breadth = breadth;
				std::printf("%4.1f %11.2f %12.1f   |", mass, 1000.0 * rest, 1000.0 * breadth);
				for (size_t r : {2, 3}) {
					double lowest = 1e9, sounds = 0.0;
					for (double ratio = 0.70; ratio <= 1.301; ratio += 0.04) {
						double pitch = 0.0;
						const double t = threshold(ratio * peaks[r], 5000.0, &pitch);
						if (t > 0.0 && pitch > 0.0 && std::fabs(cents(pitch, peaks[r])) < 150.0 && t < lowest) {
							lowest = t;
							sounds = pitch;
						}
					}
					if (lowest < 1e9)
						std::printf("  %6.0f Pa  %+5.2f%%            |", lowest, 100.0 * (sounds / peaks[r] - 1.0));
					else
						std::printf("  none below 5 kPa                |");
				}
				std::printf("\n");
				std::fflush(stdout);
			}
}

/** THE CLOSURE'S SHAPE SWEPT: one line for the corner height, mass and breadth given (corner= mass= breadth=,
the rest Shape's defaults), with the lowest threshold of F3 and B flat 3 over the lips from 0.70 to 1.30 of
the resonance and where it sounds, the pitch with the lips at 0.98 at 1.3 kPa, and the pedal with the lips
at 55 and 60 Hz. */
static void sweepCorner() {
	const std::vector<double> peaks = resonances(0.0);
	std::printf("corner %.2f mm  mass %.2f  breadth %.1f mm  Q %.1f |", 1000.0 * hingedNow.cornerHeight, hingedNow.mass, 1000.0 * hingedNow.breadth,
		hingedNow.quality);
	for (size_t r : {2, 3}) {
		double lowest = 1e9, sounds = 0.0, lipAt = 0.0;
		for (double ratio = 0.70; ratio <= 1.301; ratio += 0.04) {
			double pitch = 0.0;
			const double t = threshold(ratio * peaks[r], 5000.0, &pitch);
			if (t > 0.0 && pitch > 0.0 && std::fabs(cents(pitch, peaks[r])) < 150.0 && t < lowest) {
				lowest = t;
				sounds = pitch;
				lipAt = ratio;
			}
		}
		// The lips at 0.98 at 1.3 kPa.
		Player p;
		p.setup();
		p.setResonance(0.98 * peaks[r]);
		const size_t n = (size_t) (1.0 * RATE);
		std::vector<double> cup(n);
		for (size_t i = 0; i < n; i++) {
			p.setMouthPressure(1300.0 * std::fmin(1.0, (double) i / (0.02 * RATE)));
			cup[i] = p.step();
		}
		const double f0 = pitchOf(cup, n - (size_t) (0.4 * RATE), (size_t) (0.4 * RATE));
		const bool on = f0 > 0.0 && std::fabs(cents(f0, peaks[r])) < 150.0 && swingOf(cup, n - 4800, 4800) > 50.0;
		if (lowest < 1e9)
			std::printf(" %s lowest %5.0f Pa at %.2f, %+5.2f%%; at 0.98 and 1.3 kPa ", r == 2 ? "F3" : "Bb3", lowest, lipAt, 100.0 * (sounds / peaks[r] - 1.0));
		else
			std::printf(" %s none under 5 kPa; at 0.98 and 1.3 kPa ", r == 2 ? "F3" : "Bb3");
		if (on)
			std::printf("%+5.2f%% |", 100.0 * (f0 / peaks[r] - 1.0));
		else
			std::printf("silent |");
	}
	for (double lip : {55.0, 60.0}) {
		Player p;
		p.setup();
		p.setResonance(lip);
		const size_t n = (size_t) (1.5 * RATE);
		std::vector<double> cup(n);
		for (size_t i = 0; i < n; i++) {
			p.setMouthPressure(1200.0 * std::fmin(1.0, (double) i / (0.02 * RATE)));
			cup[i] = p.step();
		}
		const double f0 = pitchOf(cup, n - (size_t) (0.5 * RATE), (size_t) (0.5 * RATE));
		std::printf(" pedal %.0f: %5.1f Hz", lip, f0);
	}
	std::printf("\n");
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "onemass") == 0)
			oneMass = true;
		if (std::strcmp(argv[i], "sharpdamping") == 0)
			blendDamping = false;
		if (std::strncmp(argv[i], "corner=", 7) == 0)
			hingedNow.cornerHeight = std::atof(argv[i] + 7);
		if (std::strncmp(argv[i], "mass=", 5) == 0)
			hingedNow.mass = std::atof(argv[i] + 5);
		if (std::strncmp(argv[i], "breadth=", 8) == 0)
			hingedNow.breadth = std::atof(argv[i] + 8);
		if (std::strncmp(argv[i], "quality=", 8) == 0)
			hingedNow.quality = std::atof(argv[i] + 8);
	}
	if (argc > 1 && std::strcmp(argv[1], "corners") == 0) {
		sweepCorner();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "crescendo") == 0) {
		crescendo();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "aliasing") == 0) {
		if (argc > 2 && std::atof(argv[2]) > 0.0)
			RATE = std::atof(argv[2]);
		aliasing();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "regular") == 0) {
		regular();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "brass") == 0) {
		if (argc > 2 && std::atof(argv[2]) > 0.0)
			RATE = std::atof(argv[2]);
		brass();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "listen") == 0) {
		listen();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "sweep") == 0) {
		if (oneMass)
			sweep();
		else
			sweepHinged();
		return 0;
	}
	const std::vector<double> peaks = resonances(0.0);
	std::printf("THE BORE'S RESONANCES, first position:");
	for (size_t i = 0; i < 9 && i < peaks.size(); i++)
		std::printf(" %.1f", peaks[i]);
	std::printf(" Hz\n");

	// THRESHOLDS: for each resonance from the second to the eighth, the lips' resonance from 0.7 to 1.4 of it.
	std::printf("\nTHRESHOLDS (the mouth's pressure rising 2 kPa a second): for each resonance, the lowest found and the lips' resonance that gave it, and where the note sounds\n");
	std::printf("  resonance      Hz   lips Hz   lips / resonance   threshold Pa   sounds Hz   above the resonance\n");
	std::vector<double> bestLip(peaks.size(), 0.0), bestThreshold(peaks.size(), 0.0);
	for (size_t r = 1; r < 8 && r < peaks.size(); r++) {
		double lowest = 1e9, lip = 0.0, sounds = 0.0;
		for (double ratio = 0.70; ratio <= 1.401; ratio += 0.02) {
			double pitch = 0.0;
			const double t = threshold(ratio * peaks[r], 8000.0, &pitch);
			// Only a note on this resonance: within a semitone and a half of it.
			if (t > 0.0 && pitch > 0.0 && std::fabs(cents(pitch, peaks[r])) < 150.0 && t < lowest) {
				lowest = t;
				lip = ratio * peaks[r];
				sounds = pitch;
			}
		}
		bestLip[r] = lip;
		bestThreshold[r] = lowest < 1e9 ? lowest : 0.0;
		if (lowest < 1e9)
			std::printf("  %9zu %7.1f %9.1f %18.2f %14.0f %11.1f %+10.1f c (%+.2f%%)\n", r + 1, peaks[r], lip, lip / peaks[r], lowest,
				sounds, cents(sounds, peaks[r]), 100.0 * (sounds / peaks[r] - 1.0));
		else
			std::printf("  %9zu %7.1f   no note on this resonance below 8 kPa\n", r + 1, peaks[r]);
	}

	// HELD NOTES at one and a half and three times the threshold, with the lips that gave the threshold.
	std::printf("\nHELD NOTES: pitch against the resonance, the cup's swing against the mouth's pressure, and the level a metre away\n");
	std::printf("  resonance   mouth Pa   sounds Hz   above the resonance   cup swing Pa   swing / mouth   level dB SPL\n");
	for (size_t r = 1; r < 8 && r < peaks.size(); r++) {
		if (!(bestLip[r] > 0.0))
			continue;
		for (double times : {1.5, 3.0}) {
			const double mouth = times * bestThreshold[r];
			double pitch, swing, level;
			held(bestLip[r], mouth, &pitch, &swing, &level);
			std::printf("  %9zu %10.0f %11.1f %+14.1f c %14.0f %15.1f %13.1f\n", r + 1, mouth, pitch, pitch > 0 ? cents(pitch, peaks[r]) : 0.0,
				swing, swing / mouth, level);
		}
	}

	// BENDING at F3 and B flat 3 (the third and fourth resonances): the mouth's pressure held at twice the
	// threshold, the lips' resonance moved slowly from 0.6 to 1.6 of the resonance and back; how far the note
	// on that resonance goes either way of its normal pitch before it leaves it.
	std::printf("\nBENDING (B2: F3 from 12.1%% under to 8.1%% over its normal pitch, B flat 3 from 10.0%% under to 8.9%% over)\n");
	for (size_t r : {2, 3}) {
		if (!(bestLip[r] > 0.0))
			continue;
		// THE NORMAL NOTE, as players play it, about one per cent over the resonance: the lips at 0.98 of it.
		double normal, swing, level;
		held(0.98 * peaks[r], 2.0 * bestThreshold[r], &normal, &swing, &level);
		double lowest = 1e9, highest = 0.0;
		for (int direction : {1, -1}) {
			Player p;
			p.setup();
			const double seconds = 8.0;
			const size_t n = (size_t) (seconds * RATE), window = (size_t) (0.04 * RATE);
			std::vector<double> cup(n);
			for (size_t i = 0; i < n; i++) {
				const double u = (double) i / (double) n;
				const double ratio = direction > 0 ? 0.6 + 1.0 * u : 1.6 - 1.0 * u;
				p.setResonance(ratio * peaks[r]);
				p.setMouthPressure(2.0 * bestThreshold[r]);
				cup[i] = p.step();
			}
			for (size_t i = window; i + window < n; i += window) {
				const double f = pitchOf(cup, i, window);
				if (f > 0.0 && swingOf(cup, i, window) > 50.0 && std::fabs(cents(f, peaks[r])) < 300.0) {
					// On this resonance: nearer it than either neighbour.
					if (std::fabs(cents(f, peaks[r])) < std::fabs(cents(f, peaks[r - 1])) && std::fabs(cents(f, peaks[r])) < std::fabs(cents(f, peaks[r + 1]))) {
						lowest = std::min(lowest, f);
						highest = std::max(highest, f);
					}
				}
			}
		}
		std::printf("  resonance %zu: normal %.1f Hz; it goes from %.1f to %.1f Hz, %.1f%% under to %.1f%% over\n", r + 1, normal, lowest, highest,
			100.0 * (1.0 - lowest / normal), 100.0 * (highest / normal - 1.0));
	}

	// THE PEDAL: the lips from 45 to 75 Hz, the mouth's pressure rising; whether a steady note near B flat 1
	// (58.3 Hz) starts, and at what.
	std::printf("\nTHE PEDAL (B3: B flat 1, 58.3 Hz, with no resonance under it)\n");
	for (double lip = 45.0; lip <= 75.01; lip += 5.0) {
		double pitch = 0.0;
		const double t = threshold(lip, 6000.0, &pitch);
		if (t > 0.0)
			std::printf("  lips %.0f Hz: starts at %.0f Pa, sounds %.1f Hz (%+.0f c from B flat 1)\n", lip, t, pitch, pitch > 0 ? cents(pitch, 58.27) : 0.0);
		else
			std::printf("  lips %.0f Hz: no note below 6 kPa\n", lip);
	}
	return 0;
}
