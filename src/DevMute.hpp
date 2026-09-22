#pragma once
/** THE SPACE BAR MUTES RACK — on a development machine only.

WHY IT EXISTS. Testing a generator means listening, stopping to think, and listening again, and
turning the output down by hand every time is a chore. The Mac's own mute would silence the
text-to-speech the tests are read back by. So one key silences what Rack plays and nothing else.

WHY IT IS NOT A FEATURE. A plugin cannot pause the engine or reach the audio device; all it can do
is turn down modules it knows. This turns down Rack's own audio modules — Core's AudioInterface
family, whose first parameter is the level — and would miss any other plugin's output module. A
half-working mute has no place in a shipped chart, so this does nothing unless a file named
`dev-keys` is in the DreamerMPX folder of the Rack user folder. Nobody else has that file.

HOW. Polled once a frame from the chart's panel, which is in every patch these tests use: the key
is read directly, so it works wherever the mouse is, but only while Rack's window is in front and
never while a text field is being typed into.

THE STATE IS THE LEVEL, NOT A FLAG. Muting sets the level to nought, and Rack saves that level in
the patch — so a patch saved while muted opens muted, with a flag in memory saying it is not. The
first version trusted the flag: the key then "muted" a level already at nought, remembered nought,
and unmuting restored nought, and the sound could not be brought back from the keyboard at all.
So a press unmutes whenever the output is already down, and the level to go back to is kept in
a file, `dev-mute-level`, beside `dev-keys`, which survives a restart. Failing that, full level.
*/
#include "plugin.hpp"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <map>

namespace px {

struct DevMute {
	bool enabled = false;
	bool keyWas = false;
	double lastFrame = -1.0;
	/** Where each audio module's level is heading, by module id, while a fade is under way. */
	std::map<int64_t, float> target;
	/** Frames of fade left: four, a twentieth of a second or so at the usual frame rate. */
	int fadeLeft = 0;

	static DevMute& get() {
		static DevMute m;
		static bool checked = false;
		if (!checked) {
			m.enabled = system::isFile(asset::user("DreamerMPX/dev-keys"));
			checked = true;
		}
		return m;
	}

	static bool isAudioOutput(engine::Module* m) {
		return m && m->model && m->model->plugin && m->model->plugin->slug == "Core"
			&& m->model->slug.rfind("AudioInterface", 0) == 0 && !m->params.empty();
	}

	static std::string levelFile() {
		return asset::user("DreamerMPX/dev-mute-level");
	}

	static float rememberedLevel() {
		float v = 1.f;
		if (FILE* f = std::fopen(levelFile().c_str(), "r")) {
			if (std::fscanf(f, "%f", &v) != 1)
				v = 1.f;
			std::fclose(f);
		}
		return v > 0.001f ? v : 1.f;
	}

	static void rememberLevel(float v) {
		if (FILE* f = std::fopen(levelFile().c_str(), "w")) {
			std::fprintf(f, "%.4f\n", v);
			std::fclose(f);
		}
	}

	/** Called by every chart's panel each frame; acts once a frame however many there are. */
	void step() {
		if (!enabled || !APP->window || !APP->window->win)
			return;
		const double frame = APP->window->getFrameTime();
		if (frame == lastFrame)
			return;
		lastFrame = frame;

		const bool focused = glfwGetWindowAttrib(APP->window->win, GLFW_FOCUSED) == GLFW_TRUE;
		const bool typing = dynamic_cast<ui::TextField*>(APP->event->getSelectedWidget()) != NULL;
		const bool key = focused && !typing
			&& glfwGetKey(APP->window->win, GLFW_KEY_SPACE) == GLFW_PRESS;
		if (key && !keyWas) {
			// DOWN ALREADY? Then this press brings it back, whatever state anything remembers.
			bool anyUp = false;
			for (int64_t id : APP->engine->getModuleIds()) {
				engine::Module* m = APP->engine->getModule(id);
				if (isAudioOutput(m) && m->params[0].getValue() > 0.001f)
					anyUp = true;
			}
			target.clear();
			const float back = rememberedLevel();
			for (int64_t id : APP->engine->getModuleIds()) {
				engine::Module* m = APP->engine->getModule(id);
				if (!isAudioOutput(m))
					continue;
				if (anyUp) {
					const float now = m->params[0].getValue();
					if (now > 0.001f)
						rememberLevel(now);
					target[id] = 0.f;
				}
				else {
					target[id] = back;
				}
			}
			fadeLeft = 4;
		}
		keyWas = key;

		// THE FADE: a quarter of the remaining distance a frame, then exactly there.
		if (fadeLeft > 0) {
			fadeLeft--;
			for (auto& kv : target) {
				engine::Module* m = APP->engine->getModule(kv.first);
				if (!isAudioOutput(m))
					continue;
				const float now = m->params[0].getValue();
				m->params[0].setValue(fadeLeft == 0 ? kv.second
					: now + (kv.second - now) / (float) (fadeLeft + 1));
			}
		}
	}
};

} // namespace px
