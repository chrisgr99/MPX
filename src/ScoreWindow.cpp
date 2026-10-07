/** See ScoreWindow.hpp. */
// Verovio is reached through Score, which keeps its headers to itself — but Score.hpp forward
// declares vrv::Toolkit only, so nothing here has to be included in a particular order.
#include "ScoreWindow.hpp"
#include "ScoreFollow.hpp"

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

	/** THE DOCUMENT THE MODULE LOADED, where the file it loaded was MusicXML. Shown in preference
	to the song, because it is not the same score: a piano is one part of two staves in the file
	and two tracks of one staff in the model, so a window built from the model shows one hand and
	loses the pedalling, which is written under the other one. The module holds these bytes
	already — a patch carries the score inside it — so this costs nothing to carry. */
	std::shared_ptr<const std::string> document;

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
	/** THE PAGE IS THE SHAPE OF THE WINDOW, which is what makes one page one windowful.

	A page of a fixed shape is a page that does not fit: the window would scroll down it and then
	turn to the next one, so the reader is working two ways of moving through the same score at
	once, and the breaks are in the engraver's places rather than anywhere to do with what can be
	seen. Given the window's own shape to lay out into, Verovio fills each page with as many
	systems as fit and never splits one across the bottom edge, so turning the page is the only
	movement there is and every page is full. */
	float wantAspect = 1.f, aspectNow = 0.f;
	/** Which instrument is being shown, and which one the worker last fetched. */
	int wantPart = 0;
	int partNow = 0;
	/** Which staves the music is written on, and which the worker last wrote. See GpStaves. */
	GpStaves wantStaves = GP_BOTH;
	GpStaves stavesNow = GP_BOTH;

	/** THE BARS BEING LOOPED, as the file numbers them, counting from nought; minus one for none.
	Held here as well as published so the window can draw what it asked for without reading back
	something a module may not have taken yet. */
	int loopFrom = -1, loopTo = -1;
	/** The bar last clicked, which a shift-click makes a loop back to. */
	int anchor = -1;
	/** WHETHER THE PAGE FOLLOWS THE MUSIC. On until the reader turns a page by hand, which is a
	reader who wants to look at something, and back on when the music is sent somewhere by hand. */
	bool follow = true;
	bool wasPlaying = false;
	/** Set when the module hands over a new document: the worker may be busy with the old one,
	and a request dropped because something was running is how the window came to be showing a
	score that had been replaced. */
	bool wantReload = false;
	/** Which tool the pointer is over, or -1. For the tip: these are glyphs, and a glyph is only
	obvious to whoever chose it. */
	int hoverTool = -1;

	/** HOW FAR DOWN THE WHOLE SCORE THE VIEW IS, in page units from the top of the first system —
	not pixels, which change with the width of the window, and not a page, of which there are none
	to the reader. `want` is where it is going and `flow` where it has got to: a view that jumped
	to a new system left the eye with nothing to follow, so it glides. */
	float flow = 0.f, flowWant = 0.f;
	/** Kept only because the page buttons and the engraving talk in pages; the reader never sees
	one, since a page here holds a single system. */

	/** 0 nothing, 1 moving, 2 resizing. */
	int drag = 0;
	int edges = 0;

	ScoreWindow() {
		box.size = math::Vec(760.f, 620.f);
		box.pos = math::Vec(140.f, 70.f);
	}

	~ScoreWindow() override {
		// THE LOOP GOES WITH THE WINDOW THAT ASKED FOR IT. The module would otherwise be left
		// going round a bar with nothing on screen admitting to it and nothing to click to stop
		// it, which is a state a patch cannot be got out of.
		if (loopFrom >= 0)
			px::scoreFollowSetLoop(moduleId, -1, -1, false);
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
		const float aspect = wantAspect;
		const int part = wantPart;
		const GpStaves staves = wantStaves;
		const std::string file = path;
		const std::shared_ptr<const GpSong> carried = song;
		const std::shared_ptr<const std::string> doc = document;
		worker = std::thread([this, load, units, aspect, part, staves, file, carried, doc]() {
			std::string reason;
			bool ok = true;
			// ONE SYSTEM TO A PAGE, which Verovio is told outright: see Score::applyOptions.
			// The page is then made generously tall rather than deliberately short, so that a
			// score whose systems are tall has room for them instead of overflowing.
			score.setPage(units, (int) (units * 2.f));
			(void) aspect;
			if (doc) {
				// THE WRITER'S OWN DOCUMENT, cut to one part as a file opened by name is.
				if (load)
					ok = score.loadBytes(*doc, name, &reason);
				else if (part != score.part())
					ok = score.setPart(part, &reason);
				partNow = score.part();
			}
			else if (carried) {
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
			if (ok)
				score.measureSystems();
			unitsNow = units;
			aspectNow = aspect;
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
		flow = flowWant = 0.f;
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
		ui::Menu* menu = createMenu();

		// TABLATURE NEEDS STRINGS TO PUT THE FINGERS ON. A fugue or a quartet has none, so the
		// choice is not offered rather than offered and refused — the converter would fall back
		// to notation anyway, which is a menu that lies about what it does.
		const bool fretted = song && wantPart >= 0
			&& wantPart < (int) song->tracks.size()
			&& !song->tracks[(size_t) wantPart].tuning.empty()
			&& !song->tracks[(size_t) wantPart].percussion;

		if (fretted) {
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
			if (fretted)
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
		flow = flowWant = 0.f;
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
		flow = flowWant = 0.f;
		beginWork(false);
	}

	void step() override {
		widget::OpaqueWidget::step();
		// A SIZE ASKED FOR WHILE THE LAST ONE WAS STILL BEING DRAWN. Picked up here rather than
		// in the button, so pressing plus four times quickly lays the score out once at the end
		// instead of four times on the way.
		// THE WINDOW'S SHAPE NO LONGER DECIDES THE PAGE'S. A page holds one system, as tall as
		// it needs to be, and the width is fitted to the music rather than to the window — so
		// resizing the window scales what is drawn and does not lay the score out again. It used
		// to, and that relayout was where the fitted width was lost.
		if (wantReload && !working.load()) {
			wantReload = false;
			unitsNow = 0;
			beginWork(true);
		}
		if (!working.load() && ready.load()
				&& (wantUnits != unitsNow || wantPart != partNow
					|| (song && wantStaves != stavesNow))) {
			beginWork(false);
		}
		// STARTING TO PLAY PUTS THE VIEW BACK ON THE MUSIC. Scrolling by hand stops the view
		// chasing it, which is right for a reader looking ahead at something — but then pressing
		// play is them asking to hear the piece, and a view still parked where they were reading
		// would leave the music running along out of sight.
		{
			int at = -1;
			float through = 0.f;
			bool going = false;
			if (px::scoreFollowRead(moduleId, at, through, going)) {
				if (going && !wasPlaying)
					follow = true;
				wasPlaying = going;
			}
		}
		// THE VIEW FOLLOWS THE MUSIC.
		if (follow && usable() && !working.load()) {
			int at = -1;
			float through = 0.f;
			bool going = false;
			if (px::scoreFollowRead(moduleId, at, through, going) && going && at >= 0)
				bringIntoView(at);
		}
		// AND GETS THERE SMOOTHLY. A view that jumped to a new system left the eye with nothing
		// to follow — which is the whole complaint against pages — so it glides, fast enough to
		// keep up with the music and slow enough to be followed. A move of less than a tenth of a
		// unit is finished rather than approached for ever.
		if (std::fabs(flowWant - flow) > 0.1f)
			flow += (flowWant - flow) * 0.18f;
		else
			flow = flowWant;
		// THE VIEW CANNOT BE PAST THE END, which a score laid out again at a new size may leave
		// it: fewer systems, or shorter ones, and what was the middle is now past the bottom.
		if (usable() && !working.load()) {
			const float most = deepest();
			if (flowWant > most)
				flowWant = most;
			if (flow > most)
				flow = most;
		}
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
		hoverTool = -1;
		if (e.pos.y < SW_TITLE) {
			for (int i = 0; i <= 6; i++) {
				if (toolBox(i).contains(e.pos)) {
					hoverTool = i;
					break;
				}
			}
		}
		int mask = edgesAt(e.pos);
		if (!mask && gripBox().contains(e.pos))
			mask = 2 | 8;
		setCursor(cursorFor(mask));
		OpaqueWidget::onHover(e);
	}

	void onLeave(const LeaveEvent& e) override {
		hoverTool = -1;
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
					stepView(-1);
					claim(e, this);
					return;
				}
				if (toolBox(3).contains(e.pos)) {
					stepView(1);
					claim(e, this);
					return;
				}
				if (toolBox(4).contains(e.pos)) {
					followAgain(!follow);
					claim(e, this);
					return;
				}
				if ((partCount() > 1 || song) && toolBox(5).contains(e.pos)) {
					offerParts();
					claim(e, this);
					return;
				}
				if (loopFrom >= 0 && toolBox(6).contains(e.pos)) {
					clearLoop();
					claim(e, this);
					return;
				}
				drag = 1;
				claim(e, this);
				return;
			}
			// ON THE MUSIC ITSELF: the bar under the pointer is the one to go round. Clicking
			// one loops it; holding shift and clicking another takes the loop out to there;
			// clicking inside the loop again lets it go. See ScoreFollow.hpp.
			// ON THE MUSIC ITSELF.
			//
			// A PLAIN CLICK GOES TO THAT BAR AND LOOPS NOTHING. Clicking a bar is how a passage
			// is found, and it is what every program that shows music does: it moves the music
			// there and lets go of whatever was repeating. Looping used to be the same gesture,
			// which meant a reader who wanted to hear bar forty got bar forty over and over.
			//
			// HOLDING SHIFT MAKES A LOOP, from the bar last clicked to this one, which is how a
			// range is picked everywhere else.
			const int bar = barAt(e.pos);
			if (bar >= 0) {
				if ((e.mods & GLFW_MOD_SHIFT) && anchor >= 0) {
					loopFrom = std::min(anchor, bar);
					loopTo = std::max(anchor, bar);
					px::scoreFollowSetLoop(moduleId, loopFrom, loopTo, true);
				}
				else {
					anchor = bar;
					clearLoop();
					px::scoreFollowGoTo(moduleId, bar);
				}
				follow = true;
				claim(e, this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void clearLoop() {
		loopFrom = loopTo = -1;
		px::scoreFollowSetLoop(moduleId, -1, -1, false);
	}

	/** WHICH BAR IS UNDER A POINT IN THE WINDOW, or minus one for none. The boxes are in the
	page's own units, so the point is put into those before it is asked about — the same
	transform the drawing uses, undone. */
	int barAt(math::Vec pos) const {
		if (!usable())
			return -1;
		const math::Rect area = const_cast<ScoreWindow*>(this)->body();
		if (!area.contains(pos))
			return -1;
		const float unit = score.pageScale(area.size.x);
		if (unit <= 0.f)
			return -1;
		const float x = (pos.x - area.pos.x) / unit;
		const float y = (pos.y - area.pos.y) / unit + flow;
		const std::vector<px::ScoreSystem>& sys = score.systems();
		for (size_t i = 0; i < sys.size(); i++) {
			if (y < sys[i].top || y > sys[i].top + sys[i].height)
				continue;
			for (size_t b = 0; b < sys[i].bars.size(); b++) {
				const px::ScoreBox& box = sys[i].bars[b];
				if (box.number.empty())
					continue;
				if (x >= box.x1 && x <= box.x2
						&& y >= box.y1 + sys[i].top && y <= box.y2 + sys[i].top)
					return std::atoi(box.number.c_str());
			}
		}
		return -1;
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

	/** Follows the music again, and goes to it at once rather than waiting for the next system:
	somebody turning this on is asking to see where the music is now. */
	void followAgain(bool on) {
		follow = on;
		if (!on)
			return;
		int at = -1;
		float through = 0.f;
		bool going = false;
		if (px::scoreFollowRead(moduleId, at, through, going) && at >= 0)
			bringIntoView(at);
	}

	/** BRINGS A BAR INTO VIEW, by putting the system it is on near the top of the window.

	NOTHING MOVES WHILE THE BAR IS WHOLLY VISIBLE — a view that scrolled on every bar would never
	be still, and still is what a page of small print has to be to be read. When it is not, the
	bar's own system goes to the top, which is where the most music after it is. */
	void bringIntoView(int bar) {
		px::ScoreBox box;
		if (!boxOf(bar, box))
			return;
		const math::Rect area = body();
		const float unit = score.pageScale(area.size.x);
		if (unit <= 0.f)
			return;
		// Where the bar would sit in the window once the view has finished moving.
		const float top = (box.y1 - flowWant) * unit, bottom = (box.y2 - flowWant) * unit;
		if (top >= 0.f && bottom <= area.size.y)
			return;
		const float margin = std::fmin(24.f, area.size.y * 0.04f) / unit;
		flowWant = math::clamp(box.y1 - margin, 0.f, deepest());
	}

	/** How far down the flow the view can go: far enough to bring the last system to the bottom
	of the window and no further, so the score does not scroll off into nothing. */
	float deepest() {
		const math::Rect area = body();
		const float unit = score.pageScale(area.size.x);
		if (unit <= 0.f)
			return 0.f;
		return std::fmax(0.f, score.flowHeight() - area.size.y / unit);
	}

	/** A WINDOWFUL AT A TIME, which is what the two arrows do now that there are no pages: the
	reader wanting the next screen of music, with a system's worth kept in common so the eye has
	something it has already read to land on. */
	void stepView(int way) {
		follow = false;
		const math::Rect area = body();
		const float unit = score.pageScale(area.size.x);
		if (unit <= 0.f)
			return;
		const float screen = std::fmax(1.f, area.size.y / unit);
		flowWant = math::clamp(flowWant + way * screen * 0.85f, 0.f, deepest());
	}


	/** THE WHEEL WALKS THE WHOLE SCORE, not one page. A page taller than the window has to be
	scrolled, and reaching the bottom of one page and then having to find a button to get to the
	next is a pause in the middle of reading. Scrolling past the end turns the page. */
	void onHoverScroll(const HoverScrollEvent& e) override {
		// NOT WHILE IT IS BEING ENGRAVED. A long score takes over a second to lay out, which is a
		// second in which the wheel is there to be turned.
		if (!usable()) {
			e.consume(this);
			e.stopPropagating();
			return;
		}
		follow = false;
		const float unit = score.pageScale(body().size.x);
		// THE WHEEL MOVES THE SCORE, not a page of it: a notch is about a line of music at any
		// size, since the units are the score's and the pixels are the window's.
		if (unit > 0.f)
			flowWant = math::clamp(flowWant - e.scrollDelta.y / unit, 0.f, deepest());
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


	/** THE BOX OF ONE BAR ON THE PAGE AS DRAWN, or nothing where that bar is on another page.
	The boxes come from the drawing, so this is only true of the page last drawn. */
	/** WHERE A BAR IS IN THE WHOLE FLOW: its box with the system's own offset already in it, so
	everything above works in one set of coordinates. False where there is no such bar. */
	bool boxOf(int bar, px::ScoreBox& out) const {
		if (bar < 0)
			return false;
		// THE NUMBER THE PAGE PRINTS, which is what the module sends: a pickup is bar nought and
		// the first whole bar is bar one. See GuitarChart's writtenNumber.
		const std::string want = std::to_string(bar);
		const std::vector<px::ScoreSystem>& sys = score.systems();
		for (size_t i = 0; i < sys.size(); i++) {
			for (size_t b = 0; b < sys[i].bars.size(); b++) {
				if (sys[i].bars[b].number != want)
					continue;
				out = sys[i].bars[b];
				out.y1 += sys[i].top;
				out.y2 += sys[i].top;
				return true;
			}
		}
		return false;
	}


	/** PUTS WHATEVER IS DRAWN NEXT ON A WHOLE PIXEL OF THE SCREEN.

	A staff line is thinner than a pixel, so where it falls between two of them decides how it is
	shared across both. While the music moves, a line seated at a fraction that changes every frame
	is shared differently every frame, and that is seen as the lines shimmering — which on a page
	of small print makes the score unreadable while it scrolls.

	ROUNDING IN THE WINDOW'S OWN COORDINATES IS NOT ENOUGH, which is the thing that took a second
	go. Rack draws the whole rack through a zoom, so a whole number here is a fraction on the
	screen at any zoom that is not exactly one — and at that point the rounding has done nothing
	at all. So the transform in force is asked where it is actually putting things, and the nudge
	is worked out there and applied back through the scale. */
	static void snapToPixels(NVGcontext* vg) {
		float xf[6] = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
		nvgCurrentTransform(vg, xf);
		// [4] and [5] are where the origin lands on the screen; [0] and [3] are the scale it got
		// there by. A scale of nought would be a transform with nothing to draw into.
		if (std::fabs(xf[0]) < 1e-6f || std::fabs(xf[3]) < 1e-6f)
			return;
		const float dx = (std::round(xf[4]) - xf[4]) / xf[0];
		const float dy = (std::round(xf[5]) - xf[5]) / xf[3];
		nvgTranslate(vg, dx, dy);
	}

	void drawPage(const DrawArgs& args, std::shared_ptr<window::Font> face) {
		const math::Rect area = body();
		nvgSave(args.vg);
		nvgScissor(args.vg, area.pos.x, area.pos.y, area.size.x, area.size.y);

		nvgBeginPath(args.vg);
		nvgRect(args.vg, area.pos.x, area.pos.y, area.size.x, area.size.y);
		nvgFillColor(args.vg, SW_PAPER);
		nvgFill(args.vg);

		if (px::ScoreDraw* back = const_cast<px::ScoreDraw*>(score.backend()))
			back->paper = SW_PAPER;
		const float unit = score.pageScale(area.size.x);

		// THE SYSTEMS THAT CAN BE SEEN, AND NO OTHERS. A continuous view of a long score is
		// hundreds of systems and drawing them all would cost half a second a frame; drawing the
		// two or three in the window costs what one page used to.
		const std::vector<px::ScoreSystem>& sys = score.systems();
		for (size_t i = 0; i < sys.size(); i++) {
			const float top = (sys[i].top - flow) * unit;
			const float bottom = top + sys[i].height * unit;
			if (bottom < 0.f || top > area.size.y)
				continue;
			nvgSave(args.vg);
			// ON WHOLE PIXELS. A staff line is drawn half a pixel wide, so where it falls between
			// two of them decides how it is shared across both — and a system seated at a
			// fraction that changes every frame shares them differently every frame, which is
			// seen as the lines shimmering while the music moves. Scrolling by whole pixels keeps
			// every line at the same fraction it had when it was still, so they hold steady. The
			// score still moves smoothly: a pixel at this size is a fiftieth of a staff space.
			//
			// The page's own top is taken off here as well: see ScoreSystem::shift.
			nvgTranslate(args.vg, area.pos.x, area.pos.y + top - sys[i].shift * unit);
			snapToPixels(args.vg);
			nvgScale(args.vg, unit, unit);
			score.draw(args.vg, (int) i + 1, unit);
			nvgRestore(args.vg);
		}

		// WHAT IS BEING LOOPED AND WHERE THE MUSIC IS, over the music and in the window's own
		// pixels, since the systems each have a transform of their own and these do not.
		nvgTranslate(args.vg, area.pos.x, area.pos.y);
		snapToPixels(args.vg);
		if (loopFrom >= 0 && loopTo >= loopFrom) {
			for (int b = loopFrom; b <= loopTo; b++) {
				px::ScoreBox box;
				if (!boxOf(b, box))
					continue;
				// SEEN ON DARK PAPER. A wash at a tenth of full strength is how this is done on
				// white and is all but invisible on a page this dark, which left a reader looping
				// a bar with nothing to show for it. A stronger wash and an outline around it:
				// the wash says which bars and the outline says so again at a glance.
				nvgBeginPath(args.vg);
				nvgRect(args.vg, box.x1 * unit, (box.y1 - flow) * unit,
					(box.x2 - box.x1) * unit, (box.y2 - box.y1) * unit);
				nvgFillColor(args.vg, nvgRGBA(255, 190, 60, 46));
				nvgFill(args.vg);
				nvgStrokeColor(args.vg, nvgRGBA(255, 190, 60, 235));
				nvgStrokeWidth(args.vg, 2.f);
				nvgStroke(args.vg);
			}
		}
		int at = -1;
		float through = 0.f;
		bool going = false;
		px::ScoreBox box;
		if (px::scoreFollowRead(moduleId, at, through, going) && at >= 0 && boxOf(at, box)) {
			// A BAR LIT AND A LINE IN IT. The bar says where to look from across the room and
			// the line says where in it, which the bar alone cannot.
			nvgBeginPath(args.vg);
			nvgRect(args.vg, box.x1 * unit, (box.y1 - flow) * unit,
				(box.x2 - box.x1) * unit, (box.y2 - box.y1) * unit);
			nvgFillColor(args.vg, nvgRGBA(120, 200, 255, going ? 58 : 34));
			nvgFill(args.vg);
			const float x = (box.x1 + (box.x2 - box.x1) * math::clamp(through, 0.f, 1.f)) * unit;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x, (box.y1 - flow) * unit);
			nvgLineTo(args.vg, x, (box.y2 - flow) * unit);
			nvgStrokeColor(args.vg, nvgRGBA(120, 200, 255, going ? 220 : 120));
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}
		// THE BAR NUMBERS, in the window's own pixels so they are the same size whatever the
		// notation is scaled to, and in amber so they read as ours rather than as something the
		// composer wrote. Every bar carries its number: a score is talked about in bar numbers,
		// and counting from the last one printed is what a reader should not have to do.
		nvgSave(args.vg);
		nvgTranslate(args.vg, area.pos.x, area.pos.y);
		snapToPixels(args.vg);
		nvgFontFaceId(args.vg, face->handle);
		nvgFontSize(args.vg, 13.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		for (size_t i = 0; i < sys.size(); i++) {
			const float top = (sys[i].top - flow) * unit;
			if (top + sys[i].height * unit < 0.f || top > area.size.y)
				continue;
			for (size_t b = 0; b < sys[i].bars.size(); b++) {
				const px::ScoreBox& box = sys[i].bars[b];
				if (box.number.empty())
					continue;
				// ABOVE THE BAR AND A LITTLE INTO IT, which is where an engraver puts one.
				const float x = box.x1 * unit + 2.f;
				const float y = (box.y1 + sys[i].top - flow) * unit - 1.f;
				if (y < 8.f || y > area.size.y)
					continue;
				// ON A BACKING OF ITS OWN. A number dropped onto an engraving lands among
				// slurs, beams and dynamics, and amber alone was not enough to pick it out of
				// them; a patch of the paper behind it is.
				float at[4] = {0.f, 0.f, 0.f, 0.f};
				nvgTextBounds(args.vg, x, y, box.number.c_str(), NULL, at);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, at[0] - 3.f, at[1] - 1.f,
					at[2] - at[0] + 6.f, at[3] - at[1] + 2.f, 2.f);
				nvgFillColor(args.vg, nvgRGBA(28, 30, 34, 225));
				nvgFill(args.vg);
				nvgFillColor(args.vg, nvgRGBA(255, 176, 64, 255));
				crispText(args.vg, x, y, box.number.c_str(), NULL);
			}
		}
		nvgRestore(args.vg);

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
		// WHICH BAR, NOT WHICH PAGE. There are no pages to the reader any more, and a bar number
		// is what a score is talked about in and what a passage being worked on is found by.
		if (usable()) {
			int first = -1, last = -1;
			const std::vector<px::ScoreSystem>& sys = score.systems();
			for (size_t i = 0; i < sys.size(); i++) {
				for (size_t b = 0; b < sys[i].bars.size(); b++) {
					if (sys[i].bars[b].number.empty())
						continue;
					const int n = std::atoi(sys[i].bars[b].number.c_str());
					if (last < 0 || n > last)
						last = n;
					if (first < 0 && sys[i].top + sys[i].height >= flow)
						first = n;
				}
			}
			if (first > 0 && last > 0) {
				char count[64];
				std::snprintf(count, sizeof(count), "   bar %d of %d", first, last);
				label += count;
			}
			if (loopFrom >= 0) {
				char loop[64];
				if (loopTo > loopFrom)
					std::snprintf(loop, sizeof(loop), "   looping bars %d to %d",
						loopFrom, loopTo);
				else
					std::snprintf(loop, sizeof(loop), "   looping bar %d", loopFrom);
				label += loop;
			}
		}
		crispText(args.vg, SW_TITLE + 6.f, SW_TITLE / 2.f, label.c_str(), NULL);

		drawCross(args.vg, closeBox());
		drawCross(args.vg, closeBoxLeft());
		drawTip(args.vg, face);
		drawTool(args.vg, toolBox(0), "\xe2\x88\x92", face);   // minus
		drawTool(args.vg, toolBox(1), "+", face);
		drawTool(args.vg, toolBox(2), "\xe2\x80\xb9", face);   // single angle quotes
		drawTool(args.vg, toolBox(3), "\xe2\x80\xba", face);
		// FOLLOWING THE MUSIC OR NOT. Scrolling by hand stops the view chasing the playhead, so
		// that a passage can be read while the piece plays — and until this button there was no
		// way back short of clicking a bar, which also moves the music, so somebody who only
		// wanted to go on watching had to interrupt what they were listening to.
		drawTool(args.vg, toolBox(4), "\xe2\x97\x89", face, follow ? 1 : 0);   // a ringed dot
		// THE INSTRUMENT, offered only when there is more than one. A control that opens a list
		// of one thing is a control that does nothing.
		if (partCount() > 1 || song)
			drawTool(args.vg, toolBox(5), "\xe2\x96\xbe", face);   // a small downward triangle
		// A WAY OUT OF THE LOOP THAT CAN BE SEEN. Clicking the looped bar again clears it, which
		// is quick once you know and invisible until somebody tells you — and a reader who has
		// looped a bar by accident is stuck with the same bar repeating and nothing on the window
		// admitting to it. So the loop says it is there and offers to stop.
		if (loopFrom >= 0)
			drawTool(args.vg, toolBox(6), "\xe2\x9c\x95", face);   // a cross
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

	/** WHAT THE TOOL UNDER THE POINTER DOES, in a word. The tools are glyphs, and a glyph only
	says what it means to whoever chose it. Kept to a word or two: this is a tip, not a sentence. */
	void drawTip(NVGcontext* vg, std::shared_ptr<window::Font> face) {
		if (hoverTool < 0)
			return;
		static const char* TIPS[] = {"Smaller", "Larger", "Back", "Forward", "Follow",
			"Instrument", "Loop off"};
		if (hoverTool >= (int) (sizeof(TIPS) / sizeof(TIPS[0])))
			return;
		// NOTHING ABOUT A TOOL THAT IS NOT THERE. Two of them come and go.
		if (hoverTool == 5 && !(partCount() > 1 || song))
			return;
		if (hoverTool == 6 && loopFrom < 0)
			return;
		const char* tip = TIPS[hoverTool];
		nvgFontFaceId(vg, face->handle);
		nvgFontSize(vg, 12.f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		float bounds[4] = {0.f, 0.f, 0.f, 0.f};
		nvgTextBounds(vg, 0.f, 0.f, tip, NULL, bounds);
		const float w = bounds[2] - bounds[0] + 12.f, h = 18.f;
		const math::Rect r = toolBox(hoverTool);
		// UNDER THE BUTTON, and never off the edge of the window.
		float x = math::clamp(r.getCenter().x - w / 2.f, 2.f, std::fmax(2.f, box.size.x - w - 2.f));
		const float y = SW_TITLE + 3.f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, w, h, 3.f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 225));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		nvgFillColor(vg, SW_INK);
		crispText(vg, x + w / 2.f, y + h / 2.f, tip, NULL);
	}

	void drawTool(NVGcontext* vg, math::Rect r, const char* glyph,
			std::shared_ptr<window::Font> face, int lit = -1) {
		// A TOOL THAT IS A SWITCH SHOWS WHICH WAY IT IS SET. Lit, it is doing something now;
		// dark, it is there to be turned on. -1 is a tool that is not a switch at all.
		if (lit >= 0) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x + 3.f, r.pos.y + 4.f, r.size.x - 6.f, r.size.y - 8.f, 3.f);
			nvgFillColor(vg, lit ? nvgRGBA(120, 200, 255, 60) : nvgRGBA(255, 255, 255, 16));
			nvgFill(vg);
		}
		nvgFontFaceId(vg, face->handle);
		nvgFontSize(vg, SW_TITLE * 0.8f);
		nvgFillColor(vg, (lit == 0) ? nvgRGBA(255, 255, 255, 120) : SW_INK);
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
			APP->window->loadFont(asset::plugin(pluginInstance, "res/Hack-Regular.ttf"));
		if (!face || face->handle < 0)
			return;

		drawFrame(args, face);
		if (working.load())
			drawMessage(args, face, "Engraving…");
		else if (!usable())
			drawMessage(args, face, trouble.empty() ? "No score loaded." : trouble);
		else
			drawPage(args, face);

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


void scoreWindowReplaceFor(int64_t moduleId, const std::shared_ptr<const std::string>& musicxml,
		const std::string& title) {
	if (!gScoreWindow || gScoreWindow->moduleId != moduleId || !musicxml || musicxml->empty())
		return;
	if (gScoreWindow->document == musicxml)
		return;
	gScoreWindow->document = musicxml;
	gScoreWindow->song.reset();
	gScoreWindow->path.clear();
	gScoreWindow->name = title;
	gScoreWindow->wantPart = 0;
	gScoreWindow->partNow = -1;
	gScoreWindow->flow = gScoreWindow->flowWant = 0.f;
	gScoreWindow->loopFrom = gScoreWindow->loopTo = -1;
	gScoreWindow->wantReload = true;
}


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


void scoreWindowShowDocument(const std::shared_ptr<const std::string>& musicxml,
		const std::string& title, int64_t moduleId) {
	if (!musicxml || musicxml->empty())
		return;
	// THE SAME SCORE AGAIN CLOSES IT, as the song and the file both do.
	if (gScoreWindow && gScoreWindow->moduleId == moduleId && gScoreWindow->document == musicxml) {
		gScoreWindow->requestDelete();
		return;
	}
	ScoreWindow* window = makeWindow(moduleId);
	window->document = musicxml;
	window->name = title;
	window->wantPart = 0;
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
