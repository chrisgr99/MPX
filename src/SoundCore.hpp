#pragma once
/** THE BAND ITSELF, shared by mpxFluidSynth, mpxGuitarChart6 and mpxGuitarChartExpander.

Both modules are the same synthesiser with different ways in: mpxFluidSynth takes its parts from
cables and lets each part's sound be chosen, and the expander takes the twelve rows of the Guitar
Chart beside it and plays each track on the instrument its file names. Everything between the
cable and the sound — the bank, the performers, the channels a string each, the rendering — is
here, once, so a fix made for one is made for both.

EVERY PART HAS AN OUTPUT OF ITS OWN, and with voiceOutputs every voice of every part does.
FluidSynth sends a MIDI channel to audio group channel % groups, and a part's channels are its
number plus a multiple of the part count. With one group a part, the groups are the parts; with
one group a channel, each voice — a string, or a held note — has its own output, which is what a
polyphonic cable carries. FluidSynth allows 128 groups, so a part then has ten voices rather than
twelve. The effects are one unit shared by all the parts; their return is mixed into the whole
band only.
*/
#include "plugin.hpp"
#include "FluidEngine.hpp"
#include "NoteBus.hpp"
#include "Perform.hpp"

#include <atomic>
#include <functional>
#include <thread>

namespace px {


/** FROM THE SYNTHESISER'S OUTPUT TO RACK'S VOLTS, for every module that plays through this core.

A General MIDI bank is quiet on purpose, so that a whole orchestra fits in it without clipping,
and FluidSynth's own gain is a fifth on top of that. Scaled by five, as it first was, a guitar note
at a moderate dynamic peaked at about 0.15 V — some twenty decibels under mpxGuitar's for the same
note. Sixty-five brought the two together, and this brings them both to MPX_FULL_VOLTS, which is
what a level of one is defined to be worth.

ONE FIGURE FOR A WHOLE BANK, which cannot be exact. The programs inside a SoundFont are recorded
at whatever level their samples happened to have: at this scale the grand piano lands two decibels
under the standard, the steel guitar on it, and the nylon guitar two over. That spread is the
bank's and not something a constant here can take out. `make leveltest` measures all three. */
static const float SOUND_VOLTS = 98.f;


/** How many notes of one instant are taken as a chord. See mpxShare. */
static const int SOUND_TOGETHER = 16;


struct SoundCore {
	static const int MAX_PARTS = 12;
	/** Channels per part: one per voice the performer holds, which is one per string of a
	twelve string and enough for anything unfretted. */
	static const int VOICES = PERFORM_VOICES;
	/** How many frames are rendered at a time. About one and a third milliseconds at 48k, which
	is finer than the ear places an onset, and a power of two. */
	static const int BLOCK = 64;

	int parts = 6;
	/** Voices a part plays, and so channels it has. VOICES unless there is an output a voice;
	see setParts. */
	int voices = VOICES;
	/** Whether each voice renders to an output of its own. */
	bool voiceOutputs = false;
	/** The most separate outputs FluidSynth will render. */
	static const int MAX_GROUPS = 128;
	/** Who is speaking, in the log. */
	std::string logName = "mpxFluidSynth";

	FluidEngine engine;
	/** The bank is read on a worker, since thirty megabytes of samples is not something to do
	between two audio callbacks. Nothing sounds while it is loading. */
	std::atomic<bool> busy{false};
	std::atomic<bool> haveBank{false};
	std::atomic<bool> failed{false};
	std::thread worker;
	std::string wantBank;
	std::string message;
	/** Called on the worker once a bank has been read, before it is played. */
	std::function<void()> onBankRead;

	/** WHETHER A PART TAKES ITS SOUND FROM ITS CABLE. The expander does: each track plays the
	General MIDI program its file names. mpxFluidSynth does not; its sounds are chosen. */
	bool followInstrument = false;

	BusReader reader[MAX_PARTS];
	std::atomic<bool> relink{false};
	std::atomic<int> wantCount[MAX_PARTS];
	std::atomic<int> wantSlots[MAX_PARTS][MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_PARTS][MAX_UPSTREAM];

	/** ONE PERFORMER PER PART, because the articulations are per instrument: a string is a
	voice, and two parts have their own strings. The same library mpxGuitarist reads, so the two
	modules cannot disagree about what a hammer-on is. */
	Performer performer[MAX_PARTS];
	Instrument instrument[MAX_PARTS];
	uint32_t instrumentChange[MAX_PARTS] = {};
	PerformRules loadedRules;
	std::atomic<bool> rulesReady{false};
	/** How recently each part played, for a lamp: one at a note, falling to nought. */
	float active[MAX_PARTS] = {};
	bool attachedWas[MAX_PARTS] = {};

	float rate = 44100.f;
	bool reverbOn = true, chorusOn = true;

	float dry[MAX_GROUPS * 2][BLOCK] = {};
	float wet[4][BLOCK] = {};
	float* dryAt[MAX_GROUPS * 2] = {};
	float* wetAt[4] = {};
	int blockAt = BLOCK;

	SoundCore();
	~SoundCore();

	/** How many parts, and whether each voice has an output, before the engine is started.
	Main thread. */
	void setParts(int count, bool outputPerVoice = false);
	int channels() const { return ((parts * voices + 15) / 16) * 16; }
	/** The MIDI channel a part's voice plays on. A voice beyond the part's count shares the
	last. */
	int channel(int part, int voice) const {
		return ((voice < voices) ? voice : voices - 1) * parts + part;
	}
	int groups() const { return voiceOutputs ? parts * voices : parts; }

	void link(int part, const int* slots, const uint32_t* generations, int n);

	// ---- the bank, on the main thread -----------------------------------------------------------

	void loadBank(const std::string& path);
	/** Makes the synthesiser and reads a bank: `preferred` if one is named, otherwise the one it
	had, otherwise the first in the banks folder. */
	/** WHETHER THE SYNTHESISER MAY BE TOUCHED, and whether it is being touched.
	
	THESE EXIST BECAUSE THE SYNTHESISER IS MADE AND UNMADE ON THE MAIN THREAD WHILE THE AUDIO
	THREAD IS PLAYING IT. The band starts when anything can hear it and stops when nothing can,
	and whether anything can hear it changes the moment a cable goes into a jack — so the decision
	is taken by the widget, every frame, on the main thread. Rack holds its engine locked while it
	calls a module's onAdd, onRemove or onSampleRateChange, so those are safe; a widget's step is
	none of those and is not.
	
	WHAT HAPPENED WITHOUT THEM. Plugging a cable into mpxScore's mix output while the band was
	sounding called stopEngine from the widget, which called delete_fluid_synth, while the audio
	thread was inside fluid_synth_process on that same synthesiser. The audio thread then read the
	freed object, found nothing where its event handler had been, and died reaching through it:
	EXC_BAD_ACCESS at 0x30 in fluid_rvoice_mixer_get_bufs. It took Rack down with it.
	
	HOW THEY WORK. `usable` is permission and `rendering` is occupancy, and the order of the two
	is what makes it correct. The audio thread claims occupancy FIRST and then asks permission,
	giving the claim up again if it is refused; the main thread withdraws permission FIRST and then
	waits for the occupancy to clear. Whichever gets there first, the other sees it: there is no
	moment at which the main thread can believe the audio thread has let go while it is still
	holding on. The wait is a spin, and it is microseconds — the audio thread's turn through here
	is one sample's worth of work. */
	std::atomic<bool> usable{false};
	std::atomic<bool> rendering{false};

	/** Takes the synthesiser out of the audio thread's hands and waits for it to let go. Main
	thread only: it waits, and waiting is the one thing an audio callback may never do. */
	void holdOff();

	void startEngine(float sampleRate, bool reverb, bool chorus,
		const std::string& preferred = "");
	void stopEngine();
	/** Every one of a part's channels, so each string plays the same instrument. */
	void setProgram(int part, int bank, int program);
	/** Reads the performance rules, the same file mpxGuitarist reads. */
	void readRules();

	static std::string bankFolder();
	/** The first SoundFont in the banks folder, or nothing. */
	static std::string firstBank();

	// ---- the audio thread -----------------------------------------------------------------------

	bool playing() const { return haveBank.load() && !busy.load(); }
	/** Takes the notes, plays them, and moves the audio on by one frame. `muted` may be null. */
	void step(float sampleTime, const bool* muted, float humanise, bool reverb, bool chorus);
	/** A part's sound, every voice of it. */
	float partLeft(int part) const;
	float partRight(int part) const;
	/** One voice's sound, when each voice has an output. */
	float voiceLeft(int part, int voice) const {
		return dry[2 * channel(part, voice)][blockAt - 1];
	}
	float voiceRight(int part, int voice) const {
		return dry[2 * channel(part, voice) + 1][blockAt - 1];
	}
	float wetLeft() const { return wet[0][blockAt - 1] + wet[2][blockAt - 1]; }
	float wetRight() const { return wet[1][blockAt - 1] + wet[3][blockAt - 1]; }

private:
	void followProgram(int part, const Instrument& in);
};


} // namespace px
