/** mpxViola — an MPX part played on a physically modelled viola. Everything that plays is in
BowedModule.hpp; this is which instrument it is, and its controls with their help. See
docs/bowed-strings.md. */
#include "BowedModule.hpp"

namespace px {


static BowedKind violaKind() {
	BowedKind k;
	k.slug = "mpxViola";
	k.design = BowedInstrument::viola();
	k.habits = BowedPlayer::violaHabits();
	// AS LOUD AS mpxPiano PLAYING THE SAME MARKINGS: through the Fauré's first minute, K-weighted bar
	// by bar, 9.0 dB under where a single fff note would peak at 8 V. That note, 0.66 Pa on the open
	// G, now peaks at 2.8 V.
	k.voltsPerPascal = 4.29f;
	// Eight times the engine's rate up to here, sixteen above it: few viola parts go higher.
	k.unsaidHighestHz = 880.f;
	// THE BRIGHTNESS KNOB'S CORNER, a little under the viola's bridge hill.
	k.brightCornerHz = 2000.f;
	return k;
}


//?module Plays one MPX part on a modelled viola: four strings bowed through a model of rosin's
//? friction, on one bridge and one body, each note's bowing chosen by a model of a player.
//?note From a score, each note is announced to the module four and a half seconds before it sounds. At
//? Fidelity levels 1 to 3, ways of starting it are tried on a copy of the viola while the music
//? plays. A note from a keyboard or a generator, which nothing announced, is played as it arrives.
//?note One viola plays one note at a time: a chord on the cable is played as its top note, and of the
//? channels of a polyphonic gate, the highest whose gate is up. A section is a module for each player.
//?note A part on the cable that goes below the viola's lowest open string is played as a whole an
//? octave or two higher, as far as its highest note allows, and one that goes past the end of the
//? fingerboard, two octaves and a sixth above the highest string, an octave or two lower. Any note
//? still out of reach, and any from the ports, is moved by octaves by itself. While the part goes out
//? of reach, the plug light on the source's output blinks. Notes high on the fingerboard but within
//? it are played where they are written.
//?note Fidelity, in the right-click menu, sets how much is tried on a copy of the viola before each
//? note sounds. Level 1 tries up to eighteen ways of starting each note from the string, and each bow
//? change three ways. Level 2 tries two ways of starting, a short glide and one bite. Level 3 plays
//? one bite without trying it and still tries each bow change. Level 4, the default, tries nothing:
//? each note is played as it arrives, so notes need not be announced ahead, and no second copy of
//? the viola is worked out, for the least computing.
//?note Enable diagnostics, in the right-click menu, shows the number and the REC button along the
//? top of the panel; it is on to begin with. The number counts the notes announced on the cable that
//? were played
//? without anything tried, because the trying had not finished when they were due; a busy computer is
//? the usual cause. Notes nobody announced, from a keyboard or the ports, are not counted. It counts
//? from nought whenever Fidelity, Economy or the part's range changes and whenever the source goes
//? back to its start, and stays at nought at level 4.
//?note Economy, in the right-click menu, is for less computing at less fidelity: the strings are
//? worked out at half the rate, and a string nobody is playing stops once it has rung down, so it
//? no longer rings in sympathy with the others. It takes about half the computing.
struct ViolaModule : BowedModule {
	ViolaModule() : BowedModule(violaKind()) {
		configParam(P_VIBRATO, 0.f, 2.f, 1.f, "Vibrato", "%", 0.f, 100.f);
		//? How wide the vibrato is, as a share of each note's own width: at the middle, each note's
		//? own — wider on long and loud notes, none on open strings or where the part says senza
		//? vibrato, narrower in earlier music; at the bottom, none; at the top, twice as wide. A
		//? change is heard at once, on the notes already sounding.
		configParam(P_BRIGHT, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);
		//? Turns the viola's highs down. Fully clockwise, the viola as it is; turned counter-clockwise,
		//? everything above about 2 kHz is turned down, by up to 12 dB fully counter-clockwise, and
		//? nothing below it is touched. A filter on the output, after the viola: it changes nothing about
		//? how the notes are bowed or started.
		configInput(I_MPX, "MPX note");
		//? One part, played on the viola: its notes, from mpxScore or any MPX source. From a
		//? score each note is announced ahead, and at Fidelity levels 1 to 3 its start is worked out
		//? before it sounds.
		configSwitch(P_RECORD, 0.f, 1.f, 0.f, "Record", {"Off", "On"});
		//? Records what the module puts out to a WAV file while it is lit, with a text file beside
		//? it listing every note: how it began, the bow's force, speed and place, and whether its
		//? start was tried ahead. In the Rack user folder, under DreamerMPX/recordings; the
		//? right-click menu opens the folder. Each file is named for the module, the time, and how
		//? far into the piece it began.
		configInput(I_GATE, "Gate");
		//? Plays the viola without a cable: a note while the gate is up, let go when it falls. With a
		//? polyphonic gate, the highest pitch among the channels that are up. Each note is played as
		//? it comes, as Fidelity's live level plays it, since nothing says ahead what is coming. While
		//? an MPX cable is in, this port and the three below it are not read.
		configInput(I_PITCH, "1V/oct");
		//? The pitch of the gate's note, a volt an octave, nought at middle C. A move of more than half
		//? a semitone while the gate stays up is the next note, slurred in the same bow; a smaller one
		//? is left alone.
		configInput(I_LEVEL, "Level");
		//? How loud each gate's note is played, read as it begins: nought volts ppp, ten fff, in
		//? sevenths between. With nothing patched, mf.
		configInput(I_VIBRATO, "Vibrato CV");
		//? Added to the VIB knob: five volts add as much as the knob's middle, and the sum is held
		//? between none and twice each note's own width.
		configOutput(O_OUT, "Out");
		//? The viola, in mono, as loud as mpxPiano playing the same markings. A single note at fff
		//? peaks about 2.8V.
		configLight(L_LEVEL, "Level");
		//? Flashes green as each note begins, lights orange when the output passes 7.07V, 3 dB
		//? under clipping, and red when it reaches 10V, where Rack's audio interface clips.
	}
};


struct ViolaWidget : BowedWidget {
	ViolaWidget(ViolaModule* module) : BowedWidget(module, "mpxViola") {}
};


} // namespace px


Model* modelMpxViola = createModel<px::ViolaModule, px::ViolaWidget>("mpxViola");
