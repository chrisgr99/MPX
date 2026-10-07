#include "../src/GuitarPro.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace px;
int main(int argc, char** argv) {
	std::ifstream f(argv[1], std::ios::binary); std::stringstream all; all << f.rdbuf();
	const std::string t = all.str(); const std::vector<uint8_t> b(t.begin(), t.end());
	GpSong s; std::string why;
	if (!gpReadBytes(b, s, &why)) { std::printf("%s\n", why.c_str()); return 1; }
	for (size_t i = 0; i < s.tracks.size(); i++)
		std::printf("  track %2d  %-34s percussion=%-3s program=%-3d channel=%d\n", (int) i,
			s.tracks[i].name.c_str(), s.tracks[i].percussion ? "YES" : "no",
			s.tracks[i].midiProgram, s.tracks[i].midiChannel);
	return 0;
}
