/** See GpTimeline.hpp. */
#include <cmath>
#include "GpTimeline.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace px {

/** HOW LONG ONE NOTE OF A TRILL LASTS, in seconds. A trill is fingers rather than tempo: a player
trills at much the same speed in a slow piece as in a fast one, somewhere around a dozen notes a
second, so this is a time and not a fraction of the note. */
static const double TRILL_SECONDS = 0.08;

/** HOW MUCH LIGHTER AN ORNAMENT'S QUICK NOTES ARE than the note they decorate, on the dynamic
scale where a whole marking is about a seventh: about one marking.

THE STRESS IS ON THE FIRST NOTE, the one on the beat, and what follows it is a flick of the finger
— which every source from Leopold Mozart and Türk to Hummel says, and which a pianist does without
thinking. Played at the note's own dynamic, every note of a mordent in a forte passage was a forte
strike: in the Pathétique's bars 87 and 88 all three came from the piano's hard top layers, three
hard hammer blows inside a tenth of a second, which was heard as metallic beside the mordents a
layer or two softer around them. */
static const float ORNAMENT_LIGHTER = 0.12f;

/** AND A MORDENT'S TWO MORE, a second marking lighter again: the quickest flick of all, two notes
in the time a trill takes over one, and heard as a grace on the note rather than as notes of their
own. Judged by ear on the Pathétique's second theme against one marking. */
static const float MORDENT_LIGHTER = 0.24f;

/** HOW MUCH LONGER A BEAT UNDER A FERMATA IS HELD. Nothing says; players hold about half as long
again to twice, and this is the middle of that. */
static const double FERMATA_HELD = 1.75;

namespace {


/** A song that jumps to a sign that is not there, or repeats a section that contains its own
jump back, would walk for as long as there is memory. This is the end of it. Sixteen times the
longest transcription anyone writes. */
static const size_t MAX_PLAYED_BARS = 8192;


bool starts(const std::string& s, const char* prefix) {
	const size_t n = std::strlen(prefix);
	return s.size() >= n && s.compare(0, n, prefix) == 0;
}

/** Is this one of the marks that sends you somewhere, rather than a place to come back to? */
bool isJump(const std::string& mark) {
	return starts(mark, "Da") && mark != "DaCoda" && mark != "DaDoubleCoda";
}

/** Where a jump goes. Da Capo goes to the top, which is named here as an empty string. */
std::string targetOf(const std::string& jump) {
	if (starts(jump, "DaSegnoSegno"))
		return "SegnoSegno";
	if (starts(jump, "DaSegno"))
		return "Segno";
	return "";
}

bool has(const std::vector<std::string>& marks, const char* name) {
	for (size_t i = 0; i < marks.size(); i++) {
		if (marks[i] == name)
			return true;
	}
	return false;
}

bool ends(const std::string& jump, const char* suffix) {
	const size_t n = std::strlen(suffix);
	return jump.size() >= n && jump.compare(jump.size() - n, n, suffix) == 0;
}

/** The first bar carrying a named sign, or -1. */
int barOfMark(const GpSong& song, const std::string& mark) {
	if (mark.empty())
		return 0;                        // The top of the song.
	for (size_t i = 0; i < song.masterBars.size(); i++) {
		if (has(song.masterBars[i].marks, mark.c_str()))
			return (int) i;
	}
	return -1;
}


/** THE BARS IN THE ORDER THEY SOUND, walked the way a player walks them.

Repeats go back until the section has been played as many times as it says. A bar carrying
alternate endings is played only on the passes it names, and a bar that is skipped takes its
repeat with it, so a first ending holding the repeat does not send you back a second time.

A jump — D.C. or D.S., with or without al Coda and al Fine — is taken once. Repeats are not taken
again after one, which is the usual reading and also the thing that stops a song from growing
without end. */
void playedOrder(const GpSong& song, std::vector<std::pair<int, int> >& order, int* skipped) {
	const int n = (int) song.masterBars.size();
	int at = 0;
	int repeatStart = 0;
	int played = 1;                      /**< Times the current section has been played. */
	bool afterJump = false;
	bool codaArmed = false, fineArmed = false;
	std::string codaMark = "Coda";
	std::set<int> jumped;

	while (at >= 0 && at < n && order.size() < MAX_PLAYED_BARS) {
		const GpMasterBar& bar = song.masterBars[(size_t) at];
		if (bar.repeatOpen && at != repeatStart) {
			repeatStart = at;
			played = 1;
		}

		// An ending this pass does not take, skipped whole — including any repeat on it.
		if (!bar.endings.empty()
				&& std::find(bar.endings.begin(), bar.endings.end(), played)
					== bar.endings.end()) {
			at++;
			continue;
		}

		order.push_back(std::make_pair(at, played));

		// The bar is played, and then it says where to go.
		if (fineArmed && has(bar.marks, "Fine"))
			return;

		if (codaArmed && (has(bar.marks, "DaCoda") || has(bar.marks, "DaDoubleCoda"))) {
			const int target = barOfMark(song, codaMark);
			codaArmed = false;
			if (target >= 0) {
				at = target;
				continue;
			}
			(*skipped)++;                // Sent to a coda the song does not carry.
		}

		std::string jump;
		for (size_t i = 0; i < bar.marks.size() && jump.empty(); i++) {
			if (isJump(bar.marks[i]))
				jump = bar.marks[i];
		}
		if (!jump.empty() && jumped.find(at) == jumped.end()) {
			jumped.insert(at);
			const int target = barOfMark(song, targetOf(jump));
			if (target >= 0) {
				fineArmed = ends(jump, "AlFine");
				codaArmed = ends(jump, "AlCoda") || ends(jump, "AlDoubleCoda");
				codaMark = ends(jump, "AlDoubleCoda") ? "DoubleCoda" : "Coda";
				afterJump = true;
				repeatStart = target;
				played = 1;
				at = target;
				continue;
			}
			(*skipped)++;                // Sent to a sign the song does not carry.
		}

		if (bar.repeatCount > 1 && !afterJump && played < bar.repeatCount) {
			played++;
			at = repeatStart;
			continue;
		}
		// THE PASS NUMBER SURVIVES THE REPEAT. The bar after the last time through carries the
		// second ending, and it is reached on the last pass, not on a fresh one. It is the next
		// opening repeat that starts the counting again.
		if (bar.repeatCount > 1)
			repeatStart = at + 1;
		at++;
	}
}


struct Clock {
	std::vector<GpSegment>& segments;

	explicit Clock(std::vector<GpSegment>& into) : segments(into) {}

	void add(double quarters, double secondsPerQuarter) {
		GpSegment s;
		s.quarters = quarters;
		s.secondsPerQuarter = secondsPerQuarter;
		if (segments.empty())
			s.seconds = 0.0;
		else {
			const GpSegment& last = segments.back();
			s.seconds = last.seconds + (quarters - last.quarters) * last.secondsPerQuarter;
		}
		if (!segments.empty() && quarters <= segments.back().quarters)
			segments.back() = s;         // Two changes in the same place: the later one wins.
		else
			segments.push_back(s);
	}

	double secondsAt(double quarters) const {
		if (segments.empty())
			return quarters * 0.5;
		size_t lo = 0, hi = segments.size();
		while (lo + 1 < hi) {
			const size_t mid = (lo + hi) / 2;
			if (segments[mid].quarters <= quarters)
				lo = mid;
			else
				hi = mid;
		}
		const GpSegment& s = segments[lo];
		return s.seconds + (quarters - s.quarters) * s.secondsPerQuarter;
	}
};


/** A tempo written as a count of some note value, as quarter notes a minute.

THE UNIT IS A CODE AND NOT A DENOMINATOR. Two means a quarter note, which is what almost every
file writes and what anybody means by a tempo. Reading it as the bottom of a time signature made
every one of these songs play at twice its speed. */
double quartersPerMinute(const GpTempo& t) {
	double beat = 1.0;                   // Quarter notes in one of the beats being counted.
	switch (t.unit) {
		case 1: beat = 0.5; break;       // An eighth.
		case 3: beat = 1.5; break;       // A dotted quarter.
		case 4: beat = 2.0; break;       // A half.
		case 5: beat = 3.0; break;       // A dotted half.
		default: beat = 1.0; break;      // A quarter, which is two and anything unexpected.
	}
	return (double) t.bpm * beat;
}


/** Which segment a position falls in. */
size_t segmentAt(const std::vector<GpSegment>& segments, double quarters) {
	size_t lo = 0, hi = segments.size();
	while (lo + 1 < hi) {
		const size_t mid = (lo + hi) / 2;
		if (segments[mid].quarters <= quarters)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}


} // namespace


double gpSecondsAt(const GpTimeline& line, double quarters) {
	if (line.clock.empty())
		return quarters * 0.5;
	const GpSegment& s = line.clock[segmentAt(line.clock, quarters)];
	return s.seconds + (quarters - s.quarters) * s.secondsPerQuarter;
}


double gpSecondsPerQuarter(const GpTimeline& line, double quarters) {
	if (line.clock.empty())
		return 0.5;
	return line.clock[segmentAt(line.clock, quarters)].secondsPerQuarter;
}


bool gpBuildTimeline(const GpSong& song, GpTimeline& out, std::string* why) {
	out = GpTimeline();
	if (song.masterBars.empty()) {
		if (why)
			*why = "this song has no bars in it";
		return false;
	}

	// ---- the order the bars sound in -----------------------------------------------------------
	std::vector<std::pair<int, int> > order;
	playedOrder(song, order, &out.skipped);
	if (order.empty()) {
		if (why)
			*why = "this song plays no bars at all";
		return false;
	}

	// ---- the tempo changes, by the bar they belong to -------------------------------------------
	std::vector<GpTempo> tempos = song.tempos;
	if (tempos.empty()) {
		GpTempo t;
		t.bpm = (song.tempo > 0.f) ? song.tempo : 120.f;
		tempos.push_back(t);
	}
	std::map<int, std::vector<GpTempo> > byBar;
	for (size_t i = 0; i < tempos.size(); i++)
		byBar[tempos[i].bar].push_back(tempos[i]);
	for (std::map<int, std::vector<GpTempo> >::iterator it = byBar.begin(); it != byBar.end();
			++it) {
		std::vector<GpTempo>& list = it->second;
		for (size_t i = 1; i < list.size(); i++) {         // Few enough to sort by hand.
			for (size_t j = i; j > 0 && list[j].position < list[j - 1].position; j--)
				std::swap(list[j], list[j - 1]);
		}
	}

	// ---- the bars, with their places and their times ---------------------------------------------
	//
	// A REPEATED BAR CARRIES ITS TEMPO AGAIN. The change is written on the bar, so every pass
	// through that bar is played at that tempo, which is what a player does.
	Clock clock(out.clock);
	double quarters = 0.0;
	double bpm = quartersPerMinute(tempos[0]);
	// A tempo written before the first played bar is in force from the start.
	for (size_t i = 0; i < tempos.size(); i++) {
		if (tempos[i].bar <= order[0].first)
			bpm = quartersPerMinute(tempos[i]);
	}
	clock.add(0.0, 60.0 / bpm);

	out.bars.reserve(order.size());
	for (size_t i = 0; i < order.size(); i++) {
		const int written = order[i].first;
		const GpMasterBar& master = song.masterBars[(size_t) written];

		GpPlayedBar bar;
		bar.written = written;
		bar.pass = order[i].second;
		bar.startQuarters = quarters;
		// A PICKUP IS AS LONG AS IT IS, not as long as the metre says. See GpMasterBar::quarters.
		bar.quarters = (master.quarters > 0.f) ? (double) master.quarters
			: (double) master.beatsPerBar * 4.0 / ((master.beatUnit > 0) ? master.beatUnit : 4);

		const std::map<int, std::vector<GpTempo> >::const_iterator found = byBar.find(written);
		if (found != byBar.end()) {
			const std::vector<GpTempo>& list = found->second;
			for (size_t k = 0; k < list.size(); k++) {
				bpm = quartersPerMinute(list[k]);
				clock.add(quarters + (double) list[k].position, 60.0 / bpm);
			}
		}

		// A FERMATA IS A PAUSE, AND EVERYTHING WAITS FOR IT.
		//
		// It holds one beat longer than it is written and then the music goes on at the tempo it
		// was at, so it is a change of clock and not of notes — a rest under a fermata is held
		// too, and a renderer that stretched only the notes would leave the silence unstretched.
		// Written in every part at the same moment, so the first one found at a position is the
		// pause and the others are the same pause written again.
		//
		// HOW LONG IS NOT WRITTEN ANYWHERE. Players hold about half as long again to twice; this
		// takes the middle of that, which is long enough to hear as a pause and short enough not
		// to sound like a mistake.
		{
			std::set<double> paused;
			for (size_t t = 0; t < song.tracks.size(); t++) {
				if (written >= (int) song.tracks[t].bars.size())
					continue;
				const GpBar& wb = song.tracks[t].bars[(size_t) written];
				for (size_t e = 0; e < wb.beats.size(); e++) {
					const GpBeat& beat = wb.beats[e];
					if (!beat.fermata || beat.quarters <= 0.f)
						continue;
					if (!paused.insert((double) beat.start).second)
						continue;
					clock.add(quarters + (double) beat.start, 60.0 / bpm * FERMATA_HELD);
					clock.add(quarters + (double) beat.start + (double) beat.quarters,
						60.0 / bpm);
				}
			}
		}

		bar.bpm = (float) bpm;
		bar.startSeconds = clock.secondsAt(bar.startQuarters);
		bar.seconds = clock.secondsAt(bar.startQuarters + bar.quarters) - bar.startSeconds;
		out.bars.push_back(bar);
		quarters += bar.quarters;
	}
	out.quarters = quarters;
	out.seconds = clock.secondsAt(quarters);

	// ---- the pedal -------------------------------------------------------------------------------
	//
	// EVERY PASS PEDALS AGAIN. A pedal mark is written on a bar, and a bar inside a repeat is
	// played more than once, so the marks are laid out against the played bars rather than the
	// written ones — the same expansion the notes get, and for the same reason.
	if (!song.pedals.empty()) {
		std::map<int, std::vector<const GpPedalMark*> > byWritten;
		for (size_t i = 0; i < song.pedals.size(); i++)
			byWritten[song.pedals[i].bar].push_back(&song.pedals[i]);
		for (size_t i = 0; i < out.bars.size(); i++) {
			const std::map<int, std::vector<const GpPedalMark*> >::const_iterator found =
				byWritten.find(out.bars[i].written);
			if (found == byWritten.end())
				continue;
			for (size_t k = 0; k < found->second.size(); k++) {
				const GpPedalMark& m = *found->second[k];
				GpPlayedPedal p;
				p.startQuarters = out.bars[i].startQuarters + (double) m.start;
				p.startSeconds = clock.secondsAt(p.startQuarters);
				p.kind = m.kind;
				p.sostenuto = m.sostenuto;
				out.pedals.push_back(p);
			}
		}
		std::stable_sort(out.pedals.begin(), out.pedals.end(),
			[](const GpPlayedPedal& a, const GpPlayedPedal& b) {
				return a.startQuarters < b.startQuarters;
			});
		// LIFTED AND TAKEN AGAIN ON THE SPOT IS A CHANGE, whatever the writer called it. A
		// pedalled piece is usually written as a row of brackets that touch — each one stops
		// where the next begins — and read literally that is a foot that never leaves the floor,
		// so the whole piece rings into one chord. The place where two brackets meet is the
		// moment the pianist clears the sound, which is the opposite of what it reads as, and it
		// is the single most audible thing in a pedal mark. The Chopin Ballade is 250 brackets
		// and 159 of these joins.
		std::vector<GpPlayedPedal> merged;
		merged.reserve(out.pedals.size());
		for (size_t i = 0; i < out.pedals.size(); i++) {
			const bool joins = i + 1 < out.pedals.size()
				&& out.pedals[i].kind == GpPedalMark::UP
				&& out.pedals[i + 1].kind == GpPedalMark::DOWN
				&& out.pedals[i].sostenuto == out.pedals[i + 1].sostenuto
				&& out.pedals[i + 1].startQuarters - out.pedals[i].startQuarters < 1e-6;
			if (joins) {
				GpPlayedPedal p = out.pedals[i];
				p.kind = GpPedalMark::CHANGE;
				merged.push_back(p);
				i++;
			} else
				merged.push_back(out.pedals[i]);
		}
		out.pedals.swap(merged);
	}

	// ---- the notes -------------------------------------------------------------------------------
	//
	// TIED NOTES BECOME ONE. A tie is not a note; it is the one before it going on sounding, so
	// the note already emitted is made longer and nothing new is struck. The previous note is
	// found by its string where the file gives one, since the same pitch can be sounding on two
	// strings at once, and by its pitch where it does not.
	out.tracks.resize(song.tracks.size());
	for (size_t t = 0; t < song.tracks.size(); t++) {
		const GpTrack& track = song.tracks[t];
		std::vector<GpPlayedNote>& played = out.tracks[t];
		std::map<int, size_t> lastOnString;        /**< String number, or a pitch made negative. */

		for (size_t i = 0; i < out.bars.size(); i++) {
			const size_t written = (size_t) out.bars[i].written;
			if (written >= track.bars.size())
				continue;
			const GpBar& bar = track.bars[written];
			const double barStart = out.bars[i].startQuarters;

			// HOW FAST THE STRUMMING HAND IS MOVING IN THIS BAR.
			//
			// A strumming hand keeps swinging whether or not it strikes: at eighths it goes down
			// on the beat and up between, and it strikes on the way past. So which way the hand
			// is going at any moment follows from the RATE it is swinging at, and that rate is
			// the fastest the part asks for — a bar of sixteenth chords is a hand moving in
			// sixteenths, and a bar of quarter chords is a hand still moving in eighths and
			// striking only on the way down, which is why quarter chords come out all
			// down-strokes and sixteenths come out alternating.
			//
			// NEVER SLOWER THAN EIGHTHS, for that reason: a hand that moved in quarters would
			// play every other quarter upwards, which no one does.
			double pulse = 0.5;
			for (size_t b = 0; b < bar.beats.size(); b++) {
				const GpBeat& e = bar.beats[b];
				if (e.rest || e.notes.size() < 3 || e.quarters <= 0.f)
					continue;
				int struck = 0;
				for (size_t o = 0; o < e.notes.size(); o++) {
					if (!e.notes[o].tied)
						struck++;
				}
				if (struck >= 3)
					pulse = std::fmin(pulse, (double) e.quarters);
			}
			pulse = std::fmax(0.0625, pulse);

			for (size_t b = 0; b < bar.beats.size(); b++) {
				const GpBeat& beat = bar.beats[b];
				if (beat.rest || beat.notes.empty())
					continue;
				const double start = barStart + (double) beat.start;
				const double length = (double) beat.quarters;

				for (size_t k = 0; k < beat.notes.size(); k++) {
					const GpNote& note = beat.notes[k];
					const int key = (note.string > 0) ? note.string : -note.midi;

					if (note.tied) {
						std::map<int, size_t>::iterator it = lastOnString.find(key);
						// WITH NO STRINGS, THE NOTE THAT ENDS WHERE THIS ONE BEGINS AND IS TIED
						// ONWARD. The pitch alone names whichever voice struck it last, and in the
						// Waldstein finale a trilled G5 tied into the next bar was continued onto
						// the melody's G5, struck at the same moment in another voice and ending at
						// the same moment too: the trill stopped and the melody note held for eight
						// quarters. Of the notes ending here, one the file says is tied onward is
						// taken first. Looked for among the recent notes; where none ends there,
						// the last of that pitch, as before.
						size_t ending = played.size();
						if (note.string <= 0) {
							const size_t from = played.size() > 256 ? played.size() - 256 : 0;
							for (size_t q = played.size(); q-- > from; ) {
								const GpPlayedNote& c = played[q];
								if (c.note.midi != note.midi
										|| std::fabs(c.startQuarters + c.lengthQuarters - start) >= 1e-6)
									continue;
								if (ending == played.size() || (c.note.tiesOn
										&& !played[ending].note.tiesOn))
									ending = q;
							}
						}
						if (ending < played.size()) {
							lastOnString[key] = ending;
							it = lastOnString.find(key);
						}
						if (it != lastOnString.end() && it->second < played.size()) {
							GpPlayedNote& before = played[it->second];
							// Tied onward from here only if this continuation is.
							before.note.tiesOn = note.tiesOn;
							before.lengthQuarters = start + length - before.startQuarters;
							before.lengthSeconds =
								clock.secondsAt(before.startQuarters + before.lengthQuarters)
								- before.startSeconds;
							continue;
						}
						out.skipped++;     // A tie continuing a note that is not there.
					}

					GpPlayedNote p;
					p.track = (int) t;
					p.bar = (int) i;
					p.startQuarters = start;
					p.lengthQuarters = length;
					p.startSeconds = clock.secondsAt(start);
					p.lengthSeconds = clock.secondsAt(start + length) - p.startSeconds;
					p.dynamic = beat.dynamic;
					p.brush = 0;   // Filled in below, once an unmarked chord has been read.
					p.brushMs = beat.brushMs;
					p.grace = beat.grace ? beat.grace : note.grace;
					// WHERE THE HAND WAS. The note before it on the same string, so a hammer-on
					// can be told from a pull-off and a slide knows where it starts.
					{
						const std::map<int, size_t>::const_iterator was = lastOnString.find(key);
						if (was != lastOnString.end() && was->second < played.size())
							p.fromMidi = played[was->second].note.midi;
					}
					// A CHORD NOBODY MARKED AS A STRUM IS STILL A STRUM.
					//
					// Across the forty-six Guitar Pro files here there are 20,654 chords of three
					// or more notes and FIFTY-SIX of them are marked as brushed — three in a
					// thousand. Transcribers do not write it down because a guitarist reading the
					// page does not need telling; the mark is reserved for a stroke slow enough
					// to be worth pointing out. Played as written, every one of the other 20,598
					// is six strings struck at the same instant at the same level, which is a
					// block chord and sounds like one.
					//
					// WHICH WAY THE HAND IS GOING COMES FROM THE RHYTHM, not from a setting. A
					// chord on the beat is a down-stroke and one falling between beats is the
					// hand coming back up, which is what makes a strummed bar sound like a bar
					// rather than a row of attacks. In six-eight the beat is a dotted quarter,
					// so the bar's own beat is what the position is measured against.
					int brush = beat.brush;
					if (brush == 0 && !track.tuning.empty()) {
						int struck = 0;
						for (size_t o = 0; o < beat.notes.size(); o++) {
							if (!beat.notes[o].tied)
								struck++;
						}
						if (struck >= 3) {
							// WHICH WAY THE HAND IS GOING: down on the even swings of the pulse
							// and up on the odd ones. See `pulse` above.
							const long swing =
								(long) std::floor((double) beat.start / pulse + 0.5);
							brush = ((swing % 2) == 0) ? 1 : -1;
						}
					}

					// WHERE IN THE STROKE. A brushed chord is struck low to high going down and
					// high to low coming up, so the order the hand reaches the strings is the
					// order the notes speak in.
					if (brush != 0) {
						// A PIANO HAS NO STRINGS TO COUNT. The order of a brush is the order the
						// hand reaches the strings, which on a fingerboard is a string number —
						// and a rolled chord in a piano score has none, since MusicXML.cpp sets
						// every string to nought for music with no fingerboard. Every note of
						// every rolled chord therefore scored the same and came out first, so
						// the roll was read, carried and then played flat. Ordered by pitch
						// where there are no strings, which is what the sign means anyway: up
						// the chord or down it.
						bool byString = false;
						for (size_t o = 0; o < beat.notes.size(); o++)
							if (beat.notes[o].string > 0)
								byString = true;
						int before = 0, total = 0;
						for (size_t o = 0; o < beat.notes.size(); o++) {
							if (beat.notes[o].tied)
								continue;
							total++;
							// One is the highest string, so a downstroke starts at the largest
							// number and an upstroke at the smallest; by pitch, a stroke up the
							// chord starts at the lowest note.
							const bool earlier = byString
								? ((brush > 0) ? (beat.notes[o].string > note.string)
								               : (beat.notes[o].string < note.string))
								: ((brush > 0) ? (beat.notes[o].midi < note.midi)
								               : (beat.notes[o].midi > note.midi));
							if (earlier)
								before++;
						}
						p.strumIndex = (uint8_t) before;
						p.strumCount = (uint8_t) total;
					}
					p.brush = brush;
					p.note = note;
					p.note.tied = false;
					// AFTER THE NOTE IS COPIED, not before it: what the beat says is added to
					// what the note says, and setting it first only to overwrite it was a
					// quiet way of losing every tapped note in the song.
					//
					// WHAT THE RIGHT HAND DID belongs to the beat, and goes on each of its
					// notes, since a note is what travels.
					p.note.tapped = p.note.tapped || beat.tapped;
					// THE WHAMMY BAR MOVES EVERY STRING of the beat, so a note with no bend of
					// its own takes the bar's movement as its bend. A note that is bent as well
					// keeps its own, which is the more specific statement.
					//
					// NO HIGHER THAN A GUITAR GOES. Files carry raises no hand could make —
					// a whammy bar pulled up three and a half tones, standing in for a pitch
					// pedal — and played as written they sound like a mistake. A bar pulls up a
					// tone and a half at most, and a string bends three tones, which is as far
					// as Guitar Pro's own bend editor goes. Downward is left alone: a bar
					// dives a long way.
					static const float BAR_UP_CENTS = 300.f;
					static const float BEND_UP_CENTS = 600.f;
					if (p.note.bend.empty() && !beat.whammy.empty()) {
						p.note.bend = beat.whammy;
						for (auto& point : p.note.bend)
							point.second = std::min(point.second, BAR_UP_CENTS);
					}
					else {
						for (auto& point : p.note.bend)
							point.second = std::min(point.second, BEND_UP_CENTS);
					}
					lastOnString[key] = played.size();
					played.push_back(p);
				}
			}
		}

		// The voices of a bar are read one after another, so the list is in bar order but not in
		// time order inside a bar. Everything downstream reads it as a stream.
		std::stable_sort(played.begin(), played.end(),
			[](const GpPlayedNote& a, const GpPlayedNote& b) {
				return a.startQuarters < b.startQuarters;
			});
	}

	// ---- ornaments --------------------------------------------------------------------------
	//
	// AN ORNAMENT IS WRITTEN OUT AS THE NOTES IT STANDS FOR. Done here rather than left to an
	// instrument, so that every instrument plays one and none has to know what the sign means —
	// the same reason a rolled chord becomes an order of notes rather than a flag.
	//
	// A STEP IN THE KEY, not a tone and not a semitone. The Nocturne's trill is on F in E flat
	// major, where the step above is G; in another key the same F trills to F sharp. A MusicXML
	// file spells it outright — the next letter name, with the bar's accidentals and any sharp,
	// flat or natural printed on the ornament — and the reader works it out there. A Guitar Pro
	// file spells nothing, and the key signature gives seven notes to take the next of.
	//
	// ONLY THE SIGN, NEVER A HIDDEN SPELLING-OUT OF IT. Notes not on the page are not read at all
	// (see MusicXML.cpp), so every ornament is played from what the sign means.
	//
	// A TRILL LASTS THE WHOLE NOTE and starts and ends on the written one, so its count of
	// alternations is odd. The short ornaments are a flick at the start and then the note itself:
	// a mordent is main, below, main; an inverted mordent — which is what a short trill is
	// written as — is main, above, main; a turn is above, main, below, main, and an inverted
	// turn below, main, above, main.
	for (size_t t = 0; t < out.tracks.size(); t++) {
		std::vector<GpPlayedNote>& played = out.tracks[t];
		std::vector<GpPlayedNote> grown;
		bool any = false;
		for (size_t i = 0; i < played.size(); i++) {
			const GpPlayedNote& n = played[i];
			const bool quick = n.note.mordent || n.note.invertedMordent || n.note.turn
				|| n.note.invertedTurn;

			if ((!n.note.trill && !quick) || n.note.midi <= 0
					|| n.lengthSeconds < 2.0 * TRILL_SECONDS) {
				grown.push_back(n);
				continue;
			}
			any = true;
			// THE KEY THIS BAR IS IN. The signature's sharps or flats name a major key, and its
			// seven notes are the same seven whatever mode is being played in.
			int fifths = 0;
			if (n.bar >= 0 && n.bar < (int) out.bars.size()) {
				const int written = out.bars[(size_t) n.bar].written;
				if (written >= 0 && written < (int) song.masterBars.size())
					fifths = song.masterBars[(size_t) written].keyAccidentals;
			}
			const int tonic = ((fifths * 7) % 12 + 12) % 12;
			static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
			// THE NEAREST NOTE OF THE KEY each way, a semitone first. It used to start from the
			// semitone as a placeholder and could not tell it from a semitone found, so a key
			// note a semitone away was passed over for the one beyond: E to F in B flat trilled
			// to G, and B to C in C major would have trilled to D.
			int above = -1, below = -1;
			for (int step = 1; step <= 3; step++) {
				bool up = false, down = false;
				const int pu = ((n.note.midi + step) % 12 + 12) % 12;
				const int pd = ((n.note.midi - step) % 12 + 12) % 12;
				for (int d = 0; d < 7; d++) {
					if (pu == (tonic + MAJOR[d]) % 12) up = true;
					if (pd == (tonic + MAJOR[d]) % 12) down = true;
				}
				if (up && above < 0)
					above = n.note.midi + step;
				if (down && below < 0)
					below = n.note.midi - step;
			}
			if (above < 0)
				above = n.note.midi + 2;
			if (below < 0)
				below = n.note.midi - 2;
			// AS SPELLED, WHERE THE FILE SPELLS IT, which a MusicXML file does: the next letter
			// name each way, with the bar's accidentals and any printed on the ornament. The key
			// alone above is all a Guitar Pro file gives. See GpNote::upper.
			if (n.note.upper >= 0)
				above = n.note.upper;
			if (n.note.lower >= 0)
				below = n.note.lower;
			// THE NOTES THE SIGN STANDS FOR, and how many of them are the ornament rather than
			// the note it is on.
			// A TRILL ENDS WHERE ITS TERMINATION BEGINS.
			//
			// A trill is often finished by two small notes written under the end of it — the
			// turn out of the trill, which a player trills up to and then plays. They are
			// written in a voice of their own at the end of the trilled note's span, so a trill
			// spread over the whole of that span goes on alternating while they sound, and the
			// two run over each other.
			//
			// So the alternation stops where the next note near it begins. Near in pitch as well
			// as in time: an accompaniment plays all the way through a trill and is an octave or
			// more below it, while a termination is a step or two away.
			double trilled = n.lengthSeconds;
			if (n.note.trill) {
				for (size_t j = 0; j < played.size(); j++) {
					if (j == i)
						continue;
					const GpPlayedNote& other = played[j];
					if (other.startSeconds <= n.startSeconds
							|| other.startSeconds >= n.startSeconds + n.lengthSeconds)
						continue;
					// A STEP OR TWO AWAY, as a termination is. Within an octave let a scale in
					// the other hand end the Waldstein finale's long trills where it passed
					// under them, four quarters early.
					if (std::abs(other.note.midi - n.note.midi) > 2)
						continue;
					// AND SHORT, as a termination is. A melody or an accompaniment note may pass
					// close to a trill in pitch and is no reason to stop one; two small notes
					// written under the end of it are.
					if (other.lengthSeconds > TRILL_SECONDS * 3.0)
						continue;
					const double upTo = other.startSeconds - n.startSeconds;
					if (upTo < trilled)
						trilled = upTo;
				}
				if (trilled < 2.0 * TRILL_SECONDS)
					trilled = n.lengthSeconds;   // Nothing worth stopping for.
			}

			const bool terminated = n.note.trill && trilled < n.lengthSeconds - 1e-6;
			// THE STYLE'S RULES: where a trill starts and how fast, how quick a mordent and a
			// turn are, whether a turn is added. See OrnamentStyle.hpp.
			const OrnamentRules& rules = ornamentRules(song.ornamentStyle);
			const float lighter = (n.note.mordent || n.note.invertedMordent)
				? MORDENT_LIGHTER : ORNAMENT_LIGHTER;
			const double perQuarter = (n.lengthSeconds > 0.0)
				? n.lengthQuarters / n.lengthSeconds : 0.0;
			// One note of the figure, at a time and a length in seconds from the written note's
			// start; the first at the note's own dynamic and the rest lighter.
			auto emit = [&](int midi, double at, double length, bool first) {
				GpPlayedNote one = n;
				one.note.trill = one.note.mordent = one.note.invertedMordent = false;
				one.note.turn = one.note.invertedTurn = one.note.delayedTurn = false;
				one.note.midi = midi;
				one.note.tied = false;
				if (!first)
					one.dynamic = std::max(0.f, n.dynamic - lighter);
				one.startSeconds = n.startSeconds + at;
				one.lengthSeconds = length;
				one.startQuarters = n.startQuarters + at * perQuarter;
				one.lengthQuarters = length * perQuarter;
				grown.push_back(one);
			};

			if (n.note.trill) {
				// WHERE IT STARTS. What the file says, if it says; then what is written before
				// it; then the style.
				//
				// A GRACE NOTE WRITTEN BEFORE THE TRILL IS ITS START, in every style: the note
				// above means the trill goes on from the written note, the written note itself
				// means it goes on from above without striking it twice, and a note below — the
				// prefix from below Chopin and Weber write — means from the written note.
				//
				// THE NOTE ABOVE ALREADY SOUNDING AND SLURRED IN is not struck again: the trill
				// goes on from the written note. That is the bound trill of every eighteenth-
				// century table, and it is what the hand does in any style.
				bool fromAbove = rules.trillFromAbove;
				if (n.note.trillStart >= 0)
					fromAbove = n.note.trillStart == 1;
				else {
					bool decided = false;
					for (size_t j = 0; j < played.size() && !decided; j++) {
						const GpPlayedNote& before = played[j];
						const double ends = before.startSeconds + before.lengthSeconds;
						if (j == i || std::fabs(ends - n.startSeconds) > 0.004
								|| std::abs(before.note.midi - n.note.midi) > 2)
							continue;
						if (before.grace) {
							fromAbove = before.note.midi == n.note.midi;
							decided = true;
						}
						else if (before.note.midi == above && before.note.slurred) {
							fromAbove = false;
							decided = true;
						}
					}
				}
				// HOW FAST: the style's speed in the middle of the keyboard, a tenth slower at
				// the bottom and a tenth faster at the top, as Quantz, Leopold Mozart and Türk all
				// have it.
				const double register_ = std::max(-1.0, std::min(1.0, (n.note.midi - 66) / 18.0));
				const double rate = (double) rules.trillPerSecond * (1.0 + 0.1 * register_);
				// A TURN ADDED AT THE END, where the style adds one and nothing is written: on a
				// trill a beat or longer, the note below and then the written note.
				const bool turnAdded = rules.trillTurnAdded && !terminated && n.lengthQuarters >= 0.99;
				int count = std::max(3, (int) std::lround(trilled * rate));
				// THE ALTERNATION ENDS ON THE WRITTEN NOTE — unless a written termination follows,
				// and then on the note above.
				//
				// THE REASON IS THE STRING. A termination is the written note's neighbour and then
				// the written note again, and a trill that has just played the written note leaves
				// that string ringing: struck again a tenth of a second later it barely speaks.
				// Ending above hands the termination a string that is not sounding.
				const int alternation = turnAdded ? count - 2 : count;
				const bool endAbove = terminated;
				// From above, the notes at even places are above; from the note, the odd ones.
				auto aboveAt = [&](int k) { return fromAbove ? (k % 2) == 0 : (k % 2) == 1; };
				int notes = std::max(2, alternation);
				if (aboveAt(notes - 1) != endAbove)
					notes++;
				const int total = notes + (turnAdded ? 2 : 0);
				const double each = trilled / (double) total;
				for (int k = 0; k < notes; k++)
					emit(aboveAt(k) ? above : n.note.midi, each * k, each, k == 0);
				if (turnAdded) {
					emit(below, each * notes, each, false);
					emit(n.note.midi, each * (notes + 1), trilled - each * (notes + 1), false);
				}
				continue;
			}

			int pitches[4];
			int count = 0;
			if (n.note.turn) {
				pitches[0] = above; pitches[1] = n.note.midi;
				pitches[2] = below; pitches[3] = n.note.midi;
				count = 4;
			}
			else if (n.note.invertedTurn) {
				pitches[0] = below; pitches[1] = n.note.midi;
				pitches[2] = above; pitches[3] = n.note.midi;
				count = 4;
			}
			else {
				pitches[0] = n.note.midi;
				pitches[1] = n.note.invertedMordent ? above : below;
				pitches[2] = n.note.midi;
				count = 3;
			}
			const bool mordent = n.note.mordent || n.note.invertedMordent;

			// A TURN PRINTED AFTER THE NOTE: the note first, held, and the four notes of the turn
			// late, just before the next one — never early, which C. P. E. Bach says is what
			// beginners get wrong. They take at most the second half of the note.
			if (n.note.delayedTurn && !mordent) {
				double each = rules.turnSeconds;
				if (each * count > n.lengthSeconds * 0.5)
					each = n.lengthSeconds * 0.5 / count;
				const double figure = each * count;
				emit(n.note.midi, 0.0, n.lengthSeconds - figure, true);
				for (int k = 0; k < count; k++)
					emit(pitches[k], n.lengthSeconds - figure + each * k, each, false);
				continue;
			}

			// A FLICK AND THEN THE NOTE. The ornament never takes more than half of what it is
			// on, so a short note keeps something of itself to be heard as. THE HALF IS SHARED BY
			// THE QUICK NOTES ONLY: a figure ending on the written note holds that last note, so
			// a mordent's half is two notes and not three. Divided by three, the Pathétique's
			// mordents ran at thirty milliseconds a note, half again as fast as its own engraver
			// spelled them out, and three hammer strikes that close sound hard.
			const bool holdsLast = pitches[count - 1] == n.note.midi;
			const int quickNotes = holdsLast ? count - 1 : count;
			double each = mordent ? rules.mordentSeconds : rules.turnSeconds;
			if (each * quickNotes > n.lengthSeconds * 0.5)
				each = n.lengthSeconds * 0.5 / quickNotes;
			// BEFORE THE BEAT, where the style plays them so: the quick notes as a pickup and the
			// written note on the beat, held. Jazz.
			const double early = (rules.figuresBeforeBeat && holdsLast) ? each * quickNotes : 0.0;
			// THE LAST NOTE OF THE FIGURE IS HELD. Every one of these ends on the written note, and
			// a pianist plays that last note and keeps it down for the rest of the length — it is
			// the note the ornament is an ornament of. Played as a note of the figure and then
			// struck again for the rest, the key was hit three times inside a tenth of a second,
			// which in the Pathétique's second theme was heard as a rattle on every mordent.
			double at = -early;
			for (int k = 0; k < count; k++) {
				const bool last = k == count - 1 && holdsLast;
				// The stress on the beat: the first note, or the last where a pickup leads to it.
				emit(pitches[k], at, last ? n.lengthSeconds - at : each, early > 0.0 ? last : k == 0);
				at += each;
			}
			if (holdsLast)
				continue;
			// AND THE REST OF THE NOTE, for a figure that does not end on it.
			if (n.lengthSeconds - at > 0.01)
				emit(n.note.midi, at, n.lengthSeconds - at, false);
		}
		if (any) {
			std::stable_sort(grown.begin(), grown.end(),
				[](const GpPlayedNote& a, const GpPlayedNote& b) {
					return a.startQuarters < b.startQuarters;
				});
			played.swap(grown);
		}
	}

	// ---- rolled chords ---------------------------------------------------------------------
	//
	// HOW LONG A ROLL TAKES, from the style: so far apart a note, times the notes it rolls through,
	// which is how long the whole stroke is — what the cable carries. Every note marked as rolled
	// at the same instant in a part is one roll, whichever staff it is on, since a pianist rolls a
	// chord through both hands. And whether what is struck with it and not rolled lands with its
	// top note, which only some styles' sources say. Only for a score with a style: a Guitar Pro
	// brush keeps the time its file gives it.
	if (song.ornamentStyle >= 0) {
		const OrnamentRules& rules = ornamentRules(song.ornamentStyle);
		for (size_t t = 0; t < out.tracks.size(); t++) {
			std::vector<GpPlayedNote>& played = out.tracks[t];
			for (size_t i = 0; i < played.size(); ) {
				size_t j = i;
				int rolled = 0;
				while (j < played.size() && played[j].startQuarters < played[i].startQuarters + 1e-9) {
					if (played[j].brush != 0)
						rolled++;
					j++;
				}
				if (rolled > 1) {
					const int ms = (int) std::lround(1000.0 * rules.rollSeconds * (rolled - 1));
					for (size_t k = i; k < j; k++) {
						if (played[k].brush == 0)
							continue;
						played[k].brushMs = std::min(255, ms);
						played[k].rollToTop = rules.rollOthersWithTop;
					}
				}
				i = j;
			}
		}
	}

	return true;
}



float gpHarmonyWeight(const GpTimeline& line, const GpSong& song, const GpPlayedNote& n) {
	// LENGTH, AGAINST THE BEAT. A sixteenth is worth a quarter of a quarter note. Anything longer
	// than a beat is capped, since a whole note is not six times the harmony a half note is — it
	// is the same harmony, held.
	float worth = std::fmin(1.5f, std::fmax(0.12f, (float) n.lengthQuarters));

	// WHERE IN THE BAR. A note on the beat is carrying the harmony; one between beats is as
	// likely to be passing through it.
	float where = 0.5f;
	if (n.bar >= 0 && n.bar < (int) line.bars.size()) {
		const double inBar = n.startQuarters - line.bars[(size_t) n.bar].startQuarters;
		float beatLength = 1.f;
		const size_t written = (size_t) line.bars[(size_t) n.bar].written;
		if (written < song.masterBars.size()) {
			const GpMasterBar& m = song.masterBars[written];
			beatLength = 4.f / (float) std::max(1, m.beatUnit);
			if (m.beatUnit >= 8 && m.beatsPerBar % 3 == 0 && m.beatsPerBar > 3)
				beatLength *= 3.f;
		}
		const double beat = inBar / (double) beatLength;
		where = (beat - std::floor(beat + 1e-6) < 1e-4) ? 1.f : 0.45f;
		if (inBar < 1e-4)
			where = 1.25f;      // The downbeat carries more than the other beats.
	}
	return worth * where * std::fmax(0.2f, n.dynamic);
}

} // namespace px


namespace px {

float gpPedalAt(const GpTimeline& line, double quarters, float bpm, int& index) {
	if (line.pedals.empty()) {
		index = -1;
		return 0.f;
	}
	const int last = (int) line.pedals.size() - 1;
	if (index > last)
		index = last;
	if (index < -1)
		index = -1;
	while (index < last && line.pedals[(size_t) index + 1].startQuarters <= quarters)
		index++;
	while (index >= 0 && line.pedals[(size_t) index].startQuarters > quarters)
		index--;
	if (index < 0)
		return 0.f;
	const GpPlayedPedal& p = line.pedals[(size_t) index];
	// THE MIDDLE PEDAL IS NOT THE RIGHT ONE. Sostenuto holds the keys that are already down and
	// nothing struck after them, which nothing downstream can do, so it is carried in the model
	// and never answered here as a sustain it is not.
	if (p.sostenuto)
		return 0.f;
	// A CHANGE IS A RELEASE AND A RETAKE on the spot: the foot comes up for the moment it takes
	// to clear the old sound and goes straight back down under the new note.
	if (p.kind == GpPedalMark::CHANGE) {
		const double lift = 0.05 * (double) std::max(1.f, bpm) / 60.0;   // In quarters.
		if (quarters - p.startQuarters < lift)
			return 0.f;
	}
	return (p.kind == GpPedalMark::UP) ? 0.f : 1.f;
}

}  // namespace px
