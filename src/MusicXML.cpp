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
const Loudness LOUDNESS[] = {
	{"ppp", 0.15f}, {"pp", 0.25f}, {"p", 0.35f}, {"mp", 0.5f},
	{"mf", 0.6f}, {"f", 0.75f}, {"ff", 0.88f}, {"fff", 1.f},
	{"sf", 0.85f}, {"sfz", 0.9f}, {"fp", 0.75f},
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
arrive somewhere; this is the distance it travels, which is about one step of the ladder. */
const float HAIRPIN_REACH = 0.25f;


/** THE LOUDNESS OF THE WHOLE PART AS A LINE, built from the marks: a list of points with straight
lines between them. A step is two points at the same moment. */
std::vector<std::pair<double, float> > levelCurve(std::vector<LevelEvent> events) {
	std::sort(events.begin(), events.end(),
		[](const LevelEvent& a, const LevelEvent& b) { return a.at < b.at; });

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
			// A WORDED RAMP RUNS UNTIL SOMETHING ELSE IS SAID. There is no bracket, so the end
			// is the next mark of any kind, and a word with nothing after it ramps over four
			// quarters, which is a bar of the commonest metre.
			const bool up = (e.kind == LevelEvent::SWELL_WORD);
			double end = e.at + 4.0;
			float target = now + (up ? HAIRPIN_REACH : -HAIRPIN_REACH);
			for (size_t j = i + 1; j < events.size(); j++) {
				if (events[j].at <= e.at)
					continue;
				end = events[j].at;
				if (events[j].kind == LevelEvent::MARK)
					target = events[j].value;
				break;
			}
			target = std::max(0.05f, std::min(1.f, target));
			curve.push_back(std::make_pair(e.at, now));
			curve.push_back(std::make_pair(end, target));
			now = target;
		}
		else {
			// WHERE THE HAIRPIN ARRIVES: the next mark after it if there is one, and otherwise
			// a step of its own in whichever direction it was going.
			float target = now;
			bool found = false;
			for (size_t j = i + 1; j < events.size(); j++) {
				if (events[j].kind == LevelEvent::MARK) {
					target = events[j].value;
					found = true;
					break;
				}
			}
			if (!found) {
				// Which way it was going, from the last hairpin opened before this stop.
				float way = HAIRPIN_REACH;
				for (size_t j = i; j > 0; j--) {
					if (events[j - 1].kind == LevelEvent::FADE) {
						way = -HAIRPIN_REACH;
						break;
					}
					if (events[j - 1].kind == LevelEvent::SWELL)
						break;
				}
				target = now + way;
			}
			target = std::max(0.05f, std::min(1.f, target));
			curve.push_back(std::make_pair(e.at, target));
			now = target;
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
	for (size_t i = 1; i < curve.size(); i++) {
		if (at > curve[i].first)
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
			}

			// One list of beats per voice, each keyed by where in the bar it falls.
			std::map<int, Building> voices;
			long cursor = 0;      // Divisions from the start of the bar.
			long lastNoteAt = 0;
			int lastVoice = 1;

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
				std::map<long, size_t>::iterator found = building.at.find(where);
				size_t index;
				if (found == building.at.end()) {
					GpBeat beat;
					beat.start = (float) where / (float) divisions;
					beat.quarters = (float) duration / (float) divisions;
					beat.rest = isRest;
					beat.dynamic = 0.6f;   // Replaced below, from the whole part's line.
					beat.grace = isGrace ? 2 : 0;
					index = building.beats.size();
					building.beats.push_back(beat);
					building.at[where] = index;
				}
				else {
					index = found->second;
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
				}
			}

			// THE NEXT BAR BEGINS A BAR'S LENGTH LATER, counted from the metre rather than from
			// what the bar happens to hold, which is what the timeline does too.
			{
				double length = 4.0;
				if (barNumber < (int) out.masterBars.size()) {
					const GpMasterBar& m = out.masterBars[(size_t) barNumber];
					length = (double) m.beatsPerBar * 4.0 / (double) std::max(1, m.beatUnit);
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
