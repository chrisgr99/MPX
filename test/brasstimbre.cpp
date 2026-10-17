/** THE TROMBONE'S TIMBRE AGAINST THE RECORDINGS, QUICKLY: phase 6 of docs/brass.md, for trying the lips and
the sound without rebuilding the tables or playing a passage.

    make brasstimbre ARGS="[pp] [mf] [ff] [notes=40,41,42] NAME:KEY=VALUE,KEY=VALUE ..."

Each NAME is a setting of the trombone, its keys any of: corner, rest and soft, millimetres (the lips'
corners' height, their rest opening — less than nought pressed together — and the corners' height near the
threshold where they firm as the player blows harder, BrassHingedLips::Shape); from and to, the pressures,
as times the lips' threshold, over which they firm; noise and noisehz, the breath noise
(BrassInstrument::Design); angle, the listener's, degrees; over, the oversampling; pull, the tuning slide's,
millimetres, 55 as the trombone is, which moves every resonance against the notes, the notes staying where
they are; q, qshut, mass and breadth, the lips' quality open and shut, their mass and their breadth in
millimetres (BrassHingedLips::Shape); and rise, seconds, how long the breath takes to rise from nothing as
each note starts, 0.03 where it is not given. With no keys, the
trombone as it is. The dynamics are mf where none is given.

FOR EACH SETTING AND EACH NOTE — B flat 2, F3, B flat 3 and F4 where none are given, else the MIDI notes
given, each where the player would play it: on the lowest resonance that reaches it, the slide where that
resonance, aimed as the tables aim it, sounds it — the mouth's pressure is stepped a half octave at
a time from 300 Pa to 4.8 kPa, the lips tuned at each to sound the note; each dynamic is then played at the
pressure where the brightness reaches the recordings' at that dynamic, 620 Hz at pp, 960 at mf and 1800 at
ff, as the player chooses it (BrassPlayer, HOW HARD), held 1.7 s.

WHAT COMES OUT: build/Timbre NAME.wav, for each dynamic and note the Iowa recording's first 1.2 s and then
the model's, as loud over their steady parts, and every such pair at one level, 20 dB under full scale; and
for each note and dynamic build/Timbre NOTE DYNAMIC, the recording before each.wav, the recording before
each setting's note in turn, so that the sound aimed at is always just before; the recordings from the Rack
user folder's DreamerMPX/trombone-reference (make brassref ARGS="cutall DIR"); how long each note takes to
rise from 30 dB under its level to within 3 dB of it, the model's and the recording's; and for each, the harmonics to the 20th against the
recording's, the power between them, the brightness, and the steps in level from mf, against the
recording's. The notes are shared out over the machine's threads. */
#include "brassbench.hpp"
#include "../src/BrassInstrument.hpp"

#include <atomic>
#include <complex>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using namespace bench;
using px::BrassInstrument;

static const char* const DYNAMICS[3] = {"pp", "mf", "ff"};
static const double BRIGHTNESS[3] = {620.0, 960.0, 1800.0};

struct Setting {
	std::string name;
	BrassInstrument::Design design;
	/** How long the breath takes to rise from nothing as each note starts, seconds; and the breath played
	against the one found for the dynamic, dB, as mpxTrombone's BREATH has it, the lips kept as found. */
	double rise = 0.03, breathDb = 0.0;
};

struct TimbreNote {
	std::string name, tsv;
	int midi = 46, resonance = 2;
	double extension = 0.0;
};

static double hzOfMidi(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

/** ONE HELD NOTE on the trombone: the lips at `lipHz`, the breath rising to `mouth` over 30 ms. */
static void play(const BrassInstrument::Design& d, double extension, double rise, double lipHz, double mouth, double seconds, std::vector<double>& out,
	std::vector<double>& cup, double* openShare = nullptr, double* halfShare = nullptr) {
	BrassInstrument inst;
	inst.setup(d);
	inst.setSlide(extension);
	inst.setLips(lipHz);
	inst.setTongue(1.0);
	const size_t n = (size_t) (seconds * d.rate);
	out.assign(n, 0.0);
	cup.assign(n, 0.0);
	// HOW LONG THE LIPS ARE OPEN in each cycle, over the last half second: at all, and over half their widest.
	const size_t from = n > (size_t) (0.5 * d.rate) ? n - (size_t) (0.5 * d.rate) : 0;
	std::vector<double> area;
	for (size_t i = 0; i < n; i++) {
		inst.setBreath(mouth * std::fmin(1.0, (double) i / (std::fmax(1e-4, rise) * d.rate)));
		out[i] = inst.step();
		if (i >= from)
			area.push_back(inst.lipOpening());
		cup[i] = inst.cupPressure();
	}
	double widest = 0.0;
	size_t open = 0, half = 0;
	for (double a : area)
		widest = std::max(widest, a);
	for (double a : area) {
		open += a > 0.0 ? 1 : 0;
		half += a > 0.5 * widest ? 1 : 0;
	}
	if (openShare)
		*openShare = area.empty() ? 0.0 : (double) open / (double) area.size();
	if (halfShare)
		*halfShare = area.empty() ? 0.0 : (double) half / (double) area.size();
}

/** THE PITCH of the cup's pressure over its last `n` samples, by its rising crossings of its mean. */
static double pitchAt(const std::vector<double>& x, size_t n, double rate) {
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
	return count >= 3 ? rate * (count - 1) / (last - first) : 0.0;
}

static double swing(const std::vector<double>& x, size_t n) {
	double lo = 1e18, hi = -1e18;
	for (size_t i = x.size() - n; i < x.size(); i++) {
		lo = std::min(lo, x[i]);
		hi = std::max(hi, x[i]);
	}
	return hi - lo;
}

/** THE LIPS THAT SOUND `target` HERTZ on the resonance at `res`, at `mouth`: halving between 0.70 and 1.30 of
it — damped lips sound the note only set well over it, and held to 1.06, as the tables' search is, they
came out irregular. Sounding nothing counts as too high at the top of the range and too low at the bottom. */
static double tune(const BrassInstrument::Design& d, double extension, double res, double target, double mouth) {
	double lo = 0.70, hi = 1.30;
	std::vector<double> out, cup;
	for (int i = 0; i < 9; i++) {
		const double ratio = 0.5 * (lo + hi);
		play(d, extension, 0.03, ratio * res, mouth, 0.6, out, cup);
		const size_t n = (size_t) (0.25 * d.rate);
		const double f0 = pitchAt(cup, n, d.rate);
		const bool sounds = f0 > 0.0 && std::fabs(cents(f0, res)) < 150.0 && swing(cup, (size_t) (0.1 * d.rate)) > 50.0;
		const bool high = sounds ? f0 > target : ratio > 1.0;
		(high ? hi : lo) = ratio;
	}
	return 0.5 * (lo + hi);
}

/** THE HARMONICS of `x` over its last half second, dB, by a Hann-windowed transform at each multiple of `f0`
under 10 kHz. */
static std::vector<double> harmonics(const std::vector<double>& x, double f0, double rate) {
	const size_t n = (size_t) (0.5 * rate), from = x.size() - n;
	std::vector<double> h;
	for (int k = 1; k * f0 < 10000.0 && k <= 40; k++) {
		const double w = 2.0 * M_PI * k * f0 / rate;
		double re = 0.0, im = 0.0;
		for (size_t i = 0; i < n; i++) {
			const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
			re += x[from + i] * win * std::cos(w * (double) i);
			im -= x[from + i] * win * std::sin(w * (double) i);
		}
		h.push_back(10.0 * std::log10((re * re + im * im) * 16.0 / ((double) n * (double) n) + 1e-30));
	}
	return h;
}

/** THE BRIGHTNESS, the centroid of the harmonics' amplitudes, hertz, as the recordings' table has it. */
static double brightness(const std::vector<double>& h, double f0) {
	double a = 0.0, fa = 0.0;
	for (size_t k = 0; k < h.size(); k++) {
		const double amp = std::pow(10.0, h[k] / 20.0);
		a += amp;
		fa += amp * f0 * (double) (k + 1);
	}
	return a > 0.0 ? fa / a : 0.0;
}

/** THE POWER BETWEEN THE HARMONICS of the half second of `x` from `from`, dB of the whole over 50 Hz. */
static double between(const std::vector<double>& x, size_t from, double f0, double rate) {
	size_t m = 1;
	while (m < (size_t) (0.5 * rate))
		m *= 2;
	// The window over the samples there are, the rest of the transform nought.
	const size_t n = std::min(m, x.size() - from);
	std::vector<std::complex<double>> a(m, 0.0);
	for (size_t i = 0; i < n; i++)
		a[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n));
	fft(a);
	double all = 0.0, off = 0.0;
	for (size_t k = 1; k < m / 2; k++) {
		const double hz = (double) k * rate / (double) m;
		if (hz < 50.0)
			continue;
		const double p = std::norm(a[k]);
		all += p;
		const double toHarmonic = std::fabs(hz / f0 - std::round(hz / f0)) * f0;
		if (toHarmonic > 0.25 * f0)
			off += p;
	}
	return 10.0 * std::log10(off / all + 1e-30);
}

static double rms(const std::vector<double>& x, size_t a, size_t b) {
	double s = 0.0;
	for (size_t i = a; i < b && i < x.size(); i++)
		s += x[i] * x[i];
	return std::sqrt(s / (double) std::max<size_t>(1, b - a));
}

/** HOW LONG A NOTE TAKES TO RISE, ms: from where it first comes within 30 dB of its level over 0.8 to 1.1 s
to where it first comes within 3 dB of it, over 10 ms windows. */
static double growth(const std::vector<double>& x, double rate) {
	const size_t w = (size_t) (0.01 * rate);
	if (x.size() < (size_t) (1.15 * rate))
		return 0.0;
	const double settled = 20.0 * std::log10(rms(x, (size_t) (0.8 * rate), (size_t) (1.1 * rate)) + 1e-12);
	double t0 = -1.0;
	for (size_t i = 0; i + w < (size_t) (0.8 * rate); i += w / 2) {
		const double l = 20.0 * std::log10(rms(x, i, i + w) + 1e-12);
		if (t0 < 0.0 && l > settled - 30.0)
			t0 = (double) i;
		if (t0 >= 0.0 && l > settled - 3.0)
			return 1000.0 * ((double) i - t0) / rate;
	}
	return 0.0;
}

/** A WAV, mono, 16 bits, as doubles; its rate into `rate`. */
static std::vector<double> readWav(const std::string& path, double& rate) {
	std::vector<double> x;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return x;
	std::vector<uint8_t> b;
	uint8_t chunk[65536];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		b.insert(b.end(), chunk, chunk + got);
	std::fclose(f);
	auto u32 = [&](size_t i) { return (uint32_t) b[i] | (uint32_t) b[i + 1] << 8 | (uint32_t) b[i + 2] << 16 | (uint32_t) b[i + 3] << 24; };
	for (size_t i = 12; i + 8 <= b.size(); ) {
		const uint32_t size = u32(i + 4);
		if (!std::memcmp(&b[i], "fmt ", 4))
			rate = (double) u32(i + 12);
		if (!std::memcmp(&b[i], "data", 4))
			for (size_t j = i + 8; j + 1 < std::min(b.size(), (size_t) (i + 8 + size)); j += 2)
				x.push_back((double) (int16_t) (b[j] | b[j + 1] << 8) / 32768.0);
		i += 8 + size + (size & 1);
	}
	return x;
}

/** FROM ONE RATE TO ANOTHER, by a windowed sinc of 32 taps. */
static std::vector<double> resample(const std::vector<double>& x, double from, double to) {
	const size_t n = (size_t) ((double) x.size() * to / from);
	const double cut = 0.95 * std::min(1.0, to / from);
	std::vector<double> y(n, 0.0);
	for (size_t i = 0; i < n; i++) {
		const double t = (double) i * from / to;
		const long c = (long) std::floor(t);
		double sum = 0.0;
		for (long k = c - 15; k <= c + 16; k++) {
			if (k < 0 || k >= (long) x.size())
				continue;
			const double dd = t - (double) k;
			const double sinc = std::fabs(dd) < 1e-9 ? cut : std::sin(M_PI * cut * dd) / (M_PI * dd);
			sum += x[(size_t) k] * sinc * (0.5 + 0.5 * std::cos(M_PI * dd / 16.0));
		}
		y[i] = sum;
	}
	return y;
}

/** THE RECORDINGS' TABLE: for a note and dynamic, its level, brightness and harmonics. */
struct Recorded {
	bool found = false;
	double level = 0.0, centroid = 0.0;
	std::vector<double> h;
};
static Recorded recorded(const char* note, const char* dynamic) {
	Recorded r;
	FILE* f = std::fopen("research/brass/iowa-tenor-trombone.tsv", "r");
	char line[4096];
	while (f && std::fgets(line, sizeof line, f)) {
		std::vector<std::string> cells;
		std::string cell;
		for (char* c = line; *c && *c != '\n'; c++) {
			if (*c == '\t') {
				cells.push_back(cell);
				cell.clear();
			}
			else
				cell += *c;
		}
		cells.push_back(cell);
		if (cells.size() >= 33 && cells[0] == note && cells[2] == dynamic) {
			r.found = true;
			r.level = std::atof(cells[5].c_str());
			r.centroid = std::atof(cells[8].c_str());
			for (int k = 0; k < 20; k++)
				r.h.push_back(std::atof(cells[(size_t) (13 + k)].c_str()));
		}
	}
	if (f)
		std::fclose(f);
	return r;
}

/** WHAT ONE NOTE AT ONE DYNAMIC CAME TO. */
struct Result {
	bool played = false;
	double mouth = 0.0, lips = 0.0, f0 = 0.0, level = 0.0, centroid = 0.0, noise = 0.0, iowaNoise = 0.0, growth = 0.0, iowaGrowth = 0.0;
	/** The share of each cycle the lips are open at all, and over half their widest. */
	double open = 0.0, half = 0.0;
	double groups[3] = {0, 0, 0};
	/** Each of the first 20 harmonics against the recording's, dB, each against its own strongest. */
	std::vector<double> each;
	std::vector<double> clip, iowa;
};

/** WHERE THE PLAYER WOULD PLAY A NOTE: the lowest resonance from the second that reaches it with the slide
between first and seventh positions, aimed as the tables aim it — the second 4.8 per cent over itself, the
rest 1.1 — the slide found by halving, at the tuning slide's PULL. */
static TimbreNote place(int midi) {
	static const char* const SHARPS[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	static const char* const SPOKEN[12] = {"C", "C sharp", "D", "E flat", "E", "F", "F sharp", "G", "A flat", "A", "B flat", "B"};
	TimbreNote n;
	n.midi = midi;
	n.name = std::string(SPOKEN[midi % 12]) + " " + std::to_string(midi / 12 - 1);
	n.tsv = std::string(SHARPS[midi % 12]) + std::to_string(midi / 12 - 1);
	const double f = hzOfMidi(midi);
	const std::vector<double> in = resonances(POSITIONS[0]), out = resonances(POSITIONS[6]);
	double best = 1e9;
	for (size_t r = 1; r < in.size() && r < out.size(); r++) {
		const double aim = r == 1 ? 1.048 : 1.011;
		if (!(f <= in[r] * aim * 1.01 && f >= out[r] * aim * 0.99))
			continue;
		double lo = 0.0, hi = POSITIONS[6];
		for (int i = 0; i < 18; i++) {
			const double mid = 0.5 * (lo + hi);
			const std::vector<double> at = resonances(mid);
			if (r < at.size() && at[r] * aim > f)
				lo = mid;
			else
				hi = mid;
		}
		const double e = 0.5 * (lo + hi);
		if (e < best) {
			best = e;
			n.resonance = (int) r + 1;
			n.extension = e;
		}
	}
	return n;
}

int main(int argc, char** argv) {
	RATE = 48000.0;
	PULL = 0.055;
	bool want[3] = {false, false, false};
	std::vector<Setting> settings;
	std::vector<int> midis;
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		bool dynamic = false;
		for (int k = 0; k < 3; k++)
			if (a == DYNAMICS[k]) {
				want[k] = true;
				dynamic = true;
			}
		if (dynamic)
			continue;
		if (a.compare(0, 6, "notes=") == 0) {
			size_t at = 6;
			while (at < a.size()) {
				size_t comma = a.find(',', at);
				if (comma == std::string::npos)
					comma = a.size();
				midis.push_back(std::atoi(a.substr(at, comma - at).c_str()));
				at = comma + 1;
			}
			continue;
		}
		Setting s;
		const size_t colon = a.find(':');
		s.name = a.substr(0, colon);
		s.design.rate = 48000.0;
		if (colon != std::string::npos) {
			std::string rest = a.substr(colon + 1);
			size_t at = 0;
			while (at < rest.size()) {
				size_t comma = rest.find(',', at);
				if (comma == std::string::npos)
					comma = rest.size();
				const std::string kv = rest.substr(at, comma - at);
				const size_t eq = kv.find('=');
				const std::string k = kv.substr(0, eq);
				const double v = eq == std::string::npos ? 0.0 : std::atof(kv.c_str() + eq + 1);
				if (k == "corner") s.design.lips.cornerHeight = v * 1e-3;
				else if (k == "q") s.design.lips.quality = v;
				else if (k == "qshut") s.design.lips.qualityShut = v;
				else if (k == "mass") s.design.lips.mass = v;
				else if (k == "breadth") s.design.lips.breadth = v * 1e-3;
				else if (k == "rise") s.rise = v;
				else if (k == "breath") s.breathDb = v;
				else if (k == "rest") s.design.lips.restY = v * 1e-3;
				else if (k == "soft") s.design.lips.cornerSoft = v * 1e-3;
				else if (k == "from") s.design.lips.firmFrom = v;
				else if (k == "to") s.design.lips.firmTo = v;
				else if (k == "noise") s.design.breathNoise = v;
				else if (k == "noisehz") s.design.breathNoiseHz = v;
				else if (k == "noisefull") s.design.breathNoiseFull = v;
				else if (k == "angle") s.design.listener.angle = v * M_PI / 180.0;
				else if (k == "over") s.design.oversampling = (int) v;
				else if (k == "pull") s.design.tuningPull = v * 1e-3;
				else std::printf("unknown key %s\n", k.c_str());
				at = comma + 1;
			}
		}
		settings.push_back(s);
	}
	if (!want[0] && !want[1] && !want[2])
		want[1] = true;
	if (settings.empty()) {
		Setting s;
		s.name = "as it is";
		s.design.rate = 48000.0;
		settings.push_back(s);
	}
	if (midis.empty())
		midis = {46, 53, 58, 65};
	std::vector<TimbreNote> notes;
	for (int m : midis)
		notes.push_back(place(m));
	const int noteCount = (int) notes.size();
	// Each setting's resonance for each note, for its own tuning slide.
	std::vector<std::vector<double>> resOf(settings.size(), std::vector<double>((size_t) noteCount, 100.0));
	for (size_t si = 0; si < settings.size(); si++) {
		PULL = settings[si].design.tuningPull;
		for (int n = 0; n < noteCount; n++) {
			const std::vector<double> peaks = resonances(notes[(size_t) n].extension);
			if ((size_t) (notes[(size_t) n].resonance - 1) < peaks.size())
				resOf[si][(size_t) n] = peaks[(size_t) (notes[(size_t) n].resonance - 1)];
		}
	}
	for (const TimbreNote& n : notes)
		std::printf("  %s: resonance %d, the slide %.0f mm out\n", n.name.c_str(), n.resonance, 1000.0 * n.extension);

	// THE RECORDINGS, at 48 kHz, each from 20 ms before it is heard: from the Rack user folder, else the bench's cuts.
	const char* home = std::getenv("HOME");
	const std::string folder = std::string(home ? home : "") + "/Library/Application Support/Rack2/DreamerMPX/trombone-reference/";
	std::vector<std::vector<double>> iowaClips((size_t) noteCount * 3);
	for (int n = 0; n < noteCount; n++)
		for (int k = 0; k < 3; k++) {
			if (!want[k])
				continue;
			double rate = 44100.0;
			std::vector<double> x = readWav(folder + std::to_string(notes[(size_t) n].midi) + " " + DYNAMICS[k] + ".wav", rate);
			if (x.empty())
				x = readWav(std::string("build/iowa-cuts/") + notes[(size_t) n].tsv + " " + DYNAMICS[k] + ".wav", rate);
			if (x.empty()) {
				std::printf("no recording of %s at %s\n", notes[(size_t) n].name.c_str(), DYNAMICS[k]);
				continue;
			}
			x = resample(x, rate, 48000.0);
			double loud = 0.0;
			const size_t w50 = 2400, w2 = 96;
			for (size_t i = 0; i + w50 < x.size(); i += w2)
				loud = std::max(loud, rms(x, i, i + w50));
			size_t from = 0;
			for (size_t i = 0; i + w2 < x.size(); i += w2)
				if (rms(x, i, i + w2) > loud * 0.0316) {
					from = i > 960 ? i - 960 : 0;
					break;
				}
			iowaClips[(size_t) n * 3 + (size_t) k].assign(x.begin() + (long) from, x.begin() + (long) std::min(x.size(), from + (size_t) (1.2 * 48000.0)));
		}

	// EACH SETTING AND NOTE, shared out.
	const int jobs = (int) settings.size() * noteCount;
	std::vector<Result> results((size_t) jobs * 3);
	std::atomic<int> next{0};
	std::mutex print;
	auto work = [&]() {
		for (;;) {
			const int j = next.fetch_add(1);
			if (j >= jobs)
				return;
			const size_t si = (size_t) (j / noteCount), ni = (size_t) (j % noteCount);
			const Setting& s = settings[si];
			const TimbreNote& note = notes[ni];
			const double res = resOf[si][ni], target = hzOfMidi(note.midi);
			// The pressures, the lips tuned at each, and what each sounds like.
			struct Point { double mouth, ratio, centroid; };
			std::vector<Point> points;
			std::vector<double> out, cup;
			for (int p = 0; p <= 8; p++) {
				const double mouth = 300.0 * std::pow(2.0, 0.5 * p);
				const double ratio = tune(s.design, note.extension, res, target, mouth);
				play(s.design, note.extension, 0.03, ratio * res, mouth, 1.0, out, cup);
				const double f0 = pitchAt(cup, (size_t) (0.25 * 48000.0), 48000.0);
				if (!(f0 > 0.0) || std::fabs(cents(f0, target)) > 40.0 || swing(cup, 4800) < 50.0)
					continue;
				points.push_back({mouth, ratio, brightness(harmonics(out, f0, 48000.0), f0)});
			}
			for (int k = 0; k < 3; k++) {
				if (!want[k] || points.empty())
					continue;
				// Where the brightness reaches the recordings', on straight lines in the pressure's logarithm.
				Point use = points.front();
				if (BRIGHTNESS[k] >= points.back().centroid)
					use = points.back();
				else
					for (size_t i = 0; i + 1 < points.size(); i++)
						if (points[i].centroid <= BRIGHTNESS[k] && points[i + 1].centroid > BRIGHTNESS[k]) {
							const double u = (BRIGHTNESS[k] - points[i].centroid) / (points[i + 1].centroid - points[i].centroid);
							use.mouth = points[i].mouth * std::pow(points[i + 1].mouth / points[i].mouth, u);
							use.ratio = points[i].ratio + u * (points[i + 1].ratio - points[i].ratio);
							break;
						}
				const double mouth = use.mouth * std::pow(10.0, s.breathDb / 20.0);
				Result& r = results[(size_t) j * 3 + (size_t) k];
				play(s.design, note.extension, s.rise, use.ratio * res, mouth, 1.7, out, cup, &r.open, &r.half);
				r.played = true;
				r.mouth = mouth;
				r.lips = use.ratio;
				r.f0 = pitchAt(cup, (size_t) (0.5 * 48000.0), 48000.0);
				const std::vector<double> h = harmonics(out, r.f0, 48000.0);
				r.centroid = brightness(h, r.f0);
				r.level = 20.0 * std::log10(rms(out, out.size() - 24000, out.size()) / 20e-6);
				r.noise = between(out, out.size() - 24000, r.f0, 48000.0);
				r.growth = growth(out, 48000.0);
				const Recorded rec = recorded(note.tsv.c_str(), DYNAMICS[k]);
				if (rec.found) {
					double top = -1e9, itop = -1e9;
					for (size_t q = 0; q < 20 && q < h.size(); q++)
						top = std::max(top, h[q]);
					for (double v : rec.h)
						itop = std::max(itop, v);
					int counts[3] = {0, 0, 0};
					for (size_t q = 0; q < 20 && q < h.size(); q++) {
						const int g = q < 5 ? 0 : q < 12 ? 1 : 2;
						r.groups[g] += (h[q] - top) - (rec.h[q] - itop);
						r.each.push_back((h[q] - top) - (rec.h[q] - itop));
						counts[g]++;
					}
					for (int g = 0; g < 3; g++)
						r.groups[g] /= std::max(1, counts[g]);
				}
				r.iowa = iowaClips[ni * 3 + (size_t) k];
				if (r.iowa.size() > 36000)
					r.iowaNoise = between(r.iowa, 14400, r.f0, 48000.0);
				r.iowaGrowth = growth(r.iowa, 48000.0);
				r.clip.assign(out.begin(), out.begin() + (long) (1.2 * 48000.0));
				// As loud as the recording over the steady part.
				const double want = r.iowa.size() > 38400 ? rms(r.iowa, 14400, 38400) : 0.1;
				const double g = want / std::max(1e-12, rms(r.clip, 14400, 38400));
				for (double& v : r.clip)
					v *= g;
			}
			std::lock_guard<std::mutex> hold(print);
			std::printf("  %s: %s done\n", s.name.c_str(), note.name.c_str());
		}
	};
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency() - 3); t++)
		pool.emplace_back(work);
	for (std::thread& t : pool)
		t.join();

	auto writeWav = [](const std::string& path, std::vector<double> file) {
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f)
			return;
		const uint32_t count = (uint32_t) file.size(), bytes = count * 2, rate = 48000;
		auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
		auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
		std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
		u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
		std::fwrite("data", 1, 4, f); u32(bytes);
		for (double v : file) {
			const int16_t q = (int16_t) std::lround(std::fmax(-1.0, std::fmin(1.0, v)) * 32767.0);
			std::fwrite(&q, 2, 1, f);
		}
		std::fclose(f);
		std::printf("  wrote %s\n", path.c_str());
	};
	// A PAIR AT ONE HEALTHY LEVEL: its steady part 20 dB under full scale, or lower where a peak would pass 0.95.
	auto pairGain = [](const std::vector<double>& a, const std::vector<double>& b) {
		const std::vector<double>& ref = a.size() > 38400 ? a : b;
		double g = 0.1 / std::max(1e-12, rms(ref, 14400, 38400));
		double peak = 0.0;
		for (double v : a)
			peak = std::max(peak, std::fabs(v));
		for (double v : b)
			peak = std::max(peak, std::fabs(v));
		return std::min(g, 0.95 / std::max(1e-12, peak));
	};

	// THE NUMBERS AND EACH SETTING'S FILE.
	for (size_t si = 0; si < settings.size(); si++) {
		std::printf("\n%s\n", settings[si].name.c_str());
		std::printf("  note        dyn   mouth Pa   lips   rise ms: model, recording   brightness Hz: model, recording   harmonics against the recording, dB: 1-5, 6-12, 13-20   between them, dB: model, recording\n");
		std::vector<double> file;
		for (int k = 0; k < 3; k++)
			for (int n = 0; n < noteCount; n++) {
				const Result& r = results[(si * (size_t) noteCount + (size_t) n) * 3 + (size_t) k];
				if (!want[k])
					continue;
				if (!r.played) {
					std::printf("  %-10s  %-4s  did not sound\n", notes[(size_t) n].name.c_str(), DYNAMICS[k]);
					continue;
				}
				const Recorded rec = recorded(notes[(size_t) n].tsv.c_str(), DYNAMICS[k]);
				std::printf("  %-10s  %-4s %9.0f  %5.3f   %10.0f %10.0f   %15.0f %10.0f   %24.1f %5.1f %5.1f   %18.1f %9.1f\n", notes[(size_t) n].name.c_str(),
					DYNAMICS[k], r.mouth, r.lips, r.growth, r.iowaGrowth, r.centroid, rec.centroid, r.groups[0], r.groups[1], r.groups[2], r.noise, r.iowaNoise);
				std::printf("      the lips open %.0f per cent of each cycle, over half their widest %.0f per cent\n", 100.0 * r.open, 100.0 * r.half);
				std::printf("      harmonics 1 to 20 against the recording's, dB:");
				for (double v : r.each)
					std::printf(" %+.0f", v);
				std::printf("\n");
				const double g = pairGain(r.iowa, r.clip);
				for (double v : r.iowa)
					file.push_back(g * v);
				file.insert(file.end(), (size_t) (0.25 * 48000.0), 0.0);
				for (double v : r.clip)
					file.push_back(g * v);
				file.insert(file.end(), (size_t) (0.6 * 48000.0), 0.0);
			}
		writeWav("build/Timbre " + settings[si].name + ".wav", file);
	}
	// EACH NOTE'S FILE: the recording before each setting's note in turn.
	for (int k = 0; k < 3; k++)
		for (int n = 0; n < noteCount; n++) {
			if (!want[k])
				continue;
			std::vector<double> file;
			for (size_t si = 0; si < settings.size(); si++) {
				const Result& r = results[(si * (size_t) noteCount + (size_t) n) * 3 + (size_t) k];
				if (!r.played)
					continue;
				const double g = pairGain(r.iowa, r.clip);
				for (double v : r.iowa)
					file.push_back(g * v);
				file.insert(file.end(), (size_t) (0.4 * 48000.0), 0.0);
				for (double v : r.clip)
					file.push_back(g * v);
				file.insert(file.end(), (size_t) (1.2 * 48000.0), 0.0);
			}
			writeWav("build/Timbre " + notes[(size_t) n].name + " " + DYNAMICS[k] + ", the recording before each.wav", file);
		}
	return 0;
}
