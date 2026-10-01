#pragma once
/** What mpxGuitarVoice does to the sound an oscillator sends back. See docs/guitar-voice.md.

NO RACK IN HERE, for the reason the performer has none: it builds into a command-line program as
well as into the plugin, so an envelope can be measured rather than listened to hopefully.

THE FILTER COMES FIRST, the envelope after: each string's audio goes through a low-pass filter of
its own, whose cutoff follows the string's pitch and the performer's timbre, and only then is it
shaped and placed.

A VOICE IS A STRING. The performer decides when a string is struck, how hard, and when it stops;
this applies those decisions to the audio. Struck, the voice's envelope rises over the attack and
then dies away for as long as the note is held, as a plucked string does — there is no sustain
level. Ended, it falls away over the release. A hammer-on or a legato slide is not struck, so its
envelope carries on from where the string already was while the pitch moves under it.
*/
#include <cstdint>

namespace px {


/** One string's envelope. Times are in seconds. */
struct VoiceEnvelope {
	enum Stage : uint8_t { IDLE, ATTACK, DECAY, RELEASE };

	/** From silence to full. Linear, so a short one is a clean edge. */
	float attack = 0.002f;
	/** How long a held note takes to fall by sixty decibels, a thousandth of its level. */
	float decay = 3.f;
	/** How long a note that has ended takes to fall by sixty decibels. */
	float release = 0.08f;

	Stage stage = IDLE;
	float value = 0.f;

	/** Struck. From wherever the envelope is, so striking a string that is still ringing does
	not click. */
	void strike();
	/** Ended. */
	void end();
	/** Moves on by `dt` seconds and returns the level, nought to one. */
	float step(float dt);
	bool sounding() const { return stage != IDLE; }
};


/** A TWO-POLE LOW-PASS, a state-variable filter in its trapezoidal form: stable at any cutoff
and resonance, and with no delay in its feedback, so changing the cutoff every sample does not
make it ring or blow up. */
struct LowPass {
	float ic1 = 0.f, ic2 = 0.f;

	/** `g` is the cutoff as tan(pi f / rate), `k` the damping: two for none at all, 1.414 for
	a flat response, less for a peak. */
	float process(float in, float g, float k) {
		const float a1 = 1.f / (1.f + g * (g + k));
		const float a2 = g * a1;
		const float a3 = g * a2;
		const float v3 = in - ic2;
		const float v1 = a1 * ic1 + a2 * v3;
		const float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		return v2;
	}

	void reset() { ic1 = ic2 = 0.f; }
};


/** The filter controls, applied to every string. */
struct FilterSettings {
	/** Off, the strings' audio goes straight to their envelopes. */
	bool on = true;
	/** Hertz, for a string at the reference pitch, nought volts, middle C. */
	float cutoff = 20000.f;
	/** Nought, a flat response, to one, a strong peak at the cutoff. */
	float resonance = 0.f;
	/** Two poles, 12 dB an octave, or four, 24. */
	bool steep = false;
	/** How far the cutoff follows the string's pitch: at one, an octave for every octave. */
	float keyTracking = 1.f;
	/** How far the filter envelope moves the cutoff at its peak, in octaves. */
	float envelopeDepth = 2.f;
};


/** The brightness the performer gives an ordinary note, and how many octaves the cutoff moves
for each unit of timbre away from it: a palm mute, at 0.25, is an octave and a half darker. */
static const float TIMBRE_OPEN = 0.6f;
static const float TIMBRE_OCTAVES = 4.f;


/** Every string's filter, envelope, loudness and place, and the mix of them. */
struct GuitarVoices {
	static const int VOICES = 12;

	VoiceEnvelope envelope[VOICES];
	/** EACH STRING'S FILTER ENVELOPE, struck with it: up over its attack, then down over its
	decay whether the note is held or not, which is how a plucked string's brightness goes —
	brightest at the pick, darkening as it rings. A hammer-on does not strike it again. */
	VoiceEnvelope filterEnvelope[VOICES];
	/** The level the performer gives each string, nought to one: what it was struck at, moved
	by the articulations while it sounds. Kept when the note ends, so the release fades from
	the level the note had rather than from nothing. */
	float level[VOICES] = {};
	/** Where each string sits, from -1 at the left to 1 at the right. */
	float pan[VOICES] = {};
	/** Each string's pitch as played, in volts with nought at middle C, and its brightness from
	the performer, nought to one. Both move the string's cutoff. */
	float pitch[VOICES] = {};
	float timbre[VOICES] = {};

	FilterSettings filter;
	LowPass first[VOICES], second[VOICES];

	GuitarVoices() {
		for (int v = 0; v < VOICES; v++)
			timbre[v] = TIMBRE_OPEN;
	}

	/** Where a string's cutoff is, in hertz, before it is held below the top of the band. */
	float cutoffOf(int voice) const;

	/** The envelope controls, applied to every string. */
	void times(float attack, float decay, float release);
	/** The filter envelope's, likewise. Its release is its decay, so a note ending does not
	change how its brightness falls. */
	void filterTimes(float attack, float decay);

	void strike(int voice, float level);
	void setLevel(int voice, float level);
	void end(int voice);
	void silence();

	/** One sample. `in` is the audio coming back for each string, `count` of them; a single
	channel is used for every string. Writes the mix to `left` and `right`. */
	void process(const float* in, int count, float dt, float* left, float* right);
};


} // namespace px
