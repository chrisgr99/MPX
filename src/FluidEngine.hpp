#pragma once
/** A SoundFont synthesiser, wrapped.

NO RACK IN HERE, for the reason the Guitar Pro reader has none: this builds into a command-line
program as well as into the plugin, so a bank can be loaded and a note rendered to a file and
looked at, rather than patched and listened to hopefully.

WHY FLUIDSYNTH. It is the synthesiser the Ultimate Guitar player uses, and the realism of that
player is not in it — it is in what is sent to it. FluidSynth knows note-on, note-off, pitch bend,
control changes and programs, and nothing about a hammer-on or a palm mute. Every articulation is
therefore translated before it arrives here. See docs/guitar-player-spec.md.

ONE SYNTH, ONE BANK, MANY CHANNELS. A bank is thirty megabytes, so one is loaded and shared by
every part: each part gets a block of channels, and one channel per string of a fretted part, so a
bend on one string leaves the others where they are and each string is monophonic as a real one is.

WHAT IS BUILT AND WHAT IS NOT. The library is built without audio drivers, MIDI drivers,
libsndfile, LADSPA, readline, networking and its own threads — none of which a module inside a
host has any business owning. What is left is the synthesis, which is all that is wanted.
*/
#include <string>
#include <vector>

// FluidSynth's own names for these, so the header is not included here: they are typedefs of
// structs it declares, and declaring them again would clash.
struct _fluid_hashtable_t;
struct _fluid_synth_t;

namespace px {


/** One sound the bank holds, as the bank itself names it. */
struct FluidPreset {
	int bank = 0;
	int program = 0;
	std::string name;
	bool percussion = false;    /**< Bank 128, a drum kit rather than an instrument. */
};


struct FluidEngine {
	FluidEngine();
	~FluidEngine();

	/** Makes the synthesiser, at the host's sample rate. Called again when the rate changes;
	the bank then has to be loaded again. Main thread. */
	bool start(double sampleRate, int channels, int polyphony);
	void stop();
	bool running() const { return synth != NULL; }

	/** Reads a SoundFont. Slow — thirty megabytes of samples — so never from the audio thread.
	Returns false and fills `reason`. */
	bool loadBank(const std::string& path);
	const std::string& reason() const { return why; }
	bool loaded() const { return font >= 0; }
	const std::string& bankPath() const { return path; }
	const std::string& bankName() const { return name; }
	/** Every sound in the bank, in the bank's own order. Read on the main thread only. */
	const std::vector<FluidPreset>& presets() const { return sounds; }

	// ---- the audio thread ----------------------------------------------------------------------

	void program(int channel, int bank, int program);
	/** How far a full bend reaches, in semitones. Twelve, so a two-tone bend and a long slide
	both resolve smoothly through fourteen bits rather than in steps. */
	void bendRange(int channel, int semitones);
	void noteOn(int channel, int key, int velocity);
	void noteOff(int channel, int key);
	/** Cents from the note, positive or negative, within the bend range. */
	void bend(int channel, float cents);
	void controller(int channel, int cc, int value);
	void allOff();
	/** Reverb and chorus, FluidSynth's own, as the Ultimate Guitar player leaves them on. */
	void effects(bool reverb, bool chorus);

	/** Writes `frames` of stereo into two buffers. */
	void render(float* left, float* right, int frames);

private:
	_fluid_hashtable_t* settings = NULL;
	_fluid_synth_t* synth = NULL;
	int font = -1;
	std::string path, name, why;
	/** What each channel's bend range was set to. FluidSynth has no way to ask. */
	std::vector<int> ranges;
	std::vector<FluidPreset> sounds;

	void readPresets();
};


} // namespace px
