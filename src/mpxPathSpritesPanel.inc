/** mpxPathSprites' panel. Included by mpxPathSprites.cpp, which is where the module is.

SEPARATE FROM THE MODULE because the picture, the drawing and the dragging are most of the file
and none of it is what the module does. The module runs heads along paths and puts out voltages;
this is how a path gets drawn and how a picture gets chosen.
*/

namespace px {


/** THE MEASUREMENTS. The picture is a square as tall as the panel, as it is in mpxSprites, and
the width is then chosen for the column beside it. */
static const float PS_PANEL_H = 128.5f;
static const float PS_PANEL_HP = 36.f;
static const float PS_PANEL_W = PS_PANEL_HP * 5.08f;
static const float PS_INSET = 1.5f;
static const float PS_SQUARE = PS_PANEL_H - 2.f * PS_INSET;
static const float PS_COLUMN = PS_PANEL_W - PS_SQUARE - 2.f * PS_INSET;


/** THE PICTURE, THE PATHS ON IT, AND THE POINTER.

WHAT A DRAG DOES depends on the path's kind, and the panel says which kind that is before you
make one — no modifier to remember and nothing to hold down.

	A CIRCLE is placed by its centre. Its size is the panel's business, since a circle has one
	number and a number belongs on a knob.

	A LINE is placed by its ends. Dragging near an end moves that end; dragging anywhere else
	moves the whole line without turning it, so a line that is pointed the way you want can be
	carried somewhere else and still point that way.

A sprite switched on arrives in the middle, so there is always something to take hold of. */
struct PictureField : widget::OpaqueWidget {
	PathSpritesModule* module = NULL;
	int handle = -1;
	uint32_t shownGeneration = 0;

	/** What a drag has hold of, and where the pointer was when it took hold. */
	enum Grab { GRAB_NONE, GRAB_CENTRE, GRAB_END_A, GRAB_END_B, GRAB_WHOLE };
	int grab = GRAB_NONE;
	int grabbed = -1;
	/** IN THE WIDGET'S OWN PIXELS, accumulated from the drag's movement.

	NOT FROM THE MOUSE'S POSITION. Rack gives the pointer in the RACK's coordinates, which are not
	this widget's — they carry the rack's own scroll and zoom — so subtracting the widget's
	absolute position from them mixes two frames of reference and the answer is wherever those two
	happen to differ. It sent every path to a corner. Every draggable thing in this plugin
	accumulates the movement instead, divided by the zoom. */
	math::Vec dragAt;

	/** Whether the chooser is up over the picture, and what the pointer is over in it. */
	bool choosing = false;
	int hoverCell = -1;
	std::vector<PictureMemory> cells;
	std::vector<int> thumbs;

	math::Vec toPicture(math::Vec pos) const {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return math::Vec(0.5f, 0.5f);
		return math::Vec(math::clamp(pos.x / box.size.x, 0.f, 1.f),
			math::clamp(pos.y / box.size.y, 0.f, 1.f));
	}
	math::Vec toScreen(float x, float y) const {
		return math::Vec(x * box.size.x, y * box.size.y);
	}

	int tabNow() const {
		return module ? math::clamp(module->tab.load(), 0, MAX_PATHS - 1) : 0;
	}

	// ---- choosing a picture --------------------------------------------------------------------

	static const int ACROSS = 5, DOWN = 4;

	math::Rect cellRect(int i) const {
		const float w = box.size.x / (float) ACROSS;
		const float h = (box.size.y - 18.f) / (float) DOWN;
		return math::Rect(math::Vec((i % ACROSS) * w, 18.f + (i / ACROSS) * h),
			math::Vec(w, h));
	}

	void openChooser() {
		cells = pictureHistory();
		thumbs.assign(cells.size(), -1);
		choosing = true;
		hoverCell = -1;
	}

	/** The file dialog, and the picture kept where the plugin can find it again. */
	void pickFile() {
		osdialog_filters* filters = osdialog_filters_parse(
			"Pictures:png,jpg,jpeg,bmp,gif,tga;Any:*");
		const std::string start = pictureFolder();
		char* picked = osdialog_file(OSDIALOG_OPEN, start.empty() ? NULL : start.c_str(),
			NULL, filters);
		osdialog_filters_free(filters);
		if (!picked)
			return;
		const std::string path = picked;
		std::free(picked);
		if (!module)
			return;
		if (!module->picture.load(path)) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "That picture could not be read.");
			return;
		}
		const std::string kept = pictureKeepCopy(module->picture.name, module->picture.file);
		pictureRemember(kept.empty() ? path : kept, module->picture.name, module->picture.file);
		choosing = false;
	}

	// ---- taking hold of a path -------------------------------------------------------------------

	/** HOW NEAR COUNTS, in screen pixels. Large enough to hit under magnification without having
	to be accurate, small enough that two paths near each other are still two things. */
	static float grabRadius() { return 8.f; }

	static float distance(math::Vec a, math::Vec b) {
		const float dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	/** How far a point is from a line segment. */
	static float toSegment(math::Vec p, math::Vec a, math::Vec b) {
		const float vx = b.x - a.x, vy = b.y - a.y;
		const float len = vx * vx + vy * vy;
		if (len < 1e-6f)
			return distance(p, a);
		float t = ((p.x - a.x) * vx + (p.y - a.y) * vy) / len;
		t = math::clamp(t, 0.f, 1.f);
		return distance(p, math::Vec(a.x + t * vx, a.y + t * vy));
	}

	/** WHAT IS UNDER THE POINTER on path `i`, or nothing.

	A PATH IS TAKEN HOLD OF BY ITS OWN OUTLINE, never by empty picture: clicking somewhere a path
	is not should do nothing at all, because the alternative is that every misjudged click moves
	something. A circle is grabbed by its perimeter or by the centre mark; a line by either end
	or by anywhere along it. */
	int hitTest(math::Vec pos, int i) const {
		if (!module)
			return GRAB_NONE;
		const Path& p = module->path[i];
		const float near = grabRadius();
		if (p.kind == Path::CIRCLE) {
			const math::Vec c = toScreen(p.cx, p.cy);
			if (distance(pos, c) < near)
				return GRAB_CENTRE;
			const float r = p.diameter * 0.5f * box.size.x;
			if (std::fabs(distance(pos, c) - r) < near)
				return GRAB_WHOLE;
			return GRAB_NONE;
		}
		const math::Vec a = toScreen(p.ax, p.ay);
		const math::Vec b = toScreen(p.bx, p.by);
		if (distance(pos, a) < near)
			return GRAB_END_A;
		if (distance(pos, b) < near)
			return GRAB_END_B;
		if (toSegment(pos, a, b) < near)
			return GRAB_WHOLE;
		return GRAB_NONE;
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS) {
			widget::OpaqueWidget::onButton(e);
			return;
		}
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			// THE PICTURE IS CHANGED FROM THE PICTURE, not from a menu: the thing you are looking
			// at is the thing you click.
			if (choosing)
				choosing = false;
			else
				openChooser();
			e.consume(this);
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			widget::OpaqueWidget::onButton(e);
			return;
		}

		if (choosing) {
			if (e.pos.y < 18.f) {
				pickFile();
				e.consume(this);
				return;
			}
			const int at = (int) (((int) ((e.pos.y - 18.f) / ((box.size.y - 18.f) / DOWN))) * ACROSS
				+ (int) (e.pos.x / (box.size.x / ACROSS)));
			if (at >= 0 && at < (int) cells.size()) {
				if (module->picture.load(cells[(size_t) at].path)) {
					pictureRemember(cells[(size_t) at].path, module->picture.name,
						module->picture.file);
				}
				choosing = false;
			}
			e.consume(this);
			return;
		}

		// WITH NO PICTURE THERE IS NOTHING TO DRAG, so a click asks for one. Right-click opens
		// the chooser at any time; this is so that the first click anybody makes on an empty
		// module does the only useful thing there is to do.
		if (!module->picture.has()) {
			openChooser();
			e.consume(this);
			return;
		}

		// THE TAB IN FRONT FIRST, so that two paths lying over each other are not a lottery; then
		// any other that is switched on, and taking hold of one brings its tab to the front.
		int found = GRAB_NONE, on = -1;
		const int front = tabNow();
		if (module->enabled(front))
			found = hitTest(e.pos, front);
		if (found != GRAB_NONE) {
			on = front;
		}
		else {
			for (int i = 0; i < MAX_PATHS && on < 0; i++) {
				if (i == front || !module->enabled(i))
					continue;
				const int g = hitTest(e.pos, i);
				if (g != GRAB_NONE) {
					found = g;
					on = i;
				}
			}
		}
		if (on < 0) {
			// NOTHING UNDER THE POINTER, so nothing moves.
			e.consume(this);
			return;
		}
		if (on != front) {
			module->tab.store(on);
			module->selection.store(1 << on);
		}

		grab = found;
		grabbed = on;
		dragAt = e.pos;
		e.consume(this);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (grab != GRAB_NONE && grabbed >= 0 && module) {
			const float zoom = getAbsoluteZoom();
			const math::Vec was = toPicture(dragAt);
			dragAt = dragAt.plus(e.mouseDelta.div(zoom > 0.f ? zoom : 1.f));
			const math::Vec now = toPicture(dragAt);
			const float mx = now.x - was.x, my = now.y - was.y;

			// EVERYTHING MOVES BY HOW FAR THE POINTER MOVED, never to where it is. Nothing jumps
			// when it is taken hold of: a circle grabbed by its rim keeps that part of its rim
			// under your hand, and an end grabbed slightly off centre stays slightly off centre.
			Path& p = module->path[grabbed];
			switch (grab) {
				case GRAB_CENTRE:
				case GRAB_WHOLE:
					if (p.kind == Path::CIRCLE) {
						p.cx = math::clamp(p.cx + mx, 0.f, 1.f);
						p.cy = math::clamp(p.cy + my, 0.f, 1.f);
					}
					else {
						// THE WHOLE LINE, KEEPING ITS BEARING: both ends by the same amount, so a
						// line pointed the way you want can be carried somewhere else and still
						// point that way.
						p.ax = math::clamp(p.ax + mx, 0.f, 1.f);
						p.ay = math::clamp(p.ay + my, 0.f, 1.f);
						p.bx = math::clamp(p.bx + mx, 0.f, 1.f);
						p.by = math::clamp(p.by + my, 0.f, 1.f);
					}
					break;
				case GRAB_END_A:
					p.ax = math::clamp(p.ax + mx, 0.f, 1.f);
					p.ay = math::clamp(p.ay + my, 0.f, 1.f);
					break;
				case GRAB_END_B:
					p.bx = math::clamp(p.bx + mx, 0.f, 1.f);
					p.by = math::clamp(p.by + my, 0.f, 1.f);
					break;
				default:
					break;
			}
			p.moved();
		}
		widget::OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		grab = GRAB_NONE;
		grabbed = -1;
		widget::OpaqueWidget::onDragEnd(e);
	}

	void onHover(const HoverEvent& e) override {
		if (choosing) {
			hoverCell = -1;
			if (e.pos.y >= 18.f) {
				const int at = (int) (((int) ((e.pos.y - 18.f) / ((box.size.y - 18.f) / DOWN)))
					* ACROSS + (int) (e.pos.x / (box.size.x / ACROSS)));
				if (at >= 0 && at < (int) cells.size())
					hoverCell = at;
			}
		}
		widget::OpaqueWidget::onHover(e);
	}

	// ---- drawing ---------------------------------------------------------------------------------

	void draw(const DrawArgs& args) override {
		const float w = box.size.x, h = box.size.y;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, w, h);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);

		if (module && module->picture.has()) {
			if (handle < 0 || shownGeneration != module->picture.generation) {
				if (handle >= 0)
					nvgDeleteImage(args.vg, handle);
				handle = nvgCreateImageRGBA(args.vg, PICTURE_SIDE, PICTURE_SIDE, 0,
					module->picture.rgba.data());
				shownGeneration = module->picture.generation;
			}
			if (handle >= 0) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0.f, 0.f, w, h);
				nvgFillPaint(args.vg, nvgImagePattern(args.vg, 0.f, 0.f, w, h, 0.f, handle, 1.f));
				nvgFill(args.vg);
			}
		}
		else {
			std::shared_ptr<window::Font> font = gridFont();
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 20.f);
				nvgFillColor(args.vg, nvgRGB(0xd2, 0xdb, 0xe4));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, w / 2.f, h / 2.f - 14.f, "click for a picture", NULL);
				nvgFontSize(args.vg, 13.f);
				nvgFillColor(args.vg, nvgRGB(0x8a, 0x94, 0xa2));
				nvgText(args.vg, w / 2.f, h / 2.f + 12.f, "right-click changes it later", NULL);
			}
		}

		if (!choosing)
			drawPaths(args);
		else
			drawChooser(args);

		widget::OpaqueWidget::draw(args);
	}

	void drawPaths(const DrawArgs& args) {
		if (!module)
			return;
		const int front = tabNow();
		for (int i = 0; i < MAX_PATHS; i++) {
			if (!module->enabled(i))
				continue;
			const Path& p = module->path[i];
			const NVGcolor ink = PATH_INK[i];
			const bool primary = (i == front);

			// THE PATH ITSELF, drawn twice: a dark thread under a bright one, so it is seen over
			// a light picture and over a dark one alike.
			for (int pass = 0; pass < 2; pass++) {
				nvgBeginPath(args.vg);
				if (p.kind == Path::CIRCLE) {
					const math::Vec c = toScreen(p.cx, p.cy);
					nvgEllipse(args.vg, c.x, c.y, p.diameter * 0.5f * box.size.x,
						p.diameter * 0.5f * box.size.y);
				}
				else {
					const math::Vec a = toScreen(p.ax, p.ay);
					const math::Vec b = toScreen(p.bx, p.by);
					nvgMoveTo(args.vg, a.x, a.y);
					nvgLineTo(args.vg, b.x, b.y);
				}
				nvgStrokeColor(args.vg, pass == 0 ? nvgRGBA(0, 0, 0, 0xaa) : ink);
				nvgStrokeWidth(args.vg, pass == 0 ? (primary ? 4.f : 3.f) : (primary ? 2.f : 1.2f));
				nvgStroke(args.vg);
			}

			// WHAT CAN BE TAKEN HOLD OF, and only on the path the controls belong to — eight
			// sets of handles would be a picture of handles. A circle shows its centre, which is
			// what moves it; a line shows its two ends, which is what changes it.
			if (p.kind == Path::CIRCLE && primary) {
				const math::Vec c = toScreen(p.cx, p.cy);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, 5.f);
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0xaa));
				nvgFill(args.vg);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, 3.5f);
				nvgFillColor(args.vg, ink);
				nvgFill(args.vg);
				// A CROSS THROUGH IT, so it is a centre mark and not another sprite.
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, c.x - 8.f, c.y);
				nvgLineTo(args.vg, c.x + 8.f, c.y);
				nvgMoveTo(args.vg, c.x, c.y - 8.f);
				nvgLineTo(args.vg, c.x, c.y + 8.f);
				nvgStrokeColor(args.vg, nvgRGBA(ink.r * 255, ink.g * 255, ink.b * 255, 0x99));
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);
			}
			if (p.kind == Path::LINE && primary) {
				for (int end = 0; end < 2; end++) {
					const math::Vec at = toScreen(end ? p.bx : p.ax, end ? p.by : p.ay);
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, at.x, at.y, 4.f);
					nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0xaa));
					nvgFill(args.vg);
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, at.x, at.y, 3.f);
					nvgFillColor(args.vg, ink);
					nvgFill(args.vg);
				}
			}

			// THE SPRITE, where it is now.
			float x = 0.f, y = 0.f;
			p.at(module->at[i], &x, &y);
			const math::Vec at = toScreen(x, y);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, at.x, at.y, primary ? 5.f : 4.f);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0xbb));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, at.x, at.y, primary ? 3.5f : 2.5f);
			nvgFillColor(args.vg, ink);
			nvgFill(args.vg);
		}
	}

	void drawChooser(const DrawArgs& args) {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBA(0x0d, 0x10, 0x14, 0xe8));
		nvgFill(args.vg);

		std::shared_ptr<window::Font> font = gridFont();
		if (font && font->handle >= 0) {
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 11.f);
			nvgFillColor(args.vg, nvgRGB(0xd2, 0xdb, 0xe4));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x / 2.f, 9.f, "a picture from a file", NULL);
		}

		for (int i = 0; i < (int) cells.size() && i < ACROSS * DOWN; i++) {
			const math::Rect r = cellRect(i);
			if (thumbs[(size_t) i] < 0) {
				std::vector<uint8_t> bytes;
				if (pictureThumb(cells[(size_t) i], bytes))
					thumbs[(size_t) i] = nvgCreateImageRGBA(args.vg, THUMB_SIDE, THUMB_SIDE, 0,
						bytes.data());
			}
			if (thumbs[(size_t) i] >= 0) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, r.pos.x + 1.f, r.pos.y + 1.f, r.size.x - 2.f, r.size.y - 2.f);
				nvgFillPaint(args.vg, nvgImagePattern(args.vg, r.pos.x + 1.f, r.pos.y + 1.f,
					r.size.x - 2.f, r.size.y - 2.f, 0.f, thumbs[(size_t) i], 1.f));
				nvgFill(args.vg);
			}
			const bool current = module && module->picture.has()
				&& cells[(size_t) i].name == module->picture.name;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x - 1.f, r.size.y - 1.f);
			nvgStrokeColor(args.vg, (i == hoverCell) ? nvgRGBA(0x78, 0x96, 0x78, 0xff)
				: current ? nvgRGBA(0xff, 0xf1, 0x4a, 0xcc) : nvgRGBA(0x50, 0x5a, 0x6a, 0xcc));
			nvgStrokeWidth(args.vg, (i == hoverCell || current) ? 2.f : 1.f);
			nvgStroke(args.vg);
		}
	}
};


/** THE TABS: which path the controls belong to. The same control as mpxPattern's and mpxSprites',
each with its own light so every path's state is visible at once rather than only the one in
front, and switching one on brings it to the front while switching one off leaves you where you
were. */
struct PathTabs : widget::OpaqueWidget {
	PathSpritesModule* module = NULL;

	float tabsFrom() const { return box.size.y + 3.f; }
	float tabWidth() const { return (box.size.x - tabsFrom()) / MAX_PATHS; }

	static const int ALL_BUTTON = -2;
	int at(math::Vec pos) {
		if (box.size.x <= 0.f)
			return -1;
		if (pos.x < box.size.y)
			return ALL_BUTTON;
		if (pos.x < tabsFrom())
			return -1;
		return math::clamp((int) ((pos.x - tabsFrom()) / tabWidth()), 0, MAX_PATHS - 1);
	}

	math::Rect lampRect(int i) const {
		const float w = tabWidth();
		const float r = std::min(w * 0.145f, box.size.y * 0.28f);
		return math::Rect(math::Vec(tabsFrom() + i * w + 2.5f, box.size.y / 2.f - r),
			math::Vec(2.f * r, 2.f * r));
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const int i = at(e.pos);
			const int all = (1 << MAX_PATHS) - 1;
			if (i == ALL_BUTTON) {
				module->selection.store(all);
				e.consume(this);
				return;
			}
			if (i >= 0) {
				const int chosen = module->selection.load() | (1 << module->tab.load());
				const int bit = 1 << i;
				if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
					int next = chosen ^ bit;
					if (next == 0)
						next = bit;
					module->selection.store(next);
					if (!(next & (1 << module->tab.load())))
						for (int k = 0; k < MAX_PATHS; k++)
							if (next & (1 << k)) {
								module->tab.store(k);
								break;
							}
				}
				else if (lampRect(i).contains(e.pos)) {
					const bool turningOn = !module->enabled(i);
					module->flipEnable.store(i);
					if (turningOn) {
						module->selection.store(bit);
						module->tab.store(i);
					}
				}
				else {
					module->selection.store(bit);
					module->tab.store(i);
				}
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = gridFont();
		const int shown = module ? math::clamp(module->tab.load(), 0, MAX_PATHS - 1) : 0;
		const int chosen = module ? (module->selection.load() | (1 << shown)) : 1;
		const int all = (1 << MAX_PATHS) - 1;

		{
			const float s = box.size.y;
			const bool lit = chosen == all;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.75f, 0.75f, s - 1.5f, s - 1.5f, 2.f);
			nvgFillColor(args.vg, lit ? nvgRGB(0xd8, 0xde, 0xe6) : nvgRGB(0x2a, 0x30, 0x3a));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0xd8, 0xde, 0xe6, lit ? 255 : 150));
			nvgStrokeWidth(args.vg, lit ? 1.4f : 0.8f);
			nvgStroke(args.vg);
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 7.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, lit ? nvgRGB(0x14, 0x18, 0x1e) : nvgRGB(0xc8, 0xd0, 0xd8));
				nvgText(args.vg, s / 2.f, s / 2.f, "ALL", NULL);
			}
		}

		const float w = tabWidth();
		for (int i = 0; i < MAX_PATHS; i++) {
			const float x = tabsFrom() + i * w;
			const bool on = (chosen & (1 << i)) != 0;
			const bool primary = (i == shown);
			const NVGcolor ink = PATH_INK[i];

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 0.75f, 0.75f, w - 1.5f, box.size.y - 1.5f, 2.f);
			nvgFillColor(args.vg, on
				? nvgRGBA((unsigned char) (ink.r * 255), (unsigned char) (ink.g * 255),
					(unsigned char) (ink.b * 255), 210)
				: nvgRGB(0x2a, 0x30, 0x3a));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, primary && chosen != (1 << i)
				? nvgRGB(0xff, 0xff, 0xff)
				: nvgRGBA((unsigned char) (ink.r * 255), (unsigned char) (ink.g * 255),
					(unsigned char) (ink.b * 255), on ? 255 : 150));
			nvgStrokeWidth(args.vg, primary ? 1.6f : (on ? 1.2f : 0.8f));
			nvgStroke(args.vg);

			{
				const math::Rect r = lampRect(i);
				const bool liveNow = module && module->enabled(i);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y / 2.f,
					r.size.x / 2.f);
				nvgFillColor(args.vg, liveNow ? nvgRGB(0xff, 0x4a, 0x40) : nvgRGB(0x6a, 0x74, 0x82));
				nvgFill(args.vg);
				nvgStrokeColor(args.vg, liveNow ? nvgRGBA(0xff, 0xc0, 0xb8, 0xdd)
					: nvgRGBA(0xc0, 0xc8, 0xd2, 0xcc));
				nvgStrokeWidth(args.vg, 1.2f);
				nvgStroke(args.vg);
			}

			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 9.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, on ? nvgRGB(0x14, 0x18, 0x1e) : nvgRGB(0xc8, 0xd0, 0xd8));
				const float from = lampRect(i).pos.x + lampRect(i).size.x;
				nvgText(args.vg, (from + x + w - 2.f) / 2.f, box.size.y / 2.f,
					string::f("%d", i + 1).c_str(), NULL);
			}
		}
		widget::OpaqueWidget::draw(args);
	}
};


static Layout pathSpritesLayout() {
	Layout L;
	L.hp = PS_PANEL_HP;
	L.title = "mpxPathSprites";
	// THE NAME GOES OVER THE CONTROLS, not across the top of the panel: the picture takes the
	// whole height and there is no strip along the top to write in.
	L.titleWidth = PS_COLUMN;

	const float left = PS_INSET;
	const float colMid = PS_COLUMN / 2.f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			float size = 0.f, const std::string& owner = "",
			Panel::Align align = Panel::CENTRE) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = true; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			float size = 0.f) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + 7.6f, name, size, key);
	};

	auto plate = [&](const std::string& key, float x, float y, int id, int chars,
			const std::string& name, const std::vector<std::string>& notes = {}) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "readout";
		i.x = x; i.y = y; i.chars = chars; i.h = 3.0f; i.notes = notes;
		L.items.push_back(i);
		label(key + ".label", x, y + 5.0f, name, 0.f, key);
	};

	auto lamps = [&](const std::string& key, float x, float y, int id,
			const std::vector<std::string>& names, const std::string& group) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "lamps";
		i.x = x; i.y = y; i.names = names; i.horizontal = true; i.pitch = 11.f;
		i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label(key + ".group", x + 10.f, y - 4.2f, group, 0.f, key);
	};

	auto display = [&](const std::string& key, float x, float y, float w, float h) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY; i.x = x; i.y = y; i.w = w; i.h = h;
		L.items.push_back(i);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label(key + ".label", x, y - 5.6f, name, 6.4f, key);
	};

	// THE PICTURE, the whole height of the panel on the right.
	display("d.picture", PS_COLUMN + PS_INSET, PS_INSET, PS_SQUARE, PS_SQUARE);

	// THE TABS at the top of the column.
	display("d.tabs", left, 14.f, PS_COLUMN - 2.f * PS_INSET, 6.f);

	lamps("p.kind", left + 3.f, 26.f, PathSpritesModule::P_KIND, {"LINE", "CIRCLE"}, "PATH");
	knob("p.diameter", colMid, 40.f, PathSpritesModule::P_DIAMETER, "DIAMETER");

	plate("p.source", colMid, 56.f, PathSpritesModule::P_SOURCE, 10, "READS",
		std::vector<std::string>(CV_SOURCE_NOTES, CV_SOURCE_NOTES + NUM_CV_SOURCES));

	knob("p.rate", colMid - 11.f, 72.f, PathSpritesModule::P_RATE, "RATE");
	knob("p.smooth", colMid + 11.f, 72.f, PathSpritesModule::P_SMOOTH, "SMOOTH");
	lamps("p.rateFrom", left + 3.f, 84.f, PathSpritesModule::P_RATE_FROM,
		{"FREE", "THE BEAT"}, "RATE FROM");

	knob("p.level", colMid - 11.f, 98.f, PathSpritesModule::P_LEVEL, "LEVEL");
	knob("p.offset", colMid + 11.f, 98.f, PathSpritesModule::P_OFFSET, "OFFSET");

	// THE OUTPUTS: one a sprite, in two rows of four, and the polyphonic one beside the input.
	for (int i = 0; i < MAX_PATHS; i++) {
		const float x = left + 5.5f + (float) (i % 4) * 9.5f;
		const float y = 112.f + (float) (i / 4) * 9.f;
		Item o;
		o.key = string::f("out.cv%d", i + 1);
		o.kind = Item::PORT_OUT; o.id = PathSpritesModule::O_CV + i; o.x = x; o.y = y;
		o.ring = PATH_INK[i];
		L.items.push_back(o);
	}
	label("l.outs", left + 5.5f, 106.f, "OUT", 6.4f, "", Panel::LEFT);

	jack("in.chart", Item::PORT_IN, PS_COLUMN - 9.f, 112.f, PathSpritesModule::I_CHART,
		"mpx", NOTE_CABLE);
	jack("out.poly", Item::PORT_OUT, PS_COLUMN - 9.f, 121.f, PathSpritesModule::O_POLY,
		"ALL", nvgRGB(0x9a, 0xa4, 0xb2));

	Item run;
	run.key = "p.run"; run.kind = Item::PARAM; run.id = PathSpritesModule::P_RUN;
	run.style = "latch"; run.diameter = 5.5f; run.x = left + 5.5f; run.y = 98.f;
	L.items.push_back(run);
	label("p.run.label", left + 5.5f, 104.f, "RUN", 6.4f, "p.run");

	L.bindOffsets();
	return L;
}


struct PathSpritesWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	PathSpritesWidget(PathSpritesModule* module) {
		setModule(module);
		layout = pathSpritesLayout();
		layoutApplyUser("mpxPathSprites", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		PictureField* field = new PictureField;
		field->module = module;
		layoutPlaceDisplay(this, layout, "d.picture", field);

		PathTabs* tabs = new PathTabs;
		tabs->module = module;
		layoutPlaceDisplay(this, layout, "d.tabs", tabs);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPathSprites");
	}

	void step() override {
		ModuleWidget::step();
		PathSpritesModule* m = dynamic_cast<PathSpritesModule*>(module);
		if (!m)
			return;
		// THE TABLES ARE BUILT HERE, on the main thread: building one walks a thousand pixels of
		// a picture, which is not a thing to do between two samples.
		m->tick();

		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(PathSpritesModule::I_CHART)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				if (n >= MAX_UPSTREAM)
					break;
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int s = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (s < 0)
					continue;
				cw->color = NOTE_CABLE;
				slots[n] = s;
				generations[n] = g;
				n++;
			}
		}
		m->link(slots, generations, n);
	}
};


} // namespace px
