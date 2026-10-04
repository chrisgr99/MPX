/** See Score.hpp. */
// Verovio before anything of Rack's: Rack defines APP as a macro and Verovio has an enumerator
// of that name.
#include <vrv/toolkit.h>
#include <vrv/vrvdef.h>

#include "Score.hpp"
#include "ScoreDraw.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace px {


Score::Score() {
	// FALSE, so that the fonts are not looked for before we have said where they are. Verovio
	// looks in a compiled-in path otherwise, which on this machine is wherever it was built.
	toolkit = new vrv::Toolkit(false);
	device = new ScoreDraw;
	toolkit->SetResourcePath(resourcePath());
}


Score::~Score() {
	delete toolkit;
	delete device;
}


std::string Score::resourcePath() {
	return asset::plugin(pluginInstance, "res/verovio");
}


void Score::applyOptions() {
	// FOOTER AND HEADER OFF. They are for printing a page, and this is a window on a score: the
	// title and the composer would take a third of the first page and say what the panel says.
	//
	// BREAKS "auto" lets Verovio decide where a system ends, which is what makes the notation
	// the size it was asked for rather than squeezed to fit a bar count.
	char buf[512];
	std::snprintf(buf, sizeof(buf),
		"{\"pageWidth\": %d, \"pageHeight\": %d,"
		" \"adjustPageHeight\": false, \"breaks\": \"auto\","
		" \"footer\": \"none\", \"header\": \"none\","
		" \"font\": \"Leipzig\"}",
		width, height);
	toolkit->SetOptions(buf);
}


bool Score::load(const std::string& musicxml, std::string* reason) {
	loaded = false;
	applyOptions();
	if (!toolkit->LoadData(musicxml)) {
		if (reason)
			*reason = "this score could not be read";
		return false;
	}
	loaded = true;
	return true;
}


bool Score::loadFile(const std::string& path, std::string* reason) {
	std::ifstream file(path.c_str());
	if (!file) {
		if (reason)
			*reason = "that file could not be opened";
		return false;
	}
	std::stringstream all;
	all << file.rdbuf();

	const size_t slash = path.find_last_of('/');
	name = (slash == std::string::npos) ? path : path.substr(slash + 1);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);

	return load(all.str(), reason);
}


bool Score::ready() const {
	return loaded;
}


void Score::setPage(int w, int h) {
	if (w == width && h == height)
		return;
	width = w;
	height = h;
	if (!loaded)
		return;
	// THE SCORE IS LAID OUT AGAIN, which is what changing the size of the notation means: the
	// systems break in different places and the page count changes.
	applyOptions();
	toolkit->RedoLayout();
}


int Score::pageCount() const {
	return loaded ? toolkit->GetPageCount() : 0;
}


float Score::pageScale(float pixels) const {
	// Verovio lays out in its own units and reports the page as `width` of them times the view
	// box factor; a page drawn this many pixels wide is that many units to the pixel.
	const float units = (float) width * 10.f;
	return (units > 0.f) ? pixels / units : 1.f;
}


void Score::draw(NVGcontext* vg, int page, float pixelsPerUnit) {
	if (!loaded || !device || page < 1 || page > pageCount())
		return;
	device->vg = vg;
	device->pixelsPerUnit = pixelsPerUnit;
	toolkit->RenderToDeviceContext(page, device);
	device->vg = NULL;
}


void Score::drawOneGlyph(NVGcontext* vg, char32_t code, int width, int height) {
	if (!device || !loaded)
		return;
	// A RENDER WITH NOTHING TO DRAW ON. Every drawing in ScoreDraw is guarded on having a context,
	// so this draws nothing at all — but it is how Verovio hands the device its resources, and the
	// glyphs are in those.
	device->vg = NULL;
	toolkit->RenderToDeviceContext(1, device);

	device->vg = vg;
	device->pixelsPerUnit = 1.f;
	device->drawGlyphLarge(code, width, height);
	device->vg = NULL;
}


float Score::drawnHeight() const {
	return device ? device->page.y2 : 0.f;
}


const std::vector<ScoreBox>& Score::bars() const {
	static const std::vector<ScoreBox> none;
	return device ? device->bars : none;
}


} // namespace px
