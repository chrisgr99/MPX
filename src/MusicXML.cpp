/** See MusicXML.hpp. */
#include "MusicXML.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace px {

namespace {


/** HOW LOUD EACH DYNAMIC MARK IS, nought to one, as the model counts a beat's loudness.

The same ladder Guitar Pro uses for its own marks, so a MusicXML score and a Guitar Pro one at the
same marking play at the same loudness. Anything not named here is left where the music was. */
struct Loudness {
	const char* name;
	float level;
};
/** THE EIGHT MARKINGS, EVENLY SPREAD FROM NOUGHT TO ONE.

EVENLY, BECAUSE LEVEL IS A DECIBEL SCALE. See MPX_RANGE_DB: a level is worth twenty-two times
(1 - level) decibels below full, so an even spacing here is an even spacing in loudness — one
seventh of the range, about three decibels, from each marking to the next. A marking names a
number of decibels, and it names the same number in every score.

WHAT THIS WAS, AND THE TWO THINGS WRONG WITH IT. The levels were .15 .25 .35 .5 .6 .75 .88 1,
which came from nobody in particular and were spaced evenly in nothing at all. Measured through
the piano they gave steps of 4.7, 6.1, 4.4, 4.0, 4.0, 4.6 — and then 0.9 from ff to fff, so the
two loudest markings in music were the same loudness and seven marks did the work of six. And the
whole span came to twenty-nine decibels, which is a piano in a quiet room and leaves pianissimo
close to inaudible in a rack.

MF AND FFF ARE WHERE THEY WERE, deliberately: the eighteen scores that carry no dynamics at all
sit at mf and should not move, and the top of an instrument should not move. Everything below mf
comes up, ff comes down by the two decibels that were never really there. */
const Loudness LOUDNESS[] = {
	{"ppp", 0.f},       {"pp", 1.f / 7.f},  {"p", 2.f / 7.f},   {"mp", 3.f / 7.f},
	{"mf", 4.f / 7.f},  {"f", 5.f / 7.f},   {"ff", 6.f / 7.f},  {"fff", 1.f},
	// THE ACCENTS, which are a marking and a blow rather than a level that holds: sf sits
	// between f and ff, sfz at ff, and fp strikes at f and drops to p straight after — the
	// drop is not read, so it is the f that is kept.
	{"sf", 5.5f / 7.f}, {"sfz", 6.f / 7.f}, {"fp", 5.f / 7.f},
};
const int LOUDNESS_COUNT = (int) (sizeof(LOUDNESS) / sizeof(LOUDNESS[0]));

/** A STEP AND AN ALTER AS A MIDI NOTE. C4, middle C, is sixty. */
int midiOf(const std::string& step, int alter, int octave) {
	static const char* STEPS = "CDEFGAB";
	static const int SEMITONES[7] = {0, 2, 4, 5, 7, 9, 11};
	const char* at = step.empty() ? NULL : std::strchr(STEPS, step[0]);
	const int which = at ? (int) (at - STEPS) : 0;
	return (octave + 1) * 12 + SEMITONES[which] + alter;
}


/** A POINT WHERE THE LOUDNESS IS SAID OR STARTS TO MOVE.

A dynamic mark is a step and a hairpin is a ramp, and a ramp's destination is not written on the
ramp — it is whatever mark comes after it, which may be bars away. So nothing can be decided while
the notes are being read: the marks are collected as they pass, and the loudness of every note is
worked out afterwards from the whole part at once. */
struct LevelEvent {
	double at = 0.0;            /**< Quarters from the start of the part. */
	/** SWELL and FADE open a hairpin, which STOP closes. SWELL_WORD and FADE_WORD are the same
	instruction written as a word — "cresc.", "calando" — and a word has no bracket to close it,
	so it runs to whatever is marked next. */
	enum Kind { MARK, SWELL, FADE, STOP, SWELL_WORD, FADE_WORD } kind = MARK;
	float value = 0.6f;         /**< For a mark. */
};


/** HOW FAR A HAIRPIN GOES WHEN NOTHING SAYS. A crescendo with no mark after it still has to
arrive somewhere; this is the distance it travels, which is one step of the ladder.

ONE STEP OF THE LADDER, which is now a seventh of it: the eight markings are evenly spread, so
this is exactly the distance from one to the next, about three decibels. A quarter — which this
was once, while claiming to be one step — is two dynamics at a stroke: every crescendo from f
slammed into the ceiling and stayed there, which put an eighth of the Ballade at full. */
const float HAIRPIN_REACH = 1.f / 7.f;


/** THE QUIETEST A RAMP MAY ARRIVE AT. ppp is the quietest thing written in music, and a ramp whose
destination nobody wrote is working from a guess: it may fade, but not to somewhere no composer
ever asks for. A mark that really says ppp is obeyed, since that is not a guess.

Nought, because ppp is the bottom of the ladder now that the markings are evenly spread. */
const float QUIETEST = 0.f;


/** HOW LONG AN ORNAMENT LASTS, in quarter notes.

A GRACE NOTE HAS NO DURATION IN THE FILE, because it takes none of the bar's time — the written
lengths of a bar must add up without it. Read literally it is a note of no length, which came out
as the shortest thing the cable can carry, a hundredth of a second: a click rather than a note.

It is also written at the same position as the note it ornaments, so the two shared a beat and the
main note was left with the grace's nought length as well.

SO IT IS GIVEN A LENGTH AND THE NOTE IT LEANS ON IS MOVED ALONG BY IT. A grace in this music is
played ON the beat, taking its moment from the note that follows rather than from the bar before —
which is how it was played when this was written, and it keeps the ornament from having to cross a
bar line to be heard. An eighth of a quarter is about seventy milliseconds at these tempos, which
is a short note rather than a click; it never takes more than half of what it leans on, so a grace
before a short note stays shorter than it. */
const float GRACE_QUARTERS = 0.125f;


/** HOW CLOSE A MARK HAS TO BE TO BE WHAT A HAIRPIN LEADS TO, in quarter notes. A dynamic written
under the end of a hairpin is its destination; one two bars further on belongs to something else,
and a hairpin that jumped to it would arrive long before the music does. */
const float HAIRPIN_LEADS = 4.f;


/** THE LOUDNESS OF THE WHOLE PART AS A LINE, built from the marks: a list of points with straight
lines between them. A step is two points at the same moment. */
std::vector<std::pair<double, float> > levelCurve(std::vector<LevelEvent> events) {
	// WHAT HAPPENS FIRST WHERE TWO THINGS HAPPEN AT ONCE. A hairpin leading to a dynamic puts its
	// stop and that dynamic at the same moment, which is the commonest marking in music; read
	// with the mark first, the music arrives at pp and the hairpin then takes another step DOWN
	// from it, inventing a level below the one written. So a stop is settled before a mark at the
	// same moment — it is the mark it was leading to — and a hairpin OPENS after one, since it
	// sets out from the dynamic just given.
	auto rank = [](const LevelEvent& e) {
		if (e.kind == LevelEvent::STOP)
			return 0;
		return (e.kind == LevelEvent::MARK) ? 1 : 2;
	};
	std::stable_sort(events.begin(), events.end(),
		[&rank](const LevelEvent& a, const LevelEvent& b) {
			if (a.at != b.at)
				return a.at < b.at;
			return rank(a) < rank(b);
		});

	// A BENCH FOR THIS, since what it decides is heard and never seen. Set MPX_LEVELS to a range
	// of quarters — "100 140" — and every mark and every point of the line in that range is
	// printed. Temporary only in the sense that nothing calls it unless asked.
	const char* watch = std::getenv("MPX_LEVELS");
	double from = 0.0, to = 0.0;
	if (watch)
		std::sscanf(watch, "%lf %lf", &from, &to);
	if (watch) {
		static const char* KINDS[] = {"mark", "swell", "fade", "stop", "swell-word", "fade-word"};
		for (size_t i = 0; i < events.size(); i++) {
			if (events[i].at < from || events[i].at > to)
				continue;
			std::fprintf(stderr, "  at %8.2f  %-11s %.2f\n", events[i].at,
				KINDS[(int) events[i].kind], events[i].value);
		}
	}

	std::vector<std::pair<double, float> > curve;
	float now = 0.6f;
	curve.push_back(std::make_pair(-1.0, now));

	for (size_t i = 0; i < events.size(); i++) {
		const LevelEvent& e = events[i];
		if (e.kind == LevelEvent::MARK) {
			curve.push_back(std::make_pair(e.at, now));     // Hold up to here,
			curve.push_back(std::make_pair(e.at, e.value)); // then step.
			now = e.value;
		}
		else if (e.kind == LevelEvent::SWELL || e.kind == LevelEvent::FADE) {
			curve.push_back(std::make_pair(e.at, now));     // The ramp starts from here.
		}
		else if (e.kind == LevelEvent::SWELL_WORD || e.kind == LevelEvent::FADE_WORD) {
			// A WORDED RAMP GOES WHERE THE NEXT DYNAMIC MARK SAYS, however far off that is, and
			// several in a row are ONE journey to it rather than a step each.
			//
			// WHY THIS IS NOT A STEP APIECE. The Ballade reaches bar 60 at p and is marked
			// calando, then smorz., then più dimin., and only at bar 68 does it say pp. Those are
			// not three instructions to drop a dynamic each — between them they are one long fade
			// from p to pp, which is a fifth quieter. Taken as a step each they take the music to
			// a seventh of where it should be, and bars 62 to 67 came out at the floor: the
			// composer asked for quiet and got all but silence. A written word says WHICH WAY the
			// loudness is going; the next mark is the only thing that says HOW FAR.
			const bool up = (e.kind == LevelEvent::SWELL_WORD);
			size_t last = i;
			size_t j = i + 1;
			double end = -1.0;
			float target = 0.f;
			bool toward = false;
			for (; j < events.size(); j++) {
				if (events[j].at < e.at)
					continue;
				if (events[j].kind == LevelEvent::MARK) {
					end = events[j].at;
					target = events[j].value;
					toward = true;
					break;
				}
				// ANOTHER WORD THE SAME WAY carries the one fade on; anything else — a hairpin, or
				// a word going the other way — ends it, since the music has been told something
				// new.
				const bool again = up ? (events[j].kind == LevelEvent::SWELL_WORD)
					: (events[j].kind == LevelEvent::FADE_WORD);
				if (!again)
					break;
				last = j;
			}
			// A WORD MEANS WHAT IT SAYS. Where the next mark is on the wrong side of where the
			// music already is — a cresc. whose next mark is the dynamic it is already at, which
			// happens when a hairpin has got there first — the mark cannot be the destination,
			// since a crescendo that arrives no louder is not one. The word wins and it travels
			// a step, as a ramp with no mark after it does.
			if (toward && (up ? (target <= now) : (target > now)))
				toward = false;
			if (!toward) {
				// NOTHING SAYS WHERE IT ARRIVES, so it travels about one step of the ladder, as a
				// hairpin with no mark after it does.
				end = (j < events.size()) ? events[j].at : e.at + 4.0;
				target = now + (up ? HAIRPIN_REACH : -HAIRPIN_REACH);
			}
			if (end <= e.at)
				end = e.at + 4.0;
			// NEVER QUIETER THAN THE QUIETEST THING ANYBODY WRITES. A ramp with no destination is
			// a guess, and a guess has no business inventing a dynamic below ppp.
			target = std::max(QUIETEST, std::min(1.f, target));
			curve.push_back(std::make_pair(e.at, now));
			curve.push_back(std::make_pair(end, target));
			now = target;
			// The words taken into this one journey are not read again.
			i = last;
		}
		else {
			// WHERE THE HAIRPIN ARRIVES.
			//
			// WHICH WAY IT WAS GOING, first, from the last one opened before this stop: nothing
			// else here means anything without it.
			bool up = true;
			for (size_t j = i; j > 0; j--) {
				if (events[j - 1].kind == LevelEvent::FADE) {
					up = false;
					break;
				}
				if (events[j - 1].kind == LevelEvent::SWELL)
					break;
			}

			// THE MARK IT LEADS TO, but only if it really does lead to it: the mark must be the
			// next thing written and must be close enough to be this hairpin's destination.
			//
			// TAKING THE NEXT MARK ANYWHERE AHEAD WAS WRONG, and wrong in a way that undid whole
			// passages. Bars 56 to 60 of the Ballade are a crescendo, then alternating
			// diminuendos and crescendos, and the next mark after all of them is one p four bars
			// later. Every hairpin therefore arrived at p: the crescendo in bar 56 took the music
			// DOWN from f to p, and the four bars after it did nothing whatever, because each was
			// already at the value it was aiming for. Five bars of written expression came out
			// flat and one came out backwards.
			float target = now;
			bool leads = false;
			for (size_t j = i + 1; j < events.size(); j++) {
				// NOT PAST A MARK AT THE SAME MOMENT. A hairpin leading to a dynamic puts the two
				// at the same place, so a look-ahead that skipped everything "not later than
				// here" skipped the one thing it was looking for — and every such hairpin, which
				// is most of them, fell back to a blind step instead of arriving where it was
				// written to arrive.
				if (events[j].at < e.at)
					continue;
				// A MARK SITTING UNDER THE END OF THE HAIRPIN is what it leads to. Anything else
				// in between means this hairpin is its own instruction and that mark belongs to
				// whatever comes after it.
				if (events[j].kind == LevelEvent::MARK && events[j].at - e.at <= HAIRPIN_LEADS) {
					target = events[j].value;
					leads = true;
				}
				break;
			}
			// AND A HAIRPIN MEANS WHAT IT SAYS. A crescendo that arrives no louder is not a
			// crescendo, whatever the mark after it says.
			// A MARK THAT RESTATES THE DYNAMIC IS STILL THE DESTINATION, going down. "pp, dim.,
			// pp" is a composer saying it is already quiet and to stay there, and a fade that
			// insisted on moving would invent a level below the one written. Going up there is
			// no such risk and a crescendo that arrives no louder is not one, so it moves.
			if (leads && (up ? (target <= now) : (target > now)))
				leads = false;
			if (!leads)
				target = now + (up ? HAIRPIN_REACH : -HAIRPIN_REACH);
			target = std::max(QUIETEST, std::min(1.f, target));
			curve.push_back(std::make_pair(e.at, target));
			now = target;
		}
	}
	if (watch) {
		std::fprintf(stderr, "  --- the line\n");
		for (size_t i = 0; i < curve.size(); i++) {
			if (curve[i].first < from || curve[i].first > to)
				continue;
			std::fprintf(stderr, "  at %8.2f  %.3f\n", curve[i].first, curve[i].second);
		}
	}
	// IN TIME ORDER, because a worded ramp puts its far end on the line before the marks that
	// come between are reached, and the line is read by walking it.
	std::stable_sort(curve.begin(), curve.end(),
		[](const std::pair<double, float>& a, const std::pair<double, float>& b) {
			return a.first < b.first;
		});
	return curve;
}


/** The loudness at a moment, read off the line. */
float levelAt(const std::vector<std::pair<double, float> >& curve, double at) {
	if (curve.empty())
		return 0.6f;
	if (at <= curve.front().first)
		return curve.front().second;
	// A MARK APPLIES TO THE NOTE IT IS WRITTEN ON. The line holds a step as two points at the
	// same moment — the level up to there, then the level from there — and stopping at the first
	// of them gave a note sitting exactly on a mark the dynamic BEFORE it. Marks are written on
	// beats and notes are played on beats, so that was most of the notes that carry a mark at
	// all: a note on a ff was played at the p before it. Walking past both points takes the
	// level from the second, which is what the mark says.
	for (size_t i = 1; i < curve.size(); i++) {
		if (at >= curve[i].first)
			continue;
		const double a = curve[i - 1].first, b = curve[i].first;
		if (b - a < 1e-9)
			return curve[i].second;
		const double how = (at - a) / (b - a);
		return curve[i - 1].second
			+ (float) how * (curve[i].second - curve[i - 1].second);
	}
	return curve.back().second;
}


/** WHAT IS BEING BUILT FOR ONE VOICE OF ONE BAR: the beats, each at a position, in the order the
voice reaches them. A chord is one beat with several notes. */
struct Building {
	std::map<long, size_t> at;    /**< Position in divisions to the beat holding it. */
	std::vector<GpBeat> beats;
	/** WHERE THE ORNAMENTS ARE, so the note each leans on knows how much of itself it has given
	up. One entry per grace note, at the position of the note it ornaments. */
	std::vector<long> graces;
};


/** A NOTE AS IT IS SPELLED IN A BAR: where it falls, its letter from C as nought, its octave, and
how far it is sharpened or flattened. An accidental holds for that letter in that octave from the
note it is on to the bar line, so these are what an ornament's neighbour is looked up in. */
struct Spelled {
	long at;
	int letter;
	int octave;
	int alter;
};

/** A NOTE CARRYING AN ORNAMENT, waiting for the end of its bar. Where it is, how it is spelled,
and any sharp, flat or natural printed on the ornament above it and below it. */
struct Ornamented {
	int voice;
	size_t beat;
	size_t note;
	Spelled spelled;
	int markAbove;
	int markBelow;
};

/** No accidental printed on the ornament. */
static const int NO_MARK = 99;

static int letterOf(const std::string& step) {
	static const char* STEPS = "CDEFGAB";
	const char* at = step.empty() ? NULL : std::strchr(STEPS, step[0]);
	return at ? (int) (at - STEPS) : 0;
}

/** WHAT THE KEY SIGNATURE DOES TO A LETTER: sharps go on in the order F C G D A E B, flats in
the reverse. */
static int keyAlter(int letter, int fifths) {
	static const int SHARPS[7] = {3, 0, 4, 1, 5, 2, 6};
	static const int FLATS[7] = {6, 2, 5, 1, 4, 0, 3};
	for (int i = 0; i < std::min(7, std::abs(fifths)); i++) {
		if ((fifths > 0 ? SHARPS[i] : FLATS[i]) == letter)
			return fifths > 0 ? 1 : -1;
	}
	return 0;
}

/** An accidental printed on an ornament, as MusicXML names it, or NO_MARK. */
static int alterOfMark(const std::string& said) {
	if (said == "sharp" || said == "natural-sharp") return 1;
	if (said == "flat" || said == "natural-flat") return -1;
	if (said == "natural") return 0;
	if (said == "double-sharp" || said == "sharp-sharp") return 2;
	if (said == "flat-flat") return -2;
	return NO_MARK;
}

static const int LETTER_SEMITONES[7] = {0, 2, 4, 5, 7, 9, 11};

/** The next letter name `way` of a written one, one up or one down, and the octave it is in. */
static void stepLetter(int letter, int octave, int way, int& toLetter, int& toOctave) {
	toLetter = letter + way;
	toOctave = octave;
	if (toLetter > 6) {
		toLetter = 0;
		toOctave++;
	}
	else if (toLetter < 0) {
		toLetter = 6;
		toOctave--;
	}
}

/** THE NOTE AN ORNAMENT GOES TO, WHERE THE BAR SETTLES IT: the next letter name `way` of the
written one, sharpened or flattened by what is printed on the ornament, or else by the last
accidental on that letter and octave earlier in the bar — the two things the page makes binding.
-1 where neither is there, and the music around it decides: see neighbourInContext.

AN ACCIDENTAL EARLIER IN THE BAR IS TAKEN ONLY WHERE IT MAKES A STEP. In the Tempest's first
movement a turn on G sharp follows an F natural in the same bar, and taking it put the turn's
lower note an augmented second down. The engraver's own spelling-out of it goes to F sharp, a
tone, which is what the music around it gives. A sign printed on the ornament is taken as it is. */
static int neighbourOf(const Spelled& note, int way, int mark, const std::vector<Spelled>& bar) {
	int letter, octave;
	stepLetter(note.letter, note.octave, way, letter, octave);
	const int natural = (octave + 1) * 12 + LETTER_SEMITONES[letter];
	if (mark != NO_MARK)
		return natural + mark;
	const int written = (note.octave + 1) * 12 + LETTER_SEMITONES[note.letter] + note.alter;
	int alter = NO_MARK;
	long latest = -1;
	for (const Spelled& s : bar) {
		if (s.letter == letter && s.octave == octave && s.at <= note.at && s.at >= latest) {
			latest = s.at;
			alter = s.alter;
		}
	}
	if (alter == NO_MARK)
		return -1;
	const int step = (natural + alter - written) * way;
	return (step == 1 || step == 2) ? natural + alter : -1;
}

/** THE NOTE AN ORNAMENT GOES TO, FROM THE MUSIC AROUND IT, where the bar did not settle it.

THE KEY SIGNATURE IS NOT ALWAYS THE KEY. A passage that modulates is often written with
accidentals rather than a new signature: Chopin's first Ballade keeps its two flats for the whole
piece, and the passage from bar 106 is in A major spelled out sharp by sharp. Its bar 119 has an
inverted mordent on F sharp with no G written before it in the bar, and read strictly the mordent
goes to G natural — which no pianist plays in A major, and which the engraver's own spelling-out
of it did not either. So the letter is spelled as the music around it spells it: whichever way it
is written most often from the bar before to the bar after. The key signature decides only where
that is a tie or the letter is not written at all.

IN THE SAME REGISTER, AND A STEP. Counted within an octave of the ornament, since a left hand's
leading notes two octaves down say nothing about the melody's neighbour — the Tempest's finale
has G sharps all through its bass under an inverted mordent on F, which goes to G. And only a
spelling that makes a semitone or a tone with the written note is taken: an ornament is a step,
and F to G sharp is an augmented second, which none is. */
static int neighbourInContext(int letter, int octave, int midi, int way, size_t bar, int fifths,
		const std::vector<std::vector<Spelled> >& bars) {
	int toLetter, toOctave;
	stepLetter(letter, octave, way, toLetter, toOctave);
	const int natural = (toOctave + 1) * 12 + LETTER_SEMITONES[toLetter];
	auto isStep = [&](int alter) {
		const int step = (natural + alter - midi) * way;
		return step == 1 || step == 2;
	};
	std::map<int, int> count;
	for (size_t b = (bar > 0 ? bar - 1 : 0); b <= bar + 1 && b < bars.size(); b++)
		for (const Spelled& s : bars[b])
			if (s.letter == toLetter && std::abs(s.octave - toOctave) <= 1 && isStep(s.alter))
				count[s.alter]++;
	const int key = keyAlter(toLetter, fifths);
	int alter = key, most = 0;
	for (std::map<int, int>::const_iterator it = count.begin(); it != count.end(); ++it) {
		if (it->second > most || (it->second == most && it->first == key)) {
			most = it->second;
			alter = it->first;
		}
	}
	// Nothing written to go by, and the key's own spelling not a step from a chromatic note:
	// the nearest spelling of the letter that is one.
	if (most == 0 && !isStep(alter)) {
		static const int TRY[4] = {0, -1, 1, 2};
		for (int t = 0; t < 4; t++)
			if (isStep(TRY[t])) {
				alter = TRY[t];
				break;
			}
	}
	return natural + alter;
}


/** THE JUMP MARKS, as MusicXML writes them and as the model names them.

MusicXML says what a direction SOUNDS like — a `sound` element with dacapo or dalsegno set — and
the words beside it say what is printed. The sound is the reliable half, so it is read first and
the words only where there is no sound element. */
std::string markFor(const pugi::xml_node& direction) {
	const pugi::xml_node sound = direction.child("sound");
	if (sound) {
		if (sound.attribute("segno"))
			return "Segno";
		if (sound.attribute("coda"))
			return "Coda";
		if (sound.attribute("fine"))
			return "Fine";
		if (sound.attribute("tocoda"))
			return "DaCoda";
		if (sound.attribute("dacapo"))
			return "DaCapo";
		if (sound.attribute("dalsegno"))
			return "DaSegno";
	}
	const pugi::xml_node type = direction.child("direction-type");
	if (type.child("segno"))
		return "Segno";
	if (type.child("coda"))
		return "Coda";
	std::string words = type.child_value("words");
	for (size_t i = 0; i < words.size(); i++)
		words[i] = (char) std::tolower((unsigned char) words[i]);
	if (words.find("fine") != std::string::npos)
		return "Fine";
	if (words.find("to coda") != std::string::npos)
		return "DaCoda";
	if (words.find("d.c.") != std::string::npos || words.find("da capo") != std::string::npos)
		return "DaCapo";
	if (words.find("d.s.") != std::string::npos || words.find("dal segno") != std::string::npos)
		return "DaSegno";
	return "";
}


/** Which passes a first or second time bar is played on: "1", or "1,2", or "1, 2". */
std::vector<int> endingsOf(const std::string& numbers) {
	std::vector<int> out;
	std::string digits;
	for (size_t i = 0; i <= numbers.size(); i++) {
		const char c = (i < numbers.size()) ? numbers[i] : ',';
		if (c >= '0' && c <= '9') {
			digits += c;
			continue;
		}
		if (!digits.empty()) {
			out.push_back(std::atoi(digits.c_str()));
			digits.clear();
		}
	}
	return out;
}


} // namespace


bool mxLooksLikeMusicXML(const std::string& head) {
	if (head.size() > 4 && head[0] == 'P' && head[1] == 'K')
		return true;     // A zip: a .mxl, or something that is not ours and will fail later.
	return head.find("score-partwise") != std::string::npos
		|| head.find("score-timewise") != std::string::npos
		|| head.find("<!DOCTYPE score") != std::string::npos;
}


bool mxRead(const std::string& xml, GpSong& out, std::string* why) {
	pugi::xml_document doc;
	if (!doc.load_buffer(xml.data(), xml.size())) {
		if (why)
			*why = "that file is not XML this can read";
		return false;
	}
	const pugi::xml_node root = doc.child("score-partwise");
	if (!root) {
		if (why) {
			*why = doc.child("score-timewise")
				? "this is timewise MusicXML, which this does not read"
				: "that file is not MusicXML";
		}
		return false;
	}

	out = GpSong();
	out.title = root.child("work").child_value("work-title");
	if (out.title.empty())
		out.title = root.child("movement-title").child_value();
	for (pugi::xml_node c = root.child("identification").child("creator"); c;
			c = c.next_sibling("creator")) {
		const std::string what = c.attribute("type").as_string();
		if (what == "composer")
			out.artist = c.child_value();
		else if (what == "lyricist")
			out.words = c.child_value();
	}
	out.copyright = root.child("identification").child_value("rights");

	// WHAT THE SCORE SAYS ABOUT WHO WROTE IT, every field that might, for the ornament style. The
	// file's own name is added by whoever knows it. See OrnamentStyle.hpp.
	{
		auto clue = [&](const std::string& text, int kind) {
			if (!text.empty())
				out.clues.push_back(StyleClue{text, kind});
		};
		clue(root.child("work").child_value("work-title"), CLUE_TITLE);
		clue(root.child("work").child_value("work-number"), CLUE_TITLE);
		clue(root.child_value("movement-title"), CLUE_TITLE);
		clue(root.child_value("movement-number"), CLUE_TITLE);
		for (pugi::xml_node c = root.child("identification").child("creator"); c;
				c = c.next_sibling("creator")) {
			const std::string what = c.attribute("type").as_string();
			if (what == "composer")
				clue(c.child_value(), CLUE_COMPOSER);
			else if (what == "lyricist" || what == "poet")
				clue(c.child_value(), CLUE_LYRICIST);
			else if (what.empty())
				clue(c.child_value(), CLUE_CREATOR);
		}
		for (pugi::xml_node r = root.child("identification").child("rights"); r;
				r = r.next_sibling("rights"))
			clue(r.child_value(), CLUE_RIGHTS);
		// A CREDIT'S WORDS ARE ONE TEXT, joined as written: each carries its own spaces, and the
		// Entertainer file spreads one dedication over fifty-seven of them.
		std::set<std::string> seen;
		for (pugi::xml_node cr = root.child("credit"); cr; cr = cr.next_sibling("credit")) {
			int kind = CLUE_CREDIT;
			bool skip = false;
			for (pugi::xml_node t = cr.child("credit-type"); t; t = t.next_sibling("credit-type")) {
				const std::string what = t.child_value();
				if (what == "composer")
					kind = CLUE_COMPOSER;
				else if ((what == "lyricist" || what == "poet") && kind != CLUE_COMPOSER)
					kind = CLUE_LYRICIST;
				else if (what == "rights" && kind == CLUE_CREDIT)
					kind = CLUE_RIGHTS;
				else if (what == "arranger" || what == "page number" || what == "part name")
					skip = true;
			}
			if (skip && kind == CLUE_CREDIT)
				continue;
			std::string text;
			for (pugi::xml_node w = cr.child("credit-words"); w; w = w.next_sibling("credit-words"))
				text += w.child_value();
			// Once each: one file repeats its copyright line in ninety-five credits.
			if (seen.insert(std::to_string(kind) + text).second)
				clue(text, kind);
		}
		// HOW MANY CHORD SYMBOLS, which marks a lead sheet where no composer is named.
		for (pugi::xml_node part = root.child("part"); part; part = part.next_sibling("part"))
			for (pugi::xml_node m = part.child("measure"); m; m = m.next_sibling("measure"))
				for (pugi::xml_node h = m.child("harmony"); h; h = h.next_sibling("harmony"))
					out.chordSymbols++;
		out.ornamentStyle = guessStyle(out.clues, out.chordSymbols).style;
	}

	// ---- the instruments ------------------------------------------------------------------------
	std::map<std::string, size_t> trackOf;
	for (pugi::xml_node p = root.child("part-list").child("score-part"); p;
			p = p.next_sibling("score-part")) {
		GpTrack track;
		track.name = p.child_value("part-name");
		track.shortName = p.child_value("part-abbreviation");
		const pugi::xml_node midi = p.child("midi-instrument");
		if (midi) {
			// MIDI COUNTS ITS INSTRUMENTS FROM ONE in a file and from nought in the model.
			const int program = midi.child("midi-program").text().as_int(1);
			track.midiProgram = std::max(0, std::min(127, program - 1));
			const int channel = midi.child("midi-channel").text().as_int(1);
			track.midiChannel = std::max(0, channel - 1);
			// CHANNEL TEN IS THE DRUMS, which is the one thing MIDI says about an instrument
			// without naming it.
			track.percussion = (channel == 10);
		}
		if (track.name.empty())
			track.name = p.attribute("id").as_string();
		trackOf[p.attribute("id").as_string()] = out.tracks.size();
		out.tracks.push_back(track);
	}
	if (out.tracks.empty()) {
		if (why)
			*why = "this score names no instruments";
		return false;
	}

	// ---- the bars -------------------------------------------------------------------------------
	//
	// WHAT A BAR SAYS ABOUT TIME AND STRUCTURE IS THE SONG'S, NOT A PART'S. MusicXML writes the
	// key, the metre and the repeats into every part that is in force for; the model keeps one
	// copy. So the first part to say something is believed and the rest are read for their notes.
	bool first = true;
	for (pugi::xml_node part = root.child("part"); part; part = part.next_sibling("part")) {
		const std::map<std::string, size_t>::const_iterator which =
			trackOf.find(part.attribute("id").as_string());
		if (which == trackOf.end())
			continue;
		GpTrack& track = out.tracks[which->second];

		int divisions = 480;
		int barNumber = 0;
		// WHERE EVERY BAR BEGINS, in quarters from the start of the part, so a mark written in
		// bar forty can be placed on the same line as a note in bar one.
		std::vector<double> barStarts;
		double barStart = 0.0;
		std::vector<LevelEvent> levels;
		// WHETHER A SLUR IS OPEN, one per voice: a slur is a span and the notes inside it are
		// joined, so each voice has to remember whether it is in one.
		std::map<int, bool> inSlur;
		// WHETHER THE OPEN PEDAL BRACKET IS THE MIDDLE PEDAL'S, since the mark that closes it
		// does not say which pedal it releases.
		bool sostenutoOpen = false;
		// EVERY BAR'S SPELLINGS, for the ornaments the bar itself does not settle. See
		// neighbourInContext. And this part's own key in each bar: a clarinet and a piano in one
		// file are in different keys, and the first part's is only the first part's.
		std::vector<std::vector<Spelled> > spelledByBar;
		std::vector<int> fifthsByBar;
		int partFifths = 0;
		// THE NOTE EACH VOICE LAST TIED ONWARD, as played and as spelled; a pitch of -1 for none.
		// A hidden note a printed note is tied to is that printed note held: see below.
		std::map<int, std::pair<int, Spelled> > tiedOn;

		for (pugi::xml_node measure = part.child("measure"); measure;
				measure = measure.next_sibling("measure"), barNumber++) {
			if ((int) track.bars.size() <= barNumber)
				track.bars.resize((size_t) barNumber + 1);
			if (first && (int) out.masterBars.size() <= barNumber)
				out.masterBars.resize((size_t) barNumber + 1);
			GpBar& bar = track.bars[(size_t) barNumber];
			barStarts.push_back(barStart);
			// WHAT THE PAGE CALLS THIS BAR, and whether the writer called it a pickup. A pickup
			// is numbered nought, so a bar's number is not its position and anything showing a
			// bar must use the number. See GpMasterBar::number.
			const bool implicit =
				std::string(measure.attribute("implicit").as_string()) == "yes";
			GpMasterBar* master = (barNumber < (int) out.masterBars.size())
				? &out.masterBars[(size_t) barNumber] : NULL;
			if (master && barNumber > 0 && first) {
				// WHAT IS IN FORCE CARRIES ON. MusicXML writes a key or a metre only where it
				// changes; the model holds one on every bar.
				*master = out.masterBars[(size_t) barNumber - 1];
				master->repeatOpen = false;
				master->repeatCount = 0;
				master->endings.clear();
				master->marks.clear();
				master->section.clear();
				master->sectionLetter.clear();
				master->doubleBar = false;
				// WHAT IS THIS BAR'S ALONE does not carry on: the number the page gives it, and
				// its length where that is not the metre's. Inherited, a pickup made every bar
				// after it a pickup too, and numbered them all nought.
				master->number = -1;
				master->quarters = 0.f;
			}
			if (master && first) {
				const char* said = measure.attribute("number").as_string();
				if (said && said[0])
					master->number = std::atoi(said);
			}

			// One list of beats per voice, each keyed by where in the bar it falls.
			std::map<int, Building> voices;
			long cursor = 0;      // Divisions from the start of the bar.
			long filled = 0;      // The furthest any voice reached, which is what the bar holds.
			long lastNoteAt = 0;
			int lastVoice = 1;
			/** The key the last note was filed under, so a chord note joins the same beat even
			where that key is not the position — which an ornament's is not. */
			long lastKey = 0;
			/** Every note's spelling in this bar, and the notes with an ornament on them, whose
			neighbours are worked out once the bar is read: the voices come one after another, so
			an accidental earlier in time can come later in the file. See neighbourOf. */
			std::vector<Spelled> spelled;
			std::vector<Ornamented> ornamented;
			/** Set when a hidden note that would have headed a chord is passed over, so the next
			printed note written as a chord on it heads its own instead of joining the beat before. */
			bool headSkipped = false;

			for (pugi::xml_node node = measure.first_child(); node;
					node = node.next_sibling()) {
				const std::string tag = node.name();

				if (tag == "attributes") {
					if (node.child("divisions"))
						divisions = std::max(1, node.child("divisions").text().as_int(480));
					if (node.child("key"))
						partFifths = node.child("key").child("fifths").text().as_int(0);
					if (master && first) {
						const pugi::xml_node key = node.child("key");
						if (key) {
							master->keyAccidentals = key.child("fifths").text().as_int(0);
							master->minor =
								(std::string(key.child_value("mode")) == "minor");
						}
						const pugi::xml_node time = node.child("time");
						if (time) {
							master->beatsPerBar =
								std::max(1, time.child("beats").text().as_int(4));
							master->beatUnit =
								std::max(1, time.child("beat-type").text().as_int(4));
						}
					}
					continue;
				}

				if (tag == "backup") {
					cursor -= node.child("duration").text().as_int(0);
					if (cursor < 0)
						cursor = 0;
					continue;
				}
				if (tag == "forward") {
					cursor += node.child("duration").text().as_int(0);
					continue;
				}

				if (tag == "direction") {
					if (master && first) {
						const std::string mark = markFor(node);
						if (!mark.empty())
							master->marks.push_back(mark);
						// A REHEARSAL MARK IS A SECTION'S NAME, which is what the chart shows.
						const std::string rehearsal =
							node.child("direction-type").child_value("rehearsal");
						if (!rehearsal.empty())
							master->section = rehearsal;
					}
					// THE TEMPO, where it carries a number. A written one — ritardando, a tempo —
					// says nothing this can act on and is passed over.
					const pugi::xml_node sound = node.child("sound");
					if (sound && sound.attribute("tempo") && first) {
						GpTempo tempo;
						tempo.bar = barNumber;
						tempo.position = (float) cursor / (float) divisions;
						tempo.bpm = sound.attribute("tempo").as_float(120.f);
						tempo.unit = 2;
						out.tempos.push_back(tempo);
					}
					// HOW LOUD FROM HERE ON. A dynamic mark is a point, not a property of a
					// note: it holds until the next one.
					const double here = barStart + (double) cursor / (double) divisions;
					const pugi::xml_node dyn = node.child("direction-type").child("dynamics");
					if (dyn) {
						for (pugi::xml_node d = dyn.first_child(); d; d = d.next_sibling()) {
							for (int i = 0; i < LOUDNESS_COUNT; i++) {
								if (std::string(d.name()) == LOUDNESS[i].name) {
									LevelEvent e;
									e.at = here;
									e.kind = LevelEvent::MARK;
									e.value = LOUDNESS[i].level;
									levels.push_back(e);
								}
							}
						}
					}
					// A HAIRPIN: where it opens, and where it stops. What it arrives at is not
					// written here and is worked out later — see levelCurve.
					const pugi::xml_node wedge = node.child("direction-type").child("wedge");
					if (wedge) {
						const std::string kind = wedge.attribute("type").as_string();
						LevelEvent e;
						e.at = here;
						if (kind == "crescendo")
							e.kind = LevelEvent::SWELL;
						else if (kind == "diminuendo")
							e.kind = LevelEvent::FADE;
						else
							e.kind = LevelEvent::STOP;
						levels.push_back(e);
					}
					// A CRESCENDO WRITTEN AS A WORD. Not every one is a hairpin: "cresc." and
					// "calando" are the same instruction in letters, and this score carries
					// fifteen of them that no wedge covers. Only the words that say which way
					// the loudness goes are read — "dolce" and "agitato" say how to play, not
					// how loud, and guessing a level for them would be inventing one.
					{
						std::string w = node.child("direction-type").child_value("words");
						for (size_t i = 0; i < w.size(); i++)
							w[i] = (char) std::tolower((unsigned char) w[i]);
						const bool up = w.find("cresc") != std::string::npos;
						const bool down = w.find("dim") != std::string::npos
							|| w.find("decresc") != std::string::npos
							|| w.find("calando") != std::string::npos
							|| w.find("smorz") != std::string::npos
							|| w.find("morendo") != std::string::npos
							|| w.find("perdendo") != std::string::npos;
						if (up || down) {
							LevelEvent e;
							e.at = here;
							e.kind = up ? LevelEvent::SWELL_WORD : LevelEvent::FADE_WORD;
							levels.push_back(e);
						}
						// AND A DYNAMIC WRITTEN AS A WORD, which is a level and not a manner.
						//
						// "PIANO E DOLCE" IS A DYNAMIC. The Appassionata's slow movement opens
						// with it and carries no dynamic element at all until bar 15, where a p
						// appears — so the whole opening played at the default mezzo-forte and
						// then dropped six decibels at a mark that was only restating what had
						// been true since the first bar. It is written in letters because that
						// is how the composer wrote it, and a reader that only takes the symbol
						// hears the piece wrong for fourteen bars.
						//
						// BY WHOLE WORDS AND NEVER BY SUBSTRING. "Allegro ma non troppo" and
						// "appassionato" both contain the letters pp, and both are in the
						// scores here: matching inside a word would make a pianissimo of a
						// tempo marking. Split on letters and compare whole tokens.
						//
						// "PIU" MAKES IT A DIRECTION RATHER THAN A PLACE. "sempre forte" is a
						// level and stays; "sempre piu forte" is an instruction to go on
						// getting louder, which is a crescendo in words and is read as one.
						//
						// WHAT IS LEFT OUT, deliberately: "sotto voce" and "mezza voce" say how
						// to play and not how loud — veiled, under the breath — and the nearest
						// level is a guess. "dolce" and "agitato" likewise. A manner is not a
						// dynamic even when it implies one.
						if (!up && !down) {
							std::vector<std::string> token;
							std::string one;
							for (size_t i = 0; i <= w.size(); i++) {
								const bool letter = i < w.size()
									&& std::isalpha((unsigned char) w[i]);
								if (letter)
									one += w[i];
								else if (!one.empty()) {
									token.push_back(one);
									one.clear();
								}
							}
							const bool more = w.find("piÃ¹") != std::string::npos
								|| w.find("piu") != std::string::npos;
							static const struct { const char* word; const char* mark; } SAID[] = {
								{"fff", "fff"}, {"ff", "ff"}, {"f", "f"},
								{"ppp", "ppp"}, {"pp", "pp"}, {"p", "p"},
								{"mf", "mf"}, {"mp", "mp"},
								{"fortissimo", "ff"}, {"forte", "f"},
								{"pianissimo", "pp"}, {"piano", "p"},
							};
							const char* said = NULL;
							for (size_t t = 0; t < token.size() && !said; t++) {
								// "mezzo forte" and "mezzo piano" are two words and one level.
								if (token[t] == "mezzo" && t + 1 < token.size()) {
									if (token[t + 1] == "forte") { said = "mf"; break; }
									if (token[t + 1] == "piano") { said = "mp"; break; }
								}
								for (size_t k = 0; k < sizeof(SAID) / sizeof(SAID[0]); k++)
									if (token[t] == SAID[k].word) {
										said = SAID[k].mark;
										break;
									}
							}
							if (said) {
								LevelEvent e;
								e.at = here;
								if (more) {
									// "piu forte" is a crescendo and "piu pp" a diminuendo:
									// which way is told by the level that was named.
									e.kind = (said[0] == 'f') ? LevelEvent::SWELL_WORD
										: LevelEvent::FADE_WORD;
								}
								else {
									e.kind = LevelEvent::MARK;
									for (int i = 0; i < LOUDNESS_COUNT; i++)
										if (std::string(said) == LOUDNESS[i].name)
											e.value = LOUDNESS[i].level;
								}
								levels.push_back(e);
							}
						}
					}
					// THE SUSTAIN PEDAL. Written as a bracket — a start and a stop — and kept as
					// the two moves of the foot that the bracket stands for. `continue` and
					// `sostenuto` are the two the standard allows that are not moves: the first
					// says a bracket carries on past a line break and is nothing to act on, and
					// the second is the middle pedal, kept apart so a player can tell them.
					// Read from the first part only, like the metre: the pedal is the piano's.
					const pugi::xml_node ped = node.child("direction-type").child("pedal");
					if (ped && first) {
						const std::string kind = ped.attribute("type").as_string();
						GpPedalMark m;
						m.bar = barNumber;
						m.start = (float) cursor / (float) divisions;
						bool move = true;
						if (kind == "start") {
							m.kind = GpPedalMark::DOWN;
							sostenutoOpen = false;
						} else if (kind == "sostenuto") {
							m.kind = GpPedalMark::DOWN;
							m.sostenuto = true;
							sostenutoOpen = true;
						} else if (kind == "stop") {
							// A STOP SAYS NOTHING ABOUT WHICH FOOT, so it releases whichever
							// pedal the bracket it closes took.
							m.kind = GpPedalMark::UP;
							m.sostenuto = sostenutoOpen;
							sostenutoOpen = false;
						} else if (kind == "change")
							m.kind = GpPedalMark::CHANGE;
						else
							move = false;   // "continue", and anything a writer invents.
						if (move)
							out.pedals.push_back(m);
					}
					continue;
				}

				if (tag == "barline") {
					if (!master || !first)
						continue;
					const pugi::xml_node repeat = node.child("repeat");
					if (repeat) {
						const std::string way = repeat.attribute("direction").as_string();
						if (way == "forward")
							master->repeatOpen = true;
						else if (way == "backward")
							master->repeatCount = std::max(2, repeat.attribute("times").as_int(2));
					}
					const pugi::xml_node ending = node.child("ending");
					if (ending) {
						const std::string kind = ending.attribute("type").as_string();
						if (kind == "start")
							master->endings = endingsOf(ending.attribute("number").as_string());
					}
					if (std::string(node.child_value("bar-style")) == "light-light")
						master->doubleBar = true;
					continue;
				}

				if (tag != "note")
					continue;

				// ---- a note ----------------------------------------------------------------
				bool isChord = (bool) node.child("chord");
				const bool isGrace = (bool) node.child("grace");
				// NOT ON THE PAGE, NOT PLAYED. A note marked not to print, or printed with no head,
				// is something an engraver wrote for another program's playback: a copy of a printed
				// note starting a little late to sound after a rolled chord, or an ornament spelled
				// out the way that program could not manage itself. What is played is what a pianist
				// reads, and the signs are interpreted here and in the timeline from what they mean.
				// In Chopin's first Ballade every one of its eighty-five hidden notes is a copy of a
				// printed note or the notes of a printed ornament.
				//
				// IT STILL TAKES ITS TIME, read as a rest, so the voice it is in keeps its place and
				// a printed note written as a chord on it still finds its beat. A hidden chord note
				// or grace takes no time, and is simply passed over.
				//
				// ITS SLURS STILL COUNT. A slur drawn from a hidden note to a printed one is drawn
				// over the printed notes between, and they are joined; one ending on a hidden note
				// ends there.
				//
				// AND A PRINTED NOTE TIED INTO ONE IS HELD. A tie says the note goes on sounding, and
				// what the reader sees is the printed note tied onward; the hidden note's own pitch
				// is the other program's business. The viola in bar 53 of Fauré's first quartet,
				// third movement, ties an E flat into a hidden D, and read as a rest it left a gap.
				const int voice = std::max(1, node.child("voice").text().as_int(1));
				bool unprinted = std::string(node.attribute("print-object").as_string()) == "no"
					|| std::string(node.child_value("notehead")) == "none";
				bool tieStops = false;
				for (pugi::xml_node tie = node.child("tie"); tie; tie = tie.next_sibling("tie"))
					if (std::string(tie.attribute("type").as_string()) == "stop")
						tieStops = true;
				int heldMidi = -1;
				Spelled heldSpelling = {0, -1, 4, 0};
				if (unprinted && tieStops && !isChord && !isGrace) {
					const std::map<int, std::pair<int, Spelled> >::const_iterator t = tiedOn.find(voice);
					if (t != tiedOn.end() && t->second.first > 0) {
						heldMidi = t->second.first;
						heldSpelling = t->second.second;
						unprinted = false;
					}
				}
				if (unprinted) {
					const int slurVoice = isChord ? lastVoice : voice;
					for (pugi::xml_node sl = node.child("notations").child("slur"); sl;
							sl = sl.next_sibling("slur")) {
						const std::string kind = sl.attribute("type").as_string();
						if (kind == "start")
							inSlur[slurVoice] = true;
						else if (kind == "stop")
							inSlur[slurVoice] = false;
					}
				}
				if (unprinted && (isChord || isGrace)) {
					if (!isChord)
						headSkipped = true;
					continue;
				}
				// A printed note written as a chord on a hidden grace that was passed over.
				if (isChord && headSkipped)
					isChord = false;
				headSkipped = false;
				const bool isRest = (bool) node.child("rest") || unprinted;
				const long duration = (long) node.child("duration").text().as_llong(0);

				// A CHORD HANGS OFF THE NOTE BEFORE IT, at the same moment and in the same voice.
				const long where = isChord ? lastNoteAt : cursor;
				const int useVoice = isChord ? lastVoice : voice;

				Building& building = voices[useVoice];
				// AN ORNAMENT IS NOT PART OF THE NOTE IT ORNAMENTS. It is written at the same
				// position, so a map keyed by position would hand it the same beat; it is keyed
				// apart, below nought where no real position can reach. See GRACE_QUARTERS.
				const long key = isChord ? lastKey
					: (isGrace ? -(where + 1) - (long) building.graces.size() * 1000000L : where);
				std::map<long, size_t>::iterator found = building.at.find(key);
				size_t index;
				if (found == building.at.end()) {
					GpBeat beat;
					beat.start = (float) where / (float) divisions;
					beat.quarters = (float) duration / (float) divisions;
					beat.rest = isRest;
					beat.dynamic = 0.6f;   // Replaced below, from the whole part's line.
					beat.grace = isGrace ? 2 : 0;
					if (isGrace) {
						beat.quarters = GRACE_QUARTERS;
						// ORNAMENTS BEFORE ONE NOTE FOLLOW EACH OTHER. They are all written at
						// the position of the note they lean on, so taking that position
						// literally sounded three of them at one instant — a faint blur where a
						// run of three quick notes belongs. Each begins where the one before it
						// ended.
						int already = 0;
						for (size_t gi = 0; gi < building.graces.size(); gi++) {
							if (building.graces[gi] == where)
								already++;
						}
						beat.start += (float) already * GRACE_QUARTERS;
						building.graces.push_back(where);
					}
					else {
						// HOW MUCH OF THIS NOTE THE ORNAMENTS BEFORE IT HAVE TAKEN. They are
						// played on the beat, so this one begins after them and is shorter by
						// what they took — never by more than half of it.
						float taken = 0.f;
						for (size_t gi = 0; gi < building.graces.size(); gi++) {
							if (building.graces[gi] == where)
								taken += GRACE_QUARTERS;
						}
						if (taken > 0.f) {
							const float most = beat.quarters * 0.5f;
							if (beat.quarters > 0.f && taken > most)
								taken = most;
							beat.start += taken;
							beat.quarters -= taken;
						}
					}
					index = building.beats.size();
					building.beats.push_back(beat);
					building.at[key] = index;
					lastKey = key;
				}
				else {
					index = found->second;
					lastKey = key;
				}
				GpBeat& beat = building.beats[index];

				if (!isRest) {
					beat.rest = false;
					GpNote note;
					const pugi::xml_node pitch = node.child("pitch");
					const pugi::xml_node unpitched = node.child("unpitched");
					if (heldMidi > 0)
						note.midi = heldMidi;
					else if (pitch) {
						note.midi = midiOf(pitch.child_value("step"),
							pitch.child("alter").text().as_int(0),
							pitch.child("octave").text().as_int(4));
					}
					else if (unpitched) {
						note.midi = midiOf(unpitched.child_value("display-step"), 0,
							unpitched.child("display-octave").text().as_int(4));
					}
					// AS IT IS SPELLED, for the neighbours of any ornament in the bar. Positions are
					// doubled so a grace, written at the position of the note it leans on, can sit
					// just before it: an accidental on that note is not earlier than the grace.
					Spelled spelling = {where * 2 - (isGrace ? 1 : 0), -1, 4, 0};
					if (heldMidi > 0) {
						spelling = heldSpelling;
						note.letter = (int8_t) spelling.letter;
						note.octave = (int8_t) spelling.octave;
					}
					else if (pitch) {
						spelling.letter = letterOf(pitch.child_value("step"));
						spelling.octave = pitch.child("octave").text().as_int(4);
						spelling.alter = pitch.child("alter").text().as_int(0);
						spelled.push_back(spelling);
						note.letter = (int8_t) spelling.letter;
						note.octave = (int8_t) spelling.octave;
					}
					// NO STRING AND NO FRET. This is music with no fingerboard: see the header.
					note.string = 0;
					note.fret = -1;

					for (pugi::xml_node tie = node.child("tie"); tie; tie = tie.next_sibling("tie")) {
						const std::string kind = tie.attribute("type").as_string();
						if (kind == "stop")
							note.tied = true;
						else if (kind == "start")
							note.tiesOn = true;
					}
					const pugi::xml_node marks = node.child("notations");
					bool opensSlur = false, closesSlur = false;
					int markAbove = NO_MARK, markBelow = NO_MARK;
					if (marks) {
						const pugi::xml_node arts = marks.child("articulations");
						if (arts) {
							note.staccato = (bool) arts.child("staccato");
							note.accent = (bool) arts.child("accent");
							note.heavyAccent = (bool) arts.child("strong-accent");
							note.tenuto = (bool) arts.child("tenuto");
							// A STACCATISSIMO IS A SHORTER STACCATO and there is nothing
							// downstream that tells the two apart, so it is read as the staccato
							// it is a degree of rather than dropped.
							if (arts.child("staccatissimo"))
								note.staccato = true;
						}
						// A NOTE SOUNDED REPEATEDLY FOR ITS LENGTH. A trill is not this — it
						// alternates with the note above, which nothing downstream can do — so
						// only a written tremolo is read.
						if (marks.child("ornaments").child("tremolo"))
							note.tremolo = true;
						// A TRILL IS THE NOTE AND THE ONE ABOVE IT, ALTERNATING. It was passed
						// over once as something nothing downstream could play, which was the
						// wrong place to decide: the alternation is written out in the timeline,
						// so what an instrument receives is notes like any others.
						// HELD. A fermata is not an articulation of the note but an instruction to
					// wait, so it goes on the beat and the clock does the rest.
					if (marks.child("fermata") && index < building.beats.size())
						building.beats[index].fermata = true;
					const pugi::xml_node orn = marks.child("ornaments");
						// A WAVY LINE IS A TRILL TOO, where it STARTS one. The line is drawn as a
						// pair, a start on the note the trill is on and a stop where it ends, and
						// taking either for a trill put one on the note that merely ended it —
						// bar 7 of the Nocturne carries a stop whose start is nowhere in the
						// file, and a trill appeared there that nobody had written.
						const std::string wavy =
							orn.child("wavy-line").attribute("type").as_string();
						if (orn.child("trill-mark") || wavy == "start")
							note.trill = true;
						// AND THE SHORT ONES, which are a flick rather than an alternation.
						if (orn.child("inverted-mordent"))
							note.invertedMordent = true;
						if (orn.child("mordent"))
							note.mordent = true;
						// A TURN GOES ROUND THE NOTE FROM ABOVE; an inverted one from below. A
						// delayed turn is played as one, a little late being a nicety this does
						// not attempt.
						if (orn.child("turn") || orn.child("delayed-turn"))
							note.turn = true;
						if (orn.child("inverted-turn") || orn.child("delayed-inverted-turn"))
							note.invertedTurn = true;
						// A TURN PRINTED AFTER THE NOTE rather than over it: the note first, and
						// the turn late, just before the next.
						if (orn.child("delayed-turn") || orn.child("delayed-inverted-turn"))
							note.delayedTurn = true;
						// WHERE THE TRILL STARTS, WHERE THE FILE SAYS SO. Only where the attribute is
						// actually written: MusicXML gives an absent one a default of the main note,
						// and taking that default would overrule every style's own rule.
						{
							pugi::xml_attribute start = orn.child("trill-mark").attribute("start-note");
							if (!start)
								start = orn.child("wavy-line").attribute("start-note");
							const std::string from = start.as_string();
							if (from == "main")
								note.trillStart = 0;
							else if (from == "upper")
								note.trillStart = 1;
							else if (from == "below")
								note.trillStart = 2;
						}
						// A SHARP, FLAT OR NATURAL PRINTED ON THE ORNAMENT, above it for the note
						// above and below it for the note below. It overrides the key and the bar.
						for (pugi::xml_node am = orn.child("accidental-mark"); am;
								am = am.next_sibling("accidental-mark")) {
							const int alter = alterOfMark(am.child_value());
							if (alter == NO_MARK)
								continue;
							// NO PLACEMENT, which MusicXML allows: the note the ornament uses. A
							// mordent goes below; everything else here goes above first.
							const std::string placed = am.attribute("placement").as_string();
							if (placed == "below" || (placed.empty() && note.mordent
									&& !note.trill && !note.invertedMordent && !note.turn
									&& !note.invertedTurn))
								markBelow = alter;
							else
								markAbove = alter;
						}
						// A ROLLED CHORD. The notes are written together and played one after
						// another from the bottom, which is a brush — the same thing a guitar's
						// is, and carried by the same field.
						if (marks.child("arpeggiate")) {
							const std::string way =
								marks.child("arpeggiate").attribute("direction").as_string();
							beat.brush = (way == "down") ? -1 : 1;
							// HOW LONG THE ROLL TAKES is the style's, worked out in the timeline once
							// every note of the chord is known. See OrnamentRules::rollSeconds.
							beat.brushMs = 0;
						}
						for (pugi::xml_node sl = marks.child("slur"); sl;
								sl = sl.next_sibling("slur")) {
							const std::string kind = sl.attribute("type").as_string();
							if (kind == "start")
								opensSlur = true;
							else if (kind == "stop")
								closesSlur = true;
						}
					}
					// JOINED TO WHAT COMES NEXT. A note inside a slur runs into the note after
					// it; the note that CLOSES the slur does not, because the slur ends on it.
					const bool was = inSlur[useVoice];
					note.slurred = (was || opensSlur) && !closesSlur;
					if (opensSlur)
						inSlur[useVoice] = true;
					if (closesSlur)
						inSlur[useVoice] = false;
					beat.notes.push_back(note);
					// WHAT THIS VOICE TIES ONWARD, from the note heading the chord.
					if (!isChord && !isGrace)
						tiedOn[useVoice] = std::make_pair(note.tiesOn ? note.midi : -1, spelling);
					if (spelling.letter >= 0 && (note.trill || note.mordent || note.invertedMordent
							|| note.turn || note.invertedTurn)) {
						Ornamented o = {useVoice, index, beat.notes.size() - 1, spelling,
							markAbove, markBelow};
						ornamented.push_back(o);
					}
				}

				if (!isChord && !isGrace) {
					lastNoteAt = cursor;
					lastVoice = voice;
					cursor += duration;
					if (cursor > filled)
						filled = cursor;
				}
			}

			// THE NOTES EACH ORNAMENT GOES TO, where the bar settles them, now that every
			// accidental in it is known. The rest wait for the bars either side: see below.
			for (const Ornamented& o : ornamented) {
				std::map<int, Building>::iterator v = voices.find(o.voice);
				if (v == voices.end() || o.beat >= v->second.beats.size())
					continue;
				GpBeat& b = v->second.beats[o.beat];
				if (o.note >= b.notes.size())
					continue;
				b.notes[o.note].upper = neighbourOf(o.spelled, 1, o.markAbove, spelled);
				b.notes[o.note].lower = neighbourOf(o.spelled, -1, o.markBelow, spelled);
			}
			if ((int) spelledByBar.size() <= barNumber) {
				spelledByBar.resize((size_t) barNumber + 1);
				fifthsByBar.resize((size_t) barNumber + 1, 0);
			}
			spelledByBar[(size_t) barNumber] = spelled;
			fifthsByBar[(size_t) barNumber] = partFifths;

			// THE NEXT BAR BEGINS A BAR'S LENGTH LATER, which is the metre's length for an
			// ordinary bar and the bar's own for a short one.
			//
			// A PICKUP IS SHORTER THAN A BAR AND THE MUSIC MUST NOT WAIT OUT THE DIFFERENCE. The
			// E minor Prelude opens with one quarter of music in a bar of four; counted as four,
			// three quarters of silence were inserted between the pickup and the first whole bar,
			// and everything after it sat three quarters late. A bar is called short only where
			// the writer marked it implicit or where it is both under length and the first bar,
			// so that a bar merely left unfilled by a transcriber is not mistaken for one.
			{
				double length = 4.0;
				if (barNumber < (int) out.masterBars.size()) {
					GpMasterBar& m = out.masterBars[(size_t) barNumber];
					length = (double) m.beatsPerBar * 4.0 / (double) std::max(1, m.beatUnit);
					const double held = (double) filled / (double) divisions;
					if (first && held > 0.0 && held < length - 1e-6 && (implicit || barNumber == 0)) {
						m.quarters = (float) held;
						length = held;
					}
					// AND A BAR MAY HOLD MORE THAN THE METRE SAYS. A cadenza is written as a run
					// over a held chord, and where the writer gave it no backup to return the
					// cursor with, the run simply follows the chord: bar 32 of the Nocturne in E
					// flat is two dotted halves — a full twelve-eight bar — and then thirty-two
					// sixteenths, fourteen quarters of music in a bar of six.
					//
					// COUNTED AS SIX, the run ran past the end of the bar and into the ones after
					// it, so a passage disappeared and the bars after it played two things at
					// once. A bar is as long as what it holds, which for a cadenza is also what
					// a player does with it.
					else if (first && held > length + 1e-6) {
						m.quarters = (float) held;
						length = held;
					}
				}
				barStart += length;
			}

			// EVERY VOICE LAID ON ONE TIMELINE, in voice order, each in time order — which is how
			// the model holds Guitar Pro's own voices and what everything downstream expects.
			for (std::map<int, Building>::iterator v = voices.begin(); v != voices.end(); ++v) {
				std::vector<GpBeat>& beats = v->second.beats;
				std::sort(beats.begin(), beats.end(),
					[](const GpBeat& a, const GpBeat& b) { return a.start < b.start; });
				for (size_t i = 0; i < beats.size(); i++)
					bar.beats.push_back(beats[i]);
			}
		}

		// HOW LOUD EVERY NOTE IS, worked out from the whole part at once now that every mark and
		// every hairpin in it has been seen. A beat written where nothing is marked keeps the
		// loudness in force, which is what a dynamic mark means.
		if (!levels.empty()) {
			const std::vector<std::pair<double, float> > curve = levelCurve(levels);
			for (size_t b = 0; b < track.bars.size() && b < barStarts.size(); b++) {
				for (size_t i = 0; i < track.bars[b].beats.size(); i++) {
					GpBeat& beat = track.bars[b].beats[i];
					// NO FLOOR ANY MORE. This kept a twentieth so that nothing could be
					// silent, back when a level was something like an amplitude and nought
					// meant no sound. A level is a decibel scale now and nought is ppp,
					// twenty-two decibels down and perfectly audible — so the guard did
					// nothing but put the bottom rung of the ladder out of reach.
					beat.dynamic = std::max(0.f, std::min(1.f,
						levelAt(curve, barStarts[b] + (double) beat.start)));
				}
			}
		}

		// THE ORNAMENTS THEIR OWN BAR DID NOT SETTLE, from the music either side of them, now
		// that every bar of the part has been read. See neighbourInContext.
		for (size_t b = 0; b < track.bars.size(); b++) {
			const int fifths = (b < fifthsByBar.size()) ? fifthsByBar[b] : 0;
			for (GpBeat& beat : track.bars[b].beats) {
				for (GpNote& n : beat.notes) {
					if (n.letter < 0 || !(n.trill || n.mordent || n.invertedMordent || n.turn
							|| n.invertedTurn))
						continue;
					if (n.upper < 0)
						n.upper = neighbourInContext(n.letter, n.octave, n.midi, 1, b, fifths,
							spelledByBar);
					if (n.lower < 0)
						n.lower = neighbourInContext(n.letter, n.octave, n.midi, -1, b, fifths,
							spelledByBar);
				}
			}
		}
		first = false;
	}

	if (out.masterBars.empty()) {
		if (why)
			*why = "this score has no bars in it";
		return false;
	}
	// A SONG'S TEMPO IS THE FIRST ONE IT CARRIES.
	if (!out.tempos.empty())
		out.tempo = out.tempos[0].bpm;
	else {
		GpTempo tempo;
		out.tempos.push_back(tempo);
		out.tempo = tempo.bpm;
	}
	return true;
}


bool mxReadBytes(const std::vector<uint8_t>& bytes, GpSong& out, std::string* why) {
	std::string text((const char*) bytes.data(), bytes.size());

	// A COMPRESSED MUSICXML FILE IS A ZIP holding the document and, in META-INF, a note saying
	// which member of the zip the document is. That note is read rather than guessed at, because
	// the document's name is the writer's choice — though every writer in practice calls it
	// score.xml, which is the fallback.
	if (text.size() > 4 && text[0] == 'P' && text[1] == 'K') {
		std::vector<uint8_t> got;
		std::string inside;
		if (gpZipEntry(bytes, "META-INF/container.xml", got)) {
			const std::string container((const char*) got.data(), got.size());
			const size_t at = container.find("full-path");
			if (at != std::string::npos) {
				const size_t a = container.find('"', at);
				const size_t b = (a == std::string::npos)
					? std::string::npos : container.find('"', a + 1);
				if (b != std::string::npos)
					inside = container.substr(a + 1, b - a - 1);
			}
		}
		if (inside.empty())
			inside = "score.xml";
		if (!gpZipEntry(bytes, inside, got)) {
			if (why)
				*why = "that compressed score has nothing in it this can read";
			return false;
		}
		text.assign((const char*) got.data(), got.size());
	}
	return mxRead(text, out, why);
}


bool mxReadFile(const std::string& path, GpSong& out, std::string* why) {
	std::ifstream file(path.c_str(), std::ios::binary);
	if (!file) {
		if (why)
			*why = "that file could not be opened";
		return false;
	}
	std::stringstream all;
	all << file.rdbuf();
	const std::string text = all.str();
	const std::vector<uint8_t> bytes(text.begin(), text.end());
	return mxReadBytes(bytes, out, why);
}


} // namespace px
