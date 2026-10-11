#include "Pedalling.hpp"

#include <algorithm>
#include <cmath>

namespace px {

namespace {

/** HOW ROUGH TWO PITCH CLASSES SOUND TOGETHER, by the interval between them, nought to one. A
semitone is the worst there is; a whole tone and a tritone are half as bad; the thirds and sixths
are close to clean, and the fourth, the fifth and the unison are clean. What a pedal held through a
chord change smears is exactly these: the old chord's notes a step from the new one's. */
double roughness(int a, int b) {
	int d = std::abs(a - b) % 12;
	if (d > 6)
		d = 12 - d;
	static const double BY_CLASS[7] = {0.0, 1.0, 0.5, 0.15, 0.1, 0.0, 0.5};
	return BY_CLASS[d];
}

/** HOW MUCH WHAT IS STRUCK CLASHES WITH WHAT IS RINGING: the roughness of every pair, one
ringing and one new, against the louder of the two, squared — so a faint ring under a loud chord, or
one quiet note over a held one, counts for little. With `onlyNew`, a pitch already ringing adds
nothing when it is struck again unless it is struck at three times the ring. */
double clashOf(const double* ring, const double* fresh, bool onlyNew) {
	double ringSum = 0.0, freshSum = 0.0, rough = 0.0;
	for (int i = 0; i < 12; i++) {
		ringSum += ring[i];
		freshSum += fresh[i];
	}
	for (int j = 0; j < 12; j++) {
		const double struck = onlyNew ? std::max(0.0, fresh[j] - 3.0 * ring[j]) : fresh[j];
		if (struck <= 0.0)
			continue;
		for (int i = 0; i < 12; i++)
			rough += ring[i] * struck * roughness(i, j);
	}
	const double scale = std::max(ringSum, freshSum);
	return (scale > 1e-9) ? rough / (scale * scale) : 0.0;
}

/** HOW MUCH OF THE SOUND A NOTE IS: how hard it was struck, and how low it is. A low string is
louder for longer and its partials crowd everything above it, so the bass is what a held pedal
muddies first; a twelfth higher, a note weighs a quarter as much. */
double weightOf(const GpPlayedNote& n) {
	const double level = std::max(0.2, (double) n.dynamic);
	const double low = std::pow(2.0, (60.0 - (double) n.note.midi) / 12.0);
	return level * std::min(3.0, std::max(0.15, low));
}

/** HOW LONG A STRING TAKES TO FADE, roughly, in seconds to a third of where it started: about three
seconds an octave below middle C, longer below and shorter above. */
double fadeOf(int midi) {
	return std::min(8.0, std::max(0.5, 3.0 * std::pow(2.0, (48.0 - (double) midi) / 18.0)));
}

/** THE LENGTH OF A BEAT IN A BAR, in quarters: the lower number of the signature, and in six-eight
and its kind the dotted quarter, which is the beat anyone counts. */
double beatLengthOf(const GpSong& song, const GpTimeline& line, size_t bar) {
	const size_t written = (size_t) line.bars[bar].written;
	if (written >= song.masterBars.size())
		return 1.0;
	const GpMasterBar& m = song.masterBars[written];
	double length = 4.0 / (double) std::max(1, m.beatUnit);
	if (m.beatUnit >= 8 && m.beatsPerBar % 3 == 0 && m.beatsPerBar > 3)
		length *= 3.0;
	return length;
}

/** A beat the foot can change on: where it is, how long, and where it falls in its bar. */
struct Beat {
	double at = 0.0;
	double length = 1.0;
	enum Place { FIRST, HALF, OTHER } place = OTHER;
	/** Every note in its bar is staccato: the bar is played dry. */
	bool detached = false;
};

/** A note ringing under the pedal: what it is, how loud it started, when, and when its key comes up. */
struct Ringing {
	int midi = 60;
	double weight = 0.0;
	double struck = 0.0;     /**< Seconds. */
	double keyUp = 0.0;      /**< Quarters: the written end. */
};

/** LIFTED AND TAKEN AGAIN AT THE SAME MOMENT IS A CHANGE, as gpBuildTimeline has it for the marks. */
std::vector<GpPlayedPedal> joined(const std::vector<GpPlayedPedal>& moves) {
	std::vector<GpPlayedPedal> out;
	out.reserve(moves.size());
	for (size_t i = 0; i < moves.size(); i++) {
		const bool joins = i + 1 < moves.size()
			&& moves[i].kind == GpPedalMark::UP
			&& moves[i + 1].kind != GpPedalMark::UP
			&& moves[i].sostenuto == moves[i + 1].sostenuto
			&& moves[i + 1].startQuarters - moves[i].startQuarters < 1e-6;
		if (joins) {
			GpPlayedPedal p = moves[i + 1];
			p.kind = GpPedalMark::CHANGE;
			out.push_back(p);
			i++;
		}
		else
			out.push_back(moves[i]);
	}
	return out;
}

} // namespace


bool gpPedalledTrack(const GpTrack& track) {
	// THE PIANOS OF GENERAL MIDI: grand, bright, electric grand, honky-tonk and the two electric
	// pianos. Six and seven are the harpsichord and the clavinet.
	return !track.percussion && track.midiProgram >= 0 && track.midiProgram <= 5;
}


std::vector<GpPlayedPedal> gpPedalPassage(const GpSong& song, const GpTimeline& line, double from,
                                          double to, const PedalRule& rule) {
	std::vector<GpPlayedPedal> out;
	if (line.bars.empty() || to <= from)
		return out;

	// THE NOTES STRUCK IN THE PASSAGE, from every pedalled track: a tied note is the one before it
	// still sounding, and a dead one has no pitch to ring.
	std::vector<const GpPlayedNote*> notes;
	for (size_t t = 0; t < line.tracks.size() && t < song.tracks.size(); t++) {
		if (!gpPedalledTrack(song.tracks[t]))
			continue;
		for (const GpPlayedNote& n : line.tracks[t]) {
			if (n.note.tied || n.note.dead)
				continue;
			if (n.startQuarters >= from - 1e-9 && n.startQuarters < to - 1e-9)
				notes.push_back(&n);
		}
	}
	if (notes.empty())
		return out;
	std::stable_sort(notes.begin(), notes.end(), [](const GpPlayedNote* a, const GpPlayedNote* b) {
		return a->startQuarters < b->startQuarters;
	});

	// THE BEATS, and where each falls: the first of the bar, half way through a bar of an even
	// number of beats, or anywhere else.
	std::vector<Beat> beats;
	for (size_t i = 0; i < line.bars.size(); i++) {
		const GpPlayedBar& bar = line.bars[i];
		if (bar.startQuarters >= to || bar.startQuarters + bar.quarters <= from)
			continue;
		const double length = beatLengthOf(song, line, i);
		const int count = std::max(1, (int) std::lround(bar.quarters / length));
		// STACCATO THROUGH THE WHOLE BAR is a bar played dry. A staccato beat inside a pedalled bar
		// is not: a waltz marks its chords on the second and third beats short and pedals them
		// with the bass on the first.
		bool struck = false, detached = true;
		for (const GpPlayedNote* n : notes)
			if (n->startQuarters >= bar.startQuarters - 1e-9
					&& n->startQuarters < bar.startQuarters + bar.quarters - 1e-9) {
				struck = true;
				detached = detached && n->note.staccato;
			}
		for (int k = 0; k < count; k++) {
			Beat b;
			b.at = bar.startQuarters + k * length;
			if (b.at < from - 1e-9 || b.at >= to - 1e-9)
				continue;
			b.length = std::min(length, bar.startQuarters + bar.quarters - b.at);
			b.place = (k == 0) ? Beat::FIRST
				: (count % 2 == 0 && k == count / 2) ? Beat::HALF : Beat::OTHER;
			b.detached = struck && detached;
			beats.push_back(b);
		}
	}

	auto move = [&](double quarters, GpPedalMark::Kind kind) {
		GpPlayedPedal p;
		p.startQuarters = quarters;
		p.startSeconds = gpSecondsAt(line, quarters);
		p.kind = kind;
		p.generated = true;
		out.push_back(p);
	};

	bool down = false;
	double heldSince = 0.0;              // Seconds: when the pedal last went down or changed.
	double soundEnds = from;             // Quarters: the latest written end of anything struck.
	std::vector<Ringing> ringing;        // Everything struck since then.
	size_t next = 0;

	for (size_t bi = 0; bi < beats.size(); bi++) {
		const Beat& b = beats[bi];
		const double end = b.at + b.length;
		// WHAT THIS BEAT STRIKES: notes from here to the next beat. An upbeat struck before the
		// first beat of the passage belongs to the first.
		size_t last = next;
		while (last < notes.size() && notes[last]->startQuarters < end - 1e-9)
			last++;

		// A REST IN EVERY VOICE: nothing struck in the beat and nothing written to sound into it.
		// The pedal comes up where the sound stops, so the rest is heard as one.
		if (last == next && soundEnds <= b.at + 1e-9) {
			if (down) {
				double at = std::max(soundEnds, from);
				if (!out.empty())
					at = std::max(at, out.back().startQuarters);
				move(at, GpPedalMark::UP);
				down = false;
				ringing.clear();
			}
			continue;
		}
		if (last == next)
			continue;      // Something is still sounding from before; nothing new to decide on.

		const double firstStrike = notes[next]->startQuarters;
		const double nowSeconds = gpSecondsAt(line, std::max(b.at, firstStrike));

		// STACCATO: the pedal would undo the very thing the mark asks for.
		if (b.detached) {
			if (down) {
				move(std::max(b.at, firstStrike), GpPedalMark::UP);
				down = false;
				ringing.clear();
			}
		}
		else if (!down) {
			// DOWN WITH THE FIRST NOTE, so it is caught and held from its start.
			move(firstStrike, GpPedalMark::DOWN);
			down = true;
			heldSince = nowSeconds;
			ringing.clear();
		}
		else {
			// WHAT IS RINGING, faded by how long ago it was struck: all of it, and what rings only
			// because the pedal is down. A note whose key is still held sounds whatever the foot
			// does, so lifting would not clear it.
			double ring[12] = {0}, ringUp[12] = {0}, fresh[12] = {0};
			for (const Ringing& r : ringing) {
				const double age = std::max(0.0, nowSeconds - r.struck);
				const double left = r.weight * std::exp(-age / fadeOf(r.midi));
				const int pc = ((r.midi % 12) + 12) % 12;
				ring[pc] += left;
				if (r.keyUp <= b.at + 1e-9)
					ringUp[pc] += left;
			}
			// A SHORT NOTE IS LESS OF THE HARMONY than a long one: a passing note is gone before it
			// is heard as part of the chord.
			for (size_t k = next; k < last; k++) {
				const GpPlayedNote& n = *notes[k];
				const double worth = 0.35 + 0.65 * std::min(1.0, n.lengthQuarters / b.length);
				fresh[((n.note.midi % 12) + 12) % 12] += weightOf(n) * worth;
			}
			// A BASS OF ITS OWN: the beat's lowest note at or below anything left ringing with its key
			// up, give or take a tone. Lifting then clears the old sound and loses nothing under it.
			int lowestRinging = 1000, lowestStruck = 1000;
			for (const Ringing& r : ringing)
				if (r.keyUp <= b.at + 1e-9)
					lowestRinging = std::min(lowestRinging, r.midi);
			for (size_t k = next; k < last; k++)
				lowestStruck = std::min(lowestStruck, notes[k]->note.midi);
			const bool ownBass = lowestStruck <= lowestRinging + 2;
			const double clashNew = clashOf(ring, fresh, true);
			const double clashPairs = clashOf(ringUp, fresh, false);
			const double held = nowSeconds - heldSince;
			bool change = false;
			if (b.place == Beat::FIRST)
				change = clashNew > rule.firstNew || clashPairs > rule.firstPairs
					|| (rule.firstOwnBass && ownBass) || held >= rule.longest;
			else if (b.place == Beat::HALF)
				change = clashNew > rule.halfNew && held >= rule.halfHeld
					&& (ownBass || !rule.halfOwnBass);
			else
				change = clashNew > rule.otherNew && held >= rule.otherHeld
					&& (ownBass || !rule.otherOwnBass);
			if (change) {
				move(b.at, GpPedalMark::CHANGE);
				heldSince = nowSeconds;
				// What the fingers still hold rings on through the change.
				std::vector<Ringing> kept;
				for (const Ringing& r : ringing)
					if (r.keyUp > b.at + 1e-9)
						kept.push_back(r);
				ringing.swap(kept);
			}
		}

		for (size_t k = next; k < last; k++) {
			const GpPlayedNote& n = *notes[k];
			soundEnds = std::max(soundEnds, n.startQuarters + n.lengthQuarters);
			if (!down)
				continue;
			Ringing r;
			r.midi = n.note.midi;
			r.weight = weightOf(n);
			r.struck = gpSecondsAt(line, n.startQuarters);
			r.keyUp = n.startQuarters + n.lengthQuarters;
			ringing.push_back(r);
		}
		next = last;
	}
	if (down)
		move(std::max(to, out.empty() ? to : out.back().startQuarters), GpPedalMark::UP);
	return out;
}


void gpFillPedalling(const GpSong& song, GpTimeline& line, const PedalRule& rule) {
	line.pedalsFilled = line.pedals;
	bool pedalled = false;
	for (size_t t = 0; t < song.tracks.size() && t < line.tracks.size(); t++)
		pedalled = pedalled || gpPedalledTrack(song.tracks[t]);
	line.pedalsFilledHere = pedalled && !line.bars.empty();
	if (!line.pedalsFilledHere)
		return;

	// WHERE THE MARKS LEAVE THE PEDAL UP: from each lift to the next time it goes down, and from
	// the start and to the end.
	std::vector<std::pair<double, double> > gaps;
	bool down = false;
	double upSince = 0.0;
	for (const GpPlayedPedal& p : line.pedals) {
		if (p.sostenuto)
			continue;
		if (p.kind == GpPedalMark::UP) {
			if (down) {
				down = false;
				upSince = p.startQuarters;
			}
		}
		else if (!down) {
			gaps.push_back(std::make_pair(upSince, p.startQuarters));
			down = true;
		}
	}
	if (!down)
		gaps.push_back(std::make_pair(upSince, line.quarters));

	std::vector<GpPlayedPedal> all = line.pedals;
	for (const std::pair<double, double>& gap : gaps) {
		// LONG ENOUGH TO BE UNMARKED rather than lifted on purpose, in the bars where it starts.
		double barLength = 4.0;
		for (const GpPlayedBar& bar : line.bars)
			if (bar.startQuarters <= gap.first + 1e-9) {
				barLength = bar.quarters;
			}
			else
				break;
		if (gap.second - gap.first < rule.unmarkedBars * barLength - 1e-6)
			continue;
		const std::vector<GpPlayedPedal> moves = gpPedalPassage(song, line, gap.first, gap.second, rule);
		all.insert(all.end(), moves.begin(), moves.end());
	}
	// IN TIME ORDER, and where two moves fall together the lift first, so that a lift meeting the
	// next mark's pedal joins with it into a change.
	std::stable_sort(all.begin(), all.end(), [](const GpPlayedPedal& a, const GpPlayedPedal& b) {
		if (a.startQuarters != b.startQuarters)
			return a.startQuarters < b.startQuarters;
		return (a.kind == GpPedalMark::UP) && (b.kind != GpPedalMark::UP);
	});
	line.pedalsFilled = joined(all);
}


} // namespace px
