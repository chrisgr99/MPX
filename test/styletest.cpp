/** What style each score's ornaments would be played in, and why: the recogniser in
OrnamentStyle.cpp run over files from a command line.

    make styletest ARGS="~/Downloads/*.mxl"
*/
#include "../src/MusicXML.hpp"
#include "../src/OrnamentStyle.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace px;

int main(int argc, char** argv) {
	for (int a = 1; a < argc; a++) {
		std::ifstream f(argv[a], std::ios::binary);
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		GpSong song;
		std::string why;
		std::string name = argv[a];
		const size_t slash = name.find_last_of('/');
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		if (!mxReadBytes(bytes, song, &why)) {
			std::printf("%-58.58s  not read: %s\n", name.c_str(), why.c_str());
			continue;
		}
		std::vector<StyleClue> clues = song.clues;
		std::string base = name;
		const size_t dot = base.find_last_of('.');
		if (dot != std::string::npos)
			base = base.substr(0, dot);
		clues.push_back(StyleClue{base, CLUE_FILE});
		const StyleGuess g = guessStyle(clues, song.chordSymbols);
		std::printf("%-58.58s  %-15s  %s\n", name.c_str(), ornamentStyleName(g.style), g.because.c_str());
	}
	return 0;
}
