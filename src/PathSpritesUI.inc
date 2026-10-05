/** THE PICTURE AND THE TABS, for both path modules.

Included by each of them. Everything here works on a PathHost — the picture, the eight paths and
which tab is in front — and knows nothing of voltages or notes, which is the whole reason the two
modules can be separate without being written twice.
*/

namespace px {


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
	PathHost* module = NULL;
	int handle = -1;
	uint32_t shownGeneration = 0;

	~PictureField() {
		// NANOVG IMAGES ARE NOT FREED BY DELETING THE WIDGET. A module added and removed a few
		// times would otherwise leave a megabyte of picture behind on each one.
		if (APP && APP->window && APP->window->vg) {
			if (handle >= 0)
				nvgDeleteImage(APP->window->vg, handle);
			for (size_t i = 0; i < thumbs.size(); i++)
				if (thumbs[i] > 0)
					nvgDeleteImage(APP->window->vg, thumbs[i]);
		}
	}

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
	/** One picture decoded a frame at most, so a gallery of twenty fills in over a few frames
	rather than stopping the panel dead on the frame it is opened. */
	bool decodedThisFrame = false;

	math::Vec toPicture(math::Vec pos) const {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return math::Vec(0.5f, 0.5f);
		return math::Vec(math::clamp(pos.x / box.size.x, 0.f, 1.f),
			math::clamp(pos.y / box.size.y, 0.f, 1.f));
	}
	math::Vec toScreen(float x, float y) const {
		return math::Vec(x * box.size.x, y * box.size.y);
	}

	int tabNow() const { return module ? module->frontTab() : 0; }

	// ---- choosing a picture --------------------------------------------------------------------

	static const int ACROSS = 5, DOWN = 4;

	math::Rect cellRect(int i) const {
		const float w = box.size.x / (float) ACROSS;
		const float h = (box.size.y - 18.f) / (float) DOWN;
		return math::Rect(math::Vec((i % ACROSS) * w, 18.f + (i / ACROSS) * h),
			math::Vec(w, h));
	}

	/** Gives the gallery's images back. Opening it again reads them afresh, which is what makes a
	picture added since appear. */
	void releaseCells() {
		if (APP && APP->window && APP->window->vg)
			for (size_t i = 0; i < thumbs.size(); i++)
				if (thumbs[i] > 0)
					nvgDeleteImage(APP->window->vg, thumbs[i]);
		thumbs.clear();
	}

	void openChooser() {
		releaseCells();
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
		pictureRemember(kept.empty() ? path : kept, module->picture.name, module->picture.rgba);
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
						module->picture.rgba);
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
		if (module->pathOn(front))
			found = hitTest(e.pos, front);
		if (found != GRAB_NONE) {
			on = front;
		}
		else {
			for (int i = 0; i < MAX_PATHS && on < 0; i++) {
				if (i == front || !module->pathOn(i))
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

		// The ground, so an empty frame is a dark square rather than a hole in the panel.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, w, h);
		nvgFillColor(args.vg, nvgRGB(0x0d, 0x10, 0x14));
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
				// SAYS WHAT TO DO, rather than sitting there blank: an empty module that
				// explains itself is worth the two lines it costs.
				nvgFontSize(args.vg, 13.f);
				nvgFillColor(args.vg, nvgRGB(0x5a, 0x66, 0x74));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, w / 2.f, h / 2.f - 8.f, "No picture", NULL);
				nvgText(args.vg, w / 2.f, h / 2.f + 8.f, "Click to load one", NULL);
			}
		}

		if (!choosing)
			drawPaths(args);
		else
			drawChooser(args);

		// THE FRAME LAST, so nothing is drawn over it, and in the panel's own quiet line rather
		// than anything brighter — the module already has a green border and two green lines that
		// close together read as a mistake. The same frame mpxSprites draws, because these are the
		// same thing and should not look like three different things.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 0.5f, w - 1.f, h - 1.f);
		nvgStrokeColor(args.vg, PANEL_RULE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		widget::OpaqueWidget::draw(args);
	}

	void drawPaths(const DrawArgs& args) {
		if (!module)
			return;
		const int front = tabNow();
		for (int i = 0; i < MAX_PATHS; i++) {
			if (!module->pathOn(i))
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

	/** THE PICTURE IN A CELL, AS A THUMBNAIL.

	THE KEPT THUMBNAIL IF THERE IS ONE, AND THE PICTURE ITSELF IF THERE IS NOT. An entry remembered
	before thumbnails were written, or one whose thumbnail file has gone, has no thumbnail to read
	— and reading it was all this did, so those cells came up empty while still choosing perfectly
	well when clicked. Failing that, the picture is decoded and a thumbnail made from it, which is
	slow enough that only one is done a frame.

	A FAILURE IS REMEMBERED TOO, as minus two rather than minus one, or a picture whose file has
	gone would be retried on every frame for as long as the gallery is open. */
	int thumbFor(const DrawArgs& args, int i) {
		std::vector<uint8_t> bytes;
		if (pictureThumb(cells[(size_t) i], bytes))
			return nvgCreateImageRGBA(args.vg, THUMB_SIDE, THUMB_SIDE, 0, bytes.data());
		if (decodedThisFrame)
			return -1;                  // next frame, so the grid fills in rather than stalling
		decodedThisFrame = true;
		Picture one;
		if (!one.load(cells[(size_t) i].path))
			return -2;
		pictureThumbnail(one.rgba, bytes);
		if (bytes.empty())
			return -2;      // it could not make one; see pictureThumbnail
		// AND KEPT, so the next time this gallery is opened it is read rather than decoded.
		pictureRemember(cells[(size_t) i].path, one.name, one.rgba);
		return nvgCreateImageRGBA(args.vg, THUMB_SIDE, THUMB_SIDE, 0, bytes.data());
	}

	void drawChooser(const DrawArgs& args) {
		decodedThisFrame = false;
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
			if (thumbs[(size_t) i] < 0)
				thumbs[(size_t) i] = thumbFor(args, i);
			if (thumbs[(size_t) i] > 0) {
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
	PathHost* module = NULL;

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
					const bool turningOn = !module->pathOn(i);
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
				const bool liveNow = module && module->pathOn(i);
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



} // namespace px
