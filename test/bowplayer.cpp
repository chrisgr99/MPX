/** THE PLAYER, from a command line: phase 7 of docs/bowed-strings.md.

WHAT IT CHECKS. That the player plays the phrases settled by ear the way they were settled — every
bow change rehearsed and back to one slip a period within Guettler and Askenfelt's 90 ms, vibrato on
the stopped notes and none on the open ones, the bow kept on the hair — and that a slur is one bow.
Then it plays a part from a score: a MusicXML file, one of its parts, for as many seconds as asked,
and says what it did with each note and how long it took.

    make bowplayer                                   the phrases and the slurs
    make bowplayer ARGS="phrase"                     one section: phrase, slur, sweep
    make bowplayer ARGS="score a.mxl 0 30 violin"    part 0 of a.mxl, 30 s, on the violin
    make bowplayer ARGS="sweep friction=rosin"       with the friction chosen: curve, heat or rosin
    make bowplayer ARGS="rough violin [alone]"       roughness on every string at every dynamic, the
                                                     other strings silent with "alone"
    ... ease=0.1                                     the bite eased over 0.1 s
*/
#include "../src/BowedPlayer.hpp"
#include "../src/BowedRehearsal.hpp"
#include "../src/BowedEngine.hpp"
#include "../src/GpTimeline.hpp"
#include "../src/MusicXML.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace px;

static int failures = 0;

/** THE FRICTION, when given on the command line (friction=curve, heat or rosin), in place of the
design's: see BowedInstrument::Design::friction. Empty is the design's own. */
static std::string frictionWanted;
/** WHERE EVERY REHEARSED DECISION IS WRITTEN, when given on the command line (log=decisions.jsonl):
appended to, one line of JSON a decision. See BowedPlayer::setLog. */
static std::FILE* logFile = nullptr;
/** ECONOMY, when given on the command line: the strings at half the rate, and a string nobody is
playing stopped when it has rung down. See BowedEngine::setup. */
static bool economyWanted = false;

/** THE TABLE OF STARTS, when given on the command line (table=res/bowed-starts.txt): played from
where the player does not rehearse; with tablechanges, for every bow change it has, the starts from
the string still rehearsed; and with tableonly, everywhere, with no rehearsing at all. See
BowedTable.hpp. */
static std::string tableWanted;
static bool tableOnlyWanted = false, tableChangesWanted = false;

/** THE BITE'S EASE, when given on the command line (ease=0.1), in seconds: see Habits::biteEase. */
static float easeWanted = -1.f;

/** WHICH INSTRUMENT a section plays: its name, its design and its player's habits. */
struct Fiddle {
	const char* name;
	BowedInstrument::Design (*design)();
	BowedPlayer::Habits (*habits)();
};
static const Fiddle FIDDLES[3] = {{"violin", BowedInstrument::violin, BowedPlayer::violinHabits},
                                  {"viola", BowedInstrument::viola, BowedPlayer::violaHabits},
                                  {"cello", BowedInstrument::cello, BowedPlayer::celloHabits}};
/** The instrument named, the violin for any other word. */
static const Fiddle& fiddleNamed(const char* name) {
	for (const Fiddle& f : FIDDLES)
		if (name && std::strcmp(name, f.name) == 0)
			return f;
	return FIDDLES[0];
}

/** THE GUESS'S BITE AND ACCELERATION, when given (guessbite=1.26 guessaccel=-41): see Habits::guessBite. */
static float guessBiteWanted = -1.f, guessAccelWanted = 1.f;
/** THE SEED OF THE SMALL VARIATIONS, when given (seed=2): see Habits::seed. Two seeds played side by
side show how far two performances differ by chance alone. */
static int seedWanted = 0;

static BowedPlayer::Habits habitsFor(const Fiddle& f) {
	BowedPlayer::Habits h = f.habits();
	if (easeWanted > 0.f)
		h.biteEase = easeWanted;
	if (guessBiteWanted > 0.f)
		h.guessBite = guessBiteWanted;
	if (guessAccelWanted <= 0.f)
		h.guessAccel = guessAccelWanted;
	if (seedWanted > 0)
		h.seed = (uint32_t) seedWanted;
	return h;
}

/** guessonly: every start not rehearsed the instrument's guess (Habits::guessBite); guessstarts: starts
from the string the guess, bow changes rehearsed. See BowedPlayer::setGuessOnly and setGuessStarts. */
static bool guessOnlyWanted = false, guessStartsWanted = false;
/** live: nothing rehearsed, bow changes turned as Habits::guessTurn (BowedPlayer::LIVE). */
static bool liveWanted = false;
/** THE PART MOVED BY SO MANY SEMITONES, when given (transpose=-1): for hearing a passage's transitions
at other pitches. */
static int transposeWanted = 0;
/** noslursmooth, or slursmooth=SLIDE,LINGER,FROM: see BowedPlayer::SlurSmoothing. */
static BowedPlayer::SlurSmoothing slurSmoothingWanted;
/** REHEARSING A FEW, when given: few=BITE/ACCEL[/SHARE],... and fewglide=RAMP/FLOOR,... See
BowedPlayer::FewStarts. */
static BowedPlayer::FewStarts fewWanted;

/** The table of starts, if one was asked for, given to `p` for the instrument `f`, in its mode; and
the guess, the few starts and the slur smoothing asked for. */
static void useTable(BowedPlayer& p, const Fiddle& f) {
	p.setGuessOnly(guessOnlyWanted);
	p.setGuessStarts(guessStartsWanted);
	p.setSlurSmoothing(slurSmoothingWanted);
	p.setFewStarts(fewWanted);
	if (liveWanted)
		p.setRehearsing(BowedPlayer::LIVE);
	auto t = std::make_shared<BowedTable>();
	if (tableWanted.empty()) {
		// NO TABLE, NO REHEARSING: every start the player's first guess, for comparison.
		if (tableOnlyWanted) {
			p.setTable(t);
			p.setTableUse(BowedPlayer::EVERYTHING);
		}
		return;
	}
	const std::string who = economyWanted ? std::string(f.name) + "-economy" : std::string(f.name);
	if (!t->load(tableWanted, who)) {
		std::printf("  (no table of starts for %s in %s)\n", who.c_str(), tableWanted.c_str());
		return;
	}
	p.setTable(t);
	p.setTableUse(tableOnlyWanted ? BowedPlayer::EVERYTHING
		: (tableChangesWanted ? BowedPlayer::CHANGES : BowedPlayer::WHEN_NO_TIME));
}

static std::string upper(const char* s) {
	std::string u(s);
	for (char& c : u)
		c = (char) std::toupper((unsigned char) c);
	return u;
}

static BowedInstrument::Design withRosin(BowedInstrument::Design d) {
	if (frictionWanted == "curve")
		d.friction = BowedInstrument::Design::CURVE;
	else if (frictionWanted == "heat")
		d.friction = BowedInstrument::Design::HEAT;
	else if (frictionWanted == "rosin")
		d.friction = BowedInstrument::Design::ROSIN;
	return d;
}

static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-66s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}

static void writeWav(const std::string& path, std::vector<float> x) {
	double peak = 0.0;
	for (float v : x)
		peak = std::fmax(peak, std::fabs(v));
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t rate = 48000, n = (uint32_t) x.size(), bytes = n * 2;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (float v : x) {
		const int16_t s = (int16_t) std::lround(32767.0 * 0.8 * (peak > 0.0 ? v / peak : 0.0));
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}

/** THE SOUND AS IT CAME, unscaled, in 32-bit floats: for telling two versions of the engine apart to
the last bit, which a WAV scaled to its peak in sixteen bits cannot. */
static void writeFloatWav(const std::string& path, const std::vector<float>& x) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t rate = 48000, n = (uint32_t) x.size(), bytes = n * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(3); u16(1); u32(rate); u32(rate * 4); u16(4); u16(32);
	std::fwrite("data", 1, 4, f); u32(bytes);
	std::fwrite(x.data(), 4, x.size(), f);
	std::fclose(f);
}

/** A NOTE AND WHEN IT STARTS, as the player is given it. */
struct Timed {
	BowedPlayer::Note note;
	double at = 0.0;
};

/** PLAYS A LIST OF NOTES through a player and returns the sound, with the seconds it took to make. */
static std::vector<float> perform(BowedPlayer& player, const std::vector<Timed>& notes, double* cpu) {
	for (const Timed& t : notes)
		player.add(t.note, t.at);
	double end = 0.0;
	for (const Timed& t : notes)
		end = std::fmax(end, t.at + t.note.seconds);
	std::vector<float> out;
	const auto from = std::chrono::steady_clock::now();
	while (player.now() < end + 1.0)
		out.push_back(player.process());
	if (cpu)
		*cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
	return out;
}

static const char* kindName(int k) {
	return k == BowedPlayer::Played::FRESH ? "from the string" : k == BowedPlayer::Played::CHANGE ? "bow change"
		: k == BowedPlayer::Played::LEGATO ? "slurred" : "crossing";
}

/** How a note's start was heard, for its line: squeak, click and roughness, dB (see Played::heardSqueak). */
static std::string heardOf(const BowedPlayer::Played& p) {
	if (p.heardSqueak >= 99.0)
		return "";
	char out[80];
	std::snprintf(out, sizeof out, ", heard squeak %.0f click %.0f rough %.0f", p.heardSqueak, p.heardClick, p.heardRough);
	return out;
}

/** WHAT THE PLAYER DID, note by note, and the checks every performance has to pass. */
static void report(const BowedPlayer& player, bool each, const char* name) {
	int changes = 0, clean = 0, fresh = 0, started = 0, vibratoOpen = 0;
	for (const BowedPlayer::Played& p : player.played) {
		const float open = player.instrument().design().strings[(size_t) p.string].openHz;
		if (p.vibratoCents > 0.f && p.hz <= open * 1.0005f)
			vibratoOpen++;
		if (p.kind == BowedPlayer::Played::CHANGE) {
			changes++;
			if (p.settled >= 0.0 && p.settled / p.hz <= 0.09)
				clean++;
		}
		if (p.kind == BowedPlayer::Played::FRESH || p.kind == BowedPlayer::Played::CROSS) {
			fresh++;
			if (p.settled >= 0.0 && p.settled <= 10.0)
				started++;
		}
		if (each)
			std::printf("  %6.2f s %7.2f Hz %s level %.1f/7 %.1f/%.2f string %d %-15s %s %.2f N %.2f m/s %3.0f mm, vibrato %2.0f c%s%s\n",
				p.at, p.hz, (p.slipsPerPeriod < 0.9f || p.slipsPerPeriod > 1.1f || p.regular < 0.8f) ? "BAD " : "    ",
				p.level * 7.f,
				p.slipsPerPeriod, p.regular, p.string, kindName(p.kind), p.direction > 0 ? "down" : "up  ", p.force, p.speed,
				p.distance * 1000.f, p.vibratoCents,
				p.kind == BowedPlayer::Played::CHANGE
					? (std::string(", change ") + std::to_string((int) std::lround(p.changeSeconds * 1000)) + " ms x"
						+ std::to_string(p.changeForce).substr(0, 3) + ", settled " + (p.settled < 0 ? "never"
						: std::to_string((int) std::lround(p.settled / p.hz * 1000)) + " ms")).c_str()
					: (p.kind == BowedPlayer::Played::LEGATO ? "" : (std::string(", started in ")
						+ (p.settled < 0 ? "never" : std::to_string((int) p.settled) + " periods")).c_str()),
				(std::string(p.fromTable == 'b' ? ", table bite" : (p.fromTable == 'g' ? ", table glide"
					: (p.fromTable == 'c' ? ", table change" : ""))) + (p.kind == BowedPlayer::Played::LEGATO
					? (p.spoke < 0.0 ? ", regular again never" : ", regular again " + std::to_string((int) std::lround(p.spoke * 1000.0))
						+ " ms")
					: (p.spoke < 0.0 ? ", spoke never" : ", spoke " + std::to_string((int) std::lround(p.spoke * 1000.0))
						+ " ms")) + heardOf(p)).c_str());
	}
	// AS PLAYED, rehearsed or not: how soon each note spoke — four regular periods running from the
	// bow's moving — and how many were bad through their body, by kind; and as heard, the shares
	// rougher, clickier or squeakier than rehearsal allows or prefers.
	{
		std::vector<double> squeaks[2], clicks[2], roughs[2];
		for (const BowedPlayer::Played& p : player.played) {
			if (p.kind == BowedPlayer::Played::LEGATO || p.heardSqueak >= 99.0)
				continue;
			const int k = p.kind == BowedPlayer::Played::CHANGE ? 1 : 0;
			squeaks[k].push_back(p.heardSqueak);
			clicks[k].push_back(p.heardClick);
			roughs[k].push_back(p.heardRough);
		}
		auto quartiles = [](std::vector<double> v) {
			std::sort(v.begin(), v.end());
			char out[64];
			std::snprintf(out, sizeof out, "%5.1f %5.1f %5.1f", v[v.size() / 2], v[(v.size() * 3) / 4], v[(v.size() * 9) / 10]);
			return std::string(out);
		};
		for (int k = 0; k < 2; k++) {
			if (squeaks[k].empty())
				continue;
			int rough = 0, click = 0;
			for (size_t i = 0; i < roughs[k].size(); i++) {
				rough += roughs[k][i] > -35.0;
				click += clicks[k][i] > 0.0;
			}
			std::printf("  heard, %-15s squeak median/75%%/90%% %s, click %s, rough %s; rough %d, clicking %d of %zu\n",
				k ? "bow changes" : "from the string", quartiles(squeaks[k]).c_str(), quartiles(clicks[k]).c_str(),
				quartiles(roughs[k]).c_str(), rough, click, squeaks[k].size());
		}
	}
	{
		struct Tally { int all = 0, bad = 0, in50 = 0, in90 = 0; };
		Tally from, change;
		for (const BowedPlayer::Played& p : player.played) {
			if (p.kind == BowedPlayer::Played::LEGATO)
				continue;
			Tally& t = p.kind == BowedPlayer::Played::CHANGE ? change : from;
			t.all++;
			t.bad += (p.slipsPerPeriod < 0.9f || p.slipsPerPeriod > 1.1f || p.regular < 0.8f) ? 1 : 0;
			t.in50 += (p.spoke >= 0.0 && p.spoke <= 0.05) ? 1 : 0;
			t.in90 += (p.spoke >= 0.0 && p.spoke <= 0.09) ? 1 : 0;
		}
		auto show = [](const char* what, const Tally& t) {
			if (t.all)
				std::printf("  %-18s %4d: bad %3d, spoke within 50 ms %3.0f%%, within 90 ms %3.0f%%\n", what, t.all, t.bad,
					100.0 * t.in50 / t.all, 100.0 * t.in90 / t.all);
		};
		show("from the string", from);
		show("bow changes", change);
		// SLURRED: how soon the string is back to one slip a period after the finger moves.
		std::vector<std::pair<double, double>> slurs;
		for (const BowedPlayer::Played& p : player.played)
			if (p.kind == BowedPlayer::Played::LEGATO)
				slurs.push_back(std::make_pair(p.spoke < 0.0 ? 9.0 : p.spoke, p.at));
		if (!slurs.empty()) {
			std::vector<std::pair<double, double>> by = slurs;
			std::sort(by.begin(), by.end());
			int over = 0;
			for (const auto& v : by)
				over += v.first > 0.03 ? 1 : 0;
			std::printf("  slurred           %4zu: regular again median %.0f ms, later than 30 ms %d; slowest", by.size(),
				by[by.size() / 2].first * 1000.0, over);
			for (size_t k = by.size(); k-- > 0 && k + 5 >= by.size(); )
				std::printf(" %.0f ms at %.2f s,", by[k].first * 1000.0, by[k].second);
			std::printf("\n");
		}
	}
	char what[160];
	// GUETTLER AND ASKENFELT'S 90 MS was measured on the violin, and by ear it holds there. On the
	// cello the count of slips is no guide to what is heard: the cello phrase chosen as best settled
	// by it less often than the one it was chosen over (bow-changes.md). So it is reported, not held.
	std::snprintf(what, sizeof what, "  %s: bow changes back to one slip a period within 90 ms, share of %d", name, changes);
	if (std::strcmp(name, "cello") == 0)
		std::printf("      %-66s %g\n", what, changes ? (double) clean / changes : 1.0);
	else
		check(changes == 0 || clean >= (changes * 3) / 4, what, changes ? (double) clean / changes : 1.0);
	std::snprintf(what, sizeof what, "  %s: notes from the string started within ten periods, share of %d", name, fresh);
	check(fresh == 0 || started >= (fresh * 3) / 4, what, fresh ? (double) started / fresh : 1.0);
	std::snprintf(what, sizeof what, "  %s: notes on an open string given vibrato", name);
	check(vibratoOpen == 0, what, vibratoOpen);
}


// ---- THE PHRASES ---------------------------------------------------------------------------------

/** UP A FIFTH AND BACK, a note a bow, half a second each, at piano: the violin on its A string and the
cello on its D string, the phrases that settled how bow changes, vibrato and the bow's place are
played (research/bowed-strings/bow-changes.md). */
static std::vector<Timed> phrase(double open, bool slurred) {
	const int steps[] = {0, 2, 4, 5, 7, 5, 4, 2, 0};
	std::vector<Timed> notes;
	for (int k = 0; k < 9; k++) {
		Timed t;
		t.note.hz = (float) (open * std::pow(2.0, steps[k] / 12.0));
		t.note.level = 2.f / 7.f;
		t.note.seconds = 0.5f;
		t.note.downbeat = k == 0;
		if (slurred) {
			// In two slurs, four notes and five.
			t.note.slurred = k != 3 && k != 8;
			const int last = k <= 3 ? 3 : 8;
			t.note.slurSeconds = t.note.slurred ? (float) ((last - k + 1) * 0.5) : -1.f;
		}
		t.at = 0.2 + 0.5 * k;
		notes.push_back(t);
	}
	return notes;
}

static void phraseSection(bool slurred) {
	std::printf("\n%s: up a fifth and back at piano, %s.\n", slurred ? "SLURS" : "SEPARATE BOWS",
		slurred ? "four notes in one bow and five in the next" : "a note a bow");
	struct Inst { const char* name; BowedInstrument::Design (*design)(); BowedPlayer::Habits (*habits)(); int string; };
	const Inst insts[] = {{"violin", BowedInstrument::violin, BowedPlayer::violinHabits, 2},
	                      {"cello", BowedInstrument::cello, BowedPlayer::celloHabits, 2}};
	for (const Inst& in : insts) {
		const BowedInstrument::Design d = withRosin(in.design());
		const double open = d.strings[(size_t) in.string].openHz;
		BowedPlayer player;
		player.setHabits(in.habits());
		player.setInstrument(d, 48000.f, (float) (open * 1.5));
		double cpu = 0.0;
		const std::vector<float> out = perform(player, phrase(open, slurred), &cpu);
		std::printf("  %s, %.1f s of sound in %.1f s\n", in.name, out.size() / 48000.0, cpu);
		report(player, true, in.name);
		if (slurred) {
			// UNDER A SLUR THE BOW GOES ON: each note after a slur's first either slurred on, or, where
			// the bow would run short, a bow change hidden in the slur — never a note started afresh.
			int legato = 0, hidden = 0;
			for (const BowedPlayer::Played& p : player.played) {
				legato += p.kind == BowedPlayer::Played::LEGATO;
				hidden += p.hidden;
			}
			char what[96];
			std::snprintf(what, sizeof what, "  %s: notes under a slur slurred on (%d) or a bow change hidden in it", in.name, hidden);
			check(legato + hidden == 7, what, legato + hidden);
		}
		writeWav(std::string("build/bowed-player-") + in.name + (slurred ? "-slurs.wav" : "-phrase.wav"), out);
	}
}


// ---- EVERY STRING, EVERY DYNAMIC --------------------------------------------------------------

/** THE TWO FAULTS ACROSS THE INSTRUMENT: on each string, five notes up from the open string, at pp, p,
mf, f and ff, a note a bow and the five in one slur. Each note judged by how it moved (one regular slip
a period through its body, see Played) and by how much it squeaked (squeakOf on the sound, more than 8
dB below the harmonics passing). A cell is the notes of its phrase that failed either way. */
static bool DETAIL = false;

static void sweepSection() {
	std::printf("\nEVERY STRING, EVERY DYNAMIC: notes of five that moved irregularly (r) or squeaked (s).\n");
	struct Inst { const char* name; BowedInstrument::Design (*design)(); BowedPlayer::Habits (*habits)(); };
	const Inst insts[] = {{"violin", BowedInstrument::violin, BowedPlayer::violinHabits},
	                      {"cello", BowedInstrument::cello, BowedPlayer::celloHabits}};
	const float levels[] = {1.f / 7.f, 2.f / 7.f, 4.f / 7.f, 5.f / 7.f, 6.f / 7.f};
	const char* names[] = {"pp", "p", "mf", "f", "ff"};
	const int steps[] = {0, 2, 4, 5, 7};
	int failed = 0, all = 0;
	for (const Inst& in : insts) {
		const BowedInstrument::Design d = withRosin(in.design());
		std::printf("  %-7s          ", in.name);
		for (const char* n : names)
			std::printf("%-8s", n);
		std::printf("\n");
		for (int s = 0; s < 4; s++)
			for (int slur = 0; slur < 2; slur++) {
				std::printf("  string %d %-9s", s, slur ? "slurred" : "separate");
				for (int l = 0; l < 5; l++) {
					BowedPlayer player;
					player.setHabits(in.habits());
					const double open = d.strings[(size_t) s].openHz;
					player.setInstrument(d, 48000.f, (float) (open * 1.5));
					std::vector<Timed> notes;
					for (int k = 0; k < 5; k++) {
						Timed t;
						t.note.hz = (float) (open * std::pow(2.0, steps[k] / 12.0));
						t.note.level = levels[l];
						t.note.seconds = 0.5f;
						t.note.slurred = slur && k < 4;
						t.note.slurSeconds = t.note.slurred ? (float) ((5 - k) * 0.5) : -1.f;
						t.at = 0.2 + 0.5 * k;
						notes.push_back(t);
					}
					const std::vector<float> out = perform(player, notes, nullptr);
					int r = 0, q = 0, k = 0;
					std::string which;
					for (const BowedPlayer::Played& p : player.played) {
						k++;
						const bool rough = p.slipsPerPeriod < 0.9f || p.slipsPerPeriod > 1.1f || p.regular < 0.8f;
						const size_t a = (size_t) ((p.at + 0.01) * 48000.0), z = std::min(out.size(), (size_t) ((p.at + 0.2) * 48000.0));
						const std::vector<float> body(out.begin() + (long) a, out.begin() + (long) z);
						const double tol = 0.01 + (std::pow(2.0, p.vibratoCents / 1200.0) - 1.0);
						const double sq = squeakOf(body, 48000.0, p.hz, tol);
						r += rough;
						q += sq > -8.0;
						if (DETAIL && (rough || sq > -8.0)) {
							char line[200];
							std::snprintf(line, sizeof line, "\n      note %d %.1f Hz string %d %s: %.2f slips a period, %.2f regular, squeak %+.1f dB",
								k, p.hz, p.string, kindName(p.kind), p.slipsPerPeriod, p.regular, sq);
							which += line;
						}
					}
					all += 5;
					failed += std::max(r, q);
					char cell[16];
					if (!r && !q)
						std::snprintf(cell, sizeof cell, "ok");
					else
						std::snprintf(cell, sizeof cell, "%s%s", r ? (std::string("r") + std::to_string(r)).c_str() : "",
							q ? (std::string(r ? " s" : "s") + std::to_string(q)).c_str() : "");
					std::printf("%-8s%s", cell, which.c_str());
					std::fflush(stdout);
				}
				std::printf("\n");
			}
	}
	check(failed * 10 <= all, "  notes across both instruments that moved irregularly or squeaked, share", (double) failed / all);
}


// ---- ROUGHNESS, EVERY STRING, EVERY DYNAMIC ----------------------------------------------------

/** HOW ROUGH A STRETCH OF THE SOUND IS: roughnessOf (BowedRehearsal.hpp) between two samples. */
static double roughness(const std::vector<float>& x, size_t from, size_t to, double period) {
	if (to > x.size())
		to = x.size();
	if (to <= from)
		return 0.0;
	return roughnessOf(std::vector<float>(x.begin() + (long) from, x.begin() + (long) to), 48000.0, 48000.0 / period);
}

/** ROUGHNESS ACROSS THE INSTRUMENT: on each string the open note, a third, a fifth and an octave up, at
every marking from ppp to fff, two notes of 0.6 s in separate bows without vibrato — a start from the
string and a bow change. A cell is the rougher of the two; ROUGH is worse than -35 dB. */
static bool roughSympathy = true;

static void roughSection(const Fiddle& f) {
	const BowedInstrument::Design d = withRosin(f.design());
	const char* marks[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
	const int steps[4] = {0, 4, 7, 12};
	std::printf("\nROUGHNESS, %s%s: a fresh start / a bow change, dB (worse than -35 is rough).\n",
		upper(f.name).c_str(), roughSympathy ? "" : ", the other strings silent");
	std::printf("  %-18s", "");
	for (const char* m : marks)
		std::printf("%10s", m);
	std::printf("\n");
	int rough = 0, all = 0;
	double worst = -1e9;
	for (int s = 0; s < 4; s++)
		for (int k = 0; k < 4; k++) {
			const double hz = d.strings[(size_t) s].openHz * std::pow(2.0, steps[k] / 12.0);
			std::printf("  string %d %7.2f Hz", s, hz);
			for (int l = 0; l < 8; l++) {
				BowedPlayer player;
				player.setHabits(habitsFor(f));
				player.setLog(logFile, economyWanted ? std::string(f.name) + "-economy" : std::string(f.name));
				player.setInstrument(d, 48000.f, (float) (hz * 1.5));
				player.setSympathetic(roughSympathy);
				useTable(player, f);
				std::vector<Timed> notes;
				for (int n = 0; n < 2; n++) {
					Timed t;
					t.note.hz = (float) hz;
					t.note.level = (float) l / 7.f;
					t.note.seconds = 0.6f;
					t.note.vibrato = BowedPlayer::Note::SENZA;
					t.at = 0.2 + 0.6 * n;
					notes.push_back(t);
				}
				const std::vector<float> out = perform(player, notes, nullptr);
				double each[2];
				for (int n = 0; n < 2; n++) {
					const double at = 0.2 + 0.6 * n;
					each[n] = roughness(out, (size_t) ((at + 0.15) * 48000.0), (size_t) ((at + 0.55) * 48000.0),
						48000.0 / hz);
				}
				const double r = std::fmax(each[0], each[1]);
				std::printf("  %4.0f/%-4.0f", each[0], each[1]);
				std::fflush(stdout);
				all++;
				rough += r > -35.0;
				worst = std::fmax(worst, r);
			}
			std::printf("\n");
		}
	std::printf("  %d of %d rough, the worst %.0f dB\n", rough, all, worst);
}


// ---- A SCORE -------------------------------------------------------------------------------------

/** ONE PART OF A SCORE AS THE PLAYER IS GIVEN IT: the notes in time order, a chord's top note only (two
or more strings at once are not played yet), each with its dynamic, its slur and how long the slur
still runs, its articulation, and whether it begins a bar. */
/** LOUDNESS AS A METER HEARS IT: the K-weighting of ITU-R BS.1770 at 48 kHz, a shelf lifting the top
four decibels and a high-pass under the bass, the two as biquads. */
struct KWeight {
	double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0;
	double run(double x) {
		const double y = 1.53512485958697 * x - 2.69169618940638 * x1 + 1.19839281085285 * x2
			+ 1.69065929318241 * y1 - 0.73248077421585 * y2;
		x2 = x1; x1 = x; y2 = y1; y1 = y;
		const double z = y - 2.0 * u1 + u2 + 1.99004745483398 * z1 - 0.99007225036621 * z2;
		u2 = u1; u1 = y; z2 = z1; z1 = z;
		return z;
	}
};


/** THE BARS OF THE LAST PART READ: where each starts and ends, seconds, and its number on the page. */
struct BarSpan {
	double from = 0.0, to = 0.0;
	int written = 0;
};
static std::vector<BarSpan> barsRead;

static std::vector<Timed> partOf(const std::string& path, int track, double seconds, int* dropped) {
	GpSong song;
	std::string why;
	std::vector<Timed> notes;
	if (!mxReadFile(path, song, &why)) {
		std::printf("could not read %s: %s\n", path.c_str(), why.c_str());
		return notes;
	}
	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why) || track >= (int) line.tracks.size()) {
		std::printf("could not lay it out: %s\n", why.c_str());
		return notes;
	}
	barsRead.clear();
	for (const GpPlayedBar& b : line.bars) {
		BarSpan span;
		span.from = b.startSeconds;
		span.to = b.startSeconds + b.seconds;
		span.written = b.written + 1;
		barsRead.push_back(span);
	}
	std::vector<GpPlayedNote> list;
	for (const GpPlayedNote& p : line.tracks[(size_t) track]) {
		if (p.startSeconds > seconds)
			break;
		if (!list.empty() && std::fabs(list.back().startSeconds - p.startSeconds) < 0.001) {
			(*dropped)++;
			if (p.note.midi > list.back().note.midi)
				list.back() = p;
			continue;
		}
		list.push_back(p);
	}
	for (size_t i = 0; i < list.size(); i++) {
		const GpPlayedNote& p = list[i];
		Timed t;
		t.at = p.startSeconds;
		t.note.hz = (float) (440.0 * std::pow(2.0, (p.note.midi - 69) / 12.0));
		t.note.level = p.dynamic;
		t.note.seconds = (float) p.lengthSeconds;
		t.note.slurred = p.note.slurred;
		t.note.staccato = p.note.staccato;
		t.note.tenuto = p.note.tenuto;
		t.note.accent = p.note.accent || p.note.heavyAccent;
		if (p.bar >= 0 && p.bar < (int) line.bars.size())
			t.note.downbeat = std::fabs(line.bars[(size_t) p.bar].startSeconds - p.startSeconds) < 0.002;
		if (p.note.slurred) {
			size_t k = i;
			while (k + 1 < list.size() && list[k].note.slurred
					&& list[k + 1].startSeconds - (list[k].startSeconds + list[k].lengthSeconds) < 0.05)
				k++;
			t.note.slurSeconds = (float) (list[k].startSeconds + list[k].lengthSeconds - p.startSeconds);
		}
		notes.push_back(t);
	}
	return notes;
}


static void scoreSection(const std::string& path, int track, double seconds, const Fiddle& f) {
	int dropped = 0;
	std::vector<Timed> notes = partOf(path, track, seconds, &dropped);
	for (Timed& t : notes)
		t.note.hz *= (float) std::pow(2.0, transposeWanted / 12.0);
	if (notes.empty())
		return;
	float highest = 0.f;
	for (const Timed& t : notes)
		highest = std::fmax(highest, t.note.hz);
	std::printf("\nPART %d of %s, %.0f s, on the %s: %zu notes, %d chord notes below the top left out.\n",
		track, path.c_str(), seconds, f.name, notes.size(), dropped);
	BowedPlayer player;
	player.setHabits(habitsFor(f));
	player.setLog(logFile, economyWanted ? std::string(f.name) + "-economy" : std::string(f.name));
	player.setInstrument(withRosin(f.design()), 48000.f, economyWanted ? 0.5f * highest : highest);
	player.setQuietStringsRest(economyWanted);
	useTable(player, f);
	double cpu = 0.0;
	const std::vector<float> out = perform(player, notes, &cpu);
	std::printf("  %.1f s of sound in %.1f s of computing\n", out.size() / 48000.0, cpu);
	{
		const BowedPlayer::Spent& w = player.spent();
		std::printf("  rehearsing: run up %.1f s, shortlist %.1f s (%d of %d starts widened), bites %.1f s, glides %.1f s,"
			" bow changes %.1f s\n", w.runUp, w.shortlist, w.widened, w.starts, w.bites, w.glides, w.changes);
	}
	{
		double rehearsed = 0.0;
		for (const BowedPlayer::Played& p : player.played)
			rehearsed += (double) p.rehearsed;
		std::printf("  %.1f s of the instrument simulated in rehearsing the starts and changes\n",
			rehearsed / player.stringRate());
	}
	report(player, true, f.name);
	// EACH BAR, as pianobench reports the piano's: the mean marking of its notes, and the level through
	// it at the module's output — decibels under the 8 V a single fff note peaks at.
	if (std::getenv("BARS")) {
		// The modules' own volts a pascal: mpxCello.cpp, mpxViola.cpp, mpxViolin.cpp.
		const float volts = std::strcmp(f.name, "cello") == 0 ? 1.07f : (std::strcmp(f.name, "viola") == 0 ? 4.29f : 2.97f);
		std::printf("\nBY BAR: the marking (nought ppp, seven fff) and the %s's level\n", f.name);
		// K-weighted, both channels of the module's output summed as a meter sums them: a centred
		// instrument puts the same on each.
		std::vector<double> weighted(out.size());
		{
			KWeight k;
			for (size_t i = 0; i < out.size(); i++)
				weighted[i] = k.run((double) out[i] * volts);
		}
		for (const BarSpan& b : barsRead) {
			double marks = 0.0;
			int count = 0;
			for (const Timed& t : notes)
				if (t.at >= b.from && t.at < b.to) {
					marks += t.note.level;
					count++;
				}
			const size_t i0 = (size_t) (b.from * 48000.0), i1 = std::min(out.size(), (size_t) (b.to * 48000.0));
			if (!count || i1 <= i0)
				continue;
			double e = 0.0, ke = 0.0;
			float peak = 0.f;
			for (size_t i = i0; i < i1; i++) {
				const double v = (double) out[i] * volts;
				e += v * v;
				ke += 2.0 * weighted[i] * weighted[i];
				peak = std::fmax(peak, (float) std::fabs(v));
			}
			std::printf("  bar %3d  marking %.1f  %3d notes  level %6.1f dB  peak %5.2f V  loudness %6.1f\n", b.written,
				7.0 * marks / count, count, 10.0 * std::log10(e / (double) (i1 - i0) + 1e-30) - 20.0 * std::log10(8.0), peak,
				-0.691 + 10.0 * std::log10(ke / (double) (i1 - i0) / 100.0 + 1e-30));
		}
	}
	// Named for the friction when it was chosen, so that versions played side by side do not overwrite
	// each other.
	std::string rosin = frictionWanted.empty() ? std::string() : "-" + frictionWanted;
	if (economyWanted)
		rosin += "-economy";
	if (!slurSmoothingWanted.on)
		rosin += "-noslursmooth";
	if (transposeWanted != 0)
		rosin += "-transpose" + std::to_string(transposeWanted);
	if (liveWanted)
		rosin += "-live";
	else if (guessStartsWanted)
		rosin += "-startsguessed";
	else if (guessOnlyWanted)
		rosin += "-bite";
	if (fewWanted.any())
		rosin += "-few" + std::to_string(fewWanted.bites.size()) + "-" + std::to_string(fewWanted.glideRamps.size());
	if (tableOnlyWanted)
		rosin += tableWanted.empty() ? "-guess" : "-table";
	else if (!tableWanted.empty())
		rosin += tableChangesWanted ? "-table-changes" : "-quick-table";
	if (easeWanted > 0.f) {
		char named[32];
		std::snprintf(named, sizeof named, "-ease%g", easeWanted);
		rosin += named;
	}
	writeWav(std::string("build/bowed-player-score-") + (f.name) + rosin + ".wav", out);
	writeFloatWav(std::string("build/bowed-player-score-") + (f.name) + rosin + "-raw.wav", out);
}


/** HOW FAR AHEAD THE BENCH ANNOUNCES NOTES, seconds: the cable's MPX_ANNOUNCE_AHEAD. */
static const double AHEAD = 3.0;

/** THE ENGINE: a part played by the player alone, and through BowedEngine's two threads with each
note announced AHEAD, as mpxScore announces them. Waiting for the planner, the two must
be the same sample for sample; paced to real time, as a module is, it reports how often the playing
copy had to decide without the planner, and how much of the time the audio thread was busy. */
static void engineSection(const std::string& path, int track, double seconds, const Fiddle& f) {
	int dropped = 0;
	std::vector<Timed> notes = partOf(path, track, seconds, &dropped);
	for (Timed& t : notes)
		t.note.hz *= (float) std::pow(2.0, transposeWanted / 12.0);
	if (notes.empty())
		return;
	float highest = 0.f;
	double end = 0.0;
	for (const Timed& t : notes) {
		highest = std::fmax(highest, t.note.hz);
		end = std::fmax(end, t.at + t.note.seconds);
	}
	const BowedInstrument::Design design = withRosin(f.design());
	std::printf("\nTHE ENGINE: part %d of %s, %.0f s, on the %s.\n", track, path.c_str(), seconds,
		f.name);
	const double engineRate = 48000.0;
	const size_t length = (size_t) ((end + 1.0) * engineRate);
	// ALONE, each note given at the engine sample it starts on, as the engine is given it.
	std::vector<float> alone;
	{
		BowedPlayer solo;
		solo.setHabits(habitsFor(f));
		solo.setInstrument(design, (float) engineRate, economyWanted ? 0.5f * highest : highest);
		solo.setQuietStringsRest(economyWanted);
		const int64_t factor = std::llround(solo.stringRate() / engineRate);
		for (const Timed& t : notes)
			solo.addAt(t.note, std::llround(t.at * engineRate) * factor);
		const auto from = std::chrono::steady_clock::now();
		while (alone.size() < length)
			alone.push_back(solo.process());
		std::printf("  alone: %.1f s of sound in %.1f s\n", length / engineRate,
			std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count());
	}
	auto through = [&](bool patient, bool realTime, const char* what) {
		BowedEngine engine;
		BowedEngine::Setup setup;
		setup.design = design;
		setup.habits = habitsFor(f);
		setup.engineRate = (float) engineRate;
		setup.highestHz = highest;
		setup.announcedAhead = AHEAD;
		setup.economy = economyWanted;
		// Rehearsing as the solo player does, so the two can be compared sample for sample.
		setup.rehearsing = BowedPlayer::FULL;
		engine.setup(setup);
		engine.setPatient(patient);
		std::vector<float> out;
		out.reserve(length);
		size_t next = 0;
		double busy = 0.0, worst = 0.0;
		const auto from = std::chrono::steady_clock::now();
		for (size_t i = 0; i < length; i++) {
			const double now = (double) i / engineRate;
			while (next < notes.size() && notes[next].at <= now + AHEAD) {
				engine.announce(notes[next].note, std::llround(notes[next].at * engineRate) - (int64_t) i);
				next++;
			}
			const auto a = std::chrono::steady_clock::now();
			out.push_back(engine.process());
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
			busy += took;
			worst = std::fmax(worst, took);
			// AS A MODULE IS RUN: never ahead of the clock on the wall.
			if (realTime) {
				while (std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count() < now)
					std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
		}
		const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
		size_t differ = 0, first = 0;
		double most = 0.0;
		for (size_t i = 0; i < length; i++)
			if (out[i] != alone[i]) {
				if (!differ)
					first = i;
				differ++;
				most = std::fmax(most, std::fabs((double) out[i] - alone[i]));
			}
		std::printf("  %s: %.1f s, the audio thread busy %.0f%% of the sound's length, longest sample %.2f ms; "
			"the planner started again %d times\n", what, wall, 100.0 * busy / ((double) length / engineRate),
			worst * 1000.0, engine.restarts());
		if (differ)
			std::printf("    differs from the player alone in %zu samples, from %.3f s, by up to %.3g\n", differ,
				first / engineRate, most);
		else
			std::printf("    the same as the player alone, sample for sample\n");
		return differ;
	};
	char what[160];
	std::snprintf(what, sizeof what, "  %s: the engine waiting for its planner plays as the player alone does",
		f.name);
	const size_t differ = through(true, false, "waiting for the planner");
	check(differ == 0, what, (double) differ);
	through(false, true, "in real time");
}


/** THE THREE TOGETHER, as a patch plays a quartet: parts 0, 1 and 2 of a score on the violin, viola and
cello, each through an engine of its own sharing the one crew, paced to real time on one thread as
Rack's engine runs them. How often each one's planner fell behind, and how much of the time each one's
playing took. */
static void quartetSection(const std::string& path, double seconds) {
	const Fiddle* fiddles[3] = {&FIDDLES[0], &FIDDLES[1], &FIDDLES[2]};
	std::vector<Timed> parts[3];
	double end = 0.0;
	BowedEngine engines[3];
	for (int k = 0; k < 3; k++) {
		int dropped = 0;
		parts[k] = partOf(path, k, seconds, &dropped);
		float highest = 0.f;
		for (const Timed& t : parts[k]) {
			highest = std::fmax(highest, t.note.hz);
			end = std::fmax(end, t.at + t.note.seconds);
		}
		BowedEngine::Setup setup;
		setup.design = fiddles[k]->design();
		setup.habits = habitsFor(*fiddles[k]);
		setup.highestHz = highest;
		setup.announcedAhead = AHEAD;
		setup.economy = economyWanted;
		setup.rehearsing = BowedPlayer::FULL;
		engines[k].setup(setup);
		engines[k].setWatching(false);
	}
	std::printf("\nTHE QUARTET'S STRINGS: %s, %.0f s, in real time.\n", path.c_str(), seconds);
	const size_t length = (size_t) ((end + 1.0) * 48000.0);
	size_t next[3] = {0, 0, 0};
	double busy[3] = {0.0, 0.0, 0.0};
	const auto from = std::chrono::steady_clock::now();
	for (size_t i = 0; i < length; i++) {
		const double now = (double) i / 48000.0;
		for (int k = 0; k < 3; k++) {
			while (next[k] < parts[k].size() && parts[k][next[k]].at <= now + AHEAD) {
				engines[k].announce(parts[k][next[k]].note, std::llround(parts[k][next[k]].at * 48000.0) - (int64_t) i);
				next[k]++;
			}
			const auto a = std::chrono::steady_clock::now();
			engines[k].process();
			busy[k] += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
		}
		while (std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count() < now)
			std::this_thread::sleep_for(std::chrono::microseconds(200));
	}
	for (int k = 0; k < 3; k++)
		std::printf("  %-6s: playing busy %.0f%% of the time; its planner started again %d times\n", fiddles[k]->name,
			100.0 * busy[k] / ((double) length / 48000.0), engines[k].restarts());
}


int main(int rawArgc, char** rawArgv) {
	// The settings, wherever they are, taken out before the words are read in order.
	std::vector<char*> kept;
	for (int i = 0; i < rawArgc; i++) {
		if (std::strncmp(rawArgv[i], "friction=", 9) == 0)
			frictionWanted = rawArgv[i] + 9;
		else if (std::strncmp(rawArgv[i], "log=", 4) == 0)
			logFile = std::fopen(rawArgv[i] + 4, "a");
		else if (std::strcmp(rawArgv[i], "economy") == 0)
			economyWanted = true;
		else if (std::strncmp(rawArgv[i], "table=", 6) == 0)
			tableWanted = rawArgv[i] + 6;
		else if (std::strcmp(rawArgv[i], "tableonly") == 0)
			tableOnlyWanted = true;
		else if (std::strcmp(rawArgv[i], "tablechanges") == 0)
			tableChangesWanted = true;
		else if (std::strcmp(rawArgv[i], "guessonly") == 0)
			guessOnlyWanted = true;
		else if (std::strcmp(rawArgv[i], "guessstarts") == 0)
			guessStartsWanted = guessOnlyWanted = true;
		else if (std::strncmp(rawArgv[i], "few=", 4) == 0) {
			// BITE/ACCEL[/SHARE], comma separated.
			const char* c = rawArgv[i] + 4;
			while (*c) {
				char* e;
				const float b = std::strtof(c, &e);
				if (*e != '/')
					break;
				const float a = std::strtof(e + 1, &e);
				float share = 1.f;
				if (*e == '/')
					share = std::strtof(e + 1, &e);
				fewWanted.bites.push_back(b);
				fewWanted.accels.push_back(a);
				fewWanted.shares.push_back(share);
				c = *e == ',' ? e + 1 : e;
			}
		}
		else if (std::strncmp(rawArgv[i], "fewglide=", 9) == 0) {
			// RAMP/FLOOR, comma separated.
			const char* c = rawArgv[i] + 9;
			while (*c) {
				char* e;
				const float r = std::strtof(c, &e);
				if (*e != '/')
					break;
				const float f = std::strtof(e + 1, &e);
				fewWanted.glideRamps.push_back(r);
				fewWanted.glideFloors.push_back(f);
				c = *e == ',' ? e + 1 : e;
			}
		}
		else if (std::strncmp(rawArgv[i], "transpose=", 10) == 0)
			transposeWanted = std::atoi(rawArgv[i] + 10);
		else if (std::strcmp(rawArgv[i], "live") == 0)
			liveWanted = true;
		else if (std::strcmp(rawArgv[i], "noslursmooth") == 0)
			slurSmoothingWanted.on = false;
		else if (std::strncmp(rawArgv[i], "slursmooth=", 11) == 0)
			std::sscanf(rawArgv[i] + 11, "%f,%f,%f", &slurSmoothingWanted.slide, &slurSmoothingWanted.linger,
				&slurSmoothingWanted.from);
		else if (std::strncmp(rawArgv[i], "seed=", 5) == 0)
			seedWanted = std::atoi(rawArgv[i] + 5);

		else if (std::strncmp(rawArgv[i], "guessbite=", 10) == 0)
			guessBiteWanted = (float) std::atof(rawArgv[i] + 10);
		else if (std::strncmp(rawArgv[i], "guessaccel=", 11) == 0)
			guessAccelWanted = (float) std::atof(rawArgv[i] + 11);
		else if (std::strncmp(rawArgv[i], "ease=", 5) == 0)
			easeWanted = (float) std::atof(rawArgv[i] + 5);
		else
			kept.push_back(rawArgv[i]);
	}
	const int argc = (int) kept.size();
	char** argv = kept.data();
	const char* only = argc > 1 ? argv[1] : "";
	auto run = [&](const char* name) { return !*only || std::strcmp(only, name) == 0; };
	if (std::strcmp(only, "quartet") == 0) {
		if (argc < 3) {
			std::printf("usage: bowplayer quartet <file> [seconds]\n");
			return 1;
		}
		quartetSection(argv[2], argc > 3 ? std::atof(argv[3]) : 30.0);
	}
	else if (std::strcmp(only, "engine") == 0) {
		if (argc < 3) {
			std::printf("usage: bowplayer engine <file> [part] [seconds] [violin|viola|cello]\n");
			return 1;
		}
		engineSection(argv[2], argc > 3 ? std::atoi(argv[3]) : 0, argc > 4 ? std::atof(argv[4]) : 10.0,
			fiddleNamed(argc > 5 ? argv[5] : "violin"));
	}
	else if (std::strcmp(only, "score") == 0) {
		if (argc < 3) {
			std::printf("usage: bowplayer score <file> [part] [seconds] [violin|viola|cello]\n");
			return 1;
		}
		scoreSection(argv[2], argc > 3 ? std::atoi(argv[3]) : 0, argc > 4 ? std::atof(argv[4]) : 30.0,
			fiddleNamed(argc > 5 ? argv[5] : "violin"));
	}
	else {
		if (run("phrase")) phraseSection(false);
		if (run("slur")) phraseSection(true);
		DETAIL = argc > 2 && std::strcmp(argv[2], "detail") == 0;
		if (run("sweep")) sweepSection();
		// Not in the default run: two instruments of 128 notes each.
		if (std::strcmp(only, "rough") == 0) {
			roughSympathy = !(argc > 3 && std::strcmp(argv[3], "alone") == 0);
			roughSection(fiddleNamed(argc > 2 ? argv[2] : "violin"));
		}
	}
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
