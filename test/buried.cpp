/** NOTES THAT ARE PLAYED AND CANNOT BE HEARD.

WHY THERE IS A PROGRAM FOR THIS. Three faults in a row were not notes going missing but notes
being buried: a trill went on alternating over the two small notes that were supposed to finish
it, a dynamic mark failed to apply to the note written on it, and a page of tempo marks was drawn
over the music. In each case the thing was there and something else was on top of it, which is the
hardest kind of fault to hear — you do not notice the absence of something you were not listening
for.

So this looks for the three shapes a buried note takes, over whatever scores it is given.

A NOTE INSIDE ANOTHER OF THE SAME PITCH cannot be heard at all: the string is already sounding and
striking it again in the middle changes nothing a listener can name. That is what the trill was
doing to its termination.

A NOTE STRUCK WHILE THE SAME PITCH IS STILL SOUNDING is weaker but real. Repeated notes are
ordinary music, so this is only worth reporting where the second begins in the first tenth of the
first — which is a doubling, not a repeat.

AND A NOTE FAR QUIETER THAN WHAT SURROUNDS IT is drowned rather than buried. A line that drops
twenty decibels below its neighbours for one note and comes back is usually a fault in how the
loudness was read, not a composer's marking.

What it cannot tell you is whether being buried is WRONG. Two voices meeting on one pitch is
ordinary writing, and a score of three staves will show some of each. It reports what to look at.

	make buried ARGS="$HOME/Downloads/*.mxl"
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;

/** How far into a note another of the same pitch may start before it is a doubling rather than a
repeat, as a fraction of the first. */
static const double DOUBLING = 0.1;
/** How far below its neighbours a note must sit to be called drowned, in decibels. */
static const double DROWNED_DB = 18.0;
/** How much music either side counts as a note's neighbours, in seconds. */
static const double NEARBY = 1.5;

static int barOf(const GpTimeline& line, const GpSong& song, const GpPlayedNote& n) {
	if (n.bar < 0 || n.bar >= (int) line.bars.size())
		return -1;
	const int written = line.bars[(size_t) n.bar].written;
	if (written >= 0 && written < (int) song.masterBars.size()
			&& song.masterBars[(size_t) written].number >= 0)
		return song.masterBars[(size_t) written].number;
	return written + 1;
}

int main(int argc, char** argv) {
	for (int a = 1; a < argc; a++) {
		std::ifstream f(argv[a], std::ios::binary);
		if (!f)
			continue;
		std::stringstream all;
		all << f.rdbuf();
		const std::string text = all.str();
		const std::vector<uint8_t> bytes(text.begin(), text.end());
		GpSong song;
		std::string why;
		const std::string head = text.substr(0, std::min<size_t>(2048, text.size()));
		bool ok = false;
		if (mxLooksLikeMusicXML(head))
			ok = mxReadBytes(bytes, song, &why);
		if (!ok)
			ok = gpReadBytes(bytes, song, &why);
		if (!ok)
			continue;
		GpTimeline line;
		if (!gpBuildTimeline(song, line, &why))
			continue;

		const char* slash = std::strrchr(argv[a], '/');
		std::printf("\n%s\n", slash ? slash + 1 : argv[a]);

		int inside = 0, doubled = 0, drowned = 0, notes = 0;
		std::vector<std::string> show;
		for (size_t t = 0; t < line.tracks.size(); t++) {
			const std::vector<GpPlayedNote>& ns = line.tracks[t];
			notes += (int) ns.size();
			for (size_t i = 0; i < ns.size(); i++) {
				const GpPlayedNote& n = ns[i];
				if (n.note.midi <= 0 || n.lengthSeconds <= 0.0)
					continue;
				const double end = n.startSeconds + n.lengthSeconds;
				// BURIED, by the same pitch already sounding.
				for (size_t j = 0; j < ns.size(); j++) {
					if (j == i || ns[j].note.midi != n.note.midi)
						continue;
					const GpPlayedNote& o = ns[j];
					if (o.lengthSeconds <= 0.0 || o.startSeconds >= n.startSeconds)
						continue;
					const double oEnd = o.startSeconds + o.lengthSeconds;
					if (oEnd < end - 1e-6)
						continue;
					inside++;
					if (show.size() < 40) {
						char line1[160];
						std::snprintf(line1, sizeof line1,
							"    bar %d: note %d at %.2fs lasting %.2fs is inside one from %.2fs"
							" lasting %.2fs", barOf(line, song, n), n.note.midi, n.startSeconds,
							n.lengthSeconds, o.startSeconds, o.lengthSeconds);
						show.push_back(line1);
					}
					break;
				}
				// DOUBLED: the same pitch struck again almost at once.
				for (size_t j = i + 1; j < ns.size(); j++) {
					if (ns[j].startSeconds > n.startSeconds + n.lengthSeconds * DOUBLING)
						break;
					if (ns[j].note.midi == n.note.midi && ns[j].startSeconds > n.startSeconds) {
						doubled++;
						break;
					}
				}
				// DROWNED: far quieter than the music around it.
				double loudest = 0.0;
				for (size_t j = 0; j < ns.size(); j++) {
					if (std::fabs(ns[j].startSeconds - n.startSeconds) > NEARBY)
						continue;
					loudest = std::fmax(loudest, (double) ns[j].dynamic);
				}
				if (n.dynamic > 0.0 && loudest > 0.0) {
					const double down = 20.0 * std::log10(loudest / n.dynamic);
					if (down > DROWNED_DB)
						drowned++;
				}
			}
		}
		std::printf("  %d notes: %d buried inside the same pitch, %d doubled at the same moment,"
			" %d drowned by their neighbours\n", notes, inside, doubled, drowned);
		for (size_t i = 0; i < show.size(); i++)
			std::printf("%s\n", show[i].c_str());
	}
	return 0;
}
