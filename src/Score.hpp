#pragma once
/** A SCORE, ENGRAVED AND DRAWN.

Verovio is handed MusicXML and lays it out into pages; this holds one of those and draws a page
with Rack's own renderer — see ScoreDraw.hpp for how, and docs/notation.md for why.

LAYING OUT IS NOT DRAWING. Seventeen parts of Layla take Verovio 1.3 seconds to lay out and
twelve milliseconds to draw a page of, so loading happens on a thread of its own and drawing
happens where everything else is drawn. Nothing here is touched from the audio thread at all.

ONE PART AT A TIME. A score of seventeen staves puts three bars on a page, which is no use to
anybody reading it.
*/
#include <string>
#include <vector>

namespace vrv {
class Toolkit;
}

struct NVGcontext;

namespace px {

struct ScoreDraw;
struct ScoreBox;


/** ONE INSTRUMENT OF A SCORE, as the file names it. */
struct ScorePart {
	std::string id;     /**< What the file calls it, which is what the parts are matched by. */
	std::string name;   /**< What a person calls it: "Bass (Tr. 6A/Tr. 11B)". */
};


struct Score {
	Score();
	~Score();

	/** Where the glyphs live. Set once, before anything is loaded. */
	static std::string resourcePath();

	/** Lays out a MusicXML document. Slow — seconds for a large score — so never from the audio
	thread and never from the drawing one. Returns false and fills `reason`. */
	bool load(const std::string& musicxml, std::string* reason);
	bool loadFile(const std::string& path, std::string* reason);
	bool ready() const;

	/** THE INSTRUMENTS IN THE FILE, and which one is being shown.

	ONE AT A TIME, ALWAYS. Seventeen parts put three bars on a page: it is not untidy, it is
	unreadable, and every other thing this window does is academic until the score is one part.

	Verovio has no say in this. Its own selection takes bar ranges and element ids and nothing
	else — there is no option anywhere for which staves to draw — so a single part means handing
	it a score that HAS only one part, which is what setPart does. From phase five on there will
	be nothing to cut out, because the converter will write the one part to begin with.

	Choosing a part lays the score out again, so it belongs on the same thread as loading. */
	const std::vector<ScorePart>& parts() const { return allParts; }
	int part() const { return chosen; }
	bool setPart(int index, std::string* reason);

	/** HOW LARGE THE PAGE SAYS IT IS, in Verovio's own units, which is the only thing that decides
	how large the notation comes out. The page is drawn whatever size the caller draws it at, so a
	page that calls itself narrow is a page with fewer bars on a line, each of them bigger.

	Verovio's `scale` option is not this and is not used: it zooms the finished drawing without
	touching the layout, and the drawing here is zoomed by the caller's own transform anyway.

	Setting either lays the score out again, which is the slow thing this class does. */
	void setPage(int width, int height);
	int pageCount() const;

	/** Draws one page, counting from one, with the context already scaled and translated so that
	the page's own units land where they should — and told what that scale is, so that a line can
	be given a floor of one pixel. See pageScale. */
	void draw(NVGcontext* vg, int page, float pixelsPerUnit);

	/** ONE GLYPH, THE SIZE OF THE PICTURE: filled on the left, outlined on the right. For looking
	at a glyph that comes out wrong without hunting for it in a page of music. */
	void drawOneGlyph(NVGcontext* vg, char32_t code, int width, int height);

	/** What one of Verovio's units is worth in pixels, for a page drawn `pixels` wide. */
	float pageScale(float pixels) const;

	/** Where each bar of the last page drawn ended up, in Verovio units. */
	const std::vector<ScoreBox>& bars() const;

	/** What the last page drawn asked for, and what could not be drawn. See ScoreDraw::Tally. */
	const ScoreDraw* backend() const { return device; }

	/** HOW FAR DOWN THE LAST PAGE DRAWN ACTUALLY WENT, in Verovio units, or nought before
	anything has been drawn. Often more than the page's own height: see ScoreDraw::saw. */
	float drawnHeight() const;

	/** What the file called itself, for a title bar. */
	const std::string& title() const { return name; }

private:
	vrv::Toolkit* toolkit = NULL;
	ScoreDraw* device = NULL;
	std::string name;
	/** The file as it came, kept so that another part can be cut from it without reading it
	again. A long score is a few megabytes, which is less than the laid-out document beside it. */
	std::string source;
	std::vector<ScorePart> allParts;
	int chosen = 0;
	int width = 1500, height = 2000;

	/** The document with every part but `index` taken out of it, or the document unchanged when
	it has one part, when it is not MusicXML this can read, or when `index` is out of range. */
	std::string onePart(int index) const;
	bool loaded = false;
	void applyOptions();
};


} // namespace px
