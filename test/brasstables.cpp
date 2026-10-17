/** THE TROMBONE'S TABLES, found on the model: phase 6 of docs/brass.md (src/BrassTable.hpp).

    make brasstables                   finds them and writes src/TromboneTable.cpp
    make brasstables ARGS="resonance=2"   finds only the second resonance's, at every position, and keeps the
                                          rest from the tables as they are: a couple of minutes, not 25, for
                                          trying the lips on one note

THE TROMBONE TUNED TO PLAY: the tuning slide pulled 55 mm, which puts F3, B flat 3 and A flat 4 in first
position within 7 cents of A440 with the note 1.1 per cent over its resonance (B2); the notes that are then
sharp the player lowers with the slide. The second resonance lies about half a semitone under the series of
the others, as the Conn 8H's does (B1), so B flat 2 in first position is played at its own pitch, about 4.5
per cent over its resonance, where the higher resonances pull it: each resonance is aimed at whichever is
higher, 1.1 per cent over itself or its note in first position.

For each slide position and each resonance from the second up to 760 Hz: the mouth's pressure a quarter
octave at a time from 300 Pa to 9 kPa; at each, the lips tuned by halving to sound the note where its
resonance is aimed (test/brassbench.hpp), and the note kept if it sounds there regularly — its period
repeating with a correlation over 0.95 — and within 40 cents of that; its level and its centroid a metre away and 10
degrees off the bell's axis, worked out at 96 kHz as BrassInstrument works it out for 48. The threshold is then found between the last pressure that did not sound and
the first that did, by halving. Above the first pressure that stops sounding regularly, nothing is kept. */
#include "brassbench.hpp"
#include "../src/BrassTable.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

using namespace bench;

/** HOW REGULAR the cup's pressure is over its last 0.4 s: the highest correlation of it with itself moved by
0.8 to 1.25 of `period` samples. */
static double regularity(const std::vector<double>& cup, double period) {
	const size_t n = (size_t) (0.4 * RATE), from = cup.size() - n;
	double mean = 0.0;
	for (size_t i = from; i < cup.size(); i++)
		mean += cup[i];
	mean /= (double) n;
	double best = -1.0;
	for (int lag = (int) (0.8 * period); lag <= (int) (1.25 * period); lag++) {
		double s = 0.0, p = 0.0, q = 0.0;
		for (size_t i = from; i + (size_t) lag < cup.size(); i++) {
			const double a = cup[i] - mean, b = cup[i + (size_t) lag] - mean;
			s += a * b; p += a * a; q += b * b;
		}
		best = std::max(best, s / std::sqrt(p * q + 1e-30));
	}
	return best;
}

struct Found {
	int position = 1, resonance = 2;
	double resonanceHz = 0.0, over = 0.011, threshold = 0.0;
	std::vector<BrassTablePoint> points;
};

/** ONE PRESSURE: the point, or none where the note does not sound there as it should. */
static bool tryAt(double extension, double resonanceHz, double over, double mouth, BrassTablePoint* point) {
	const double lips = tunedLips(extension, resonanceHz, mouth, over);
	const Take t = play(extension, resonanceHz, lips * resonanceHz, mouth);
	if (!t.sounds || std::fabs(cents(t.f0, (1.0 + over) * resonanceHz)) > 40.0 || regularity(t.cup, RATE / t.f0) < 0.95)
		return false;
	*point = BrassTablePoint{(float) mouth, (float) lips, (float) t.f0, (float) t.level, (float) measure(t.heard, t.f0).centroidOverF0};
	return true;
}

static Found find(int position, int resonance, double resonanceHz, double over) {
	Found f;
	f.position = position;
	f.resonance = resonance;
	f.resonanceHz = resonanceHz;
	f.over = over;
	const double extension = POSITIONS[position - 1];
	double silent = 0.0;
	for (int k = 0; ; k++) {
		const double mouth = 300.0 * std::pow(2.0, k / 4.0);
		if (mouth > 9000.0)
			break;
		BrassTablePoint point;
		if (tryAt(extension, resonanceHz, over, mouth, &point)) {
			f.points.push_back(point);
			continue;
		}
		if (!f.points.empty())
			break;
		silent = mouth;
	}
	if (f.points.empty())
		return f;
	// THE THRESHOLD between the last pressure that did not sound and the first that did.
	double lo = silent > 0.0 ? silent : 0.5 * f.points[0].mouth, hi = f.points[0].mouth;
	for (int i = 0; i < 4; i++) {
		const double mouth = std::sqrt(lo * hi);
		BrassTablePoint point;
		if (tryAt(extension, resonanceHz, over, mouth, &point)) {
			hi = mouth;
			f.points.insert(f.points.begin(), point);
		}
		else
			lo = mouth;
	}
	f.threshold = hi;
	return f;
}

int main(int argc, char** argv) {
	// TWICE THE MODULE'S 48 kHz, as BrassInstrument works the trombone out (Design::oversampling).
	RATE = 96000.0;
	PULL = 0.055;
	// ONLY SOME RESONANCES, the rest kept from the tables as they are.
	std::vector<int> only;
	for (int i = 1; i < argc; i++)
		if (std::strncmp(argv[i], "resonance=", 10) == 0)
			only.push_back(std::atoi(argv[i] + 10));
	auto wanted = [&](int r) { return only.empty() || std::find(only.begin(), only.end(), r) != only.end(); };
	std::vector<Found> work;
	// WHERE EACH RESONANCE IS AIMED: its note in first position — the equal-tempered note nearest its
	// number times B flat 1 — or 1.1 per cent over itself, whichever is higher.
	const std::vector<double> firstPosition = resonances(0.0);
	std::vector<double> over(firstPosition.size(), 0.011);
	std::printf("WHERE EACH RESONANCE IS AIMED, over itself\n ");
	for (size_t r = 1; r < firstPosition.size() && firstPosition[r] < 760.0; r++) {
		const double semis = std::round(12.0 * std::log2((double) (r + 1) * 58.2705 / 440.0));
		const double note = 440.0 * std::pow(2.0, semis / 12.0);
		over[r] = std::max(0.011, note / firstPosition[r] - 1.0);
		std::printf(" %zu: %+.1f%%", r + 1, 100.0 * over[r]);
	}
	std::printf("\n\nTHE BORE'S RESONANCES at each position\n");
	for (int pos = 1; pos <= 7; pos++) {
		const std::vector<double> peaks = resonances(POSITIONS[pos - 1]);
		std::printf("  %d:", pos);
		for (size_t r = 1; r < peaks.size() && peaks[r] < 760.0; r++) {
			Found f;
			f.position = pos;
			f.resonance = (int) r + 1;
			f.resonanceHz = peaks[r];
			f.over = r < over.size() ? over[r] : 0.011;
			work.push_back(f);
			std::printf(" %.1f", peaks[r]);
		}
		std::printf("\n");
	}
	std::printf("\nFINDING %zu NOTES\n", work.size());
	std::atomic<size_t> next(0);
	std::mutex print;
	// What is kept: copied from the tables as they are.
	const px::BrassTable& before = px::tromboneTable();
	for (Found& f : work) {
		if (wanted(f.resonance))
			continue;
		for (int k = 0; k < before.noteCount; k++) {
			const px::BrassTableNote& n = before.notes[k];
			if (n.position != f.position || n.resonance != f.resonance)
				continue;
			f.threshold = n.threshold;
			f.points.assign(before.points + n.first, before.points + n.first + n.count);
		}
	}
	auto run = [&]() {
		for (size_t i; (i = next++) < work.size(); ) {
			if (!wanted(work[i].resonance))
				continue;
			work[i] = find(work[i].position, work[i].resonance, work[i].resonanceHz, work[i].over);
			const Found& f = work[i];
			std::lock_guard<std::mutex> lock(print);
			std::printf("  position %d, resonance %2d (%5.1f Hz): ", f.position, f.resonance, f.resonanceHz);
			if (f.points.empty())
				std::printf("does not sound\n");
			else
				std::printf("threshold %4.0f Pa, %2zu pressures to %4.0f Pa, lips %.3f to %.3f, %.1f to %.1f dB, centroid %.2f to %.2f\n", f.threshold,
					f.points.size(), f.points.back().mouth, f.points.front().lips, f.points.back().lips, f.points.front().level, f.points.back().level,
					f.points.front().centroid, f.points.back().centroid);
			std::fflush(stdout);
		}
	};
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency() - 1); t++)
		pool.emplace_back(run);
	for (auto& t : pool)
		t.join();

	FILE* out = std::fopen("src/TromboneTable.cpp", "w");
	if (!out) {
		std::printf("could not write src/TromboneTable.cpp\n");
		return 1;
	}
	std::fprintf(out, "/** THE TENOR TROMBONE'S TABLES, written by make brasstables (test/brasstables.cpp): not to be edited by\nhand. See BrassTable.hpp. */\n");
	std::fprintf(out, "#include \"BrassTable.hpp\"\n\nnamespace px {\n\n\n");
	std::fprintf(out, "static const BrassTablePoint POINTS[] = {\n");
	int at = 0;
	std::vector<int> first;
	for (const Found& f : work) {
		first.push_back(at);
		for (const BrassTablePoint& p : f.points) {
			std::fprintf(out, "\t{%.1ff, %.4ff, %.2ff, %.2ff, %.3ff},\n", p.mouth, p.lips, p.hz, p.level, p.centroid);
			at++;
		}
	}
	if (at == 0)
		std::fprintf(out, "\t{0.0f, 1.0f, 0.0f, 0.0f, 1.0f},\n");
	std::fprintf(out, "};\n\nstatic const BrassTableNote NOTES[] = {\n");
	int kept = 0;
	for (size_t i = 0; i < work.size(); i++) {
		const Found& f = work[i];
		if (f.points.empty())
			continue;
		std::fprintf(out, "\t{%d, %d, %.2ff, %.1ff, %d, %d},\n", f.position, f.resonance, f.resonanceHz, f.threshold, first[i], (int) f.points.size());
		kept++;
	}
	std::fprintf(out, "};\n\nstatic const float POSITIONS[7] = {");
	for (int p = 0; p < 7; p++)
		std::fprintf(out, "%s%.3ff", p ? ", " : "", POSITIONS[p]);
	std::fprintf(out, "};\n\n\nconst BrassTable& tromboneTable() {\n\tstatic const BrassTable table{NOTES, %d, POINTS, POSITIONS, %.1f};\n\treturn table;\n}\n\n\n} // namespace px\n",
		kept, RATE);
	std::fclose(out);
	std::printf("\nwrote src/TromboneTable.cpp: %d notes, %d pressures\n", kept, at);
	return 0;
}
