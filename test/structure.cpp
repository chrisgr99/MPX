/** THE SHAPE OF A SCORE, SO IT CAN BE TALKED ABOUT.

Marking a score up means saying where a marking goes, and that needs both people looking at the
same map: bar numbers, which voice, where a line enters. This prints that map.

WHAT IT SHOWS. The bars, with the metre and key where they change and whatever repeats, endings
and jump marks they carry. What each voice is doing in each bar. Where a voice ENTERS — comes in
after silence — which in a fugue is where the subject arrives and is the first thing anybody marks.
And what expression the score already carries, which decides how much wants adding.

	make structure ARGS="a-score.musicxml"
	make structure ARGS="a-score.musicxml 0"      one part in detail
*/
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


/** A pitch as a musician says it: C4 is middle C. */
static std::string named(int midi) {
	static const char* NAMES[12] =
		{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
	char out[16];
	std::snprintf(out, sizeof(out), "%s%d", NAMES[((midi % 12) + 12) % 12], midi / 12 - 1);
	return out;
}


/** The beats of a bar split back into the voices they were laid down as: within one voice every
beat begins strictly after the one before it, so a beat that does not is the next voice's first. */
static std::vector<std::vector<const GpBeat*> > voicesOf(const GpBar& bar) {
	std::vector<std::vector<const GpBeat*> > out;
	float last = -1.f;
	bool any = false;
	for (size_t i = 0; i < bar.beats.size(); i++) {
		const GpBeat& beat = bar.beats[i];
		const bool grace = (beat.grace != 0);
		if (out.empty() || (!grace && any && beat.start <= last + 1e-4f))
			out.push_back(std::vector<const GpBeat*>());
		out.back().push_back(&beat);
		if (!grace) {
			last = beat.start;
			any = true;
		}
	}
	return out;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: structure <score> [part]\n");
		return 1;
	}
	std::ifstream file(argv[1], std::ios::binary);
	if (!file) { std::printf("could not open it\n"); return 1; }
	std::stringstream all;
	all << file.rdbuf();
	const std::string text = all.str();
	const std::vector<uint8_t> bytes(text.begin(), text.end());

	GpSong song;
	std::string why;
	const std::string head = text.substr(0, text.size() < 2048 ? text.size() : 2048);
	bool ok = false;
	if (mxLooksLikeMusicXML(head))
		ok = mxReadBytes(bytes, song, &why);
	if (!ok)
		ok = gpReadBytes(bytes, song, &why);
	if (!ok) { std::printf("could not read it: %s\n", why.c_str()); return 1; }

	std::printf("%s", song.title.empty() ? "(no title)" : song.title.c_str());
	if (!song.artist.empty())
		std::printf(" — %s", song.artist.c_str());
	std::printf("\n%d bars, %d parts\n", (int) song.masterBars.size(), (int) song.tracks.size());

	// ---- what expression it already carries -----------------------------------------------------
	//
	// THE NUMBER THAT DECIDES EVERYTHING ELSE. A score with marks of its own wants them honoured
	// and little added; a score with none has to be given a reading outright.
	long dynamics = 0, staccato = 0, accent = 0, tenuto = 0, slurs = 0;
	float quietest = 2.f, loudest = -1.f;
	for (size_t t = 0; t < song.tracks.size(); t++) {
		const GpTrack& k = song.tracks[t];
		for (size_t b = 0; b < k.bars.size(); b++) {
			for (size_t i = 0; i < k.bars[b].beats.size(); i++) {
				const GpBeat& e = k.bars[b].beats[i];
				quietest = std::min(quietest, e.dynamic);
				loudest = std::max(loudest, e.dynamic);
				for (size_t n = 0; n < e.notes.size(); n++) {
					if (e.notes[n].staccato) staccato++;
					if (e.notes[n].accent || e.notes[n].heavyAccent) accent++;
					if (e.notes[n].tenuto) tenuto++;
					if (e.notes[n].slurred) slurs++;
				}
			}
		}
	}
	if (loudest - quietest > 0.001f)
		dynamics = 1;
	std::printf("expression it carries: loudness %s, %ld staccato, %ld accents, %ld tenuto, "
		"%ld slurred\n", dynamics ? "varies" : "never changes",
		staccato, accent, tenuto, slurs);
	if (!dynamics && !staccato && !accent && !slurs)
		std::printf("   nothing at all — every note is played the same way\n");

	const int only = (argc > 2) ? std::atoi(argv[2]) : -1;

	for (size_t t = 0; t < song.tracks.size(); t++) {
		if (only >= 0 && (int) t != only)
			continue;
		const GpTrack& k = song.tracks[t];
		std::printf("\npart %zu: %s\n", t,
			k.name.empty() ? "(unnamed)" : k.name.c_str());

		int beatsWas = -1, unitWas = -1, keyWas = -999;
		std::vector<bool> sounding;
		for (size_t b = 0; b < k.bars.size() && b < song.masterBars.size(); b++) {
			const GpMasterBar& m = song.masterBars[b];
			const std::vector<std::vector<const GpBeat*> > voices = voicesOf(k.bars[b]);

			std::string said;
			char buf[256];
			if (m.beatsPerBar != beatsWas || m.beatUnit != unitWas) {
				std::snprintf(buf, sizeof(buf), " %d/%d", m.beatsPerBar, m.beatUnit);
				said += buf;
				beatsWas = m.beatsPerBar;
				unitWas = m.beatUnit;
			}
			if (m.keyAccidentals != keyWas) {
				std::snprintf(buf, sizeof(buf), " key %+d%s",
					m.keyAccidentals, m.minor ? " minor" : "");
				said += buf;
				keyWas = m.keyAccidentals;
			}
			if (m.repeatOpen) said += " |:";
			if (m.repeatCount > 0) {
				std::snprintf(buf, sizeof(buf), " :|x%d", m.repeatCount);
				said += buf;
			}
			if (!m.endings.empty()) said += " [ending]";
			for (size_t i = 0; i < m.marks.size(); i++) {
				said += " ";
				said += m.marks[i];
			}
			if (!m.section.empty()) { said += " \""; said += m.section; said += "\""; }

			// WHAT EACH VOICE IS DOING, and whether it has just come in.
			std::string lines;
			sounding.resize(std::max(sounding.size(), voices.size()), false);
			for (size_t v = 0; v < voices.size(); v++) {
				long notes = 0;
				int lowest = 127, highest = 0, firstNote = -1;
				for (size_t i = 0; i < voices[v].size(); i++) {
					const GpBeat& e = *voices[v][i];
					for (size_t n = 0; n < e.notes.size(); n++) {
						notes++;
						lowest = std::min(lowest, e.notes[n].midi);
						highest = std::max(highest, e.notes[n].midi);
						if (firstNote < 0)
							firstNote = e.notes[n].midi;
					}
				}
				const bool wasSounding = (v < sounding.size()) ? sounding[v] : false;
				if (v >= sounding.size())
					sounding.resize(v + 1, false);
				sounding[v] = (notes > 0);
				std::snprintf(buf, sizeof(buf), "  v%d:", (int) v + 1);
				lines += buf;
				if (notes == 0) {
					lines += " —";
					continue;
				}
				std::snprintf(buf, sizeof(buf), " %2ld notes %s..%s", notes,
					named(lowest).c_str(), named(highest).c_str());
				lines += buf;
				// AN ENTRY: a voice that was silent and now is not. In a fugue this is where
				// the subject arrives, and it is the first thing anybody marks.
				if (!wasSounding && firstNote >= 0) {
					std::snprintf(buf, sizeof(buf), "  ENTERS on %s", named(firstNote).c_str());
					lines += buf;
				}
			}
			std::printf("bar %3d%-22s%s\n", (int) b + 1, said.c_str(), lines.c_str());
		}
	}
	return 0;
}
