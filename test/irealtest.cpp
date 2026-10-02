/** Reads iReal Pro playlist exports and reports what the parser made of them.

    make test

WHY A PROGRAM RATHER THAN A PATCH. The parser is pure and has no Rack in it, so it can be run
against two thousand real charts in a second. Checking it by patching cables and listening would
test one chart at a time and would not say which ones were wrong.
*/
#include "../src/IReal.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>

using namespace px;

static std::string readFile(const char* path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/** SLASH CHORDS: the bass kept as a degree of the key, spelled back as written. */
static int checkSlashes() {
	struct Case { const char* symbol; int tonic; bool minor; const char* letter; const char* roman; };
	const Case cases[] = {
		{"C/G", 0, false, "C/G", "I/5"},
		{"D-7/C", 0, false, "Dm7/C", "ii7/1"},
		{"F/A", 0, false, "F/A", "IV/6"},
		{"Ab/Bb", 3, false, "Ab/Bb", "IV/5"},
		{"G7/B", 0, false, "G7/B", "V7/7"},
		{"C/C", 0, false, "C", "I"},
		{"E7", 9, true, "E7", "V7"},
	};
	int failures = 0;
	for (const Case& c : cases) {
		Key key;
		key.tonic = (int8_t) c.tonic;
		key.minor = c.minor;
		Chord chord;
		irealParseChord(c.symbol, key, chord);
		const std::string letter = chordLetter(chord, key), roman = chordRoman(chord);
		const bool ok = letter == c.letter && roman == c.roman;
		std::printf("%s  %-6s reads as %s, %s\n", ok ? "ok  " : "FAIL", c.symbol, letter.c_str(),
			roman.c_str());
		if (!ok)
			failures++;
	}
	Key c;
	Chord g;
	irealParseChord("C/G", c, g);
	if (chordBassPitchClass(g, c) != 7 || chordRootPitchClass(g, c) != 0) {
		std::printf("FAIL  C/G's bass is G and its root C\n");
		failures++;
	}
	std::printf("\n");
	return failures;
}

int main(int argc, char** argv) {
	if (checkSlashes() > 0)
		return 1;
	int totalSongs = 0, totalUnsupported = 0, totalEmpty = 0, totalSpans = 0;
	std::map<std::string, int> unknownReasons;

	for (int a = 1; a < argc; a++) {
		const std::string html = readFile(argv[a]);
		if (html.empty()) {
			std::printf("%-24s  could not be read\n", argv[a]);
			continue;
		}
		const std::string payload = irealPayloadFromHtml(html);
		if (payload.empty()) {
			std::printf("%-24s  no irealb link in it\n", argv[a]);
			continue;
		}
		std::string name;
		const std::vector<Song> songs = irealParsePlaylist(payload, &name);

		int empty = 0, unsupported = 0, bars = 0, spans = 0;
		for (const Song& song : songs) {
			const Expansion e = irealExpand(song);
			if (e.spans.empty())
				empty++;
			if (!song.supported) {
				unsupported++;
				unknownReasons[song.why]++;
			}
			bars += e.bars;
			spans += (int) e.spans.size();
		}
		std::printf("%-22s %5d songs  %6d bars  %6d chords   %d empty  %d unsupported\n",
			name.empty() ? argv[a] : name.c_str(),
			(int) songs.size(), bars, spans, empty, unsupported);

		totalSongs += (int) songs.size();
		totalEmpty += empty;
		totalUnsupported += unsupported;
		totalSpans += spans;

		// The first song of each playlist, written out, so the shape can be eyeballed.
		if (!songs.empty()) {
			const Song& s = songs[0];
			const Expansion e = irealExpand(s);
			std::printf("   e.g. \"%s\" — %s, %s %s, %d bars\n", s.title.c_str(),
				s.style.c_str(), pitchClassNameIn(s.key.tonic, s.key),
				s.key.minor ? "minor" : "major", e.bars);
			std::printf("        ");
			for (size_t i = 0; i < e.spans.size() && i < 16; i++) {
				std::printf("%s ", e.spans[i].noChord
					? "N.C." : chordLetter(e.spans[i].chord, s.key).c_str());
			}
			std::printf("%s\n", e.spans.size() > 16 ? "..." : "");
		}
	}

	std::printf("\n%d songs, %d chords. %d parsed to nothing, %d unsupported.\n",
		totalSongs, totalSpans, totalEmpty, totalUnsupported);
	for (const auto& r : unknownReasons)
		std::printf("   %-30s %d\n", r.first.c_str(), r.second);
	// A parser that produces nothing for a chart is the failure worth failing on.
	return (totalSongs > 0 && totalEmpty * 20 < totalSongs) ? 0 : 1;
}
