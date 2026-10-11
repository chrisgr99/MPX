/** GENERATED PEDALLING AGAINST REAL PEDAL MARKS.

Every stretch a score pedals for itself is played again as though it were unmarked, and the lifts
gpPedalPassage makes there are set beside the ones the writer marked: how many of the real lifts it
found, and how many of its own are real. A missed lift is the bad error, since it holds the pedal
through a change of harmony; an extra one only sounds a little drier. Beside it, the plain rules
of changing on every beat, every half bar, every bar and every two bars, measured the same way.

The scores are split in two. The rule is tuned on the first half and the second half is never
looked at while tuning, so its numbers say whether the tuning learned pedalling or learned these
pieces.

    make pedaltest                                  the scores in ~/Downloads
    make pedaltest ARGS="halfNew=0.04 halfHeld=1.5"      with the rule changed
    make pedaltest ARGS="show=a-score.mxl 20 30"    every lift in bars 20 to 30, real and generated
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"
#include "../src/Pedalling.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace px;

/** TUNED ON, then HELD OUT. Solo piano only: in a quartet the other instruments' notes are no
part of what the pianist's foot listens to. */
static const char* TUNING[] = {
	"chopin-ballade-no-1-in-g-minor-op-23.mxl",
	"f-chopin-nocturne-op9-no2-in-e-flat-major.mxl",
	"chopin-nocturne-no-20-in-c-sharp-minor-op-posth.mxl",
	"nocturne-in-f-minor-op-55-no-1.mxl",
	"fantaisie-impromptu-in-c-minor-chopin.mxl",
	"sonate-no-17-tempest-3rd-movement.mxl",
	"fur-elise-beethoven.mxl",
	"romance-op24-no9-j-sibelius.mxl",
	"waltz-opus-64-no-2-in-c-minor.mxl",
	"a-la-maniere-de-borodine-mravel-m63.mxl",
};
static const char* HELD_OUT[] = {
	"nocturne-opus-72-no-1-in-e-minor.mxl",
	"scherzo-no-2-opus-31-in-b-minor.mxl",
	"waltz-opus-69-no-2-in-b-minor.mxl",
	"waltz-opus-69-no-1-in-a-major.mxl",
	"sonate-no-21-waldstein-3rd-movement.mxl",
	"sonate-no-14-moonlight-3rd-movement.mxl",
	"the-spruce-op-75-no-5-jean-sibelius.mxl",
	"ravel-valses-nobles-et-sentimentales-i-modere-tres-franc.mxl",
	"polonaise-opus-40-no-1-in-a-major-military-polonaise.mxl",
	"etude-op10-no3-e-major-tristesse-chopin.mxl",
	"misty-piano-solo-annotated.mxl",
	"finlandia-jean-sibelius-arr-dillon-schel.mxl",
};

/** The real lifts the generated pedalling missed, by place: first beat, half bar, other beat, off the beat. */
static int missedAt[4] = {0, 0, 0, 0};

/** HOW CLOSE A LIFT MUST BE TO A REAL ONE TO BE IT, in quarters. */
static double tolerance = 0.3;

/** Lifts found against lifts made, added up. */
struct Score {
	int real = 0, found = 0, made = 0, right = 0;
	void add(const Score& o) {
		real += o.real;
		found += o.found;
		made += o.made;
		right += o.right;
	}
	double recall() const { return real ? (double) found / real : 0.0; }
	double precision() const { return made ? (double) right / made : 0.0; }
};

/** HOW MANY OF `real` HAVE ONE OF `made` NEAR THEM, and the other way round. */
static Score compare(const std::vector<double>& real, const std::vector<double>& made) {
	Score s;
	s.real = (int) real.size();
	s.made = (int) made.size();
	for (double r : real) {
		for (double m : made)
			if (std::fabs(m - r) <= tolerance) {
				s.found++;
				break;
			}
	}
	for (double m : made) {
		for (double r : real)
			if (std::fabs(m - r) <= tolerance) {
				s.right++;
				break;
			}
	}
	return s;
}

/** WHERE THE FOOT CLEARS THE SOUND inside a stretch: a change, or the pedal coming up. A lift that
is followed within a beat and a half by the pedal going down again is one clearing, counted where it
goes down — the Moonlight's finale lifts on the last eighth of a bar and pedals again on the next
first beat, and that is one change of pedal for the new bar, not two. A mark repeated on a second
staff moves nothing and is not counted. */
static std::vector<double> liftsOf(const std::vector<GpPlayedPedal>& moves, double from, double to) {
	std::vector<double> out;
	bool down = false;
	double upAt = -1.0;
	for (const GpPlayedPedal& p : moves) {
		if (p.sostenuto || p.startQuarters < from - 1e-9 || p.startQuarters >= to - 1e-9)
			continue;
		if (p.kind == GpPedalMark::UP) {
			if (down) {
				down = false;
				upAt = p.startQuarters;
			}
			continue;
		}
		if (p.kind == GpPedalMark::CHANGE && down) {
			out.push_back(p.startQuarters);
			continue;
		}
		if (!down && upAt >= 0.0) {
			out.push_back(p.startQuarters - upAt <= 1.5 ? p.startQuarters : upAt);
			upAt = -1.0;
		}
		down = true;
	}
	std::vector<double> inside;
	for (double q : out)
		if (q > from + 1e-9 && q < to - 1e-9)
			inside.push_back(q);
	return inside;
}

static const int RULES = 5;
static const char* RULE_NAMES[RULES] = {"generated", "every beat", "every half bar", "every bar",
	"every two bars"};

/** THE STRETCHES A SCORE PEDALS FOR ITSELF: from the pedal going down to its coming up with no
mark for `unmarkedBars` bars after. A shorter gap is a lift inside the stretch. */
static std::vector<std::pair<double, double> > stretches(const GpTimeline& line, double bars) {
	std::vector<std::pair<double, double> > out;
	bool down = false;
	double start = 0.0, upAt = -1.0;
	auto barLength = [&](double at) {
		double length = 4.0;
		for (const GpPlayedBar& b : line.bars)
			if (b.startQuarters <= at + 1e-9)
				length = b.quarters;
		return length;
	};
	for (const GpPlayedPedal& p : line.pedals) {
		if (p.sostenuto)
			continue;
		if (p.kind == GpPedalMark::UP) {
			if (down) {
				down = false;
				upAt = p.startQuarters;
			}
			continue;
		}
		if (down)
			continue;
		if (upAt >= 0.0 && p.startQuarters - upAt < bars * barLength(upAt) - 1e-6) {
			down = true;      // A lift on purpose, inside the stretch.
			continue;
		}
		if (upAt >= 0.0)
			out.push_back(std::make_pair(start, upAt));
		start = p.startQuarters;
		down = true;
	}
	if (upAt >= 0.0 && upAt > start && !down)
		out.push_back(std::make_pair(start, upAt));
	return out;
}

/** The beats of the bars between two places, each with its index in the bar and the bar's count. */
struct BeatAt {
	double at;
	int index, count;
};
static std::vector<BeatAt> beatsBetween(const GpSong& song, const GpTimeline& line, double from, double to) {
	std::vector<BeatAt> out;
	for (size_t i = 0; i < line.bars.size(); i++) {
		const GpPlayedBar& bar = line.bars[i];
		const size_t w = (size_t) bar.written;
		double length = 1.0;
		if (w < song.masterBars.size()) {
			const GpMasterBar& m = song.masterBars[w];
			length = 4.0 / std::max(1, m.beatUnit);
			if (m.beatUnit >= 8 && m.beatsPerBar % 3 == 0 && m.beatsPerBar > 3)
				length *= 3.0;
		}
		const int count = std::max(1, (int) std::lround(bar.quarters / length));
		for (int k = 0; k < count; k++) {
			const double at = bar.startQuarters + k * length;
			if (at > from + 1e-9 && at < to - 1e-9)
				out.push_back({at, k, count});
		}
	}
	return out;
}

/** ONE SCORE: the five rules against its marks. `show` prints every lift in a range of bars. */
static bool measure(const std::string& path, const PedalRule& rule, Score* scores, bool quiet,
                    int showFrom = -1, int showTo = -1) {
	GpSong song;
	std::string why;
	if (!mxReadFile(path, song, &why)) {
		std::printf("  could not read %s: %s\n", path.c_str(), why.c_str());
		return false;
	}
	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) {
		std::printf("  nothing to play in %s\n", path.c_str());
		return false;
	}
	Score each[RULES];
	double dryReal = 0.0, dryOurs = 0.0;
	for (const std::pair<double, double>& s : stretches(line, rule.unmarkedBars)) {
		const std::vector<double> real = liftsOf(line.pedals, s.first, s.second);
		const std::vector<GpPlayedPedal> moves = gpPedalPassage(song, line, s.first, s.second, rule);
		std::vector<double> made[RULES];
		made[0] = liftsOf(moves, s.first, s.second);
		for (const BeatAt& b : beatsBetween(song, line, s.first, s.second)) {
			made[1].push_back(b.at);
			if (b.index == 0 || (b.count % 2 == 0 && b.index == b.count / 2))
				made[2].push_back(b.at);
			if (b.index == 0)
				made[3].push_back(b.at);
		}
		// Every two bars: every other first beat, counted from the stretch's start.
		for (size_t i = 0; i < made[3].size(); i += 2)
			made[4].push_back(made[3][i]);
		for (int r = 0; r < RULES; r++)
			each[r].add(compare(real, made[r]));
		// THE REAL LIFTS IT MISSED, by where they fall: the first beat of a bar, half way, another
		// beat, or between beats.
		for (double r : real) {
			bool found = false;
			for (double m : made[0])
				found = found || std::fabs(m - r) <= tolerance;
			if (found)
				continue;
			int place = 3;
			for (const BeatAt& b : beatsBetween(song, line, r - tolerance - 1e-6, r + tolerance + 1e-6))
				if (std::fabs(b.at - r) <= tolerance)
					place = (b.index == 0) ? 0 : (b.count % 2 == 0 && b.index == b.count / 2) ? 1 : 2;
			// A bar line exactly at the stretch's edge is not returned by beatsBetween.
			for (const GpPlayedBar& bar : line.bars)
				if (std::fabs(bar.startQuarters - r) <= tolerance)
					place = 0;
			missedAt[place]++;
		}

		// HOW MUCH OF THE TIME THE REAL PEDAL IS DOWN this leaves it up: the rests and the
		// staccato, wrongly.
		int ri = -1, oi = -1;
		for (double q = s.first; q < s.second; q += 0.05) {
			const float bpm = 120.f;
			const bool realDown = gpPedalAt(line.pedals, q, bpm, ri) > 0.5f;
			const bool oursDown = gpPedalAt(moves, q, bpm, oi) > 0.5f;
			if (realDown) {
				dryReal += 0.05;
				if (!oursDown)
					dryOurs += 0.05;
			}
		}

		if (showFrom >= 0) {
			for (size_t i = 0; i < line.bars.size(); i++) {
				const GpPlayedBar& bar = line.bars[i];
				const int number = bar.written + 1;
				if (number < showFrom || number > showTo)
					continue;
				const double a = bar.startQuarters, z = a + bar.quarters;
				if (z <= s.first || a >= s.second)
					continue;
				std::printf("  bar %d (quarters %.2f)\n", number, a);
				for (double r : real)
					if (r >= a - 1e-9 && r < z - 1e-9)
						std::printf("      real lift  at beat %.2f\n", r - a);
				for (const GpPlayedPedal& p : moves)
					if (p.startQuarters >= a - 1e-9 && p.startQuarters < z - 1e-9)
						std::printf("      ours %-6s at beat %.2f\n",
							p.kind == GpPedalMark::UP ? "up" : p.kind == GpPedalMark::DOWN ? "down" : "change",
							p.startQuarters - a);
			}
		}
	}
	if (!quiet) {
		const size_t slash = path.find_last_of('/');
		std::printf("  %-52.52s %4d real |", path.substr(slash + 1).c_str(), each[0].real);
		for (int r = 0; r < RULES; r++)
			std::printf(" %3.0f%% %3.0f%% |", 100.0 * each[r].recall(), 100.0 * each[r].precision());
		std::printf(" dry %2.0f%%\n", dryReal > 0 ? 100.0 * dryOurs / dryReal : 0.0);
	}
	for (int r = 0; r < RULES; r++)
		scores[r].add(each[r]);
	return true;
}

/** ONE BEAT, ASKED EVERY QUESTION, with the pedal held from the last real lift before it: so each
measure is judged on its own and not on the choices it would have made earlier. */
struct Asked {
	bool lift = false;
	int place = 2;             // 0 the first beat of a bar, 1 half way, 2 any other.
	double value[10] = {0};
	/** The beat strikes a bass of its own, at or below anything left ringing with its key up. */
	bool ownBass = false;
};
static const int MEASURES = 8;
static const char* MEASURE_NAMES[MEASURES] = {"clash, keys up", "clash, all", "bass moves",
	"bass rough", "chroma distance", "new pitches", "seconds held", "every pair"};
/** Which measure the table of lifts is drawn up by. */
static int tableMeasure = 1;

static double roughnessOf(int a, int b) {
	int d = std::abs(a - b) % 12;
	if (d > 6)
		d = 12 - d;
	static const double BY_CLASS[7] = {0.0, 1.0, 0.5, 0.15, 0.1, 0.0, 0.5};
	return BY_CLASS[d];
}
static double weightOf(const GpPlayedNote& n) {
	const double level = std::max(0.2, (double) n.dynamic);
	return level * std::min(3.0, std::max(0.15, std::pow(2.0, (60.0 - n.note.midi) / 12.0)));
}
static double fadeOf(int midi) {
	return std::min(8.0, std::max(0.5, 3.0 * std::pow(2.0, (48.0 - midi) / 18.0)));
}

static void ask(const std::string& path, std::vector<Asked>& out) {
	GpSong song;
	std::string why;
	if (!mxReadFile(path, song, &why))
		return;
	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why))
		return;
	std::vector<const GpPlayedNote*> notes;
	for (size_t t = 0; t < line.tracks.size() && t < song.tracks.size(); t++) {
		if (!gpPedalledTrack(song.tracks[t]))
			continue;
		for (const GpPlayedNote& n : line.tracks[t])
			if (!n.note.tied && !n.note.dead)
				notes.push_back(&n);
	}
	std::stable_sort(notes.begin(), notes.end(), [](const GpPlayedNote* a, const GpPlayedNote* b) {
		return a->startQuarters < b->startQuarters;
	});
	for (const std::pair<double, double>& s : stretches(line, 2.0)) {
		const std::vector<double> real = liftsOf(line.pedals, s.first, s.second);
		double held = s.first;
		for (const BeatAt& b : beatsBetween(song, line, s.first, s.second)) {
			// The beat's length, to the next beat or the bar's end.
			const double length = (double) [&]() {
				for (const GpPlayedBar& bar : line.bars)
					if (b.at >= bar.startQuarters - 1e-9 && b.at < bar.startQuarters + bar.quarters - 1e-9)
						return bar.quarters / b.count;
				return 1.0;
			}();
			Asked a;
			for (double r : real)
				if (std::fabs(r - b.at) <= tolerance)
					a.lift = true;
			a.place = (b.index == 0) ? 0 : (b.count % 2 == 0 && b.index == b.count / 2) ? 1 : 2;
			const double now = gpSecondsAt(line, b.at);
			double ringUp[12] = {0}, ringAll[12] = {0}, heldW[12] = {0}, fresh[12] = {0};
			int heldBass = 200, newBass = 200;
			bool any = false;
			for (const GpPlayedNote* p : notes) {
				const GpPlayedNote& n = *p;
				const int pc = ((n.note.midi % 12) + 12) % 12;
				if (n.startQuarters >= held - 1e-9 && n.startQuarters < b.at - 1e-9) {
					const double age = std::max(0.0, now - gpSecondsAt(line, n.startQuarters));
					const double w = weightOf(n) * std::exp(-age / fadeOf(n.note.midi));
					ringAll[pc] += w;
					if (n.startQuarters + n.lengthQuarters <= b.at + 1e-9)
						ringUp[pc] += w;
					heldW[pc] += weightOf(n);
					heldBass = std::min(heldBass, n.note.midi);
				}
				else if (n.startQuarters >= b.at - 1e-9 && n.startQuarters < b.at + length - 1e-9) {
					const double worth = 0.35 + 0.65 * std::min(1.0, n.lengthQuarters / length);
					fresh[pc] += weightOf(n) * worth;
					newBass = std::min(newBass, n.note.midi);
					any = true;
				}
			}
			if (!any) {
				if (a.lift)
					held = b.at;
				continue;
			}
			auto clash = [&](const double* ring) {
				double rs = 0, fs = 0, rough = 0;
				for (int i = 0; i < 12; i++) {
					rs += ring[i];
					fs += fresh[i];
				}
				for (int j = 0; j < 12; j++) {
					const double novel = std::max(0.0, fresh[j] - 3.0 * ring[j]);
					for (int i = 0; i < 12 && novel > 0; i++)
						rough += ring[i] * novel * roughnessOf(i, j);
				}
				const double scale = std::max(rs, fs);
				return scale > 1e-9 ? rough / (scale * scale) : 0.0;
			};
			a.value[0] = clash(ringUp);
			a.value[1] = clash(ringAll);
			{
				double rs = 0, fs = 0, rough = 0;
				for (int i = 0; i < 12; i++) {
					rs += ringUp[i];
					fs += fresh[i];
				}
				for (int i = 0; i < 12; i++)
					for (int j = 0; j < 12; j++)
						rough += ringUp[i] * fresh[j] * roughnessOf(i, j);
				const double scale = std::max(rs, fs);
				a.value[7] = scale > 1e-9 ? rough / (scale * scale) : 0.0;
			}
			if (heldBass < 200) {
				const int hp = heldBass % 12, np = newBass % 12;
				a.value[2] = (hp != np) ? 1.0 : 0.0;
				double most = 0;
				for (int i = 0; i < 12; i++)
					most = std::max(most, heldW[i]);
				a.value[3] = roughnessOf(hp, np) + ((heldW[np] < 0.2 * most) ? 0.5 : 0.0);
				double dot = 0, hh = 0, ff = 0, total = 0, novel = 0;
				for (int i = 0; i < 12; i++) {
					dot += heldW[i] * fresh[i];
					hh += heldW[i] * heldW[i];
					ff += fresh[i] * fresh[i];
					total += fresh[i];
					if (heldW[i] < 0.2 * most)
						novel += fresh[i];
				}
				a.value[4] = (hh > 0 && ff > 0) ? 1.0 - dot / std::sqrt(hh * ff) : 0.0;
				a.value[5] = total > 0 ? novel / total : 0.0;
			}
			a.value[6] = now - gpSecondsAt(line, held);
			{
				int lowestRinging = 200;
				for (const GpPlayedNote* p : notes) {
					const GpPlayedNote& n = *p;
					if (n.startQuarters >= held - 1e-9 && n.startQuarters < b.at - 1e-9
							&& n.startQuarters + n.lengthQuarters <= b.at + 1e-9)
						lowestRinging = std::min(lowestRinging, n.note.midi);
				}
				a.ownBass = newBass <= lowestRinging + 2;
			}
			out.push_back(a);
			if (a.lift)
				held = b.at;
		}
	}
}

/** HOW WELL ONE MEASURE PICKS THE BEATS THAT LIFT: the share of its picks that are real, at the
threshold that finds each share of the real lifts. */
static void separation(const std::vector<Asked>& asked, int m, int place) {
	std::vector<std::pair<double, bool> > v;
	int lifts = 0;
	for (const Asked& a : asked)
		if (place < 0 || a.place == place) {
			v.push_back(std::make_pair(a.value[m], a.lift));
			lifts += a.lift;
		}
	std::sort(v.begin(), v.end(), [](const std::pair<double, bool>& a, const std::pair<double, bool>& b) {
		return a.first > b.first;
	});
	std::printf("    %-18s", MEASURE_NAMES[m]);
	const double wanted[5] = {0.5, 0.7, 0.8, 0.9, 0.95};
	int found = 0, w = 0;
	for (size_t i = 0; i < v.size() && w < 5; i++) {
		found += v[i].second;
		if (lifts && found >= wanted[w] * lifts) {
			std::printf("  %3.0f%% at %-7.3g", 100.0 * found / (double) (i + 1), v[i].first);
			w++;
		}
	}
	std::printf("\n");
}

static void features(const std::vector<std::string>& paths) {
	std::vector<Asked> asked;
	for (const std::string& p : paths)
		ask(p, asked);
	const char* PLACES[3] = {"first beats of bars", "half bars", "other beats"};
	for (int place = -1; place < 3; place++) {
		int lifts = 0, beats = 0;
		for (const Asked& a : asked)
			if (place < 0 || a.place == place) {
				beats++;
				lifts += a.lift;
			}
		std::printf("\n  %s: %d beats with something struck, %d of them lifts (%.0f%%)\n",
			place < 0 ? "all beats" : PLACES[place], beats, lifts, beats ? 100.0 * lifts / beats : 0.0);
		std::printf("    %-18s  %-17s %-17s %-17s %-17s %-17s\n", "real, at recall", "50%", "70%", "80%",
			"90%", "95%");
		for (int m = 0; m < MEASURES; m++)
			separation(asked, m, place);
	}
	// WHERE NOTHING NEW CLASHES, whether the beat strikes its own bass: lifting then loses nothing.
	for (int place = 0; place < 3; place++) {
		int n[2][2] = {{0, 0}, {0, 0}};
		for (const Asked& a : asked)
			if (a.place == place && a.value[1] <= 0.002 && a.value[7] <= 0.05) {
				n[a.ownBass][0]++;
				n[a.ownBass][1] += a.lift;
			}
		std::printf("\n  %s with no clash: own bass %d of %d lift, bass left ringing %d of %d lift\n",
			PLACES[place], n[1][1], n[1][0], n[0][1], n[0][0]);
	}
	for (int place = 0; place < 3; place++) {
		int n[2][2] = {{0, 0}, {0, 0}};
		for (const Asked& a : asked)
			if (a.place == place && !(a.value[1] <= 0.002 && a.value[7] <= 0.05)) {
				n[a.ownBass][0]++;
				n[a.ownBass][1] += a.lift;
			}
		std::printf("  %s with clash: own bass %d of %d lift, bass left ringing %d of %d lift\n",
			PLACES[place], n[1][1], n[1][0], n[0][1], n[0][0]);
	}

	// HOW OFTEN A BEAT LIFTS, by its place, its clash and how long the pedal has been held: the
	// table a rule is read from.
	const double CLASH[6] = {-1, 0.002, 0.02, 0.05, 0.1, 1e9};
	const double HELD[5] = {-1, 1.0, 2.0, 4.0, 1e9};
	for (int place = 0; place < 3; place++) {
		std::printf("\n  %s: lifts of beats, by clash (across) and seconds held (down)\n", PLACES[place]);
		std::printf("    %-10s %-12s %-12s %-12s %-12s %-12s\n", "", "none", "to 0.02", "to 0.05",
			"to 0.1", "above");
		for (int h = 0; h < 4; h++) {
			std::printf("    %-10s", h == 0 ? "under 1" : h == 1 ? "1 to 2" : h == 2 ? "2 to 4" : "over 4");
			for (int c = 0; c < 5; c++) {
				int n = 0, l = 0;
				for (const Asked& a : asked)
					if (a.place == place && a.value[tableMeasure] > CLASH[c] && a.value[tableMeasure] <= CLASH[c + 1]
							&& a.value[6] > HELD[h] && a.value[6] <= HELD[h + 1]) {
						n++;
						l += a.lift;
					}
				std::printf(" %4d/%-4d %2s", l, n, n ? "" : "");
			}
			std::printf("\n");
		}
	}
}

static void totals(const char* what, const Score* scores) {
	std::printf("  %-52s %4d real |", what, scores[0].real);
	for (int r = 0; r < RULES; r++)
		std::printf(" %3.0f%% %3.0f%% |", 100.0 * scores[r].recall(), 100.0 * scores[r].precision());
	std::printf("\n  %-52s           ", "  lifts made, per real lift");
	for (int r = 0; r < RULES; r++)
		std::printf("   %4.2f    |", scores[r].real ? (double) scores[r].made / scores[r].real : 0.0);
	std::printf("\n");
}

int main(int argc, char** argv) {
	PedalRule rule;
	std::vector<std::string> files;
	std::string show;
	int showFrom = -1, showTo = -1;
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		const size_t eq = a.find('=');
		if (eq == std::string::npos) {
			files.push_back(a);
			continue;
		}
		const std::string key = a.substr(0, eq), value = a.substr(eq + 1);
		if (key == "features") {
			const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
			std::vector<std::string> paths;
			const size_t colon = value.find(':');
			if (colon != std::string::npos)
				tableMeasure = std::atoi(value.c_str() + colon + 1);
			if (value.compare(0, 4, "held") == 0)
				for (const char* f : HELD_OUT)
					paths.push_back(home + "/Downloads/" + f);
			else
				for (const char* f : TUNING)
					paths.push_back(home + "/Downloads/" + f);
			features(paths);
			return 0;
		}
		if (key == "show") {
			show = value;
			if (i + 2 < argc) {
				showFrom = std::atoi(argv[++i]);
				showTo = std::atoi(argv[++i]);
			}
			continue;
		}
		const double v = std::atof(value.c_str());
		if (key == "firstNew") rule.firstNew = v;
		else if (key == "firstPairs") rule.firstPairs = v;
		else if (key == "halfNew") rule.halfNew = v;
		else if (key == "halfHeld") rule.halfHeld = v;
		else if (key == "otherNew") rule.otherNew = v;
		else if (key == "otherHeld") rule.otherHeld = v;
		else if (key == "firstOwnBass") rule.firstOwnBass = v > 0.5;
		else if (key == "halfOwnBass") rule.halfOwnBass = v > 0.5;
		else if (key == "otherOwnBass") rule.otherOwnBass = v > 0.5;
		else if (key == "longest") rule.longest = v;
		else if (key == "unmarkedBars") rule.unmarkedBars = v;
		else if (key == "tol") tolerance = v;
		else
			std::printf("not a setting: %s\n", key.c_str());
	}
	const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
	const std::string downloads = home + "/Downloads/";

	if (!show.empty()) {
		Score s[RULES];
		measure(show.find('/') == std::string::npos ? downloads + show : show, rule, s, false,
			showFrom, showTo);
		return 0;
	}

	std::printf("GENERATED PEDALLING AGAINST THE MARKS: lifts found of the real ones, and of its own\n"
		"lifts the share that are real, for each rule. Dry is how much of the time the real pedal\n"
		"is down that the generated one leaves it up.\n\n");
	std::printf("  %-52s %9s |", "", "");
	for (int r = 0; r < RULES; r++)
		std::printf(" %-14s|", RULE_NAMES[r]);
	std::printf("\n");
	if (!files.empty()) {
		Score s[RULES];
		for (const std::string& f : files)
			measure(f, rule, s, false);
		totals("all", s);
		return 0;
	}
	Score tuned[RULES], held[RULES];
	std::printf(" tuned on\n");
	for (const char* f : TUNING)
		measure(downloads + f, rule, tuned, false);
	totals("tuned on, all", tuned);
	std::printf("\n held out\n");
	for (const char* f : HELD_OUT)
		measure(downloads + f, rule, held, false);
	totals("held out, all", held);
	std::printf("\n  missed, by place: %d on first beats, %d half way, %d on other beats, %d between beats\n",
		missedAt[0], missedAt[1], missedAt[2], missedAt[3]);
	return 0;
}
