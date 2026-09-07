#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;
extern Model* modelFromMPX;
extern Model* modelToMPX;
extern Model* modelEuclid;
extern Model* modelProgression;
extern Model* modelMonitor;
extern Model* modelChart;
extern Model* modelMpxComp;

namespace px {

/** The bus slot a toMPX output is writing to, so a fromMPX can find it across
the cable. Returns -1 for any other module, and for any port that is not a voice output. */
int noteBusOf(engine::Module* module, int outputId, uint32_t* generation);

/** Whether this input is an MPX one — the other half of the question above, asked from the
sending end so a cable can be coloured for whether the link actually works rather than for which
jack it happens to leave. */
bool isMPXInput(engine::Module* module, int inputId);

/** Implemented by any module with an MPX input, so the sending end can tell whether a cable
lands somewhere that listens. The same question as noteBusOf, asked the other way round. */
struct NoteSink {
	virtual ~NoteSink() {}
	virtual bool isMPXInputId(int inputId) = 0;
};

/** The signal families, coloured the same way DreamRack colours them: the colour says what
kind of signal a jack carries, and it is the same code on every panel. */
extern const NVGcolor SIG_AUDIO;
extern const NVGcolor SIG_CV;
extern const NVGcolor SIG_GATE;
extern const NVGcolor SIG_PITCH;
/** The voice cable, and the jacks at each end of it. */
extern const NVGcolor NOTE_CABLE;

/** The panel both modules are drawn with. No artwork ships with the plugin. */
struct Panel : widget::Widget {
	std::string titleAbove;
	std::string title;

	enum Align { LEFT, CENTRE, RIGHT };

	struct Label {
		float x = 0.f, y = 0.f;
		std::string text;
		/** A section heading: larger, in the title face, and set in the panel's own ink
		rather than the dimmer label ink. */
		bool heading = false;
		Align align = CENTRE;
		float size = 0.f;   /**< Overrides the default size for this label. */
		/** Deleted, and so not drawn at all. Bringing one back is a menu item rather than
		something you can point at, because a panel with ghosts on it cannot be judged. */
		bool hidden = false;
	};
	std::vector<Label> labels;

	/** A square bracket enclosing several things, saying they belong together — and, where it
	reaches down to a control, that the control is theirs. Cheaper than repeating a name on
	every one of them, and it is how DreamRack marks the two modes that share a time. */
	struct Bracket {
		float x = 0.f, y = 0.f, h = 0.f, arm = 0.f;
	};
	std::vector<Bracket> brackets;


	/** Rules across the panel, separating one group from the next. */
	std::vector<float> rules;

	void draw(const DrawArgs& args) override;
};

/** A column or row of lamps, one per value of a stepped parameter, each with its name beside
it. A knob that snaps to five positions says nothing about what the five are; a list of five
names says all of it without a tooltip, and clicking a name is a bigger target than turning a
knob to a detent. */
/** A BUTTON THAT LOOKS LIKE A BUTTON.

Rack's stock buttons are dark on a dark panel, which on ours meant a control you had to know was
there. This one is drawn as a raised cap: a shadow under it, a body lit from the top, a bright
edge along the upper rim and a dark one along the lower, and a sheen across the top half. Those
are the four things that make a flat shape read as something standing proud of the surface, and
between them they make it obvious that it is for pressing.

Pressed, the lighting turns over — dark at the top, bright at the bottom — which is what a cap
going down actually does to the light. A latch that is ON is lit in the panel's accent, so its
state is a colour rather than a shade. */
void drawRaisedButton(NVGcontext* vg, math::Vec size, bool down, bool on);

/** The momentary one. Rack's Switch gives it its behaviour; this only draws. */
struct DreamerButton : app::Switch {
	DreamerButton();
	void draw(const DrawArgs& args) override;
};

/** The latching one, which stays down and lit. */
struct DreamerLatch : app::Switch {
	DreamerLatch();
	void draw(const DrawArgs& args) override;
};


/** THE TRANSPORT PAIR, drawn as a transport is drawn everywhere: a triangle that becomes two
bars while it is running, and a pair of arrows back.

A word would have done, but the symbols are read without being read — nobody spells out "play"
on a tape machine — and at six millimetres a symbol is legible where two words are not. */
struct DreamerPlay : app::Switch {
	DreamerPlay();
	void draw(const DrawArgs& args) override;
};

struct DreamerRewind : app::Switch {
	DreamerRewind();
	void draw(const DrawArgs& args) override;
};

/** The transport glyphs on their own, so the same shapes can be drawn in the chart window
without a parameter behind them. `playing` draws the pause bars instead of the triangle. */
void drawPlayGlyph(NVGcontext* vg, math::Vec size, bool playing);
void drawRewindGlyph(NVGcontext* vg, math::Vec size);


struct Lamps : ParamWidget {
	std::vector<std::string> names;
	bool horizontal = false;
	/** Distance from one lamp to the next. */
	float pitch = 18.f;
	/** Where the names sit relative to the lamps. */
	Panel::Align labelSide = Panel::LEFT;
	/** A horizontal pair with its names on the outside: the first to the left of its lamp, the
	rest to the right. Two lamps with both names on the right will not fit across a narrow
	panel, and this is how DreamRack draws the same switch. */
	bool labelsOutward = false;

	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	/** The lamp nearest a point, or -1. */
	int lampAt(math::Vec pos);
	math::Vec lampPos(int i);
	/** SIZES THE BOX FROM THE LAMPS RATHER THAN BEING TOLD. A box smaller than the row it holds
	is a control whose later lamps are outside it and can never be clicked, and that is not
	something an author should have to work out. The names are covered too, so clicking one
	chooses it — a word beside a lamp is part of the same control. */
	void fit();
};

/** Painted OVER the ports rather than behind them, because a jack's colour belongs on the jack.
Added last so it draws on top of everything the module has put down. */
struct JackPaint : widget::Widget {
	/** Pairs of port widget and the colour it should carry. Rebuilt when the layout changes. */
	struct Mark {
		widget::Widget* port = NULL;
		NVGcolor color;
		bool isOutput = false;
	};
	std::vector<Mark> marks;

	void draw(const DrawArgs& args) override;
};

/** Paints a jack in its signal family's colour, the same way Clarity paints every jack in the
rack: COLOUR SAYS WHAT KIND OF SIGNAL and SHAPE SAYS WHICH WAY IT GOES — the dashes hug the
outer edge on an output and the hole on an input. Never one cue carrying both, because a jack
has to answer two questions at once and neither answer should depend on reading the other. */
void drawJack(NVGcontext* vg, math::Vec c, float r, NVGcolor color, bool isOutput);

/** The panel's own colours, shared so a module drawing something of its own matches. */
extern const NVGcolor PANEL_BG;
extern const NVGcolor PANEL_INK;
extern const NVGcolor PANEL_DIM;
extern const NVGcolor PANEL_EDGE;

} // namespace px
