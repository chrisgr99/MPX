/** See ScoreWindow.hpp. */
// Verovio is reached through Score, which keeps its headers to itself — but Score.hpp forward
// declares vrv::Toolkit only, so nothing here has to be included in a particular order.
#include "ScoreWindow.hpp"

#include "GpMusicXML.hpp"
#include "GuitarPro.hpp"
#include "Score.hpp"
#include "ScoreDraw.hpp"
#include "plugin.hpp"

#include <atomic>
#include <cmath>
#include <thread>

namespace px {

namespace {


/** THE SAME FURNITURE AS THE CHART WINDOW, and deliberately the same numbers: two windows in one
plugin that are dragged and resized differently are two things to learn. */
const float SW_MIN_W = 420.f;
const float SW_MIN_H = 260.f;
const float SW_TITLE = 22.f;
const float SW_GRIP = 14.f;
const float SW_EDGE_GRAB = 7.f;

/** DARK PAPER AND LIGHT INK, like everything else on the rack. The page is a shade up from the
window's own ground, so the paper reads as paper and not as a hole in the window. */
const NVGcolor SW_PAPER = nvgRGB(0x1c, 0x1e, 0x22);
const NVGcolor SW_BACK = nvgRGB(0x12, 0x14, 0x18);
const NVGcolor SW_BAND = nvgRGB(0x2e, 0x2e, 0x2e);
const NVGcolor SW_INK = nvgRGB(0xff, 0xff, 0xff);

/** HOW LARGE THE NOTATION IS, IS HOW NARROW THE PAGE IS.

The page is drawn the full width of the window whatever it says it measures, so a page that calls
itself 900 units wide and one that calls itself 3600 come out the same size on the screen — and the
narrow one has a quarter as many bars on a line, each of them four times the size. That is the
control: fewer, larger bars or more, smaller ones.

NOT VEROVIO'S `scale` OPTION, which sounds like this and is not. It zooms the finished drawing and
leaves the layout exactly as it was, so turning it up makes a bigger picture of the same crowded
page. It is left at a hundred here and the page's own width does the work.

Smaller numbers mean larger notation, so the button marked plus subtracts. */
const int SW_UNITS_MIN = 700;
const int SW_UNITS_MAX = 3600;
const int SW_UNITS_STEP = 200;

/** The page's shape, fixed: portrait, three to four, so that changing how large the notation is
does not also change the shape of the paper under it. */
const float SW_PAGE_ASPECT = 4.f / 3.f;


/** Where the window was last dragged to, or negative when nobody has moved it. */
math::Vec gScoreWhere = math::Vec(-1.f, -1.f);


struct ScoreWindow;
ScoreWindow* gScoreWindow = NULL;


struct ScoreWindow : widget::OpaqueWidget {
	/** Which module asked for the window, so the window goes when the module does. */
	int64_t moduleId = -1;
	std::string path, name, trouble;
	/** THE SONG, WHERE THERE IS ONE. A window opened on a Guitar Pro file holds the song and
	writes a track of it out whenever the instrument changes; one opened on a MusicXML file holds
	a path and cuts the part out of the document instead. Only one of the two is ever set. */
	std::shared_ptr<const GpSong> song;

	/** THE SCORE, AND THE THREAD THAT LAYS IT OUT.

	Seventeen parts of a long song take Verovio over a second, which is thirty dropped frames if
	it happens where the drawing happens. So every slow thing — loading a file and laying it out
	again at a new size — is done here, and drawing is not allowed near the score until `ready`
	says the thread has finished with it. */
	Score score;
	std::thread worker;
	std::atomic<bool> working{false};
	std::atomic<bool> ready{false};
	/** How wide the page says it is, which is how large the notation comes out. What the worker
	should use next, and what it last used. */
	int wantUnits = 1500;
	int unitsNow = 0;
	/** Which instrument is being shown, and which one the worker last fetched. */
	int wantPart = 0;
	int partNow = 0;
	/** Which staves the music is written on, and which the worker last wrote. See GpStaves. */
	GpStaves wantStaves = GP_BOTH;
	GpStaves stavesNow = GP_BOTH;

	int page = 1;
	/** How far down the page the view is, in pixels of the page as drawn. */
	float scroll = 0.f;
	/** 0 nothing, 1 moving, 2 resizing. */
	int drag = 0;
	int edges = 0;

	ScoreWindow() {
		box.size = math::Vec(760.f, 620.f);
		box.pos = math::Vec(140.f, 70.f);
	}

	~ScoreWindow() override {
		if (worker.joinable())
			worker.join();
	}

	// ---- the slow work ---------------------------------------------------------------------------

	/** Starts the worker on whatever needs doing. Does nothing while one is already running: the
	buttons set `wantScale` and the next finish picks it up, so holding a button down cannot start
	a queue of layouts. */
	void beginWork(bool load) {
		if (working.load())
			return;
		if (worker.joinable())
			worker.join();
		working.store(true);
		ready.store(false);
		const int units = wantUnits;
		const int part = wantPart;
		const GpStaves staves = wantStaves;
		const std::string file = path;
		const std::shared_ptr<const GpSong> carried = song;
		worker = std::thread([this, load, units, part, staves, file, carried]() {
			std::string reason;
			bool ok = true;
			score.setPage(units, (int) (units * SW_PAGE_ASPECT));
			if (carried) {
				// FROM THE SONG ITSELF, one track written out as MusicXML. Converting is quick —
				// thousandths of a second against the tenths that laying it out takes — so there
				// is nothing to be gained by keeping the last one.
				if (load || part != partNow || staves != stavesNow) {
					std::string note;
					const std::string xml = gpToMusicXML(*carried, part, staves, &note);
					if (xml.empty()) {
						reason = "that instrument could not be written out";
						ok = false;
					}
					else {
						ok = score.load(xml, &reason);
						if (ok && !note.empty())
							reason = note;
					}
				}
				if (ok) {
					partNow = part;
					stavesNow = staves;
				}
			}
			else {
				if (load)
					ok = score.loadFile(file, &reason);     // Which opens on the first part.
				else if (part != score.part())
					ok = score.setPart(part, &reason);
				partNow = score.part();
			}
			unitsNow = units;
			trouble = ok ? "" : reason;
			ready.store(ok);
			working.store(false);
		});
	}

	/** THE LIST OF INSTRUMENTS, as Rack's own menu.

	A menu rather than anything drawn here: a score can have twenty parts with long names, which
	is a list that has to scroll, sit inside the window, and go away when you click elsewhere —
	all of which Rack's menu already does.

	Nothing is offered while the score is being laid out, because the list is read from the
	document the worker is rebuilding. */
	/** How many instruments there are to choose between, from whichever source the window has. */
	int partCount() {
		if (song)
			return (int) song->tracks.size();
		return (int) score.parts().size();
	}

	std::string partNamed(int index) {
		if (song) {
			if (index < 0 || index >= (int) song->tracks.size())
				return "";
			const std::string& said = song->tracks[(size_t) index].name;
			// A TRACK WITH NO NAME IS STILL AN INSTRUMENT. Plenty of transcriptions leave the
			// name empty, and a chooser full of blank rows is a chooser nobody can use.
			return said.empty() ? ("Track " + std::to_string(index + 1)) : said;
		}
		if (index < 0 || index >= (int) score.parts().size())
			return "";
		return score.parts()[(size_t) index].name;
	}

	/** Shows the music on other staves. Written out again and laid out again, so, the worker. */
	void setStaves(GpStaves to) {
		if (to == wantStaves)
			return;
		wantStaves = to;
		page = 1;
		scroll = 0.f;
		beginWork(false);
	}

	/** EVERYTHING ABOUT WHAT IS SHOWN, UNDER ONE BUTTON: which instrument, and written how.
	
	Two buttons would be tidier in the code and worse on the strip, which is an inch of window
	already carrying four controls and two crosses. They are also one question — what am I
	looking at — so they are one menu.

	NOTATION, TABLATURE OR BOTH is offered only for a Guitar Pro song, because only a Guitar Pro
	song knows what string and fret each note is played at. A MusicXML file from elsewhere is
	written however it was written. */
	void offerParts() {
		if (!usable())
			return;
		if (partCount() < 2 && !song)
			return;
		ui::Menu* menu = createMenu();

		if (song) {
			menu->addChild(createMenuLabel("Written as"));
			static const struct { const char* name; GpStaves staves; } FORMS[] = {
				{"Notation", GP_NOTATION},
				{"Tablature", GP_TAB},
				{"Notation and tablature", GP_BOTH},
			};
			for (int i = 0; i < 3; i++) {
				const GpStaves which = FORMS[i].staves;
				menu->addChild(createCheckMenuItem(FORMS[i].name, "",
					[=]() { return wantStaves == which; },
					[=]() { setStaves(which); }));
			}
		}

		if (partCount() > 1) {
			if (song)
				menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuLabel("Instrument"));
			for (int i = 0; i < partCount(); i++) {
				const int index = i;
				menu->addChild(createCheckMenuItem(partNamed(i), "",
					[=]() { return wantPart == index; },
					[=]() { setPart(index); }));
			}
		}
	}

	/** Shows another instrument. Laying the score out again, so it goes to the worker. */
	void setPart(int index) {
		if (index == wantPart)
			return;
		wantPart = index;
		page = 1;
		scroll = 0.f;
		beginWork(false);
	}

	/** What the strip says is being shown, or nothing when the file has only one instrument in
	it and there is nothing to choose. */
	std::string partName() {
		if (!usable() || partCount() < 2)
			return "";
		return partNamed(song ? partNow : score.part());
	}

	/** Larger notation is a narrower page, which is why this takes a step rather than a size. */
	void stepSize(int by) {
		wantUnits = math::clamp(wantUnits - by * SW_UNITS_STEP, SW_UNITS_MIN, SW_UNITS_MAX);
		if (wantUnits == unitsNow)
			return;
		scroll = 0.f;
		beginWork(false);
	}

	void step() override {
		widget::OpaqueWidget::step();
		// A SIZE ASKED FOR WHILE THE LAST ONE WAS STILL BEING DRAWN. Picked up here rather than
		// in the button, so pressing plus four times quickly lays the score out once at the end
		// instead of four times on the way.
		if (!working.load() && ready.load()
				&& (wantUnits != unitsNow || wantPart != partNow
					|| (song && wantStaves != stavesNow))) {
			beginWork(false);
		}
		if (usable() && page > score.pageCount())
			page = std::max(1, score.pageCount());
		escapeStep();
	}

	// ---- the furniture ---------------------------------------------------------------------------

	void placeBeside(math::Rect module) {
		const math::Vec scene = APP->scene->box.size;
		math::Vec at(module.pos.x + module.size.x, module.pos.y);
		if (at.x + box.size.x > scene.x)
			at.x = module.pos.x - box.size.x;
		at.x = math::clamp(at.x, 0.f, std::fmax(0.f, scene.x - box.size.x));
		at.y = math::clamp(at.y, 0.f, std::fmax(0.f, scene.y - box.size.y));
		box.pos = at;
	}

	math::Rect closeBox() {
		return math::Rect(math::Vec(box.size.x - SW_TITLE, 0.f),
			math::Vec(SW_TITLE, SW_TITLE));
	}

	math::Rect closeBoxLeft() {
		return math::Rect(math::Vec(0.f, 0.f), math::Vec(SW_TITLE, SW_TITLE));
	}

	math::Rect gripBox() {
		return math::Rect(math::Vec(box.size.x - SW_GRIP, box.size.y - SW_GRIP),
			math::Vec(SW_GRIP, SW_GRIP));
	}

	/** The controls in the title strip, right to left in front of the close cross: smaller,
	larger, previous page, next page, and the instrument. Square, the height of the strip. */
	math::Rect toolBox(int which) {
		const float w = SW_TITLE;
		return math::Rect(math::Vec(box.size.x - SW_TITLE - w * (which + 1) - 4.f, 0.f),
			math::Vec(w, SW_TITLE));
	}

	/** The paper, inside the frame and under the title strip. */
	math::Rect body() {
		return math::Rect(math::Vec(0.f, SW_TITLE),
			math::Vec(box.size.x, std::fmax(10.f, box.size.y - SW_TITLE)));
	}

	int edgesAt(math::Vec pos) {
		int mask = 0;
		if (pos.x <= SW_EDGE_GRAB)
			mask |= 1;
		if (pos.x >= box.size.x - SW_EDGE_GRAB)
			mask |= 2;
		if (pos.y <= SW_EDGE_GRAB)
			mask |= 4;
		if (pos.y >= box.size.y - SW_EDGE_GRAB)
			mask |= 8;
		return mask;
	}

	/** Cached, the way the chart window caches it: asked for every frame the pointer is over the
	window, so a new cursor each time would be a new cursor every frame. */
	static void setCursor(int shape) {
		static int current = -1;
		if (shape == current || !APP || !APP->window || !APP->window->win)
			return;
		current = shape;
		static GLFWcursor* cursors[5] = {NULL};
		static const int SHAPES[5] = {
			GLFW_ARROW_CURSOR, GLFW_RESIZE_EW_CURSOR, GLFW_RESIZE_NS_CURSOR,
			GLFW_RESIZE_NWSE_CURSOR, GLFW_RESIZE_NESW_CURSOR,
		};
		for (int i = 0; i < 5; i++) {
			if (SHAPES[i] != shape)
				continue;
			if (!cursors[i])
				cursors[i] = glfwCreateStandardCursor(shape);
			glfwSetCursor(APP->window->win, cursors[i]);
			return;
		}
	}

	static int cursorFor(int mask) {
		if ((mask & 1 && mask & 4) || (mask & 2 && mask & 8))
			return GLFW_RESIZE_NWSE_CURSOR;
		if ((mask & 2 && mask & 4) || (mask & 1 && mask & 8))
			return GLFW_RESIZE_NESW_CURSOR;
		if (mask & 1 || mask & 2)
			return GLFW_RESIZE_EW_CURSOR;
		if (mask & 4 || mask & 8)
			return GLFW_RESIZE_NS_CURSOR;
		return GLFW_ARROW_CURSOR;
	}

	static void claim(const ButtonEvent& e, widget::Widget* by) {
		e.consume(by);
		e.stopPropagating();
	}

	void onHover(const HoverEvent& e) override {
		int mask = edgesAt(e.pos);
		if (!mask && gripBox().contains(e.pos))
			mask = 2 | 8;
		setCursor(cursorFor(mask));
		OpaqueWidget::onHover(e);
	}

	void onLeave(const LeaveEvent& e) override {
		setCursor(GLFW_ARROW_CURSOR);
		OpaqueWidget::onLeave(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (closeBox().contains(e.pos) || closeBoxLeft().contains(e.pos)) {
				requestDelete();
				claim(e, this);
				return;
			}
			const int mask = edgesAt(e.pos);
			if (mask != 0 || gripBox().contains(e.pos)) {
				drag = 2;
				edges = mask ? mask : (2 | 8);
				claim(e, this);
				return;
			}
			if (e.pos.y < SW_TITLE) {
				if (toolBox(0).contains(e.pos)) {
					stepSize(-1);
					claim(e, this);
					return;
				}
				if (toolBox(1).contains(e.pos)) {
					stepSize(1);
					claim(e, this);
					return;
				}
				if (toolBox(2).contains(e.pos)) {
					turnTo(page - 1);
					claim(e, this);
					return;
				}
				if (toolBox(3).contains(e.pos)) {
					turnTo(page + 1);
					claim(e, this);
					return;
				}
				if (toolBox(4).contains(e.pos)) {
					offerParts();
					claim(e, this);
					return;
				}
				drag = 1;
				claim(e, this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (drag == 1) {
			box.pos = box.pos.plus(e.mouseDelta);
			gScoreWhere = box.pos;
		}
		else if (drag == 2) {
			if (edges & 1) {
				const float want = std::fmax(SW_MIN_W, box.size.x - e.mouseDelta.x);
				box.pos.x += box.size.x - want;
				box.size.x = want;
			}
			if (edges & 2)
				box.size.x = std::fmax(SW_MIN_W, box.size.x + e.mouseDelta.x);
			if (edges & 4) {
				const float want = std::fmax(SW_MIN_H, box.size.y - e.mouseDelta.y);
				box.pos.y += box.size.y - want;
				box.size.y = want;
			}
			if (edges & 8)
				box.size.y = std::fmax(SW_MIN_H, box.size.y + e.mouseDelta.y);
		}
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		drag = 0;
		OpaqueWidget::onDragEnd(e);
	}

	/** WHETHER THE SCORE CAN BE ASKED ANYTHING AT ALL.

	The worker owns it outright while it is laying it out — Verovio is pulling the document apart
	and putting it back together — so asking it how many pages it has in the middle of that reads
	a half-built document. Every path that touches the score goes through this, and anything that
	cannot wait does nothing instead. */
	bool usable() const {
		return ready.load() && !working.load();
	}

	void turnTo(int to) {
		if (!usable())
			return;
		const int last = std::max(1, score.pageCount());
		page = math::clamp(to, 1, last);
		scroll = 0.f;
	}

	/** THE WHEEL WALKS THE WHOLE SCORE, not one page. A page taller than the window has to be
	scrolled, and reaching the bottom of one page and then having to find a button to get to the
	next is a pause in the middle of reading. Scrolling past the end turns the page. */
	void onHoverScroll(const HoverScrollEvent& e) override {
		// NOT WHILE IT IS BEING ENGRAVED. A long score takes over a second to lay out, which is a
		// second in which the wheel is there to be turned — and turning it asked the document how
		// many pages it had while Verovio was still deciding.
		if (!usable()) {
			e.consume(this);
			e.stopPropagating();
			return;
		}
		const float over = pageHeight() - body().size.y;
		scroll -= e.scrollDelta.y;
		if (scroll > std::fmax(0.f, over)) {
			if (page < score.pageCount()) {
				page++;
				scroll = 0.f;
			}
			else
				scroll = std::fmax(0.f, over);
		}
		else if (scroll < 0.f) {
			if (page > 1) {
				page--;
				scroll = std::fmax(0.f, pageHeight() - body().size.y);
			}
			else
				scroll = 0.f;
		}
		e.consume(this);
		e.stopPropagating();
	}

	/** Escape closes it, asked of the keyboard for the reason the chart window gives. */
	bool escapeWasDown = false;

	void escapeStep() {
		if (!APP->window || !APP->window->win)
			return;
		const bool down = glfwGetKey(APP->window->win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
		const bool wasDown = escapeWasDown;
		escapeWasDown = down;
		if (!down || wasDown)
			return;
		if (dynamic_cast<ui::TextField*>(APP->event->getSelectedWidget()))
			return;
		for (widget::Widget* child : APP->scene->children) {
			ui::MenuOverlay* overlay = dynamic_cast<ui::MenuOverlay*>(child);
			if (overlay && overlay->visible && !overlay->requestedDelete)
				return;
		}
		requestDelete();
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			e.consume(this);
			e.stopPropagating();
			requestDelete();
			return;
		}
		widget::OpaqueWidget::onHoverKey(e);
	}

	void onRemove(const RemoveEvent& e) override {
		setCursor(GLFW_ARROW_CURSOR);
		if (gScoreWindow == this)
			gScoreWindow = NULL;
		widget::OpaqueWidget::onRemove(e);
	}

	// ---- drawing -------------------------------------------------------------------------------

	/** HOW FAR THE VIEW HAS TO SCROLL, in pixels.

	The page's own height, or how far the music actually reached if that is further — which it is
	whenever a score has more parts than a page that size can hold. Scrolling to the page's height
	alone would leave the rest of the music below the bottom edge with no way to reach it.

	How far it reached is only known once the page has been drawn, so the first frame scrolls to
	the nominal height and every frame after that to the true one. */
	float pageHeight() {
		const float nominal = body().size.x * SW_PAGE_ASPECT;
		if (!usable())
			return nominal;
		const float unit = score.pageScale(body().size.x);
		const float drawn = score.drawnHeight() * unit;
		return std::fmax(nominal, drawn + body().size.y * 0.04f);
	}

	void drawPage(const DrawArgs& args) {
		const math::Rect area = body();
		// THE PAPER IS THE WHOLE WIDTH. A page narrower than the window with grey each side
		// would be tidier and would also mean the notation is smaller than it needs to be, which
		// is the one thing this window is for.
		nvgSave(args.vg);
		nvgScissor(args.vg, area.pos.x, area.pos.y, area.size.x, area.size.y);

		nvgBeginPath(args.vg);
		nvgRect(args.vg, area.pos.x, area.pos.y, area.size.x, area.size.y);
		nvgFillColor(args.vg, SW_PAPER);
		nvgFill(args.vg);

		// THE BACKEND IS TOLD WHAT THE PAPER IS, so it can take ink away as well as put it down —
		// a tablature line is broken behind its fret number by painting the number's box out in
		// this colour. See ScoreDraw::paper.
		if (px::ScoreDraw* back = const_cast<px::ScoreDraw*>(score.backend()))
			back->paper = SW_PAPER;
		const float unit = score.pageScale(area.size.x);
		nvgTranslate(args.vg, area.pos.x, area.pos.y - scroll);
		nvgScale(args.vg, unit, unit);
		score.draw(args.vg, page, unit);
		nvgRestore(args.vg);
	}

	void drawFrame(const DrawArgs& args, std::shared_ptr<window::Font> face) {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.f);
		nvgFillColor(args.vg, SW_BACK);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, SW_INK);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, SW_TITLE, 5.f);
		nvgRect(args.vg, 0, SW_TITLE - 6.f, box.size.x, 6.f);
		nvgFillColor(args.vg, SW_BAND);
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, SW_TITLE);
		nvgLineTo(args.vg, box.size.x, SW_TITLE);
		nvgStrokeColor(args.vg, SW_INK);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);

		// THE SONG'S NAME HERE, unlike the chart window — a score window that does not say which
		// score is in it leaves nothing on the screen that does, because engraved music does not
		// carry its own title when the header is off.
		nvgFontFaceId(args.vg, face->handle);
		nvgFontSize(args.vg, SW_TITLE * 0.8f);
		nvgFillColor(args.vg, SW_INK);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		std::string label = name.empty() ? std::string("Score") : name;
		// THE INSTRUMENT NEXT TO THE SONG, because a window showing one part of seventeen and
		// not saying which is a window you cannot trust.
		const std::string instrument = partName();
		if (!instrument.empty())
			label += "   " + instrument;
		if (usable() && score.pageCount() > 1) {
			char count[64];
			std::snprintf(count, sizeof(count), "   page %d of %d", page, score.pageCount());
			label += count;
		}
		crispText(args.vg, SW_TITLE + 6.f, SW_TITLE / 2.f, label.c_str(), NULL);

		drawCross(args.vg, closeBox());
		drawCross(args.vg, closeBoxLeft());
		drawTool(args.vg, toolBox(0), "\xe2\x88\x92", face);   // minus
		drawTool(args.vg, toolBox(1), "+", face);
		drawTool(args.vg, toolBox(2), "\xe2\x80\xb9", face);   // single angle quotes
		drawTool(args.vg, toolBox(3), "\xe2\x80\xba", face);
		// THE INSTRUMENT, offered only when there is more than one. A control that opens a list
		// of one thing is a control that does nothing.
		if (partCount() > 1 || song)
			drawTool(args.vg, toolBox(4), "\xe2\x96\xbe", face);   // a small downward triangle
	}

	static void drawCross(NVGcontext* vg, math::Rect r) {
		const float d = 6.f;
		const math::Vec c = r.getCenter();
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x - d, c.y - d);
		nvgLineTo(vg, c.x + d, c.y + d);
		nvgMoveTo(vg, c.x + d, c.y - d);
		nvgLineTo(vg, c.x - d, c.y + d);
		nvgStrokeColor(vg, SW_INK);
		nvgStrokeWidth(vg, 1.4f);
		nvgStroke(vg);
	}

	void drawTool(NVGcontext* vg, math::Rect r, const char* glyph,
			std::shared_ptr<window::Font> face) {
		nvgFontFaceId(vg, face->handle);
		nvgFontSize(vg, SW_TITLE * 0.8f);
		nvgFillColor(vg, SW_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		const math::Vec c = r.getCenter();
		crispText(vg, c.x, c.y, glyph, NULL);
	}

	void drawMessage(const DrawArgs& args, std::shared_ptr<window::Font> face,
			const std::string& text) {
		const math::Rect area = body();
		nvgFontFaceId(args.vg, face->handle);
		nvgFontSize(args.vg, 13.f);
		nvgFillColor(args.vg, SW_INK);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		crispText(args.vg, area.getCenter().x, area.getCenter().y, text.c_str(), NULL);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!face || face->handle < 0)
			return;

		drawFrame(args, face);
		if (working.load())
			drawMessage(args, face, "Engraving…");
		else if (!usable())
			drawMessage(args, face, trouble.empty() ? "No score loaded." : trouble);
		else
			drawPage(args);

		nvgStrokeColor(args.vg, SW_INK);
		nvgStrokeWidth(args.vg, 1.2f);
		const math::Rect r = gripBox();
		for (int i = 1; i <= 3; i++) {
			const float d = i * 3.5f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x + r.size.x - d, r.pos.y + r.size.y - 1.f);
			nvgLineTo(args.vg, r.pos.x + r.size.x - 1.f, r.pos.y + r.size.y - d);
			nvgStroke(args.vg);
		}
	}
};


} // namespace


void scoreWindowCloseFor(int64_t moduleId) {
	if (gScoreWindow && gScoreWindow->moduleId == moduleId)
		gScoreWindow->requestDelete();
}


/** The window, made or taken over, placed where it was last left or beside its module. */
static ScoreWindow* makeWindow(int64_t moduleId) {
	if (gScoreWindow) {
		gScoreWindow->requestDelete();
		gScoreWindow = NULL;
	}
	ScoreWindow* window = new ScoreWindow;
	window->moduleId = moduleId;
	if (gScoreWhere.x >= 0.f)
		window->box.pos = gScoreWhere;
	else if (app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId))
		window->placeBeside(math::Rect(mw->getAbsoluteOffset(math::Vec()),
			mw->box.size.mult(mw->getAbsoluteZoom())));
	gScoreWindow = window;
	APP->scene->addChild(window);
	return window;
}


void scoreWindowShowSong(const std::shared_ptr<const GpSong>& song, int track,
		const std::string& title, int64_t moduleId) {
	if (!song)
		return;
	// THE SAME SONG AGAIN CLOSES IT, the way the chart button does.
	if (gScoreWindow && gScoreWindow->moduleId == moduleId && gScoreWindow->song == song) {
		gScoreWindow->requestDelete();
		return;
	}
	ScoreWindow* window = makeWindow(moduleId);
	window->song = song;
	window->name = title;
	window->wantPart = math::clamp(track, 0, (int) std::max<size_t>(1, song->tracks.size()) - 1);
	window->partNow = -1;
	window->beginWork(true);
}


void scoreWindowShow(const std::string& path, const std::string& title, int64_t moduleId) {
	if (gScoreWindow) {
		// THE SAME SCORE AGAIN CLOSES IT, the way the chart button does; a different one takes
		// the window over rather than opening a second.
		if (gScoreWindow->moduleId == moduleId && gScoreWindow->path == path) {
			gScoreWindow->requestDelete();
			return;
		}
		gScoreWindow->requestDelete();
		gScoreWindow = NULL;
	}

	ScoreWindow* window = new ScoreWindow;
	window->moduleId = moduleId;
	window->path = path;
	window->name = title;
	if (gScoreWhere.x >= 0.f)
		window->box.pos = gScoreWhere;
	else if (app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId))
		window->placeBeside(math::Rect(mw->getAbsoluteOffset(math::Vec()),
			mw->box.size.mult(mw->getAbsoluteZoom())));
	gScoreWindow = window;
	APP->scene->addChild(window);
	window->beginWork(true);
}


} // namespace px
