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
extern Model* modelPolyToStereo;
extern Model* modelMpxArp;
extern Model* modelMpxScatter;

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
/** WHERE THE NEXT LINE OF A NAME SITS, as a multiple of the text's own size.

Uppercase text stands about 0.72 of its size tall, so the whole of the rest is the space between
one line and the next. It was 1.15, which left a gap three fifths of a letter's height and read
as two names rather than one in two lines; this leaves half that. Panel text is one or two short
words and wants setting tight.

In one place because two files need it: the panel draws by it and the layout measures by it, and
a name whose measured height disagrees with its drawn height sits at the wrong distance from
whatever it names. */
inline float panelLineStep(float size) { return size * 0.935f; }

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


	/** THE MARKS ROUND A KNOB that say what its positions are.

	A knob with detents and nothing drawn round it is a knob you have to turn while watching a
	tooltip. Ticks say how many positions there are and where they fall; numbers beside them say
	which is which, and then the knob can be set by looking at it. */
	struct Scale {
		float x = 0.f, y = 0.f;    /**< The centre of the knob it belongs to. */
		float radius = 0.f;        /**< Where the inner end of each tick sits. */
		float length = 0.f;        /**< How far each tick reaches outward. */
		int count = 2;             /**< Two draws the ends of the sweep and nothing between. */
		float textRadius = 0.f;    /**< Where the numbers sit, if there are any. */
		float textSize = 6.f;
		std::vector<std::string> marks;
	};
	std::vector<Scale> scales;

	/** Rules across the panel, separating one group from the next. */
	std::vector<float> rules;

	void draw(const DrawArgs& args) override;
};

/** A KNOB DRAWN RATHER THAN LOADED, so that it can be any size.

Rack ships five knobs and they are five fixed widths, which is fine until the width you want is
between two of them. Drawing it means the diameter is a number in millimetres like everything
else on the panel, and it means the sweep is ours: the marks round a knob have to agree with its
pointer exactly, and a knob whose angles are its own cannot drift from them.

It is also the same hand as the buttons and the lamps, which are drawn here too. */
struct DreamerKnob : app::Knob {
	/** Across, in millimetres. */
	float diameter = 9.6f;

	DreamerKnob();
	void setDiameter(float mm);
	void draw(const DrawArgs& args) override;
};

/** A NUMBER YOU CAN READ, AND A LIST YOU CAN CHOOSE FROM.

A knob with sixteen detents is a poor way to set a count: you cannot see what it says without a
tooltip, and you cannot get from four to twelve without dragging through everything between. A
readout shows the value in figures, and a click opens the whole list so any value is one press
away. The wheel still steps it, for the times when the next one along is what you want.

Its text is the parameter's own display string, so a switch shows its name and a number shows its
number, with whatever unit the module gave it. */
/** A JACK THAT WILL NOT TAKE A CABLE IT CANNOT USE.

An MPX cable is an ordinary Rack cable carrying nothing — the notes travel on a bus the two ends
find by looking at what is patched — so a cable between an MPX jack and an ordinary one is
accepted, looks entirely normal, and does nothing whatever. That is the worst kind of fault:
everything appears right and nothing happens.

So the connection is simply not made. Dropping onto one of these refuses outright; and a cable
made the other way round, onto some other maker's jack where we have no say over the drop, is
taken away on the next frame, which is soon enough to look like it never landed. A cable in a
saved patch goes the same way, since it never worked either.

REFUSING IS KINDER THAN ALLOWING. A connection that cannot work is more confusing once it is made
than it is by never appearing, because then the question becomes why the patch is silent. */
struct MPXPort : PJ301MPort {
	void step() override;
	void onDragDrop(const DragDropEvent& e) override;
};

/** Whether these two ends could carry notes to each other: one an MPX output, the other an MPX
input, or neither of them MPX at all. Anything mixed is refused. */
bool mpxCompatible(engine::Module* outModule, int outId, engine::Module* inModule, int inId);

/** MEASURED FROM Nunito-Bold ITSELF rather than estimated: a capital stands 0.7041 of the font
size and a digit advances 0.6011 of it. Everything a readout's size depends on comes from these
two, so a plate is exactly as big as what is written on it. */
static const float FIGURE_CAP = 0.7041f;
static const float FIGURE_ADVANCE = 0.6011f;
/** The surround, in millimetres, top to bottom and side to side together. */
static const float FIGURE_SURROUND = 1.f;
/** HOW MUCH SLOWER THAN A KNOB the wheel moves a readout.

MEASURED AGAINST RACK'S OWN SCALE rather than against the raw wheel. A scroll event's size
depends on the mouse, the platform and the host's own sensitivity setting, so a threshold in
those units means nothing — it was set to four of them and made no difference anybody could see,
because a single notch is worth many. Taking Rack's knob sensitivity and dividing gives a rate
that matches the knobs the user is already used to, in whatever units their wheel speaks.

Half rather than the quarter it was first set to: a quarter was slow enough to feel stuck. */
static const float READOUT_SCROLL_DIVISOR = 2.f;

struct Readout : ParamWidget {
	/** HOW MANY FIGURES IT HAS TO HOLD, which is what sets its width — a plate wide enough for
	four when it will only ever show two is a hole in the panel.

	SET IT. Nought means two, which holds anything up to 99. It used to mean "work it out from the
	parameter's range", which had to happen after the widget was placed and therefore moved it —
	and it tied a panel's arrangement to a parameter's range, so widening a range from 99 to 100
	shifted a plate. A width is a decision about the panel, so the panel makes it. */
	int chars = 0;
	/** Wheel gathered but not yet spent. A trackpad sends a great many small movements where a
	wheel sends one large one, and adding them up rather than counting them means both behave
	the same. */
	float scrolled = 0.f;
	/** HOW TALL A FIGURE IS, in millimetres — the figure itself, not the plate it sits on. The
	plate is that and a millimetre, so it hugs what it shows. */
	float figureMM = 2.8f;

	Readout();
	void setFigures(int chars, float figureMM);
	/** The longest display string the parameter can produce, in characters. */
	int widestValue();
	/** NO step(). It had one, to work its width out from the parameter's range once the
	parameter existed; the width is a number in the layout now and never changes after the plate
	is made. Declaring it here without defining it anywhere is what stopped the whole plugin
	loading: the vtable is emitted beside the first virtual function that has a body, so a
	declaration with nothing behind it left px::Readout with no vtable at all, and Rack refused
	the library with "symbol not found". The compiler says nothing — the link is done by dlopen. */
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;
	/** The values it offers, which is every step between the parameter's ends. */
	void openList();
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
	/** The size the names are set at. */
	float nameSize = 8.f;
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
