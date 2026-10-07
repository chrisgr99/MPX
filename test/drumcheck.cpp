/** WHAT A DRUM TRACK LOOKS LIKE BY THE TIME IT REACHES THE SYNTHESISER. */
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace px;
int main(int argc, char** argv) {
	for (int a = 1; a < argc; a++) {
		std::ifstream f(argv[a], std::ios::binary);
		if (!f) continue;
		std::stringstream all; all << f.rdbuf();
		const std::string text = all.str();
		const std::vector<uint8_t> bytes(text.begin(), text.end());
		GpSong song; std::string why;
		const std::string head = text.substr(0, std::min<size_t>(2048, text.size()));
		bool ok = false;
		if (mxLooksLikeMusicXML(head)) ok = mxReadBytes(bytes, song, &why);
		if (!ok) ok = gpReadBytes(bytes, song, &why);
		if (!ok) { std::printf("%s: %s\n", argv[a], why.c_str()); continue; }
		const char* slash = std::strrchr(argv[a], '/');
		std::printf("\n%s\n", slash ? slash + 1 : argv[a]);
		GpTimeline line; gpBuildTimeline(song, line, &why);
		for (size_t t = 0; t < song.tracks.size(); t++) {
			const GpTrack& tr = song.tracks[t];
			if (!tr.percussion && tr.name.find("rum") == std::string::npos
					&& tr.name.find("erc") == std::string::npos) continue;
			std::map<int,int> hits; int unpitched = 0;
			if (t < line.tracks.size())
				for (const GpPlayedNote& n : line.tracks[t]) {
					if (n.note.midi <= 0) unpitched++; else hits[n.note.midi]++;
				}
			std::printf("  track %d \"%s\": percussion=%s, program=%d, channel=%d, %d notes\n",
				(int) t, tr.name.c_str(), tr.percussion ? "YES" : "NO",
				tr.midiProgram, tr.midiChannel,
				(int) (t < line.tracks.size() ? line.tracks[t].size() : 0));
			if (unpitched) std::printf("    %d notes with NO pitch\n", unpitched);
			std::printf("    kit numbers used:");
			int shown = 0;
			for (std::map<int,int>::const_iterator i = hits.begin(); i != hits.end() && shown < 14; ++i, shown++)
				std::printf(" %d(%d)", i->first, i->second);
			std::printf("%s\n", hits.size() > 14 ? " ..." : "");
		}
	}
	return 0;
}
