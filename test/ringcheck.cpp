/** HOW MUCH IS SOUNDING AT ONCE, and whether a string is asked to sound two notes together.

A guitar has six strings and can sound six notes. If the played timeline asks for more than that
at a moment, something is ringing that a player's hand would have stopped — either because a note
is written longer than the gap before the next chord, or because a string is left ringing when the
chord moves off it.

Counted from the timeline, in seconds, so it measures what is actually asked for rather than what
any renderer does with it. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace px;

int main(int argc, char** argv) {
	GpSong song;
	std::string why;
	if (!gpRead(argv[1], song, &why)) { std::printf("could not read: %s\n", why.c_str()); return 1; }
	GpTimeline line;
	gpBuildTimeline(song, line, NULL);
	const int track = (argc > 2) ? std::atoi(argv[2]) : 0;
	if (track < 0 || track >= (int) line.tracks.size()) { std::printf("no such track\n"); return 1; }
	const std::vector<GpPlayedNote>& notes = line.tracks[track];
	if (notes.empty()) { std::printf("nothing played\n"); return 1; }

	// Every start and every end, in order: walking them counts what is sounding.
	std::vector<std::pair<double,int> > edges;
	for (size_t i = 0; i < notes.size(); i++) {
		if (notes[i].lengthSeconds <= 0.0) continue;
		edges.push_back(std::make_pair(notes[i].startSeconds, 1));
		edges.push_back(std::make_pair(notes[i].startSeconds + notes[i].lengthSeconds, -1));
	}
	std::sort(edges.begin(), edges.end());
	int now = 0, most = 0;
	double weighted = 0.0, last = edges.empty() ? 0.0 : edges[0].first, over6 = 0.0;
	for (size_t i = 0; i < edges.size(); i++) {
		weighted += now * (edges[i].first - last);
		if (now > 6) over6 += edges[i].first - last;
		last = edges[i].first;
		now += edges[i].second;
		most = std::max(most, now);
	}
	const double span = last - edges[0].first;

	// A STRING SOUNDING TWO NOTES AT ONCE is the thing a hand cannot do.
	long doubled = 0;
	double worstOverlap = 0.0;
	for (int s = 1; s <= 8; s++) {
		std::vector<const GpPlayedNote*> on;
		for (size_t i = 0; i < notes.size(); i++)
			if (notes[i].note.string == s) on.push_back(&notes[i]);
		for (size_t i = 0; i + 1 < on.size(); i++) {
			const double endOf = on[i]->startSeconds + on[i]->lengthSeconds;
			const double overlap = endOf - on[i + 1]->startSeconds;
			if (overlap > 0.001) { doubled++; worstOverlap = std::max(worstOverlap, overlap); }
		}
	}

	std::printf("track %d: %zu notes over %.0f seconds\n", track, notes.size(), span);
	std::printf("   sounding at once: %.1f on average, %d at most\n",
		span > 0 ? weighted / span : 0.0, most);
	std::printf("   more than six sounding: %.1f seconds (%.0f%% of the part)\n",
		over6, span > 0 ? 100.0 * over6 / span : 0.0);
	std::printf("   one string asked to sound two notes at once: %ld times, worst %.2f s\n",
		doubled, worstOverlap);
	return 0;
}
