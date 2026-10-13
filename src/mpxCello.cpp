/** mpxCello — an MPX part played on a physically modelled cello. Everything that plays is in
BowedModule.hpp; this is which instrument it is, and its controls with their help. See
docs/bowed-strings.md. */
#include "BowedModule.hpp"

namespace px {


static BowedKind celloKind() {
	BowedKind k;
	k.slug = "mpxCello";
	k.design = BowedInstrument::cello();
	k.habits = BowedPlayer::celloHabits();
	// AS LOUD AS mpxPiano PLAYING THE SAME MARKINGS: through the Fauré's first minute, K-weighted bar
	// by bar, 9.5 dB under where a single fff note would peak at 8 V — 12 at pp and 7 at ff, the
	// cello's range being narrower than the piano's. That note, 2.5 Pa on the open G, now peaks at
	// 2.7 V.
	k.voltsPerPascal = 1.07f;
	// Four times the engine's rate up to here, eight above it: few cello parts go higher.
	k.unsaidHighestHz = 640.f;
	return k;
}


//?module Plays one MPX part on a modelled cello, or on several where notes sound together: four
//? strings bowed through a model of rosin's friction, on one bridge and one body, each note's bowing
//? chosen by a model of a player.
//?note From a score, each note is announced to the module three seconds before it sounds. At
//? Fidelity levels 1 to 3, ways of starting it are tried on a copy of the cello while the music
//? plays. A note from a keyboard or a generator, which nothing announced, is played as it arrives.
//?note Notes sounding together, a chord on the cable or the channels of a polyphonic gate, are each
//? played by a cello of its own, up to sixteen. From the cable, the highest note of a chord goes to
//? the first cello, the next to the second, and so on; a note that starts as another ends goes to
//? the cello that played that one, so a line stays on one cello. The first cello is in tune; the
//? others are tuned up to four cents sharp or flat, and their vibratos run at slightly different
//? rates. Each cello playing takes as much computing as a module of its own; one with nothing to
//? play takes almost none.
//?note Fidelity, in the right-click menu, sets how much is tried on a copy of the cello before each
//? note sounds. Level 1 tries up to eighteen ways of starting each note from the string, and each bow
//? change three ways. Level 2 tries two ways of starting, a short glide and one bite. Level 3 plays
//? one bite without trying it and still tries each bow change. Level 4, the default, tries nothing:
//? each note is played as it arrives, so notes need not be announced ahead, and no second copy of
//? the cello is worked out, for the least computing.
//?note Economy, in the right-click menu, is for less computing at less fidelity: the strings are
//? worked out at half the rate, and a string nobody is playing stops once it has rung down, so it
//? no longer rings in sympathy with the others. It takes about half the computing.
struct CelloModule : BowedModule {
	CelloModule() : BowedModule(celloKind()) {
		configParam(P_VIBRATO, 0.f, 2.f, 1.f, "Vibrato", "%", 0.f, 100.f);
		//? How wide the vibrato is, as a share of each note's own width: at the middle, each note's
		//? own — wider on long and loud notes, none on open strings or where the part says senza
		//? vibrato, narrower in earlier music; at the bottom, none; at the top, twice as wide. A
		//? change is heard at once, on the notes already sounding.
		configInput(I_MPX, "MPX note");
		//? One part, played on the cello: its notes, from mpxScore or any MPX source. From a
		//? score each note is announced ahead, and at Fidelity levels 1 to 3 its start is worked out
		//? before it sounds.
		configInput(I_GATE, "Gate");
		//? Plays the cello without a cable: a note while the gate is up, let go when it falls. Each
		//? channel is a cello of its own, up to sixteen, and the same channel of V/OCT, LEVEL and VIB
		//? is that cello's; a one-channel cable into one of those is read by every cello. Each note is
		//? played as it comes, as Fidelity's live level plays it, since nothing says ahead what is
		//? coming. While an MPX cable is in, this port and the three below it are not read.
		configInput(I_PITCH, "1V/oct");
		//? The pitch of the gate's note, a volt an octave, nought at middle C, a channel for each
		//? cello. A move of more than half a semitone while the gate stays up is the next note, slurred
		//? in the same bow; a smaller one is left alone.
		configInput(I_LEVEL, "Level");
		//? How loud each gate's note is played, read as it begins, a channel for each cello: nought
		//? volts ppp, ten fff, in sevenths between. With nothing patched, mf.
		configInput(I_VIBRATO, "Vibrato CV");
		//? Added to the VIBRATO knob, a channel for each cello: five volts add as much as the knob's
		//? middle, and the sum is held between none and twice each note's own width.
		configOutput(O_OUT, "Out");
		//? The cellos playing, summed, in mono. Each is as loud as mpxPiano playing the same markings:
		//? a single note at fff peaks about 2.7V.
		configLight(L_LEVEL, "Level");
		//? Flashes green as each note begins, lights orange when the output passes 7.07V, 3 dB
		//? under clipping, and red when it reaches 10V, where Rack's audio interface clips.
	}
};


struct CelloWidget : BowedWidget {
	CelloWidget(CelloModule* module) : BowedWidget(module, "mpxCello") {}
};


} // namespace px


Model* modelMpxCello = createModel<px::CelloModule, px::CelloWidget>("mpxCello");
