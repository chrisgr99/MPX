/** WHAT VEROVIO MAKES OF A SCORE, WITHOUT RACK'S WINDOW.

Engraving is slow and drawing is not, so the slow half is worth being able to run on its own: this
loads a MusicXML file, lays it out at each size the window offers, and says how long each took and
how many pages it came to. What it cannot do is show the page — there is no OpenGL context here,
so ScoreDraw draws nothing — but everything up to the drawing is exercised.

	make scoretest ARGS="path/to/file.musicxml"
*/
#include <rack.hpp>

#include "../src/Score.hpp"

#include <chrono>
#include <cstdio>
#include <string>

/** THE PLUGIN, STUBBED. Score asks Rack where the plugin's resources are, and here they are in
the source tree. */
rack::plugin::Plugin* pluginInstance = NULL;


static double seconds(std::chrono::steady_clock::time_point from) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: scoretest <file.musicxml> [more...]\n");
		return 1;
	}

	rack::plugin::Plugin plugin;
	plugin.path = ".";
	pluginInstance = &plugin;
	std::printf("glyphs at %s\n\n", px::Score::resourcePath().c_str());

	int failed = 0;
	for (int i = 1; i < argc; i++) {
		px::Score score;
		std::string why;
		std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
		if (!score.loadFile(argv[i], &why)) {
			std::printf("%-40s FAILED: %s\n", argv[i], why.c_str());
			failed++;
			continue;
		}
		std::printf("%-40s %6.2fs  %2d pages as loaded\n",
			score.title().c_str(), seconds(began), score.pageCount());

		// HOW LARGE THE NOTATION IS, is how SMALL the page is in Verovio's units: the page is
		// drawn the width of the window whatever it says it is, so a narrow page in units is a
		// page with fewer bars on a line, each of them bigger. Verovio's own `scale` option is
		// not this — it zooms the finished drawing and does not change the layout at all.
		for (int w = 900; w <= 3600; w += 450) {
			began = std::chrono::steady_clock::now();
			score.setPage(w, w * 4 / 3);
			std::printf("    page %4d units  %6.2fs  %3d pages\n",
				w, seconds(began), score.pageCount());
		}
	}
	return failed ? 1 : 0;
}
