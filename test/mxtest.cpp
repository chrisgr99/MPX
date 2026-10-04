/** THE MUSICXML READER, CHECKED AGAINST THE WRITER.

We can write a track of a song out as MusicXML and we can read MusicXML back in, so the pair can
be checked against each other without a single file from outside: read a Guitar Pro file, write a
track of it out, read that back, and the notes that come home should be the notes that went.

WHAT IS COMPARED. How many bars, how many notes, and every note's pitch in played order. Not the
loudness — the writer does not put dynamic marks on the page, so there is nothing for the reader
to find — and not the techniques, which are a guitar's business and deliberately not read here.

WHAT A DIFFERENCE MEANS. A note count that does not match is the reader losing or inventing
notes. A pitch that does not match at the same position is worse: the music is wrong rather than
incomplete.

	make mxtest ARGS="a-file.gp4"
	make mxtest ARGS="$HOME/Downloads/*.gp5"
*/
#include "../src/GpMusicXML.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace px;


/** Every note of a track in played order, as pitches, so two readings can be set side by side. */
static std::vector<int> pitchesOf(const GpTrack& track) {
	std::vector<int> out;
	for (size_t b = 0; b < track.bars.size(); b++)
		for (size_t i = 0; i < track.bars[b].beats.size(); i++)
			for (size_t n = 0; n < track.bars[b].beats[i].notes.size(); n++)
				out.push_back(track.bars[b].beats[i].notes[n].midi);
	return out;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: mxtest <file.gp...>\n");
		return 1;
	}
	int failures = 0;
	long tracksDone = 0, notesDone = 0;

	std::printf("%-34s %3s  %-20s %6s %6s %8s %8s\n",
		"file", "trk", "instrument", "bars", "bars2", "notes", "notes2");

	for (int a = 1; a < argc; a++) {
		GpSong song;
		std::string why;
		if (!gpRead(argv[a], song, &why)) {
			std::printf("%-34s  COULD NOT BE READ: %s\n", argv[a], why.c_str());
			failures++;
			continue;
		}
		std::string shown = argv[a];
		const size_t slash = shown.find_last_of('/');
		if (slash != std::string::npos)
			shown = shown.substr(slash + 1);
		if (shown.size() > 33)
			shown = shown.substr(0, 33);

		for (size_t t = 0; t < song.tracks.size(); t++) {
			const std::string xml = gpToMusicXML(song, (int) t, GP_NOTATION, NULL);
			if (xml.empty())
				continue;

			GpSong back;
			std::string trouble;
			if (!mxRead(xml, back, &trouble)) {
				std::printf("%-34s %3d  COULD NOT BE READ BACK: %s\n",
					(t == 0) ? shown.c_str() : "", (int) t, trouble.c_str());
				failures++;
				continue;
			}
			if (back.tracks.empty()) {
				std::printf("%-34s %3d  CAME BACK WITH NO INSTRUMENT\n",
					(t == 0) ? shown.c_str() : "", (int) t);
				failures++;
				continue;
			}

			const GpTrack& was = song.tracks[t];
			const GpTrack& now = back.tracks[0];
			const std::vector<int> before = pitchesOf(was);
			const std::vector<int> after = pitchesOf(now);

			std::string who = was.name;
			if (who.size() > 19)
				who = who.substr(0, 19);

			const bool sameBars = (was.bars.size() == now.bars.size());
			const bool sameCount = (before.size() == after.size());
			int wrongPitch = -1;
			if (sameCount) {
				for (size_t i = 0; i < before.size(); i++) {
					if (before[i] != after[i]) {
						wrongPitch = (int) i;
						break;
					}
				}
			}
			const bool ok = sameBars && sameCount && wrongPitch < 0;

			std::printf("%-34s %3d  %-20s %6d %6d %8d %8d%s\n",
				(t == 0) ? shown.c_str() : "", (int) t, who.c_str(),
				(int) was.bars.size(), (int) now.bars.size(),
				(int) before.size(), (int) after.size(),
				ok ? "" : "   DIFFERENT");
			if (wrongPitch >= 0) {
				std::printf("    note %d came back as %d where it went as %d\n",
					wrongPitch, after[(size_t) wrongPitch], before[(size_t) wrongPitch]);
			}
			if (!ok)
				failures++;
			tracksDone++;
			notesDone += (long) before.size();
		}
	}

	std::printf("\n%ld tracks, %ld notes round-tripped\n", tracksDone, notesDone);
	std::printf("%s\n", failures ? "SOMETHING DID NOT COME HOME — see above"
		: "every note went out and came back the same");
	return failures ? 1 : 0;
}
