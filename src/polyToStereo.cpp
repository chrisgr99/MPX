/** polyToStereo — where a patch's voices become a signal a mixer can take.

WHAT IT IS. Per voice it is a channel strip: two gain stages and a pan. Sixteen strips arriving at
one destination is a mixdown in everything but the fader caps.

IT IS NOT AN MPX MODULE, and that is deliberate. Any polyphonic patch can use it. It takes no note
cable because UNBUNDLING ASSIGNS VOICES: fromMPX decides which arriving note goes into which
channel, using its own polyphony count and its own rollover rule, and a second module doing its
own allocation would put channel three's velocity on channel three's audio only by luck. One
module turns a note cable into voltages and everything after it works in voltages.

TWO LEVEL INPUTS, MULTIPLIED. A voice has two amplitudes that mean different things: the envelope,
which is the note's shape over time, and the velocity, which is how hard it was struck — one
number for the whole note. Their product is what every polysynth computes, and with a single level
input you would need a separate amplifier to compute it, which is this module coming back. So the
multiply lives here and the panel prints the sign between them.

They are A and B rather than ENVELOPE and VELOCITY because multiplication is symmetric and the
module cannot tell which you patched. Breath goes in either one: pressure is a level, and a player
sending pressure is not also sending velocity.

TWO RATHER THAN THREE. A voice has three amplitudes on paper — envelope, velocity, pressure — but
the last two rarely coexist. A keyboard sends velocity and no meaningful pressure; a breath
controller sends pressure and barely any velocity. A third input covers a case nobody plays.

AND NOTHING THAT DEPENDS ON WHAT THE VOICE IS MADE OF. Timbre acts before the amplifier, on a
filter or a folder or an oscillator's shape; this is after the sound exists, and filtering after
the amplifier is a different thing and usually the wrong one.

WHAT IS DIFFERENT FROM DREAMRACK'S, which is the same module:

  - ONE INSTANCE FOR ALL THE VOICES. There, a voice was a copy of a page and each copy carried its
    own strip, with the summing done by the rack. Here polyphony is channels on one cable, so one
    module holds every strip and does its own summing.
  - KNOBS AND JACKS RATHER THAN A knАck. There the CV arrives in the knob; Rack has no such
    control, so each stage is a knob and a jack beside it, and the knob is the offset exactly as
    it was — nothing patched with the knob at the top is unity, so a module just placed passes
    audio through untouched.
  - A PAN LAW HAD TO BE CHOSEN. Equal power, so a voice swept across keeps its loudness rather
    than dipping in the middle.
  - MONO FOLDS DOWN. With only the left output patched it carries the sum, because a patch being
    listened to in mono should not silently lose half of itself.
*/
#include "plugin.hpp"
#include "Layout.hpp"

#include <cmath>

namespace px {


struct PolyStereoModule : Module {
	enum ParamId {
		P_A,
		P_B,
		P_PAN,
		NUM_PARAMS
	};
	enum InputId {
		I_AUDIO,
		I_A,
		I_B,
		I_PAN,
		NUM_INPUTS
	};
	enum OutputId {
		O_LEFT,
		O_RIGHT,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	PolyStereoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// UNITY AT THE TOP, so a module with nothing patched passes what it is given. Turn a
		// stage down and patch it, and the stage follows what arrives.
		configParam(P_A, 0.f, 1.f, 1.f, "Level A", "%", 0.f, 100.f);
		configParam(P_B, 0.f, 1.f, 1.f, "Level B", "%", 0.f, 100.f);
		configParam(P_PAN, -1.f, 1.f, 0.f, "Pan");
		configInput(I_AUDIO, "Poly audio");
		configInput(I_A, "Poly level A");
		configInput(I_B, "Poly level B");
		configInput(I_PAN, "Pan");
		configOutput(O_LEFT, "Left");
		configOutput(O_RIGHT, "Right");
		configBypass(I_AUDIO, O_LEFT);
		configBypass(I_AUDIO, O_RIGHT);
	}

	void process(const ProcessArgs& args) override {
		const int n = std::max(1, inputs[I_AUDIO].getChannels());
		const float knobA = params[P_A].getValue();
		const float knobB = params[P_B].getValue();
		const float knobPan = params[P_PAN].getValue();

		float left = 0.f, right = 0.f;
		for (int c = 0; c < n; c++) {
			// THE KNOB IS THE OFFSET AND THE CABLE ADDS TO IT. Ten volts is unity, which is what
			// an envelope and a velocity both come out at.
			const float a = clamp(knobA + inputs[I_A].getPolyVoltage(c) / 10.f, 0.f, 2.f);
			const float b = clamp(knobB + inputs[I_B].getPolyVoltage(c) / 10.f, 0.f, 2.f);
			// PAN SUMS RATHER THAN REPLACES, so a lane carrying where each note was played can
			// be patched here and the knob still moves the whole part.
			const float pan = clamp(knobPan + inputs[I_PAN].getPolyVoltage(c) / 5.f, -1.f, 1.f);

			// Equal power: a voice swept across keeps its loudness instead of dipping in the
			// middle, which is what a plain linear pan does.
			const float theta = (pan + 1.f) * (float) M_PI / 4.f;
			const float v = inputs[I_AUDIO].getVoltage(c) * a * b;
			left += v * std::cos(theta);
			right += v * std::sin(theta);
		}

		// The pair is louder than the sum by the square root of two at the centre, which is what
		// equal power means; nothing is scaled to hide it.
		if (outputs[O_RIGHT].isConnected()) {
			outputs[O_LEFT].setChannels(1);
			outputs[O_RIGHT].setChannels(1);
			outputs[O_LEFT].setVoltage(left);
			outputs[O_RIGHT].setVoltage(right);
		}
		else {
			// Only one output patched: it carries the sum rather than half the patch.
			outputs[O_LEFT].setChannels(1);
			outputs[O_LEFT].setVoltage((left + right) * 0.70710678f);
			outputs[O_RIGHT].setChannels(1);
			outputs[O_RIGHT].setVoltage(0.f);
		}
	}
};


static Layout polyStereoLayout() {
	Layout L;
	L.hp = 8.f;
	L.title = "polyToStereo";
	L.titleAbove = "DREAMER DEVELOPMENT";

	static const float NAME_HALF = 1.22f, KNOB_EDGE = 4.8f, PORT_EDGE = 4.01f, GAP = 2.f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			bool heading = false, float size = 0.f, const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = Panel::CENTRE; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
		i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + KNOB_EDGE + GAP + NAME_HALF, name, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring, float size = 7.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		// Two millimetres from the jack's edge, and the half-height of the name at its own size.
		// A capital stands 0.72 of the font size, which is in Rack's pixels — seventy-five to
		// the inch — so it takes 25.4/75 to become millimetres.
		const float halfName = 0.36f * size * 25.4f / 75.f;
		label(key + ".label", x, y + PORT_EDGE + GAP + halfName, name, false, size, key);
	};

	// THE AUDIO COMES IN AT THE TOP AND LEAVES AT THE BOTTOM, with the two stages and the pan
	// between them in the order the signal passes through them.
	// POLY AUDIO, said plainly: this takes the whole chord on one cable, and somebody looking at
	// a jack marked only "audio" has no reason to expect that.
	jack("in.audio", Item::PORT_IN, 20.3f, 22.f, PolyStereoModule::I_AUDIO, "poly\naudio",
		SIG_AUDIO, 9.f);

	knob("p.a", 12.f, 44.f, PolyStereoModule::P_A, "A");
	knob("p.b", 28.6f, 44.f, PolyStereoModule::P_B, "B");
	// The sign between them, because the two stages multiply and nothing else on the panel
	// would say so.
	label("h.times", 20.3f, 44.f, "x", false, 11.f);
	// POLY LEVEL, over two lines like the audio above them: each carries one level per voice,
	// not one level for the lot, and the word that says so is worth the height.
	jack("in.a", Item::PORT_IN, 12.f, 62.f, PolyStereoModule::I_A, "poly\nlevel", SIG_CV, 9.f);
	jack("in.b", Item::PORT_IN, 28.6f, 62.f, PolyStereoModule::I_B, "poly\nlevel", SIG_CV, 9.f);

	knob("p.pan", 20.3f, 84.f, PolyStereoModule::P_PAN, "PAN");
	jack("in.pan", Item::PORT_IN, 20.3f, 100.f, PolyStereoModule::I_PAN, "pan", SIG_CV);

	jack("out.l", Item::PORT_OUT, 12.f, 118.f, PolyStereoModule::O_LEFT, "left", SIG_AUDIO);
	jack("out.r", Item::PORT_OUT, 28.6f, 118.f, PolyStereoModule::O_RIGHT, "right", SIG_AUDIO);

	L.bindOffsets();
	return L;
}


struct PolyStereoWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	PolyStereoWidget(PolyStereoModule* module) {
		setModule(module);
		layout = polyStereoLayout();
		layoutApplyUser("polyToStereo", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "polyToStereo");
	}
};


} // namespace px


Model* modelPolyToStereo = createModel<px::PolyStereoModule, px::PolyStereoWidget>("polyToStereo");
