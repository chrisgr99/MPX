/** mpxViolin — an MPX part played on a physically modelled violin. Everything that plays is in
BowedModule.hpp; this is which instrument it is, and its controls with their help. See
docs/bowed-strings.md. */
#include "BowedModule.hpp"

namespace px {


static BowedKind violinKind() {
	BowedKind k;
	k.slug = "mpxViolin";
	k.design = BowedInstrument::violin();
	k.habits = BowedPlayer::violinHabits();
	// AS LOUD AS mpxPiano PLAYING THE SAME MARKINGS: through the Fauré's first minute, K-weighted bar
	// by bar, 10.1 dB under where a single fff note would peak at 8 V. That note, 0.85 Pa on the open
	// D, now peaks at 2.5 V.
	k.voltsPerPascal = 2.97f;
	// Eight times the engine's rate up to here, sixteen above it.
	k.unsaidHighestHz = 1280.f;
	return k;
}


//?module Plays one MPX part on a modelled violin: four strings bowed through a model of rosin's
//? friction, on one bridge and one body, each note's bowing worked out ahead of it.
//?note From a score, each note is announced to the module three seconds before it sounds, and ways
//? of starting it are tried on a copy of the violin while the music plays. A note from a keyboard
//? or a generator, which nothing announced, is played as it arrives, started the way found best
//? for the nearest note like it.
//?note A chord is played as its top note.
//?note Fidelity, in the right-click menu, sets how much is tried on a copy of the violin before each
//? note sounds. Level 1 tries up to eighteen ways of starting each note from the string, and each bow
//? change three ways. Level 2 tries two ways of starting, a short glide and one bite. Level 3, the
//? default, plays one bite without trying it and still tries each bow change. Level 4 tries nothing:
//? each note is played as it arrives, so notes need not be announced ahead, and no second copy of
//? the violin is worked out, for the least computing.
//?note Economy, in the right-click menu, is for less computing at less fidelity: the strings are
//? worked out at half the rate, and a string nobody is playing stops once it has rung down, so it
//? no longer rings in sympathy with the others. It takes about half the computing.
struct ViolinModule : BowedModule {
	ViolinModule() : BowedModule(violinKind()) {
		configParam(P_VIBRATO, 0.f, 2.f, 1.f, "Vibrato", "%", 0.f, 100.f);
		//? How wide the vibrato is, as a share of each note's own width: at the middle, each note's
		//? own — wider on long and loud notes, none on open strings or where the part says senza
		//? vibrato, narrower in earlier music; at the bottom, none; at the top, twice as wide. A
		//? change is heard at once, on the notes already sounding.
		configInput(I_MPX, "MPX note");
		//? One part, played on the violin: its notes, from mpxScore or any MPX source. From a
		//? score each note is announced ahead, and its start is worked out before it sounds.
		configOutput(O_OUT, "Out");
		//? The violin, in mono, as loud as mpxPiano playing the
		//? same markings. A single note at fff peaks about 2.5V.
		configLight(L_LEVEL, "Level");
		//? Flashes green as each note begins, lights orange when the output passes 7.07V, 3 dB
		//? under clipping, and red when it reaches 10V, where Rack's audio interface clips.
	}
};


struct ViolinWidget : BowedWidget {
	ViolinWidget(ViolinModule* module) : BowedWidget(module, "mpxViolin") {}
};


} // namespace px


Model* modelMpxViolin = createModel<px::ViolinModule, px::ViolinWidget>("mpxViolin");
