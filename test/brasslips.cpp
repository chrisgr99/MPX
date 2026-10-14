/** THE LIPS ON THE AIR COLUMN, from a command line: phase 3 of docs/brass.md, measured against
research/brass/trombone-benchmark.md, B2 to B4.

    make brasslips                     thresholds, held notes, bending and the pedal
    make brasslips ARGS=listen         held notes and a slur to listen to, into build/

The trombone is BrassWaveguide at 48 kHz, first position, the tuning slide pulled 33 mm, the air at 20 C;
its resonances are BrassBore's calculation for the same geometry and radiation. Each step the column says
what the cup's pressure will be as a straight line in the flow (BrassWaveguide::begin), the lips give the
flow (BrassLips::step), and the column finishes the step.
*/
#include "../src/BrassBore.hpp"
#include "../src/BrassLips.hpp"
#include "../src/BrassWaveguide.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

using namespace px;

static const double RATE = 48000.0;
static const double PULL = 0.033;

static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

/** WHICH LIPS: the hinged block (BrassHingedLips) unless asked for the one mass (BrassLips). */
static bool oneMass = false;
static BrassHingedLips::Shape hingedNow;

/** A TROMBONE AND ITS PLAYER'S LIPS, stepped together. */
struct Player {
	BrassAir air = BrassAir(20.0);
	BrassWaveguide column;
	BrassLips lips;
	BrassHingedLips hinged;
	BrassLips::Shape shape;
	double lastFlow = 0.0;
	void setResonance(double hz) {
		lips.setResonance(hz);
		hinged.setResonance(hz);
	}
	void setMouthPressure(double pa) {
		lips.setMouthPressure(pa);
		hinged.setMouthPressure(pa);
	}
	void setup(double extension = 0.0) {
		BrassWaveguide::Setup s;
		s.air = air;
		s.rate = RATE;
		s.tuningPull = PULL;
		s.extension = extension;
		column.setup(s);
		lips.setup(shape, air, RATE);
		hinged.setup(hingedNow, air, RATE);
	}
	/** One step: the cup's pressure after it; `heard`, the pressure a metre in front of the bell. */
	double step(double* heard = nullptr) {
		double without = 0.0, perFlow = 0.0;
		column.begin(&without, &perFlow);
		const double flow = oneMass ? lips.step(column.cupPressure(), without, perFlow)
			: hinged.step(column.cupPressure(), without, perFlow);
		const double cup = column.end(flow);
		if (heard) {
			const double u = column.radiatedFlow();
			*heard = air.rho / (4.0 * M_PI) * (u - lastFlow) * RATE;
			lastFlow = u;
		}
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

/** A WAV, 16 bits, its peak at 1 dB under full scale. */
static void writeWav(const char* path, const std::vector<double>& x) {
	double peak = 1e-12;
	for (double v : x)
		peak = std::fmax(peak, std::fabs(v));
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

int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++)
		if (std::strcmp(argv[i], "onemass") == 0)
			oneMass = true;
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
