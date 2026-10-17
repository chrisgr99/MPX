/** THE TROMBONE'S SOUND AGAINST THE RECORDINGS, from a command line: phase 5 of docs/brass.md, targets B6
and B9 of research/brass/trombone-benchmark.md.

    make brasssound                    every note from E2 to C5 at pp, mf and ff, against the Iowa table
    make brasssound ARGS="listen 30"   Iowa's notes and the model's side by side, the model heard 30 degrees
                                       off the bell's axis, into build/

EVERY NOTE THE RECORDINGS HOLD, E2 to C5, played on the model at 44.1 kHz, their rate: the slide position
and resonance whose note, 1.1 per cent over the resonance as players play it (B2), is nearest; the lips
tuned at each pressure to sound there (as in test/brasslips.cpp). The pressures: mf at 1.3 kPa (B4) for a
note at 200 Hz, between F3 and B flat 3, scaled as the frequency to the power three quarters, which
Fletcher and Tarnopolsky found keeps a note's spectrum the same from note to note; pp and ff where the level
is 13 dB under mf and 8 over, the recordings' steps. Each note is heard a metre from the bell at nought to 45
degrees from its axis (src/BrassRadiation), and measured as test/brassref.cpp measures the
recordings: the harmonics to 10 kHz, each the strongest bin within 3 per cent of its multiple of the pitch,
against the strongest; the centroid over them; the slopes under and over 1 kHz; the share over 1 kHz. The
medians by register and dynamic are set beside the recordings'.

The model has no noise. The recordings have the room's and the player's under every note; where the
harmonics fall under it, the strongest bin near each is the noise, which flattens the slopes over 1 kHz at
pp. So the slopes are also given counting only the harmonics within 40 dB of the strongest, in both. */
#include "brassbench.hpp"
#include "../src/BrassBore.hpp"
#include "../src/BrassLips.hpp"
#include "../src/BrassRadiation.hpp"
#include "../src/BrassWaveguide.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace px;

using namespace bench;

static const double ANGLES[5] = {0.0, 10.0, 20.0, 30.0, 45.0};

/** WHERE A NOTE IS PLAYED: the slide's extension and the resonance, and the pressures found for it. */
struct Plan {
	int midi = 0;
	double extension = 0.0, resonance = 0.0;
	int position = 1, partial = 2;
	Played dyn[3];   // pp, mf, ff
	bool done = false;
};

static Plan plan(int midi, const std::vector<std::vector<double>>& peaks) {
	Plan p;
	p.midi = midi;
	const double want = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
	double best = 1e9;
	for (int pos = 0; pos < 7; pos++)
		for (size_t r = 1; r < peaks[(size_t) pos].size(); r++) {
			const double c = std::fabs(cents(1.011 * peaks[(size_t) pos][r], want));
			if (c < best - 10.0) {
				best = c;
				p.extension = POSITIONS[pos];
				p.resonance = peaks[(size_t) pos][r];
				p.position = pos + 1;
				p.partial = (int) r + 1;
			}
		}
	return p;
}

/** The pressures for pp, mf and ff, as the header says. */
static void findDynamics(Plan& p) {
	const double hz = 1.011 * p.resonance;
	double mf = 1300.0 * std::pow(hz / 200.0, 0.75);
	p.dyn[1] = at(p.extension, p.resonance, mf);
	for (int i = 0; i < 8 && !p.dyn[1].take.sounds; i++) {
		mf *= 1.19;
		p.dyn[1] = at(p.extension, p.resonance, mf);
	}
	if (!p.dyn[1].take.sounds)
		return;
	const double mfLevel = p.dyn[1].take.level;
	// pp: between an eighth of mf's pressure and mf's, the level 13 dB under; too quiet where it does not sound.
	for (int d : {0, 2}) {
		const double target = mfLevel + (d == 0 ? -13.0 : 8.0);
		double lo = d == 0 ? mf / 8.0 : mf, hi = d == 0 ? mf : mf * 6.0;
		Played best = p.dyn[1];
		for (int i = 0; i < 7; i++) {
			const double mouth = std::sqrt(lo * hi);
			const Played q = at(p.extension, p.resonance, mouth);
			if (q.take.sounds && std::fabs(q.take.level - target) < std::fabs(best.take.level - target))
				best = q;
			const bool tooLoud = q.take.sounds && q.take.level > target;
			(tooLoud ? hi : lo) = mouth;
		}
		p.dyn[d] = best;
	}
	p.done = true;
}


/** EVERY NOTE, on as many threads as the machine has, less one. */
static std::vector<Plan> playAll(const std::vector<int>& midis) {
	std::vector<std::vector<double>> peaks;
	for (double e : POSITIONS)
		peaks.push_back(resonances(e));
	std::vector<Plan> plans;
	for (int m : midis)
		plans.push_back(plan(m, peaks));
	std::atomic<size_t> next(0);
	std::mutex print;
	auto work = [&]() {
		for (size_t i; (i = next++) < plans.size(); ) {
			findDynamics(plans[i]);
			std::lock_guard<std::mutex> lock(print);
			const Plan& p = plans[i];
			std::printf("  %-4s position %d, resonance %d (%.1f Hz): ", noteName(p.midi).c_str(), p.position, p.partial, p.resonance);
			if (!p.done)
				std::printf("does not sound at mf\n");
			else
				std::printf("pp %.0f Pa, mf %.0f, ff %.0f; levels %.1f, %.1f, %.1f dB\n", p.dyn[0].mouth, p.dyn[1].mouth, p.dyn[2].mouth,
					p.dyn[0].take.level, p.dyn[1].take.level, p.dyn[2].take.level);
			std::fflush(stdout);
		}
	};
	const unsigned threads = std::max(1u, std::thread::hardware_concurrency() - 1);
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < threads; t++)
		pool.emplace_back(work);
	for (auto& t : pool)
		t.join();
	return plans;
}

/** THE BELL'S FILTER for a listener `degrees` off the axis, a metre away, at this rate. */
static std::vector<double> listen(const std::vector<double>& intoBell, double degrees) {
	const BrassAir air(20.0);
	const BrassBore bore = BrassBore::tromboneTimed(0.0, PULL);
	size_t first = 2, last = first;
	while (last < bore.sections.size() && bore.sections[last].r0 == bore.sections[last].r1)
		last++;
	BrassGrid::Setup g;
	g.air = air;
	g.rate = RATE;
	g.bore = bore;
	g.bore.sections.assign(bore.sections.begin() + (long) last, bore.sections.end());
	g.startIsPort = true;
	static std::mutex lock;
	static std::map<int, BrassListenerFilter> filters;
	BrassListenerFilter f;
	{
		std::lock_guard<std::mutex> l(lock);
		auto it = filters.find((int) degrees);
		if (it == filters.end()) {
			BrassListener where;
			where.angle = degrees * M_PI / 180.0;
			BrassListenerFilter made;
			made.design(g, where);
			it = filters.emplace((int) degrees, made).first;
		}
		f = it->second;
	}
	std::vector<double> out(intoBell.size());
	for (size_t i = 0; i < intoBell.size(); i++)
		out[i] = f.process(intoBell[i]);
	return out;
}

static int registerOf(int midi) { return midi < 48 ? 0 : midi < 60 ? 1 : 2; }

static void compare() {
	std::vector<int> midis;
	for (int m = 40; m <= 72; m++)
		midis.push_back(m);
	std::printf("PLAYING E2 TO C5 at pp, mf and ff\n");
	const std::vector<Plan> plans = playAll(midis);
	const std::vector<IowaRow> iowa = readIowa("research/brass/iowa-tenor-trombone.tsv");
	const char* registers[3] = {"E2 to B2", "C3 to B3", "C4 to C5"};
	const char* dynamics[3] = {"pp", "mf", "ff"};
	double missCentroid[5] = {0, 0, 0, 0, 0}, missSlope[5] = {0, 0, 0, 0, 0}, missShare[5] = {0, 0, 0, 0, 0};
	std::printf("\nMEDIANS BY REGISTER AND DYNAMIC: the recordings, then the model a metre away at 0 to 45 degrees\n");
	std::printf("  register   dyn   source      centroid/f0   slope<1k   slope>1k   slope>1k within 40 dB   share>1k dB\n");
	for (int reg = 0; reg < 3; reg++)
		for (int d = 0; d < 3; d++) {
			std::vector<double> c, sb, sa, sa40, ab;
			for (const IowaRow& r : iowa)
				if (registerOf(r.midi) == reg && r.dynamic == dynamics[d]) {
					c.push_back(r.m.centroidOverF0);
					sb.push_back(r.m.slopeBelow);
					sa.push_back(r.m.slopeAbove);
					sa40.push_back(r.m.slopeAbove40);
					ab.push_back(r.m.aboveDb);
				}
			std::printf("  %-9s  %s    Iowa      %12.2f %10.1f %10.1f %22.1f %13.1f\n", registers[reg], dynamics[d], median(c), median(sb), median(sa),
				median(sa40), median(ab));
			for (size_t ai = 0; ai < sizeof ANGLES / sizeof ANGLES[0]; ai++) {
				const double degrees = ANGLES[ai];
				std::vector<double> mc, msb, msa, msa40, mab;
				for (const Plan& p : plans)
					if (p.done && registerOf(p.midi) == reg) {
						const Take& t = p.dyn[d].take;
						const Measures m = measure(listen(t.intoBell, degrees), t.f0);
						mc.push_back(m.centroidOverF0);
						msb.push_back(m.slopeBelow);
						msa.push_back(m.slopeAbove);
						msa40.push_back(m.slopeAbove40);
						mab.push_back(m.aboveDb);
					}
				std::printf("  %-9s  %s    model %2.0f  %12.2f %10.1f %10.1f %22.1f %13.1f\n", "", dynamics[d], degrees, median(mc), median(msb),
					median(msa), median(msa40), median(mab));
				// How far from the recordings: the centroid in per cent, the slope within 40 dB and the share in dB.
				missCentroid[ai] += std::fabs(100.0 * std::log(median(mc) / median(c)));
				missSlope[ai] += std::fabs(median(msa40) - median(sa40));
				missShare[ai] += std::fabs(median(mab) - median(ab));
			}
		}
	std::printf("\nHOW FAR FROM THE RECORDINGS, the mean over registers and dynamics: the centroid in per cent, the slope over 1 kHz within 40 dB and the share over 1 kHz in dB\n");
	for (size_t ai = 0; ai < sizeof ANGLES / sizeof ANGLES[0]; ai++)
		std::printf("  %2.0f degrees: centroid %.0f%%, slope %.1f dB an octave, share %.1f dB\n", ANGLES[ai], missCentroid[ai] / 9.0, missSlope[ai] / 9.0,
			missShare[ai] / 9.0);
}

/** A MONO WAV, 16 bits, as doubles from minus one to one; empty where it cannot be read. */
static std::vector<double> readWav(const std::string& path) {
	std::vector<double> x;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return x;
	std::fseek(f, 44, SEEK_SET);
	int16_t v;
	while (std::fread(&v, 2, 1, f) == 1)
		x.push_back(v / 32768.0);
	std::fclose(f);
	return x;
}

static void writeWav(const std::string& path, const std::vector<double>& x) {
	double peak = 1e-12;
	for (double v : x)
		peak = std::max(peak, std::fabs(v));
	const double g = peak > 0.95 ? 0.95 / peak : 1.0;
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t n = (uint32_t) x.size(), bytes = n * 2, r = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(r); u32(r * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (double v : x) {
		const int16_t sample = (int16_t) std::lround(v * g * 32767.0);
		std::fwrite(&sample, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s%s\n", path.c_str(), g < 1.0 ? " (turned down to fit)" : "");
}

static double rmsOfMiddle(const std::vector<double>& x) {
	double sum = 0.0;
	const size_t a = x.size() / 4, b = x.size() - x.size() / 4;
	for (size_t i = a; i < b; i++)
		sum += x[i] * x[i];
	return std::sqrt(sum / (double) (b - a));
}

/** BESIDE THE RECORDINGS (B9): for B flat 2, F3, B flat 3 and F4, one file each — the Iowa note at pp, then
the model's at the same loudness, then the two at mf, then at ff — the model heard `degrees` off the bell's
axis. The Iowa notes are cut by make brassref ARGS="cut NOTE DYN build/iowa-cuts/NOTE DYN.wav" first. The
model's notes start with the pressure rising over 30 ms and stop with it falling over 50: no tongue yet. */
static void listenBeside(double degrees) {
	const std::vector<int> midis = {46, 53, 58, 65};
	const char* spoken[4] = {"B flat 2", "F3", "B flat 3", "F4"};
	const char* dynamics[3] = {"pp", "mf", "ff"};
	std::printf("FINDING THE PRESSURES\n");
	const std::vector<Plan> plans = playAll(midis);
	for (size_t k = 0; k < plans.size(); k++) {
		const Plan& p = plans[k];
		if (!p.done)
			continue;
		std::vector<double> out;
		for (int d = 0; d < 3; d++) {
			const std::vector<double> iowa = readWav("build/iowa-cuts/" + noteName(p.midi) + " " + dynamics[d] + ".wav");
			if (iowa.empty()) {
				std::printf("no cut of %s %s\n", noteName(p.midi).c_str(), dynamics[d]);
				return;
			}
			// The model's note, two seconds.
			Player pl;
			pl.setup(p.extension);
			pl.lips.setResonance(p.dyn[d].lips * p.resonance);
			const double seconds = 2.0;
			const size_t n = (size_t) (seconds * RATE);
			std::vector<double> into(n);
			for (size_t i = 0; i < n; i++) {
				const double t = (double) i / RATE;
				const double env = std::fmax(0.0, std::fmin(1.0, std::fmin(t / 0.03, (seconds - 0.1 - t) / 0.05)));
				pl.lips.setMouthPressure(p.dyn[d].mouth * env);
				double heard = 0.0;
				pl.step(&into[i], &heard);
			}
			std::vector<double> model = listen(into, degrees);
			const double g = rmsOfMiddle(iowa) / std::max(1e-12, rmsOfMiddle(model));
			for (double& v : model)
				v *= g;
			out.insert(out.end(), iowa.begin(), iowa.end());
			out.insert(out.end(), (size_t) (0.4 * RATE), 0.0);
			out.insert(out.end(), model.begin(), model.end());
			out.insert(out.end(), (size_t) (1.0 * RATE), 0.0);
		}
		writeWav(std::string("build/Trombone ") + spoken[k] + ", Iowa then model, pp mf ff.wav", out);
	}
}

int main(int argc, char** argv) {
	if (argc > 1 && std::strcmp(argv[1], "listen") == 0) {
		listenBeside(argc > 2 ? std::atof(argv[2]) : 30.0);
		return 0;
	}
	compare();
	return 0;
}
