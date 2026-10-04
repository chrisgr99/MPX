/** GUITAR PRO FILES WRITTEN OUT AS MUSICXML, AND WHAT CAME OF IT.

Reads each file given, converts every track of it, and reports what the conversion had to guess
at. Writes one track's document out so it can be read, or engraved by scorerender.

The checks are the ones that catch a converter being wrong rather than merely invalid. A bar that
does not add up is the commonest fault and the one that shows as a gap on the page, so every bar
of every track is added up against its time signature. A length that is not any ordinary note is
counted, because those are where the written form had to be guessed.

	make gpxmltest ARGS="a-file.gp5"
	make gpxmltest ARGS="-o out.musicxml -t 2 a-file.gp5"     write track 2 out
*/
#include "../src/GpMusicXML.hpp"
#include "../src/GuitarPro.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace px;


/** Every length in the song put through the figuring, to see which have no ordinary written
form. Reported per track, since one bad track in twenty is worth knowing about. */
static int inexactLengths(const GpTrack& track, float* worst) {
	int bad = 0;
	*worst = 0.f;
	for (size_t b = 0; b < track.bars.size(); b++) {
		for (size_t i = 0; i < track.bars[b].beats.size(); i++) {
			const float q = track.bars[b].beats[i].quarters;
			const GpFigure f = gpNoteFigure(q);
			if (!f.exact) {
				bad++;
				if (q > *worst)
					*worst = q;
			}
		}
	}
	return bad;
}


/** WHETHER EACH BAR ADDS UP.

A bar holds what its time signature says it holds. Guitar Pro's voices are laid on one timeline by
the reader, so a bar with two voices counts twice over — each voice is measured on its own, the
way the converter writes them, and the longest is the bar's length. A bar that comes out longer
than its signature is one Verovio will draw running past its barline; shorter, and there is a gap.

Pickup bars and the last bar are genuinely short in plenty of real transcriptions, so a short bar
is counted separately from a long one: a long bar is a fault, a short one is usually the music. */
static void measureBars(const GpSong& song, const GpTrack& track, int* longBars, int* shortBars) {
	*longBars = 0;
	*shortBars = 0;
	const size_t bars = std::min(track.bars.size(), song.masterBars.size());
	for (size_t b = 0; b < bars; b++) {
		const GpMasterBar& master = song.masterBars[b];
		const float should = (float) master.beatsPerBar * 4.f / (float) master.beatUnit;

		// A SILENT BAR IS A WHOLE REST however long the bar is, and the converter writes it as a
		// measure rest — so it is not measured against the signature here either. See
		// gpToMusicXML.
		if (track.bars[b].beats.size() == 1
				&& (track.bars[b].beats[0].rest || track.bars[b].beats[0].notes.empty())) {
			continue;
		}

		// The same rule the converter splits voices by — see splitVoices.
		float voice = 0.f, longest = 0.f, last = -1.f;
		bool any = false;
		for (size_t i = 0; i < track.bars[b].beats.size(); i++) {
			const GpBeat& beat = track.bars[b].beats[i];
			const bool grace = (beat.grace != 0);
			if (!grace && any && beat.start <= last + 1e-4f) {
				longest = std::max(longest, voice);
				voice = 0.f;
			}
			if (!grace) {
				voice += beat.quarters;
				last = beat.start;
				any = true;
			}
		}
		longest = std::max(longest, voice);
		if (longest > should + 0.01f)
			(*longBars)++;
		else if (longest < should - 0.01f)
			(*shortBars)++;
	}
}


int main(int argc, char** argv) {
	std::string out;
	int only = -1;
	GpStaves staves = GP_NOTATION;
	int first = 1;
	while (first < argc && argv[first][0] == '-') {
		if (!std::strcmp(argv[first], "-o") && first + 1 < argc) {
			out = argv[first + 1];
			first += 2;
		}
		else if (!std::strcmp(argv[first], "-t") && first + 1 < argc) {
			only = std::atoi(argv[first + 1]);
			first += 2;
		}
		// Which staves to write: notation, tablature, or both one above the other.
		else if (!std::strcmp(argv[first], "-tab")) {
			staves = GP_TAB;
			first += 1;
		}
		else if (!std::strcmp(argv[first], "-both")) {
			staves = GP_BOTH;
			first += 1;
		}
		else {
			break;
		}
	}
	if (first >= argc) {
		std::printf("usage: gpxmltest [-o out.musicxml] [-t track] [-tab|-both] <file.gp...>\n");
		return 1;
	}

	int failures = 0;
	std::printf("%-34s %3s  %-26s %5s %7s %6s %6s %8s\n",
		"file", "trk", "instrument", "bars", "notes", "long", "short", "guessed");

	for (int a = first; a < argc; a++) {
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
			if (only >= 0 && (int) t != only)
				continue;
			const GpTrack& track = song.tracks[t];

			std::string trouble;
			const std::string xml = gpToMusicXML(song, (int) t, staves, &trouble);
			if (xml.empty()) {
				std::printf("%-34s %3d  NOTHING WRITTEN\n", shown.c_str(), (int) t);
				failures++;
				continue;
			}

			long notes = 0;
			for (size_t b = 0; b < track.bars.size(); b++)
				for (size_t i = 0; i < track.bars[b].beats.size(); i++)
					notes += (long) track.bars[b].beats[i].notes.size();

			int longBars = 0, shortBars = 0;
			measureBars(song, track, &longBars, &shortBars);
			float worst = 0.f;
			const int guessed = inexactLengths(track, &worst);

			std::string who = track.name;
			if (who.size() > 25)
				who = who.substr(0, 25);
			std::printf("%-34s %3d  %-26s %5d %7ld %6d %6d %8d%s\n",
				(t == 0) ? shown.c_str() : "", (int) t, who.c_str(),
				(int) track.bars.size(), notes, longBars, shortBars, guessed,
				longBars ? "   BARS OVERFULL" : "");
			if (longBars)
				failures++;

			if (!out.empty()) {
				std::ofstream file(out.c_str());
				file << xml;
				std::printf("    wrote %s (%zu bytes)\n", out.c_str(), xml.size());
				out.clear();
			}
		}
	}

	std::printf("\n%s\n", failures ? "SOMETHING IS WRONG — see above"
		: "every track converted, every bar adds up");
	return failures ? 1 : 0;
}
