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

			for (pugi::xml_node node = measure.first_child(); node;
					node = node.next_sibling()) {
				const std::string tag = node.name();

				if (tag == "attributes") {
					if (node.child("divisions"))
						divisions = std::max(1, node.child("divisions").text().as_int(480));
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
				const bool isChord = (bool) node.child("chord");
				const bool isRest = (bool) node.child("rest");
				const bool isGrace = (bool) node.child("grace");
				const int voice = std::max(1, node.child("voice").text().as_int(1));
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
					if (pitch) {
						note.midi = midiOf(pitch.child_value("step"),
							pitch.child("alter").text().as_int(0),
							pitch.child("octave").text().as_int(4));
					}
					else if (unpitched) {
						note.midi = midiOf(unpitched.child_value("display-step"), 0,
							unpitched.child("display-octave").text().as_int(4));
					}
					// WRITTEN FOR THE SOUND AND NOT FOR THE EYE. See GpNote::hidden.
					note.hidden = std::string(node.attribute("print-object").as_string()) == "no";
					// NO STRING AND NO FRET. This is music with no fingerboard: see the header.
					note.string = 0;
					note.fret = -1;

					for (pugi::xml_node tie = node.child("tie"); tie; tie = tie.next_sibling("tie")) {
						if (std::string(tie.attribute("type").as_string()) == "stop")
							note.tied = true;
					}
					const pugi::xml_node marks = node.child("notations");
					bool opensSlur = false, closesSlur = false;
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
						if (orn.child("turn") || orn.child("inverted-turn"))
							note.turn = true;
						// A ROLLED CHORD. The notes are written together and played one after
						// another from the bottom, which is a brush — the same thing a guitar's
						// is, and carried by the same field.
						if (marks.child("arpeggiate")) {
							const std::string way =
								marks.child("arpeggiate").attribute("direction").as_string();
							beat.brush = (way == "down") ? -1 : 1;
							beat.brushMs = 45;
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
				}

				if (!isChord && !isGrace) {
					lastNoteAt = cursor;
					lastVoice = voice;
					cursor += duration;
					if (cursor > filled)
						filled = cursor;
				}
			}

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
					beat.dynamic = std::max(0.05f, std::min(1.f,
						levelAt(curve, barStarts[b] + (double) beat.start)));
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
