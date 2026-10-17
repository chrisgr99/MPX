/** THE TROMBONE'S PLAYER, from a command line: phase 6 of docs/brass.md (src/BrassPlayer).

    make brassplay                     a B flat major scale over two octaves, tongued and slurred, a note
                                       from ppp to fff and a glissando: what the player chose for each
                                       note, how it sounded against what was asked, and WAVs into build/

Each note's pitch is measured on the cup's pressure over the middle half of its sounding length, by rising
crossings of its mean, against the note asked for. */
#include "../src/BrassJudge.hpp"
#include "../src/BrassPlayer.hpp"
#include "../src/BrassStart.hpp"
#include "../src/Crew.hpp"
#include <chrono>
#include <thread>
#include "../src/BrassTable.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include <string>
#include <vector>

using namespace px;

/** HOW MANY THREADS a crew gets: the machine's cores less `spare`, or MPX_THREADS where it says, so that a run
in the background leaves the computer usable. */
static int crewSize(int spare) {
	if (const char* t = std::getenv("MPX_THREADS"))
		return std::max(1, std::atoi(t));
	return std::max(1, (int) std::thread::hardware_concurrency() - spare);
}


static const double RATE = 48000.0;

static double hzOf(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static std::string nameOf(int midi) { return std::string(NAMES[midi % 12]) + std::to_string(midi / 12 - 1); }

/** A WAV, 16 bits, its peak 1 dB under full scale. */
static void writeWav(const std::string& path, const std::vector<double>& x) {
	double peak = 1e-12;
	for (double v : x)
		peak = std::max(peak, std::fabs(v));
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
		const int16_t s = (int16_t) std::lround(v * 0.891 * 32767.0 / peak);
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}

/** THE PITCH of `x` from `a` to `b`: rising crossings of its mean. */
static double pitchOf(const std::vector<double>& x, size_t a, size_t b) {
	if (b <= a + 16 || b > x.size())
		return 0.0;
	double mean = 0.0;
	for (size_t i = a; i < b; i++)
		mean += x[i];
	mean /= (double) (b - a);
	double first = -1.0, last = -1.0;
	int count = 0;
	for (size_t i = a + 1; i < b; i++) {
		const double p = x[i - 1] - mean, q = x[i] - mean;
		if (p < 0.0 && q >= 0.0) {
			const double at = (double) (i - 1) + p / (p - q);
			if (first < 0.0)
				first = at;
			last = at;
			count++;
		}
	}
	return count >= 3 ? RATE * (count - 1) / (last - first) : 0.0;
}

struct Asked {
	int midi;
	BrassPlayer::Note note;
	double at;   // seconds
};

/** PLAY A PART and report each note. */
static BrassPlayer::Habits habits;
/** Rehearse each slur (ARGS="... rehearse"). */
static bool rehearse = false;

/** THE MEAN DIP AT EACH KIND OF JOIN, dB, from the last part played. */
static double dipSum[5], dipCount[5];

/** PLAY A PART and report each note, unless `quiet`. */
static void play(const char* title, const std::vector<Asked>& part, const char* wav, bool quiet = false) {
	for (int j = 0; j < 5; j++)
		dipSum[j] = dipCount[j] = 0.0;
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	for (const Asked& a : part)
		player.add(a.note, (int64_t) std::lround(a.at * RATE));
	const double end = part.back().at + part.back().note.seconds + 0.6;
	const size_t n = (size_t) (end * RATE);
	std::vector<double> out(n), cup(n);
	for (size_t i = 0; i < n; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
	}
	if (!quiet)
		std::printf("\n%s\n", title);
	if (!quiet)
		std::printf("  note   level   asked Hz   position   resonance   slide cm   join            mouth Pa   lips   sounds   off, cents   level dB   onset ms   dip dB\n");
	const char* joins[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
	double worst = 0.0, sum = 0.0;
	int counted = 0;
	for (size_t k = 0; k < player.decisions().size(); k++) {
		const BrassPlayer::Decision& d = player.decisions()[k];
		const size_t a = (size_t) d.start, b = (size_t) d.end;
		const size_t from = a + (b - a) / 4, to = b - (b - a) / 4;
		const double hz = pitchOf(cup, from, to);
		const double off = hz > 0.0 ? 1200.0 * std::log2(hz / d.note.hz) : NAN;
		int midi = (int) std::lround(69.0 + 12.0 * std::log2(d.note.hz / 440.0));
		// ITS LEVEL over the middle half; ITS ONSET as test/brassref.cpp measures the recordings', from 30 dB
		// under that level to within 3 dB of it in 2 ms windows; and for a joined note, how far the level dips
		// at the join, 5 ms windows from 60 ms before to 60 ms after, under the lower of the two notes' levels.
		auto rms = [&](size_t i0, size_t i1) {
			double sum = 0.0;
			for (size_t i = i0; i < i1 && i < out.size(); i++)
				sum += out[i] * out[i];
			return 10.0 * std::log10(sum / (double) std::max<size_t>(1, i1 - i0) + 1e-30);
		};
		const double level = rms(from, to);
		double onset = NAN;
		if (d.join == BrassPlayer::TONGUED) {
			const size_t w = (size_t) (0.002 * RATE);
			size_t rise0 = 0, rise1 = 0;
			for (size_t i = a > (size_t) (0.1 * RATE) ? a - (size_t) (0.1 * RATE) : 0; i + w < to; i += w) {
				const double l = rms(i, i + w);
				if (!rise0 && l > level - 30.0)
					rise0 = i;
				if (rise0 && l > level - 3.0) {
					rise1 = i;
					break;
				}
			}
			onset = rise1 > rise0 ? 1000.0 * (double) (rise1 - rise0) / RATE : NAN;
		}
		double dip = NAN;
		if (d.join != BrassPlayer::TONGUED && k > 0) {
			const BrassPlayer::Decision& b4 = player.decisions()[k - 1];
			const double before = rms((size_t) b4.start + ((size_t) b4.end - (size_t) b4.start) / 4, (size_t) b4.end - ((size_t) b4.end - (size_t) b4.start) / 4);
			const size_t w = (size_t) (0.005 * RATE);
			double lowest = 1e9;
			for (size_t i = a - (size_t) (0.06 * RATE); i + w < a + (size_t) (0.06 * RATE); i += w)
				lowest = std::min(lowest, rms(i, i + w));
			dip = lowest - std::min(before, level);
		}
		if (!std::isnan(dip)) {
			dipSum[d.join] += dip;
			dipCount[d.join] += 1.0;
		}
		if (!quiet)
			std::printf("  %-5s  %5.2f  %8.2f   %8d   %9d   %8.1f   %-14s  %8.0f   %.3f  %6.1f   %+6.1f   %8.1f   %8.0f   %6.1f\n", nameOf(midi).c_str(),
			d.note.level, d.note.hz, d.position, d.resonance, 100.0 * d.extension, joins[d.join], d.mouth, d.lips, hz, off,
			10.0 * std::log10(1.0 / (20e-6 * 20e-6)) + level, onset, dip);
		if (!std::isnan(off)) {
			worst = std::max(worst, std::fabs(off));
			sum += std::fabs(off);
			counted++;
		}
	}
	if (quiet)
		return;
	std::printf("  pitch: mean %.1f cents off, worst %.1f; %zu of %zu notes played\n", counted ? sum / counted : 0.0, worst, player.decisions().size(),
		part.size());
	writeWav(wav, out);
}

static std::vector<Asked> scalePart(bool slurred);
static std::vector<Asked> lipSlurPart();

/** THE SLURS' HABITS SWEPT: the slurred scale with each, and the mean dip at each kind of join. */
static void sweep() {
	std::printf("lip slur s   natural slur s   push   |  dip dB: lip slur   natural slur   legato tongue\n");
	for (double lip : {0.03, 0.05, 0.08})
		for (double natural : {0.024, 0.05, 0.08})
			for (double push : {0.0, 0.1, 0.2}) {
				habits = BrassPlayer::Habits();
				habits.lipSlur = lip;
				habits.naturalSlur = natural;
				habits.slurPush = push;
				play("", scalePart(true), "", true);
				std::printf("  %9.3f   %14.3f   %4.1f   |  %16.1f   %12.1f   %13.1f\n", lip, natural, push, dipSum[1] / std::max(1.0, dipCount[1]),
					dipSum[2] / std::max(1.0, dipCount[2]), dipSum[3] / std::max(1.0, dipCount[3]));
				std::fflush(stdout);
			}
}

/** LIP SLURS: the B flat harmonic series in first position, slurred up and down, a beat each. */
static std::vector<Asked> lipSlurPart() {
	const int notes[11] = {46, 53, 58, 62, 65, 70, 65, 62, 58, 53, 46};
	std::vector<Asked> part;
	for (int i = 0; i < 11; i++) {
		Asked a;
		a.midi = notes[i];
		a.note.hz = hzOf(notes[i]);
		a.note.level = 4.0 / 7.0;
		a.note.seconds = 0.6;
		a.note.slurred = i < 10;
		a.at = 0.4 + 0.6 * i;
		part.push_back(a);
	}
	return part;
}

static void sweepLip() {
	std::printf("lip slur s   push   |  dip dB: lip slur\n");
	for (double lip : {0.02, 0.03, 0.05, 0.08, 0.12})
		for (double push : {0.0, 0.1, 0.2, 0.3}) {
			habits = BrassPlayer::Habits();
			habits.lipSlur = lip;
			habits.slurPush = push;
			play("", lipSlurPart(), "", true);
			std::printf("  %9.3f   %4.1f   |  %16.1f  (%.0f joins)\n", lip, push, dipSum[1] / std::max(1.0, dipCount[1]), dipCount[1]);
			std::fflush(stdout);
		}
}

static void sweepLegato() {
	std::printf("above threshold   least open   |  dip dB: legato tongue\n");
	for (double above : {1.25, 1.6, 2.0, 2.5})
		for (double least : {0.45, 0.6}) {
			habits = BrassPlayer::Habits();
			habits.legatoAbove = above;
			habits.legatoLeast = least;
			play("", scalePart(true), "", true);
			std::printf("  %15.2f   %10.2f   |  %20.1f  (%.0f joins)\n", above, least, dipSum[3] / std::max(1.0, dipCount[3]), dipCount[3]);
			std::fflush(stdout);
		}
}

static std::vector<Asked> scalePart(bool slurred) {
	const int degrees[8] = {0, 2, 4, 5, 7, 9, 11, 12};
	std::vector<int> scale;
	for (int o = 0; o < 2; o++)
		for (int i = 0; i < 7; i++)
			scale.push_back(46 + 12 * o + degrees[i]);
	scale.push_back(70);
	for (int i = (int) scale.size() - 2; i >= 0; i--)
		scale.push_back(scale[(size_t) i]);
	std::vector<Asked> part;
	for (size_t i = 0; i < scale.size(); i++) {
		Asked a;
		a.midi = scale[i];
		a.note.hz = hzOf(scale[i]);
		a.note.level = 4.0 / 7.0;
		a.note.seconds = 0.6;
		a.note.slurred = slurred && i + 1 < scale.size();
		a.at = 0.4 + 0.6 * (double) i;
		part.push_back(a);
	}
	return part;
}

/** HOW A JOIN SOUNDS: the rehearsal's own judge (src/BrassJudge). */
typedef BrassJudged Judged;

/** TWO NOTES SLURRED, the second at 1.2 s, and how the join sounds. */
static Judged slurred(int a, int b, int* join = nullptr) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	BrassPlayer::Note n;
	n.hz = hzOf(a);
	n.seconds = 0.8;
	n.slurred = true;
	player.add(n, (int64_t) (0.4 * RATE));
	n.hz = hzOf(b);
	n.slurred = false;
	player.add(n, (int64_t) (1.2 * RATE));
	const size_t total = (size_t) (1.8 * RATE);
	std::vector<double> out(total), cup(total);
	for (size_t i = 0; i < total; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
	}
	if (join)
		*join = player.decisions().back().join;
	return brassJudgeJoin(cup, out, (size_t) (1.2 * RATE), RATE, player.decisions()[0].sounds, player.decisions().back().sounds);
}

/** THE SLURS JUDGED: lip slurs, natural slurs and legato tongues up and down, each with its measures. */
static const int SLURS[][2] = {{53, 58}, {58, 53}, {58, 62}, {46, 53}, {53, 55}, {55, 53}, {46, 48}, {57, 60}, {48, 50}, {50, 48}, {57, 58}, {58, 57}};

static void judgeAll(bool each) {
	const char* joins[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
	double settle = 0, smear = 0, squawk = 0, dip = 0;
	const int n = (int) (sizeof SLURS / sizeof SLURS[0]);
	for (int i = 0; i < n; i++) {
		int kind = 0;
		const Judged j = slurred(SLURS[i][0], SLURS[i][1], &kind);
		if (each)
			std::printf("  %-4s to %-4s  %-14s  settles %4.0f ms   smear %4.0f   rough %4.0f   squawk %4.0f   gap %4.0f ms   dip %6.1f dB\n",
				nameOf(SLURS[i][0]).c_str(), nameOf(SLURS[i][1]).c_str(), joins[kind], j.settle, j.smear, j.rough, j.squawk, j.gap, j.dip);
		settle += j.settle;
		smear += j.smear;
		squawk += j.squawk;
		dip += j.dip;
	}
	std::printf("  mean: settles %.0f ms, smear %.0f ms, squawk %.0f ms, dip %.1f dB\n", settle / n, smear / n, squawk / n, dip / n);
}

/** THE SLURS' HABITS SEARCHED, each kind of join on its own notes: for each setting, the mean settling, smear
and squawk. */
static void judgeKind(int kind, const char* setting) {
	double settle = 0, smear = 0, squawk = 0, dip = 0;
	int n = 0;
	for (const auto& pair : SLURS) {
		int k = 0;
		const Judged j = slurred(pair[0], pair[1], &k);
		if (k != kind)
			continue;
		settle += j.settle; smear += j.smear; squawk += j.squawk; dip += j.dip;
		n++;
	}
	if (n)
		std::printf("  %-40s settles %4.0f   smear %4.0f   squawk %4.0f   dip %6.1f   (%d)\n", setting, settle / n, smear / n, squawk / n, dip / n, n);
	std::fflush(stdout);
}

static void searchSlurs() {
	char text[128];
	std::printf("LIP SLURS: glide seconds, push\n");
	for (double g : {0.0, 0.01, 0.02, 0.03, 0.06})
		for (double push : {0.0, 0.1, 0.2, 0.35}) {
			habits = BrassPlayer::Habits();
			habits.lipSlur = g;
			habits.slurPush = push;
			std::snprintf(text, sizeof text, "glide %.2f push %.2f", g, push);
			judgeKind(BrassPlayer::LIP_SLUR, text);
		}
	std::printf("NATURAL SLURS: slide speed, when the lips change, glide, push\n");
	for (double sp : {1.0, 0.5, 0.3})
		for (double when : {0.0, 0.5, 1.0})
			for (double push : {0.0, 0.2}) {
				habits = BrassPlayer::Habits();
				habits.slurSlide = sp;
				habits.naturalLipsAt = when;
				habits.slurPush = push;
				std::snprintf(text, sizeof text, "slide x%.1f lips at %.1f push %.2f", sp, when, push);
				judgeKind(BrassPlayer::NATURAL_SLUR, text);
			}
	std::printf("LEGATO TONGUE: slide speed, how far the lips feel over their threshold\n");
	for (double sp : {1.0, 0.5, 0.3})
		for (double above : {0.6, 0.9, 1.3, 2.0}) {
			habits = BrassPlayer::Habits();
			habits.slurSlide = sp;
			habits.legatoAbove = above;
			habits.legatoLeast = 0.1;
			std::snprintf(text, sizeof text, "slide x%.1f above %.1f", sp, above);
			judgeKind(BrassPlayer::LEGATO_TONGUE, text);
		}
}

/** THE BEST EACH LIP SLUR CAN DO, each searched on its own: the lips' overshoot, when they change, how fast
and the breath's push or dip. A clean slur settles soon with no squawk and little smear. */
static void searchEachLipSlur() {
	const int pairs[][2] = {{53, 58}, {58, 53}, {46, 53}, {58, 65}, {65, 58}, {46, 58}};
	for (const auto& pair : pairs) {
		Judged best;
		double score = 1e9;
		char text[160] = "";
		for (double over : {1.0, 1.02, 1.05, 1.1})
			for (double shift : {-0.02, -0.01, 0.0, 0.01})
				for (double g : {0.0, 0.015, 0.03})
					for (double push : {-0.3, -0.15, 0.0, 0.2}) {
						habits = BrassPlayer::Habits();
						habits.lipsOvershoot = over;
						habits.slurShift = shift;
						habits.lipSlur = g;
						habits.slurPush = push;
						int k = 0;
						const Judged j = slurred(pair[0], pair[1], &k);
						const double sc = j.score();
						if (sc < score) {
							score = sc;
							best = j;
							std::snprintf(text, sizeof text, "overshoot %.2f shift %+.3f glide %.3f push %+.2f", over, shift, g, push);
						}
					}
		std::printf("  %-4s to %-4s  best: %s   settles %3.0f ms  smear %3.0f  squawk %3.0f  dip %5.1f\n", nameOf(pair[0]).c_str(), nameOf(pair[1]).c_str(),
			text, best.settle, best.smear, best.squawk, best.dip);
		std::fflush(stdout);
	}
}

/** A SLUR'S PITCH BY AUTOCORRELATION: every 5 ms from 60 ms before the join to 150 ms after it, over the
cup's last 15 ms, the period between 1.25 ms and 16 ms with the highest correlation, as a frequency, and how
high the correlation is (one, the cup repeating exactly); with the lips' frequency as the player set them.
Zero crossings miscount where a transition has strong harmonics; this does not. */
static void joinAutocorrelated(int a, int b) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	BrassPlayer::Note n;
	n.hz = hzOf(a);
	n.seconds = 0.8;
	n.slurred = true;
	player.add(n, (int64_t) (0.4 * RATE));
	n.hz = hzOf(b);
	n.slurred = false;
	player.add(n, (int64_t) (1.2 * RATE));
	const size_t total = (size_t) (1.5 * RATE);
	std::vector<double> cup(total), out(total), lips(total), slide(total);
	for (size_t i = 0; i < total; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
		lips[i] = player.instrument().lipsHz();
		slide[i] = player.instrument().slide();
	}
	const char* joins[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
	std::printf("\n%s to %s, %s: autocorrelated pitch of the cup\n   ms   level dB   pitch Hz   repeats   lips Hz   slide cm\n", nameOf(a).c_str(),
		nameOf(b).c_str(), joins[player.decisions().back().join]);
	const size_t at = (size_t) (1.2 * RATE), w = (size_t) (0.015 * RATE);
	for (int ms = -60; ms <= 150; ms += 5) {
		const size_t i = at + (size_t) ((long) ms * (long) RATE / 1000);
		double mean = 0.0;
		for (size_t k = i - w; k < i; k++)
			mean += cup[k];
		mean /= (double) w;
		double best = -1.0;
		int lag = 0;
		for (int L = (int) (0.00125 * RATE); L <= (int) (0.016 * RATE); L++) {
			double sxy = 0, sxx = 0, syy = 0;
			for (size_t k = i - w; k < i; k++) {
				const double x = cup[k] - mean, y = cup[k - (size_t) L] - mean;
				sxy += x * y; sxx += x * x; syy += y * y;
			}
			const double c = sxy / std::sqrt(sxx * syy + 1e-30);
			if (c > best + 0.02) {
				best = c;
				lag = L;
			}
		}
		double e = 0.0;
		for (size_t k = i - (size_t) (0.005 * RATE); k < i; k++)
			e += out[k] * out[k];
		const double level = 10.0 * std::log10(e / (0.005 * RATE) / (20e-6 * 20e-6) + 1e-30);
		std::printf("  %+4d   %8.1f   %8.1f   %7.2f   %7.1f   %8.1f\n", ms, level, RATE / lag, best, lips[i], 100.0 * slide[i]);
	}
}

/** The listener's angle from the bell's axis, degrees, with angle=; the design's own where it is not given.
And the breath noise's share, with noise=, and its low pass, with noisehz=. */
static double listenerDegrees = -1.0, noiseShare = -1.0, noiseHz = -1.0;

/** For attack: the note accented, with "accent", and how long it is written, with seconds=. */
static bool attackAccent = false;
static double attackSeconds = 0.8;

/** ONE TONGUED NOTE'S START LOOKED AT CLOSELY, from silence: every 2.5 ms for 150 ms, the level a metre
away, the pitch of the cup by YIN over 10 ms and how surely it repeats, against the note. */
static void attack(int midi, double level) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	BrassPlayer::Note n;
	n.hz = hzOf(midi);
	n.level = level;
	n.seconds = attackSeconds;
	n.accent = attackAccent;
	player.add(n, (int64_t) (0.4 * RATE));
	const size_t total = (size_t) (1.0 * RATE);
	std::vector<double> cup(total), out(total);
	for (size_t i = 0; i < total; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
	}
	const BrassPlayer::Decision& d = player.decisions().back();
	std::printf("\n%s at level %.2f: mouth %.0f Pa, lips %.3f, sounds %.1f Hz\n", nameOf(midi).c_str(), level, d.mouth, d.lips, d.sounds);
	{
		// ITS SHAPE BESIDE THE RECORDINGS', every 10 ms, and how far each measure is from theirs.
		const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) (0.39 * RATE), RATE, d.sounds);
		const BrassStartTemplate& t = tromboneStart(brassStartRegister(n.hz), brassStartDynamic(level));
		double parts[4];
		const double far = brassStartDistance(shape, t, parts);
		std::printf("  from the recordings %.2f: level %.2f, pitch %.2f, repeating %.2f, brightness %.2f\n", far, parts[0], parts[1], parts[2], parts[3]);
		std::printf("    ms   level: model, recordings   cents: model, recordings   sure: model, recordings   bright: model, recordings\n");
		for (int k = 0; k < BrassStartShape::FRAMES && shape.valid; k += 2)
			std::printf("  %4d   %6.1f %6.1f   %+6.0f %+6.0f   %5.2f %5.2f   %+5.2f %+5.2f\n", k * 5, shape.level[k], t.level[k], shape.cents[k], t.cents[k],
				shape.sure[k], t.sure[k], shape.bright[k], t.bright[k]);
	}
	std::printf("   ms   level dB   pitch Hz   cents off   repeats\n");
	const size_t at = (size_t) (0.4 * RATE), w = (size_t) (0.01 * RATE);
	for (int k = 0; k <= 60; k++) {
		const size_t i = at + (size_t) (k * 0.0025 * RATE);
		double e = 0.0;
		for (size_t j = i; j < i + w; j++)
			e += out[j] * out[j];
		double sure = 0.0, sureHeard = 0.0;
		const double hz = brassPitchOf(&cup[i], (int) w, RATE, 0.55 * d.sounds, 1.7 * d.sounds, &sure);
		const double heardHz = brassPitchOf(&out[i], (int) w, RATE, 0.55 * d.sounds, 1.7 * d.sounds, &sureHeard);
		double slow = 0.0;
		const size_t w20 = (size_t) (0.02 * RATE);
		for (size_t j = i + w / 2 - w20 / 2; j < i + w / 2 + w20 / 2; j++)
			slow += cup[j];
		slow /= (double) w20;
		std::printf("  %5.1f   %8.1f   %8.1f   %+9.0f   %7.2f      heard %+6.0f c %5.2f   cup's 20 ms mean %+6.0f Pa\n", k * 2.5 + 5.0,
			10.0 * std::log10(e / w / (20e-6 * 20e-6) + 1e-30), hz, 1200.0 * std::log2(hz / d.sounds), sure,
			1200.0 * std::log2(heardHz / d.sounds), sureHeard, slow);
	}
}

/** A QUICK NOTE AFTER ANOTHER, as in the Cavatine's bar 21: a note of 0.42 s, then 0.62 s after it began a
note of 0.21 s and another after that, tongued, at mf; the second note's start every 5 ms, from 30 ms before
it to 200 ms after — the level, the cup's pitch against the note, the lips, the breath the lips feel, and the
slide. */
/** The level of the notes `after` plays, with level=. */
static double afterLevel = 4.0 / 7.0;

static void after(int before, int midi, int then) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	const int notes[3] = {before, midi, then};
	const double at[3] = {0.4, 1.02, 1.23}, length[3] = {0.42, 0.21, 0.21};
	for (int i = 0; i < 3; i++) {
		BrassPlayer::Note n;
		n.hz = hzOf(notes[i]);
		n.level = afterLevel;
		n.seconds = length[i];
		player.add(n, (int64_t) (at[i] * RATE));
	}
	const size_t total = (size_t) (1.6 * RATE);
	std::vector<double> cup(total), out(total), lips(total), mouth(total), slide(total);
	for (size_t i = 0; i < total; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
		lips[i] = player.instrument().lipsHz();
		mouth[i] = player.instrument().breathNow() * player.instrument().tongueNow();
		slide[i] = player.instrument().slide();
	}
	for (const auto& d : player.decisions())
		std::printf("  %s at %.2f s: resonance %d, slide %.0f mm, mouth %.0f Pa, lips %.3f, sounds %.1f Hz, way %d\n", nameOf(notes[&d - &player.decisions()[0]]).c_str(),
			(double) d.start / RATE, d.resonance, 1000.0 * d.extension, d.mouth, d.lips, d.sounds, d.way);
	const BrassPlayer::Decision& d = player.decisions()[1];
	std::printf("     ms   level dB   cents off   repeats   lips Hz   mouth Pa   slide mm\n");
	const size_t w = (size_t) (0.01 * RATE);
	for (int ms = -30; ms <= 200; ms += 5) {
		const size_t i = (size_t) d.start + (size_t) ((double) ms * 0.001 * RATE);
		double e = 0.0;
		for (size_t j = i; j < i + w; j++)
			e += out[j] * out[j];
		double sure = 0.0;
		const double hz = brassPitchOf(&cup[i], (int) w, RATE, 0.55 * d.sounds, 1.7 * d.sounds, &sure);
		std::printf("  %+5d   %8.1f   %+9.0f   %7.2f   %7.1f   %8.0f   %8.1f\n", ms, 10.0 * std::log10(e / (double) w / 4e-10), 1200.0 * std::log2(hz / d.sounds),
			sure, lips[i], mouth[i], 1000.0 * slide[i]);
	}
}

/** A WAV, mono, 16 bits, as doubles; its rate into `rate`. Empty where it is anything else. */
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
			for (size_t j = i + 8; j + 1 < std::min(b.size(), i + 8 + size); j += 2)
				x.push_back((double) (int16_t) (b[j] | b[j + 1] << 8) / 32768.0);
		i += 8 + size + (size & 1);
	}
	return x;
}

/** FROM ONE RATE TO ANOTHER, by a windowed sinc of 32 taps, low passed under the lower rate's half. */
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
			const double d = t - (double) k;
			const double sinc = std::fabs(d) < 1e-9 ? cut : std::sin(M_PI * cut * d) / (M_PI * d);
			const double window = 0.5 + 0.5 * std::cos(M_PI * d / 16.0);
			sum += x[(size_t) k] * sinc * window;
		}
		y[i] = sum;
	}
	return y;
}

/** THE STARTS THE REHEARSAL CHOOSES AMONG, BY EAR: the Iowa recording's note first, then the model's, the same
note and marking, each started one of the ways a rehearsal tries and the last the way it chose, each its
first 1.2 s, as loud over its steady part as the recording's, half a second apart, to build/NAME.wav. And for
each, how far its start is from the recordings'. */
static void audition(int midi, const char* dynamic, const char* name) {
	const double level = std::strcmp(dynamic, "pp") == 0 ? 1.0 / 7.0 : std::strcmp(dynamic, "ff") == 0 ? 6.0 / 7.0 : 4.0 / 7.0;
	double iowaRate = 44100.0;
	const std::string cut = "build/iowa-cuts/" + nameOf(midi) + " " + dynamic + ".wav";
	std::vector<double> iowa = readWav(cut, iowaRate);
	if (iowa.empty()) {
		std::printf("no %s: make brassref ARGS=\"cut %s %s '%s'\" first\n", cut.c_str(), nameOf(midi).c_str(), dynamic, cut.c_str());
		return;
	}
	iowa = resample(iowa, iowaRate, RATE);
	auto rmsOver = [](const std::vector<double>& x, size_t a, size_t b) {
		double s2 = 0.0;
		for (size_t i = a; i < b && i < x.size(); i++)
			s2 += x[i] * x[i];
		return std::sqrt(s2 / (double) std::max<size_t>(1, b - a));
	};
	// Where the recording's note is heard from: the first 2 ms within 30 dB of its loudest 50 ms.
	size_t from = 0;
	{
		double loud = 0.0;
		const size_t w50 = (size_t) (0.05 * RATE), w2 = (size_t) (0.002 * RATE);
		for (size_t i = 0; i + w50 < iowa.size(); i += w2)
			loud = std::max(loud, rmsOver(iowa, i, i + w50));
		for (size_t i = 0; i + w2 < iowa.size(); i += w2)
			if (rmsOver(iowa, i, i + w2) > loud * 0.0316) {
				from = i > (size_t) (0.02 * RATE) ? i - (size_t) (0.02 * RATE) : 0;
				break;
			}
	}
	const size_t length = (size_t) (1.2 * RATE), fade = (size_t) (0.05 * RATE);
	std::vector<std::vector<double>> clips;
	std::vector<double> head(iowa.begin() + (long) from, iowa.begin() + (long) std::min(iowa.size(), from + length));
	clips.push_back(head);
	const double want = rmsOver(head, (size_t) (0.3 * RATE), (size_t) (0.8 * RATE));
	struct Way {
		const char* what;
		double lips, rise, floor, over;
		bool rehearsed;
	};
	const Way ways[] = {
		{"a step of breath, the lips at the note (the old start)", 1.0, 0.0, 1.0, 0.08, false},
		{"a step of breath, the lips 6 per cent sharp", 1.06, 0.0, 1.0, 0.08, false},
		{"rising over 40 ms from 1.3 times the threshold", 1.0, 0.04, 1.3, 0.0, false},
		{"rising over 80 ms from 1.3 times the threshold", 1.0, 0.08, 1.3, 0.0, false},
		{"rising over 140 ms from 1.3 times the threshold", 1.0, 0.14, 1.3, 0.0, false},
		{"rising over 300 ms from 1.3 times the threshold", 1.0, 0.3, 1.3, 0.0, false},
		{"rising over 80 ms from half the threshold", 1.0, 0.08, 0.5, 0.0, false},
		{"rising over 140 ms from twice the threshold", 1.0, 0.14, 2.0, 0.0, false},
		{"the rehearsal's choice", 1.0, 0.0, 1.0, 0.0, true},
	};
	std::printf("  1. the Iowa recording, %s %s\n", nameOf(midi).c_str(), dynamic);
	int number = 2;
	for (const Way& w : ways) {
		BrassPlayer::Habits h = habits;
		h.startLips = w.lips;
		h.startGlide = 0.04;
		h.startRise = w.rise;
		h.startFloor = w.floor;
		h.overOrdinary = w.over;
		BrassInstrument::Design design;
		design.rate = RATE;
		if (listenerDegrees >= 0.0)
			design.listener.angle = listenerDegrees * M_PI / 180.0;
		if (noiseShare >= 0.0)
			design.breathNoise = noiseShare;
		if (noiseHz > 0.0)
			design.breathNoiseHz = noiseHz;
		BrassPlayer player;
		player.setup(design, tromboneTable(), h);
		player.setRehearsing(w.rehearsed);
		BrassPlayer::Note n;
		n.hz = hzOf(midi);
		n.level = level;
		n.seconds = 1.0;
		const int64_t at = (int64_t) (0.3 * RATE);
		player.add(n, at);
		std::vector<double> out((size_t) at + length);
		for (size_t i = 0; i < out.size(); i++)
			out[i] = player.process();
		// From 20 ms before the tongue lets go, as the recording's is from 20 ms before it is heard.
		std::vector<double> clip(out.begin() + at - (long) (0.02 * RATE), out.begin() + at - (long) (0.02 * RATE) + (long) length);
		const double g = want / std::max(1e-12, rmsOver(clip, (size_t) (0.3 * RATE), (size_t) (0.8 * RATE)));
		for (double& v : clip)
			v *= g;
		const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) (at - (int64_t) (0.01 * RATE)), RATE, player.decisions().back().sounds);
		double parts[4] = {0, 0, 0, 0};
		const double far = brassStartDistance(shape, tromboneStart(brassStartRegister(n.hz), brassStartDynamic(level)), parts);
		const double late = 1000.0 * ((double) shape.onset - (double) at) / RATE;
		std::printf("  %d. %s: from the recordings %.2f (level %.1f, pitch %.1f, repeating %.1f, brightness %.1f), heard %.0f ms after the tongue\n", number++,
			w.what, far, parts[0], parts[1], parts[2], parts[3], late);
		clips.push_back(clip);
	}
	std::vector<double> all;
	for (std::vector<double>& c : clips) {
		for (size_t i = 0; i < fade && i < c.size(); i++)
			c[c.size() - 1 - i] *= (double) i / (double) fade;
		all.insert(all.end(), c.begin(), c.end());
		all.insert(all.end(), (size_t) (0.5 * RATE), 0.0);
	}
	double peak = 0.0;
	for (double v : all)
		peak = std::max(peak, std::fabs(v));
	for (double& v : all)
		v *= 0.9 / std::max(1e-12, peak);
	const std::string path = std::string("build/") + name + ".wav";
	FILE* f = std::fopen(path.c_str(), "wb");
	const uint32_t count = (uint32_t) all.size(), bytes = count * 2, r = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(r); u32(r * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (double v : all) {
		const int16_t q = (int16_t) std::lround(v * 32767.0);
		std::fwrite(&q, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}

/** THE STARTS BY EAR: one note at one marking started eleven of the ways a rehearsal tries — the lips at the
note or 6 per cent over it, the breath a step or rising from half, 1.3 or twice the threshold over 40 to 300
ms — and the way the rehearsal chose; of those, only the clean ones (brassStartClean: no more blip or blat
than the recordings), the five nearest the recordings' starts at most, the rehearsal's always among them if
clean; each played after the Iowa recording of the note, so the sound aimed at is always just before it;
each 1.5 s, as loud as each other over their steady parts, 20 dB under full scale, to build/NAME.wav. The
rest are listed, and why they were left out. */
static void startsByEar(int midi, const char* dynamic, const char* name) {
	const double level = std::strcmp(dynamic, "pp") == 0 ? 1.0 / 7.0 : std::strcmp(dynamic, "ff") == 0 ? 6.0 / 7.0 : 4.0 / 7.0;
	struct Way {
		const char* what;
		double lips, rise, floor, over;
		bool rehearsed;
	};
	const Way ways[] = {
		{"a step of breath, a little over its mark, the lips at the note", 1.0, 0.0, 1.0, 0.08, false},
		{"a step of breath, a little over its mark, the lips 6 per cent sharp", 1.06, 0.0, 1.0, 0.08, false},
		{"rising over 40 ms from half the threshold", 1.0, 0.04, 0.5, 0.0, false},
		{"rising over 40 ms from 1.3 times the threshold", 1.0, 0.04, 1.3, 0.0, false},
		{"rising over 80 ms from half the threshold", 1.0, 0.08, 0.5, 0.0, false},
		{"rising over 80 ms from 1.3 times the threshold", 1.0, 0.08, 1.3, 0.0, false},
		{"rising over 140 ms from 1.3 times the threshold", 1.0, 0.14, 1.3, 0.0, false},
		{"rising over 140 ms from twice the threshold", 1.0, 0.14, 2.0, 0.0, false},
		{"rising over 200 ms from 1.3 times the threshold", 1.0, 0.2, 1.3, 0.0, false},
		{"rising over 300 ms from 1.3 times the threshold", 1.0, 0.3, 1.3, 0.0, false},
		{"rising over 140 ms from 1.3 times the threshold, the lips 6 per cent sharp", 1.06, 0.14, 1.3, 0.0, false},
		{"the rehearsal's choice", 1.0, 0.0, 1.0, 0.0, true},
	};
	static const double rises[10] = {0.0, 0.04, 0.04, 0.08, 0.08, 0.14, 0.14, 0.2, 0.2, 0.3};
	static const double floors[10] = {1.0, 0.5, 1.3, 0.5, 1.3, 1.3, 2.0, 1.3, 2.0, 1.3};
	const size_t length = (size_t) (1.5 * RATE), steadyFrom = (size_t) (0.6 * RATE), steadyTo = (size_t) (1.2 * RATE);
	std::vector<std::vector<double>> clips;
	std::vector<double> steady, distances;
	std::vector<bool> clean;
	std::vector<std::string> whats;
	// THE RECORDING, from 20 ms before it is heard.
	std::vector<double> recording;
	{
		double iowaRate = 44100.0;
		std::vector<double> x = readWav("build/iowa-cuts/" + nameOf(midi) + " " + dynamic + ".wav", iowaRate);
		if (!x.empty()) {
			x = resample(x, iowaRate, RATE);
			auto rmsOver = [&](size_t a, size_t b) {
				double s2 = 0.0;
				for (size_t i = a; i < b && i < x.size(); i++)
					s2 += x[i] * x[i];
				return std::sqrt(s2 / (double) std::max<size_t>(1, b - a));
			};
			const size_t w50 = (size_t) (0.05 * RATE), w2 = (size_t) (0.002 * RATE);
			double loud = 0.0;
			for (size_t i = 0; i + w50 < x.size(); i += w2)
				loud = std::max(loud, rmsOver(i, i + w50));
			size_t from = 0;
			for (size_t i = 0; i + w2 < x.size(); i += w2)
				if (rmsOver(i, i + w2) > loud * 0.0316) {
					from = i > (size_t) (0.02 * RATE) ? i - (size_t) (0.02 * RATE) : 0;
					break;
				}
			recording.assign(x.begin() + (long) from, x.begin() + (long) std::min(x.size(), from + length));
			recording.resize(length, 0.0);
		}
	}
	std::printf("%s %s:\n", nameOf(midi).c_str(), dynamic);
	if (recording.empty())
		std::printf("  (no build/iowa-cuts/%s %s.wav: the recording left out)\n", nameOf(midi).c_str(), dynamic);
	for (const Way& w : ways) {
		BrassPlayer::Habits h = habits;
		h.startLips = w.lips;
		h.startGlide = 0.04;
		h.startRise = w.rise;
		h.startFloor = w.floor;
		h.overOrdinary = w.over;
		BrassInstrument::Design design;
		design.rate = RATE;
		BrassPlayer player;
		player.setup(design, tromboneTable(), h);
		player.setRehearsing(w.rehearsed);
		BrassPlayer::Note n;
		n.hz = hzOf(midi);
		n.level = level;
		n.seconds = 1.6;
		const int64_t at = (int64_t) (0.3 * RATE);
		player.add(n, at);
		std::vector<double> out((size_t) at + length);
		for (size_t i = 0; i < out.size(); i++)
			out[i] = player.process();
		std::vector<double> clip(out.begin() + at - (long) (0.02 * RATE), out.begin() + at - (long) (0.02 * RATE) + (long) length);
		const BrassPlayer::Decision& d = player.decisions().back();
		const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) (at - (int64_t) (0.01 * RATE)), RATE, d.sounds);
		double parts[4] = {0, 0, 0, 0};
		const double far = brassStartDistance(shape, tromboneStart(brassStartRegister(n.hz), brassStartDynamic(level)), parts);
		std::string what = w.what;
		if (w.rehearsed && d.way >= 0) {
			char chosen[160];
			const int b = (d.way / 2) % 10;
			std::snprintf(chosen, sizeof chosen, ": the lips %s, %s%s", d.way / 20 == 0 ? "at the note" : d.way / 20 == 1 ? "3 per cent sharp" : "6 per cent sharp",
				rises[b] == 0.0 ? "a step of breath" : (std::string("rising over ") + std::to_string((int) std::lround(1000.0 * rises[b])) + " ms from "
					+ (floors[b] == 0.5 ? "half" : floors[b] == 2.0 ? "twice" : "1.3 times") + " the threshold").c_str(),
				d.way % 2 ? ", a little over its mark" : "");
			what += chosen;
		}
		char line[512];
		std::snprintf(line, sizeof line, "%s: from the recordings %.2f, heard %.0f ms after the tongue, blip %.1f dB, blat %.1f dB", what.c_str(), far,
			1000.0 * ((double) shape.onset - (double) at) / RATE, shape.blip, shape.blat);
		whats.push_back(line);
		distances.push_back(far);
		clean.push_back(brassStartClean(shape));
		double s2 = 0.0;
		for (size_t i = steadyFrom; i < steadyTo; i++)
			s2 += clip[i] * clip[i];
		steady.push_back(std::sqrt(s2 / (double) (steadyTo - steadyFrom)));
		clips.push_back(clip);
	}
	// THE ONES PLAYED: the clean, nearest the recordings first, five at most, the rehearsal's among them.
	std::vector<size_t> order;
	for (size_t i = 0; i < clips.size(); i++)
		if (clean[i])
			order.push_back(i);
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return distances[a] < distances[b]; });
	const size_t chosen = clips.size() - 1;
	if (order.size() > 5) {
		const bool hasChosen = std::find(order.begin(), order.begin() + 5, chosen) != order.begin() + 5;
		const bool chosenClean = clean[chosen];
		order.resize(5);
		if (chosenClean && !hasChosen)
			order.back() = chosen;
	}
	std::printf("  played, each after the recording:\n");
	for (size_t k = 0; k < order.size(); k++)
		std::printf("  %2zu. %s\n", k + 1, whats[order[k]].c_str());
	if (order.empty())
		std::printf("  none clean\n");
	std::printf("  left out:\n");
	for (size_t i = 0; i < clips.size(); i++)
		if (std::find(order.begin(), order.end(), i) == order.end())
			std::printf("      %s%s\n", whats[i].c_str(), clean[i] ? "" : " (a blip or a blat)");
	// THE RECORDING, as loud over its steady part as the middle of the model's.
	std::vector<double> sorted = steady;
	std::sort(sorted.begin(), sorted.end());
	const double middle = sorted[sorted.size() / 2];
	if (!recording.empty()) {
		double s2 = 0.0;
		for (size_t i = steadyFrom; i < steadyTo; i++)
			s2 += recording[i] * recording[i];
		const double r = std::sqrt(s2 / (double) (steadyTo - steadyFrom));
		for (double& v : recording)
			v *= middle / std::max(1e-12, r);
	}
	{
		std::vector<std::vector<double>> played;
		for (size_t i : order) {
			if (!recording.empty())
				played.push_back(recording);
			played.push_back(clips[i]);
		}
		clips = played;
	}
	// ONE GAIN FOR ALL: the middle steady level at 20 dB under full scale, less where a peak would pass 0.95.
	double g = 0.1 / std::max(1e-12, middle);
	double peak = 0.0;
	for (const std::vector<double>& c : clips)
		for (double v : c)
			peak = std::max(peak, std::fabs(v));
	g = std::min(g, 0.95 / std::max(1e-12, peak));
	const size_t fade = (size_t) (0.05 * RATE);
	// Half a second between the recording and the try after it, a second and a half before the next pair.
	std::vector<double> all;
	for (size_t k = 0; k < clips.size(); k++) {
		std::vector<double>& c = clips[k];
		for (size_t i = 0; i < fade; i++)
			c[c.size() - 1 - i] *= (double) i / (double) fade;
		for (double v : c)
			all.push_back(g * v);
		const bool afterRecording = !recording.empty() && k % 2 == 0;
		all.insert(all.end(), (size_t) ((afterRecording ? 0.5 : 1.5) * RATE), 0.0);
	}
	const std::string path = std::string("build/") + name + ".wav";
	FILE* f = std::fopen(path.c_str(), "wb");
	const uint32_t count = (uint32_t) all.size(), bytes = count * 2, r = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(r); u32(r * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (double v : all) {
		const int16_t q = (int16_t) std::lround(std::fmax(-1.0, std::fmin(1.0, v)) * 32767.0);
		std::fwrite(&q, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}

/** TONGUED STARTS JUDGED: notes from silence across the range at pp, mf and ff, each judged from its
tongue's release (BrassJudge::brassJudgeStart), rehearsed or not as asked, the rehearsals shared out over a
crew of the machine's threads. */
static void starts() {
	static Crew crew(crewSize(1));
	const int notes[6] = {46, 53, 58, 62, 65, 70};
	const double levels[3] = {1.0 / 7.0, 4.0 / 7.0, 6.0 / 7.0};
	double settle = 0, smear = 0, rough = 0, squawk = 0, slow = 0, likeSum = 0;
	int count = 0;
	const auto t0 = std::chrono::steady_clock::now();
	std::printf("  note   dynamic   settles ms   smear   rough   squawk   slow rise   tried   from the recordings' starts\n");
	for (int m : notes)
		for (double level : levels) {
			BrassInstrument::Design design;
			design.rate = RATE;
			BrassPlayer player;
			player.setup(design, tromboneTable(), habits);
			player.setRehearsing(rehearse);
			player.setCrew(&crew);
			BrassPlayer::Note n;
			n.hz = hzOf(m);
			n.level = level;
			n.seconds = 0.6;
			player.add(n, (int64_t) (0.4 * RATE));
			const size_t total = (size_t) (0.9 * RATE);
			std::vector<double> cup(total), out(total);
			for (size_t i = 0; i < total; i++) {
				out[i] = player.process();
				cup[i] = player.instrument().cupPressure();
			}
			const BrassPlayer::Decision& d = player.decisions().back();
			std::vector<double> c(cup.begin(), cup.begin() + (long) (0.4 * RATE + 0.25 * RATE));
			std::vector<double> o(out.begin(), out.begin() + (long) (0.4 * RATE + 0.25 * RATE));
			const BrassJudged j = brassJudgeStart(c, o, (size_t) (0.4 * RATE), RATE, d.sounds);
			const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) (0.39 * RATE), RATE, d.sounds);
			const double like = brassStartDistance(shape, tromboneStart(brassStartRegister(n.hz), brassStartDynamic(n.level)));
			likeSum += like;
			// The way chosen, as BrassPlayer::candidates lists a tongued start's: lips, then breath, then over.
			static const double rises[10] = {0.0, 0.04, 0.04, 0.08, 0.08, 0.14, 0.14, 0.2, 0.2, 0.3};
			static const double floors[10] = {1.0, 0.5, 1.3, 0.5, 1.3, 1.3, 2.0, 1.3, 2.0, 1.3};
			char way[64] = "";
			if (d.way >= 0)
				std::snprintf(way, sizeof way, "lips %.2f, rise %.2f from %.1f, over %.2f", 1.0 + 0.03 * (d.way / 20), rises[(d.way / 2) % 10],
					floors[(d.way / 2) % 10], d.way % 2 ? 0.08 : 0.0);
			std::printf("  %-5s  %7s   %10.0f   %5.0f   %5.0f   %6.0f   %9.0f   %5d   %6.2f   %s\n", nameOf(m).c_str(), level < 0.3 ? "pp" : level < 0.7 ? "mf" : "ff",
				j.settle, j.smear, j.rough, j.squawk, 3.0 * j.gap, d.tried, like, way);
			settle += j.settle; smear += j.smear; rough += j.rough; squawk += j.squawk; slow += 3.0 * j.gap;
			count++;
		}
	std::printf("  mean: settles %.0f ms, smear %.0f, rough %.0f, squawk %.0f, slow rise %.0f, from the recordings %.2f; %.1f s of computing\n", settle / count,
		smear / count, rough / count, squawk / count, slow / count, likeSum / count, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

/** ONE SLUR LOOKED AT CLOSELY: two notes slurred, and every 5 ms from 150 ms before the join to 200 ms after
it, the level and the pitch, from the cup's last two periods, against the two notes; with the slide, the lips
and the breath and tongue as the player set them. */
static void join(int a, int b) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer player;
	player.setup(design, tromboneTable(), habits);
	player.setRehearsing(rehearse);
	BrassPlayer::Note n;
	n.hz = hzOf(a);
	n.seconds = 0.8;
	n.slurred = true;
	player.add(n, (int64_t) (0.4 * RATE));
	n.hz = hzOf(b);
	n.slurred = false;
	player.add(n, (int64_t) (1.2 * RATE));
	const size_t total = (size_t) (2.4 * RATE);
	std::vector<double> out(total), cup(total), slide(total), lips(total), mouth(total);
	for (size_t i = 0; i < total; i++) {
		out[i] = player.process();
		cup[i] = player.instrument().cupPressure();
		slide[i] = player.instrument().slide();
		mouth[i] = player.instrument().breathNow() * player.instrument().tongueNow();
	}
	const char* joins[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
	const BrassPlayer::Decision& d = player.decisions().back();
	std::printf("\n%s to %s: %s, slide %.1f to %.1f cm\n", nameOf(a).c_str(), nameOf(b).c_str(), joins[d.join], 100.0 * player.decisions()[0].extension,
		100.0 * d.extension);
	std::printf("   ms   level dB   pitch Hz   against %s, cents   against %s   slide cm   mouth Pa\n", nameOf(a).c_str(), nameOf(b).c_str());
	const size_t at = (size_t) (1.2 * RATE), w = (size_t) (0.005 * RATE);
	for (int ms = -150; ms <= 200; ms += 5) {
		const size_t i = at + (size_t) ((long) ms * (long) RATE / 1000);
		double sum = 0.0;
		for (size_t k = i - w; k < i; k++)
			sum += out[k] * out[k];
		const double level = 10.0 * std::log10(sum / w / (20e-6 * 20e-6) + 1e-30);
		// The last two rising crossings of the cup's 20 ms mean, before i.
		double mean = 0.0;
		const size_t span = (size_t) (0.02 * RATE);
		for (size_t k = i - span; k < i; k++)
			mean += cup[k];
		mean /= (double) span;
		double c1 = -1, c2 = -1;
		for (size_t k = i - 1; k > i - span; k--) {
			const double p = cup[k - 1] - mean, q = cup[k] - mean;
			if (p < 0.0 && q >= 0.0) {
				const double t = (double) (k - 1) + p / (p - q);
				if (c1 < 0)
					c1 = t;
				else {
					c2 = t;
					break;
				}
			}
		}
		const double hz = c1 > 0 && c2 > 0 ? RATE / (c1 - c2) : 0.0;
		std::printf("  %+4d   %8.1f   %8.1f   %+20.0f   %+12.0f   %8.1f   %8.0f\n", ms, level, hz, hz > 0 ? 1200.0 * std::log2(hz / hzOf(a)) : 0.0,
			hz > 0 ? 1200.0 * std::log2(hz / hzOf(b)) : 0.0, 100.0 * slide[i], mouth[i]);
	}
}

/** HOW LOUD IT GETS: notes across the range at mf, ff and fff, each the peak and the loudest 150 ms a metre
away, pascals, for setting the module's volts. */
static void peaks() {
	std::printf("  note    level   peak Pa   loudest 150 ms Pa\n");
	for (int m : {40, 46, 53, 58, 65, 70, 74})
		for (double level : {4.0 / 7.0, 6.0 / 7.0, 1.0}) {
			BrassInstrument::Design design;
			design.rate = RATE;
			BrassPlayer player;
			player.setup(design, tromboneTable(), habits);
			BrassPlayer::Note n;
			n.hz = hzOf(m);
			n.level = level;
			n.seconds = 1.2;
			player.add(n, (int64_t) (0.2 * RATE));
			const size_t total = (size_t) (1.6 * RATE), w = (size_t) (0.15 * RATE);
			std::vector<double> out(total);
			double peak = 0.0, run = 0.0, best = 0.0;
			for (size_t i = 0; i < total; i++) {
				out[i] = player.process();
				peak = std::max(peak, std::fabs(out[i]));
				run += out[i] * out[i];
				if (i >= w)
					run -= out[i - w] * out[i - w];
				best = std::max(best, run);
			}
			std::printf("  %-5s   %5.2f   %7.2f   %7.2f\n", nameOf(m).c_str(), level, peak, std::sqrt(best / (double) w));
		}
}

/** THE MARKINGS' LOUDNESS, measured as make brassref measures the Iowa recordings: each of the eight, ppp to
fff, on every third semitone from E2 to A sharp 4 — as it is blown with "raw", else with each note's gain
for its loudness — the level over the steady middle of a note of 1.2 s, and
the median by register of each marking against mf. The Iowa recordings' are printed beside it: pp and ff
against mf, research/brass/iowa-tenor-trombone.tsv. */
static void ladder(bool raw) {
	static const char* const MARK[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
	std::vector<double> against[3][8];
	for (int m = 40; m <= 70; m += 3) {
		double db[8], predicted[8];
		for (int k = 0; k < 8; k++) {
			BrassInstrument::Design design;
			design.rate = RATE;
			BrassPlayer player;
			player.setup(design, tromboneTable(), habits);
			player.setEvenLoudness(!raw);
			BrassPlayer::Note n;
			n.hz = hzOf(m);
			n.level = k / 7.0;
			n.seconds = 1.2;
			player.add(n, (int64_t) (0.2 * RATE));
			const size_t total = (size_t) (1.4 * RATE);
			double sum = 0.0;
			for (size_t i = 0; i < total; i++) {
				const double v = player.process();
				// The middle half of the note.
				if (i >= (size_t) (0.5 * RATE) && i < (size_t) (1.1 * RATE))
					sum += v * v;
			}
			db[k] = 10.0 * std::log10(sum / (0.6 * RATE) / (20e-6 * 20e-6));
			predicted[k] = player.tableLevel(player.decisions().back());
		}
		std::printf("  tables");
		for (int k = 0; k < 8; k++)
			std::printf("  %5.1f", predicted[k]);
		std::printf("\n");
		const int reg = m < 48 ? 0 : m < 60 ? 1 : 2;
		std::printf("  %-5s", nameOf(m).c_str());
		for (int k = 0; k < 8; k++) {
			against[reg][k].push_back(db[k] - db[4]);
			std::printf("  %5.1f", db[k]);
		}
		std::printf("\n");
	}
	auto median = [](std::vector<double> v) {
		std::sort(v.begin(), v.end());
		return v.empty() ? 0.0 : v[v.size() / 2];
	};
	static const char* const REG[3] = {"E2 to B2", "C3 to B3", "C4 to C5"};
	std::printf("\n  against mf  ");
	for (int k = 0; k < 8; k++)
		std::printf("  %5s", MARK[k]);
	std::printf("\n");
	for (int r = 0; r < 3; r++) {
		std::printf("  %-10s  ", REG[r]);
		for (int k = 0; k < 8; k++)
			std::printf("  %+5.1f", median(against[r][k]));
		std::printf("\n");
	}
	std::vector<double> all[8];
	for (int r = 0; r < 3; r++)
		for (int k = 0; k < 8; k++)
			all[k].insert(all[k].end(), against[r][k].begin(), against[r][k].end());
	std::printf("  %-10s  ", "all");
	for (int k = 0; k < 8; k++)
		std::printf("  %+5.1f", median(all[k]));
	std::printf("\n  Iowa: pp -13.1, ff +7.8 (E2 to B2 -12.6 +8.0, C3 to B3 -13.8 +8.5, C4 to C5 -11.4 +6.8)\n");
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "rehearse") == 0)
			rehearse = true;
		if (std::strcmp(argv[i], "accent") == 0)
			attackAccent = true;
		// A start's habits, for trying: rise=0.06 floor=0.3 startlips=1.0 over=0.08
		const std::string a = argv[i];
		const size_t eq = a.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string k = a.substr(0, eq);
		const double v = std::atof(a.c_str() + eq + 1);
		if (k == "rise")
			habits.startRise = v;
		else if (k == "floor")
			habits.startFloor = v;
		else if (k == "startlips")
			habits.startLips = v;
		else if (k == "over")
			habits.overOrdinary = v;
		else if (k == "seconds")
			attackSeconds = v;
		else if (k == "level")
			afterLevel = v;
		else if (k == "release")
			habits.releaseOrdinary = v;
		else if (k == "angle")
			listenerDegrees = v;
		else if (k == "noise")
			noiseShare = v;
		else if (k == "noisehz")
			noiseHz = v;
	}
	if (argc > 1 && std::strcmp(argv[1], "eachlip") == 0) {
		searchEachLipSlur();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "slursearch") == 0) {
		searchSlurs();
		return 0;
	}
	if (argc > 3 && std::strcmp(argv[1], "joinx") == 0) {
		joinAutocorrelated(std::atoi(argv[2]), std::atoi(argv[3]));
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "ladder") == 0) {
		ladder(argc > 2 && std::strcmp(argv[2], "raw") == 0);
		return 0;
	}
	if (argc > 4 && std::strcmp(argv[1], "after") == 0) {
		after(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
		return 0;
	}
	if (argc > 5 && std::strcmp(argv[1], "lipsfor") == 0) {
		// THE LIPS' SETTING for a note with its own lips: lipsfor MIDI DYN MASS QUALITY [TENSION...]; with
		// tensions, the breath and the slide found for each.
		BrassInstrument::Design design;
		design.rate = RATE;
		BrassPlayer::Note n;
		n.hz = hzOf(std::atoi(argv[2]));
		n.level = std::strcmp(argv[3], "pp") == 0 ? 1.0 / 7.0 : std::strcmp(argv[3], "ff") == 0 ? 6.0 / 7.0 : 4.0 / 7.0;
		n.seconds = 0.75;
		n.mass = std::atof(argv[4]);
		n.quality = std::atof(argv[5]);
		static Crew crew(crewSize(3));
		for (int a = 6; a <= std::max(6, argc - 1); a++) {
			n.tunedLips = a < argc ? std::atof(argv[a]) : -1.0;
			const auto t0 = std::chrono::steady_clock::now();
			const BrassPlayer::Tuned t = BrassPlayer::tuneFor(design, tromboneTable(), habits, n, &crew);
			std::printf("  mouth %.0f Pa, the lips at %.3f of the resonance, the slide %+.0f cents, left %+.0f cents; %.1f s\n", t.mouth, t.lips,
				t.slideCents, t.offCents, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		return 0;
	}
	if (argc > 3 && std::strcmp(argv[1], "options") == 0) {
		// THE START OPTIONS mpxTrombone's tuning offers for a note, best first.
		static Crew crew(crewSize(3));
		BrassInstrument::Design design;
		design.rate = RATE;
		BrassPlayer::Note n;
		n.hz = hzOf(std::atoi(argv[2]));
		n.level = std::strcmp(argv[3], "pp") == 0 ? 1.0 / 7.0 : std::strcmp(argv[3], "ff") == 0 ? 6.0 / 7.0 : 4.0 / 7.0;
		n.seconds = 0.75;
		const auto t0 = std::chrono::steady_clock::now();
		const std::vector<BrassPlayer::StartTry> tries = BrassPlayer::startsFor(design, tromboneTable(), habits, n, &crew);
		for (const BrassPlayer::StartTry& t : tries)
			std::printf("  rise %3.0f ms, lips %.2f: %s, %.0f\n", 1000.0 * t.rise, t.lips, t.clean ? "clean" : "blip or blat", t.distance);
		std::printf("  %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		return 0;
	}
	if (argc > 5 && std::strcmp(argv[1], "phrase") == 0) {
		// A PHRASE FROM SETTINGS FOUND BY EAR: phrase SETTINGS.tsv TEMPO DYN NOTE..., each NOTE MIDI:BEATS, :s
		// where it is slurred into the next, a MIDI of nought a rest; each note's lips, breath and start from
		// the file's line for its pitch and marking (midi, dynamic, tension, gap mm, damp, mass, mouth Pa, rise
		// s, floor, curve — the keepers' columns, research/brass/trombone-keepers.tsv), its slide found for them
		// (tuneFor), the joins the player's own. Written to build/Phrase.wav.
		std::map<std::pair<int, std::string>, std::vector<double>> settings;
		{
			FILE* f = std::fopen(argv[2], "r");
			char line[2048];
			while (f && std::fgets(line, sizeof line, f)) {
				if (line[0] == '#' || !std::isdigit((unsigned char) line[0]))
					continue;
				std::vector<std::string> cells;
				std::string cell;
				for (const char* c = line; *c && *c != '\n'; c++) {
					if (*c == '\t') {
						cells.push_back(cell);
						cell.clear();
					}
					else
						cell += *c;
				}
				cells.push_back(cell);
				if (cells.size() < 10)
					continue;
				std::vector<double> v;
				for (size_t i = 2; i < 10; i++)
					v.push_back(std::atof(cells[i].c_str()));
				settings[std::make_pair(std::atoi(cells[0].c_str()), cells[1])] = v;
			}
			if (f)
				std::fclose(f);
		}
		const double beat = 60.0 / std::atof(argv[3]);
		const std::string dyn = argv[4];
		const double level = dyn == "pp" ? 1.0 / 7.0 : dyn == "ff" ? 6.0 / 7.0 : 4.0 / 7.0;
		BrassInstrument::Design design;
		design.rate = RATE;
		static Crew crew(crewSize(3));
		BrassPlayer p;
		p.setup(design, tromboneTable(), habits);
		double at = 0.5;
		// push=SHARE release=S settle=S: the notes after, where tongued straight after another, started so
		// (BrassPlayer::Note::passagePush).
		double push = -1.0, release = 0.004, settle = 0.03, closeS = 0.004, slideS = 0.0;
		bool easedT = false;
		// formants=C2,S2,C3,S3,Q: the notes after with the second and third formants each on the harmonic of the
		// note nearest C hertz, of strength S MPa s/m^3, quality Q. out=PATH: written there instead.
		double centre2 = 0.0, strength2 = 0.0, centre3 = 0.0, strength3 = 0.0, quality = 6.0;
		// mouth=F1,S1,Q1,F2,S2,Q2,F3,S3,Q3: the notes after with the three formants where they are said to be,
		// not on the note's harmonics — a measured mouth (Boutin et al. 2013, 2015; Hanna et al. 2016).
		double mouthSet[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
		// tongue=RATIO,STRENGTH,Q: the notes after with the first formant at RATIO times the note's pitch — the
		// tongue placed just below the note — of STRENGTH MPa s/m^3 and quality Q.
		double tongueRatio = 0.0, tongueStrength = 0.0, tongueQ = 2.0;
		std::string outPath = "build/Phrase.wav";
		double traceFrom = 0.0, traceTo = 0.0, fixSlide = 0.0;
		bool fixed = false;
		for (int a = 5; a < argc; a++) {
			if (!std::strncmp(argv[a], "formants=", 9)) {
				std::sscanf(argv[a] + 9, "%lf,%lf,%lf,%lf,%lf", &centre2, &strength2, &centre3, &strength3, &quality);
				continue;
			}
			if (!std::strncmp(argv[a], "out=", 4)) { outPath = argv[a] + 4; continue; }
			if (!std::strncmp(argv[a], "trace=", 6)) { std::sscanf(argv[a] + 6, "%lf:%lf", &traceFrom, &traceTo); continue; }
			// fixslide=CENTS: the notes after with the slide there, not found for their lips (tuneFor).
			if (!std::strncmp(argv[a], "fixslide=", 9)) { fixSlide = std::atof(argv[a] + 9); fixed = true; continue; }
			if (!std::strncmp(argv[a], "tongue=", 7)) {
				std::sscanf(argv[a] + 7, "%lf,%lf,%lf", &tongueRatio, &tongueStrength, &tongueQ);
				continue;
			}
			if (!std::strncmp(argv[a], "mouth=", 6)) {
				std::sscanf(argv[a] + 6, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &mouthSet[0], &mouthSet[1], &mouthSet[2], &mouthSet[3],
					&mouthSet[4], &mouthSet[5], &mouthSet[6], &mouthSet[7], &mouthSet[8]);
				continue;
			}
			// chest=SCALE,S,Q: the chest's three resonances, 600, 1400 and 2200 Hz times SCALE, each of strength S
			// MPa s/m^3 and quality Q, from now on.
			if (!std::strncmp(argv[a], "chest=", 6)) {
				double scale = 1.0, strength = 0.0, q = 4.0;
				std::sscanf(argv[a] + 6, "%lf,%lf,%lf", &scale, &strength, &q);
				const double at[3] = {600.0, 1400.0, 2200.0};
				for (int k = 0; k < 3; k++)
					p.instrument().setFormant(3 + k, scale * at[k], 1e6 * strength, q);
				continue;
			}
			if (!std::strncmp(argv[a], "push=", 5)) { push = std::atof(argv[a] + 5); continue; }
			if (!std::strncmp(argv[a], "release=", 8)) { release = std::atof(argv[a] + 8); continue; }
			if (!std::strncmp(argv[a], "settle=", 7)) { settle = std::atof(argv[a] + 7); continue; }
			// close=S eased=1: the tongue shutting over S seconds, both its moves eased (Note::passageClose).
			if (!std::strncmp(argv[a], "close=", 6)) { closeS = std::atof(argv[a] + 6); continue; }
			if (!std::strncmp(argv[a], "eased=", 6)) { easedT = std::atoi(argv[a] + 6) != 0; continue; }
			if (!std::strncmp(argv[a], "slide=", 6)) { slideS = std::atof(argv[a] + 6); continue; }
			// legato=OPEN: the legato tongue closing only to this share open, a lighter tongue.
			if (!std::strncmp(argv[a], "legato=", 7)) {
				habits.legatoLeast = habits.legatoMost = std::atof(argv[a] + 7);
				p.setup(design, tromboneTable(), habits);
				continue;
			}
			const int midi = std::atoi(argv[a]);
			const char* colon = std::strchr(argv[a], ':');
			const double beats = colon ? std::atof(colon + 1) : 1.0;
			const bool slurred = std::strstr(argv[a], ":s") != nullptr;
			if (midi > 0) {
				const auto found = settings.find(std::make_pair(midi, dyn));
				if (found == settings.end()) {
					std::printf("no settings for %d %s\n", midi, dyn.c_str());
					return 1;
				}
				const std::vector<double>& v = found->second;
				BrassPlayer::Note n;
				n.hz = hzOf(midi);
				n.level = level;
				n.seconds = beats * beat;
				n.slurred = slurred;
				n.tunedLips = v[0];
				n.hasGap = true;
				n.gap = 1e-3 * v[1];
				n.quality = 1.0 / v[2];
				n.mass = v[3];
				n.tunedMouth = v[4];
				n.rise = v[5];
				n.startFloor = v[6];
				n.riseCurve = v[7];
				// No breath noise: the recordings have none to hear, and the settings were found without it.
				n.air = 0.0;
				n.passagePush = push;
				n.passageRelease = release;
				n.passageSettle = settle;
				n.passageClose = closeS;
				n.passageEased = easedT;
				n.passageSlide = slideS;
				for (int k = 0; k < 3; k++)
					if (mouthSet[3 * k + 1] > 0.0) {
						n.formantHz[k] = mouthSet[3 * k];
						n.formantStrength[k] = 1e6 * mouthSet[3 * k + 1];
						n.formantQuality[k] = mouthSet[3 * k + 2];
					}
				if (tongueRatio > 0.0 && tongueStrength > 0.0) {
					n.formantHz[0] = tongueRatio * n.hz;
					n.formantStrength[0] = 1e6 * tongueStrength;
					n.formantQuality[0] = tongueQ;
				}
				// The formants on the note's harmonics nearest their centres.
				const double centres[2] = {centre2, centre3}, strengths[2] = {strength2, strength3};
				for (int k = 0; k < 2; k++)
					if (strengths[k] > 0.0 && centres[k] > 0.0) {
						n.formantHz[k + 1] = n.hz * std::max(1.0, std::round(centres[k] / n.hz));
						n.formantStrength[k + 1] = 1e6 * strengths[k];
						n.formantQuality[k + 1] = quality;
					}
				const BrassPlayer::Tuned t = fixed ? BrassPlayer::Tuned() : BrassPlayer::tuneFor(design, tromboneTable(), habits, n, &crew);
				n.slideCents = fixed ? fixSlide : t.slideCents;
				std::printf("  %s for %.2f s%s: the slide %+.0f cents\n", nameOf(midi).c_str(), n.seconds, slurred ? ", slurred on" : "", t.slideCents);
				p.add(n, (int64_t) (at * RATE), false);
			}
			at += beats * beat;
		}
		std::vector<double> out((size_t) ((at + 1.0) * RATE));
		// trace=FROM:TO: each sample from FROM to TO seconds, what the instrument is doing, to build/trace.tsv.
		FILE* trace = nullptr;
		if (traceTo > traceFrom) {
			trace = std::fopen("build/trace.tsv", "w");
			std::fprintf(trace, "t\ttongue\tbreath\tmouth at lips\tlip opening mm2\tcup\tout\ty um\tx um\tflow cm3s\tin opening\tbell in\n");
		}
		for (size_t i = 0; i < out.size(); i++) {
			out[i] = p.process();
			const double t = (double) i / RATE;
			if (trace && t >= traceFrom && t <= traceTo) {
				BrassInstrument& in = p.instrument();
				const BrassHingedLips& lp = in.lipsNow();
				std::fprintf(trace, "%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n", t, in.tongueNow(), in.breathNow(), in.mouthAtLips(),
					1e6 * in.lipOpening(), in.cupPressure(), out[i], 1e6 * lp.tipY(), 1e6 * lp.tipX(), 1e6 * lp.flowThrough(), lp.openingPressure(), in.bellInNow());
			}
		}
		if (trace)
			std::fclose(trace);
		// HOW EACH NOTE WAS JOINED to the one before, and where.
		static const char* const JOINS[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
		// And each start as the start judge hears it (BrassStart): its blip and blat, and how late after the
		// tongue it is heard.
		for (const BrassPlayer::Decision& d : p.decisions()) {
			const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) std::max<int64_t>(0, d.start - (int64_t) (0.01 * RATE)), RATE, d.sounds);
			std::printf("  %s: %s, resonance %d, the slide %.0f mm out; blip %.1f, blat %.1f dB, heard %.0f ms after\n",
				nameOf((int) std::lround(69.0 + 12.0 * std::log2(d.note.hz / 440.0))).c_str(), JOINS[d.join], d.resonance, 1000.0 * d.extension, shape.blip,
				shape.blat, 1000.0 * ((double) shape.onset - (double) d.start) / RATE);
		}
		writeWav(outPath, out);
		return 0;
	}
	if (argc > 4 && std::strcmp(argv[1], "tract") == 0) {
		// THE VOCAL TRACT MOVING UNDER ONE HELD NOTE: tract SETTINGS.tsv MIDI DYN [strength=SHARE] [q=Q]
		// [f1=LO:HI] [f2=HZ] [f3=HZ] [off=S] [seconds=S] [waverbreath=SHARE] [waverlips=SHARE] — the note as
		// the settings' line for it has it, its first `off` seconds with no vocal tract; then the three
		// formants, each `strength` times the bore's impedance at its peak by the note, of quality `q`: the
		// first at HI for a second, gliding down to LO over three, held a second, back up over three and held a
		// second; the second and third where `f2` and `f3` say. With f1=HZ alone, the first held there, and
		// the note `seconds` long after `off`. s1= s2= s3=SHARE each formant's own strength. swing=SHARE [swingk=K]
		// [hold=S]: formant K (1 to 3, the second unless said) held where it is set for `hold` seconds, then
		// up by that share over two, held one, down to that share under over four, held one, and back over two,
		// held one. off=0: the formants in place at full strength before the note starts, for its attack.
		// vib=SHARE [vibhz=HZ]: formant K swung that share above and below, `vibhz` times a second, from 0.4 s
		// into the note, coming in over 0.3 s — a vibrato made with the tongue; vibside=1, only to one side,
		// from where it is set to that share over and back, once a swing, under where the share is less than
		// nought. Written to build/Tract MIDI DYN
		// ....wav, from 0.2 s before the note.
		std::vector<double> v;
		{
			FILE* f = std::fopen(argv[2], "r");
			char line[2048];
			while (f && std::fgets(line, sizeof line, f)) {
				if (std::atoi(line) != std::atoi(argv[3]))
					continue;
				const char* tab = std::strchr(line, '\t');
				if (!tab || std::strncmp(tab + 1, argv[4], std::strlen(argv[4])) || tab[1 + std::strlen(argv[4])] != '\t')
					continue;
				const char* at = tab + 1 + std::strlen(argv[4]);
				for (int i = 0; i < 8 && at; i++) {
					v.push_back(std::atof(at + 1));
					at = std::strchr(at + 1, '\t');
				}
			}
			if (f)
				std::fclose(f);
		}
		if (v.size() < 8) {
			std::printf("no settings for %s %s in %s\n", argv[3], argv[4], argv[2]);
			return 1;
		}
		double share = 0.3, q = 6.0, lo = 150.0, hi = 800.0, f2 = 1200.0, f3 = 2500.0, off = 2.0, sweep = 9.0;
		double waverBreath = 0.0, waverLips = 0.0;
		double shares[3] = {-1.0, -1.0, -1.0}, swing = 0.0, hold = 3.0, vib = 0.0, vibHz = 5.5;
		bool vibSide = false;
		int swingK = 2;
		std::string tag;
		for (int a = 5; a < argc; a++) {
			const char* eq = std::strchr(argv[a], '=');
			if (!eq)
				continue;
			const std::string key(argv[a], (size_t) (eq - argv[a]));
			if (key == "strength") share = std::atof(eq + 1);
			else if (key == "q") q = std::atof(eq + 1);
			else if (key == "f1") {
				lo = std::atof(eq + 1);
				hi = std::strchr(eq, ':') ? std::atof(std::strchr(eq, ':') + 1) : lo;
			}
			else if (key == "f2") f2 = std::atof(eq + 1);
			else if (key == "f3") f3 = std::atof(eq + 1);
			else if (key == "off") off = std::atof(eq + 1);
			else if (key == "seconds") sweep = std::atof(eq + 1);
			else if (key == "waverbreath") waverBreath = std::atof(eq + 1);
			else if (key == "waverlips") waverLips = std::atof(eq + 1);
			else if (key == "s1") shares[0] = std::atof(eq + 1);
			else if (key == "s2") shares[1] = std::atof(eq + 1);
			else if (key == "s3") shares[2] = std::atof(eq + 1);
			else if (key == "swing") swing = std::atof(eq + 1);
			else if (key == "swingk") swingK = std::atoi(eq + 1);
			else if (key == "hold") hold = std::atof(eq + 1);
			else if (key == "vib") vib = std::atof(eq + 1);
			else if (key == "vibhz") vibHz = std::atof(eq + 1);
			else if (key == "vibside") vibSide = std::atoi(eq + 1) != 0;
			tag += " " + std::string(argv[a]);
		}
		const int midi = std::atoi(argv[3]);
		const std::string dyn = argv[4];
		BrassInstrument::Design design;
		design.rate = RATE;
		BrassPlayer::Note n;
		n.hz = hzOf(midi);
		n.level = dyn == "pp" ? 1.0 / 7.0 : dyn == "ff" ? 6.0 / 7.0 : 4.0 / 7.0;
		n.tunedLips = v[0];
		n.hasGap = true;
		n.gap = 1e-3 * v[1];
		n.quality = 1.0 / v[2];
		n.mass = v[3];
		n.tunedMouth = v[4];
		n.rise = v[5];
		n.startFloor = v[6];
		n.riseCurve = v[7];
		n.air = 0.0;
		n.waverBreath = waverBreath;
		n.waverLips = waverLips;
		// Held its whole length, as written.
		n.tenuto = true;
		if (swing > 0.0)
			sweep = hold + 11.0;
		n.seconds = off + sweep + 0.2;
		static Crew crew(crewSize(3));
		n.slideCents = BrassPlayer::tuneFor(design, tromboneTable(), habits, n, &crew).slideCents;
		BrassPlayer p;
		p.setup(design, tromboneTable(), habits);
		const double start = 0.3;
		p.add(n, (int64_t) (start * RATE), false);
		double placed = -1.0;
		{
			// Where the note's slide is, from a short run to just after it starts.
			BrassPlayer dry;
			dry.setup(design, tromboneTable(), habits);
			dry.add(n, (int64_t) (start * RATE), false);
			for (int i = 0; i < (int) ((start + 0.05) * RATE); i++)
				dry.process();
			placed = dry.instrument().slide();
		}
		std::vector<double> out((size_t) ((start + n.seconds + 0.4) * RATE));
		double strength = 0.0, peakZ = 0.0, extension = -1.0;
		for (size_t i = 0; i < out.size(); i++) {
			const double t = (double) i / RATE - start;
			if (extension < 0.0) {
				// THE BORE'S IMPEDANCE AT ITS PEAK BY THE NOTE, the slide where the note has it: a puff of flow into
				// the cup, the pressure it sends back, and its spectrum against the puff's.
				extension = placed;
				BrassWaveguide::Setup ws;
				ws.air = design.air;
				ws.rate = design.rate * std::max(1, design.oversampling);
				ws.tuningPull = design.tuningPull;
				ws.steepening = false;
				ws.extension = extension;
				BrassWaveguide wg;
				wg.setup(ws);
				const double puff = 1e-6;
				std::vector<double> back((size_t) (1.5 * ws.rate));
				for (size_t j = 0; j < back.size(); j++)
					back[j] = wg.step(j == 0 ? puff : 0.0);
				double peak = 0.0, at = 0.0;
				for (double hz = 0.85 * n.hz; hz <= 1.15 * n.hz; hz += 0.25) {
					double re = 0.0, im = 0.0;
					const double w = 2.0 * M_PI * hz / ws.rate;
					for (size_t j = 0; j < back.size(); j++) {
						re += back[j] * std::cos(w * (double) j);
						im -= back[j] * std::sin(w * (double) j);
					}
					const double z = std::sqrt(re * re + im * im) / puff;
					if (z > peak) {
						peak = z;
						at = hz;
					}
				}
				strength = share * peak;
				peakZ = peak;
				std::printf("  the bore's peak by %s: %.1f MPa s/m^3 at %.1f Hz; the formants %.2f MPa s/m^3 each, quality %.1f\n",
					nameOf(midi).c_str(), 1e-6 * peak, at, 1e-6 * strength, q);
			}
			// THE FORMANTS: none until `off`, then in over 0.3 s, the first moving.
			double s1 = 0.0, f1 = hi;
			if ((t >= off || off <= 0.0) && strength > 0.0) {
				const double u = t - off;
				s1 = off <= 0.0 ? strength : strength * std::fmin(1.0, u / 0.3);
				if (lo == hi) f1 = hi;
				else if (u < 1.0) f1 = hi;
				else if (u < 4.0) f1 = hi * std::pow(lo / hi, (u - 1.0) / 3.0);
				else if (u < 5.0) f1 = lo;
				else if (u < 8.0) f1 = lo * std::pow(hi / lo, (u - 5.0) / 3.0);
				else f1 = hi;
			}
			// Each formant's own strength where given, and the one that swings.
			double hzs[3] = {f1, f2, f3}, ss[3] = {s1, s1, s1};
			for (int k = 0; k < 3; k++)
				if (shares[k] >= 0.0)
					ss[k] = strength > 0.0 ? s1 / strength * shares[k] * peakZ : 0.0;
			if (swing > 0.0 && t >= off) {
				const double u = t - off - hold, up = 1.0 + swing, down = 1.0 - swing;
				double factor = 1.0;
				if (u < 0.0) factor = 1.0;
				else if (u < 2.0) factor = std::pow(up, u / 2.0);
				else if (u < 3.0) factor = up;
				else if (u < 7.0) factor = up * std::pow(down / up, (u - 3.0) / 4.0);
				else if (u < 8.0) factor = down;
				else if (u < 10.0) factor = down * std::pow(1.0 / down, (u - 8.0) / 2.0);
				hzs[swingK - 1] *= factor;
			}
			if (vib != 0.0 && t > 0.4) {
				const double phase = 2.0 * M_PI * vibHz * (t - 0.4), depth = vib * std::fmin(1.0, (t - 0.4) / 0.3);
				hzs[swingK - 1] *= 1.0 + depth * (vibSide ? 0.5 - 0.5 * std::cos(phase) : std::sin(phase));
			}
			for (int k = 0; k < 3; k++)
				p.instrument().setFormant(k, hzs[k], ss[k], q);
			out[i] = p.process();
		}
		out.erase(out.begin(), out.begin() + (size_t) (0.1 * RATE));
		std::printf("  the first formant: %.0f Hz from %.1f s, down to %.0f Hz by %.1f s, held, back up to %.0f Hz by %.1f s; the note at %.1f Hz\n",
			hi, off + 1.0 + 0.2, lo, off + 4.0 + 0.2, hi, off + 8.0 + 0.2, n.hz);
		writeWav("build/Tract " + std::to_string(midi) + " " + dyn + tag + ".wav", out);
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "fubini") == 0) {
		// THE STEEPENING AGAINST FUBINI'S SOLUTION: a 200 Hz tone through one BrassSteepeningDelay of 9.4 ms,
		// its amplitude set for each sigma, the distance as a share of the shock's; harmonic n's amplitude
		// against the tone's should be 2 J_n(n sigma) / (n sigma) before the shock.
		const BrassAir air;
		const double steep = 0.5 * (air.gamma + 1.0) / (air.rho * air.c * air.c), f = 200.0, w = 2.0 * M_PI * f, delay = 450.0;
		const double travel = delay / RATE;
		std::printf("  sigma   harmonic: model, Fubini\n");
		for (double sigma : {0.3, 0.6, 0.9}) {
			const double amplitude = sigma / (travel * steep * w);
			BrassSteepeningDelay sd;
			sd.setup(600, steep, RATE);
			const size_t period = (size_t) (RATE / f), skip = 2000, count = 100 * period;
			std::vector<double> y(skip + count);
			for (size_t i = 0; i < y.size(); i++) {
				y[i] = sd.read();
				sd.write(amplitude * std::sin(w * (double) i / RATE), delay);
			}
			std::printf("  %.1f  (%.0f Pa)", sigma, amplitude);
			for (int n = 1; n <= 6; n++) {
				double re = 0.0, im = 0.0;
				for (size_t i = 0; i < count; i++) {
					re += y[skip + i] * std::cos(n * w * (double) i / RATE);
					im += y[skip + i] * std::sin(n * w * (double) i / RATE);
				}
				const double model = 2.0 * std::sqrt(re * re + im * im) / (double) count / amplitude;
				// Bessel's J_n by its series.
				auto jn = [](int order, double x) {
					double sum = 0.0, term = std::pow(0.5 * x, order) / std::tgamma(order + 1.0);
					for (int m = 0; m < 40; m++) {
						sum += term;
						term *= -(0.25 * x * x) / ((m + 1.0) * (m + 1.0 + order));
					}
					return sum;
				};
				const double fubini = 2.0 * jn(n, n * sigma) / (n * sigma);
				std::printf("   %d: %.4f %.4f", n, model, fubini);
			}
			std::printf("\n");
		}
		return 0;
	}
	if (argc > 3 && std::strcmp(argv[1], "envelope") == 0) {
		// ONE NOTE AS mpxTrombone's TUNING PLAYS IT: envelope MIDI DYN [KEY=VALUE ...], the keys its knobs —
		// rise seconds, lips per cent sharp, breath dB, tension, damp, mass, gap the lips' at rest in mm, floor and
		// curve the breath's start and rise (Note::startFloor, riseCurve), waverbreath and waverlips — and
		// seconds, its length, 0.75 as
		// the tuning scores have it; mouth and slide, the breath and slide found for the lips, else found now
		// (tuneFor). Written to build/Envelope MIDI DYN.wav from 0.2 s before it.
		BrassPlayer::Note n;
		n.hz = hzOf(std::atoi(argv[2]));
		{
			// The marking, ppp to fff, on the cable's ladder.
			static const char* const MARKS[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
			n.level = 4.0 / 7.0;
			for (int k = 0; k < 8; k++)
				if (!std::strcmp(argv[3], MARKS[k]))
					n.level = k / 7.0;
		}
		n.seconds = 0.75;
		n.rise = 0.14;
		n.lips = 1.0;
		double damp = 1.0 / 3.0, mouth = -1.0, slide = 0.0, gapMm = 0.0;
		bool slideGiven = false;
		bool steep = true;
		n.mass = 0.9;
		n.tunedLips = 0.95;
		std::string tag;
		for (int a = 4; a < argc; a++) {
			const char* eq = std::strchr(argv[a], '=');
			if (!eq)
				continue;
			const std::string key(argv[a], (size_t) (eq - argv[a]));
			const double v = std::atof(eq + 1);
			if (key == "rise") n.rise = v;
			else if (key == "lips") n.lips = 1.0 + v / 100.0;
			else if (key == "breath") n.breath = std::pow(10.0, v / 20.0);
			else if (key == "tension") n.tunedLips = v;
			else if (key == "damp") damp = v;
			else if (key == "mass") n.mass = v;
			else if (key == "seconds") n.seconds = v;
			else if (key == "mouth") mouth = v;
			else if (key == "slide") {
				slide = v;
				slideGiven = true;
			}
			else if (key == "gap") gapMm = v;
			else if (key == "steep") steep = v != 0.0;
			else if (key == "floor") n.startFloor = v;
			else if (key == "curve") n.riseCurve = v;
			else if (key == "waverbreath") n.waverBreath = v;
			else if (key == "waverlips") n.waverLips = v;
			tag += " " + std::string(argv[a]);
		}
		n.quality = 1.0 / damp;
		BrassInstrument::Design design;
		design.rate = RATE;
		design.steepening = steep;
		n.hasGap = true;
		n.gap = gapMm * 1e-3;
		if (mouth <= 0.0 || !slideGiven) {
			// The breath and the slide found, or, the mouth given, the slide for it (tuneFor).
			static Crew crew(crewSize(3));
			n.tunedMouth = mouth;
			const BrassPlayer::Tuned t = BrassPlayer::tuneFor(design, tromboneTable(), habits, n, &crew);
			mouth = t.mouth;
			slide = t.slideCents;
			std::printf("  found: mouth %.0f Pa, the slide %+.0f cents\n", mouth, slide);
		}
		n.tunedMouth = mouth;
		n.slideCents = slide;
		BrassPlayer p;
		p.setup(design, tromboneTable(), habits);
		p.add(n, (int64_t) (0.5 * RATE), false);
		std::vector<double> out((size_t) ((0.5 + n.seconds + 0.3) * RATE)), cup(out.size());
		for (size_t i = 0; i < out.size(); i++) {
			out[i] = p.process();
			cup[i] = p.instrument().cupPressure();
		}
		out.erase(out.begin(), out.begin() + (size_t) (0.3 * RATE));
		cup.erase(cup.begin(), cup.begin() + (size_t) (0.3 * RATE));
		writeWav("build/Envelope " + std::string(argv[2]) + " " + argv[3] + tag + ".wav", out);
		{
			// The cup's alternating pressure over the note's steady part, 0.35 to 0.7 s after it starts.
			const size_t a = (size_t) (0.55 * RATE), b = (size_t) (0.9 * RATE);
			double mean = 0.0, peak = 0.0, sq = 0.0;
			for (size_t i = a; i < b; i++)
				mean += cup[i];
			mean /= (double) (b - a);
			for (size_t i = a; i < b; i++) {
				sq += (cup[i] - mean) * (cup[i] - mean);
				peak = std::max(peak, std::fabs(cup[i] - mean));
			}
			std::printf("  in the cup: mean %.0f Pa, alternating %.0f Pa rms, %.0f Pa at its peak\n", mean, std::sqrt(sq / (double) (b - a)), peak);
		}
		// And the pressure in the mouthpiece's cup, for what the bell and the listener do to it.
		writeWav("build/Envelope " + std::string(argv[2]) + " " + argv[3] + tag + ", in the cup.wav", cup);
		return 0;
	}
	if (argc > 4 && std::strcmp(argv[1], "byear") == 0) {
		startsByEar(std::atoi(argv[2]), argv[3], argv[4]);
		return 0;
	}
	if (argc > 4 && std::strcmp(argv[1], "audition") == 0) {
		audition(std::atoi(argv[2]), argv[3], argv[4]);
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "peaks") == 0) {
		peaks();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "starts") == 0) {
		starts();
		return 0;
	}
	if (argc > 3 && std::strcmp(argv[1], "attack") == 0) {
		attack(std::atoi(argv[2]), std::atof(argv[3]));
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "slurs") == 0) {
		judgeAll(true);
		return 0;
	}
	if (argc > 3 && std::strcmp(argv[1], "join") == 0) {
		join(std::atoi(argv[2]), std::atoi(argv[3]));
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "sweep") == 0) {
		sweep();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "legatosweep") == 0) {
		sweepLegato();
		return 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "lipsweep") == 0) {
		sweepLip();
		return 0;
	}
	// A B FLAT MAJOR SCALE over two octaves, up and down, quarter notes at 100 a minute.
	const int degrees[8] = {0, 2, 4, 5, 7, 9, 11, 12};
	std::vector<int> scale;
	for (int o = 0; o < 2; o++)
		for (int i = 0; i < 7; i++)
			scale.push_back(46 + 12 * o + degrees[i]);
	scale.push_back(70);
	for (int i = (int) scale.size() - 2; i >= 0; i--)
		scale.push_back(scale[(size_t) i]);
	const double beat = 0.6;
	for (int slurred = 0; slurred < 2; slurred++) {
		std::vector<Asked> part;
		for (size_t i = 0; i < scale.size(); i++) {
			Asked a;
			a.midi = scale[i];
			a.note.hz = hzOf(scale[i]);
			a.note.level = 4.0 / 7.0;
			a.note.seconds = beat;
			a.note.slurred = slurred == 1 && i + 1 < scale.size();
			a.at = 0.4 + beat * (double) i;
			part.push_back(a);
		}
		play(slurred ? "A B FLAT MAJOR SCALE, SLURRED, mf" : "A B FLAT MAJOR SCALE, TONGUED, mf", part,
			slurred ? (rehearse ? "build/Trombone scale, slurred, rehearsed.wav" : "build/Trombone scale, slurred.wav") : "build/Trombone scale, tongued.wav");
	}
	// ONE NOTE FROM ppp TO fff.
	{
		std::vector<Asked> part;
		for (int d = 0; d <= 7; d++) {
			Asked a;
			a.midi = 58;
			a.note.hz = hzOf(58);
			a.note.level = d / 7.0;
			a.note.seconds = 1.0;
			a.at = 0.4 + 1.4 * d;
			part.push_back(a);
		}
		play("B FLAT 3 FROM ppp TO fff", part, "build/Trombone B flat 3, ppp to fff.wav");
	}
	// A GLISSANDO from B flat 2 down to E2 and back, and from F3 up to B flat 3 by a natural slur.
	{
		std::vector<Asked> part;
		const int notes[5] = {46, 40, 46, 53, 58};
		for (int i = 0; i < 5; i++) {
			Asked a;
			a.midi = notes[i];
			a.note.hz = hzOf(notes[i]);
			a.note.level = 5.0 / 7.0;
			a.note.seconds = 1.0;
			a.note.glissando = i < 2;
			a.note.slurred = i >= 2 && i < 4;
			a.at = 0.4 + 1.0 * i;
			part.push_back(a);
		}
		play("GLISSANDI AND SLURS", part, "build/Trombone glissando.wav");
	}
	return 0;
}
