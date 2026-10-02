/** The finger picker's two hands, played through a progression. See docs/finger-picker.md.

	make pickertest
*/
#include "../src/FingerPicking.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

using namespace px;

static int failures = 0;

static void check(bool ok, const std::string& what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok)
		failures++;
}

/** A chord from its symbol: the root, then m, 7, m7 or maj7, then an optional slash bass. The
root, the third and any seventh are essential; the fifth is not. */
static ChordNotes chordFrom(const std::string& name) {
	static const int natural[7] = {9, 11, 0, 2, 4, 5, 7};
	auto letter = [&](size_t at, size_t* used) {
		int pc = natural[name[at] - 'A'];
		*used = 1;
		if (at + 1 < name.size() && (name[at + 1] == '#' || name[at + 1] == 'b')) {
			pc += name[at + 1] == '#' ? 1 : -1;
			*used = 2;
		}
		return (pc + 12) % 12;
	};
	size_t used;
	const int root = letter(0, &used);
	std::string q = name.substr(used);
	int bass = root;
	const size_t slash = q.find('/');
	if (slash != std::string::npos) {
		size_t u;
		bass = letter(used + slash + 1, &u);
		q = q.substr(0, slash);
	}
	int third = 4, seventh = -1;
	if (q == "m")
		third = 3;
	else if (q == "7")
		seventh = 10;
	else if (q == "m7") {
		third = 3;
		seventh = 10;
	}
	else if (q == "maj7")
		seventh = 11;
	ChordNotes c;
	auto add = [&](int interval, bool essential, int degree) {
		c.pc[c.count] = (root + interval) % 12;
		c.essential[c.count] = essential;
		c.degree[c.count] = (int8_t) degree;
		c.count++;
	};
	add(0, true, 1);
	add(third, true, 3);
	add(7, false, 5);
	if (seventh >= 0)
		add(seventh, true, 7);
	c.bass = bass;
	return c;
}

/** Whether a shape meets every finger-picking requirement, said in words if not. */
static std::string faults(const Shape& s, const ChordNotes& c, const Tuning& t, const Hand& hand) {
	if (!gripOf(s, t, hand).playable)
		return "beyond the hand";
	int bass = -1, bassStrings = 0;
	for (int k = BASS_STRING_FIRST; k < t.strings; k++)
		if (s.sounds(k)) {
			bass = k;
			bassStrings++;
		}
	if (bassStrings != 1)
		return "not one bass string";
	if (s.note(t, bass) % 12 != c.bass)
		return "the bass is not the chord's";
	for (int k = 0; k < BASS_STRING_FIRST; k++) {
		if (!s.sounds(k) || !c.contains(s.note(t, k) % 12))
			return "a treble string off the chord";
		if (s.note(t, k) <= s.note(t, bass))
			return "a treble note under the bass";
	}
	for (int i = 0; i < c.count; i++) {
		if (!c.essential[i])
			continue;
		bool present = false;
		for (int k = 0; k < t.strings; k++)
			if (s.sounds(k) && s.note(t, k) % 12 == c.pc[i])
				present = true;
		if (!present)
			return "an essential tone missing";
	}
	return "";
}

/** A PROGRESSION PLAYED AS THE MODULE PLAYS IT: a chord a bar in four, phrases of four bars, the
chances drawn from the seed. Every step's notes, and every bar's shape. */
struct Played {
	std::vector<std::vector<PickedNote>> steps;
	std::vector<Shape> shapes;
	std::vector<ChordNotes> chords;
};

static Played play(const std::vector<std::string>& bars, uint32_t seed, const PickVariation& v,
	int pattern = 0) {
	FingerPicker picker;
	picker.variation = v;
	picker.pattern = pattern;
	Played out;
	const int cMajor = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);
	for (size_t b = 0; b < bars.size(); b++) {
		const ChordNotes c = chordFrom(bars[b]);
		const bool change = b == 0 || bars[b] != bars[b - 1];
		if (change)
			picker.setChord(c, pickChance(seed, (int64_t) b * 8, 100));
		else
			picker.revoice(pickChance(seed, (int64_t) b * 8, 101));
		out.shapes.push_back(picker.shape());
		out.chords.push_back(c);
		for (int step = 0; step < 8; step++) {
			PickContext at;
			at.step = step;
			at.barBeats = 4;
			at.clock = (int64_t) b * 8 + step;
			at.seed = seed;
			at.chordStart = change && step == 0;
			const bool last = b + 1 >= bars.size();
			at.stepsToChange = last ? -1 : 8 - step;
			at.nextBass = last ? -1 : chordFrom(bars[b + 1]).bass;
			at.phraseStepsLeft = (int) (4 - b % 4) * 8 - step;
			at.motif = (uint32_t) (b / 4) + 1;
			// The next shape, a step before the change, as the module asks for it.
			Shape coming;
			if (at.stepsToChange == 1 && !last && bars[b + 1] != bars[b]
					&& picker.preview(chordFrom(bars[b + 1]), pickChance(seed, (int64_t) (b + 1) * 8, 100),
						&coming))
				at.nextShape = &coming;
			at.scaleMask = cMajor;
			PickedNote notes[MAX_STEP_NOTES];
			const int n = picker.notesAt(at, notes);
			out.steps.push_back(std::vector<PickedNote>(notes, notes + n));
		}
	}
	return out;
}

static bool sameNotes(const Played& a, const Played& b) {
	if (a.steps.size() != b.steps.size())
		return false;
	for (size_t i = 0; i < a.steps.size(); i++) {
		if (a.steps[i].size() != b.steps[i].size())
			return false;
		for (size_t k = 0; k < a.steps[i].size(); k++)
			if (a.steps[i][k].note != b.steps[i][k].note || a.steps[i][k].string != b.steps[i][k].string
					|| a.steps[i][k].level != b.steps[i][k].level
					|| a.steps[i][k].technique != b.steps[i][k].technique)
				return false;
	}
	return true;
}

int main() {
	Tuning t = Tuning::standard();
	Hand hand;
	hand.barreOverMuted = true;

	std::printf("The open chords, on the strings the pattern plays\n");
	struct Known { const char* name; const char* shape; };
	const Known open[] = {
		{"C", "x3x010"}, {"G", "3xx003"}, {"D", "xx0232"}, {"A", "x0x220"}, {"E", "0xx100"},
		{"Am", "x0x210"}, {"Em", "0xx000"}, {"Dm", "xx0231"}, {"A7", "x0x020"}, {"E7", "0xx130"},
	};
	for (const Known& k : open) {
		Shape s;
		const bool found = bestPickingShape(chordFrom(k.name), t, hand, 0, nullptr, &s);
		const std::string got = found ? s.written(t) : "none";
		check(got == k.shape, std::string(k.name) + " " + got
			+ (got == k.shape ? "" : std::string(", wanted ") + k.shape));
	}

	std::printf("\nEvery chord, every position: the requirements met\n");
	const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
		"Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm",
		"C7", "D7", "E7", "F7", "G7", "A7", "B7", "Bb7", "Cmaj7", "Fmaj7", "Gmaj7", "Dm7",
		"Em7", "Am7", "Bm7", "F#m7", "C/G", "D/F#", "G/B", "Am/G", "D/C", "C/E", "F/C"};
	int tried = 0, bad = 0, none = 0;
	std::string firstBad;
	for (const char* name : names)
		for (int near = 0; near <= 12; near += 2) {
			tried++;
			Shape s;
			const ChordNotes c = chordFrom(name);
			if (!bestPickingShape(c, t, hand, near, nullptr, &s)) {
				none++;
				if (firstBad.empty())
					firstBad = std::string(name) + " near " + std::to_string(near) + ": none";
				continue;
			}
			const std::string f = faults(s, c, t, hand);
			if (!f.empty()) {
				bad++;
				if (firstBad.empty())
					firstBad = std::string(name) + " " + s.written(t) + ": " + f;
			}
		}
	check(bad == 0 && none == 0, std::to_string(tried) + " tried, " + std::to_string(none)
		+ " with no shape, " + std::to_string(bad) + " breaking a rule"
		+ (firstBad.empty() ? "" : "; first " + firstBad));

	std::printf("\nUp the neck\n");
	{
		Shape s;
		bestPickingShape(chordFrom("C"), t, hand, 8, nullptr, &s);
		const Grip g = gripOf(s, t, hand);
		check(g.lowFret >= 7 && g.lowFret <= 9, "C near the eighth fret: " + s.written(t));
		bestPickingShape(chordFrom("D/F#"), t, hand, 0, nullptr, &s);
		check(s.fret[5] == 2, "D over F sharp puts the thumb on the low E's second fret: "
			+ s.written(t));
	}

	std::printf("\nA progression, C Am F G, a bar each, through every pattern\n");
	for (int pat = 0; pat < NUM_PICK_PATTERNS; pat++)
		for (int barBeats : {4, 3}) {
			const PickPattern& pp = PICK_PATTERNS[pat];
			const int steps = barBeats * 2;
			const uint8_t* form = barBeats == 3 ? pp.triple : pp.even;
			FingerPicker picker;
			picker.pattern = pat;
			const char* bars[] = {"C", "Am", "F", "G"};
			bool tonesOk = true, stringsOk = true, stepsOk = true, handOk = true;
			int changes = 0;
			Shape was;
			std::string shapes;
			for (int b = 0; b < 4; b++) {
				const ChordNotes c = chordFrom(bars[b]);
				picker.setChord(c);
				if (b > 0 && picker.shape().written(t) != was.written(t))
					changes++;
				was = picker.shape();
				if (!gripOf(was, t, picker.hand).playable)
					handOk = false;
				for (int step = 0; step < steps; step++) {
					PickedNote notes[4];
					const int n = picker.notesAt(step, barBeats, notes);
					uint8_t played = 0;
					for (int i = 0; i < n; i++) {
						const PickedNote& p = notes[i];
						played |= (uint8_t) (1 << p.finger);
						if (p.finger == THUMB) {
							if (p.note % 12 != c.bass || p.string != was.bassString(t))
								stringsOk = false;
						}
						else if (p.string != FINGER_STRING[p.finger])
							stringsOk = false;
						if (!c.contains(p.note % 12) && p.note % 12 != c.bass)
							tonesOk = false;
						if (p.note != t.open[p.string] + p.fret)
							stringsOk = false;
					}
					if (played != form[step % (barBeats == 3 ? pp.tripleSteps : pp.evenSteps)])
						stepsOk = false;
				}
			}
			std::printf("      %s in %d\n", pp.name, barBeats);
			check(tonesOk && stringsOk && stepsOk && handOk && changes == 3,
				"the fingers the pattern names, each on its string, the thumb on the bass, every"
				" note a chord tone, the hand within reach, the shape moving at each change");
		}

	std::printf("\nThe forward roll's own steps\n");
	{
		FingerPicker picker;
		picker.setChord(chordFrom("G"));
		auto fingers = [&](int barBeats) {
			std::string out;
			for (int step = 0; step < barBeats * 2; step++) {
				PickedNote notes[4];
				const int n = picker.notesAt(step, barBeats, notes);
				out += n == 1 ? "TIMR"[notes[0].finger] : '?';
			}
			return out;
		};
		check(fingers(4) == "TIMRTRMI", "in four, thumb, index, middle, ring, thumb, ring, middle,"
			" index: " + fingers(4));
		check(fingers(3) == "TIMRMI", "in three, thumb, index, middle, ring, middle, index: "
			+ fingers(3));
	}

	std::printf("\nThe dynamics\n");
	{
		FingerPicker picker;
		picker.setChord(chordFrom("C"));
		PickedNote a[4], b[4], c[4], d[4];
		picker.notesAt(0, 4, a);   // thumb, the first beat
		picker.notesAt(4, 4, b);   // thumb, the third beat
		picker.notesAt(1, 4, c);   // index
		check(a[0].level > b[0].level && b[0].level > c[0].level,
			"the first beat over the third, the thumb over the fingers: "
			+ std::to_string(a[0].level) + ", " + std::to_string(b[0].level) + ", "
			+ std::to_string(c[0].level));
		picker.pattern = 2;
		const int n = picker.notesAt(1, 4, d);
		check(n == 2 && d[0].level == d[1].level && d[0].string == 1 && d[1].string == 0,
			"a pinch is the first two strings at one level");
		bool loud = true;
		for (int pat = 0; pat < NUM_PICK_PATTERNS; pat++) {
			picker.pattern = pat;
			for (int step = 0; step < 8; step++) {
				PickedNote e[4];
				const int k = picker.notesAt(step, 4, e);
				for (int i = 0; i < k; i++)
					if (e[i].level <= 0.f || e[i].level > 1.f)
						loud = false;
			}
		}
		check(loud, "every level between nought and one");
	}


	std::printf("\nVariation\n");
	{
		std::vector<std::string> song;
		for (int pass = 0; pass < 8; pass++)
			for (const char* c : {"C", "G", "Am", "F"})
				song.push_back(c);
		const Tuning tt = Tuning::standard();

		PickVariation none;
		const Played plainA = play(song, 7, none), plainB = play(song, 99, none);
		check(sameNotes(plainA, plainB), "with every variation at nought, the seed changes nothing");
		bool sameC = true;
		for (size_t b = 4; b < song.size(); b += 4)
			if (plainA.shapes[b].written(tt) != plainA.shapes[0].written(tt))
				sameC = false;
		check(sameC, "and C is held the same way every time it returns");

		PickVariation all;
		all.voicing = all.picking = all.ornament = 1.f;
		const Played a = play(song, 7, all), again = play(song, 7, all), other = play(song, 8, all);
		check(sameNotes(a, again), "the same seed plays the same notes");
		check(!sameNotes(a, other), "another seed plays others");

		// VOICING: C held more than one way, every shape meeting every requirement.
		std::vector<std::string> cShapes;
		bool shapesOk = true;
		std::string firstBad;
		for (size_t b = 0; b < song.size(); b++) {
			const std::string f = faults(a.shapes[b], a.chords[b], tt, hand);
			if (!f.empty() && firstBad.empty())
				firstBad = song[b] + " " + a.shapes[b].written(tt) + ": " + f;
			if (!f.empty())
				shapesOk = false;
			if (song[b] == "C") {
				const std::string w = a.shapes[b].written(tt);
				bool seen = false;
				for (const std::string& x : cShapes)
					seen = seen || x == w;
				if (!seen)
					cShapes.push_back(w);
			}
		}
		std::string list;
		for (const std::string& x : cShapes)
			list += " " + x;
		check(cShapes.size() >= 2, "Voicing: C held " + std::to_string(cShapes.size())
			+ " ways in eight returns:" + list);
		check(shapesOk, "every shape meets every requirement" + (firstBad.empty() ? "" : ": " + firstBad));

		// PICKING AND ORNAMENT: what changed, and that it is all still the chord and the hand.
		int changed = 0, altBass = 0, hammers = 0, pulls = 0, runs = 0;
		bool thumbOnOne = true, tonesOk = true, ornamentsOk = true, runsOk = true;
		const int scale[] = {0, 2, 4, 5, 7, 9, 11};
		auto inScale = [&](int note) {
			for (int pc : scale)
				if (note % 12 == pc)
					return true;
			return false;
		};
		const Played written = play(song, 7, none);
		for (size_t i = 0; i < a.steps.size(); i++) {
			const size_t bar = i / 8;
			const int step = (int) (i % 8);
			const ChordNotes& c = a.chords[bar];
			const Shape& sh = a.shapes[bar];
			const std::vector<PickedNote>& notes = a.steps[i];
			bool same = notes.size() == written.steps[i].size();
			for (size_t k = 0; same && k < notes.size(); k++)
				same = notes[k].string == written.steps[i][k].string;
			if (!same)
				changed++;
			if (step == 0) {
				bool thumb = false;
				for (const PickedNote& p : notes)
					thumb = thumb || (p.finger == THUMB && p.note % 12 == c.bass);
				thumbOnOne = thumbOnOne && thumb;
			}
			const bool run = step >= 6 && bar + 1 < song.size() && notes.size() == 1
				&& notes[0].finger == THUMB && notes[0].note % 12 != c.bass;
			if (run) {
				runs++;
				const int next = chordFrom(song[bar + 1]).bass;
				const int gap = ((notes[0].note - next) % 12 + 12) % 12;
				if (!inScale(notes[0].note) || notes[0].string < BASS_STRING_FIRST
						|| (step == 7 && gap != 1 && gap != 2 && gap != 10 && gap != 11))
					runsOk = false;
				continue;
			}
			for (size_t k = 0; k < notes.size(); k++) {
				const PickedNote& p = notes[k];
				if (p.technique == PT_HAMMER_ON || p.technique == PT_PULL_OFF) {
					(p.technique == PT_HAMMER_ON ? hammers : pulls)++;
					// Into the note the shape holds, from a note of the scale on the same string.
					const PickedNote& from = notes[k - 1];
					if (p.fret != sh.fret[p.string] || from.string != p.string || !inScale(from.note)
							|| p.delay <= 0.f || from.delay != 0.f
							|| (p.technique == PT_HAMMER_ON) != (from.fret < p.fret))
						ornamentsOk = false;
					continue;
				}
				if (k + 1 < notes.size() && notes[k + 1].technique != 0)
					continue;
				if (p.finger == THUMB && p.string != sh.bassString(tt)) {
					altBass++;
					if (!c.contains(p.note % 12) || p.string < BASS_STRING_FIRST)
						tonesOk = false;
					continue;
				}
				// On the step before a change, a treble note may already be the next chord's.
				const bool early = step == 7 && bar + 1 < song.size()
					&& a.chords[bar + 1].contains(p.note % 12);
				if (!c.contains(p.note % 12) && p.note % 12 != c.bass && !early)
					tonesOk = false;
			}
		}
		std::printf("      %d of %d steps changed, %d alternate bass notes, %d hammer-ons, %d pull-offs,"
			" %d bass-run notes\n", changed, (int) a.steps.size(), altBass, hammers, pulls, runs);
		check(changed > 0 && altBass > 0 && hammers + pulls > 0 && runs > 0,
			"each kind of variation turns up");
		check(thumbOnOne, "the thumb plays the bass on the first beat of every bar");
		check(tonesOk, "every picked note a tone of its chord or, a step before a change, of the next, an alternate"
			" bass on a bass string");
		check(ornamentsOk, "every hammer-on and pull-off lands on the held note, from a scale note on"
			" its string, a step later");
		check(runsOk, "every run note in the scale, on a bass string, the last a step from the new bass");
	}

	std::printf("\nA figure for the phrase, through its chord changes\n");
	{
		std::vector<std::string> song;
		for (int pass = 0; pass < 4; pass++)
			for (const char* c : {"C", "Am", "F", "G"})
				song.push_back(c);
		PickVariation v;
		v.picking = 1.f;
		const Played p = play(song, 7, v);
		auto fingersAt = [&](size_t bar, int step) {
			int mask = 0;
			for (const PickedNote& n : p.steps[bar * 8 + step])
				mask |= 1 << n.finger;
			return mask;
		};
		bool repeats = true;
		int phrasesDiffer = 0;
		std::string figures;
		for (size_t phrase = 0; phrase < song.size() / 4; phrase++) {
			std::string figure;
			for (int step = 0; step < 8; step++) {
				const int first = fingersAt(phrase * 4, step);
				for (size_t bar = 1; bar < 3; bar++)
					if (fingersAt(phrase * 4 + bar, step) != first)
						repeats = false;
				figure += std::to_string(first) + (step < 7 ? "," : "");
			}
			if (phrase > 0 && figure != figures)
				phrasesDiffer++;
			figures = figure;
		}
		check(repeats, "the first three bars of each phrase play one figure, chord after chord");
		check(phrasesDiffer > 0, "and the phrases' figures are not all the same");
		bool fills = false;
		for (size_t phrase = 0; phrase < song.size() / 4; phrase++)
			for (int step = 0; step < 8; step++)
				if (fingersAt(phrase * 4 + 3, step) != fingersAt(phrase * 4, step))
					fills = true;
		check(fills, "the last bar of a phrase adds to it");
		// NO STRING PICKED TWICE IN A ROW that the pattern does not itself pick twice, in any
		// pattern, with Picking at the top: a pinch's added note is never the next step's note.
		bool apart = true;
		std::string where;
		for (int pat = 0; pat < NUM_PICK_PATTERNS; pat++) {
			PickVariation all;
			all.picking = 1.5f;
			const Played q = play(song, 7, all, pat);
			FingerPicker plain;
			plain.pattern = pat;
			plain.setChord(chordFrom("C"));
			for (size_t bar = 0; bar < song.size(); bar++)
				for (int step = 0; step + 1 < 8; step++) {
					PickedNote w0[MAX_STEP_NOTES], w1[MAX_STEP_NOTES];
					const int n0 = plain.notesAt(step, 4, w0), n1 = plain.notesAt(step + 1, 4, w1);
					for (const PickedNote& x : q.steps[bar * 8 + step])
						for (const PickedNote& y : q.steps[bar * 8 + step + 1]) {
							if (x.string != y.string || x.string >= BASS_STRING_FIRST)
								continue;
							bool written = false;
							for (int i = 0; i < n0; i++)
								for (int k = 0; k < n1; k++)
									written = written || (w0[i].finger == x.finger && w1[k].finger == y.finger);
							if (!written && apart) {
								apart = false;
								where = std::string(PICK_PATTERNS[pat].name) + ", bar " + std::to_string(bar + 1)
									+ ", step " + std::to_string(step + 1);
							}
						}
				}
		}
		check(apart, "no treble string picked on two steps running unless the pattern does so"
			+ (where.empty() ? std::string() : ": " + where));
	}

	std::printf("\nThe last eighth before a change\n");
	{
		std::vector<std::string> song;
		for (int pass = 0; pass < 4; pass++)
			for (const char* c : {"C", "Am", "F", "G"})
				song.push_back(c);
		for (float voicing : {0.f, 1.f}) {
			PickVariation v;
			v.voicing = voicing;
			const Played p = play(song, 11, v);
			bool early = true, moved = false;
			for (size_t b = 0; b + 1 < song.size(); b++)
				for (const PickedNote& n : p.steps[b * 8 + 7]) {
					if (n.finger == THUMB)
						continue;
					if (n.fret != p.shapes[b + 1].fret[n.string])
						early = false;
					if (n.fret != p.shapes[b].fret[n.string])
						moved = true;
				}
			check(early && moved, std::string("Voicing ") + (voicing > 0.f ? "at the top" : "at nought")
				+ ": every treble note on the step before a change is the next shape's, some moved early");
		}
	}

	std::printf("\nStyle, pop to jazz\n");
	{
		const Tuning tt = Tuning::standard();
		// The chords as a jazz player hears them: G is the dominant in C.
		const ChordNotes c7 = jazzTones(chordFrom("C"), 1.f, false), g7 = jazzTones(chordFrom("G"), 1.f, true);
		const ChordNotes am7 = jazzTones(chordFrom("Am"), 1.f, false), plain = jazzTones(chordFrom("C"), 0.2f, false);
		check(c7.contains(11) && c7.contains(2) && c7.isEssential(11) && !c7.contains(10),
			"C gains its major seventh, required, and its ninth");
		check(g7.contains(5) && g7.contains(9) && g7.contains(4), "G, the dominant, gains F, its ninth A and its thirteenth E");
		check(am7.contains(7) && am7.contains(11), "A minor gains G and its ninth B");
		check(plain.count == 3, "below a quarter of the way, C stays a triad");
		check(jazzTones(chordFrom("C7"), 1.f, false).contains(10)
			&& !jazzTones(chordFrom("C7"), 1.f, false).contains(11), "a written seventh is kept as written");

		std::vector<std::string> song;
		for (int pass = 0; pass < 2; pass++)
			for (const char* c : {"C", "Am", "F", "G"})
				song.push_back(c);
		auto measure = [&](float jazz, int* open, int* colour, int* doubled, float* fret, int* moved, int* bad) {
			FingerPicker picker;
			picker.variation.jazz = jazz;
			*open = *colour = *doubled = *moved = *bad = 0;
			*fret = 0.f;
			Shape was;
			for (size_t b = 0; b < song.size(); b++) {
				const ChordNotes c = jazzTones(chordFrom(song[b]), jazz, song[b] == "G");
				picker.setChord(c);
				const Shape& s = picker.shape();
				if (!faults(s, c, tt, picker.hand).empty())
					(*bad)++;
				const Grip g = gripOf(s, tt, picker.hand);
				*open += g.open;
				*fret += (float) g.lowFret / song.size();
				int seen = 0;
				for (int k = 0; k < tt.strings; k++) {
					if (!s.sounds(k))
						continue;
					const int pc = s.note(tt, k) % 12;
					if (seen & (1 << pc))
						(*doubled)++;
					seen |= 1 << pc;
					if (k < BASS_STRING_FIRST) {
						for (int i = 0; i < c.count; i++)
							if (c.pc[i] == pc && (c.degree[i] == 9 || c.degree[i] == 13 || c.degree[i] == 7))
								(*colour)++;
						if (b > 0)
							*moved += std::abs(s.note(tt, k) - was.note(tt, k));
					}
				}
				was = s;
			}
		};
		int o0, c0, d0, m0, b0, o1, c1, d1, m1, b1;
		float f0, f1;
		measure(0.f, &o0, &c0, &d0, &f0, &m0, &b0);
		measure(1.f, &o1, &c1, &d1, &f1, &m1, &b1);
		std::printf("      pop: %d open strings, %d sevenths and colours on top, %d doubled notes, lowest fret %.1f,"
			" treble moves %d semitones\n", o0, c0, d0, f0, m0);
		std::printf("      jazz: %d open strings, %d sevenths and colours on top, %d doubled notes, lowest fret %.1f,"
			" treble moves %d semitones\n", o1, c1, d1, f1, m1);
		check(b0 == 0 && b1 == 0, "every shape, pop and jazz, meets every requirement of its chord");
		check(o1 < o0 && c1 > c0 && d1 < d0 && f1 > f0, "toward jazz: fewer open strings, more colour on top,"
			" fewer doublings, higher on the neck");
		{
			FingerPicker plainPicker;
			plainPicker.setChord(chordFrom("C"));
			check(plainPicker.shape().written(tt) == "x3x010", "at nought, C is still x3x010");
		}
	}

	std::printf("\nPalm mute\n");
	{
		std::vector<std::string> song;
		for (int pass = 0; pass < 4; pass++)
			for (const char* c : {"C", "G", "Am", "F"})
				song.push_back(c);
		auto count = [&](float palm, int* thumbs, int* muted, int* mutedOnOne, int* fingersMuted) {
			PickVariation v;
			v.palm = palm;
			const Played p = play(song, 7, v);
			*thumbs = *muted = *mutedOnOne = *fingersMuted = 0;
			for (size_t i = 0; i < p.steps.size(); i++)
				for (const PickedNote& n : p.steps[i]) {
					const bool m = (n.technique & PT_PALM_MUTE) != 0;
					if (n.finger != THUMB) {
						*fingersMuted += m;
						continue;
					}
					(*thumbs)++;
					*muted += m;
					if (i % 8 == 0)
						*mutedOnOne += m;
				}
		};
		int t0, m0, o0, f0, th, mh, oh, fh, t1, m1, o1, f1;
		count(0.f, &t0, &m0, &o0, &f0);
		count(0.5f, &th, &mh, &oh, &fh);
		count(1.f, &t1, &m1, &o1, &f1);
		std::printf("      muted thumb notes: %d of %d at nought, %d of %d at a half, %d of %d at the top\n",
			m0, t0, mh, th, m1, t1);
		check(m0 == 0 && m1 == t1, "at nought none, at the top every thumb note");
		check(mh > 0 && mh < th && oh == 0, "at a half some, never on the first beat of the bar");
		check(f0 + fh + f1 == 0, "no finger's note is ever muted");
		// A FIGURE: the same steps muted in each of a phrase's bars.
		PickVariation v;
		v.palm = 0.5f;
		const Played p = play(song, 7, v);
		bool figure = true;
		for (size_t phrase = 0; phrase < song.size() / 4; phrase++)
			for (int step = 0; step < 8; step++) {
				bool first = false;
				for (const PickedNote& n : p.steps[phrase * 32 + step])
					first = first || (n.technique & PT_PALM_MUTE);
				for (size_t bar = 1; bar < 4; bar++) {
					bool here = false;
					for (const PickedNote& n : p.steps[(phrase * 4 + bar) * 8 + step])
						here = here || (n.technique & PT_PALM_MUTE);
					if (here != first)
						figure = false;
				}
			}
		check(figure, "the same steps muted in every bar of a phrase");
	}

	std::printf("\nAccent\n");
	{
		FingerPicker picker;
		picker.setChord(chordFrom("C"));
		auto spread = [&](float accent) {
			picker.variation.accent = accent;
			float lo = 1.f, hi = 0.f;
			for (int step = 0; step < 8; step++) {
				PickContext at;
				at.step = step;
				PickedNote n[MAX_STEP_NOTES];
				const int k = picker.notesAt(at, n);
				for (int i = 0; i < k; i++) {
					lo = std::min(lo, n[i].level);
					hi = std::max(hi, n[i].level);
				}
			}
			return hi - lo;
		};
		const float flat = spread(0.f), middle = spread(0.5f), full = spread(1.f);
		check(flat < 1e-6f && middle > 0.3f && full > middle, "at nought every note alike, then wider: "
			+ std::to_string(flat) + ", " + std::to_string(middle) + ", " + std::to_string(full));
	}

	std::printf("\n%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
