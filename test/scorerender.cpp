/** A PAGE OF A SCORE, DRAWN WITH THE REAL RENDERER, INTO A FILE — AND A SWEEP OVER MANY SCORES.

The window cannot be looked at from here, and a drawing fault that only shows on the screen is a
fault nobody can work on. So this does what the window does — the same Score, the same ScoreDraw,
the same NanoVG — into a hidden window, and reads the pixels back out as an image.

The window it opens never appears: GLFW is told not to show it, and it is destroyed before this
returns. Nothing is brought to the front and nothing is rearranged.

	make scorerender ARGS="a-file.musicxml"                   page one as build/score.png
	make scorerender ARGS="a-file.musicxml 2 1600 2000"       page, width, height in pixels
	make scorerender ARGS="a-file.musicxml 0"                 one glyph, large, filled and outlined
	make scorerender ARGS="sweep one.musicxml two.musicxml"   every page of each, counted
*/
// SCOREDRAW FIRST, BEFORE ANYTHING OF RACK'S. It brings in Verovio's headers, and Rack defines
// APP as a macro while Verovio has an enumerator of that name — whichever is included second
// loses, and the error names neither.
#include "../src/ScoreDraw.hpp"
#include "../src/Score.hpp"

#include <rack.hpp>

// GLEW BEFORE GLFW AND BEFORE NANOVG. Rack's NanoVG is built against GLEW, so the GL entry points
// have to be loaded — without glewInit the first call through one of them is a null pointer, which
// is a crash inside nvgCreateGL2 with nothing to show where it came from.
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <nanovg.h>

extern "C" NVGcontext* nvgCreateGL2(int flags);
extern "C" void nvgDeleteGL2(NVGcontext* ctx);

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

rack::plugin::Plugin* pluginInstance = NULL;


/** One page, drawn. The pixels are the caller's business; this is the transform the window uses. */
static int gLettering = -1;

static void drawPage(NVGcontext* vg, px::Score& score, int page, int width, int height) {
	if (px::ScoreDraw* back = const_cast<px::ScoreDraw*>(score.backend()))
		back->fallbackFont = gLettering;
	nvgBeginFrame(vg, (float) width, (float) height, 1.f);
	const float unit = score.pageScale((float) width);
	nvgSave(vg);
	nvgScale(vg, unit, unit);
	score.draw(vg, page, unit);
	nvgRestore(vg);
	nvgEndFrame(vg);
}


/** EVERY INSTRUMENT OF EVERY SCORE, SWITCHED TO AND DRAWN.

Showing one part means handing Verovio a score cut down to that part, and a cut that leaves
behind a part list referring to parts that are gone, or a bracket around nothing, gives a
document that will not load — or worse, one that loads and is missing something. So each part is
selected in turn and its first page drawn, and the page count is reported: a part that fails to
load, or lays out to nothing, shows up here.  */
static int sweepParts(NVGcontext* vg, int from, int argc, char** argv, int width, int height) {
	int failures = 0;
	std::printf("\n%-34s %5s  %-38s %6s %7s\n", "score", "part", "instrument", "pages", "glyphs");
	for (int a = from; a < argc; a++) {
		px::Score score;
		std::string why;
		if (!score.loadFile(argv[a], &why)) {
			std::printf("%-34s  COULD NOT BE READ: %s\n", argv[a], why.c_str());
			failures++;
			continue;
		}
		std::string shown = score.title();
		if (shown.size() > 33)
			shown = shown.substr(0, 33);
		const int count = (int) score.parts().size();
		for (int p = 0; p < count; p++) {
			if (!score.setPart(p, &why)) {
				std::printf("%-34s %5d  FAILED: %s\n", (p == 0) ? shown.c_str() : "", p + 1,
					why.c_str());
				failures++;
				continue;
			}
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			drawPage(vg, score, 1, width, height);
			const px::ScoreDraw* back = score.backend();
			const long glyphs = back ? back->tally.glyphs : 0;
			std::string who = score.parts()[p].name;
			if (who.size() > 37)
				who = who.substr(0, 37);
			std::printf("%-34s %5d  %-38s %6d %7ld%s\n",
				(p == 0) ? shown.c_str() : "", p + 1, who.c_str(), score.pageCount(), glyphs,
				(score.pageCount() < 1 || glyphs < 1) ? "   NOTHING DRAWN" : "");
			if (score.pageCount() < 1 || glyphs < 1)
				failures++;
		}
	}
	return failures;
}


/** EVERY PAGE OF EVERY SCORE, AT EACH SIZE THE WINDOW OFFERS, DRAWN AND COUNTED.

No picture comes out: what is wanted is what the backend was asked for and what it could not do.
A glyph the font does not have, a glyph whose file will not parse and a drawing call that is not
written all produce the same thing on a page — nothing — so the only way to find them is to count
them. Drawing still has to happen for the count to mean anything, so it draws into the hidden
window and throws the pixels away. */
static int sweep(NVGcontext* vg, int from, int argc, char** argv, int width, int height) {
	static const int SIZES[] = {700, 1100, 1500, 2200, 3000};
	static const int NSIZES = (int) (sizeof(SIZES) / sizeof(SIZES[0]));
	int failures = 0;

	// `turned` counts arpeggio signs and wavy glissandi, which are drawn on their side — a count
	// rather than a complaint, so that a page carrying one can be found and looked at.
	std::printf("%-34s %5s %6s %8s %8s %7s %7s %7s %7s %6s %7s\n",
		"score", "size", "pages", "glyphs", "unknown", "unread", "artwork", "turned",
		"bars", "wrong", "overrun");

	for (int a = from; a < argc; a++) {
		px::Score score;
		std::string why;
		if (!score.loadFile(argv[a], &why)) {
			std::printf("%-34s  COULD NOT BE READ: %s\n", argv[a], why.c_str());
			failures++;
			continue;
		}
		for (int i = 0; i < NSIZES; i++) {
			score.setPage(SIZES[i], SIZES[i] * 4 / 3);
			const int pages = score.pageCount();
			long glyphs = 0, unknown = 0, unread = 0, art = 0, turned = 0, words = 0;
			long boxes = 0, boxesOdd = 0, boxesOver = 0, pagesNoBoxes = 0;
			int firstTurned = 0;
			std::vector<char32_t> missing;
			for (int p = 1; p <= pages; p++) {
				glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
				drawPage(vg, score, p, width, height);
				const px::ScoreDraw* back = score.backend();
				if (!back)
					continue;
				glyphs += back->tally.glyphs;
				unknown += back->tally.glyphsUnknown;
				unread += back->tally.glyphsUnread;
				art += back->tally.svgShapes;
				if (back->tally.rotations && !firstTurned)
					firstTurned = p;
				turned += back->tally.rotations;
				words += back->tally.wordsSkipped;

				// THE BOX EACH BAR OCCUPIES, which is what a cursor and a loop are drawn from.
				// A box that is empty, inside out, or off the page is one the cursor would be
				// drawn in the wrong place from — and none of that shows on the page itself.
				const float pageW = (float) SIZES[i] * 10.f;
				const float pageH = (float) (SIZES[i] * 4 / 3) * 10.f;
				const std::vector<px::ScoreBox>& bars = score.bars();
				if (bars.empty())
					pagesNoBoxes++;
				for (size_t b = 0; b < bars.size(); b++) {
					boxes++;
					const px::ScoreBox& box = bars[b];
					// EMPTY, INSIDE OUT, OR NOWHERE NEAR THE PAGE is a box that is wrong, and a
					// cursor drawn from it would land anywhere.
					//
					// A LITTLE TO THE LEFT OF THE PAGE IS NOT WRONG. The first measure of a system
					// carries the brace that joins the staves and the clefs in front of it, and
					// Verovio draws those left of the page's own origin — so that measure's box
					// legitimately starts outside the page. Checked against a page of Fortunate
					// Son, where the bracket is plainly inside the first bar's extent.
					if (!box.valid() || box.x1 < -pageW || box.y1 < -pageH) {
						boxesOdd++;
						if (boxesOdd <= 3)
							std::printf("    WRONG box on page %d: x %.0f..%.0f  y %.0f..%.0f"
								"  (page is %.0f x %.0f)\n",
								p, box.x1, box.x2, box.y1, box.y2, pageW, pageH);
					}
					// PAST THE BOTTOM OF THE PAGE is not wrong. A score of many parts at a small
					// notation size genuinely has systems taller than the page, and Verovio draws
					// them; the window scrolls to where the ink stopped rather than to the page's
					// own height, so they can still be read. Counted, so it stays visible.
					else if (box.y2 > pageH || box.x2 > pageW)
						boxesOver++;
				}
				for (size_t k = 0; k < back->tally.missing.size() && missing.size() < 16; k++)
					missing.push_back(back->tally.missing[k]);
			}
			std::string shown = score.title();
			if (shown.size() > 33)
				shown = shown.substr(0, 33);
			std::printf("%-34s %5d %6d %8ld %8ld %7ld %7ld %7ld %7ld %6ld %7ld",
				(i == 0) ? shown.c_str() : "", SIZES[i], pages,
				glyphs, unknown, unread, art, turned, boxes, boxesOdd, boxesOver);
			if (pagesNoBoxes)
				std::printf("  (%ld pages recorded no bars)", pagesNoBoxes);
			std::printf("\n");
			if (boxesOdd)
				failures++;
			// A ROTATION IS NOT A FAILURE: it is counted so that a score which needs one can be
			// found and looked at. What cannot be drawn at all is a failure.
			if (unknown || unread || art)
				failures++;
			if (!missing.empty()) {
				std::printf("    the font has no glyph for:");
				for (size_t k = 0; k < missing.size(); k++)
					std::printf(" %04X", (unsigned) missing[k]);
				std::printf("\n");
			}
		}
	}
	return failures;
}


int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: scorerender <file.musicxml> [page] [width] [height] [units]\n");
		std::printf("       scorerender sweep <file.musicxml>...\n");
		std::printf("       scorerender parts <file.musicxml>...\n");
		return 1;
	}
	const bool sweeping = (std::string(argv[1]) == "sweep"
		|| std::string(argv[1]) == "parts");
	const char* file = argv[1];
	const int page = (!sweeping && argc > 2) ? std::atoi(argv[2]) : 1;
	const int width = (!sweeping && argc > 3) ? std::atoi(argv[3]) : 1200;
	const int height = (!sweeping && argc > 4) ? std::atoi(argv[4]) : 1600;
	// How wide the page says it is, which is how large the notation comes out. See Score::setPage.
	const int units = (!sweeping && argc > 5) ? std::atoi(argv[5]) : 0;

	rack::plugin::Plugin plugin;
	plugin.path = ".";
	pluginInstance = &plugin;

	if (!glfwInit()) {
		std::printf("glfw would not start\n");
		return 1;
	}
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_STENCIL_BITS, 8);
	glfwWindowHint(GLFW_SAMPLES, 4);
	GLFWwindow* win = glfwCreateWindow(width, height, "score", NULL, NULL);
	if (!win) {
		std::printf("no window, so no renderer\n");
		return 1;
	}
	glfwMakeContextCurrent(win);
	glewExperimental = GL_TRUE;
	const GLenum glew = glewInit();
	if (glew != GLEW_OK) {
		std::printf("glew would not start: %s\n", (const char*) glewGetErrorString(glew));
		return 1;
	}

	NVGcontext* vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
	if (!vg) {
		std::printf("no NanoVG context\n");
		return 1;
	}
	// A FONT FOR THE LETTERING. Inside Rack this comes from Rack; here it has to be found, or a
	// page of tablature draws with every fret number missing — which is most of the page.
	const int lettering = nvgCreateFont(vg, "text", "/System/Library/Fonts/Supplemental/Arial.ttf");
	if (lettering < 0)
		std::printf("no text font found: the lettering will not be drawn\n");
	gLettering = lettering;

	glViewport(0, 0, width, height);
	glClearColor(0x1c / 255.f, 0x1e / 255.f, 0x22 / 255.f, 1.f);

	if (std::string(argv[1]) == "parts") {
		const int bad = sweepParts(vg, 2, argc, argv, width, height);
		std::printf("\n%s\n", bad ? "SOMETHING WAS NOT DRAWN — see above"
			: "every instrument of every score drew");
		glfwDestroyWindow(win);
		glfwTerminate();
		return bad ? 1 : 0;
	}
	if (sweeping) {
		const int bad = sweep(vg, 2, argc, argv, width, height);
		std::printf("\n%s\n", bad ? "SOMETHING WAS NOT DRAWN — see the counts above"
			: "every glyph of every page was drawn");
		glfwDestroyWindow(win);
		glfwTerminate();
		return bad ? 1 : 0;
	}

	int stencilBits = 0, samples = 0;
	glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
	glGetIntegerv(GL_SAMPLES, &samples);
	std::printf("context: %d stencil bits, %d samples\n", stencilBits, samples);

	px::Score score;
	std::string why;
	// THE PAGE BEFORE THE SCORE, which is the order the window uses: the options are read when a
	// document is laid out, and some of them — how many systems a page may hold — are not revised
	// by a later relayout.
	{
		const int tallFirst = (width > 0) ? (int) ((double) units * height / width) : units * 4 / 3;
		if (units > 0)
			score.setPage(units, tallFirst);
	}
	if (!score.loadFile(file, &why)) {
		std::printf("could not read it: %s\n", why.c_str());
		return 1;
	}
	// THE PAGE IS THE SHAPE OF WHAT IT IS BEING DRAWN INTO, which is what the score window does
	// with the window: one page is then one windowful and there is nothing to scroll.
	const int tall = (width > 0) ? (int) ((double) units * height / width) : units * 4 / 3;
	if (units > 0)
		score.setPage(units, tall);
	// EVERY SYSTEM MEASURED WITHOUT DRAWING ANY OF THEM. A continuous view has to know how tall
	// each system is before it can stack them, and a render with no context does everything up to
	// the drawing — which is how the glyph sheet is fetched too. If this is cheap, a continuous
	// view costs one pass at load and nothing per frame.
	if (page == -1) {
		// AND AGAIN AFTER A RESIZE, which is what the window does when its shape settles. The
		// fitted width was thrown away by that and the crammed layout came back, so the bench
		// asks for the page a second time and reports both.
		std::printf("  before a resize: %d systems\n", score.pageCount());
		score.setPage(units + 1, (units + 1) * 2);
		std::printf("  after a resize:  %d systems\n", score.pageCount());
		const auto began = std::chrono::steady_clock::now();
		score.measureSystems();
		const double took = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - began).count();
		const std::vector<px::ScoreSystem>& sys = score.systems();
		int bars = 0;
		bool rising = true;
		float was = -1.f;
		for (size_t i = 0; i < sys.size(); i++) {
			bars += (int) sys[i].bars.size();
			if (sys[i].top <= was)
				rising = false;
			was = sys[i].top;
		}
		std::printf("measured %d systems in %.0f ms: %d bars, flow %.0f units tall,"
			" tops %s\n", (int) sys.size(), took * 1000.0, bars, score.flowHeight(),
			rising ? "rise all the way down" : "DO NOT RISE — the stack is wrong");
		for (size_t i = 0; i < sys.size() && i < 4; i++) {
			std::printf("    system %d: top %7.0f  height %5.0f  shift %5.0f  bars %d",
				(int) i + 1, sys[i].top, sys[i].height, sys[i].shift, (int) sys[i].bars.size());
			for (size_t b = 0; b < sys[i].bars.size(); b++)
				std::printf(" %s", sys[i].bars[b].number.c_str());
			std::printf("\n");
		}
		return 0;
	}
	std::printf("%s: %d pages, drawing page %d at %dx%d\n",
		score.title().c_str(), score.pageCount(), page, width, height);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	if (page == 0) {
		// PAGE NOUGHT IS NOT A PAGE: it draws one glyph the size of the picture, filled on the
		// left and outlined on the right, so that a glyph that comes out wrong can be looked at
		// on its own instead of eight to a bar.
		nvgBeginFrame(vg, (float) width, (float) height, 1.f);
		score.drawOneGlyph(vg, (char32_t) 0xE0A4, width, height);
		nvgEndFrame(vg);
	}
	else {
		drawPage(vg, score, page, width, height);
	}

	if (const px::ScoreDraw* back = score.backend()) {
		// WHICH BARS THE PAGE HOLDS, and whether they know their own numbers. The score window
		// finds the playing bar on the page by that number, so a page whose boxes are unnumbered
		// is a playhead that never appears. See ScoreFollow.hpp.
		{
			const std::vector<px::ScoreBox>& boxes = score.bars();
			int numbered = 0;
			std::string first, last;
			for (size_t i = 0; i < boxes.size(); i++) {
				if (boxes[i].number.empty())
					continue;
				numbered++;
				if (first.empty())
					first = boxes[i].number;
				last = boxes[i].number;
			}
			// HOW FAR DOWN THE PAGE THE MUSIC ACTUALLY WENT, against the page it was given.
			// The score window makes its pages the shape of the window so that one page is one
			// windowful; if the drawing runs past the page, it is not and the window scrolls.
			std::printf("that page: %d bar boxes, %d of them numbered, bars %s to %s,"
				" drawn %.0f of %d units tall\n",
				(int) boxes.size(), numbered, first.empty() ? "?" : first.c_str(),
				last.empty() ? "?" : last.c_str(), score.drawnHeight(), tall * 10);
		}
		std::printf("that page: %d glyphs, %d unknown, %d unread, %d runs of lettering skipped\n",
			back->tally.glyphs, back->tally.glyphsUnknown, back->tally.glyphsUnread,
			back->tally.wordsSkipped);
	}

	std::vector<unsigned char> pixels((size_t) width * height * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

	// Written as a plain PPM, which needs no library; the Makefile turns it into a PNG.
	FILE* f = std::fopen("build/score.ppm", "wb");
	if (!f) {
		std::printf("could not write build/score.ppm\n");
		return 1;
	}
	std::fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (int y = height - 1; y >= 0; y--)
		std::fwrite(&pixels[(size_t) y * width * 3], 1, (size_t) width * 3, f);
	std::fclose(f);
	std::printf("wrote build/score.ppm\n");

	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}
