/** See ScoreDraw.hpp. */
// Verovio before anything of Rack's, for the reason given in the header.
#include <vrv/glyph.h>
#include <vrv/object.h>
#include <vrv/resources.h>
#include <vrv/vrvdef.h>

#include "ScoreDraw.hpp"

#include "vrv/measure.h"

#include <nanosvg.h>

#include <cmath>
#include <cstdio>

namespace px {

namespace {


/** Verovio keeps a colour as one number, red in the high byte. */
NVGcolor colourOf(int colour, float opacity) {
	if (colour == COLOR_NONE)
		colour = 0;
	const float a = (opacity >= 0.f && opacity <= 1.f) ? opacity : 1.f;
	return nvgRGBAf(((colour >> 16) & 0xFF) / 255.f, ((colour >> 8) & 0xFF) / 255.f,
		(colour & 0xFF) / 255.f, a);
}


/** Twice the signed area of a contour, whose SIGN is the only part wanted: which way round the
contour runs, and so whether it is an outline or a hole inside one. */
double contourArea(const std::vector<float>& pts) {
	double sum = 0.0;
	for (size_t i = 0; i + 3 < pts.size(); i += 2)
		sum += (double) pts[i] * pts[i + 3] - (double) pts[i + 2] * pts[i + 1];
	return sum;
}


/** A cubic through four points, which is most of what an engraver draws. */
void cubic(NVGcontext* vg, const vrv::Point* p) {
	nvgMoveTo(vg, (float) p[0].x, (float) p[0].y);
	nvgBezierTo(vg, (float) p[1].x, (float) p[1].y, (float) p[2].x, (float) p[2].y,
		(float) p[3].x, (float) p[3].y);
}


} // namespace


ScoreDraw::ScoreDraw() : vrv::DeviceContext() {}

ScoreDraw::~ScoreDraw() {}


// ---- the pen and the brush ---------------------------------------------------------------------

bool ScoreDraw::penIsOn() const {
	return !m_penStack.empty() && m_penStack.top().GetWidth() > 0;
}

bool ScoreDraw::brushIsOn() const {
	return !m_brushStack.empty();
}

/** Verovio's black, turned into the ink this is actually drawn in. Any other colour it asked for
it asked for deliberately and comes through as it is. */
NVGcolor ScoreDraw::asInk(NVGcolor colour, bool glyph) const {
	if (!dark || colour.r > 0.02f || colour.g > 0.02f || colour.b > 0.02f)
		return colour;
	NVGcolor out = glyph ? glyphInk : ink;
	out.a = colour.a;
	return out;
}

NVGcolor ScoreDraw::penColour(bool glyph) const {
	if (m_penStack.empty())
		return asInk(nvgRGB(0, 0, 0), glyph);
	const vrv::Pen& pen = m_penStack.top();
	return asInk(colourOf(pen.HasColor() ? pen.GetColor() : 0,
		pen.HasOpacity() ? pen.GetOpacity() : 1.f), glyph);
}

NVGcolor ScoreDraw::brushColour(bool glyph) const {
	if (m_brushStack.empty())
		return asInk(nvgRGB(0, 0, 0), glyph);
	const vrv::Brush& brush = m_brushStack.top();
	return asInk(colourOf(brush.HasColor() ? brush.GetColor() : 0,
		brush.HasOpacity() ? brush.GetOpacity() : 1.f), glyph);
}

/** The width to stroke at, never thinner than a pixel. See pixelsPerUnit.

`thinner` scales what the engraver asked for BEFORE the floor is applied, so a line asked to be
half as thick becomes half as thick where there is room for it and stays a pixel where there is
not. A line thinner than a pixel is not a thin line, it is a grey one. */
float ScoreDraw::strokeWidth(float thinner) const {
	const float asked =
		(m_penStack.empty() ? 1.f : (float) m_penStack.top().GetWidth()) * thinner;
	const float floorUnits = (pixelsPerUnit > 0.f) ? 1.1f / pixelsPerUnit : asked;
	return std::fmax(asked, floorUnits);
}

void ScoreDraw::strokeHere(float thinner) {
	if (!vg)
		return;
	nvgStrokeColor(vg, penColour());
	nvgStrokeWidth(vg, strokeWidth(thinner));
	nvgStroke(vg);
}

void ScoreDraw::fillHere() {
	if (!vg)
		return;
	nvgFillColor(vg, brushIsOn() ? brushColour() : penColour());
	nvgFill(vg);
}


// ---- where things ended up ----------------------------------------------------------------------

void ScoreDraw::saw(float x, float y) {
	// HOW FAR DOWN THE DRAWING ACTUALLY WENT, kept for everything and not only for measures.
	//
	// A page is as tall as it says it is, and the music on it is as tall as it needs to be. Those
	// are not the same number: a score of many parts at a small notation size has systems three
	// times the height of the page it is nominally on, and Verovio draws them anyway. A window
	// that scrolls to the page's own height would leave two thirds of that music unreachable, so
	// the window is told where the ink stopped instead.
	if (!seenAnything) {
		seenAnything = true;
		page.x1 = page.x2 = x;
		page.y1 = page.y2 = y;
	}
	else {
		page.x1 = std::fmin(page.x1, x);
		page.y1 = std::fmin(page.y1, y);
		page.x2 = std::fmax(page.x2, x);
		page.y2 = std::fmax(page.y2, y);
	}

	if (!inMeasure)
		return;
	if (!measure.valid() && measure.x1 == 0.f && measure.x2 == 0.f && measure.y1 == 0.f) {
		measure.x1 = measure.x2 = x;
		measure.y1 = measure.y2 = y;
		return;
	}
	measure.x1 = std::fmin(measure.x1, x);
	measure.y1 = std::fmin(measure.y1, y);
	measure.x2 = std::fmax(measure.x2, x);
	measure.y2 = std::fmax(measure.y2, y);
}

void ScoreDraw::sawBox(float x1, float y1, float x2, float y2) {
	saw(x1, y1);
	saw(x2, y2);
}


// ---- the glyphs ----------------------------------------------------------------------------------

const ScoreGlyph* ScoreDraw::glyphFor(const vrv::Glyph* glyph, char32_t code) {
	std::map<char32_t, ScoreGlyph>::iterator found = glyphs.find(code);
	if (found != glyphs.end())
		return found->second.loaded ? &found->second : NULL;

	ScoreGlyph& made = glyphs[code];
	made.loaded = false;
	if (!glyph)
		return NULL;

	// EACH GLYPH IS A FILE HOLDING ONE PATH, and it is a fragment rather than a document: a `g`
	// with a `path` in it, no `svg` element around it and so no size.
	//
	// WRAPPED IN AN SVG ELEMENT BEFORE IT IS PARSED, AND IT HAS TO BE. nanosvg takes the scale
	// from the root element's size against its viewBox, and with no root it falls back to the
	// bounding box of what it found — which NORMALISES EVERY GLYPH TO ITS OWN EXTENT. A note head
	// that belongs across the staff line, from minus 133 to plus 133, came out at 0 to 266: below
	// the line it should sit on, by its own height, and every glyph displaced by a different
	// amount. Any size will do here so long as the viewBox matches it, because then the scale is
	// one and the font's own coordinates come through untouched.
	const std::string file = glyph->GetPath();
	std::string body;
	{
		FILE* handle = std::fopen(file.c_str(), "rb");
		if (!handle)
			return NULL;
		char buf[4096];
		size_t got;
		while ((got = std::fread(buf, 1, sizeof(buf), handle)) > 0)
			body.append(buf, got);
		std::fclose(handle);
	}
	const std::string wrapped =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1000\" height=\"1000\""
		" viewBox=\"0 0 1000 1000\">" + body + "</svg>";

	// nsvgParse writes into the string it is given, so it gets a copy of its own.
	std::vector<char> scratch(wrapped.begin(), wrapped.end());
	scratch.push_back('\0');
	NSVGimage* image = nsvgParse(scratch.data(), "px", 96.f);
	if (!image)
		return NULL;
	for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
		for (NSVGpath* p = shape->paths; p; p = p->next) {
			ScoreGlyph::Contour contour;
			contour.closed = p->closed;
			contour.points.assign(p->pts, p->pts + (size_t) p->npts * 2);
			contour.area = contourArea(contour.points);
			made.contours.push_back(contour);
		}
	}
	nsvgDelete(image);

	// WHICH CONTOURS ARE HOLES. A clef or a flat has counters in it — the enclosed white — and a
	// font draws those by running them the opposite way round from the outline that holds them.
	// NanoVG wants to be told, so the largest contour decides which way round "solid" is and
	// anything running the other way is a hole.
	double biggest = 0.0;
	for (size_t i = 0; i < made.contours.size(); i++) {
		if (std::fabs(made.contours[i].area) > std::fabs(biggest))
			biggest = made.contours[i].area;
	}
	for (size_t i = 0; i < made.contours.size(); i++)
		made.contours[i].hole = (made.contours[i].area * biggest) < 0.0;

	made.loaded = !made.contours.empty();
	return made.loaded ? &made : NULL;
}


void ScoreDraw::DrawMusicText(const std::u32string& text, int x, int y, bool setSmuflGlyph) {
	(void) setSmuflGlyph;
	if (!vg || m_fontStack.empty())
		return;
	const vrv::Resources* resources = this->GetResources();
	if (!resources)
		return;

	// LETTERING COMES THROUGH HERE TOO, AND THAT IS NOT OBVIOUS.
	//
	// Verovio sets its own words — a fret number, a figure over a tuplet — in a text font it
	// ships as glyph files, exactly like the music font. So they arrive here rather than at
	// DrawText, and anything meant for "words" has to be done here or it is not done at all.
	// A face name is what tells them apart: the music font has none.
	const bool lettering = !m_fontStack.top()->GetFaceName().empty();

	// THE GROUND UNDER A FRET NUMBER, CLEARED FIRST. A number in tablature sits ON a string
	// line, and the line is broken behind it — six lines running through six digits is the one
	// thing tablature must not look like. The run is measured before any of it is drawn, because
	// the break has to be made before the first digit lands on it.
	if (lettering) {
		const double scale = (double) m_fontStack.top()->GetPointSize()
			/ 1000.0 * DEFINITION_FACTOR;
		float wide = 0.f, high = 0.f;
		for (char32_t c : text) {
			const vrv::Glyph* glyph = resources->GetGlyph(c);
			if (!glyph)
				continue;
			int gx = 0, gy = 0, w = 0, h = 0;
			glyph->GetBoundingBox(gx, gy, w, h);
			const double advance = (double) m_fontStack.top()->GetPointSize()
				/ glyph->GetUnitsPerEm();
			wide += (float) ((glyph->GetHorizAdvX() > 0 ? glyph->GetHorizAdvX() : w) * advance);
			high = std::fmax(high, (float) (h * advance));
		}
		if (wide > 0.f && high > 0.f) {
			(void) scale;
			// A LITTLE WIDER THAN THE DIGITS AND SHORTER THAN THEM: the width is where the line
			// has to stop, so it is given air; the height is kept inside the digits so clearing
			// one string never reaches the string above or below.
			const float sideways = high * 0.16f;
			const float shorter = high * 0.18f;
			nvgBeginPath(vg);
			nvgRect(vg, (float) x - sideways, (float) y - high + shorter,
				wide + sideways * 2.f, high - shorter * 2.f);
			nvgFillColor(vg, paper);
			nvgFill(vg);
		}
	}

	for (char32_t c : text) {
		const vrv::Glyph* glyph = resources->GetGlyph(c);
		if (!glyph) {
			tally.glyphsUnknown++;
			if (tally.missing.size() < 64)
				tally.missing.push_back(c);
			continue;
		}
		const ScoreGlyph* outline = glyphFor(glyph, c);
		if (outline)
			tally.glyphs++;
		else
			tally.glyphsUnread++;

		// THE SIZE VEROVIO ASKS FOR, AGAINST THE SIZE THE OUTLINE IS DRAWN AT — and these are two
		// different sums, which is the trap.
		//
		// The page is laid out in tenths of a unit, so a glyph drawn into it is scaled by the
		// point size over the em AND BY THAT TENTH: without the ten, every glyph comes out a
		// tenth of its size, which on a staff is a speck. An ADVANCE is not scaled by it, because
		// an advance is a step across the page in the page's own units. Verovio's own SVG writer
		// does exactly this, and the two sums sit two lines apart there as well.
		const double advance = (double) m_fontStack.top()->GetPointSize() / glyph->GetUnitsPerEm();
		const double scale = advance * DEFINITION_FACTOR;
		double scaleX = scale;
		if (m_fontStack.top()->GetWidthToHeightRatio() != 1.0f)
			scaleX *= m_fontStack.top()->GetWidthToHeightRatio();

		if (outline) {
			nvgSave(vg);
			nvgTranslate(vg, (float) x, (float) y);
			// NO FLIP HERE. The outline is drawn upwards and the page downwards, and every one
			// of these glyph files turns it over itself with a scale of minus one in Y — so a
			// second flip puts the glyph back upside down and above the line it belongs on.
			nvgScale(vg, (float) scaleX, (float) scale);
			nvgBeginPath(vg);
			for (size_t i = 0; i < outline->contours.size(); i++) {
				const ScoreGlyph::Contour& contour = outline->contours[i];
				const std::vector<float>& pts = contour.points;
				if (pts.size() < 2)
					continue;
				nvgMoveTo(vg, pts[0], pts[1]);
				for (size_t k = 0; k + 7 < pts.size(); k += 6) {
					nvgBezierTo(vg, pts[k + 2], pts[k + 3], pts[k + 4], pts[k + 5],
						pts[k + 6], pts[k + 7]);
				}
				if (contour.closed)
					nvgClosePath(vg);
				// The white inside a clef or a flat. Declared after the contour it belongs to,
				// which is how NanoVG takes it.
				nvgPathWinding(vg, contour.hole ? NVG_HOLE : NVG_SOLID);
			}
			nvgFillColor(vg, brushIsOn() ? brushColour(true) : penColour(true));
			nvgFill(vg);
			nvgRestore(vg);
		}

		// Where the next character of the same word goes, and how much of the page this one took.
		int gx = 0, gy = 0, w = 0, h = 0;
		// WHAT THE GLYPH COVERS, IN THE PAGE'S UNITS.
		//
		// Verovio gives the box in the font's own units with Y UPWARDS, and the page runs
		// downwards — so the box turns over, and what was the bottom becomes the top. The scale
		// is the ADVANCE scale and not the drawing one: the drawing scale carries the page's
		// extra tenth, and a box measured in page units must not.
		//
		// It used to take the height alone and hang it above the baseline, which is right only
		// for a glyph that sits on the line. A note head straddles it and a flag hangs below it,
		// so every box was out by half a glyph in a direction that depended on the glyph.
		glyph->GetBoundingBox(gx, gy, w, h);
		sawBox((float) (x + gx * advance), (float) (y - (gy + h) * advance),
			(float) (x + (gx + w) * advance), (float) (y - gy * advance));
		if (glyph->GetHorizAdvX() > 0)
			x += (int) (glyph->GetHorizAdvX() * advance);
		else
			x += (int) (w * advance);
	}
}


// ---- lines and shapes ------------------------------------------------------------------------------

void ScoreDraw::DrawLine(int x1, int y1, int x2, int y2) {
	// MEASURED WHETHER OR NOT THERE IS ANYTHING TO DRAW ON. A render with no context is how the
	// extent of every system is found without putting any of them on the screen, which is what a
	// continuous view needs before it can stack them. See Score::systems.
	sawBox((float) std::min(x1, x2), (float) std::min(y1, y2),
		(float) std::max(x1, x2), (float) std::max(y1, y2));
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) x1, (float) y1);
	nvgLineTo(vg, (float) x2, (float) y2);
	// THE LINES THAT RUN ALONG THE PAGE ARE DRAWN THINNER THAN THE REST.
	//
	// A staff line, a tablature string and a ledger line are the ruled paper the music is written
	// on, not part of the music: they are there to be measured against and should not compete
	// with what is written over them. Six strings at the weight of a stem is a page of lines with
	// some numbers in it. Barlines and stems are upright and keep their full weight, since those
	// ARE the music.
	// A RULED LINE IS PUT ON A PIXEL. A staff line, a ledger line and a barline are all one
	// straight line of a single weight, and a line a pixel wide landing between two rows of
	// pixels is drawn across both at part strength each — which is not a thin line, it is two
	// grey ones, and while the music scrolls it reads as the staff going soft. Seated on the
	// middle of a row and given exactly one pixel, it is the same line wherever the music has
	// scrolled to.
	//
	// ASKED OF THE TRANSFORM IN FORCE, not of this function's own coordinates: the score is drawn
	// through its own scale and Rack through a zoom on top of that, so where a line really lands
	// is only known at the bottom of the stack. See ScoreWindow::snapToPixels, which does the
	// same for the system as a whole.
	if (y1 == y2 || x1 == x2) {
		float xf[6] = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
		nvgCurrentTransform(vg, xf);
		const float sx = xf[0], sy = xf[3];
		if (std::fabs(sx) > 1e-6f && std::fabs(sy) > 1e-6f) {
			nvgBeginPath(vg);
			if (y1 == y2) {
				const float device = (float) y1 * sy + xf[5];
				const float seated = std::floor(device) + 0.5f;
				const float y = (seated - xf[5]) / sy;
				nvgMoveTo(vg, (float) x1, y);
				nvgLineTo(vg, (float) x2, y);
				nvgStrokeWidth(vg, 1.f / std::fabs(sy));
			}
			else {
				const float device = (float) x1 * sx + xf[4];
				const float seated = std::floor(device) + 0.5f;
				const float x = (seated - xf[4]) / sx;
				nvgMoveTo(vg, x, (float) y1);
				nvgLineTo(vg, x, (float) y2);
				nvgStrokeWidth(vg, std::fmax(1.f / std::fabs(sx), strokeWidth(1.f)));
			}
			nvgStrokeColor(vg, penColour());
			nvgStroke(vg);
			return;
		}
	}
	strokeHere((y1 == y2) ? 0.5f : 1.f);
}


void ScoreDraw::DrawPolyline(int n, vrv::Point points[], bool close) {
	if (n < 2)
		return;
	for (int i = 0; i < n; i++)
		saw((float) points[i].x, (float) points[i].y);
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) points[0].x, (float) points[0].y);
	for (int i = 1; i < n; i++) {
		nvgLineTo(vg, (float) points[i].x, (float) points[i].y);
	}
	if (close)
		nvgClosePath(vg);
	strokeHere();
}


void ScoreDraw::DrawPolygon(int n, vrv::Point points[]) {
	if (n < 2)
		return;
	for (int i = 0; i < n; i++)
		saw((float) points[i].x, (float) points[i].y);
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) points[0].x, (float) points[0].y);
	for (int i = 1; i < n; i++) {
		nvgLineTo(vg, (float) points[i].x, (float) points[i].y);
	}
	nvgClosePath(vg);
	fillHere();
}


void ScoreDraw::DrawRectangle(int x, int y, int width, int height) {
	sawBox((float) x, (float) y, (float) (x + width), (float) (y + height));
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgRect(vg, (float) x, (float) y, (float) width, (float) height);
	fillHere();
}


void ScoreDraw::DrawRoundedRectangle(int x, int y, int width, int height, int radius) {
	sawBox((float) x, (float) y, (float) (x + width), (float) (y + height));
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, (float) x, (float) y, (float) width, (float) height, (float) radius);
	fillHere();
}


void ScoreDraw::DrawCircle(int x, int y, int radius) {
	DrawEllipse(x - radius, y - radius, radius * 2, radius * 2);
}


void ScoreDraw::DrawEllipse(int x, int y, int width, int height) {
	sawBox((float) x, (float) y, (float) (x + width), (float) (y + height));
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgEllipse(vg, (float) x + width / 2.f, (float) y + height / 2.f,
		width / 2.f, height / 2.f);
	fillHere();
}


/** An arc of an ellipse, which Verovio asks for in degrees and NanoVG wants in radians going the
other way: its Y runs down the page, so an angle measured anticlockwise is drawn clockwise. */
void ScoreDraw::DrawEllipticArc(int x, int y, int width, int height, double start, double end) {
	sawBox((float) x, (float) y, (float) (x + width), (float) (y + height));
	if (!vg)
		return;
	const float cx = (float) x + width / 2.f;
	const float cy = (float) y + height / 2.f;
	const float rx = width / 2.f;
	const float ry = height / 2.f;
	const int steps = 24;
	nvgBeginPath(vg);
	for (int i = 0; i <= steps; i++) {
		const double a = (start + (end - start) * i / (double) steps) * M_PI / 180.0;
		const float px = cx + rx * (float) std::cos(a);
		const float py = cy - ry * (float) std::sin(a);
		if (i == 0)
			nvgMoveTo(vg, px, py);
		else
			nvgLineTo(vg, px, py);
	}
	strokeHere();
	sawBox((float) x, (float) y, (float) (x + width), (float) (y + height));
}


void ScoreDraw::DrawQuadBezierPath(vrv::Point bezier[3]) {
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) bezier[0].x, (float) bezier[0].y);
	nvgQuadTo(vg, (float) bezier[1].x, (float) bezier[1].y,
		(float) bezier[2].x, (float) bezier[2].y);
	strokeHere();
	for (int i = 0; i < 3; i++)
		saw((float) bezier[i].x, (float) bezier[i].y);
}


void ScoreDraw::DrawCubicBezierPath(vrv::Point bezier[4]) {
	if (!vg)
		return;
	nvgBeginPath(vg);
	cubic(vg, bezier);
	strokeHere();
	for (int i = 0; i < 4; i++)
		saw((float) bezier[i].x, (float) bezier[i].y);
}


/** A SLUR IS TWO CURVES AND THE SPACE BETWEEN THEM: out along the first, back along the second,
filled. Drawn as two strokes it would be a pair of hairlines rather than a slur that thickens in
the middle. */
void ScoreDraw::DrawCubicBezierPathFilled(vrv::Point bezier1[4], vrv::Point bezier2[4]) {
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) bezier1[0].x, (float) bezier1[0].y);
	nvgBezierTo(vg, (float) bezier1[1].x, (float) bezier1[1].y,
		(float) bezier1[2].x, (float) bezier1[2].y, (float) bezier1[3].x, (float) bezier1[3].y);
	nvgLineTo(vg, (float) bezier2[3].x, (float) bezier2[3].y);
	nvgBezierTo(vg, (float) bezier2[2].x, (float) bezier2[2].y,
		(float) bezier2[1].x, (float) bezier2[1].y, (float) bezier2[0].x, (float) bezier2[0].y);
	nvgClosePath(vg);
	fillHere();
	for (int i = 0; i < 4; i++) {
		saw((float) bezier1[i].x, (float) bezier1[i].y);
		saw((float) bezier2[i].x, (float) bezier2[i].y);
	}
}


/** A beam: four corners and a thickness, which is a filled quadrilateral. */
void ScoreDraw::DrawBentParallelogramFilled(vrv::Point side[4], int height) {
	if (!vg)
		return;
	nvgBeginPath(vg);
	nvgMoveTo(vg, (float) side[0].x, (float) side[0].y);
	for (int i = 1; i < 4; i++)
		nvgLineTo(vg, (float) side[i].x, (float) side[i].y);
	for (int i = 3; i >= 0; i--)
		nvgLineTo(vg, (float) side[i].x, (float) (side[i].y + height));
	nvgClosePath(vg);
	fillHere();
	for (int i = 0; i < 4; i++) {
		saw((float) side[i].x, (float) side[i].y);
		saw((float) side[i].x, (float) (side[i].y + height));
	}
}


/** A tie or a slur given as points to be smoothed through. Drawn as a line through them, which is
right to within a hair at the sizes this is read at. */
void ScoreDraw::DrawSpline(int n, vrv::Point points[]) {
	DrawPolyline(n, points, false);
}


// ---- words ------------------------------------------------------------------------------------

void ScoreDraw::StartText(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment) {
	textX = (float) x;
	textY = (float) y;
	textAlign = alignment;
}

void ScoreDraw::EndText() {}

void ScoreDraw::MoveTextTo(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment) {
	textX = (float) x;
	if (y != VRV_UNSET)
		textY = (float) y;
	textAlign = alignment;
}

void ScoreDraw::MoveTextVerticallyTo(int y) {
	textY = (float) y;
}


/** THE LETTERING — dynamics, rehearsal marks, bar numbers, tempo — drawn with the host's own
text, since it is ordinary type rather than music. The music font is never used here: Verovio
sends glyphs through DrawMusicText and words through this. */
void ScoreDraw::DrawText(const std::string& text, const std::u32string& wtext, int x, int y,
		int width, int height) {
	(void) wtext;
	(void) width;
	(void) height;
	if (!vg || text.empty() || m_fontStack.empty())
		return;
	// LETTERING COMES FROM RACK'S OWN FONT, and off the rack there is no Rack — see fallbackFont.
	int face = fallbackFont;
	if (APP && APP->window) {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font && font->handle >= 0)
			face = font->handle;
	}
	if (face < 0) {
		tally.wordsSkipped++;
		return;
	}

	const float at = (x == VRV_UNSET) ? textX : (float) x;
	const float on = (y == VRV_UNSET) ? textY : (float) y;

	nvgSave(vg);
	nvgFontFaceId(vg, face);
	nvgFontSize(vg, (float) m_fontStack.top()->GetPointSize());
	int align = NVG_ALIGN_LEFT;
	if (textAlign == vrv::HORIZONTALALIGNMENT_center)
		align = NVG_ALIGN_CENTER;
	else if (textAlign == vrv::HORIZONTALALIGNMENT_right)
		align = NVG_ALIGN_RIGHT;
	nvgTextAlign(vg, align | NVG_ALIGN_BASELINE);

	// THE GROUND UNDER THE WORDS, CLEARED FIRST. See `paper` in the header: this is what breaks
	// a tablature string line behind a fret number.
	//
	// A LITTLE WIDER THAN THE LETTERS AND NOT AS TALL. The width is where the line has to stop,
	// so it is given air on both sides; the height is kept inside the letters so that clearing
	// one line never reaches the line above or below it.
	float box[4] = {0.f, 0.f, 0.f, 0.f};
	nvgTextBounds(vg, at, on, text.c_str(), NULL, box);
	{
		const float size = (float) m_fontStack.top()->GetPointSize();
		const float sideways = size * 0.18f;
		const float shorter = (box[3] - box[1]) * 0.17f;
		nvgBeginPath(vg);
		nvgRect(vg, box[0] - sideways, box[1] + shorter,
			(box[2] - box[0]) + sideways * 2.f, (box[3] - box[1]) - shorter * 2.f);
		nvgFillColor(vg, paper);
		nvgFill(vg);
	}

	nvgFillColor(vg, brushIsOn() ? brushColour() : penColour());
	nvgText(vg, at, on, text.c_str(), NULL);
	nvgRestore(vg);

	float bounds[4] = {0.f, 0.f, 0.f, 0.f};
	nvgTextBounds(vg, at, on, text.c_str(), NULL, bounds);
	sawBox(bounds[0], bounds[1], bounds[2], bounds[3]);
	textX = bounds[2];
}


void ScoreDraw::DrawRotatedText(const std::string& text, int x, int y, double angle) {
	if (!vg)
		return;
	nvgSave(vg);
	nvgTranslate(vg, (float) x, (float) y);
	nvgRotate(vg, (float) (-angle * M_PI / 180.0));
	DrawText(text, U"", 0, 0);
	nvgRestore(vg);
}


// ---- the ones that do nothing here ----------------------------------------------------------------

void ScoreDraw::SetBackground(int colour, int style) { (void) colour; (void) style; }
void ScoreDraw::SetBackgroundImage(void* image, double opacity) { (void) image; (void) opacity; }
void ScoreDraw::SetBackgroundMode(int mode) { (void) mode; }
void ScoreDraw::SetTextForeground(int colour) { (void) colour; }
void ScoreDraw::SetTextBackground(int colour) { (void) colour; }
void ScoreDraw::DrawBackgroundImage(int x, int y) { (void) x; (void) y; }
void ScoreDraw::DrawGraphicUri(int x, int y, int width, int height, const std::string& uri) {
	(void) x; (void) y; (void) width; (void) height; (void) uri;
}
/** An SVG dropped into the score — a fingering diagram or a maker's mark. Not drawn yet; the
files this reads carry none. */
/** ARTWORK EMBEDDED IN A SCORE — a drawing in the score's own file rather than anything from the
music font. Counted and not drawn: it needs a general SVG reader, and the sweep over real scores
says whether any of them carries one. */
void ScoreDraw::DrawSvgShape(int x, int y, int width, int height, double scale,
		pugi::xml_node svg) {
	(void) x; (void) y; (void) width; (void) height; (void) scale; (void) svg;
	tally.svgShapes++;
}

void ScoreDraw::SetLogicalOrigin(int x, int y) {
	originX = x;
	originY = y;
}

vrv::Point ScoreDraw::GetLogicalOrigin() {
	vrv::Point point;
	point.x = originX;
	point.y = originY;
	return point;
}


// ---- what is being drawn, and where it went -----------------------------------------------------

void ScoreDraw::StartGraphic(vrv::Object* object, const std::string& gClass,
		const std::string& gId, vrv::GraphicID graphicID, bool preprend) {
	(void) gClass;
	(void) graphicID;
	(void) preprend;
	// A MEASURE IS THE ONE THING WORTH MEASURING. Everything inside it is drawn between this and
	// the matching end, so its extent is whatever went past in between.
	if (object && object->Is(vrv::MEASURE)) {
		if (inMeasure == 0) {
			measure = ScoreBox();
			measure.id = gId;
			if (const vrv::Measure* m = dynamic_cast<const vrv::Measure*>(object))
				measure.number = m->GetN();
		}
		inMeasure++;
	}
	// Every graphic gets a place on the stack, because a rotation asked for inside one has to be
	// taken off again when that one ends and not when some other one does.
	turned.push_back(false);
}


void ScoreDraw::EndGraphic(vrv::Object* object, vrv::View* view) {
	(void) view;
	if (object && object->Is(vrv::MEASURE) && inMeasure > 0) {
		inMeasure--;
		if (inMeasure == 0 && measure.valid())
			bars.push_back(measure);
	}
	if (!turned.empty()) {
		if (turned.back() && vg)
			nvgRestore(vg);
		turned.pop_back();
	}
}


void ScoreDraw::ResumeGraphic(vrv::Object* object, std::string gId) {
	StartGraphic(object, "", gId);
}

void ScoreDraw::EndResumedGraphic(vrv::Object* object, vrv::View* view) {
	EndGraphic(object, view);
}

/** EVERYTHING DRAWN FROM HERE TO THE END OF THIS GRAPHIC, TURNED.

An arpeggio sign — the wavy stroke down the front of a strummed chord — is a horizontal glyph in
the font, and Verovio puts it on its side rather than carrying a second glyph for it. A wavy
glissando is the same thing at whatever angle the two notes make. So this is not a nicety: without
it those signs are drawn lying flat, which in guitar music is often.

IT LASTS UNTIL THE GRAPHIC ENDS, which is why the transform is pushed here and taken off in
EndGraphic. One to a graphic, as Verovio's own SVG has it: a second call inside the same graphic
would otherwise turn what is already turned. */
void ScoreDraw::RotateGraphic(vrv::Point const& orig, double angle) {
	tally.rotations++;
	if (!vg || turned.empty() || turned.back())
		return;
	nvgSave(vg);
	nvgTranslate(vg, (float) orig.x, (float) orig.y);
	nvgRotate(vg, (float) (angle * M_PI / 180.0));
	nvgTranslate(vg, (float) -orig.x, (float) -orig.y);
	turned.back() = true;
}

void ScoreDraw::drawGlyphLarge(char32_t code, int width, int height) {
	if (!vg)
		return;
	const vrv::Resources* resources = this->GetResources() ? this->GetResources() : kept;
	if (!resources) {
		std::printf("no resources, so no glyphs\n");
		return;
	}
	const vrv::Glyph* glyph = resources->GetGlyph(code);
	if (!glyph) {
		std::printf("no glyph for %04X\n", (unsigned) code);
		return;
	}
	const ScoreGlyph* outline = glyphFor(glyph, code);
	if (!outline) {
		std::printf("glyph %04X would not load\n", (unsigned) code);
		return;
	}

	std::printf("glyph %04X: %d contours, units per em %d\n",
		(unsigned) code, (int) outline->contours.size(), glyph->GetUnitsPerEm());

	// WHAT VEROVIO SAYS THE GLYPH COVERS, AGAINST WHAT IT ACTUALLY COVERS. The first is what a
	// bar's extent is worked out from and the second is what lands on the page, and if the two
	// disagree then every box recorded is wrong in the same direction.
	int gx = 0, gy = 0, gw = 0, gh = 0;
	glyph->GetBoundingBox(gx, gy, gw, gh);
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (size_t i = 0; i < outline->contours.size(); i++) {
		const std::vector<float>& pts = outline->contours[i].points;
		for (size_t k = 0; k + 1 < pts.size(); k += 2) {
			x0 = std::fmin(x0, pts[k]);
			x1 = std::fmax(x1, pts[k]);
			y0 = std::fmin(y0, pts[k + 1]);
			y1 = std::fmax(y1, pts[k + 1]);
		}
	}
	std::printf("   Verovio says  x %d..%d  y %d..%d  (y upwards)\n", gx, gx + gw, gy, gy + gh);
	std::printf("   outline is    x %.0f..%.0f  y %.0f..%.0f  (y downwards, as drawn)\n",
		x0, x1, y0, y1);
	std::printf("   so turned over, Verovio's box is y %d..%d\n", -(gy + gh), -gy);
	for (size_t i = 0; i < outline->contours.size(); i++) {
		std::printf("   contour %d: %d points, %s, %s\n", (int) i,
			(int) (outline->contours[i].points.size() / 2),
			outline->contours[i].closed ? "closed" : "open",
			outline->contours[i].hole ? "hole" : "solid");
	}

	for (int side = 0; side < 2; side++) {
		nvgSave(vg);
		nvgTranslate(vg, width * (side ? 0.72f : 0.28f), height * 0.5f);
		const float size = (float) height * 0.3f / 266.f;   // The note head is 266 units tall.
		nvgScale(vg, size, size);
		nvgBeginPath(vg);
		for (size_t i = 0; i < outline->contours.size(); i++) {
			const ScoreGlyph::Contour& contour = outline->contours[i];
			const std::vector<float>& pts = contour.points;
			if (pts.size() < 2)
				continue;
			nvgMoveTo(vg, pts[0], pts[1]);
			for (size_t k = 0; k + 7 < pts.size(); k += 6) {
				nvgBezierTo(vg, pts[k + 2], pts[k + 3], pts[k + 4], pts[k + 5],
					pts[k + 6], pts[k + 7]);
			}
			if (contour.closed)
				nvgClosePath(vg);
			nvgPathWinding(vg, contour.hole ? NVG_HOLE : NVG_SOLID);
		}
		if (side) {
			nvgStrokeColor(vg, nvgRGB(0xff, 0x90, 0x30));
			nvgStrokeWidth(vg, 4.f / size);
			nvgStroke(vg);
		}
		else {
			nvgFillColor(vg, glyphInk);
			nvgFill(vg);
		}
		nvgRestore(vg);
	}
}


void ScoreDraw::StartPage() {
	kept = this->GetResources();
	tally = Tally();
	turned.clear();
	bars.clear();
	page = ScoreBox();
	seenAnything = false;
	inMeasure = 0;
	measure = ScoreBox();
}

void ScoreDraw::EndPage() {}


} // namespace px
