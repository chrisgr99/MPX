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
						const std::map<int, size_t>::iterator it = lastOnString.find(key);
						if (it != lastOnString.end() && it->second < played.size()) {
							GpPlayedNote& before = played[it->second];
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
						int before = 0, total = 0;
						for (size_t o = 0; o < beat.notes.size(); o++) {
							if (beat.notes[o].tied)
								continue;
							total++;
							// One is the highest string, so a downstroke starts at the largest
							// number and an upstroke at the smallest.
							const bool earlier = (brush > 0)
								? (beat.notes[o].string > note.string)
								: (beat.notes[o].string < note.string);
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

	// ---- trills -----------------------------------------------------------------------------
	//
	// A TRILL IS WRITTEN OUT AS THE NOTES IT STANDS FOR: the written note and the one a step
	// above it in the key, alternating for the note's whole length. Done here rather than left
	// to an instrument, so that every instrument plays it and none has to know what the sign
	// means — the same reason a rolled chord becomes an order of notes rather than a flag.
	//
	// A STEP IN THE KEY, not a tone and not a semitone. The Nocturne's trill is on F in E flat
	// major, where the step above is G; in another key the same F trills to F sharp. The key
	// signature gives the seven notes and the one above is the next of them.
	//
	// IT STARTS AND ENDS ON THE WRITTEN NOTE, which is what the ear expects of it and what makes
	// the trill sound like an ornament of that note rather than of the pair. So the count of
	// alternations is odd.
	for (size_t t = 0; t < out.tracks.size(); t++) {
		std::vector<GpPlayedNote>& played = out.tracks[t];
		std::vector<GpPlayedNote> grown;
		bool any = false;
		for (size_t i = 0; i < played.size(); i++) {
			const GpPlayedNote& n = played[i];
			if (!n.note.trill || n.lengthSeconds < 2.0 * TRILL_SECONDS || n.note.midi <= 0) {
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
			int above = n.note.midi + 1;
			for (int step = 1; step <= 3; step++) {
				const int pc = ((n.note.midi + step) % 12 + 12) % 12;
				bool inKey = false;
				for (int d = 0; d < 7; d++) {
					if (pc == (tonic + MAJOR[d]) % 12)
						inKey = true;
				}
				if (inKey) {
					above = n.note.midi + step;
					break;
				}
			}
			int count = (int) std::lround(n.lengthSeconds / TRILL_SECONDS);
			if (count < 3)
				count = 3;
			if ((count % 2) == 0)
				count++;
			for (int k = 0; k < count; k++) {
				GpPlayedNote one = n;
				one.note.trill = false;
				one.note.midi = (k % 2) ? above : n.note.midi;
				one.note.tied = false;
				one.startQuarters = n.startQuarters + n.lengthQuarters * (double) k / count;
				one.lengthQuarters = n.lengthQuarters / (double) count;
				one.startSeconds = n.startSeconds + n.lengthSeconds * (double) k / count;
				one.lengthSeconds = n.lengthSeconds / (double) count;
				grown.push_back(one);
			}
		}
		if (any) {
			std::stable_sort(grown.begin(), grown.end(),
				[](const GpPlayedNote& a, const GpPlayedNote& b) {
					return a.startQuarters < b.startQuarters;
				});
			played.swap(grown);
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
