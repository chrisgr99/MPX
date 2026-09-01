#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;
extern Model* modelFromMPX;
extern Model* modelToMPX;

namespace px {

/** The bus slot a toMPX output is writing to, so a fromMPX can find it across
the cable. Returns -1 for any other module, and for any port that is not a voice output. */
int noteBusOf(engine::Module* module, int outputId, uint32_t* generation);

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
		/** Deleted. Drawn only while the panel is being edited, and faintly, so the slot can
		be seen and the deletion taken back. */
		bool hidden = false;
	};
	std::vector<Label> labels;
	/** Set by the editor. A deleted label is nothing at all to everybody else. */
	bool showHidden = false;

	/** A coloured ring behind a jack, saying what family of signal it carries. Drawn by the
	panel rather than by a widget of its own, because it sits underneath the port and the panel
	is what is underneath everything. */
	struct Ring {
		float x = 0.f, y = 0.f;
		NVGcolor color;
	};
	std::vector<Ring> rings;

	/** Rules across the panel, separating one group from the next. */
	std::vector<float> rules;

	void draw(const DrawArgs& args) override;
};

/** A column or row of lamps, one per value of a stepped parameter, each with its name beside
it. A knob that snaps to five positions says nothing about what the five are; a list of five
names says all of it without a tooltip, and clicking a name is a bigger target than turning a
knob to a detent. */
struct Lamps : ParamWidget {
	std::vector<std::string> names;
	bool horizontal = false;
	/** Distance from one lamp to the next. */
	float pitch = 18.f;
	/** Where the names sit relative to the lamps. */
	Panel::Align labelSide = Panel::LEFT;

	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	/** The lamp nearest a point, or -1. */
	int lampAt(math::Vec pos);
	math::Vec lampPos(int i);
};

/** The panel's own colours, shared so a module drawing something of its own matches. */
extern const NVGcolor PANEL_BG;
extern const NVGcolor PANEL_INK;
extern const NVGcolor PANEL_DIM;
extern const NVGcolor PANEL_EDGE;

} // namespace px
