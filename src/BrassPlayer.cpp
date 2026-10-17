/** See BrassPlayer.hpp. */
#include "BrassPlayer.hpp"
#include "BrassStart.hpp"
#include "Crew.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace px {


/** How long before a note it is decided: time enough to move the slide its whole length and set the lips. */
static const double PREPARE = 0.3;
/** The slide's furthest, metres from first position. */
static const double FURTHEST = 0.60;

/** THE BRIGHTNESS OF EACH DYNAMIC, hertz, by register — E2 to B2, C3 to B3 and C4 to C5, taken at their
middles, MIDI 43.5, 53.5 and 66, as LOUDNESS is — the median of the Iowa recordings' spectral centroids at pp,
mf and ff (research/brass/iowa-tenor-trombone.tsv). Over every pitch they are 628, 982 and 1839 Hz, but the
low register's ff is 1526: aimed at the one figure, the low notes were blown far brighter than the
recordings, and had to be held back by ear. */
static const double BRIGHTNESS_MIDI[3] = {43.5, 53.5, 66.0};
static const double BRIGHTNESS[3][3] = {
	{590.0, 877.0, 1526.0},
	{602.0, 973.0, 1927.0},
	{676.0, 1039.0, 1926.0},
};


/** THE BRIGHTNESS A NOTE'S DYNAMIC ASKS, hertz, nought ppp to seven fff: its register's, by equal ratios from
pp at one to mf at four and on to ff at six, and on beyond them. */
static double brightnessFor(double hz, double dynamic) {
	const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
	auto at = [&](int r) {
		const double* b = BRIGHTNESS[r];
		return dynamic <= 4.0 ? std::log(b[0]) + (dynamic - 1.0) / 3.0 * std::log(b[1] / b[0])
			: std::log(b[1]) + (dynamic - 4.0) / 2.0 * std::log(b[2] / b[1]);
	};
	if (midi <= BRIGHTNESS_MIDI[0])
		return std::exp(at(0));
	if (midi >= BRIGHTNESS_MIDI[2])
		return std::exp(at(2));
	const int r = midi < BRIGHTNESS_MIDI[1] ? 0 : 1;
	const double u = (midi - BRIGHTNESS_MIDI[r]) / (BRIGHTNESS_MIDI[r + 1] - BRIGHTNESS_MIDI[r]);
	return std::exp(at(r) + u * (at(r + 1) - at(r)));
}

/** LOUDNESS, by register — E2 to B2, C3 to B3 and C4 to C5, taken at their middles, MIDI 43.5, 53.5 and 66,
and on straight lines between — each marking's level against mf, dB, over the steady middle of a note: the
model's as it is blown, ppp to fff (make brassplay ARGS="ladder raw"), used under mf, where over it each
note's own level in the tables is; and the Iowa recordings' pp and ff
(make brassref, research/brass/iowa-tenor-trombone.tsv, the medians over the notes they have at all three).
The recordings have no ppp, p, mp, f nor fff: the markings between pp and mf, and between mf and ff, are
spaced evenly, and ppp and fff are one more such step beyond. Each note is played at the difference. */
static const double LOUDNESS_MIDI[3] = {43.5, 53.5, 66.0};
static const double LOUDNESS_MODEL[3][8] = {
	{-16.2, -11.5, -7.9, -4.3, 0.0, 4.8, 8.6, 19.3},
	{-18.7, -13.5, -8.4, -3.8, 0.0, 7.0, 11.7, 22.9},
	{-18.4, -14.8, -11.2, -5.5, 0.0, 9.9, 16.0, 20.7},
};
static const double LOUDNESS_RECORDED_PP[3] = {-12.6, -13.8, -11.4}, LOUDNESS_RECORDED_FF[3] = {8.0, 8.5, 6.8};
/** A NOTE SHORTER THAN THIS IS QUICK, seconds: its start is judged by how soon and how cleanly it speaks
(brassJudgeStart), not against the recordings' starts, which are of long notes; the longest it may take to
rise; and the quickest any tongued start rises, an accent's. */
static const double QUICK_NOTE = 0.45, QUICK_RISE = 0.06, SHORTEST_RISE = 0.025;

/** HOW LATE A START MAY BE HEARD after the tongue lets go, seconds, and what each millisecond more costs in
hundredths of the spreads of the recordings' starts. The shape of a start is measured from where it is
heard, so a note whose lips sputter for 80 ms and then sound all at once looked like a good start: an
accented F sharp 2 did so, 75 cents sharp. A player's note speaks as the tongue lets go. */
static const double LATEST_HEARD = 0.025, LATE_COST = 5.0;

/** WHERE A START SET BY EAR BEGINS ITS BREATH, times the threshold: just over it, where every clean start the
rehearsals found began (make brassplay ARGS="byear ...") — under it the lips sputter and sound late, at twice
it and with a step they blip. */
static const double EAR_FLOOR = 1.3;

/** The gain's glide from one note's to the next, seconds, centred on the note's start. */
static const double GAIN_GLIDE = 0.04;


double BrassPlayer::Ramp::valueAt(int64_t t) const {
	if (t <= at || length <= 0)
		return t <= at && length > 0 ? from : to;
	if (t >= at + length)
		return to;
	const double u = (double) (t - at) / (double) length;
	if (ratios && from > 0.0 && to > 0.0)
		return from * std::pow(to / from, 1.0 - std::pow(1.0 - u, curve));
	const double s = straight ? u : u * u * u * (10.0 - 15.0 * u + 6.0 * u * u);
	return from + s * (to - from);
}


void BrassPlayer::setup(const BrassInstrument::Design& design, const BrassTable& t) {
	setup(design, t, Habits());
}


void BrassPlayer::setup(const BrassInstrument::Design& design, const BrassTable& t, const Habits& h) {
	habits = h;
	defaultAir = design.breathNoise;
	defaultMass = design.lips.mass;
	defaultQuality = design.lips.quality;
	defaultGap = design.lips.restY;
	table = &t;
	rate = design.rate;
	live = Live();
	live.inst.setup(design);
	live.tongue.from = live.tongue.to = 1.0;
	live.lipsHz.from = live.lipsHz.to = std::log(100.0);
	upcoming.clear();
	decided.clear();
	byResonance.clear();
	for (int i = 0; i < t.noteCount; i++) {
		const BrassTableNote& n = t.notes[i];
		if (n.count < 4)
			continue;
		if ((int) byResonance.size() <= n.resonance)
			byResonance.resize((size_t) n.resonance + 1, std::vector<const BrassTableNote*>(8, nullptr));
		byResonance[(size_t) n.resonance][(size_t) n.position] = &n;
	}
	hasLast = false;
}


double BrassPlayer::gainNow() const {
	const double u = std::fmin(1.0, (double) (live.now - gainAt) / (GAIN_GLIDE * rate));
	return gainFrom + u * (gainTo - gainFrom);
}


double BrassPlayer::loudnessGain(const Decision& d, double dynamic) const {
	const double hz = d.note.hz;
	const double k = std::fmax(0.0, std::fmin(7.0, dynamic));
	// OVER mf, THE NOTE'S OWN LEVEL, from the tables: at its pressure, against at mf's. The tables are within
	// half a decibel of the played level from mp up, and as much as 8 dB under it at pp high up, so under mf
	// the register's are used.
	const double ownOverMf = k > 4.0 ? tableAt(d.resonance, d.extension, d.mouth).level
		- tableAt(d.resonance, d.extension, pressureFor(d.resonance, d.extension, brightnessFor(hz, 4.0))).level : 0.0;
	auto atRegister = [&](int r) {
		const double pp = LOUDNESS_RECORDED_PP[r], ff = LOUDNESS_RECORDED_FF[r];
		const double low = -pp / 3.0, high = ff / 2.0;
		auto wanted = [&](int m) { return m <= 4 ? (double) (m - 4) * low : (double) (m - 4) * high; };
		const int a = std::min(6, (int) std::floor(k));
		const double u = k - a;
		const double want = wanted(a) + u * (wanted(a + 1) - wanted(a));
		const double model = k > 4.0 ? ownOverMf : LOUDNESS_MODEL[r][a] + u * (LOUDNESS_MODEL[r][a + 1] - LOUDNESS_MODEL[r][a]);
		return want - model;
	};
	const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
	double db;
	if (midi <= LOUDNESS_MIDI[0])
		db = atRegister(0);
	else if (midi >= LOUDNESS_MIDI[2])
		db = atRegister(2);
	else {
		const int r = midi < LOUDNESS_MIDI[1] ? 0 : 1;
		const double u = (midi - LOUDNESS_MIDI[r]) / (LOUDNESS_MIDI[r + 1] - LOUDNESS_MIDI[r]);
		db = atRegister(r) + u * (atRegister(r + 1) - atRegister(r));
	}
	return std::pow(10.0, db / 20.0);
}


void BrassPlayer::add(const Note& n, int64_t start, bool rehearse) {
	Pending p;
	p.note = n;
	p.start = start;
	p.rehearse = rehearse;
	auto at = std::upper_bound(upcoming.begin(), upcoming.end(), start, [](int64_t s, const Pending& q) { return s < q.start; });
	upcoming.insert(at, p);
}


void BrassPlayer::release() {
	// Nothing more the breath was to do for it; the breath falls now, and the vibrato stops.
	live.actions.erase(std::remove_if(live.actions.begin(), live.actions.end(), [&](const Action& a) {
		return a.kind == Action::BREATH || a.kind == Action::VIBRATO;
	}), live.actions.end());
	Action end;
	end.at = live.now;
	end.kind = Action::BREATH;
	end.value = 0.0;
	end.seconds = 0.045;
	schedule(live, end);
	live.vibratoUntil = std::min(live.vibratoUntil, live.now);
	if (hasLast)
		last.end = std::min(last.end, live.now);
}


void BrassPlayer::withdraw() {
	upcoming.clear();
	while (!gainChanges.empty() && gainChanges.back().at > live.now)
		gainChanges.pop_back();
	// What was planned for notes decided but not begun goes with them: all but the slide's movements.
	live.actions.erase(std::remove_if(live.actions.begin(), live.actions.end(), [](const Action& a) {
		return a.kind != Action::SLIDE;
	}), live.actions.end());
	release();
	// The lips back to following the slide, and the tongue clear, as after any note.
	Action open;
	open.at = live.now;
	open.kind = Action::TONGUE;
	open.value = 1.0;
	open.seconds = 0.004;
	open.straight = true;
	schedule(live, open);
}


double BrassPlayer::resonanceAt(int resonance, double extension) const {
	if (resonance < 0 || resonance >= (int) byResonance.size())
		return 100.0;
	// The positions the tables have for this resonance; the two either side, or the two at the nearer end.
	double es[7], logs[7];
	int n = 0;
	for (int p = 1; p <= 7; p++) {
		const BrassTableNote* note = byResonance[(size_t) resonance][(size_t) p];
		if (note) {
			es[n] = table->positions[p - 1];
			logs[n] = std::log(note->resonanceHz);
			n++;
		}
	}
	if (n == 0)
		return 100.0;
	if (n == 1)
		return std::exp(logs[0]);
	int i = 0;
	while (i + 2 < n && es[i + 1] < extension)
		i++;
	const double u = (extension - es[i]) / (es[i + 1] - es[i]);
	return std::exp(logs[i] + u * (logs[i + 1] - logs[i]));
}


BrassTablePoint BrassPlayer::tableAt(int resonance, double extension, double mouth) const {
	const std::vector<const BrassTableNote*>& ns = byResonance[(size_t) resonance];
	int a = -1, b = -1;
	for (int p = 1; p <= 7; p++)
		if (ns[(size_t) p]) {
			if (table->positions[p - 1] <= extension)
				a = p;
			else if (b < 0)
				b = p;
		}
	if (a < 0)
		return brassTableAt(*table, *ns[(size_t) b], mouth);
	if (b < 0)
		return brassTableAt(*table, *ns[(size_t) a], mouth);
	const BrassTablePoint pa = brassTableAt(*table, *ns[(size_t) a], mouth), pb = brassTableAt(*table, *ns[(size_t) b], mouth);
	const double u = (extension - table->positions[a - 1]) / (table->positions[b - 1] - table->positions[a - 1]);
	auto mix = [&](float x, float y) { return (float) (x + u * (y - x)); };
	return BrassTablePoint{(float) mouth, mix(pa.lips, pb.lips), (float) std::exp(std::log(pa.hz) + u * (std::log(pb.hz) - std::log(pa.hz))),
		mix(pa.level, pb.level), mix(pa.centroid, pb.centroid)};
}


BrassPlayer::Place BrassPlayer::placeFor(double hz, int resonance, bool anyway) const {
	Place place;
	place.resonance = resonance;
	if (resonance < 2 || resonance >= (int) byResonance.size())
		return place;
	// Where this resonance sounds at each position, at the middle of its pressures.
	std::vector<double> es, logs;
	for (int p = 1; p <= 7; p++) {
		const BrassTableNote* n = byResonance[(size_t) resonance][(size_t) p];
		if (!n)
			continue;
		const BrassTablePoint& mid = table->points[n->first + n->count / 2];
		es.push_back(table->positions[p - 1]);
		logs.push_back(std::log(mid.hz));
	}
	if (es.size() < 2)
		return place;
	const double want = std::log(hz);
	// The sounding pitch falls as the slide goes out: the span holding the wanted pitch, or the end one.
	size_t i = 0;
	while (i + 2 < es.size() && logs[i + 1] > want)
		i++;
	const double u = (want - logs[i]) / (logs[i + 1] - logs[i]);
	double e = es[i] + u * (es[i + 1] - es[i]);
	// Beyond the slide's ends, the lips make up what is left, up to 20 cents.
	double left = 0.0;
	if (e < 0.0 || e > FURTHEST) {
		const double clamped = std::fmin(FURTHEST, std::fmax(0.0, e));
		size_t j = clamped <= es[0] ? 0 : es.size() - 2;
		const double v = (clamped - es[j]) / (es[j + 1] - es[j]);
		const double there = logs[j] + v * (logs[j + 1] - logs[j]);
		left = want - there;
		e = clamped;
		if (std::fabs(left) > std::log(2.0) * 20.0 / 1200.0 && !anyway)
			return place;
	}
	place.can = true;
	place.extension = e;
	place.left = left;
	return place;
}


double BrassPlayer::pressureFor(int resonance, double extension, double centroidHz) const {
	const std::vector<const BrassTableNote*>& ns = byResonance[(size_t) resonance];
	// At each of the two positions either side: the pressure at which the centroid reaches what is wanted.
	auto at = [&](const BrassTableNote* n) {
		const BrassTablePoint* p = table->points + n->first;
		double mouth = p[n->count - 1].mouth;
		if (centroidHz <= p[0].centroid * p[0].hz)
			mouth = p[0].mouth;
		else
			for (int i = 0; i + 1 < n->count; i++) {
				const double c0 = p[i].centroid * p[i].hz, c1 = p[i + 1].centroid * p[i + 1].hz;
				if (c1 >= centroidHz && c1 > c0) {
					const double u = (centroidHz - c0) / (c1 - c0);
					mouth = p[i].mouth * std::pow((double) p[i + 1].mouth / p[i].mouth, u);
					break;
				}
			}
		return std::fmax(mouth, 1.08 * n->threshold);
	};
	int a = -1, b = -1;
	for (int p = 1; p <= 7; p++)
		if (ns[(size_t) p]) {
			if (table->positions[p - 1] <= extension)
				a = p;
			else if (b < 0)
				b = p;
		}
	if (a < 0)
		return at(ns[(size_t) b]);
	if (b < 0)
		return at(ns[(size_t) a]);
	const double u = (extension - table->positions[a - 1]) / (table->positions[b - 1] - table->positions[a - 1]);
	return std::exp(std::log(at(ns[(size_t) a])) + u * (std::log(at(ns[(size_t) b])) - std::log(at(ns[(size_t) a]))));
}


double BrassPlayer::thresholdAt(int resonance, double extension) const {
	const std::vector<const BrassTableNote*>& ns = byResonance[(size_t) resonance];
	int a = -1, b = -1;
	for (int p = 1; p <= 7; p++)
		if (ns[(size_t) p]) {
			if (table->positions[p - 1] <= extension)
				a = p;
			else if (b < 0)
				b = p;
		}
	if (a < 0)
		return ns[(size_t) b]->threshold;
	if (b < 0)
		return ns[(size_t) a]->threshold;
	const double u = (extension - table->positions[a - 1]) / (table->positions[b - 1] - table->positions[a - 1]);
	return ns[(size_t) a]->threshold + u * (ns[(size_t) b]->threshold - ns[(size_t) a]->threshold);
}


double BrassPlayer::secondsToMove(double from, double to) const {
	const double d = std::fabs(to - from);
	return d < 0.002 ? 0.0 : 0.035 + 0.12 * d / 0.56;
}


double BrassPlayer::quickest(double from, double to) const {
	const double d = std::fabs(to - from);
	return d < 0.002 ? 0.0 : 0.02 + d / 5.0;
}


void BrassPlayer::schedule(Live& l, const Action& a) {
	auto at = std::upper_bound(l.actions.begin(), l.actions.end(), a.at, [](int64_t t, const Action& b) { return t < b.at; });
	l.actions.insert(at, a);
}


void BrassPlayer::decide(const Pending& p) {
	const Note& n = p.note;
	const int64_t s = p.start;
	const bool joined = hasLast && (n.fromSlur
		|| ((last.note.slurred || last.note.glissando) && s - last.start <= (int64_t) ((last.note.seconds + 0.08) * rate)));
	// THE NOTE AFTER, if it is already known, for the look ahead.
	const Pending* next = upcoming.size() > 1 ? &upcoming[1] : nullptr;
	if (next && next->start - s > (int64_t) (1.5 * rate))
		next = nullptr;

	// WHERE TO PLAY IT.
	Decision d;
	d.start = s;
	d.note = n;
	double best = 1e18;
	Place chosen;
	for (int r = 2; r < (int) byResonance.size(); r++) {
		const Place place = placeFor(n.hz, r);
		if (!place.can)
			continue;
		double cost = 10.0 * place.extension + 50.0 * std::fabs(place.left);
		if (hasLast && s - last.end < (int64_t) (0.5 * rate))
			cost += 4.0 * std::fabs(place.extension - last.extension);
		if (joined) {
			const bool moves = std::fabs(place.extension - last.extension) > 0.005;
			const bool up = n.hz > last.note.hz;
			if (last.note.glissando)
				cost += r == last.resonance ? 0.0 : 5.0;
			else if (moves && r == last.resonance)
				cost += 1.5;
			else if (moves && !(up == (place.extension > last.extension)))
				cost += 1.5;
		}
		if (next) {
			double after = 1e18;
			for (int q = 2; q < (int) byResonance.size(); q++) {
				const Place there = placeFor(next->note.hz, q);
				if (there.can)
					after = std::min(after, 4.0 * std::fabs(there.extension - place.extension) + 10.0 * there.extension);
			}
			if (after < 1e17)
				cost += 0.5 * after;
		}
		if (cost < best) {
			best = cost;
			chosen = place;
		}
	}
	if (!chosen.can) {
		// Nowhere: a note outside the instrument is not played.
		return;
	}
	// SET BY EAR, the slide moved to bring the note in tune with its lips' tension: as far as it reaches.
	if (n.slideCents != 0.0) {
		const Place moved = placeFor(n.hz * std::pow(2.0, -n.slideCents / 1200.0), chosen.resonance, true);
		if (moved.can) {
			chosen.extension = moved.extension;
			chosen.left = 0.0;
		}
	}
	d.resonance = chosen.resonance;
	d.extension = chosen.extension;
	for (int pos = 1; pos <= 7; pos++)
		if (table->positions[pos - 1] <= chosen.extension + 0.04)
			d.position = pos;

	// HOW HARD: the brightness of the written dynamic.
	double dynamic = std::fmin(7.0, std::fmax(0.0, 7.0 * n.level)) - (n.ghost ? 1.5 : 0.0);
	// The tables' pressure for the brightness, or the note's own, found for its own lips.
	double mouth = n.tunedMouth > 0.0 ? n.tunedMouth : pressureFor(chosen.resonance, chosen.extension, brightnessFor(n.hz, dynamic));
	// SET BY EAR, the breath trimmed, never under the threshold; the lips then as the tables give them for it.
	if (n.breath != 1.0)
		mouth = std::fmax(1.02 * thresholdAt(chosen.resonance, chosen.extension), mouth * n.breath);
	const BrassTablePoint pt = tableAt(chosen.resonance, chosen.extension, mouth);
	// The lips: the tables' for that pressure, and up to 20 cents more or less where the slide could not
	// reach, the pitch moving about 0.3 times as far as the lips; or, the note's own lips, at their tension.
	const double ratio = n.tunedLips > 0.0 ? n.tunedLips : pt.lips + chosen.left / 0.3;
	d.mouth = mouth;
	d.lips = ratio;
	d.gain = evenLoudness ? loudnessGain(d, dynamic) : 1.0;
	d.sounds = pt.hz * std::exp(chosen.left);
	d.centroid = pt.centroid * pt.hz;

	// HOW IT JOINS THE NOTE BEFORE.
	if (joined) {
		const bool moves = std::fabs(chosen.extension - last.extension) > 0.005;
		const bool up = n.hz > last.note.hz;
		if (last.note.glissando && chosen.resonance == last.resonance)
			d.join = GLISSANDO;
		else if (!moves)
			d.join = chosen.resonance != last.resonance ? LIP_SLUR : LEGATO_TONGUE;
		else if (chosen.resonance != last.resonance && up == (chosen.extension > last.extension))
			d.join = NATURAL_SLUR;
		else
			d.join = LEGATO_TONGUE;
		// The note before runs into this one: what it had planned as its end is not to be.
		live.actions.erase(std::remove_if(live.actions.begin(), live.actions.end(), [&](const Action& a) {
			return a.kind == Action::BREATH && a.value == 0.0 && a.at >= s - (int64_t) (0.3 * rate);
		}), live.actions.end());
	}
	else
		d.join = TONGUED;

	// THE JOIN, REHEARSED: a slur or a tongued start, the ways of making it each tried on a copy of everything
	// as it will be 200 ms before the note, played to 250 ms after it or the note's end, and judged; or, a
	// situation met before, played the way found best then. A slur may also be made with the legato tongue,
	// as players make the hard ones.
	std::vector<Action> join;
	// A TONGUED START SET BY EAR is played as it was set, untried.
	const bool byEar = n.rise >= 0.0 && d.join == TONGUED;
	if (byEar)
		joinActions(d, dynamic, earHabits(n), live.now, join);
	else
		joinActions(d, dynamic, habits, live.now, join);
	const bool sounding = hasLast && s - last.end < (int64_t) (0.15 * rate);
	if (!byEar && d.join != GLISSANDO && (rehearsing || !memory.empty())) {
		const uint64_t key = situation(d, dynamic, sounding);
		const auto known = memory.find(key);
		// Each way with its place among its join's candidates, which is what is remembered.
		std::vector<std::pair<Join, Habits>> ways;
		std::vector<int> places;
		if (known == memory.end() && !(rehearsing && p.rehearse)) {
			// NOT REHEARSED, and nothing remembered: by the habits.
		}
		else if (known != memory.end()) {
			const std::vector<Habits> all = candidates(known->second.first);
			if (known->second.second < (int) all.size()) {
				ways.push_back(std::make_pair(known->second.first, all[(size_t) known->second.second]));
				places.push_back(known->second.second);
			}
			rememberedCount++;
		}
		else {
			// Fewer ways: every third, the first always among them.
			const int stride = fewer ? 3 : 1;
			const std::vector<Habits> own = candidates(d.join);
			for (int k = 0; k < (int) own.size(); k += stride) {
				ways.push_back(std::make_pair(d.join, own[(size_t) k]));
				places.push_back(k);
			}
			if (d.join != LEGATO_TONGUE && d.join != TONGUED) {
				const std::vector<Habits> legato = candidates(LEGATO_TONGUE);
				for (int k = 0; k < (int) legato.size(); k += stride) {
					ways.push_back(std::make_pair(LEGATO_TONGUE, legato[(size_t) k]));
					places.push_back(k);
				}
			}
			rehearsedCount++;
		}
		if (ways.empty()) {
		}
		else if (ways.size() == 1) {
			Decision tryD = d;
			tryD.join = ways[0].first;
			join.clear();
			joinActions(tryD, dynamic, ways[0].second, live.now, join);
			d.join = ways[0].first;
			d.way = places[0];
		}
		else {
			Live base = live;
			const int64_t from = std::max(base.now, s - (int64_t) (0.2 * rate));
			while (base.now < from)
				step(base);
			// A start is judged against the recordings' over its first 450 ms from where it can be heard, held
			// 0.6 s whatever the note's length, so that a slow one is heard out: nothing after it is planned
			// yet. A quick note's is judged over its own length, by how soon and how cleanly it speaks.
			const bool quick = n.seconds < QUICK_NOTE;
			const int64_t until = s + (int64_t) ((d.join == TONGUED && !quick ? 0.6 : std::min(0.25, n.seconds)) * rate);
			std::vector<BrassJudged> judged(ways.size());
			std::vector<double> scores(ways.size(), 1e18);
			const BrassStartTemplate& like = tromboneStart(brassStartRegister(n.hz), brassStartDynamic(n.level));
			std::vector<std::vector<Action>> acts(ways.size());
			auto trial = [&](int k) {
				Live l = base;
				Decision tryD = d;
				tryD.join = ways[(size_t) k].first;
				joinActions(tryD, dynamic, ways[(size_t) k].second, l.now, acts[(size_t) k]);
				for (const Action& a : acts[(size_t) k])
					schedule(l, a);
				std::vector<double> cup, heard;
				const int64_t first = l.now;
				cup.reserve((size_t) (until - first));
				heard.reserve((size_t) (until - first));
				while (l.now < until) {
					heard.push_back(step(l));
					cup.push_back(l.inst.cupPressure());
				}
				if (tryD.join == TONGUED && quick) {
					judged[(size_t) k] = brassJudgeStart(cup, heard, (size_t) (s - first), rate, d.sounds);
					scores[(size_t) k] = judged[(size_t) k].score();
				}
				else if (tryD.join == TONGUED) {
					// A START: how far its shape is from the recordings' starts at its register and dynamic.
					judged[(size_t) k] = brassJudgeStart(cup, heard, (size_t) (s - first), rate, d.sounds);
					const BrassStartShape shape = brassStartShape(heard.data(), heard.size(), (size_t) std::max<int64_t>(0, s - first - (int64_t) (0.01 * rate)),
						rate, d.sounds);
					const double late = 1000.0 * ((double) shape.onset - (double) (s - first)) / rate - 1000.0 * LATEST_HEARD;
					// A start with a blip or a blat at its front loses to any without (brassStartClean).
					scores[(size_t) k] = shape.valid ? 100.0 * brassStartDistance(shape, like) + LATE_COST * std::fmax(0.0, late)
						+ (brassStartClean(shape) ? 0.0 : 1000.0) : judged[(size_t) k].score() + 2000.0;
				}
				else {
					judged[(size_t) k] = brassJudgeJoin(cup, heard, (size_t) (s - first), rate, last.sounds, d.sounds);
					scores[(size_t) k] = judged[(size_t) k].score();
				}
			};
			if (crew)
				crew->each((int) ways.size(), trial);
			else
				for (int k = 0; k < (int) ways.size(); k++)
					trial(k);
			// The best, in order, so that the choice is the same however the trials were shared out.
			size_t best = 0;
			for (size_t k = 1; k < ways.size(); k++)
				if (scores[k] < scores[best])
					best = k;
			d.tried = (int) ways.size();
			d.judged = judged[best];
			d.join = ways[best].first;
			join = acts[best];
			memory[key] = std::make_pair(ways[best].first, places[best]);
			d.way = places[best];
		}
	}
	if (d.way < 0 && d.join != GLISSANDO)
		habitCount++;
	{
		GainChange g;
		g.at = std::max(live.now, s - (int64_t) (0.5 * GAIN_GLIDE * rate));
		g.gain = d.gain;
		gainChanges.push_back(g);
	}
	for (const Action& a : join)
		schedule(live, a);

	// HOW IT ENDS, unless it runs into the next.
	auto t = [&](double seconds) { return std::max(live.now, s + (int64_t) std::lround(seconds * rate)); };
	double length = n.seconds * (n.staccato ? 0.45 : n.tenuto ? 1.0 : 0.93);
	if (n.staccato)
		length = std::max(length, std::min(n.seconds, 0.06));
	// A tongued note straight after, with no rest: the line goes on, connected (see HOW IT JOINS), and the
	// breath is not let go — the next note's tongue stops it.
	d.connectedNext = next && !(n.slurred || n.glissando) && !n.staccato
		&& (double) (next->start - s) / rate <= n.seconds + 0.05;
	if (next && !(n.slurred || n.glissando) && !d.connectedNext) {
		// A tongued note close after: end in time for the slide to move and the tongue to set.
		const double gap = (double) (next->start - s) / rate;
		length = std::min(length, std::max(0.04, gap - 0.065));
	}
	if (d.connectedNext)
		length = n.seconds;
	d.end = s + (int64_t) std::lround(length * rate);
	if (!d.connectedNext) {
		Action end;
		end.kind = Action::BREATH;
		end.value = 0.0;
		if (n.slurred || n.glissando) {
			end.at = t(n.seconds + 0.03);
			end.seconds = 0.045;
		}
		else {
			end.at = d.end;
			end.seconds = n.staccato ? 0.025 : 0.045;
		}
		schedule(live, end);
	}
	if (n.vibrato > 0 && n.seconds > 0.35) {
		Action v;
		v.at = t(0.18);
		v.kind = Action::VIBRATO;
		v.value = n.vibrato == 2 ? 22.0 : 10.0;
		v.seconds = length - 0.18;
		schedule(live, v);
	}
	// ITS BREATH NOISE AND ITS LIPS, from its start: its own, or the trombone's.
	{
		Action a;
		a.at = t(0.0);
		a.kind = Action::NOISE;
		a.value = n.air >= 0.0 ? n.air : defaultAir;
		schedule(live, a);
		const bool paced = hasLast && last.connectedNext && d.join == TONGUED && n.passagePush > -1.0 && n.passageEased;
		Action lips;
		lips.at = std::max(live.now, t(paced ? -0.025 : hasLast && last.connectedNext && d.join == TONGUED ? -0.02 : -0.05));
		lips.seconds = paced ? 0.02 : 0.0;
		lips.kind = Action::LIPMASS;
		lips.value = n.mass > 0.0 ? n.mass : defaultMass;
		lips.value2 = n.quality > 0.0 ? n.quality : defaultQuality;
		lips.value3 = n.hasGap ? n.gap : defaultGap;
		lips.value4 = d.mouth;
		schedule(live, lips);
		Action waver;
		waver.at = lips.at;
		waver.kind = Action::WAVER;
		waver.value = n.waverBreath;
		waver.value2 = n.waverLips;
		schedule(live, waver);
		// THE VOCAL TRACT: to this note's, gliding while the tongue is stopped before a connected note.
		for (int k = 0; k < 3; k++)
			if (n.formantStrength[k] > 0.0 || (hasLast && last.note.formantStrength[k] > 0.0)) {
				Action f;
				f.at = lips.at;
				f.kind = Action::FORMANT;
				f.resonance = k;
				f.value = n.formantStrength[k] > 0.0 ? n.formantHz[k] : last.note.formantHz[k];
				f.value2 = n.formantStrength[k];
				f.value3 = n.formantStrength[k] > 0.0 ? n.formantQuality[k] : last.note.formantQuality[k];
				f.seconds = paced ? 0.02 : hasLast && last.connectedNext && d.join == TONGUED ? 0.015 : 0.0;
				schedule(live, f);
			}
	}
	decided.push_back(d);
	if (decided.size() > keptMost)
		decided.erase(decided.begin(), decided.begin() + (long) (decided.size() - keptMost / 2));
	last = d;
	hasLast = true;
}


void BrassPlayer::joinActions(const Decision& d, double dynamic, const Habits& h, int64_t earliest, std::vector<Action>& into) const {
	const Note& n = d.note;
	const int64_t s = d.start;
	const double mouth = d.mouth, ratio = d.lips;
	const double from = hasLast ? last.extension : 0.0;
	double move = secondsToMove(from, d.extension);
	// The tongue shuts this long before a tongued note's slide moves.
	double shut = 0.03;
	if (hasLast && d.join == TONGUED) {
		// IN A QUICK PASSAGE the slide moves as fast as it can and the tongue stops the air only while it
		// moves, so that the note before keeps half its time: B flat 2 to C3, first position to sixth, as
		// eighths at 144 a minute left the B flat 50 ms otherwise, and it never spoke.
		const double since = (double) (s - last.start) / rate;
		if (move + shut > 0.5 * since) {
			move = std::min(move, std::max(quickest(from, d.extension), 0.5 * since - 0.01));
			shut = 0.01;
		}
	}
	auto t = [&](double seconds) { return std::max(earliest, s + (int64_t) std::lround(seconds * rate)); };
	auto act = [&](int64_t at, Action::Kind kind, double value, double seconds, bool straight = false, bool ratios = false, double curve = 2.0) {
		Action a;
		a.at = at;
		a.kind = kind;
		a.value = value;
		a.seconds = seconds;
		a.resonance = d.resonance;
		a.straight = straight;
		a.ratios = ratios;
		a.curve = curve;
		into.push_back(a);
	};
	// The lips' first aim in a slur: past the new setting by the overshoot, the way they move.
	double aim = ratio;
	if (hasLast && h.lipsOvershoot != 1.0) {
		const double toward = ratio * resonanceAt(d.resonance, d.extension) / (last.lips * resonanceAt(last.resonance, last.extension));
		aim = ratio * std::pow(toward, h.lipsOvershoot - 1.0);
	}
	const double over = n.heavy ? h.overHeavy : n.accent ? h.overAccent : n.ghost ? 0.0 : h.overOrdinary;
	switch (d.join) {
	case TONGUED: {
		const double release = n.heavy || n.accent ? h.releaseAccent : (n.ghost || dynamic < 2.0) ? h.releaseSoft : h.releaseOrdinary;
		if (hasLast && last.connectedNext) {
			// CONNECTED, the air kept up: stopped only as long as the slide needs at its quickest, the breath
			// passing to the new note's and the lips to its setting while it is, and let go at the breath.
			// The tongue shutting over `close`, the slide moving once it is shut.
			const bool told = n.passagePush > -1.0;
			const double close = told ? n.passageClose : 0.004;
			const bool eased = told && n.passageEased;
			const double fast = std::fmax(quickest(from, d.extension), told ? n.passageSlide : 0.0), stop = std::max(0.025, fast + 0.006 + close);
			// Not a start from silence: the push the player's own, even where the note's start was set by ear.
			const double push = n.heavy ? habits.overHeavy : n.accent ? habits.overAccent : n.ghost ? 0.0 : habits.overOrdinary;
			act(t(-stop), Action::TONGUE, 0.0, close, !eased);
			if (fast > 0.0)
				act(t(-stop + close), Action::SLIDE, d.extension, fast);
			// TONGUED IN A PASSAGE, where the note says how: the breath built up behind the tongue, let go, settling.
			if (n.passagePush > -1.0) {
				act(t(-stop), Action::BREATH, mouth * (1.0 + n.passagePush), stop);
				if (eased)
					act(t(-0.025), Action::LIPS, ratio, 0.02);
				else
					act(t(-0.014), Action::LIPS, ratio, 0.008);
				act(t(0.0), Action::TONGUE, 1.0, n.passageRelease, !eased);
				act(t(n.passageRelease), Action::BREATH, mouth, n.passageSettle);
				break;
			}
			// A note whose start was set by ear starts so here too, only from the tongue's stop instead of silence:
			// the breath down to its floor behind the tongue, then rising over its own rise, the lips with it.
			// At full breath and a little over instead, C sharp 4 after B3 at pp blatted, 7 dB.
			if (h.startRise > 0.0) {
				const double floor = std::fmin(mouth, h.startFloor * thresholdAt(d.resonance, d.extension));
				const double lipsFloor = tableAt(d.resonance, d.extension, floor).lips + (ratio - tableAt(d.resonance, d.extension, mouth).lips);
				act(t(-stop), Action::BREATH, floor, stop);
				act(t(-0.014), Action::LIPS, lipsFloor * h.startLips, 0.008);
				act(t(0.0), Action::LIPS, ratio, std::fmax(h.startGlide, h.startRise));
				act(t(0.0), Action::TONGUE, 1.0, release, true);
				act(t(0.0), Action::BREATH, mouth, h.startRise, false, true, h.riseCurve);
				break;
			}
			// The breath builds behind the stopped tongue a little over the new note's, as in any tongued start.
			act(t(-stop), Action::BREATH, mouth * (1.0 + push), stop);
			act(t(-0.014), Action::LIPS, ratio, 0.008);
			act(t(0.0), Action::TONGUE, 1.0, release, true);
			act(t(0.008), Action::BREATH, mouth, 0.06);
			break;
		}
		act(t(-(move + shut)), Action::TONGUE, 0.0, 0.004, true);
		act(t(-(move + shut - 0.004)), Action::SLIDE, d.extension, move);
		if (h.startRise > 0.0) {
			// Rising from just over the threshold, where the lips sound at once but softly, to the mark: from
			// under it they would sound only late, and then all at once. The recordings' notes are long; a
			// shorter one rises over 80 ms less than half its length at most — a start set by ear, 80 ms less
			// than its length — a quick one over 60 ms — quicker, low notes pop out late and sharp — and an
			// accent over 25 ms.
			const double longest = (h.riseWhole ? n.seconds : 0.5 * n.seconds) - 0.08;
			double rise = std::fmin(h.startRise, std::fmax(QUICK_RISE, longest));
			if (n.accent || n.heavy)
				rise = std::fmin(rise, SHORTEST_RISE);
			const double floor = std::fmin(mouth, h.startFloor * thresholdAt(d.resonance, d.extension));
			const double lipsFloor = tableAt(d.resonance, d.extension, floor).lips + (ratio - tableAt(d.resonance, d.extension, mouth).lips);
			act(t(-0.014), Action::LIPS, lipsFloor * h.startLips, 0.008);
			act(t(0.0), Action::LIPS, ratio, std::fmax(h.startGlide, rise));
			act(t(-0.016), Action::BREATH, floor, 0.008);
			act(t(0.0), Action::BREATH, mouth * (1.0 + over), rise, false, true, h.riseCurve);
			act(t(rise), Action::BREATH, mouth, 0.06);
		}
		else {
			// The lips set as the tongue lets go, and gliding to the note's setting.
			act(t(-0.014), Action::LIPS, ratio * h.startLips, 0.008);
			if (h.startLips != 1.0)
				act(t(0.0), Action::LIPS, ratio, h.startGlide);
			act(t(-0.016), Action::BREATH, mouth * (1.0 + over), 0.008);
			act(t(0.008), Action::BREATH, mouth, 0.06);
		}
		act(t(0.0), Action::TONGUE, 1.0, release, true);
		break;
	}
	case LIP_SLUR: {
		const double g = h.lipSlur, at = h.slurShift;
		act(t(at - 0.5 * g), Action::LIPS, aim, 0.5 * g);
		if (aim != ratio)
			act(t(at + 0.5 * g), Action::LIPS, ratio, std::max(0.01, g));
		act(t(at - 0.5 * g), Action::BREATH, mouth * (1.0 + h.slurPush), std::max(0.005, 0.5 * g));
		act(t(at + 0.5 * g), Action::BREATH, mouth, std::max(0.01, g));
		break;
	}
	case NATURAL_SLUR: {
		const double g = h.naturalSlur, m = std::max(move * h.slurSlide, quickest(from, d.extension));
		const double switchAt = -0.5 * m + h.naturalLipsAt * m + h.slurShift;
		act(t(-0.5 * m), Action::SLIDE, d.extension, m);
		act(t(switchAt - 0.5 * g), Action::LIPS, aim, std::max(0.0, 0.5 * g));
		if (aim != ratio)
			act(t(switchAt + 0.5 * g), Action::LIPS, ratio, std::max(0.01, g));
		act(t(-0.5 * m), Action::BREATH, mouth * (1.0 + h.slurPush), std::max(0.005, switchAt + 0.5 * m));
		act(t(switchAt + 0.5 * g), Action::BREATH, mouth, std::max(0.01, g));
		break;
	}
	case LEGATO_TONGUE: {
		// The tongue half closes while the slide moves, but not so far that the lips stop.
		const double m = std::max(move * h.slurSlide, std::max(0.015, quickest(from, d.extension)));
		const double least = h.legatoAbove * std::max(thresholdAt(d.resonance, d.extension), thresholdAt(last.resonance, last.extension));
		const double open = std::fmin(h.legatoMost, std::fmax(h.legatoLeast, least / std::fmax(mouth, last.mouth)));
		act(t(-(m + 0.008) + h.slurShift), Action::TONGUE, open, 0.008, true);
		act(t(-m + h.slurShift), Action::SLIDE, d.extension, m);
		act(t(-0.012 + h.slurShift), Action::LIPS, aim, 0.012);
		if (aim != ratio)
			act(t(h.slurShift), Action::LIPS, ratio, 0.02);
		act(t(-m + h.slurShift), Action::BREATH, mouth, m);
		act(t(h.slurShift), Action::TONGUE, 1.0, 0.006, true);
		break;
	}
	case GLISSANDO: {
		const double before = std::min(0.6, 0.7 * last.note.seconds);
		const int64_t start = std::max(earliest, std::max(last.start + (int64_t) (0.08 * rate), s - (int64_t) (before * rate)));
		const double seconds = (double) (s - start) / rate;
		act(start, Action::SLIDE, d.extension, seconds, true);
		act(start, Action::LIPS, ratio, 0.0);
		act(start, Action::BREATH, mouth, seconds);
		break;
	}
	}
}


std::vector<BrassPlayer::Habits> BrassPlayer::candidates(Join join) const {
	std::vector<Habits> all;
	switch (join) {
	case LIP_SLUR:
		for (double over : {1.0, 1.05, 1.1})
			for (double shift : {-0.02, 0.0})
				for (double g : {0.0, 0.015, 0.03})
					for (double push : {-0.3, -0.15, 0.0, 0.2}) {
						Habits h = habits;
						h.lipsOvershoot = over;
						h.slurShift = shift;
						h.lipSlur = g;
						h.slurPush = push;
						all.push_back(h);
					}
		break;
	case NATURAL_SLUR:
		for (double sp : {0.3, 0.6})
			for (double when : {0.0, 0.25, 0.5})
				for (double over : {1.0, 1.05})
					for (double push : {-0.15, 0.0, 0.2}) {
						Habits h = habits;
						h.slurSlide = sp;
						h.naturalLipsAt = when;
						h.lipsOvershoot = over;
						h.slurPush = push;
						all.push_back(h);
					}
		break;
	case LEGATO_TONGUE:
		for (double sp : {0.3, 0.6})
			for (double above : {1.3, 1.6, 2.2, 2.8})
				for (double shift : {-0.01, 0.0})
					for (double over : {1.0, 1.05}) {
						Habits h = habits;
						h.slurSlide = sp;
						h.legatoAbove = above;
						h.legatoLeast = 0.3;
						h.slurShift = shift;
						h.lipsOvershoot = over;
						all.push_back(h);
					}
		break;
	case TONGUED:
		// The recordings' starts begin sharp and fall into tune within 30 to 50 ms, and rise over about 100
		// (BrassStart.hpp): the lips from at their note to 6 per cent over it; and the breath at its mark at
		// once, or rising to it from a little over the threshold, where the lips sound at once but softly.
		for (double lips : {1.0, 1.03, 1.06})
			for (int breath = 0; breath < 10; breath++)
				for (double over : {0.0, 0.08}) {
					static const double rises[10] = {0.0, 0.04, 0.04, 0.08, 0.08, 0.14, 0.14, 0.2, 0.2, 0.3};
					static const double floors[10] = {1.0, 0.5, 1.3, 0.5, 1.3, 1.3, 2.0, 1.3, 2.0, 1.3};
					Habits h = habits;
					h.startLips = lips;
					h.startGlide = 0.04;
					h.startRise = rises[breath];
					h.startFloor = floors[breath];
					h.overOrdinary = over;
					all.push_back(h);
				}
		break;
	default:
		all.push_back(habits);
	}
	return all;
}


uint64_t BrassPlayer::situation(const Decision& d, double dynamic, bool sounding) const {
	const uint64_t join = (uint64_t) d.join;
	const uint64_t res = (uint64_t) std::max(0, std::min(31, d.resonance));
	const uint64_t where = (uint64_t) std::max(0, std::min(63, (int) std::lround(d.extension * 50.0)));
	const uint64_t step = (uint64_t) std::max(0, std::min(15, (int) std::lround(dynamic * 2.0)));
	const uint64_t before = sounding && hasLast ? (uint64_t) std::max(0, std::min(31, last.resonance)) : 0;
	const uint64_t beforeWhere = sounding && hasLast ? (uint64_t) std::max(0, std::min(63, (int) std::lround(last.extension * 50.0))) : 0;
	const uint64_t accent = d.note.heavy ? 2 : d.note.accent ? 1 : 0;
	const uint64_t quick = d.note.seconds < QUICK_NOTE ? 1 : 0;
	return join | res << 4 | where << 9 | step << 15 | before << 19 | beforeWhere << 24 | accent << 30 | quick << 32;
}


void BrassPlayer::apply(Live& l, const Action& a) const {
	const int64_t samples = (int64_t) std::lround(a.seconds * rate);
	switch (a.kind) {
	case Action::SLIDE:
		l.slide.move(l.now, a.value, samples, a.straight);
		break;
	case Action::LIPS: {
		const double here = l.lipsHz.valueAt(l.now);
		l.lipsResonance = a.resonance;
		l.lipsRatio = a.value;
		l.lipsHz.from = here;
		l.lipsHz.to = std::log(l.lipsRatio * resonanceAt(l.lipsResonance, l.slide.to));
		l.lipsHz.at = l.now;
		l.lipsHz.length = samples;
		l.lipsHz.straight = false;
		l.lipsFollowFrom = l.now + samples;
		break;
	}
	case Action::BREATH:
		l.breath.move(l.now, a.value, samples, a.straight, a.ratios, a.curve);
		break;
	case Action::TONGUE:
		l.tongue.move(l.now, a.value, samples, a.straight);
		break;
	case Action::NOISE:
		l.inst.setBreathNoise(a.value);
		break;
	case Action::LIPMASS:
		l.inst.setLipGapBreath(a.value4);
		if (samples > 0 && l.lipNow[0] > 0.0) {
			// Gliding there, from where they are.
			for (int j = 0; j < 3; j++)
				l.lipFrom[j] = l.lipNow[j];
			l.lipTo[0] = a.value;
			l.lipTo[1] = a.value2;
			l.lipTo[2] = a.value3;
			l.lipAt = l.now;
			l.lipLength = samples;
			l.lipMoving = true;
			break;
		}
		l.inst.setLipMassAndQuality(a.value, a.value2);
		l.inst.setLipGap(a.value3);
		l.lipNow[0] = a.value;
		l.lipNow[1] = a.value2;
		l.lipNow[2] = a.value3;
		l.lipMoving = false;
		break;
	case Action::WAVER:
		l.waverBreath = std::fmax(0.0, a.value);
		l.waverLips = std::fmax(0.0, a.value2);
		break;
	case Action::FORMANT: {
		// From where it is now, or, where it had none, from the new frequency and quality at no strength.
		const int k = std::min(2, std::max(0, a.resonance));
		const double u = l.formantLength[k] > 0 ? std::fmin(1.0, (double) (l.now - l.formantAt[k]) / (double) l.formantLength[k]) : 1.0;
		double now[3];
		for (int j = 0; j < 3; j++)
			now[j] = l.formantFrom[k][j] + u * (l.formantTo[k][j] - l.formantFrom[k][j]);
		if (now[1] <= 0.0) {
			now[0] = a.value;
			now[2] = a.value3;
		}
		for (int j = 0; j < 3; j++)
			l.formantFrom[k][j] = now[j];
		l.formantTo[k][0] = a.value;
		l.formantTo[k][1] = std::fmax(0.0, a.value2);
		l.formantTo[k][2] = a.value3;
		l.formantAt[k] = l.now;
		l.formantLength[k] = samples;
		l.formantMoving[k] = true;
		break;
	}
	case Action::VIBRATO:
		l.vibratoDepth = a.value;
		l.vibratoFrom = l.now;
		l.vibratoUntil = l.now + samples;
		break;
	}
}


double BrassPlayer::step(Live& l) const {
	while (!l.actions.empty() && l.actions.front().at <= l.now) {
		apply(l, l.actions.front());
		l.actions.erase(l.actions.begin());
	}
	double e = l.slide.valueAt(l.now);
	// SLIDE VIBRATO: the length that moves the pitch by the depth, the slide moving half of it.
	if (l.vibratoDepth > 0.0 && l.now >= l.vibratoFrom && l.now < l.vibratoUntil) {
		const double fade = std::fmin(1.0, (double) (l.now - l.vibratoFrom) / (0.25 * rate));
		const double cents = l.vibratoDepth * fade * std::sin(l.vibratoPhase);
		e += 0.5 * (2.95 + 2.0 * e) * (std::pow(2.0, cents / 1200.0) - 1.0);
		l.vibratoPhase += 2.0 * M_PI * 5.5 / rate;
	}
	else
		l.vibratoPhase = 0.0;
	// THE LIPS: gliding, or following the resonance as the slide moves.
	double logLips;
	if (l.now < l.lipsFollowFrom)
		logLips = l.lipsHz.valueAt(l.now);
	else {
		logLips = std::log(l.lipsRatio * resonanceAt(l.lipsResonance, e));
		l.lipsHz.from = l.lipsHz.to = logLips;
	}
	l.inst.setSlide(e);
	double breath = l.breath.valueAt(l.now);
	// THE WAVER: each wander an Ornstein-Uhlenbeck process of spread one forgetting itself over 55 ms, a step a
	// millisecond, straight between; the breath and the lips' tension each moved by its share of it.
	if (l.waverBreath > 0.0 || l.waverLips > 0.0) {
		const int64_t every = std::max<int64_t>(1, (int64_t) std::lround(0.001 * rate));
		if (l.now - l.wanderAt >= every) {
			l.wanderAt = l.now;
			const double keep = std::exp(-(double) every / (0.055 * rate)), fresh = std::sqrt(1.0 - keep * keep);
			auto gauss = [&]() {
				auto next = [&]() {
					l.wanderState ^= l.wanderState >> 12;
					l.wanderState ^= l.wanderState << 25;
					l.wanderState ^= l.wanderState >> 27;
					return (double) ((l.wanderState * 2685821657736338717ull) >> 11) / 9007199254740992.0;
				};
				const double u1 = std::fmax(1e-12, next()), u2 = next();
				return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
			};
			l.wanderBreath[0] = l.wanderBreath[1];
			l.wanderBreath[1] = keep * l.wanderBreath[1] + fresh * gauss();
			l.wanderLips[0] = l.wanderLips[1];
			l.wanderLips[1] = keep * l.wanderLips[1] + fresh * gauss();
		}
		const double u = (double) (l.now - l.wanderAt) / (double) every;
		breath *= 1.0 + l.waverBreath * (l.wanderBreath[0] + u * (l.wanderBreath[1] - l.wanderBreath[0]));
		logLips += std::log(1.0 + l.waverLips * (l.wanderLips[0] + u * (l.wanderLips[1] - l.wanderLips[0])));
	}
	l.inst.setLips(std::exp(logLips));
	l.inst.setBreath(breath);
	if (l.lipMoving) {
		const double u = std::fmin(1.0, (double) (l.now - l.lipAt) / (double) l.lipLength), s = u * u * u * (10.0 - 15.0 * u + 6.0 * u * u);
		for (int j = 0; j < 3; j++)
			l.lipNow[j] = l.lipFrom[j] + s * (l.lipTo[j] - l.lipFrom[j]);
		l.inst.setLipMassAndQuality(l.lipNow[0], l.lipNow[1]);
		l.inst.setLipGap(l.lipNow[2]);
		if (u >= 1.0)
			l.lipMoving = false;
	}
	for (int k = 0; k < 3; k++)
		if (l.formantMoving[k]) {
			const double u = l.formantLength[k] > 0 ? std::fmin(1.0, (double) (l.now - l.formantAt[k]) / (double) l.formantLength[k]) : 1.0;
			const double* a = l.formantFrom[k];
			const double* b = l.formantTo[k];
			l.inst.setFormant(k, a[0] * std::pow(b[0] / a[0], u), a[1] + u * (b[1] - a[1]), a[2] + u * (b[2] - a[2]));
			if (u >= 1.0)
				l.formantMoving[k] = false;
		}
	l.inst.setTongue(l.tongue.valueAt(l.now));
	l.now++;
	return l.inst.step();
}


BrassPlayer::Habits BrassPlayer::earHabits(const Note& n) const {
	Habits h = habits;
	h.startRise = n.rise;
	h.startFloor = n.startFloor > 0.0 ? n.startFloor : EAR_FLOOR;
	if (n.riseCurve > 0.0)
		h.riseCurve = n.riseCurve;
	h.startLips = n.lips;
	h.startGlide = 0.04;
	h.overOrdinary = 0.0;
	h.riseWhole = true;
	return h;
}


std::vector<BrassPlayer::StartTry> BrassPlayer::startsFor(const BrassInstrument::Design& design, const BrassTable& table, const Habits& habits,
	const Note& n, Crew* crew) {
	// The ways: each rise, held as the note's length holds a start set by ear.
	std::vector<StartTry> ways;
	const double longest = std::fmax(QUICK_RISE, n.seconds - 0.08);
	for (double rise : {0.04, 0.08, 0.14, 0.2, 0.3, 0.45, 0.6}) {
		const double r = std::fmin(rise, longest);
		bool seen = false;
		for (const StartTry& w : ways)
			seen = seen || std::fabs(w.rise - r) < 1e-9;
		if (seen)
			continue;
		StartTry w;
		w.rise = r;
		ways.push_back(w);
	}
	const double rate = design.rate;
	const int64_t at = (int64_t) (0.2 * rate), until = at + (int64_t) (0.6 * rate);
	const BrassStartTemplate& like = tromboneStart(brassStartRegister(n.hz), brassStartDynamic(n.level));
	auto trial = [&](int k) {
		BrassPlayer p;
		p.setup(design, table, habits);
		Note m = n;
		m.rise = ways[(size_t) k].rise;
		m.lips = ways[(size_t) k].lips;
		// Held long enough to be measured; the rise was held to the note's own length above.
		m.seconds = std::fmax(n.seconds, 1.0);
		m.slurred = m.glissando = false;
		p.add(m, at, false);
		std::vector<double> out((size_t) until);
		for (size_t i = 0; i < out.size(); i++)
			out[i] = p.process();
		if (p.decisions().empty()) {
			ways[(size_t) k].distance = 1e9;
			return;
		}
		const BrassStartShape shape = brassStartShape(out.data(), out.size(), (size_t) (at - (int64_t) (0.01 * rate)), rate, p.decisions().back().sounds);
		const double late = 1000.0 * ((double) shape.onset - (double) at) / rate - 1000.0 * LATEST_HEARD;
		ways[(size_t) k].distance = shape.valid ? 100.0 * brassStartDistance(shape, like) + LATE_COST * std::fmax(0.0, late) : 1e9;
		ways[(size_t) k].clean = brassStartClean(shape);
	};
	if (crew)
		crew->each((int) ways.size(), trial);
	else
		for (int k = 0; k < (int) ways.size(); k++)
			trial(k);
	std::stable_sort(ways.begin(), ways.end(), [](const StartTry& a, const StartTry& b) {
		return a.clean != b.clean ? a.clean : a.distance < b.distance;
	});
	return ways;
}


/** THE PITCH of the last `n` samples of `x`, by rising crossings of their mean; nought where too few. */
static double crossingsPitch(const std::vector<double>& x, size_t n, double rate) {
	const size_t from = x.size() - n;
	double mean = 0.0;
	for (size_t i = from; i < x.size(); i++)
		mean += x[i];
	mean /= (double) n;
	double first = -1.0, last = -1.0;
	int count = 0;
	double lo = 1e18, hi = -1e18;
	for (size_t i = from + 1; i < x.size(); i++) {
		lo = std::fmin(lo, x[i]);
		hi = std::fmax(hi, x[i]);
		const double a = x[i - 1] - mean, b = x[i] - mean;
		if (a < 0.0 && b >= 0.0) {
			const double at = (double) (i - 1) + a / (a - b);
			if (first < 0.0)
				first = at;
			last = at;
			count++;
		}
	}
	return count >= 3 && hi - lo > 50.0 ? rate * (count - 1) / (last - first) : 0.0;
}


/** THE PITCH of the last `n` samples of `x` by the period that best repeats near `hz`'s, between 0.7 and 1.4 of
it: the normalized autocorrelation's peak, refined between samples; nought where nothing repeats well. Used
where a vocal tract is present, whose formants put ripples in the waveform that the crossings miscount. */
static double periodPitch(const std::vector<double>& x, size_t n, double rate, double hz) {
	if (n < 16 || x.size() < n || !(hz > 0.0))
		return 0.0;
	const size_t from = x.size() - n;
	const int lo = std::max(2, (int) std::floor(rate / (1.4 * hz))), hi = (int) std::ceil(rate / (0.7 * hz));
	const int span = (int) n - hi - 1;
	if (span < 16)
		return 0.0;
	std::vector<double> r((size_t) (hi - lo + 1), 0.0);
	for (int lag = lo; lag <= hi; lag++) {
		double xy = 0.0, xx = 0.0, yy = 0.0;
		for (int i = 0; i < span; i++) {
			const double a = x[from + (size_t) i], b = x[from + (size_t) (i + lag)];
			xy += a * b;
			xx += a * a;
			yy += b * b;
		}
		r[(size_t) (lag - lo)] = xy / std::sqrt(xx * yy + 1e-30);
	}
	int best = 0;
	for (int k = 1; k < (int) r.size(); k++)
		if (r[(size_t) k] > r[(size_t) best])
			best = k;
	if (r[(size_t) best] < 0.5)
		return 0.0;
	double lag = lo + best;
	if (best > 0 && best + 1 < (int) r.size()) {
		const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
		const double den = a - 2.0 * b + c;
		if (den != 0.0)
			lag += 0.5 * (a - c) / den;
	}
	return rate / lag;
}


/** THE BRIGHTNESS of the last half second of `x`: the centroid of its harmonics' amplitudes under 10 kHz,
hertz, by a Hann-windowed transform at each multiple of `f0`. */
static double harmonicCentroid(const std::vector<double>& x, double f0, double rate) {
	const size_t n = (size_t) (0.5 * rate), from = x.size() - n;
	double a = 0.0, fa = 0.0;
	for (int k = 1; k * f0 < 10000.0 && k <= 40; k++) {
		const double w = 2.0 * M_PI * k * f0 / rate;
		double re = 0.0, im = 0.0;
		for (size_t i = 0; i < n; i++) {
			const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
			re += x[from + i] * win * std::cos(w * (double) i);
			im -= x[from + i] * win * std::sin(w * (double) i);
		}
		const double amp = std::sqrt(re * re + im * im);
		a += amp;
		fa += amp * f0 * k;
	}
	return a > 0.0 ? fa / a : 0.0;
}


BrassPlayer::Tuned BrassPlayer::tuneFor(const BrassInstrument::Design& design, const BrassTable& table, const Habits& habits, const Note& n,
	Crew* crew) {
	Tuned tuned;
	// WHERE, as the player would play it: a player of its own decides it.
	BrassPlayer p;
	p.setup(design, table, habits);
	Note m = n;
	m.tunedLips = m.tunedMouth = -1.0;
	m.slideCents = 0.0;
	m.breath = 1.0;
	m.slurred = m.glissando = false;
	p.add(m, (int64_t) (0.31 * design.rate), false);
	for (int i = 0; i < (int) (0.05 * design.rate) && p.decisions().empty(); i++)
		p.process();
	if (p.decisions().empty())
		return tuned;
	const Decision d = p.decisions().back();
	const double rate = design.rate;
	// THE LIPS' TENSION, where the note gives it: held, and the slide moved instead.
	const double tension = n.tunedLips;
	const bool tense = tension > 0.0;
	// Where the slide is for a note this many cents flatter, as far as it reaches.
	auto extensionAt = [&](double cents) {
		if (cents == 0.0)
			return d.extension;
		const Place moved = p.placeFor(n.hz * std::pow(2.0, -cents / 1200.0), d.resonance, true);
		return moved.can ? moved.extension : d.extension;
	};
	// THE BRIGHTNESS ITS DYNAMIC ASKS, as decide has it.
	const double aim = brightnessFor(n.hz, std::fmin(7.0, std::fmax(0.0, 7.0 * n.level)));
	BrassInstrument::Design own = design;
	if (n.mass > 0.0)
		own.lips.mass = n.mass;
	if (n.quality > 0.0)
		own.lips.quality = n.quality;
	if (n.hasGap)
		own.lips.restY = n.gap;
	// THE NOTE'S VOCAL TRACT, played with it: tuned without it, a note with its tongue placed below it came out
	// 46 cents sharp (A3 at pp) — and its pitch measured by the period, which its ripples do not fool.
	bool tract = false;
	for (int k = 0; k < 3; k++)
		tract = tract || n.formantStrength[k] > 0.0;
	auto pitchOf = [&](const std::vector<double>& x, size_t count) {
		return tract ? periodPitch(x, count, rate, n.hz) : crossingsPitch(x, count, rate);
	};
	auto play = [&](double ratio, double cents, double mouth, double seconds, std::vector<double>& out, std::vector<double>& cup) {
		const double e = extensionAt(cents);
		BrassInstrument inst;
		inst.setup(own);
		for (int k = 0; k < 3; k++)
			if (n.formantStrength[k] > 0.0)
				inst.setFormant(k, n.formantHz[k], n.formantStrength[k], n.formantQuality[k]);
		inst.setSlide(e);
		inst.setLips(ratio * p.resonanceAt(d.resonance, e));
		inst.setTongue(1.0);
		const size_t length = (size_t) (seconds * rate);
		out.assign(length, 0.0);
		cup.assign(length, 0.0);
		for (size_t k = 0; k < length; k++) {
			inst.setBreath(mouth * std::fmin(1.0, (double) k / (0.03 * rate)));
			out[k] = inst.step();
			cup[k] = inst.cupPressure();
		}
	};
	// A MOUTH GIVEN (Note::tunedMouth) with the tension: only the slide is found, at the breath played, by the
	// pitch heard after 0.8 s, a cent of slide for a cent of pitch.
	if (tense && n.tunedMouth > 0.0) {
		const double played = std::fmax(1.02 * p.thresholdAt(d.resonance, d.extension), n.tunedMouth * n.breath);
		std::vector<double> out, cup;
		double cents = 0.0, off = 0.0;
		for (int i = 0; i < 4; i++) {
			play(tension, cents, played, 0.8, out, cup);
			const double f0 = pitchOf(cup, (size_t) (0.3 * rate));
			if (!(f0 > 0.0) || std::fabs(1200.0 * std::log2(f0 / n.hz)) > 300.0)
				return tuned;
			off = 1200.0 * std::log2(f0 / n.hz);
			if (std::fabs(off) < 3.0)
				break;
			cents += off;
		}
		tuned.mouth = n.tunedMouth;
		tuned.lips = tension;
		tuned.slideCents = cents;
		tuned.offCents = off;
		return tuned;
	}
	// AT EACH PRESSURE, the note brought in tune — by the lips, or by the slide with the lips held at their
	// tension — and how bright it is: six pressures from 0.8 to 4 times the tables' for the dynamic, where
	// heavier or more damped lips are found to need it.
	struct Point { double mouth = 0.0, ratio = 0.0, cents = 0.0, off = 0.0, centroid = 0.0; bool sounds = false; };
	const int PRESSURES = 6;
	std::vector<Point> points(PRESSURES);
	auto trial = [&](int k) {
		Point& pt = points[(size_t) k];
		pt.mouth = d.mouth * 0.8 * std::pow(5.0, (double) k / (PRESSURES - 1));
		std::vector<double> out, cup;
		// Halving: the lips' setting from 0.70 to 1.30 of the resonance, or the slide from 150 cents in to 150
		// out. On its resonance means within 150 cents of it.
		double lo = tense ? -150.0 : 0.70, hi = tense ? 150.0 : 1.30;
		for (int i = 0; i < 7; i++) {
			const double mid = 0.5 * (lo + hi);
			const double ratio = tense ? tension : mid, cents = tense ? mid : 0.0;
			play(ratio, cents, pt.mouth, 0.4, out, cup);
			const double f0 = pitchOf(cup, (size_t) (0.2 * rate));
			const double res = p.resonanceAt(d.resonance, extensionAt(cents));
			const bool sounds = f0 > 0.0 && std::fabs(1200.0 * std::log2(f0 / res)) < 150.0;
			if (tense) {
				// Held lips that do not sound will not sound with the slide anywhere else.
				if (!sounds)
					return;
				// Sharp: the slide further out.
				(f0 > n.hz ? lo : hi) = mid;
			}
			else
				((sounds ? f0 > n.hz : ratio > 1.0) ? hi : lo) = mid;
		}
		pt.ratio = tense ? tension : 0.5 * (lo + hi);
		pt.cents = tense ? 0.5 * (lo + hi) : 0.0;
		play(pt.ratio, pt.cents, pt.mouth, 0.8, out, cup);
		double f0 = pitchOf(cup, (size_t) (0.3 * rate));
		pt.off = f0 > 0.0 ? 1200.0 * std::log2(f0 / n.hz) : 0.0;
		// THE SLIDE PUT RIGHT BY EAR: heavy lips are still settling 0.4 s in, and halving by what was heard then
		// left E2 up to 14 cents sharp half a second later. A cent of slide moves the note about a cent.
		for (int i = 0; tense && f0 > 0.0 && i < 2 && std::fabs(pt.off) >= 3.0; i++) {
			pt.cents += pt.off;
			play(pt.ratio, pt.cents, pt.mouth, 0.8, out, cup);
			f0 = pitchOf(cup, (size_t) (0.3 * rate));
			pt.off = f0 > 0.0 ? 1200.0 * std::log2(f0 / n.hz) : 0.0;
		}
		// In tune; or, the slide at its end, on its resonance at least.
		pt.sounds = f0 > 0.0 && std::fabs(pt.off) < (tense ? 150.0 : 40.0);
		if (pt.sounds)
			pt.centroid = harmonicCentroid(out, f0, rate);
	};
	if (crew)
		crew->each(PRESSURES, trial);
	else
		for (int k = 0; k < PRESSURES; k++)
			trial(k);
	std::vector<Point> sounding;
	for (const Point& pt : points)
		if (pt.sounds)
			sounding.push_back(pt);
	if (std::getenv("MPX_TUNE_DEBUG")) {
		std::fprintf(stderr, "    aim %.0f Hz, table mouth %.0f, extension %.3f\n", aim, d.mouth, d.extension);
		for (const Point& pt : points)
			std::fprintf(stderr, "    mouth %5.0f ratio %.3f slide %+4.0f off %+4.0f centroid %5.0f %s\n", pt.mouth, pt.ratio, pt.cents, pt.off, pt.centroid,
				pt.sounds ? "" : "silent");
	}
	if (sounding.empty())
		return tuned;
	// WHERE THE BRIGHTNESS REACHES IT, on straight lines in the pressure's logarithm.
	Point use = aim >= sounding.back().centroid ? sounding.back() : sounding.front();
	for (size_t i = 0; i + 1 < sounding.size(); i++)
		if (sounding[i].centroid <= aim && sounding[i + 1].centroid > aim) {
			const double u = (aim - sounding[i].centroid) / (sounding[i + 1].centroid - sounding[i].centroid);
			use.mouth = sounding[i].mouth * std::pow(sounding[i + 1].mouth / sounding[i].mouth, u);
			use.ratio = sounding[i].ratio + u * (sounding[i + 1].ratio - sounding[i].ratio);
			use.cents = sounding[i].cents + u * (sounding[i + 1].cents - sounding[i].cents);
			use.off = sounding[i].off + u * (sounding[i + 1].off - sounding[i].off);
			break;
		}
	// THE BREATH AS SET BY EAR (Note::breath), never under the threshold, as decide has it: the slide put right
	// for it, since the breath pulls the pitch — at minus 6 dB, E2 sounded 40 cents flat where it was put for the
	// breath found.
	if (tense && n.breath != 1.0) {
		const double played = std::fmax(1.02 * p.thresholdAt(d.resonance, d.extension), use.mouth * n.breath);
		std::vector<double> out, cup;
		for (int i = 0; i < 3; i++) {
			play(use.ratio, use.cents, played, 0.8, out, cup);
			const double f0 = pitchOf(cup, (size_t) (0.3 * rate));
			use.off = f0 > 0.0 ? 1200.0 * std::log2(f0 / n.hz) : 0.0;
			if (f0 <= 0.0 || std::fabs(use.off) < 3.0)
				break;
			use.cents += use.off;
		}
	}
	tuned.mouth = use.mouth;
	tuned.lips = use.ratio;
	tuned.slideCents = use.cents;
	tuned.offCents = use.off;
	return tuned;
}


double BrassPlayer::process() {
	while (!upcoming.empty() && upcoming.front().start - (int64_t) (PREPARE * rate) <= live.now) {
		const Pending p = upcoming.front();
		decide(p);
		upcoming.pop_front();
	}
	// THE GAIN FOR LOUDNESS: on the sound only, after everything that was rehearsed or judged.
	while (!gainChanges.empty() && gainChanges.front().at <= live.now) {
		gainFrom = gainNow();
		gainTo = gainChanges.front().gain;
		gainAt = live.now;
		gainChanges.pop_front();
	}
	return step(live) * gainNow();
}


} // namespace px
