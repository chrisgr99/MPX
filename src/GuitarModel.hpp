#pragma once
/** The strings of a modelled guitar, and what the hands do to them. See docs/guitar-model.md.

NO RACK IN HERE, as with GuitarString: a command-line test plays each articulation and measures it.

THE PERFORMER DECIDES, THE STRINGS ACT. Which string, when, how hard, how long, and where its pitch
goes are the performer's; this turns each of those into what a hand does to a string. A pick is
a pluck. A hammer-on is a finger landing on a string that is already ringing, so the loop gets
shorter and only a small knock goes in. A palm mute is the side of the hand resting on the
strings by the bridge, so the string loses its energy, its top first, many times faster. A dead
note is a string held against the frets, so almost nothing rings but the pick. A harmonic is a
finger touching a node, so the string sounds the partial that has a node there. A note ending is
the hand coming down on the string.
*/
#include "GuitarString.hpp"

#include <cstdint>

namespace px {


/** The technique bits the strings act on. The same values as the cable's (NoteBus.hpp), copied
so that this needs nothing from Rack. */
enum GuitarTechnique : uint32_t {
	GT_HAMMER_ON = 1u << 0,
	GT_PULL_OFF = 1u << 1,
	GT_LEGATO_SLIDE = 1u << 2,
	GT_SHIFT_SLIDE = 1u << 3,
	GT_SLIDE_IN_BELOW = 1u << 4,
	GT_SLIDE_IN_ABOVE = 1u << 5,
	GT_SLIDE_OUT_DOWN = 1u << 6,
	GT_SLIDE_OUT_UP = 1u << 7,
	GT_PALM_MUTE = 1u << 9,
	GT_DEAD_NOTE = 1u << 10,
	GT_HARMONIC = 1u << 14,
	GT_ARTIFICIAL_HARMONIC = 1u << 15,
	GT_TAPPED = 1u << 16,
};


/** AN ACOUSTIC GUITAR'S BODY, as a handful of resonances. The strings alone are thin — a string
moves almost no air — and what is heard from an acoustic is the body the bridge drives: the air in
it breathing through the soundhole at about a hundred hertz, the top plate's first modes a little
above, and a scatter of plate and back modes up through the low kilohertz, each quieter and
narrower than the one below. Each is a band-pass of its own, all fed by the strings at once and
added to what goes straight through. */
struct GuitarBody {
	static const int MODES = 10;

	/** A resonance's own filter: the state-variable form, read at its band-pass, normalised so
	a sine at its centre comes out at the mode's gain. */
	struct Mode {
		float hz = 100.f, q = 10.f, gain = 1.f;
		float ic1 = 0.f, ic2 = 0.f;
		float g = 0.f, k = 0.1f, a1 = 1.f, a2 = 0.f, a3 = 0.f;
	};
	Mode modes[MODES];
	float rate = 0.f;
	/** THE BODY RADIATES ITS LOW AND MIDDLE RANGE AND HARDLY ITS HIGHS: a two-pole low-pass on
	what the body gives, its corner lower for a nylon-strung classical than for a steel-string. */
	float radiationHz = 0.f;
	float radIc1 = 0.f, radIc2 = 0.f, radA1 = 1.f, radA2 = 0.f, radA3 = 0.f, radK = 1.414f;

	GuitarBody();
	void setSampleRate(float rate);
	void setRadiation(float hz);
	/** `amount` nought is the bare strings, one the full body. */
	float process(float in, float amount);
	void reset();
};


/** A TWO-POLE FILTER of the state-variable kind, for the electric's fixed tone shaping: read as a
low-pass or a high-pass. */
struct ToneFilter {
	float ic1 = 0.f, ic2 = 0.f;
	float g = 0.f, k = 1.414f, a1 = 1.f, a2 = 0.f, a3 = 0.f;
	void set(float hz, float q, float rate);
	float lowpass(float in);
	float highpass(float in);
	/** Band-pass with its peak at one. */
	float bandpass(float in);
	void reset() { ic1 = ic2 = 0.f; }
};


/** The amplifier's own controls. */
struct AmpSettings {
	/** How hard the strings push the saturation, nought to one: from nearly clean to broken up. */
	float drive = 0.35f;
	/** THE TONE STACK, in decibels: a shelf below about 100 Hz, a dip or a lift around 500 Hz,
	and a shelf above about 3 kHz. The scooped middle and lifted ends are a clean amplifier's
	voice, and most of what makes an electric guitar sound like one. */
	float bass = 3.f, middle = -6.f, treble = 4.f;
};


/** AN ELECTRIC GUITAR'S AMPLIFIER, after its pickup: a soft saturation, nearly clean when the
strings are played lightly and breaking up when they are played hard or driven, then the tone
stack, then a speaker's roll-off above about 5 kHz and below about 70 Hz. */
struct GuitarAmp {
	ToneFilter speakerLow, speakerHigh, bassShelf, middlePeak, trebleShelf;
	float rate = 0.f;
	void setSampleRate(float rate);
	float process(float in, const AmpSettings& settings);
	void reset();
};


/** The settings every string shares, set by the panel. */
struct GuitarSettings {
	/** Where the strings are picked, as a fraction of their length from the bridge. */
	float pickPosition = 0.15f;
	/** The pick, nought a fingertip to one a hard plectrum. */
	float hardness = 0.6f;
	/** How long an open string rings, in seconds to fall 60 dB. */
	float sustain = 5.f;
	/** How much faster the upper partials die. */
	float damping = 0.35f;
	/** On the acoustic, how much of the body is heard, nought to one. On the electric, where the
	pickup is: nought at the bridge, one at the neck. */
	float body = 0.8f;
	/** Electric rather than acoustic: a pickup and an amplifier in place of the body. */
	bool electric = false;
	/** A NYLON-STRUNG CLASSICAL rather than a steel-string: strings far less stiff that lose
	their highs much sooner, a softer touch, and a body that radiates less of the top. Only for
	the acoustic; the electric's strings are steel. */
	bool nylon = false;
	/** How stiff the strings are, nought to one: at one the eighth partial is six cents sharp. */
	float stiffness = 0.7f;
	/** HOW MUCH STRINGS NOBODY IS PLAYING RING IN SYMPATHY with the ones that are: nought is
	none, a half is an ordinary acoustic, one twice that. */
	float sympathy = 0.5f;
	/** HOW LOUD A FINGER SLIDING ALONG A WOUND STRING IS: nought is silent, one the loudest. */
	float fingerNoise = 0.5f;
	/** HOW MUCH SHORTER A HIGH NOTE RINGS THAN A LOW ONE, nought to one. At nought every note
	rings as long as `sustain`; above it `sustain` is the open low E's ring, and each octave
	above that rings shorter, at one to 0.44 of the octave below. */
	float taper = 0.f;
	/** HOW MUCH A FRETTING FINGER TAKES FROM A NOTE, nought to one: a fingertip stops the string
	more softly than a fret or the nut, and takes energy and the upper partials with it. At one a
	fretted note rings half as long as the open string would and is noticeably duller; at nought
	alike. A note whose fret is not known is taken as open. */
	float fretDamping = 0.f;
	/** THE GUITAR'S TONE KNOB, on the electric: one is open, nought a dark, rolled-off tone. */
	float tone = 1.f;
	/** The amplifier's controls. */
	AmpSettings amp;
};


struct GuitarModel {
	static const int STRINGS = 12;

	GuitarSettings settings;
	GuitarString strings[STRINGS];
	/** Where each string sits, -1 left to 1 right. */
	float pan[STRINGS] = {};
	/** The open pitch of each string, in volts with nought at middle C, for harmonics. */
	float open[STRINGS] = {};
	/** Whether `open` has been set for a string. */
	bool openKnown[STRINGS] = {};

	void setSampleRate(float rate);

	/** A string picked. `level` nought to one; `technique` the cable's bits; `fret` where it is
	stopped, or -1. */
	void strike(int string, float level, uint32_t technique, int fret);
	/** A note the left hand sounds without a pick: a hammer-on, a pull-off or a tap onto a
	string that is ringing. The pitch moves through setPitch; this is the knock. */
	void legato(int string, float level, uint32_t technique);
	/** A level the performer gives a string that is sounding: the output follows it, relative
	to what it was struck at, which is how a tremolo-picked or faded note comes through. */
	void setLevel(int string, float level);
	/** The hand comes down on the string. */
	void end(int string);
	/** Where the string's pitch is, in volts with nought at middle C: the fret, the bend, the
	slide and the vibrato, as the performer has them. A harmonic sounds above it by however
	far its node puts it. */
	void setPitch(int string, float volts);
	void silence();

	/** One sample of the whole guitar, left and right. */
	void process(float* left, float* right);

	/** Which partial a harmonic touched at this fret sounds: two at the twelfth, three at the
	seventh and the nineteenth, four at the fifth. Nought where no node is near. */
	static int harmonicAt(int fret);

private:
	float strikeLevel[STRINGS] = {};
	float level[STRINGS] = {};
	/** Volts above the stopped pitch that a harmonic sounds. */
	float offset[STRINGS] = {};
	float last[STRINGS] = {};
	GuitarBody bodyLeft, bodyRight;

	/** THE PICKUP, one for each string: what the string was doing, kept long enough to hear it at
	a point along its length; and the coil's resonance. */
	static const int PICKUP_HISTORY = 4096;
	float history[STRINGS][PICKUP_HISTORY] = {};
	int historyAt = 0;
	ToneFilter coil[STRINGS];
	GuitarAmp ampLeft, ampRight;
	/** The guitar's tone knob: two one-pole low-passes on each side, before the amplifier. */
	float toneLeft[2] = {}, toneRight[2] = {};

	float sampleTime = 1.f / 48000.f;
	/** SYMPATHY. A string is FREE once no hand has been on it for a while: open, undamped, and
	ringing only with what comes to it through the bridge from the strings being played. */
	bool free[STRINGS] = {};
	float idle[STRINGS] = {};
	bool ended[STRINGS] = {};

	/** FINGER NOISE. Whether a string's pitch is moving under a sliding finger, and whether the
	note after this one is reached by a slide; the squeak's level, and its noise and filter. */
	bool sliding[STRINGS] = {};
	bool slideNext[STRINGS] = {};
	float squeak[STRINGS] = {};
	float squeakIc1[STRINGS] = {}, squeakIc2[STRINGS] = {};
	uint32_t noise = 77777u;

	void touch(int string, uint32_t technique);
	/** The strings' settings for the guitar being played: nylon's differ from steel's. */
	float stringDamping() const;
	/** How long a note at `volts` rings, in seconds to fall 60 dB, by the taper, and how much its
	upper partials are damped, by the fret it is stopped at. */
	float ringTime(float volts, int fret) const;
	float ringDamping(int fret) const;
	float stringStiffness() const;
	float stringHardness() const;
	void setFree(int string, bool on);
	int openCount() const;
};


} // namespace px
