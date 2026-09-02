#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <cmath>
#include <cstdio>

namespace px {


/** How many recent events are kept for the display. */
static const int LOG = 14;


/** Shows what is on an MPX cable, and passes it on unchanged.

WHY IT EXISTS AT ALL. An MPX cable carries no voltage, so a scope says nothing about it and the
only feedback anywhere is whether a lamp lights. Every module built after this one is easier to
build with a window showing the notes going past and the harmony they are played against.

IT IS ALSO THE FIRST PROCESSOR, and the first test of forwarding: it consumes a stream, changes
nothing, and puts the same stream out. Anything it drops would be dropped silently, which is
exactly the failure the forwarding rule exists to prevent — so getting it right here is worth
more than the module itself. */
struct MonitorModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_HOLD,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		NUM_OUTPUTS
	};
	enum LightId {
		L_LINKED,
		L_ACTIVE,
		NUM_LIGHTS
	};

	int slot = -1;
	uint32_t generation = 0;
	BusReader reader;

	/** Set on the main thread when the patching changes, acted on by the audio thread, so the
	reader is only ever touched by the thread that reads it. */
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};

	/** THE LOG, written by the audio thread and read by the drawing one. A ring of plain
	values with an index that only goes up: the worst a torn read can do is show one line that
	was overwritten as it was read, which is a flicker in a display rather than a fault. */
	struct Line {
		uint8_t kind = 0;
		uint8_t lane = 0;
		int64_t handle = 0;
		float a = 0.f, b = 0.f, c = 0.f;
	};
	Line log[LOG];
	std::atomic<uint32_t> logWrite{0};

	/** The harmony as last seen, for the display. */
	Harmony seen;
	std::atomic<bool> seenValid{false};
	int sounding = 0;

	MonitorModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// HOLD STOPS THE DISPLAY, NOT THE CABLE. Notes go on through either way: a monitor
		// that could silence a patch by being read would be a trap.
		configSwitch(P_HOLD, 0.f, 1.f, 0.f, "Display", {"Running", "Held"});
		configInput(I_MPX, "MPX note in");
		configOutput(O_MPX, "MPX note out");
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~MonitorModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_MPX;
	}

	/** Called from the widget once a frame with whatever the cables say. */
	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++) {
			same = (slots[i] == wantSlots[i].load())
				&& (generations[i] == wantGenerations[i].load());
		}
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store(std::min(n, MAX_UPSTREAM));
		relink.store(true);
	}

	void record(const Event& e) {
		const uint32_t w = logWrite.load(std::memory_order_relaxed);
		Line& line = log[w % LOG];
		line.kind = e.kind;
		line.lane = e.lane;
		line.handle = e.handle;
		if (e.kind == Event::ON) {
			line.a = e.pitch;
			line.b = e.level;
			line.c = e.duration;
		}
		else {
			line.a = e.value;
			line.b = 0.f;
			line.c = 0.f;
		}
		logWrite.store(w + 1, std::memory_order_release);
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		if (slot < 0)
			return;

		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		// FORWARDED AS A WHOLE, not rebuilt. The event is copied and pushed on, so a lane this
		// module has never heard of travels through it untouched.
		const bool held = params[P_HOLD].getValue() > 0.5f;
		Event e;
		bool any = false;
		while (reader.next(e)) {
			if (!held)
				record(e);
			if (e.kind == Event::ON)
				sounding++;
			else if (e.kind == Event::OFF && sounding > 0)
				sounding--;
			busPush(slot, e);
			any = true;
		}

		// The harmony is state rather than a stream, so forwarding it is a copy.
		Harmony h;
		if (reader.harmony(h)) {
			busPublishHarmony(slot, h);
			if (!held) {
				seen = h;
				seenValid.store(true);
			}
		}
		else if (!held) {
			seenValid.store(false);
		}

		lights[L_LINKED].setBrightness(reader.attached() ? 1.f : 0.f);
		lights[L_ACTIVE].setBrightnessSmooth(any ? 1.f : 0.f, args.sampleTime);
	}
};


/** A note number as a name, so a pitch reads as a pitch. Zero volts is middle C, which is
Rack's convention. */
static std::string voltsAsNote(float volts) {
	const int semis = (int) std::lround(volts * 12.f) + 60;
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%s%d", pitchClassName(semis), semis / 12 - 1);
	return buf;
}


struct MonitorDisplay : widget::Widget {
	MonitorModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x35, 0x3c, 0x47));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const float pad = 5.f;
		float y = 11.f;

		if (!module) {
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, PANEL_DIM);
			nvgText(args.vg, pad, y, "mpxMonitor", NULL);
			return;
		}

		char buf[80];
		nvgFontSize(args.vg, 10.f);

		// ---- the harmony, three short lines rather than two long ones ----
		if (module->seenValid.load()) {
			const Harmony& h = module->seen;
			nvgFillColor(args.vg, PANEL_DIM);
			std::snprintf(buf, sizeof(buf), "%s %s  %d/%d  cycle %.0f",
				pitchClassName(h.key.tonic), h.key.minor ? "min" : "maj",
				h.barBeats, h.barUnit, h.cycleBeats);
			nvgText(args.vg, pad, y, buf, NULL);
			y += 12.f;

			nvgFillColor(args.vg, nvgRGB(0xff, 0x3c, 0xc8));
			nvgFontSize(args.vg, 12.f);
			std::snprintf(buf, sizeof(buf), "%s \u2192 %s   in %.1f",
				chordLetter(h.current, h.key).c_str(),
				chordLetter(h.next, h.key).c_str(), h.beatsToNext);
			nvgText(args.vg, pad, y, buf, NULL);
			y += 13.f;

			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, PANEL_DIM);
			std::snprintf(buf, sizeof(buf), "%s   bar %d  beat %.1f",
				chordRoman(h.current).c_str(), h.bar + 1, h.beatInBar + 1.f);
			nvgText(args.vg, pad, y, buf, NULL);
			y += 14.f;
		}
		else {
			nvgFillColor(args.vg, nvgRGB(0x6a, 0x72, 0x7e));
			nvgText(args.vg, pad, y, "no harmony here", NULL);
			y += 26.f;
		}

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, pad, y - 5.f);
		nvgLineTo(args.vg, box.size.x - pad, y - 5.f);
		nvgStrokeColor(args.vg, nvgRGB(0x2c, 0x32, 0x3b));
		nvgStroke(args.vg);

		if (module->params[MonitorModule::P_HOLD].getValue() > 0.5f) {
			nvgFillColor(args.vg, nvgRGB(0xff, 0x9a, 0x3c));
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x - pad, 11.f, "HELD", NULL);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}

		// ---- the notes, newest at the top ----
		nvgFontSize(args.vg, 10.f);
		const uint32_t w = module->logWrite.load(std::memory_order_acquire);
		if (w == 0) {
			nvgFillColor(args.vg, nvgRGB(0x6a, 0x72, 0x7e));
			nvgText(args.vg, pad, y + 3.f, "no notes yet", NULL);
			return;
		}
		const int shown = (int) std::min<uint32_t>(w, LOG);
		for (int i = 0; i < shown; i++) {
			const MonitorModule::Line& line = module->log[(w - 1 - i) % LOG];
			if (line.kind == Event::ON) {
				nvgFillColor(args.vg, nvgRGB(0x3d, 0xd6, 0x8c));
				std::snprintf(buf, sizeof(buf), "on  %-4s %.2f %.2fs %lld",
					voltsAsNote(line.a).c_str(), line.b, line.c,
					(long long) (line.handle % 1000));
			}
			else if (line.kind == Event::OFF) {
				nvgFillColor(args.vg, nvgRGB(0x8a, 0x92, 0x9e));
				std::snprintf(buf, sizeof(buf), "off                %lld",
					(long long) (line.handle % 1000));
			}
			else {
				static const char* LANE[] = {"bend", "prs", "tmb"};
				nvgFillColor(args.vg, nvgRGB(0xff, 0x9a, 0x3c));
				std::snprintf(buf, sizeof(buf), "%-4s %+.3f        %lld",
					line.lane < 3 ? LANE[line.lane] : "?", line.a,
					(long long) (line.handle % 1000));
			}
			nvgText(args.vg, pad, y + 3.f + i * 11.f, buf, NULL);
		}
	}
};


// ---- panel -------------------------------------------------------------------------------

static Layout monitorLayout() {
	Layout L;
	L.hp = 14.f;
	L.title = "mpxMonitor";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, float size) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.f, name, Panel::CENTRE, true, size, key);
	};

	jack("in.mpx", Item::PORT_IN, 12.f, 116.f, MonitorModule::I_MPX, "mpxIn", 12.f);
	jack("out.mpx", Item::PORT_OUT, 59.f, 116.f, MonitorModule::O_MPX, "mpxOut", 12.f);

	Item hold;
	hold.key = "p.hold"; hold.kind = Item::PARAM; hold.id = MonitorModule::P_HOLD;
	hold.style = "lamps"; hold.x = 28.f; hold.y = 111.f;
	hold.pitch = 9.f; hold.names = {"RUN", "HOLD"};
	hold.labelSide = Panel::RIGHT;
	L.items.push_back(hold);

	Item linked;
	linked.key = "lamp.linked"; linked.kind = Item::LIGHT;
	linked.id = MonitorModule::L_LINKED; linked.x = 20.f; linked.y = 111.f;
	linked.owner = "in.mpx";
	L.items.push_back(linked);
	Item active;
	active.key = "lamp.active"; active.kind = Item::LIGHT;
	active.id = MonitorModule::L_ACTIVE; active.x = 51.f; active.y = 111.f;
	active.owner = "out.mpx";
	L.items.push_back(active);

	L.bindOffsets();
	return L;
}

struct MonitorWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	MonitorWidget(MonitorModule* module) {
		setModule(module);
		layout = monitorLayout();
		layoutApplyUser("mpxMonitor", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		MonitorDisplay* display = new MonitorDisplay;
		display->module = module;
		display->box.pos = mm2px(math::Vec(3.f, 13.f));
		display->box.size = mm2px(math::Vec(65.f, 91.f));
		addChild(display);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxMonitor");
	}

	/** Reads every cable on the input, since Rack allows several and interleaving them is what
	makes a parallel chain work. */
	void step() override {
		ModuleWidget::step();
		MonitorModule* mon = dynamic_cast<MonitorModule*>(module);
		if (!mon)
			return;
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(MonitorModule::I_MPX)) {
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
		mon->link(slots, generations, n);

		if (PortWidget* out = getOutput(MonitorModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMonitor = createModel<px::MonitorModule, px::MonitorWidget>("mpxMonitor");
