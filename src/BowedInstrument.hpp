#pragma once
/** A BOWED INSTRUMENT: four strings on one bridge, on one body. Phase 6 of docs/bowed-strings.md.

THE STRINGS SHARE A BRIDGE THAT MOVES. Every string pushes on it; it moves as the body's modes
let it; and its motion goes back down every string nobody is bowing. So a string nobody is playing
hears the ones that are, and rings where its own partials meet theirs — the open D under a D played
on the G string. The string being bowed plays against a still bridge: on a real instrument it feels
the bridge too, and that can be switched on (setBowedFeelsBridge), but by ear the cello's low notes
sounded better without it. The push and the motion depend on each other in the same sample,
which is solved rather than broken with a delay, as the deprecated piano's soundboard was: each
string says what it would push on a still bridge, the body says how fast the bridge moves per newton
and how fast it would move unpushed, and one line gives the motion.

THE BRIDGE THE STRINGS FEEL is the body's low modes, where they are few and each one counts: the
signature modes with their measured mobility. The dense modes above are left to the strings' own
losses, which were set to what strings on instruments measure; put in the loop as well they would be
counted twice.

WHAT IS HEARD is the whole body's radiation, driven by the total force on the bridge: the voice of
phase 5, at the engine's rate.

ONE RATE FOR THE WHOLE INSTRUMENT, the engine's times a factor (BowedString::oversampling, for the
highest note it will play), because strings that share a bridge sample by sample have to share the
sample. The force on the bridge is brought down to the engine's rate for the body's voice.

WHICH STRING. A note is played on the string that puts it lowest on the fingerboard: the highest
string tuned at or below it. A finger stops a string at the note's pitch, shortening it, so a bow at
the same distance from the bridge is a larger fraction of what is left.

NO RACK IN HERE, so a bench can measure it. */
#include "BowedBody.hpp"
#include "BowedString.hpp"
#include "Decimator.hpp"

#include <vector>

namespace px {


struct BowedInstrument {
	/** ONE STRING: its open pitch and its wave impedance, kilograms a second. */
	struct StringSpec {
		float openHz;
		float impedance;
	};

	/** EVERYTHING ABOUT ONE INSTRUMENT, assembled field by field. */
	struct Design {
		/** Lowest first. */
		std::vector<StringSpec> strings;
		/** The vibrating length of an open string, bridge to nut, metres. */
		float length = 0.328f;
		/** THE BODY'S VOICE, from the force on the bridge to the sound, at the engine's rate. */
		BowedBody::Design voice;
		/** THE BRIDGE AS THE STRINGS FEEL IT: the low modes that move it, with their mobility. */
		BowedBody::Design bridge;
		/** THE FRICTION. CURVE is the friction curve of phases 2 to 6. HEAT follows the rosin's
		temperature alone (BowedString::setThermal), with the contact a bow's rather than a rod's: chosen
		by ear in phase 7, since under the friction curve no player could make the bow changes of a real
		part clean and under this one the Fauré quartet's violin part played without a squeak — but it
		lets the string go softly, and quiet notes sound muffled. ROSIN is the thermal elasto-plastic
		model, which shears before it slides as well as softening as it warms (BowedString::setRosin). */
		enum Friction : uint8_t { CURVE, HEAT, ROSIN };
		Friction friction = HEAT;
		BowedString::Thermal heat;
		BowedString::Rosin rosin;
	};

	/** THE VIOLIN AND THE CELLO, from the measurements in research/bowed-strings/. The cello's body
	is provisional; see docs/bowed-strings.md, phase 5. */
	static Design violin();
	static Design viola();
	static Design cello();

	void setDesign(const Design& d);
	const Design& design() const { return wanted; }
	/** The engine's rate, and how many times faster the strings run. */
	void setRate(float engineRate, int oversampling);
	/** How many times faster than the engine the strings run. */
	int oversampling() const { return factor; }
	/** `n` SAMPLES NOT STEPPED, the instrument at rest: what the strings owe for them. */
	void idle(int64_t n) {
		for (BowedString& s : strs)
			s.idle(n);
	}
	/** THE STRINGS AND THE BRIDGE MOVING TOGETHER, or each string against a still bridge. */
	void setSympathetic(bool on) { sympathetic = on; }
	/** A STRING NOBODY IS PLAYING STOPS once its ringing has stayed under QUIET_STRING for a tenth of a
	second: not stepped, and so neither ringing on nor taking up the bridge's motion, until it is
	bowed, stopped or plucked again. Less computing for less sympathy — a string at rest no longer
	rings with the others. Off by default. */
	void setQuietStringsRest(bool on);
	/** WHETHER A STRING BEING BOWED FEELS THE BRIDGE MOVE. On a real instrument it does. Off, which
	is how it plays: the bowed string plays against a still bridge while it pushes the bridge as
	before, and the strings nobody is playing ring with it. Chosen by ear on the cello's low notes,
	which sounded rougher with the bridge moving under the bow; the free strings' sympathy was kept.
	On, the model makes no wolf of the cello's F3 with the provisional body: see the doc, phase 6. */
	void setBowedFeelsBridge(bool on) { bowedFeels = on; }

	int strings() const { return (int) strs.size(); }
	/** The string a note is played on: the highest one tuned at or below it, or the lowest. */
	int stringFor(float hz) const;

	/** A FINGER DOWN on `string` at the pitch `hz`, or, at the string's own pitch, none. */
	void stop(int string, float hz);
	/** THE OLD NOTE LINGERING, at a slurred change of a large interval: a copy of `string` as it is now,
	the bow off it, heard fading out over `seconds` while the string itself is heard fading in. Every
	harmonic of a note switched at once to the next note's is heard, dry, as a blip of pink noise — an
	ideal tone switched so has it — and an old note fading under the new one, as the body and the room
	let it, has not. What is heard only: the string's motion and its push on the bridge, which the other
	strings feel, are its own. */
	void linger(int string, float seconds);
	/** THE BOW ON `string`: how hard, newtons; how fast, metres a second; and where, metres from
	the bridge. Force nought lifts it. */
	void bow(int string, float force, float speed, float fromBridge);
	/** A string plucked where the bow would be. */
	void pluck(int string, float level, float fromBridge);

	/** ONE SAMPLE AT THE ENGINE'S RATE: the sound a metre away, pascals. */
	float process();
	/** ONE SAMPLE AT THE STRINGS' RATE, for a player who moves the bow that finely: true, and the
	sound in `*sound`, when an engine sample is ready. */
	bool step(float* sound);

	BowedString& string(int i) { return strs[(size_t) i]; }
	float bridgeVelocity() const { return (float) lastBridge; }
	/** The total force on the bridge at the strings' rate, last sample, newtons. */
	float bridgeForce() const { return (float) lastForce; }
	/** How strongly the body radiates a force on the bridge at a frequency: see BowedBody::radiativityAt. */
	float radiativityAt(float hz) const { return voice.radiativityAt(hz); }

private:
	Design wanted;
	std::vector<BowedString> strs;
	std::vector<float> stoppedAt;
	BowedBody voice, coupler;
	Decimator down;
	float engine = 48000.f;
	int factor = 1;
	bool sympathetic = true;
	bool bowedFeels = false;
	std::vector<char> bowing;
	/** WHAT BOW EACH STRING WAS LAST GIVEN, so the same again is not worked out again: the force, the
	speed and the distance from the bridge. Not a number until given, and the distance made not a number
	again when the finger moves, since the share of the string it is depends on where the finger is. */
	std::vector<float> givenForce, givenSpeed, givenFrom;
	/** The strings resting (see setQuietStringsRest): whether each is, how loud it has been lately,
	how long it has been quiet, and how many samples it has rested, owed to its rosin's cooling. */
	bool quietRest = false;
	std::vector<char> resting;
	std::vector<float> lately;
	std::vector<int64_t> quietFor, restedFor;
	float decayLately = 0.9999f;
	void wake(size_t i);
	double lastBridge = 0.0, lastForce = 0.0;
	float lastSound = 0.f;
	/** The lingering copy (see linger), held apart and only while it lingers — a string's lines are some
	two hundred kilobytes, and the instrument is copied on the stack for every trial — which string it is
	of, and how many samples it has left of how many. */
	std::vector<BowedString> ghost;
	int ghostString = -1;
	int64_t ghostLeft = 0, ghostLength = 1;

	void tuneString(int i, float hz);
};


} // namespace px
