#pragma once
/** VEROVIO'S DRAWING, DONE WITH RACK'S OWN RENDERER.

Verovio engraves a page and then hands every line, curve, glyph and word to a device context to
be drawn. It ships one that writes SVG; this is one that draws with NanoVG, which is what Rack
draws everything else with.

WHY NOT ITS SVG. A page comes out with every music glyph in a definitions block referenced by
`use`, and lettering as `text`. Rack's own SVG reader supports neither, so that route needs a
full SVG library and still ends in a picture that cannot be scaled without redrawing it.

Drawing straight through has two things going for it besides. The glyphs come out as paths at
whatever size is asked for, so the notation is as sharp magnified as it is small. And Verovio
brackets every element it draws with that element's identifier — so the box each bar occupies can
be recorded AS IT IS DRAWN, which is what a cursor, clicking a bar and tinting a loop all need
and what no picture of a page can give.

THE GLYPHS ARE PATHS ALREADY. Each one is a single SVG path in a file of its own, which nanosvg
reads in a few microseconds; parsed once, kept as the outline it is, and filled wherever it is
asked for. There is no font to rasterise and no text to measure.

See docs/notation.md.
*/
// VEROVIO FIRST, AND IT HAS TO BE. Rack defines APP as a macro and Verovio has an enumerator of
// that name, so whichever is included second loses — and the error it gives names neither.
#include <vrv/devicecontext.h>

#include "plugin.hpp"

#include <map>
#include <string>
#include <vector>

namespace px {


/** One music glyph, as the outline nanosvg made of it. Verovio's own units; the caller scales. */
struct ScoreGlyph {
	struct Contour {
		std::vector<float> points;   /**< Cubic béziers, as nanosvg gives them: six per segment. */
		bool closed = false;
		/** Which way round it runs, and so whether it is an outline or the white inside one. */
		double area = 0.0;
		bool hole = false;
	};
	std::vector<Contour> contours;
	bool loaded = false;
};


/** WHERE EVERY BAR ENDED UP, recorded as the page was drawn.

Verovio says what it is drawing before it draws it and says when it has finished, so the extent
of anything can be measured by watching what goes past between the two. Only measures are kept:
they are what the pointer is pointed at and what a loop is made of. */
struct ScoreBox {
	std::string id;
	float x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
	bool valid() const { return x2 > x1 && y2 > y1; }
};


struct ScoreDraw : vrv::DeviceContext {
	ScoreDraw();
	~ScoreDraw() override;

	/** Set before a page is drawn. The transform is the caller's: this draws in Verovio's own
	units and expects the context to be scaled and translated already. */
	NVGcontext* vg = NULL;

	/** HOW MANY PIXELS ONE OF VEROVIO'S UNITS COMES TO, which is the caller's own scale handed
	back so that a line can be given a floor in pixels.

	A staff line is a tenth of a staff space, thirteen units of a page fifteen thousand wide; on
	a screen that is two thirds of a pixel, and a two-thirds-pixel line is drawn as a grey one,
	however black the ink. The engraving is right and the screen cannot show it, so anything thinner
	than a pixel is widened to a pixel — which is what every other program drawing music on a
	screen does. */
	float pixelsPerUnit = 1.f;

	/** DARK MODE: the paper is dark and the ink is light.

	Verovio asks for black for everything it draws, because that is the one colour engraved music
	is in. It is swapped for this on the way out, and a colour Verovio asked for on purpose — the
	blue it marks a cursor in — is left alone. */
	bool dark = true;
	NVGcolor ink = nvgRGB(0xbf, 0xbf, 0xbf);

	/** THE GLYPHS ARE DRAWN DIMMER THAN THE LINES, and on a dark page they have to be.

	A staff line is a hairline: at full strength it reads as a line and at anything less it reads
	as a smudge, which is why it is drawn in the ink above. A clef is a quarter of an inch of solid
	shape, and the same full strength in something that size glares — it pulls the eye off the
	music and onto the furniture. Note heads are the same thing smaller.

	So lines keep the ink and anything drawn from the music font gets this. */
	NVGcolor glyphInk = nvgRGB(0x99, 0x99, 0x99);

	/** THE COLOUR OF THE PAGE UNDER THE INK, which has to be known in order to take ink away.

	A fret number in tablature sits ON a string line, and the line is broken behind it — that is
	not decoration, it is what makes the number readable at all, and a six-line staff with digits
	struck through by six lines is the one thing tablature must not look like. Verovio draws the
	number and leaves the line alone, so the break is made here, by painting the number's own box
	out before setting the number in it. Set by the window to whatever its paper is. */
	NVGcolor paper = nvgRGB(0x1c, 0x1e, 0x22);

	/** A FONT FOR THE LETTERING WHEN THERE IS NO RACK TO ASK FOR ONE.

	Words on a page — a fret number above all — come from Rack's own font, and off the rack there
	is no Rack. Without this the renderer in test/scorerender.cpp draws a page with every number
	missing and cannot be used to check the one thing tablature is made of. Negative when unset,
	which is the ordinary case inside Rack. */
	int fallbackFont = -1;

	/** The bars drawn on the page just rendered, in Verovio units. */
	std::vector<ScoreBox> bars;

	/** EVERYTHING DRAWN ON THAT PAGE, as one box. The music is often taller than the page says it
	is — see saw() — and this is how far it really went. */
	ScoreBox page;

	/** WHAT WAS ASKED FOR AND WHAT COULD NOT BE DONE.

	A glyph with no file, or a drawing call that is not written yet, draws NOTHING — and nothing
	is exactly what a page that is missing something looks like. A page can only be checked by
	eye if something else is counting, so this counts: every glyph asked for, every one that could
	not be found or read, and every call that is still a stub. A sweep over real scores then says
	whether any of them needs what is missing. */
	struct Tally {
		int glyphs = 0;          /**< Glyphs drawn. */
		int glyphsUnknown = 0;   /**< Asked for by code point, and the font has no such glyph. */
		int glyphsUnread = 0;    /**< The font has it and its file would not parse. */
		int words = 0;           /**< Runs of lettering drawn. */
		int wordsSkipped = 0;    /**< Lettering with no font to draw it in. */
		int svgShapes = 0;       /**< Artwork embedded in a score. Not drawn. */
		int rotations = 0;       /**< Text set at an angle. Not drawn. */
		/** The code points that were asked for and not found, so they can be named. */
		std::vector<char32_t> missing;
	};
	Tally tally;

	// ---- what a device context must answer ----------------------------------------------------

	void SetBackground(int colour, int style = vrv::PEN_SOLID) override;
	void SetBackgroundImage(void* image, double opacity = 1.0) override;
	void SetBackgroundMode(int mode) override;
	void SetTextForeground(int colour) override;
	void SetTextBackground(int colour) override;
	void SetLogicalOrigin(int x, int y) override;
	vrv::Point GetLogicalOrigin() override;

	void DrawQuadBezierPath(vrv::Point bezier[3]) override;
	void DrawCubicBezierPath(vrv::Point bezier[4]) override;
	void DrawCubicBezierPathFilled(vrv::Point bezier1[4], vrv::Point bezier2[4]) override;
	void DrawBentParallelogramFilled(vrv::Point side[4], int height) override;
	void DrawCircle(int x, int y, int radius) override;
	void DrawEllipse(int x, int y, int width, int height) override;
	void DrawEllipticArc(int x, int y, int width, int height, double start, double end) override;
	void DrawLine(int x1, int y1, int x2, int y2) override;
	void DrawPolyline(int n, vrv::Point points[], bool close = false) override;
	void DrawPolygon(int n, vrv::Point points[]) override;
	void DrawRectangle(int x, int y, int width, int height) override;
	void DrawRotatedText(const std::string& text, int x, int y, double angle) override;
	void DrawRoundedRectangle(int x, int y, int width, int height, int radius) override;
	void DrawText(const std::string& text, const std::u32string& wtext = U"", int x = VRV_UNSET,
		int y = VRV_UNSET, int width = VRV_UNSET, int height = VRV_UNSET) override;
	void DrawMusicText(const std::u32string& text, int x, int y,
		bool setSmuflGlyph = false) override;
	void DrawSpline(int n, vrv::Point points[]) override;
	void DrawGraphicUri(int x, int y, int width, int height, const std::string& uri) override;
	void DrawSvgShape(int x, int y, int width, int height, double scale,
		pugi::xml_node svg) override;
	void DrawBackgroundImage(int x = 0, int y = 0) override;

	void StartText(int x, int y,
		vrv::data_HORIZONTALALIGNMENT alignment = vrv::HORIZONTALALIGNMENT_left) override;
	void EndText() override;
	void MoveTextTo(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment) override;
	void MoveTextVerticallyTo(int y) override;

	void StartGraphic(vrv::Object* object, const std::string& gClass, const std::string& gId,
		vrv::GraphicID graphicID = vrv::PRIMARY, bool preprend = false) override;
	void EndGraphic(vrv::Object* object, vrv::View* view) override;
	void ResumeGraphic(vrv::Object* object, std::string gId) override;
	void EndResumedGraphic(vrv::Object* object, vrv::View* view) override;
	void RotateGraphic(vrv::Point const& orig, double angle) override;
	void StartPage() override;
	void EndPage() override;

	/** ONE GLYPH, AS LARGE AS THE PICTURE: filled on the left, outlined on the right, so the
	outline that was meant and the fill that came of it can be compared. For working on a glyph
	that draws wrong. */
	void drawGlyphLarge(char32_t code, int width, int height);

	/** THE GLYPHS, KEPT. Verovio lends the device its resources for the length of one render and
	takes them back at the end, so anything wanting a glyph outside a render has to have held on
	to them. Taken when a page starts. */
	const vrv::Resources* kept = NULL;

private:
	int originX = 0, originY = 0;
	/** Where the text being written sits, between StartText and EndText. */
	float textX = 0.f, textY = 0.f;
	vrv::data_HORIZONTALALIGNMENT textAlign = vrv::HORIZONTALALIGNMENT_left;

	/** The glyphs seen so far, by their code point. */
	std::map<char32_t, ScoreGlyph> glyphs;
	const ScoreGlyph* glyphFor(const vrv::Glyph* glyph, char32_t code);

	/** ONE FLAG PER OPEN GRAPHIC: whether a rotation was pushed inside it, and so whether one has
	to be taken off again when it ends. See RotateGraphic. */
	std::vector<bool> turned;

	/** Whether anything at all has been drawn on this page yet, for `page` above. */
	bool seenAnything = false;

	/** How deep inside a measure we are, and what it has covered so far. */
	int inMeasure = 0;
	ScoreBox measure;
	void saw(float x, float y);
	void sawBox(float x1, float y1, float x2, float y2);

	void strokeHere(float thinner = 1.f);
	void fillHere();
	float strokeWidth(float thinner = 1.f) const;
	NVGcolor asInk(NVGcolor colour, bool glyph = false) const;
	/** `glyph` picks the dimmer ink the music font is drawn in. See glyphInk. */
	NVGcolor penColour(bool glyph = false) const;
	NVGcolor brushColour(bool glyph = false) const;
	bool penIsOn() const;
	bool brushIsOn() const;
};


} // namespace px
