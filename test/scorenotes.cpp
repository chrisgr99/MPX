/** WHAT A SCORE SENDS DOWN THE CABLE, as plain text.

	make scorenotes ARGS="a-piece.mxl 20" > notes.txt

One line per note: when it starts, which key, how hard, how long. The same readers the modules use,
so these are the notes mpxScore would actually send — including the LENGTH, which is the thing it
sends instead of a note-off and which neither piano read until it was found by ear.

WHAT IT IS FOR. test/salamander-release.py plays these through the sampled piano's release engine
outside Rack, which is the only way to check a claim about its damping without a round trip through
somebody listening. Three successive changes to that damping were reported as working when they
were not, and none of them was visible to the compiler. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: scorenotes <file> [seconds]\n");
		return 1;
	}
	const double seconds = (argc > 2) ? std::atof(argv[2]) : 20.0;
	// WITH "marks", the expression written on the score instead of the notes themselves.
	const bool marks = (argc > 3) && std::string(argv[3]) == "marks";

	std::ifstream file(argv[1], std::ios::binary);
	if (!file) {
		std::fprintf(stderr, "could not open %s\n", argv[1]);
		return 1;
	}
	std::stringstream all;
	all << file.rdbuf();
	const std::string text = all.str();
	const std::vector<uint8_t> bytes(text.begin(), text.end());

	GpSong song;
	std::string why;
	const std::string head = text.substr(0, 2048 < text.size() ? 2048 : text.size());
	bool ok = false;
	const bool looksXML = mxLooksLikeMusicXML(head);
	if (looksXML)
		ok = mxReadBytes(bytes, song, &why);
	if (!ok)
		ok = gpReadBytes(bytes, song, looksXML ? NULL : &why);
	if (!ok) {
		std::fprintf(stderr, "could not read it: %s\n", why.c_str());
		return 1;
	}

	GpTimeline line;
	if (!gpBuildTimeline(song, line, &why)) {
		std::fprintf(stderr, "could not lay it out in time: %s\n", why.c_str());
		return 1;
	}

	// EVERY PART TOGETHER, in time order: a piano piece is one instrument however many staves it
	// is written on.
	struct Hit {
		double at, lasts;
		int midi;
		float level;
	};
	std::vector<Hit> hits;
	for (size_t t = 0; t < line.tracks.size(); t++) {
		for (const GpPlayedNote& n : line.tracks[t]) {
			if (n.startSeconds > seconds)
				break;
			// A TIED NOTE CONTINUES THE ONE BEFORE IT rather than being struck again, which is the
			// difference between a held minim and two crotchets hammered twice.
			if (n.note.tied)
				continue;
			hits.push_back(Hit{n.startSeconds, (n.lengthSeconds > 0.05) ? n.lengthSeconds : 0.05,
				n.note.midi, n.dynamic});
		}
	}
	std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });

	std::fprintf(stderr, "%s: %d notes in the first %.0f seconds of %.0f\n",
		song.title.empty() ? argv[1] : song.title.c_str(), (int) hits.size(), seconds,
		line.seconds);

	// WHAT ELSE IS WRITTEN ON THE SCORE, asked for by name: the marks, in time order against the
	// notes, so a pedal can be read against the bar it was meant for and an accent against the
	// note it sits over. Counted over the whole piece, listed over the window.
	if (marks) {
		int down = 0, up = 0, change = 0, sost = 0;
		for (const GpPlayedPedal& p : line.pedals) {
			if (p.sostenuto) sost++;
			if (p.kind == GpPedalMark::DOWN) down++;
			else if (p.kind == GpPedalMark::UP) up++;
			else change++;
		}
		int accents = 0, heavy = 0, staccato = 0, tenuto = 0, slurred = 0, rolled = 0;
		float lowest = 2.f, highest = 0.f;
		for (size_t t = 0; t < line.tracks.size(); t++)
			for (const GpPlayedNote& n : line.tracks[t]) {
				if (n.note.accent) accents++;
				if (n.note.heavyAccent) heavy++;
				if (n.note.staccato) staccato++;
				if (n.note.tenuto) tenuto++;
				if (n.note.slurred) slurred++;
				if (n.brush != 0) rolled++;
				lowest = std::min(lowest, n.dynamic);
				highest = std::max(highest, n.dynamic);
			}
		std::fprintf(stderr,
			"  pedal: %d down, %d up, %d change, %d of them the middle pedal\n"
			"  notes: %d accented, %d heavily, %d staccato, %d tenuto, %d slurred, %d rolled\n"
			"  loudness runs from %.2f to %.2f\n",
			down, up, change, sost, accents, heavy, staccato, tenuto, slurred, rolled,
			lowest, highest);

		struct Mark { double at; std::string what; };
		std::vector<Mark> list;
		// THE BAR LINES AND THE BEATS, because where a mark falls in the metre is half of what it
		// means: a pedal change on a bar line and one in the middle of a bar are different things.
		for (size_t i = 0; i < line.bars.size(); i++) {
			const GpPlayedBar& b = line.bars[i];
			if (b.startSeconds > seconds)
				break;
			char head[96];
			std::snprintf(head, sizeof head, "bar %d pass %d", b.written + 1, b.pass);
			list.push_back(Mark{b.startSeconds, head});
			const int beats = (b.written < (int) song.masterBars.size())
				? song.masterBars[(size_t) b.written].beatsPerBar : 4;
			for (int k = 1; k < beats && b.quarters > 0.0; k++) {
				const double at = b.startSeconds + b.seconds * (double) k / (double) beats;
				if (at <= seconds)
					list.push_back(Mark{at, "beat"});
			}
		}
		for (const GpPlayedPedal& p : line.pedals) {
			if (p.startSeconds > seconds)
				break;
			const char* k = (p.kind == GpPedalMark::DOWN) ? "down"
				: (p.kind == GpPedalMark::UP) ? "up" : "change";
			list.push_back(Mark{p.startSeconds,
				std::string(p.sostenuto ? "sostenuto " : "pedal ") + k});
		}
		for (size_t t = 0; t < line.tracks.size(); t++)
			for (const GpPlayedNote& n : line.tracks[t]) {
				if (n.startSeconds > seconds)
					break;
				std::string what;
				if (n.note.accent) what += " accent";
				if (n.note.heavyAccent) what += " strong-accent";
				if (n.note.staccato) what += " staccato";
				if (n.note.tenuto) what += " tenuto";
				if (n.note.slurred) what += " slurred";
				if (n.brush != 0) what += " rolled";
				if (what.empty())
					continue;
				char head[64];
				std::snprintf(head, sizeof head, "note %d at %.2f:", n.note.midi, n.dynamic);
				list.push_back(Mark{n.startSeconds, std::string(head) + what});
			}
		// THE FOOT ITSELF, which is what mpxScore publishes and the piano reads: the walk the
		// module does, run here at a hundred points a second, where it can be checked. A module
		// cannot be tested, so the walk does not live in one.
		{
			int index = -1, moves = 0;
			float was = 0.f;
			double longest = 0.0, held = 0.0;
			const double step = 0.01;
			for (double q = 0.0; q < line.quarters; q += step) {
				const float down = gpPedalAt(line, q, 120.f, index);
				if (down != was) {
					moves++;
					if (down > 0.5f)
						held = 0.0;
					else
						longest = std::max(longest, held);
					was = down;
				} else if (down > 0.5f)
					held += step;
			}
			// AND THE SAME WALK BACKWARDS, because a score can be sent back to its beginning and
			// the index has to walk back with it rather than stick.
			int back = (int) line.pedals.size();
			int disagreements = 0;
			for (double q = line.quarters; q > 0.0; q -= step) {
				int fresh = -1;
				if (gpPedalAt(line, q, 120.f, back) != gpPedalAt(line, q, 120.f, fresh))
					disagreements++;
			}
			std::fprintf(stderr,
				"  the foot moves %d times over the piece, longest hold %.1f quarters;"
				" walking backwards disagrees %d times\n", moves, longest, disagreements);
		}

		std::stable_sort(list.begin(), list.end(),
			[](const Mark& a, const Mark& b) { return a.at < b.at; });
		for (const Mark& m : list)
			std::printf("%.6f  %s\n", m.at, m.what.c_str());
		return 0;
	}

	for (const Hit& h : hits)
		std::printf("%.6f %d %.4f %.6f\n", h.at, h.midi, h.level, h.lasts);
	return 0;
}
