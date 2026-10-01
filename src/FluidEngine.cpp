/** See FluidEngine.hpp. */
#include "FluidEngine.hpp"

#include <fluidsynth.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

/** WHAT FLUIDSYNTH SAYS, AND WHAT IS WORTH REPEATING. Built without its own file helpers, it
complains once per load that they are unavailable and then carries on correctly. Those lines are
dropped; anything else is passed on, so a real fault still reaches the host's log. */
void logTo(int level, const char* message, void* data) {
	(void) level;
	(void) data;
	if (!message || std::strstr(message, "is unavailable"))
		return;
	std::fprintf(stderr, "fluidsynth: %s\n", message);
}

} // namespace


namespace px {


FluidEngine::FluidEngine() {}

FluidEngine::~FluidEngine() {
	stop();
}


bool FluidEngine::start(double sampleRate, int channels, int polyphony, int groups) {
	stop();
	settings = new_fluid_settings();
	if (!settings) {
		why = "the synthesiser could not be set up";
		return false;
	}
	// Only the three that mean something is wrong. Setting one for the debug and information
	// levels would turn those on, and they report every chunk of every bank it reads.
	fluid_set_log_function(FLUID_PANIC, logTo, NULL);
	fluid_set_log_function(FLUID_ERR, logTo, NULL);
	fluid_set_log_function(FLUID_WARN, logTo, NULL);
	fluid_settings_setnum(settings, "synth.sample-rate", sampleRate);
	fluid_settings_setint(settings, "synth.midi-channels", channels);
	fluid_settings_setint(settings, "synth.polyphony", polyphony);
	groupCount = (groups > 0) ? groups : 1;
	fluid_settings_setint(settings, "synth.audio-channels", groupCount);
	fluid_settings_setint(settings, "synth.audio-groups", groupCount);
	fluid_settings_setint(settings, "synth.effects-groups", 1);
	// THE GAIN IS LEFT WHERE IT IS. FluidSynth's default is quiet on purpose, since a General
	// MIDI bank has to hold a whole orchestra without clipping; the module's own level makes up
	// for it where it is wanted, which is one place rather than two.
	synth = new_fluid_synth(settings);
	if (!synth) {
		delete_fluid_settings(settings);
		settings = NULL;
		why = "the synthesiser could not be started";
		return false;
	}
	font = -1;
	sounds.clear();
	ranges.assign((size_t) ((channels > 0) ? channels : 1), 2);
	return true;
}


void FluidEngine::stop() {
	if (synth)
		delete_fluid_synth(synth);
	if (settings)
		delete_fluid_settings(settings);
	synth = NULL;
	settings = NULL;
	font = -1;
	sounds.clear();
}


bool FluidEngine::loadBank(const std::string& file) {
	if (!synth) {
		why = "the synthesiser is not running";
		return false;
	}
	const int id = fluid_synth_sfload(synth, file.c_str(), 1);
	if (id == FLUID_FAILED) {
		why = "that file could not be read as a SoundFont";
		return false;
	}
	if (font >= 0)
		fluid_synth_sfunload(synth, font, 1);
	font = id;
	path = file;
	const size_t slash = file.find_last_of('/');
	name = (slash == std::string::npos) ? file : file.substr(slash + 1);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);
	readPresets();
	why.clear();
	return true;
}


void FluidEngine::readPresets() {
	sounds.clear();
	if (!synth || font < 0)
		return;
	fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(synth, font);
	if (!sf)
		return;
	// THE BANK'S OWN NAMES, not a table of General MIDI ones. A user's own SoundFont may hold
	// anything at all, and what it calls its sounds is what should be offered.
	fluid_sfont_iteration_start(sf);
	for (fluid_preset_t* preset = fluid_sfont_iteration_next(sf); preset;
			preset = fluid_sfont_iteration_next(sf)) {
		FluidPreset p;
		p.bank = fluid_preset_get_banknum(preset);
		p.program = fluid_preset_get_num(preset);
		const char* n = fluid_preset_get_name(preset);
		p.name = n ? n : "";
		p.percussion = (p.bank == 128);
		sounds.push_back(p);
	}
}


void FluidEngine::program(int channel, int bank, int number) {
	if (!synth || font < 0)
		return;
	fluid_synth_program_select(synth, channel, font, (unsigned int) bank, (unsigned int) number);
}


void FluidEngine::bendRange(int channel, int semitones) {
	if (!synth)
		return;
	fluid_synth_pitch_wheel_sens(synth, channel, semitones);
	if (channel >= 0 && channel < (int) ranges.size())
		ranges[(size_t) channel] = semitones;
}


void FluidEngine::noteOn(int channel, int key, int velocity) {
	if (synth)
		fluid_synth_noteon(synth, channel, key, velocity);
}


void FluidEngine::noteOff(int channel, int key) {
	if (synth)
		fluid_synth_noteoff(synth, channel, key);
}


void FluidEngine::bend(int channel, float cents) {
	if (!synth)
		return;
	// Fourteen bits, centred, over the range set above. Clamped rather than wrapped: a bend
	// beyond the range is as far as the range goes, not a leap to the other side of it.
	const int range = (channel >= 0 && channel < (int) ranges.size())
		? ranges[(size_t) channel] : 2;
	const float span = (float) range * 100.f;
	float v = (span > 0.f) ? cents / span : 0.f;
	if (v > 1.f) v = 1.f;
	if (v < -1.f) v = -1.f;
	fluid_synth_pitch_bend(synth, channel, 8192 + (int) (8191.f * v));
}


void FluidEngine::controller(int channel, int cc, int value) {
	if (synth)
		fluid_synth_cc(synth, channel, cc, value);
}


void FluidEngine::allOff() {
	if (synth)
		fluid_synth_system_reset(synth);
}


void FluidEngine::effects(bool reverb, bool chorus) {
	if (!synth)
		return;
	fluid_synth_reverb_on(synth, -1, reverb ? 1 : 0);
	fluid_synth_chorus_on(synth, -1, chorus ? 1 : 0);
}


void FluidEngine::render(float* left, float* right, int frames) {
	if (!synth || frames <= 0)
		return;
	fluid_synth_write_float(synth, frames, left, 0, 1, right, 0, 1);
}


void FluidEngine::renderGroups(float** out, float** fx, int frames) {
	if (frames <= 0)
		return;
	for (int i = 0; i < 2 * groupCount; i++)
		std::memset(out[i], 0, sizeof(float) * (size_t) frames);
	for (int i = 0; i < 4; i++)
		std::memset(fx[i], 0, sizeof(float) * (size_t) frames);
	if (!synth)
		return;
	fluid_synth_process(synth, frames, 4, fx, 2 * groupCount, out);
}


} // namespace px
