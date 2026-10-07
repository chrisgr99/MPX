/** See Score.hpp. */
// Verovio before anything of Rack's: Rack defines APP as a macro and Verovio has an enumerator
// of that name.
#include <vrv/toolkit.h>
#include <vrv/vrvdef.h>
// Verovio reads its own XML with this, so it is already here and already linked.
#include <pugixml.hpp>

#include "GuitarPro.hpp"
#include "Score.hpp"
#include "ScoreDraw.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

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


/** HOW FAR APART STACKED SYSTEMS SIT, in page units. Each is trimmed to its own ink, so without
this they would touch. */
static const float SCORE_SYSTEM_GAP = 900.f;


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
		// ONE SYSTEM TO A PAGE, SAID OUTRIGHT. The window stacks systems itself, so it wants
		// them one at a time — and the way to get that was a page too short to hold two, which
		// works only while every system is shorter than the page. A score with a tall system —
		// a wide grand staff, ledger lines, several voices — overflowed instead: Verovio gave up
		// paginating and crammed twelve bars onto one line, with the ink starting twice the
		// page's height down. Asking for one system a page says what is wanted and leaves the
		// height out of it, so the page can be as tall as the tallest system needs.
		" \"systemMaxPerPage\": 1,"
		// AND THE NOTATION IS THE SIZE WE ASK FOR, not the size the file was engraved at. A
		// MusicXML file may carry its own scaling — so many millimetres to so many tenths — and
		// Verovio honours it, so the same page width holds two bars of a score that declares
		// none and eighteen of one that declares a small staff. The Nocturne in E flat declares
		// 7.06mm to 40 tenths and came out as two systems of eighteen bars, unreadable. Fixing
		// the staff size makes a page width mean the same thing for every score.
		" \"unit\": 9,"
		" \"footer\": \"none\", \"header\": \"none\","
		" \"font\": \"Leipzig\"}",
		width, height);
	toolkit->SetOptions(buf);
}


/** Whether this is a zip, which a compressed MusicXML file is. */
static bool isZip(const std::string& data) {
	return data.size() > 4 && data[0] == 'P' && data[1] == 'K'
		&& data[2] == '\x03' && data[3] == '\x04';
}


bool Score::load(const std::string& musicxml, std::string* reason) {
	loaded = false;
	applyOptions();

	// COMPRESSED MUSICXML IS WHAT MUSESCORE WRITES BY DEFAULT, and it is a zip holding the
	// document and a little file saying which member of the zip the document is. Verovio reads
	// it, but only through a call of its own: handed the bytes it works the format out by looking
	// at them, sees a zip, and ends up trying to read it as MEI.
	if (isZip(musicxml)) {
		const bool ok = toolkit->LoadZipDataBuffer(
			(const unsigned char*) musicxml.data(), (int) musicxml.size());
		if (!ok) {
			if (reason)
				*reason = "this compressed score could not be read";
			return false;
		}
		loaded = true;
		return true;
	}

	if (!toolkit->LoadData(musicxml)) {
		if (reason)
			*reason = "this score could not be read";
		return false;
	}
	loaded = true;
	return true;
}


/** WHAT INSTRUMENTS THE FILE HOLDS, read from the part list at the top of it.

A part with no name of its own is named by its id, because a chooser with a blank row in it is
worse than one with an ugly row in it. */
static std::vector<ScorePart> readParts(const pugi::xml_document& doc) {
	std::vector<ScorePart> found;
	const pugi::xml_node list = doc.child("score-partwise").child("part-list");
	for (pugi::xml_node p = list.child("score-part"); p; p = p.next_sibling("score-part")) {
		ScorePart part;
		part.id = p.attribute("id").as_string();
		part.name = p.child_value("part-name");
		if (part.name.empty())
			part.name = part.id.empty() ? "Part" : part.id;
		found.push_back(part);
	}
	return found;
}


std::string Score::onePart(int index) const {
	if (index < 0 || index >= (int) allParts.size() || allParts.size() <= 1)
		return source;

	pugi::xml_document doc;
	if (!doc.load_buffer(source.data(), source.size()))
		return source;
	pugi::xml_node root = doc.child("score-partwise");
	if (!root)
		return source;   // Timewise MusicXML, which this does not cut up. Shown whole.

	const std::string keep = allParts[index].id;

	// THE PART LIST FIRST. A part-group brackets several parts together, and with only one part
	// left there is nothing to bracket — leaving the group behind draws a brace around a single
	// staff, or around nothing.
	pugi::xml_node list = root.child("part-list");
	std::vector<pugi::xml_node> doomed;
	for (pugi::xml_node n = list.first_child(); n; n = n.next_sibling()) {
		const std::string tag = n.name();
		if (tag == "part-group")
			doomed.push_back(n);
		else if (tag == "score-part" && std::string(n.attribute("id").as_string()) != keep)
			doomed.push_back(n);
	}
	for (size_t i = 0; i < doomed.size(); i++)
		list.remove_child(doomed[i]);

	// THEN THE MUSIC.
	doomed.clear();
	for (pugi::xml_node n = root.child("part"); n; n = n.next_sibling("part")) {
		if (std::string(n.attribute("id").as_string()) != keep)
			doomed.push_back(n);
	}
	for (size_t i = 0; i < doomed.size(); i++)
		root.remove_child(doomed[i]);

	std::ostringstream out;
	doc.save(out, "  ");
	return out.str();
}


bool Score::setPart(int index, std::string* reason) {
	if (index < 0 || index >= (int) allParts.size()) {
		if (reason)
			*reason = "there is no such part";
		return false;
	}
	chosen = index;
	return load(onePart(chosen), reason);
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
	std::string called = path;
	const size_t slash = called.find_last_of('/');
	if (slash != std::string::npos)
		called = called.substr(slash + 1);
	return loadBytes(all.str(), called, reason);
}


/** THE SAME, ON A DOCUMENT ALREADY IN HAND. A module that loaded a file holds the bytes of it —
it has to, since a patch carries the score inside it — and handing those over shows the score the
writer wrote rather than one converted back out of the model, which is not the same document and
on a piano is not even the same number of staves. See scoreWindowShowDocument. */
bool Score::loadBytes(const std::string& bytes, const std::string& called, std::string* reason) {
	source = bytes;

	// COMPRESSED MUSICXML, OPENED HERE RATHER THAN LEFT TO VEROVIO.
	//
	// A .mxl file is a zip holding the document and a little file in META-INF saying which member
	// the document is. Verovio can read one, but only through a call of its own — and more to the
	// point, everything else here works on the XML: the list of instruments is read from it and
	// cutting the score down to one part rewrites it. A compressed file left compressed would
	// open and then refuse to be cut, so every part would show at once.
	//
	// So it is opened once, here, and nothing downstream knows the difference.
	if (source.size() > 4 && source[0] == 'P' && source[1] == 'K') {
		const std::vector<uint8_t> zip(source.begin(), source.end());
		std::vector<uint8_t> got;
		std::string inside;
		if (gpZipEntry(zip, "META-INF/container.xml", got)) {
			const std::string container((const char*) got.data(), got.size());
			const size_t at = container.find("full-path");
			if (at != std::string::npos) {
				const size_t a = container.find('"', at);
				const size_t b = (a == std::string::npos)
					? std::string::npos : container.find('"', a + 1);
				if (b != std::string::npos)
					inside = container.substr(a + 1, b - a - 1);
			}
		}
		// A container that names nothing useful: the usual name, which is what every writer uses.
		if (inside.empty())
			inside = "score.xml";
		if (gpZipEntry(zip, inside, got))
			source.assign((const char*) got.data(), got.size());
		if (reason)
			reason->clear();
	}

	name = called;
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);

	allParts.clear();
	chosen = 0;
	{
		pugi::xml_document doc;
		if (doc.load_buffer(source.data(), source.size()))
			allParts = readParts(doc);
	}

	// THE FIRST PART, NOT ALL OF THEM. See parts().
	if (!load(onePart(chosen), reason))
		return false;
	fitSystems();
	return true;
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
	// AND FITTED AGAIN, because this has just thrown the fitted width away. Laying out at the
	// width that was asked for is exactly what fitSystems exists to correct, so a resize that
	// did not refit put the crammed layout straight back — which is what a score still coming
	// out as a scribble after the fit was written turned out to be.
	fitSystems();
}


int Score::pageCount() const {
	return loaded ? toolkit->GetPageCount() : 0;
}


/** HOW MANY BARS A SYSTEM SHOULD HOLD. Two to five reads well at the sizes this window offers;
outside that the notation is either cramped to nothing or so sparse that a page holds a phrase. */
static const double FIT_BARS_LOW = 2.0;
static const double FIT_BARS_HIGH = 5.0;


void Score::fitSystems() {
	// A PAGE WIDTH DOES NOT MEAN THE SAME THING FOR EVERY SCORE.
	//
	// A MusicXML file may carry its own scaling — so many millimetres to so many tenths — and its
	// own page size, and Verovio honours them. So the width asked for here holds two bars of a
	// score that declares none and eighteen of one that declares a small staff: the Nocturne in E
	// flat came out as two systems of eighteen bars, which on the screen is a scribble.
	//
	// RATHER THAN GUESS AT WHAT THE FILE DECLARED, the layout is measured and the width adjusted
	// until a system holds a readable number of bars. Verovio is asked for one system a page, so
	// counting pages counts systems, and the bars are counted off the document. Three passes at
	// a tenth of a second each, once, when a score is opened.
	if (!loaded || !toolkit)
		return;
	size_t bars = 0;
	for (size_t at = source.find("<measure "); at != std::string::npos;
			at = source.find("<measure ", at + 1))
		bars++;
	if (bars < 2)
		return;
	for (int pass = 0; pass < 6; pass++) {
		const int systems = pageCount();
		if (systems < 1)
			return;
		const double perSystem = (double) bars / (double) systems;
		if (perSystem >= FIT_BARS_LOW && perSystem <= FIT_BARS_HIGH)
			return;
		// A SYSTEM HOLDING TWICE AS MANY BARS AS IT SHOULD WANTS HALF THE WIDTH.
		const double want = (perSystem > FIT_BARS_HIGH) ? FIT_BARS_HIGH : FIT_BARS_LOW;
		double scale = want / perSystem;
		if (scale < 0.2) scale = 0.2;
		if (scale > 5.0) scale = 5.0;
		const int wider = (int) ((double) width * scale);
		if (wider < 60 || wider > 60000 || wider == width)
			return;
		width = wider;
		applyOptions();
		toolkit->RedoLayout();
	}
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


void Score::measureSystems() {
	allSystems.clear();
	if (!loaded || !device)
		return;
	// A GAP BETWEEN SYSTEMS, since each is trimmed to its own ink and two stacked with none would
	// touch. A fifth of the mean system is about what a page leaves.
	float top = 0.f;
	const int count = pageCount();
	allSystems.reserve((size_t) std::max(0, count));
	for (int p = 1; p <= count; p++) {
		// A RENDER WITH NOTHING TO DRAW ON: see drawOneGlyph, which uses the same trick to fetch
		// the glyphs. Every drawing in ScoreDraw measures before it looks for a context.
		device->vg = NULL;
		toolkit->RenderToDeviceContext(p, device);
		ScoreSystem sys;
		sys.shift = device->page.y1;
		sys.height = std::fmax(0.f, device->page.y2 - device->page.y1);
		sys.top = top;
		sys.bars = device->bars;
		// THE BARS IN THE SYSTEM'S OWN COORDINATES, so nothing downstream has to know what the
		// page thought it was doing.
		for (size_t i = 0; i < sys.bars.size(); i++) {
			sys.bars[i].y1 -= sys.shift;
			sys.bars[i].y2 -= sys.shift;
		}
		top += sys.height + SCORE_SYSTEM_GAP;
		allSystems.push_back(sys);
	}
}


float Score::flowHeight() const {
	if (allSystems.empty())
		return 0.f;
	const ScoreSystem& last = allSystems.back();
	return last.top + last.height;
}


const std::vector<ScoreBox>& Score::bars() const {
	static const std::vector<ScoreBox> none;
	return device ? device->bars : none;
}


} // namespace px
