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

/** The panel both modules are drawn with. No artwork ships with the plugin. */
struct Panel : widget::Widget {
	std::string titleAbove;
	std::string title;
	/** Set beside or under the thing it names. */
	struct Label {
		float x = 0.f, y = 0.f;
		std::string text;
		bool heading = false;
		bool left = false;
	};
	std::vector<Label> labels;
	/** Rules across the panel, separating one group of jacks from the next. */
	std::vector<float> rules;

	void draw(const DrawArgs& args) override;
};

/** The colour a note cable is drawn in, so the domain is visible in a patch. */
extern const NVGcolor NOTE_CABLE;

} // namespace px
