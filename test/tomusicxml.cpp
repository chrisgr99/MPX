/** A SCORE SAVED AS MUSICXML, EVERY PART OF IT, SO IT CAN BE MARKED UP.

A Guitar Pro file carries no dynamics worth the name and no way to write any. MusicXML does: it
holds dynamic marks, hairpins, slurs and articulation, and our reader honours all of them. So the
way to make a Guitar Pro score expressive is to write it out as MusicXML once, mark the MusicXML
up, and load that instead. This writes it out.

EVERY PART IN ONE DOCUMENT. The writer produces one part at a time, because that is what the score
window wants; a file to be marked up wants the whole score, so each part is written and the parts
are merged into one document. Merged rather than written afresh, so the one tested writer is the
only thing that ever produces our MusicXML.

	make tomusicxml ARGS="a-song.gp5 marked.musicxml"
	make tomusicxml ARGS="a-song.gp5"            writes a-song.musicxml beside it
*/
#include "../src/GpMusicXML.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <pugixml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace px;


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: tomusicxml <score> [out.musicxml]\n");
		return 1;
	}
	const std::string from = argv[1];

	std::ifstream file(from.c_str(), std::ios::binary);
	if (!file) {
		std::printf("could not open %s\n", from.c_str());
		return 1;
	}
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
	if (!ok) {
		std::printf("could not read it: %s\n", why.c_str());
		return 1;
	}

	std::string to;
	if (argc > 2) {
		to = argv[2];
	}
	else {
		to = from;
		const size_t dot = to.find_last_of('.');
		const size_t slash = to.find_last_of('/');
		if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
			to = to.substr(0, dot);
		to += ".musicxml";
	}

	// THE FIRST PART MAKES THE DOCUMENT and the rest are added to it: the header, the work's
	// title and the identification are the same whichever part wrote them.
	pugi::xml_document out;
	int written = 0;
	for (size_t t = 0; t < song.tracks.size(); t++) {
		const std::string one = gpToMusicXML(song, (int) t, GP_NOTATION, NULL);
		if (one.empty())
			continue;
		pugi::xml_document part;
		if (!part.load_buffer(one.data(), one.size()))
			continue;
		pugi::xml_node root = part.child("score-partwise");
		if (!root)
			continue;

		// EVERY PART NEEDS A NAME OF ITS OWN. The writer calls them all P1, since it only ever
		// writes one; in one document they have to be told apart.
		char id[32];
		std::snprintf(id, sizeof(id), "P%d", (int) t + 1);
		root.child("part-list").child("score-part").attribute("id").set_value(id);
		root.child("part").attribute("id").set_value(id);

		if (written == 0) {
			out.reset(part);
		}
		else {
			pugi::xml_node into = out.child("score-partwise");
			into.child("part-list").append_copy(
				root.child("part-list").child("score-part"));
			into.append_copy(root.child("part"));
		}
		written++;
	}

	if (!written) {
		std::printf("there was nothing in it to write\n");
		return 1;
	}
	if (!out.save_file(to.c_str(), "  ")) {
		std::printf("could not write %s\n", to.c_str());
		return 1;
	}

	std::printf("%s\n   %d parts, %d bars\n   written to %s\n",
		song.title.empty() ? from.c_str() : song.title.c_str(),
		written, (int) song.masterBars.size(), to.c_str());
	std::printf("   it carries no dynamics yet: that is what it is for\n");
	return 0;
}
