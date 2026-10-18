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
	// THE BRIGHTNESS KNOB'S CORNER, a little under the violin's bridge hill.
	k.brightCornerHz = 2500.f;
	return k;
}


//?module Plays one MPX part on a physically modelled violin: four bowed strings on a bridge and body. A
//? bow-friction model drives the strings, and a performance model sets each note's bow force, speed
//? and position.
//?note mpxScore sends each note to the module 4.5 seconds before it sounds. At Fidelity levels 1 to 3,
//? the module uses that time to simulate the note's attack. Notes from sources that do not send
//? them in advance, such as a keyboard or a sequencer, are played as they arrive.
//?note Each module models one player. Two notes on adjacent strings are bowed together as a double
//? stop. Three- or four-note chords are broken: the lower two notes sound briefly, then the bow
//? moves to the upper two. Plucked chords are strummed upwards. Chord notes that cannot be fingered
//? together are dropped, lowest first. With a polyphonic gate, only the highest channel whose gate
//? is high is played. For a section, use one module per player.
//?note Notes in a passage marked pizz. are plucked, up to the next arco. The string is plucked between
//? the end of the fingerboard and the bridge, with a strength set by the dynamic. A plucked string
//? rings until it decays or is played again, regardless of the written note length. Notes marked +
//? (left-hand pizzicato) are plucked 16 dB quieter and damped at their written end. A string still
//? ringing has its vibration reduced to a quarter before it is plucked again. Plucked notes are not
//? simulated in advance. Notes from the Gate input are always bowed.
//?note If a part goes below the lowest open string, the whole part is played one or two octaves higher,
//? as far as its highest note allows. If it goes above the end of the fingerboard, two octaves and
//? a sixth above the highest open string, the whole part is played one or two octaves lower. Notes
//? still out of range, and notes from the Gate input, are moved individually by octaves. While a
//? part is out of range, the light on the cable plug at the source's MPX output blinks.
//?note Fidelity, in the right-click menu, sets how much of each note's attack is simulated in advance,
//? on a second copy of the instrument model. Level 1 simulates up to 18 attack variants for each
//? note and 3 for each bow change: the most computing. Level 2 simulates 2 attack variants for each
//? note. Level 3 simulates only bow changes, 3 variants each. Level 4, the default, simulates
//? nothing, so notes need not be sent in advance: the least computing.
//?note Enable diagnostics, in the right-click menu, shows a counter at the top of the panel. On by
//? default. The counter shows how many notes sent in advance were played without their simulation,
//? because it had not finished in time; a busy computer is the usual cause. Notes not sent in
//? advance are not counted. It resets to 0 when Fidelity, Economy or the part's range changes, or
//? when the source returns to its start, and stays at 0 at level 4.
//?note Economy, in the right-click menu, roughly halves the computing at some cost in sound quality:
//? the strings are computed at half the internal sample rate, and a string that is not being played
//? stops being computed once it falls silent, so it no longer resonates with the other strings.
struct ViolinModule : BowedModule {
	ViolinModule() : BowedModule(violinKind()) {
		configParam(P_VIBRATO, 0.f, 2.f, 1.f, "Vibrato", "%", 0.f, 100.f);
		//? Vibrato width, relative to each note's default. At the centre, the default: wider on long and
		//? loud notes, none on open strings or notes marked senza vibrato, and narrower in music from
		//? earlier periods. Fully counter-clockwise, no vibrato; fully clockwise, twice the default.
		//? Changes apply immediately, including to notes already sounding.
		configParam(P_BRIGHT, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);
		//? Cuts frequencies above about 2.5 kHz, by up to 12 dB when fully counter-clockwise. Fully
		//? clockwise, no cut. A filter on the output only; it does not change how notes are played.
		configInput(I_MPX, "MPX note");
		//? The part to play, from mpxScore or any other MPX source. While connected, the Gate, 1V/oct,
		//? Level and Vibrato CV inputs are ignored.
		// THE RECORDER IS FOR DEVELOPMENT, and is left out of the help on purpose.
		configSwitch(P_RECORD, 0.f, 1.f, 0.f, "Record", {"Off", "On"});
		configInput(I_GATE, "Gate");
		//? Plays a note while the gate is above 1 V and releases it when the gate falls below 0.5 V. With
		//? a polyphonic gate, the highest pitch among the channels whose gate is high is played. Notes
		//? from this input are played without advance simulation, as at Fidelity level 4. Ignored while
		//? an MPX cable is connected.
		configInput(I_PITCH, "1V/oct");
		//? Pitch of the note played from the Gate input, 1 V per octave, 0 V = C4. A change of more than
		//? half a semitone while the gate stays high starts a new note, slurred in the same bow stroke.
		//? Smaller changes are ignored; the note keeps its pitch.
		configInput(I_LEVEL, "Level");
		//? Dynamic of each note played from the Gate input, read when the note starts. 0 V is ppp and 10
		//? V is fff; pp, p, mp, mf, f and ff fall at equal steps of about 1.43 V between them, and
		//? voltages in between give intermediate levels. Unconnected: mf.
		configInput(I_VIBRATO, "Vibrato CV");
		//? Added to the Vibrato knob's setting: +5 V adds the same amount as the knob's centre position.
		//? The total is limited to between no vibrato and twice the default width. Ignored while an MPX
		//? cable is connected.
		configOutput(O_OUT, "Out");
		//? Mono audio. Its loudness matches mpxPiano playing the same dynamics; a single fff note peaks
		//? at about 2.5 V.
		configLight(L_LEVEL, "Level");
		//? Flashes green when a note starts. Orange when the output exceeds 7.07 V, 3 dB below clipping;
		//? red when it reaches 10 V, where Rack's audio interface clips.
	}
};


struct ViolinWidget : BowedWidget {
	ViolinWidget(ViolinModule* module) : BowedWidget(module, "mpxViolin") {}
};


} // namespace px


Model* modelMpxViolin = createModel<px::ViolinModule, px::ViolinWidget>("mpxViolin");
