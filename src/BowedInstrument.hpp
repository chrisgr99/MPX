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
	};

	/** THE VIOLIN AND THE CELLO, from the measurements in research/bowed-strings/. The cello's body
	is provisional; see docs/bowed-strings.md, phase 5. */
	static Design violin();
	static Design cello();

	void setDesign(const Design& d);
	const Design& design() const { return wanted; }
	/** The engine's rate, and how many times faster the strings run. */
	void setRate(float engineRate, int oversampling);
	/** THE STRINGS AND THE BRIDGE MOVING TOGETHER, or each string against a still bridge. */
	void setSympathetic(bool on) { sympathetic = on; }
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
	double lastBridge = 0.0, lastForce = 0.0;
	float lastSound = 0.f;

	void tuneString(int i, float hz);
};


} // namespace px
