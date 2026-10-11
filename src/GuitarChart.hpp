#pragma once
/** A Guitar Pro file, played. See docs/guitar-pro.md.

ONE CABLE IS ONE INSTRUMENT. Each row of the panel is one track of the song on its own MPX
output, because everything downstream of a cable in this plugin reads it as one instrument:
mpxVoicing decides where the voices sit, mpxArp arpeggiates the notes, mpxOut turns the cable
into control voltage, and none of them asks which part a note belongs to. A band on one cable
would have all of them playing every track at once.

THE FILE IS READ ONCE, ON THE MAIN THREAD, and what comes out of it — the bars in the order they
sound, every note with the moment it starts and the moment it stops — is fixed from then on. The
audio thread reads it and nothing else. Two copies, with a pointer swapped between them, so
loading a song while one is playing cannot be heard.

WHAT IS NOT HERE YET. There are no chord symbols on the cable: working the harmony out from the
notes comes later, and until it does the cable carries the beat, the bar and the metre but no
chords. See docs/guitar-pro.md.

TWO MODULES FROM THIS ONE FILE. mpxGuitarChart6 and mpxGuitarChart (the twelve) are this code
compiled twice, by mpxGuitarChart6.cpp and mpxGuitarChart.cpp, each saying how many rows it has
and whether it plays its own tracks. Each copy is in a namespace of its own, so the two copies of
SongModule are two types and not one type defined twice.

A BAND OF ITS OWN, where CHART_AUDIO says so. The rows play through the shared synthesiser
(SoundCore.hpp) on the instruments the file names, each with a gain and a mono output, and the
whole band on a stereo pair. The rows' outputs are shown only when the menu asks for them, the
stereo pair always, and the synthesiser runs while either can be heard. A row whose MPX output has
a cable in it is the cable's to voice instead: its notes go down the cable, at its gain, and the
synthesiser leaves it alone.
*/
#include "plugin.hpp"
#include "GpTimeline.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "ScoreFollow.hpp"
#if CHART_AUDIO
#include "SoundCore.hpp"
#include "Swing.hpp"
#include "LastFolder.hpp"
#include "MusicXML.hpp"
#include "ScoreWindow.hpp"
#endif

#include <osdialog.h>
#include <sys/stat.h>

#include <atomic>
#include <cctype>
#include <cstring>

namespace px {
namespace CHART_NS {


/** HOW MANY ROWS THERE CAN BE, which is what everything here is sized for. How many a module
actually SHOWS is a setting — six in one column or twelve in two — kept in SongModule::rows and
saved with the patch. The ports exist either way: a row the panel does not show simply carries no
track, and every loop here already passes over a row that has none.

TWELVE PORTS WHATEVER THE PANEL SHOWS, so that the stereo pair keeps the output numbers it has
always had and a patch wired to the mix stays wired to it. */
static const int MAX_ROWS = 12;
static const int ROWS = MAX_ROWS;
static const int ROWS_PER_COLUMN = 6;
/** The row count a module starts with, from the file that includes this. */
static const int DEFAULT_ROWS = CHART_ROWS;


/** THE GENERAL MIDI INSTRUMENTS, by program number, for the line under a track's name. The
standard's own names, since the bank may not be loaded when the panel is drawn. */
static const char* const GM_NAMES[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
	"Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone",
	"Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
	"Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar",
	"Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Picked Bass", "Fretless Bass",
	"Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass",
	"Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
	"String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
	"Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet",
	"French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
	"Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute",
	"Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Sawtooth Lead", "Calliope Lead", "Chiff Lead",
	"Charang Lead", "Voice Lead", "Fifths Lead", "Bass and Lead",
	"New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad",
	"Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere",
	"Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto",
	"Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
	"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
	"Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

static std::string instrumentName(const GpTrack& track) {
	if (track.percussion)
		return "Drum kit";
	return GM_NAMES[std::max(0, std::min(127, track.midiProgram))];
}

#if CHART_AUDIO
/** From the synthesiser's output to Rack's volts: SoundCore's, so the band is as loud as an
mpxGuitar playing the same part. */
static const float AUDIO_LEVEL = SOUND_VOLTS;
/** The SoundFont the band plays, in SoundCore's folder. */
static const char* CHART_BANK = "GeneralUser-GS.sf2";
#endif

/** A note's length is the file's, within reason: a note held to the end of a long let-ring
passage is still a note and not a drone. */
static const float MIN_SECONDS = 0.01f;
static const float MAX_SECONDS = 30.f;

/** HOW LONG A SILENCE HAS TO BE before a slur stops meaning legato, in seconds. A note written to
end a shade before the next is a player's articulation and the slur still holds the line together;
a real rest between them is a break the composer asked for. Fifty milliseconds is longer than the
overlap any renderer adds and far shorter than the shortest rest anybody writes. */
static const float LEGATO_GAP = 0.05f;


/** A song as it is played, with the rows' choice of track. Whole, so that swapping to a new one
is one store. */
struct Playing {
	GpSong song;
	GpTimeline line;
	/** Which track each row carries, or -1 for a row carrying nothing. */
	int track[ROWS] = {};
	std::string title;
	/** Moves when the song or a row's choice of track changes, so the instruments on the cables
	are published again and only then. */
	uint32_t partChange = 1;
	/** The tempo the song opens at, in quarter notes a minute. What the tempo control is set to
	when a song is loaded, and what it is measured against afterwards. */
	float openingBpm = 120.f;
	/** WHAT THE FILE WAS TAKEN FOR, for the menu to say: the style its ornaments are played in,
	the composer recognised, and why. Only for a MusicXML file; see OrnamentStyle.hpp. */
	StyleGuess styleGuess;
	bool hasStyle = false;

	Playing() {
		for (int r = 0; r < ROWS; r++)
			track[r] = -1;
	}
};


struct SongModule : Module, NoteSource {
	enum ParamId {
		P_PLAY,
		P_REWIND,
		P_TEMPO,
		P_MUTE,
		P_GAIN = P_MUTE + ROWS,
		// ADDED AT THE END, so every parameter a saved patch already names keeps its number.
#if CHART_AUDIO
		P_LOAD = P_GAIN + ROWS,
#else
		P_LOAD = P_GAIN,
#endif
		P_SWING,
		NUM_PARAMS
	};

	enum InputId {
		NUM_INPUTS
	};
	enum OutputId {
		O_TRACK,
		// THE STEREO PAIR SITS WHERE IT ALWAYS DID. Six rows with a per-row audio jack each put
		// the mix at twelve and thirteen; twelve rows with no audio jacks put it in the same
		// place, so a patch that was wired to the mix stays wired to it.
#if CHART_AUDIO
		O_MIX_L = O_TRACK + MAX_ROWS,
		O_MIX_R,
		NUM_OUTPUTS
#else
		NUM_OUTPUTS = O_TRACK + MAX_ROWS
#endif
	};
	enum LightId {
		L_BEAT,
		/** One a row, lit by each note the row sends. */
		L_ROW,
		NUM_LIGHTS = L_ROW + ROWS
	};

	/** ONE BUS PER ROW THAT CARRIES SOMETHING. There are sixty-four buses in the whole rack, so
	a module holding twelve whether or not it used them would be taking a sixth of them for
	nothing. They are claimed when a song is loaded, which is on the main thread, so no row is
	ever waiting for a bus while the music plays. */
	int slot[ROWS];
	uint32_t generation[ROWS] = {};

	Playing loaded[2];
	std::atomic<int> live{0};
	std::atomic<bool> haveSong{false};
	/** The file as it arrived, kept so the patch carries the song rather than a path to it: a
	patch that pointed at a file would open to silence on a machine that never had it. */
	std::string fileBytes;
	bool fileIsMusicXML = false;

	/** HOW MANY ROWS THE PANEL SHOWS, six or twelve, saved with the patch. The ports exist for
	twelve whatever this says; a row the panel does not show carries no track and so does nothing.
	Changing it rebuilds the panel and restarts the synthesiser, which is sized by the count. */
	int rows = DEFAULT_ROWS;
	std::string filePath;
	/** THE INSTRUMENT A PASTED MODULE BROUGHT WITH IT, held until Rack gives the module its id.
	A paste reads the module's data before the id is assigned, and the remembered instrument is
	kept under the id. See onAdd. */
	int pendingPart = -1;
	/** For the panel, which reads from the main thread while the audio thread plays. */
	std::atomic<int> playedBar{0};
	std::atomic<float> soundingBpm{120.f};

	/** WHERE THE MUSIC IS, in quarter notes from the start of the played timeline. */
	double quarters = 0.0;
	/** How far each row has got through its track's notes. */
	size_t cursor[ROWS] = {};
	int barIndex = 0;
	/** WHICH PEDAL MARK THE MUSIC HAS PASSED, or -1 before the first one. */
	int pedalIndex = -1;
	/** WHETHER THE SCORE'S OWN PEDAL IS SENT. On, because a score that is marked up should be
	played the way it is marked; off for a patch that would rather hold the pedal itself. */
	std::atomic<bool> sendPedal{true};
	/** WHETHER THE PASSAGES THE SCORE LEAVES UNMARKED ARE PEDALLED, as Pedalling.hpp works out from
	the harmony. On, because most scores mark the pedal in some places and not others, and a piano
	played literally through the rest is dry. Saved with the patch. */
	std::atomic<bool> fillPedal{true};
	/** THE ORNAMENT STYLE CHOSEN IN THE MENU, one of OrnamentStyle, or -1 to take what the score
	is worked out to be. Saved with the patch. */
	std::atomic<int> styleChoice{-1};
	uint32_t epoch = 0;
	uint32_t partChange = 0;

	/** HOW MUCH EACH PITCH CLASS IS WORTH AT THIS MOMENT, and what the bass is.

	See Harmony::pitchWeight. A note adds to its pitch class by being long, by falling on a beat
	and by being loud, and the whole set fades over about a beat — so a passing sixteenth is a
	flicker and a held bass note is a wall. Nothing here names a chord, because this is for music
	that has none. */
	float pitchWeight[12] = {};
	int bassMidi = -1;
	double bassWhen = -1e9;


	/** A row given another track, whose place in that track's notes has to be found again. */
	std::atomic<bool> reseek[ROWS];

#if CHART_AUDIO
	SoundCore core;
#endif

	dsp::SchmittTrigger clockTrigger, resetTrigger, rewindTrigger;
	float sinceLastPulse = 0.f;
	float quarterSeconds = 0.5f;
	bool awaitPulse = false;
	bool wasRunning = false;
	float beatLamp = 0.f;
	float rowLamp[ROWS] = {};


	SongModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(P_LOAD, "Load a score, or pick a recent one");
		// SWING, WHICH THE SCORE DOES NOT SAY. Two-sided: see swingRatios and docs/notation.md.
		// Nought by default, so nothing already patched starts swinging.
		configParam(P_SWING, -1.f, 1.f, 0.f, "Swing", "%", 0.f, 100.f);

		configSwitch(P_PLAY, 0.f, 1.f, 1.f, "Play", {"Stopped", "Playing"});
		//? Starts the song again from its first bar. It does not stop it.
		configButton(P_REWIND, "Rewind");
		//? The tempo to play at, in quarter notes a minute. Set to the song's own when one is
		//? loaded; changing it moves every tempo in the song by the same proportion, so a song
		//? that speeds up or slows down still does. A clock overrides it.
		configParam(P_TEMPO, 20.f, 400.f, 120.f, "Tempo", " BPM");
		for (int r = 0; r < ROWS; r++) {
			//? Silences this row without unpatching it, so what is downstream keeps its cable
			//? and its settings.
			configSwitch(P_MUTE + r, 0.f, 1.f, 0.f, string::f("Mute %d", r + 1),
				{"Playing", "Muted"});
			// THE NAME SAYS MPX, and has to. Clarity colours a jack by its name where it has
			// no entry for the module, and its first rule is that a name carrying MPX is an
			// MPX jack. Named for the track alone, these came out orange — the colour of a
			// control voltage. When the name becomes the instrument's, it keeps the word.
			configOutput(O_TRACK + r, string::f("MPX track %d", r + 1));
		}

		for (int r = 0; r < ROWS; r++) {
			slot[r] = -1;
			reseek[r] = false;
		}
#if CHART_AUDIO
		for (int r = 0; r < ROWS; r++) {
			//? This row's level: the notes on its MPX cable when that is patched, otherwise its sound in the band's stereo pair.
			configParam(P_GAIN + r, 0.f, 2.f, 1.f, string::f("Gain %d", r + 1), "%", 0.f, 100.f);
		}
		configOutput(O_MIX_L, "Band left");
		configOutput(O_MIX_R, "Band right");
		core.setParts(rows);
		core.logName = CHART_SLUG;
		core.followInstrument = true;
		core.readRules();
#endif
	}

#if CHART_AUDIO
	// ---- the band ------------------------------------------------------------------------------

	/** Whether the synthesiser is running. Main thread. */
	bool bandRunning = false;

	/** THE SYNTHESISER RUNS WHILE ANYTHING CAN HEAR IT, which is now simply a cable in the stereo
	pair. The rows had an audio output each and they are gone: a row is played through its MPX
	cable, and the band is heard through the mix. Main thread: called from the widget every frame,
	since a cable can be plugged in at any time. */
	void updateBand() {
		const bool want = outputs[O_MIX_L].isConnected() || outputs[O_MIX_R].isConnected();
		if (want == bandRunning)
			return;
		bandRunning = want;
		if (want)
			startEngine(APP->engine->getSampleRate());
		else
			core.stopEngine();
	}

	void startEngine(float rate) {
		// THE BAND'S OWN SOUNDFONT, always: there is no choosing another.
		core.startEngine(rate, false, false, SoundCore::bankFolder() + "/" + CHART_BANK);
	}


	void onRemove(const RemoveEvent& e) override {
		core.stopEngine();
		bandRunning = false;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (bandRunning)
			startEngine(e.sampleRate);
	}

	/** THE ROWS THE SYNTHESISER PLAYS, every frame: each row with a track and no cable in its MPX
	output. The bus is read exactly as a cable's far end reads it. */
	void playBand(const ProcessArgs& args) {
		if (!core.engine.running()) {
			outputs[O_MIX_L].setVoltage(0.f);
			outputs[O_MIX_R].setVoltage(0.f);
			return;
		}
		for (int r = 0; r < ROWS; r++) {
			int s = slot[r];
			uint32_t g = generation[r];
			const bool ours = s >= 0 && !outputs[O_TRACK + r].isConnected();
			core.link(r, &s, &g, ours ? 1 : 0);
		}
		// HUMANISE AT ITS OWN AMOUNT, and no reverb or chorus: those are better done by a
		// processor after the outputs.
		core.step(args.sampleTime, NULL, 1.f, false, false);
		float left = 0.f, right = 0.f;
		for (int r = 0; r < ROWS; r++) {
			const float gain = params[P_GAIN + r].getValue();
			const float l = core.partLeft(r) * gain;
			const float rr = core.partRight(r) * gain;
			left += l;
			right += rr;
		}
		// NOTHING LEAVES ABOVE TEN, which the band needs as much as anything: one programme
		// of a SoundFont can sit a couple of decibels above the rest and no single scale
		// factor takes that out. See mpxSoftClip.
		outputs[O_MIX_L].setVoltage(mpxSoftClip(left * AUDIO_LEVEL));
		outputs[O_MIX_R].setVoltage(mpxSoftClip(right * AUDIO_LEVEL));
	}
#endif

	void onAdd(const AddEvent& e) override {
#if CHART_AUDIO
		updateBand();
#endif
		// THE ID HAS ARRIVED, so a pasted module's instrument can be kept under it now.
		if (pendingPart >= 0)
			scoreFollowSetPart(id, pendingPart);
		pendingPart = -1;
	}

	~SongModule() {
		for (int r = 0; r < ROWS; r++)
			busRelease(slot[r]);
		// THE LOOP AND THE PLAYHEAD GO WITH THE MODULE, and its place among the followed scores
		// is free for the next one. A patch opened again in the same session brings its modules
		// back with the same ids, and they would otherwise find this one's loop waiting.
		scoreFollowDrop(id);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		const int r = outputId - O_TRACK;
		if (r < 0 || r >= ROWS)
			return -1;
		if (gen)
			*gen = generation[r];
		return slot[r];
	}

	void onReset() override {
		rewind();
		scoreFollowSetLoop(id, -1, -1, false);
	}

	/** THE ORNAMENTS PLAYED IN ANOTHER STYLE, or in the one worked out with -1: the timeline built
	again and swapped in where the music is, as a newly loaded song is, without reading the file
	again — which would put the music back to the top and the tempo back to the score's. Main
	thread. */
	void restyle(int choice) {
		styleChoice = choice;
		const int spare = 1 - live.load();
		Playing next = loaded[live.load()];
		if (!next.hasStyle)
			return;
		next.song.ornamentStyle = (choice >= 0 && choice < STYLE_COUNT) ? choice : next.styleGuess.style;
		std::string why;
		if (!gpBuildTimeline(next.song, next.line, &why))
			return;
		loaded[spare] = next;
		live.store(spare);
		for (int r = 0; r < ROWS; r++)
			reseek[r].store(true);
	}

	/** The bars being looped, as the page numbers them, or false for none — including a loop
	naming bars this piece does not have, which plays as no loop at all. Main thread. */
	bool looping(int& from, int& to) {
		if (!scoreFollowLoop(id, from, to, NULL))
			return false;
		double at = 0.0, end = 0.0;
		return loopRange(loaded[live.load()], from, to, at, end);
	}

	/** WHERE A NOTE FALLS ONCE THE MUSIC SWINGS, in quarter notes.

	A SCORE HAS TO MOVE ITS OWN NOTES. The chart publishes the swing and the modules downstream
	place their notes where it says — but those modules are inventing rhythm, and this one is
	playing rhythm that is already written. Nothing downstream is going to move a written note,
	so it is moved here.

	ONLY THE NOTES ON THE OFF-HALF AND THE OFF-QUARTER OF A BEAT ARE TOUCHED, and that one rule
	does the work of three. A triplet sits on a third of a beat and is left alone, which is right:
	a triplet is already a three-way division and swinging it would be swinging a swing. The small
	notes of an ornament have their own fast timing and are not on the grid either, so they are
	left alone too. And anything written off the grid — a tied-over syncopation, a note placed by
	hand — stays where it was put.

	IT ALSO CHOOSES THE LEVEL WITHOUT BEING TOLD. A note on the half-beat takes the eighth ratio
	and one on the quarter-beat the sixteenth, which is how Phrasing already picks and means
	there is no mode to set and nothing to get wrong on a piece that mixes the two.

	SIMPLE METRES ONLY. A ratio divides a beat in two, and a compound metre has already divided
	its beat in three — which is the thing swing is imitating. Nothing is moved in six eight.

	THE BAR LINES AND THE BEATS DO NOT MOVE. Only what is between them. */
	double swung(double startQuarters, const GpMasterBar& written) {
		const float amount = params[P_SWING].getValue();
		if (std::fabs(amount) < 0.001f || written.beatUnit >= 8)
			return startQuarters;
		float r8 = 1.f, r16 = 1.f;
		swingRatios(amount, 60.f / std::fmax(0.0001f, (float) quarterSeconds), r8, r16);
		const double beat = std::floor(startQuarters);
		const double u = startQuarters - beat;          // Where in the beat, nought to one.
		// The point a divided beat is divided AT: a ratio of one is a half, and more than one
		// puts it later.
		const double m8 = (double) r8 / ((double) r8 + 1.0);
		const double m16 = (double) r16 / ((double) r16 + 1.0);
		static const double CLOSE = 1e-4;
		if (std::fabs(u - 0.5) < CLOSE)
			return beat + m8;                           // The off-beat eighth.
		if (std::fabs(u - 0.25) < CLOSE)
			return beat + m8 * m16;                     // The off-quarter inside the first half.
		if (std::fabs(u - 0.75) < CLOSE)
			return beat + m8 + (1.0 - m8) * m16;        // And inside the second.
		return startQuarters;
	}

	void rewind() {
		quarters = 0.0;
		barIndex = 0;
		pedalIndex = -1;
		for (int r = 0; r < ROWS; r++)
			cursor[r] = 0;
	}

	// ---- loading a song ------------------------------------------------------------------------

	/** The name of the track a row carries, or an empty string when it carries none. Main
	thread. */
	std::string rowName(int r) const {
		if (!haveSong.load())
			return "";
		const Playing& L = loaded[live.load()];
		const int t = L.track[r];
		if (t < 0 || t >= (int) L.song.tracks.size())
			return "";
		std::string name = string::trim(L.song.tracks[(size_t) t].name);
		return name.empty() ? string::f("Track %d", t + 1) : name;
	}

	/** The General MIDI instrument of the track a row carries, or an empty string. Main
	thread. */
	std::string rowInstrument(int r) const {
		if (!haveSong.load())
			return "";
		const Playing& L = loaded[live.load()];
		const int t = L.track[r];
		if (t < 0 || t >= (int) L.song.tracks.size())
			return "";
		return instrumentName(L.song.tracks[(size_t) t]);
	}

	/** THE WHOLE NAME IS IN THE TOOLTIP, since the panel has room for only two short lines of
	it. The word MPX stays, for the reason given where the outputs are configured. */
	void nameOutputs() {
		for (int r = 0; r < ROWS; r++) {
			const std::string name = rowName(r);
			outputInfos[O_TRACK + r]->name = name.empty()
				? string::f("MPX track %d", r + 1)
				: string::f("MPX track %d: %s (%s)", r + 1, name.c_str(),
					rowInstrument(r).c_str());
		}
	}

	/** GIVES A ROW ANOTHER OF THE SONG'S TRACKS, or none at -1. Main thread. The row's place in
	its new track is found on the audio thread, and the instrument is published again. */
	void chooseTrack(int r, int t) {
		if (r < 0 || r >= ROWS || !haveSong.load())
			return;
		Playing& L = loaded[live.load()];
		if (t < -1 || t >= (int) L.line.tracks.size())
			return;
		L.track[r] = t;
		reseek[r] = true;
		params[P_MUTE + r].setValue((t >= 0) ? 0.f : 1.f);
		claimBuses(L);
		L.partChange++;
		nameOutputs();
	}

	/** HOW MANY ROWS TO SHOW. Main thread. A row that is taken away keeps its track in the patch,
	so turning twelve back into six and then into twelve again finds the rows as they were — but
	it stops playing and gives up its cable, since a row nobody can see should not be heard. */
	void setRowCount(int count) {
		count = (count > ROWS_PER_COLUMN) ? MAX_ROWS : ROWS_PER_COLUMN;
		if (count == rows)
			return;
		rows = count;
#if CHART_AUDIO
		core.setParts(rows);
		if (bandRunning) {
			core.stopEngine();
			startEngine(APP->engine->getSampleRate());
		}
#endif
		if (haveSong.load())
			claimBuses(loaded[live.load()]);
	}

	/** A ROW WITH NOTHING ON IT SHOWS AS MUTED. Main thread.

	A row carrying no track is silent whatever its mute says, so the mute may as well say so: an
	empty row then reads as empty at a glance instead of looking like a row that ought to be
	playing. Loading a score sets them all — the rows it filled come up unmuted, the rest muted —
	and choosing a track for a row unmutes that row, since choosing it is asking to hear it. */
	void syncMutes(const Playing& L) {
		for (int r = 0; r < ROWS; r++)
			params[P_MUTE + r].setValue((L.track[r] >= 0) ? 0.f : 1.f);
	}

	/** WHAT THE PAGE CALLS A BAR, which is not where it sits. A pickup is numbered nought and the
	first whole bar is one, so a bar's position and its number differ by one for the whole piece —
	and a cursor that used the position sat a bar ahead of the music from the first note. */
	static int writtenNumber(const Playing& L, int written) {
		if (written >= 0 && written < (int) L.song.masterBars.size()) {
			const int said = L.song.masterBars[(size_t) written].number;
			if (said >= 0)
				return said;
		}
		return written + 1;
	}

	/** WHERE A LOOP OF PAGE BARS FALLS ON THE PLAYED TIMELINE, in quarter notes: from the start of
	the first pass through `from` to the end of `to` after it. The first pass is the one meant, since
	looping a repeated section would otherwise jump between its passes. False where the piece has
	no such bars, which a loop kept across an edit of the file can name. */
	static bool loopRange(const Playing& L, int from, int to, double& at, double& end) {
		at = end = -1.0;
		for (size_t i = 0; i < L.line.bars.size(); i++) {
			const GpPlayedBar& b = L.line.bars[i];
			const int n = writtenNumber(L, b.written);
			if (at < 0.0 && n == from)
				at = b.startQuarters;
			if (at >= 0.0 && n == to) {
				end = b.startQuarters + b.quarters;
				break;
			}
		}
		return end > at && at >= 0.0;
	}

	/** A bus for every row that carries a track, and none for the rest. Main thread. */
	void claimBuses(const Playing& L) {
		for (int r = 0; r < ROWS; r++) {
			// A ROW THE PANEL DOES NOT SHOW IS NOT PLAYED, whatever track the patch remembers
			// against it: nobody can see it, mute it or unplug it.
			const bool wanted = r < rows && L.track[r] >= 0;
			if (wanted && slot[r] < 0)
				slot[r] = busClaim(&generation[r]);
			else if (!wanted && slot[r] >= 0) {
				busRelease(slot[r]);
				slot[r] = -1;
			}
		}
	}

	/** Reads a file and puts it on the spare copy. Main thread. Returns a reason on failure, and
	leaves whatever was playing alone. */
	bool setSongBytes(const std::vector<uint8_t>& bytes, const std::string& path,
			std::string* why) {
		const int spare = 1 - live.load();
		Playing next;
		// WHICH KIND OF FILE IT IS, FROM WHAT IS IN IT rather than from its name. A file
		// downloaded from the internet is as likely as not to carry the wrong extension — the
		// Guitar Pro reader already chooses between its four formats this way — and MusicXML and
		// Guitar Pro both end up as the same song, so there is no reason to make anybody say
		// which they have.
		const std::string head((const char*) bytes.data(),
			std::min<size_t>(bytes.size(), 2048));
		bool ok = false;
		bool wasMusicXML = false;
		if (mxLooksLikeMusicXML(head)) {
			ok = mxReadBytes(bytes, next.song, why);
			wasMusicXML = ok;
			// A ZIP THAT IS NOT MUSICXML IS A GUITAR PRO 7 FILE, which is also a zip. The sniff
			// cannot tell them apart, so a failure falls back rather than giving up.
			if (!ok)
				ok = gpReadBytes(bytes, next.song, NULL);
		}
		else {
			ok = gpReadBytes(bytes, next.song, why);
		}
		if (!ok)
			return false;
		// THE STYLE ITS ORNAMENTS ARE PLAYED IN, guessed from everything the file says about who
		// wrote it and from its own name, unless the menu has said otherwise. Before the timeline,
		// which is where the ornaments are written out. See OrnamentStyle.hpp.
		if (wasMusicXML) {
			std::vector<StyleClue> clues = next.song.clues;
			std::string name = path;
			const size_t slash = name.find_last_of("/\\");
			if (slash != std::string::npos)
				name = name.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			if (dot != std::string::npos && dot > 0)
				name = name.substr(0, dot);
			if (!name.empty())
				clues.push_back(StyleClue{name, CLUE_FILE});
			next.styleGuess = guessStyle(clues, next.song.chordSymbols);
			next.hasStyle = true;
			const int chosen = styleChoice.load();
			next.song.ornamentStyle = (chosen >= 0 && chosen < STYLE_COUNT) ? chosen
				: next.styleGuess.style;
		}
		if (!gpBuildTimeline(next.song, next.line, why))
			return false;

		// WHICH TRACKS THE ROWS TAKE. A song has more tracks than there are rows more often than
		// not — nineteen is an ordinary transcription — and what is wanted out of it is four or
		// five. The ones carrying the most notes are a reasonable first guess, and the next phase
		// puts the choice on the panel.
		std::vector<std::pair<int, int> > byNotes;   // Notes, then track, so a sort ranks them.
		for (size_t t = 0; t < next.line.tracks.size(); t++)
			byNotes.push_back(std::make_pair(-(int) next.line.tracks[t].size(), (int) t));
		std::sort(byNotes.begin(), byNotes.end());
		for (int r = 0; r < rows && r < (int) byNotes.size(); r++)
			next.track[r] = byNotes[(size_t) r].second;

		next.openingBpm = (float) (60.0 / std::fmax(0.0001,
			gpSecondsPerQuarter(next.line, 0.0)));
		// WHAT TO CALL IT. Plenty of files carry no title at all — the Oasis one has an empty
		// title field and the tabber's name where the artist goes — and the file's own name is
		// what somebody typed to say what the song is, so it is a better answer than a word
		// saying there is no answer.
		next.title = next.song.title;
		if (!next.title.empty() && !next.song.artist.empty())
			next.title += " — " + next.song.artist;
		if (next.title.empty()) {
			std::string name = path;
			const size_t slash = name.find_last_of("/\\");
			if (slash != std::string::npos)
				name = name.substr(slash + 1);
			// Every extension it has, since these files arrive named things like
			// "... score.gpif.xml".
			static const char* ENDS[] = {".xml", ".gpif", ".gpx", ".gp5", ".gp4", ".gp3",
				".gp", NULL};
			for (bool again = true; again; ) {
				again = false;
				for (int i = 0; ENDS[i]; i++) {
					const size_t n = std::strlen(ENDS[i]);
					if (name.size() <= n)
						continue;
					std::string tail = name.substr(name.size() - n);
					for (size_t k = 0; k < tail.size(); k++)
						tail[k] = (char) std::tolower((unsigned char) tail[k]);
					if (tail == ENDS[i]) {
						name = name.substr(0, name.size() - n);
						again = true;
						break;
					}
				}
			}
			next.title = name.empty() ? "(untitled)" : name;
		}

		next.partChange = loaded[live.load()].partChange + 1;
		loaded[spare] = next;
		live.store(spare);
		syncMutes(next);
		claimBuses(next);
		haveSong.store(!loaded[spare].line.bars.empty());
		fileBytes.assign((const char*) bytes.data(), bytes.size());
		// WHICH READER TOOK IT, rather than which one the sniff guessed: a Guitar Pro 7 file is a
		// zip and sniffs as MusicXML, and only the reading tells them apart. The score window
		// shows the writer's own document where there is one. See showScore.
		fileIsMusicXML = wasMusicXML;
		// A LOOP IS BARS OF ONE PIECE. Another file's bar numbers mean nothing in this one, so
		// loading a different file lets the loop go; the same file read again, as it is each time
		// it is saved from an editor, keeps it. The window no longer clears a loop when it closes,
		// so this is where a loop from the last piece would otherwise survive.
		if (path != filePath) {
			scoreFollowSetLoop(id, -1, -1, false);
			// And the instrument the window was showing, for the same reason.
			scoreFollowSetPart(id, -1);
		}
		else {
			// THE SAME FILE, EDITED, may have lost the bars the loop names.
			int from = -1, to = -1;
			double at = 0.0, end = 0.0;
			if (scoreFollowLoop(id, from, to, NULL) && !loopRange(next, from, to, at, end))
				scoreFollowSetLoop(id, -1, -1, false);
		}
		filePath = path;
		// A WINDOW ALREADY OPEN ON THIS MODULE GETS THE NEW SCORE. It is told what to show when
		// it is opened and was never told again, so loading another piece left it showing the
		// one before — and a window left on the old piece would set a loop and an instrument for
		// the new one from the old one's bars and parts. A Guitar Pro song goes the same way,
		// which is also how an edit saved from Guitar Pro reaches the window.
		if (wasMusicXML)
			px::scoreWindowReplaceFor(id, std::make_shared<const std::string>(fileBytes),
				next.title.empty() ? std::string("Score") : next.title);
		else if (px::scoreWindowShowingFor(id)) {
			int track = 0;
			for (int r = 0; r < ROWS; r++) {
				if (next.track[r] >= 0) {
					track = next.track[r];
					break;
				}
			}
			px::scoreWindowReplaceSongFor(id, std::make_shared<const GpSong>(next.song), track,
				next.title.empty() ? std::string("Score") : next.title);
		}
		params[P_TEMPO].setValue(math::clamp(next.openingBpm, 20.f, 400.f));
		rewind();
		nameOutputs();
		return true;
	}

	bool setSongFile(const std::string& path, std::string* why) {
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) {
			if (why)
				*why = "that file could not be opened";
			return false;
		}
		std::vector<uint8_t> bytes;
		uint8_t buf[65536];
		size_t n = 0;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			bytes.insert(bytes.end(), buf, buf + n);
		std::fclose(f);
		return setSongBytes(bytes, path, why);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!fileBytes.empty()) {
			json_object_set_new(rootJ, "file", json_string(string::toBase64(
				(const uint8_t*) fileBytes.data(), fileBytes.size()).c_str()));
			json_object_set_new(rootJ, "path", json_string(filePath.c_str()));
		}
		const Playing& L = loaded[live.load()];
		json_t* rowsJ = json_array();
		for (int r = 0; r < ROWS; r++)
			json_array_append_new(rowsJ, json_integer(L.track[r]));
		json_object_set_new(rootJ, "rows", rowsJ);
		json_object_set_new(rootJ, "rowCount", json_integer(rows));
		json_object_set_new(rootJ, "pedal", json_boolean(sendPedal.load()));
		json_object_set_new(rootJ, "pedalUnmarked", json_boolean(fillPedal.load()));
		if (styleChoice.load() >= 0)
			json_object_set_new(rootJ, "ornamentStyle", json_integer(styleChoice.load()));
		// THE INSTRUMENT THE SCORE WINDOW WAS SHOWING, so it opens on the same one next session.
		// See ScoreFollow::part.
		const int part = scoreFollowPart(id);
		if (part >= 0)
			json_object_set_new(rootJ, "scorePart", json_integer(part));
#if CHART_AUDIO
#endif
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
#if CHART_AUDIO
#endif
		if (json_t* j = json_object_get(rootJ, "rowCount"))
			setRowCount((int) json_integer_value(j));
		json_t* pedalJ = json_object_get(rootJ, "pedal");
		sendPedal = !pedalJ || json_is_true(pedalJ);
		json_t* fillJ = json_object_get(rootJ, "pedalUnmarked");
		fillPedal = !fillJ || json_is_true(fillJ);
		// BEFORE THE SONG, which is read in the style chosen.
		json_t* styleJ = json_object_get(rootJ, "ornamentStyle");
		styleChoice = styleJ ? (int) json_integer_value(styleJ) : -1;
		json_t* fileJ = json_object_get(rootJ, "file");
		if (!json_is_string(fileJ))
			return;
		const std::vector<uint8_t> bytes = string::fromBase64(json_string_value(fileJ));
		json_t* pathJ = json_object_get(rootJ, "path");
		std::string why;
		if (!setSongBytes(bytes, json_is_string(pathJ) ? json_string_value(pathJ) : "", &why))
			return;
		// AFTER THE SONG, which lets go of any instrument remembered for the file before it. A
		// pasted module has no id yet; it keeps the instrument until onAdd.
		if (json_t* partJ = json_object_get(rootJ, "scorePart")) {
			const int part = (int) json_integer_value(partJ);
			if (id >= 0)
				scoreFollowSetPart(id, part);
			else
				pendingPart = part;
		}
		// The rows as they were left, over the guess the loading made.
		json_t* rowsJ = json_object_get(rootJ, "rows");
		if (!json_is_array(rowsJ))
			return;
		Playing& L = loaded[live.load()];
		for (int r = 0; r < ROWS && r < (int) json_array_size(rowsJ); r++) {
			const int t = (int) json_integer_value(json_array_get(rowsJ, r));
			L.track[r] = (t >= 0 && t < (int) L.line.tracks.size()) ? t : -1;
		}
		syncMutes(L);
		claimBuses(L);
		nameOutputs();
	}

	// ---- playing -----------------------------------------------------------------------------

	/** WHAT THE FILE SAID ABOUT HOW THE NOTE IS PLAYED, onto the note going out. The flags say
	what it is; nothing here decides what it sounds like — that is the performer's business, so
	that two renderers of the same note agree and either can be varied. */
	static void describe(Event& e, const GpPlayedNote& n, bool letRingThroughout) {
		const GpNote& g = n.note;
		e.string = (int8_t) math::clamp(g.string, 0, 127);
		e.fret = (int8_t) math::clamp(g.fret, -1, 127);

		uint32_t t = 0;
		// HAMMER-ON OR PULL-OFF: the file writes one mark for both, and which it is depends on
		// whether the hand went up the string or down it.
		if (g.hammer)
			t |= (n.fromMidi >= 0 && g.midi < n.fromMidi) ? Event::PULL_OFF : Event::HAMMER_ON;
		if (g.slideFlags & 0x01) t |= Event::SHIFT_SLIDE;
		if (g.slideFlags & 0x02) t |= Event::LEGATO_SLIDE;
		if (g.slideFlags & 0x04) t |= Event::SLIDE_OUT_DOWN;
		if (g.slideFlags & 0x08) t |= Event::SLIDE_OUT_UP;
		if (g.slideFlags & 0x10) t |= Event::SLIDE_IN_BELOW;
		if (g.slideFlags & 0x20) t |= Event::SLIDE_IN_ABOVE;
		// A slide the file marked without saying which kind: to the next note, which is what a
		// slide usually is.
		if (g.slide && g.slideFlags == 0)
			t |= Event::SHIFT_SLIDE;
		if (g.letRing || letRingThroughout) t |= Event::LET_RING;
		if (g.palmMute) t |= Event::PALM_MUTE;
		if (g.dead) t |= Event::DEAD_NOTE;
		if (g.ghost) t |= Event::GHOST;
		if (g.accent) t |= Event::ACCENT;
		if (g.heavyAccent) t |= Event::HEAVY_ACCENT;
		if (g.harmonic) t |= Event::HARMONIC;
		if (g.artificialHarmonic) t |= Event::ARTIFICIAL_HARMONIC;
		if (g.tapped) t |= Event::TAPPED;   // From the beat: the hand does one thing at a time.
		if (g.staccato) t |= Event::STACCATO;
		if (g.tenuto && !g.staccato) t |= Event::TENUTO;
		if (g.slurred) t |= Event::SLURRED;
		if (g.tremolo) t |= Event::TREMOLO;
		e.technique = t;

		e.vibrato = g.vibrato ? Event::VIBRATO_SLIGHT : Event::VIBRATO_NONE;
		e.grace = (n.grace == 2) ? Event::GRACE_ON
			: (n.grace == 1 ? Event::GRACE_BEFORE : Event::GRACE_NONE);

		e.strum = (int8_t) ((n.brush > 0) ? 1 : (n.brush < 0 ? -1 : 0));
		e.strumIndex = n.strumIndex;
		e.strumMs = (uint8_t) math::clamp(n.brushMs, 0, 255);
		if (n.rollToTop)
			e.technique |= Event::ROLL_TO_TOP;

		e.bendCount = (uint8_t) std::min<size_t>(4, g.bend.size());
		for (int i = 0; i < (int) e.bendCount; i++) {
			e.bendPoints[i].at =
				(uint8_t) math::clamp((int) std::lround(g.bend[(size_t) i].first * 100.f), 0, 100);
			e.bendPoints[i].cents =
				(int16_t) math::clamp((int) std::lround(g.bend[(size_t) i].second), -4800, 4800);
		}
	}

	void process(const ProcessArgs& args) override {
		playSong(args);
#if CHART_AUDIO
		playBand(args);
#endif
	}

	void playSong(const ProcessArgs& args) {
		// A note cable carries no voltage: it is a real cable so that Rack owns it, and the
		// notes travel through the bus.
		for (int r = 0; r < ROWS; r++) {
			outputs[O_TRACK + r].setChannels(1);
			outputs[O_TRACK + r].setVoltage(busFlashVolts(slot[r]));
		}
		if (!haveSong.load())
			return;
		const Playing& L = loaded[live.load()];
		if (L.line.bars.empty() || L.line.quarters <= 0.0)
			return;

		if (rewindTrigger.process(params[P_REWIND].getValue(), 0.1f, 1.f))
			rewind();

		// THE SAME TRANSPORT, ASKED FOR FROM THE SCORE WINDOW. Reading the score is exactly when
		// you want to start it, stop it and take it back to the top, and the module may be
		// anywhere on the rack — possibly behind the window. The window asks and this moves the
		// module's own button, so the two cannot end up each holding a different idea of whether
		// the music is running. See ScoreFollow::wantTransport.
		switch (scoreFollowTakeTransport(id)) {
			case 1: params[P_PLAY].setValue(1.f); break;
			case 2: params[P_PLAY].setValue(0.f); break;
			case 3: rewind(); break;
			default: break;
		}

		// STOPPED MEANS STOPPED, whatever the clock is doing: this is a transport and not a
		// mute. The beat is still published, so anything following the song knows where it was
		// stopped rather than losing it.
		// NOT CONST: the end of the piece stops the transport, and the rest of this sample has
		// to know. See below.
		bool running = params[P_PLAY].getValue() > 0.5f;
		if (running && !wasRunning)
			awaitPulse = true;
		wasRunning = running;

		// WHAT A QUARTER NOTE LASTS. From the song's own tempo map, or from the clock where one
		// is patched — and then the song's tempo changes are the clock's business rather than
		// the file's, which is what patching a clock into a player means.
		// THE SONG'S OWN TEMPO MAP, MOVED BY THE CONTROL. The whole map is scaled rather than
		// replaced, so a song that speeds up or slows down still does, in proportion. There was a
		// clock input once; it is gone, and with it the business of waiting for the first pulse.
		{
			const float wanted = params[P_TEMPO].getValue();
			const float scale = (L.openingBpm > 0.f && wanted > 0.f) ? wanted / L.openingBpm : 1.f;
			quarterSeconds = (float) gpSecondsPerQuarter(L.line, quarters) / scale;
			awaitPulse = false;
		}
		soundingBpm.store(60.f / std::fmax(0.0001f, quarterSeconds));

		if (running && !awaitPulse)
			quarters += args.sampleTime / std::fmax(0.0001f, (double) quarterSeconds);

		// THE END IS THE END: it rewinds and stops, leaving the music at the beginning and ready
		// to be played again. It used to come round to the start and carry on, which is what a
		// drum machine does and not what a piece of music does — a score has a last bar, and
		// after it the right thing to do is nothing. A passage worth hearing twice is a loop,
		// which the score window sets over the bars you choose.
		if (quarters >= L.line.quarters) {
			// UNLESS A PASSAGE IS BEING LOOPED, and that passage runs to the last bar: the loop
			// below brings the music back to its start, and stopping here would get in first. A
			// loop naming bars this piece does not have is no loop, and must not stop the piece
			// ending: the music would run on past the last bar in silence.
			int from = -1, to = -1;
			double at = 0.0, end = 0.0;
			if (!scoreFollowLoop(id, from, to, NULL) || !loopRange(L, from, to, at, end)) {
				params[P_PLAY].setValue(0.f);
				rewind();
				// AND STOPPED FOR THE REST OF THIS SAMPLE TOO, which is the whole point.
				// `running` was read at the top of this call, so clearing the parameter alone
				// leaves it true until the next one — and the music is now back at nought,
				// where the first note of the piece is waiting with its moment apparently
				// come. It sounded, and the cursor stepped past it, so the next press of play
				// began at the second note. The same fault as the one below in the firing
				// loop, arriving by a different road.
				running = false;
			}
		}

		// Which played bar the music is in. A walk rather than a search: the position moves by
		// a fraction of a beat between frames, so the answer is almost always where it was.
		if (barIndex < 0 || barIndex >= (int) L.line.bars.size())
			barIndex = 0;
		for (size_t guard = 0; guard < L.line.bars.size(); guard++) {
			const GpPlayedBar& b = L.line.bars[(size_t) barIndex];
			if (quarters >= b.startQuarters && quarters < b.startQuarters + b.quarters)
				break;
			barIndex = (barIndex + 1) % (int) L.line.bars.size();
		}
		const GpPlayedBar& bar = L.line.bars[(size_t) barIndex];
		playedBar.store(barIndex);

		// WHERE THE MUSIC HAS GOT TO, for a score window to show. The bar as the FILE numbers it,
		// since the page draws each bar once however many times it is played. See ScoreFollow.hpp.
		scoreFollowPublish(id, writtenNumber(L, bar.written),
			(bar.quarters > 0.0) ? (float) ((quarters - bar.startQuarters) / bar.quarters) : 0.f,
			running && !awaitPulse);

		// TOLD TO GO TO A BAR, which is what clicking one in the score window means. Finding a
		// passage is not the same as looping it: the first pass through that written bar is the
		// place meant, as it is for a loop.
		{
			const int wanted = scoreFollowTakeGoTo(id);
			if (wanted >= 0) {
				for (size_t i = 0; i < L.line.bars.size(); i++) {
					if (writtenNumber(L, L.line.bars[i].written) != wanted)
						continue;
					quarters = L.line.bars[i].startQuarters;
					barIndex = (int) i;
					pedalIndex = -1;
					for (int r = 0; r < ROWS; r++)
						reseek[r].store(true);
					break;
				}
			}
		}

		// GOING ROUND A FEW BARS, asked for from the window so a passage can be heard over and
		// over while it is being worked on. The window names written bars; which played bars
		// those are is this module's business, and the FIRST pass through them is the one meant —
		// looping a repeated section would otherwise jump between its passes.
		{
			int from = -1, to = -1;
			bool seek = false;
			double at = -1.0, end = -1.0;
			if (scoreFollowLoop(id, from, to, &seek) && loopRange(L, from, to, at, end)) {
				if (seek || quarters < at || quarters >= end) {
					quarters = at;
					barIndex = 0;
					pedalIndex = -1;
					for (int r = 0; r < ROWS; r++)
						reseek[r].store(true);
				}
			}
		}

		// THE BEAT AND THE BAR ON EVERY CABLE, and no chords: what the harmony is comes from the
		// notes, and that is a later phase. `valid` says so, so nothing takes silence for a
		// chord it can play against.
		//
		// A BEAT HERE IS A QUARTER NOTE. A song changes metre from one bar to the next — White
		// Room has bars of five among its fours — and a count that changed its unit with the
		// signature would jump. The signature itself is published beside it, so a reader that
		// wants bars has them.
		const GpMasterBar& written = L.song.masterBars[(size_t) std::min(bar.written,
			(int) L.song.masterBars.size() - 1)];
		Harmony h;
		// NO CHORDS, BUT A BEAT. A transcription is notes and bar lines: what it does not know is
		// the harmony by name, and what it knows perfectly well is where the beat is.
		h.valid = false;
		h.timingValid = true;
		h.beat = quarters;
		// AND HOW HARD IT SWINGS, so anything playing alongside divides the beat the same way.
		// The amount is the knob; the two ratios are what it means at this tempo, worked out
		// once here from the measured curve so two modules cannot convert it differently.
		h.swing = params[P_SWING].getValue();
		swingRatios(h.swing, 60.f / std::fmax(0.0001f, (float) quarterSeconds),
			h.swingEighth, h.swingSixteenth);
		// THE KEY, WHICH THE SCORE SAYS ON EVERY BAR. The chords are not known — a transcription
		// is notes, not symbols — but the key is written down, and a module choosing pitches
		// would far rather have it than nothing. Fifths round the circle: nought is C major and
		// its relative minor, and each step of seven semitones is one more sharp.
		{
			const int fifths = written.keyAccidentals;
			const int tonic = written.minor ? ((fifths * 7 + 9) % 12 + 12) % 12
				: ((fifths * 7) % 12 + 12) % 12;
			h.key.tonic = (int8_t) tonic;
			h.key.minor = written.minor;
			h.keyValid = true;
		}
		// WHAT IS SOUNDING AND HOW MUCH IT MATTERS. See pitchWeight.
		{
			float most = 0.f;
			for (int pc = 0; pc < 12; pc++)
				most = std::fmax(most, pitchWeight[pc]);
			if (most > 0.01f) {
				for (int pc = 0; pc < 12; pc++) {
					h.pitchWeight[pc] = (uint8_t) math::clamp(
						(int) std::lround(pitchWeight[pc] / most * 255.f), 0, 255);
				}
				h.pitchesValid = true;
				h.bassPitch = (bassMidi >= 0) ? (int8_t) (((bassMidi % 12) + 12) % 12) : (int8_t) -1;
			}
		}
		h.cycleBeats = (float) L.line.quarters;
		h.barBeats = (uint8_t) math::clamp(written.beatsPerBar, 1, 255);
		h.barUnit = (uint8_t) math::clamp(written.beatUnit, 1, 255);
		h.bar = barIndex;
		h.beatInBar = (float) (quarters - bar.startQuarters);
		h.epoch = epoch;
		h.holding = !running || awaitPulse;
		for (int r = 0; r < ROWS; r++) {
			if (slot[r] >= 0)
				busPublishHarmony(slot[r], h);
		}

		// THE PEDAL, as state and not as an event, because that is what a pedal is: a module
		// that starts listening halfway through a pedalled bar has to know the dampers are up
		// now, not when the foot next moves. See NoteBus.hpp.
		//
		// WHICH SIDE OF THE MARKS THE MUSIC IS ON, kept by walking rather than searched for: the
		// playhead moves a fraction of a beat between samples, so the answer is nearly always
		// the one from last time, and a seek backwards walks back.
		{
			// AND WITH THE UNMARKED PASSAGES PEDALLED, this source decides the pedal throughout,
			// up as well as down: see Bus::pedalDecided.
			const bool filling = fillPedal.load() && L.line.pedalsFilledHere;
			const float down = (running && sendPedal.load())
				? gpPedalAt(filling ? L.line.pedalsFilled : L.line.pedals, quarters, bar.bpm, pedalIndex)
				: 0.f;
			const bool decided = sendPedal.load() && filling;
			for (int r = 0; r < ROWS; r++) {
				if (slot[r] >= 0)
					busPublishPedals(slot[r], down, 0.f, decided);
			}
		}

		// WHAT INSTRUMENT EACH CABLE IS CARRYING, published when it changes rather than every
		// sample: it changes when a song is loaded and at no other time.
		if (partChange != L.partChange) {
			partChange = L.partChange;
			for (int r = 0; r < ROWS; r++) {
				const int t = L.track[r];
				if (slot[r] < 0)
					continue;
				Instrument p;
				if (t >= 0 && t < (int) L.song.tracks.size()) {
					const GpTrack& track = L.song.tracks[(size_t) t];
					p.valid = true;
					p.program = track.midiProgram;
					p.percussion = track.percussion;
					p.pan = track.pan;
					p.capo = track.capo;
					p.stringCount = (uint8_t) std::min<size_t>(Instrument::MAX_STRINGS,
						track.tuning.size());
					for (int k = 0; k < (int) p.stringCount; k++)
						p.tuning[k] = (uint8_t) math::clamp(track.tuning[(size_t) k], 0, 127);
					p.setName(track.name);
				}
				p.change = partChange;
				busPublishInstrument(slot[r], p);
			}
		}

		// EVERY NOTE WHOSE MOMENT HAS COME. A note carries its own length, so there is no
		// note-off to keep track of: the far end is told how long it lasts when it starts.
		// A ROW GIVEN ANOTHER TRACK starts where the music is, not at the track's beginning.
		for (int r = 0; r < ROWS; r++) {
			if (!reseek[r].exchange(false))
				continue;
			const int t = L.track[r];
			cursor[r] = 0;
			if (t >= 0 && t < (int) L.line.tracks.size()) {
				const std::vector<GpPlayedNote>& notes = L.line.tracks[(size_t) t];
				while (cursor[r] < notes.size() && notes[cursor[r]].startQuarters < quarters)
					cursor[r]++;
			}
		}

		// THE SET FADES over about a beat, so it follows the music instead of accumulating it.
		// A beat rather than a fixed time, so it keeps its meaning when the tempo changes.
		{
			const float tau = std::fmax(0.05f, quarterSeconds);
			const float keep = std::exp(-args.sampleTime / tau);
			for (int pc = 0; pc < 12; pc++)
				pitchWeight[pc] *= keep;
			if (bassMidi >= 0 && quarters - bassWhen > 2.0)
				bassMidi = -1;      // Two beats with nothing lower: the bass has moved on.
		}

		bool struck = false;
		for (int r = 0; r < ROWS; r++) {
			const int t = L.track[r];
			if (t < 0 || t >= (int) L.line.tracks.size() || slot[r] < 0)
				continue;
			const std::vector<GpPlayedNote>& notes = L.line.tracks[(size_t) t];
			const bool muted = params[P_MUTE + r].getValue() > 0.5f;
#if CHART_AUDIO
			// THE GAIN ON THE CABLE, where the row goes out by its cable rather than through the
			// band: the notes leave that much softer or louder. Not on a row the band is playing,
			// whose gain is applied to the band's own sound and would otherwise count twice.
			const float cableGain = outputs[O_TRACK + r].isConnected()
				? params[P_GAIN + r].getValue() : 1.f;
#else
			const float cableGain = 1.f;
#endif
			// NOTHING SOUNDS WHILE THE TRANSPORT IS STOPPED, which wants saying out loud
			// because the clock being stopped is not enough on its own.
			//
			// THE CLOCK ONLY MOVES WHEN THE MUSIC IS RUNNING, but this loop ran every sample
			// whatever the transport was doing — and a note written at the very start satisfies
			// "has its moment come" the instant the position is nought. So loading a score, or
			// rewinding one, sounded its first note immediately and stepped the cursor past it.
			// Pressing play then began at the second note. Misty opens with a two-eighth pickup
			// and the pickup was heard on the rewind and never again.
			while (running && !awaitPulse && cursor[r] < notes.size()
					&& swung(notes[cursor[r]].startQuarters, written) <= quarters) {
				const GpPlayedNote& n = notes[cursor[r]];
				cursor[r]++;
				// THE ROW'S LIGHT FLASHES MUTED OR NOT, so a muted row shows what would be heard
				// were it turned back on.
				rowLamp[r] = 1.f;

				// WHAT THIS NOTE IS WORTH TO THE HARMONY, counted BEFORE the mute and whether or
				// not the row is muted.
				//
				// A muted row is a row that drives something without being heard, and that is
				// exactly how a score is used as a source of harmony for a generative module:
				// patch it to one, mute it, and the module improvises over music that is not
				// itself sounding. The notes stop at the mute; what they imply does not.
				{
					const int pc = ((n.note.midi % 12) + 12) % 12;
					pitchWeight[pc] += gpHarmonyWeight(L.line, L.song, n);
					// THE LOWEST SOUNDING, which says what the harmony is standing on. Taken
					// when a lower note arrives, or when the one we had has gone stale.
					if (bassMidi < 0 || n.note.midi <= bassMidi || quarters - bassWhen > 1.0) {
						bassMidi = n.note.midi;
						bassWhen = quarters;
					}
				}

				if (muted)
					continue;
				Event e;
				e.kind = Event::ON;
				e.handle = mintHandle();
				// Volts, an octave to the volt, with nought at middle C.
				e.pitch = (float) (n.note.midi - 60) / 12.f;
				e.level = math::clamp(n.dynamic * cableGain, 0.f, 1.f);
				// The length at the tempo it is being played at, which is the clock's when one
				// is patched and the file's when none is.
				//
				// AND A SWUNG NOTE KEEPS ITS WRITTEN END. Moving a start without its end is
				// exactly what swing is: the first half of a divided beat grows and the second
				// shrinks, which is the whole shape of it. Measured to where the note was
				// written to stop rather than to where it now starts, so a late off-beat does
				// not run over the next beat. See swung.
				const double ends = swung(n.startQuarters + n.lengthQuarters, written);
				const double lasts = ends - swung(n.startQuarters, written);
				e.duration = math::clamp((float) std::fmax(0.02, lasts) * quarterSeconds,
					MIN_SECONDS, MAX_SECONDS);
				describe(e, n, L.song.tracks[(size_t) t].letRingThroughout);
				// A SLUR ONLY MEANS LEGATO WHERE THERE IS SOMETHING TO BE LEGATO TO. The mark
				// says a note runs into the one after it, and a renderer holds it on a little
				// past its end for that reason — but where the next note in this part does not
				// begin until well after this one has stopped, the sound was meant to break and
				// holding it over would smear a rest. The engraver's slur is believed about
				// phrasing; about silence, the notes themselves are.
				if ((e.technique & Event::SLURRED) && cursor[r] < notes.size()) {
					const double ends = n.startQuarters + n.lengthQuarters;
					const double gap = notes[cursor[r]].startQuarters - ends;
					if (gap * (double) quarterSeconds > LEGATO_GAP)
						e.technique &= ~(uint32_t) Event::SLURRED;
				}
				busPush(slot[r], e);
				struck = true;
			}
		}

		beatLamp = struck ? 1.f : std::fmax(0.f, beatLamp - args.sampleTime * 4.f);
		lights[L_BEAT].setBrightness(beatLamp);
		for (int r = 0; r < ROWS; r++) {
			lights[L_ROW + r].setBrightness(rowLamp[r]);
			rowLamp[r] = std::fmax(0.f, rowLamp[r] - args.sampleTime * 4.f);
		}
	}
};


// ---- the panel ---------------------------------------------------------------------------------

/** The names of the tracks one column of rows carries, drawn beside the jacks. It is part of the
panel rather than of the layout because it changes with the song.

TWO LINES AT MOST, broken between words. What does not fit ends in an ellipsis; the whole name is
in the jack's tooltip. */
struct SongRows : widget::OpaqueWidget {
	SongModule* module = NULL;
	int first = 0;
	float rowY[ROWS_PER_COLUMN] = {};
	/** Where the names start, in pixels from the display's left edge: further right while the
	audio controls are shown in front of them. */
	float textX = 0.f;

	int rowAt(float y) const {
		const float half = (ROWS_PER_COLUMN > 1) ? (rowY[1] - rowY[0]) / 2.f : box.size.y;
		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			if (std::fabs(y - rowY[i]) < half)
				return i;
		}
		return -1;
	}

	/** A PRESS ON A NAME OPENS THE SONG'S TRACKS, so a row can carry any of them rather than
	the one with the most notes it was given. */
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module
				&& module->haveSong.load() && e.pos.x >= textX) {
			const int i = rowAt(e.pos.y);
			if (i >= 0) {
				showTracks(first + i);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void showTracks(int r) {
		SongModule* m = module;
		if (r >= m->rows)
			return;
		const Playing& L = m->loaded[m->live.load()];
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Row %d plays", r + 1)));
		menu->addChild(createCheckMenuItem("Nothing", "",
			[=]() { return m->loaded[m->live.load()].track[r] < 0; },
			[=]() { m->chooseTrack(r, -1); }));
		for (size_t t = 0; t < L.song.tracks.size(); t++) {
			const GpTrack& track = L.song.tracks[t];
			std::string name = string::trim(track.name);
			if (name.empty())
				name = string::f("Track %d", (int) t + 1);
			const int which = (int) t;
			menu->addChild(createCheckMenuItem(
				string::f("%d. %s, %s", which + 1, name.c_str(), instrumentName(track).c_str()), "",
				[=]() { return m->loaded[m->live.load()].track[r] == which; },
				[=]() { m->chooseTrack(r, which); }));
		}
	}

	/** As much of the text as fits in the width, with an ellipsis if any was cut. */
	static std::string fit(NVGcontext* vg, const std::string& text, float width) {
		if (nvgTextBounds(vg, 0.f, 0.f, text.c_str(), NULL, NULL) <= width)
			return text;
		static const std::string DOTS = "\u2026";
		std::string cut = text;
		while (!cut.empty()) {
			cut.pop_back();
			// Never leave half of a multi-byte character.
			while (!cut.empty() && ((unsigned char) cut.back() & 0xC0) == 0x80)
				cut.pop_back();
			if (!cut.empty() && ((unsigned char) cut.back() & 0xC0) == 0xC0)
				cut.pop_back();
			const std::string tried = string::trim(cut) + DOTS;
			if (nvgTextBounds(vg, 0.f, 0.f, tried.c_str(), NULL, NULL) <= width)
				return tried;
		}
		return DOTS;
	}

	/** Breaks the name into at most two lines, whole words on the first. */
	static void wrap(NVGcontext* vg, const std::string& text, float width,
			std::string& one, std::string& two) {
		one.clear();
		two.clear();
		std::vector<std::string> words;
		std::string word;
		for (char c : text) {
			if (c == ' ') {
				if (!word.empty())
					words.push_back(word);
				word.clear();
			}
			else
				word += c;
		}
		if (!word.empty())
			words.push_back(word);
		size_t used = 0;
		for (; used < words.size(); used++) {
			const std::string tried = one.empty() ? words[used] : one + " " + words[used];
			if (nvgTextBounds(vg, 0.f, 0.f, tried.c_str(), NULL, NULL) > width)
				break;
			one = tried;
		}
		if (one.empty()) {
			// The first word alone is wider than the column.
			one = fit(vg, text, width);
			return;
		}
		for (size_t w = used; w < words.size(); w++)
			two += (two.empty() ? "" : " ") + words[w];
		if (!two.empty())
			two = fit(vg, two, width);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		float lineHeight = 0.f;
		nvgTextMetrics(vg, NULL, NULL, &lineHeight);

		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			std::string name = module ? module->rowName(first + i) : "";
			if (name.empty())
				name = "\u2014";
			std::string one, two;
#if CHART_AUDIO
			// THE TRACK'S NAME, AND UNDER IT ITS INSTRUMENT, each on a line of its own.
			const float width = box.size.x - textX;
			one = fit(vg, name, width);
			if (module)
				two = fit(vg, module->rowInstrument(first + i), width);
#else
			wrap(vg, name, box.size.x, one, two);
#endif
			nvgFillColor(vg, PANEL_INK);
			for (int pass = 0; pass < 2; pass++) {
				if (two.empty())
					nvgText(vg, textX, rowY[i], one.c_str(), NULL);
				else {
					nvgText(vg, textX, rowY[i] - lineHeight / 2.f, one.c_str(), NULL);
					nvgText(vg, textX, rowY[i] + lineHeight / 2.f, two.c_str(), NULL);
				}
			}
		}
	}
};


/** The song's name, and a press opens the file chooser. */
struct SongTitle : widget::OpaqueWidget {
	SongModule* module = NULL;
	/** Where the LOAD button sits, in this widget's own pixels, so the word beside it follows it
	if the button is moved in the layout. Negative before it has been told. */
	float loadCx = -1.f;

	/** THE WHOLE PLATE OPENS THE SCORE, and nothing else does.
	
	Loading used to be this: a click anywhere on the name opened a file chooser. That left nowhere
	to click for the thing wanted far more often — looking at the music — and made a plate you
	learn not to touch. Loading is the green button now.

	WITH NO SONG, A CLICK DOES NOTHING. There is no score to show, and opening a file chooser
	instead would be answering a question that was not asked; the LOAD button is sitting there in
	plain sight for somebody who wanted that. */
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (module && module->haveSong.load())
				showScore();
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	void showScore() {
		if (!module)
			return;
		const Playing& L = module->loaded[module->live.load()];
		if (L.song.tracks.empty())
			return;
		// A MUSICXML FILE IS SHOWN AS ITS WRITER WROTE IT. Writing the song back out is a
		// conversion, and for a piano not a faithful one: the model has one staff per track, so
		// the two hands become two separate parts and everything written between them — the
		// pedalling, the octave shifts — is not in either. The module is holding the document it
		// loaded, since a patch carries the score inside it, so the window can simply have it.
		if (module->fileIsMusicXML && !module->fileBytes.empty()) {
			px::scoreWindowShowDocument(std::make_shared<const std::string>(module->fileBytes),
				L.title.empty() ? std::string("Score") : L.title, module->id);
			return;
		}
		// COPIED FOR THE WINDOW: the module can load another file, or be deleted, while it is up.
		std::shared_ptr<const GpSong> carried = std::make_shared<const GpSong>(L.song);
		// THE INSTRUMENT THE FIRST ROW IS PLAYING, which is what the patch is already carrying.
		int track = 0;
		for (int r = 0; r < ROWS; r++) {
			if (L.track[r] >= 0) {
				track = L.track[r];
				break;
			}
		}
		px::scoreWindowShowSong(carried, track,
			L.title.empty() ? std::string("Score") : L.title, module->id);
	}

	/** Loads one, and says so when it will not. */
	void loadPath(const std::string& path) {
		if (!module)
			return;
		std::string why;
		if (module->setSongFile(path, &why)) {
			px::rememberFolder("score", path);
			return;
		}
		// A FILE THAT HAS GONE COMES OFF THE LIST. Offering it a second time would be offering
		// to fail again.
		if (!system::isFile(path))
			px::forgetFile("score", path);
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
			("That file could not be read.\n\n" + why).c_str());
	}

	/** THE LOAD BUTTON OPENS A MENU rather than a chooser.

	WHAT IS BEING PLAYED IS USUALLY WHAT WAS PLAYED A MOMENT AGO — a passage is marked up, heard,
	marked up again — and walking a file chooser to the same place every time is the long way
	round. The chooser is still the first item; the twenty most recent are under it.

	THE NAME AND THE FOLDER IT IS IN, because a dozen movements of the same sonata are a dozen
	files with nearly the same name, and the folder is what tells them apart. One that has been
	moved or deleted is shown greyed rather than hidden: it says where something went rather
	than quietly losing it. */
	void offerLoad() {
		if (!module)
			return;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuItem("Open a score\u2026", "", [=]() { choose(); }));
		const std::vector<std::string>& recent = px::recentFiles("score");
		if (recent.empty())
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Recent"));
		for (size_t i = 0; i < recent.size(); i++) {
			const std::string path = recent[i];
			std::string name = path, folder;
			const size_t slash = name.find_last_of('/');
			if (slash != std::string::npos) {
				folder = name.substr(0, slash);
				name = name.substr(slash + 1);
				const size_t up = folder.find_last_of('/');
				if (up != std::string::npos)
					folder = folder.substr(up + 1);
			}
			const bool there = system::isFile(path);
			ui::MenuItem* item = createMenuItem(name, there ? folder : "missing",
				[=]() { loadPath(path); });
			item->disabled = !there;
			menu->addChild(item);
		}
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Forget the recent list", "",
			[]() { px::forgetAll("score"); }));
	}

	void choose() {
		if (!module)
			return;
		// NO FILTER ON A MAC. Its dialog does not match a file by extension but by the type the
		// system has recorded for it, and those records are not to be trusted for Guitar Pro files:
		// once something has claimed Guitar Pro's types (MuseScore does) and the system has
		// labelled the files with a type it does not itself define, a .gp file matches neither its
		// extension nor that type's name, and is hidden, from the dialog's search too. So every file
		// is offered, and one that is not a score is refused when it is read, with the reason.
		// Elsewhere the dialog matches extensions and the filter does what it says.
#if defined ARCH_MAC
		osdialog_filters* filters = NULL;
#else
		osdialog_filters* filters = osdialog_filters_parse(
			"Scores:gp,gpx,gp5,gp4,gp3,gpif,xml,musicxml,mxl");
#endif
		// WHERE SCORES ARE KEPT, not where the patch was last saved. See LastFolder.hpp.
		const std::string start = px::lastFolderFor("score", module->filePath);
		char* path = osdialog_file(OSDIALOG_OPEN, start.empty() ? NULL : start.c_str(),
			NULL, filters);
		if (filters)
			osdialog_filters_free(filters);
		if (!path)
			return;
		const std::string picked = path;
		std::free(path);
		loadPath(picked);
	}

	/** WHERE THE SONG IS, all in one plate: its name, and how far through it the band has got.

	The count used to be a bare strip of panel underneath, which read as a second readout about
	something else. It belongs with the name — they are one fact — and the room it takes inside
	the frame is room the name was not using. */
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
		nvgFillColor(vg, nvgRGB(0x2a, 0x2f, 0x36));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0xcf, 0xcf, 0xcf, 0x90));
		nvgStrokeWidth(vg, 1.3f);
		nvgStroke(vg);

		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		const bool have = module && module->haveSong.load();

		// THE BOTTOM OF THE PLATE IS THE COUNT'S, whether or not there is a count to put in it,
		// so the name sits in the same place before and after a song is loaded.
		const float footer = mm2px(7.f);
		const float nameH = std::fmax(mm2px(6.f), box.size.y - footer);

		std::string text = have
			? module->loaded[module->live.load()].title : std::string("LOAD A SONG");
		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		float size = 10.f;
		float bounds[4] = {0.f, 0.f, 0.f, 0.f};
		for (; size > 5.f; size -= 0.5f) {
			nvgFontSize(vg, size);
			nvgTextBoxBounds(vg, 0.f, 0.f, box.size.x - 6.f, text.c_str(), NULL, bounds);
			if (bounds[3] - bounds[1] <= nameH - 4.f)
				break;
		}
		nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		const float h = bounds[3] - bounds[1];
		for (int pass = 0; pass < 2; pass++)
			nvgTextBox(vg, 3.f, std::round((nameH - h) / 2.f), box.size.x - 6.f,
				text.c_str(), NULL);

		// THE BAR COUNT, bottom left. NO TEMPO HERE: the readout below the plate already says
		// what the tempo is, and a number that appears twice on one panel is a number somebody
		// has to check against itself.
		const float footY = box.size.y - footer / 2.f - mm2px(0.4f);
		if (have) {
			const Playing& L = module->loaded[module->live.load()];
			if (!L.line.bars.empty()) {
				const int at = math::clamp(module->playedBar.load(), 0,
					(int) L.line.bars.size() - 1);
				const GpPlayedBar& bar = L.line.bars[(size_t) at];
				// AS THE PAGE NUMBERS THEM, which is how the loop beside it, the score window and
				// the menu all count: a pickup is bar nought.
				// THE HIGHEST NUMBER IN THE PIECE, which is not always the last bar's: an
				// editor can leave the final bar out of the count.
				int last = 0;
				for (int b = 0; b < (int) L.song.masterBars.size(); b++)
					last = std::max(last, SongModule::writtenNumber(L, b));
				std::string said = string::f("BAR %d OF %d",
					SongModule::writtenNumber(L, bar.written), last);
				if (bar.pass > 1)
					said += string::f("  (%d)", bar.pass);
				// AND WHAT IT IS GOING ROUND, since the loop goes on after the score window is
				// closed and nothing else on the rack would say so. Left off where it would run
				// into the LOAD caption; the module's menu says it in full either way. WHETHER IT
				// FITS IS DECIDED BY THE WIDEST COUNT THE PIECE CAN SHOW, not the one showing now,
				// or it would come and go as the count changed width going round the loop.
				int from = -1, to = -1;
				if (module->looping(from, to)) {
					int most = 1;
					for (const GpPlayedBar& b : L.line.bars)
						most = std::max(most, b.pass);
					std::string widest = string::f("BAR %d OF %d", last, last);
					if (most > 1)
						widest += string::f("  (%d)", most);
					const std::string loop = (to > from)
						? string::f("   LOOP %d–%d", from, to) : string::f("   LOOP %d", from);
					nvgFontFaceId(vg, font->handle);
					nvgFontSize(vg, 8.f);
					float room = box.size.x - mm2px(2.f);
					if (loadCx > 0.f) {
						nvgFontSize(vg, 7.f);
						room = loadCx - mm2px(3.6f) - nvgTextBounds(vg, 0.f, 0.f, "LOAD", NULL, NULL)
							- mm2px(2.f) - mm2px(1.f);
						nvgFontSize(vg, 8.f);
					}
					if (nvgTextBounds(vg, 0.f, 0.f, (widest + loop).c_str(), NULL, NULL) <= room)
						said += loop;
				}
				nvgFontSize(vg, 8.f);
				nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				for (int pass = 0; pass < 2; pass++)
					nvgText(vg, mm2px(2.f), footY, said.c_str(), NULL);
			}
		}

		// THE BUTTON'S CAPTION, just left of it. The button itself is a panel control sitting on
		// top of this plate; only the word belongs to the plate.
		if (loadCx > 0.f) {
			nvgFontSize(vg, 7.f);
			nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xcc));
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			for (int pass = 0; pass < 2; pass++)
				nvgText(vg, loadCx - mm2px(3.6f), footY, "LOAD", NULL);
		}
	}
};




/** A ROW: a mute, the gain, the MPX jack, the audio jack, then the name, a millimetre between
each. The gain sets whichever of the two jacks the row goes out by — the notes on its MPX cable when
that is patched, its sound in the band when not — so it sits ahead of both. With the audio hidden
the audio jack goes and the name moves up into its place. The same in each column of either
module. */
static const float MUTE_D = 5.6f;
static const float MUTE_DX = 1.5f + MUTE_D / 2.f;
static const float GAIN_DX = MUTE_DX + MUTE_D / 2.f + 1.f + 6.05f / 2.f;
static const float JACK_DX = GAIN_DX + 6.05f / 2.f + 1.f + 8.03f / 2.f;
static const float AUDIO_DX = JACK_DX + 8.03f / 2.f + 1.f + 8.03f / 2.f;
/** THE ROW'S LAMP, below the gap between the mute and the gain, at about forty-five degrees from
each: down and right of the mute, down and left of the knob. Not by the MPX jack, where the pointer
covered it while the knob was being turned. */
static const float LAMP_DX = (MUTE_DX + GAIN_DX) / 2.f;
static const float LAMP_DY = (6.05f / 2.f + 0.6f + 1.1f) * 0.7071f;
/** Where the names start with the audio hidden, and how much further right with it shown. */
static const float NAME_DX = JACK_DX + 8.03f / 2.f + 1.f;
static const float NAME_SHIFT = AUDIO_DX + 8.03f / 2.f + 1.f - NAME_DX;
/** The rows close enough together to leave room at the bottom for the band's stereo pair. */
static const float ROW_TOP = 57.f;
static const float ROW_STEP = 10.5f;
static const float MIX_Y = 121.5f;

/** SIX ROWS IN ONE COLUMN IS TWELVE HP — the transport above needs the width, and the names take
the rest. TWELVE ROWS IN TWO COLUMNS IS FIFTEEN, which is what the panel measured once the per-row
audio jacks were taken off it. */
static float panelHP(int rows) { return (rows > ROWS_PER_COLUMN) ? 15.f : 12.f; }
static float panelW(int rows) { return panelHP(rows) * 5.08f; }
static int columnsFor(int rows) { return (rows > ROWS_PER_COLUMN) ? 2 : 1; }

static Layout songLayout(int rows) {
	const int COLUMNS = columnsFor(rows);
	const float PANEL_W = panelW(rows);
	const float PANEL_HP = panelHP(rows);
	Layout L;
	L.hp = PANEL_HP;
	L.title = CHART_TITLE;

	auto label = [&](const char* key, float x, float y, const char* text, Panel::Align align,
			bool heading, float size, const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = align; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};

	// The song's name, which is also the way one is chosen. Clear of the band the module's own
	// name is written in, which ends seven millimetres down: the band is seventeen PIXELS deep
	// below a four-pixel margin. Read as millimetres, it left a sixth of the panel empty.
	// ONE PLATE, HOLDING BOTH. The name and the bar count used to be two things — a framed plate
	// and a bare strip of panel under it — which read as two unrelated readouts rather than as
	// one place where the song is. Taller, with the count inside it, and the whole of it is the
	// way to the score: see SongTitle.
	//
	// It stops short of the transport at 35.5 with four millimetres to spare.
	Item title;
	title.key = "d.title"; title.kind = Item::DISPLAY;
	title.x = 3.f; title.y = 10.f; title.w = PANEL_W - 6.f; title.h = 21.f;
	L.items.push_back(title);

	// LOADING IS ITS OWN BUTTON NOW, in the bottom right of the plate.
	//
	// It used to be the plate itself: clicking anywhere on the name opened the file chooser. That
	// left nowhere to click for the thing wanted far more often — looking at the music — and a
	// plate that opens a file dialogue when you touch it is a plate you learn not to touch.
	//
	// GREEN, because it is the one control on an otherwise dark plate and it has to be findable
	// without being hunted for.
	Item load;
	load.key = "p.load"; load.kind = Item::PARAM; load.id = SongModule::P_LOAD;
	load.style = "button.green"; load.diameter = 5.f;
	load.x = PANEL_W - 9.5f; load.y = 27.f;
	L.items.push_back(load);
	// ITS CAPTION IS DRAWN INSIDE THE PLATE, beside the button, rather than under it on the
	// panel: under it is where the reset jack already is on the narrow version. See SongTitle.

	// The transport, the tempo, and the two jacks that can drive both from outside.
	Item play;
	play.key = "p.play"; play.kind = Item::PARAM; play.id = SongModule::P_PLAY;
	play.style = "transport.play"; play.x = 10.f; play.y = 36.f;
	L.items.push_back(play);
	label("p.play.label", 10.f, 42.f, "PLAY", Panel::CENTRE, true, 0.f, "p.play");

	Item rewind;
	rewind.key = "p.rewind"; rewind.kind = Item::PARAM; rewind.id = SongModule::P_REWIND;
	rewind.style = "transport.rewind"; rewind.x = 21.f; rewind.y = 36.f;
	L.items.push_back(rewind);
	label("p.rewind.label", 21.f, 42.f, "REWIND", Panel::CENTRE, true, 0.f, "p.rewind");

	// A READOUT RATHER THAN A KNOB, as on mpxChart: a tempo is a number people say out loud,
	// and a knob's position is a poor way to read one. Clicking the plate opens the list.
	Item tempo;
	tempo.key = "p.tempo"; tempo.kind = Item::PARAM; tempo.id = SongModule::P_TEMPO;
	tempo.style = "readout"; tempo.x = 34.f; tempo.y = 35.5f; tempo.chars = 3; tempo.h = 6.5f;
	L.items.push_back(tempo);
	label("p.tempo.label", 34.f, 42.f, "BPM", Panel::CENTRE, true, 0.f, "p.tempo");



	// SWING, beside the tempo: both are how the music is being played rather than what it says,
	// and the beat is divided by one and counted by the other.
	Item swing;
	swing.key = "p.swing"; swing.kind = Item::PARAM; swing.id = SongModule::P_SWING;
	swing.x = 49.f; swing.y = 35.5f;
	L.items.push_back(swing);
	label("p.swing.label", 49.f, 42.f, "SWING", Panel::CENTRE, true, 0.f, "p.swing");

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = SongModule::L_BEAT;
	lamp.x = 3.5f; lamp.y = 36.f;
	L.items.push_back(lamp);

	// THE ROWS, in columns of six. The names are drawn by the module, since they change with
	// the song; the audio jacks and their heading are shown only with the audio.
	for (int c = 0; c < COLUMNS; c++) {
		const float x = c * (PANEL_W / COLUMNS);
		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			const int r = c * ROWS_PER_COLUMN + i;
			if (r >= rows)
				break;
			const float y = ROW_TOP + i * ROW_STEP;
			Item mute;
			mute.key = string::f("p.mute%d", r + 1);
			mute.kind = Item::PARAM; mute.id = SongModule::P_MUTE + r;
			mute.style = "latch"; mute.diameter = MUTE_D; mute.x = x + MUTE_DX; mute.y = y;
			L.items.push_back(mute);

			Item gain;
			gain.key = string::f("p.gain%d", r + 1);
			gain.kind = Item::PARAM; gain.id = SongModule::P_GAIN + r;
			gain.style = "knob.trim"; gain.x = x + GAIN_DX; gain.y = y;
			L.items.push_back(gain);

			Item lamp;
			lamp.key = string::f("lamp.row%d", r + 1);
			lamp.kind = Item::LIGHT; lamp.id = SongModule::L_ROW + r;
			lamp.x = x + LAMP_DX; lamp.y = y + LAMP_DY;
			L.items.push_back(lamp);

			Item out;
			out.key = string::f("out.track%d", r + 1);
			out.kind = Item::PORT_OUT; out.id = SongModule::O_TRACK + r;
			out.x = x + JACK_DX; out.y = y; out.ring = NOTE_CABLE;
			L.items.push_back(out);

		}
		// SEVEN POINTS, each over its own column. At eight, MUTE reached within a quarter of a
		// millimetre of MPX once the jack moved up to the mute.
		const float headY = ROW_TOP - 7.f;
		label(c ? "h.mute2" : "h.mute", x + MUTE_DX, headY, "MUTE", Panel::CENTRE, true, 7.f,
			NULL);
		label(c ? "h.gain2" : "h.gain", x + GAIN_DX, headY, "GAIN", Panel::CENTRE, true, 7.f,
			NULL);
		label(c ? "h.out2" : "h.out", x + JACK_DX, headY, "MPX", Panel::CENTRE, true, 7.f, NULL);

		Item names;
		names.key = c ? "d.rows2" : "d.rows"; names.kind = Item::DISPLAY;
		names.x = x + NAME_DX; names.y = ROW_TOP - ROW_STEP / 2.f;
		names.w = PANEL_W / COLUMNS - NAME_DX - 1.f; names.h = ROWS_PER_COLUMN * ROW_STEP;
		L.items.push_back(names);
	}

	// THE WHOLE BAND, left and right, under the last column's audio jacks: the right one under
	// them, and the left one far enough before it for its letter.
	const float rightX = PANEL_W - PANEL_W / COLUMNS + AUDIO_DX + (COLUMNS > 1 ? 0.f : 12.5f);
	const float leftX = rightX - 12.5f;
	label("h.mix", leftX - 8.03f / 2.f - 1.f, MIX_Y, "MIX", Panel::RIGHT, true, 8.f, NULL);
	Item mixL;
	mixL.key = "out.mixl"; mixL.kind = Item::PORT_OUT; mixL.id = SongModule::O_MIX_L;
	mixL.x = leftX; mixL.y = MIX_Y; mixL.ring = SIG_AUDIO;
	L.items.push_back(mixL);
	label("out.mixl.label", leftX + 8.03f / 2.f + 1.f, MIX_Y, "L", Panel::LEFT, true, 8.f,
		"out.mixl");
	Item mixR;
	mixR.key = "out.mixr"; mixR.kind = Item::PORT_OUT; mixR.id = SongModule::O_MIX_R;
	mixR.x = rightX; mixR.y = MIX_Y; mixR.ring = SIG_AUDIO;
	L.items.push_back(mixR);
	label("out.mixr.label", rightX + 8.03f / 2.f + 1.f, MIX_Y, "R", Panel::LEFT, true, 8.f,
		"out.mixr");

	L.bindOffsets();
	return L;
}


struct SongWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	/** SPACE STARTS AND STOPS IT while the pointer is over the module, as it does over the
	score window. A field being typed into keeps its space without anything here arranging it:
	Rack offers a key to the selected widget first and only turns it into a hover key when that
	one does not take it, so a text field with the cursor in it has already consumed the space
	before this is asked. */
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_SPACE && (e.mods & RACK_MOD_MASK) == 0
				&& module) {
			Param& play = module->params[SongModule::P_PLAY];
			play.setValue(play.getValue() > 0.5f ? 0.f : 1.f);
			e.consume(this);
			e.stopPropagating();
			return;
		}
		ModuleWidget::onHoverKey(e);
	}
	SongTitle* titleDisplay = NULL;
	SongRows* columns[2] = {};
	/** How many rows the panel is currently showing, so a change of the setting is noticed. */
	int rowsShown = 0;
	/** The LOAD button's value last frame, so a press is seen once. See step. */
	float lastLoad = 0.f;

	/** WATCHING THE FILE, so that editing it is heard without touching the module.

	A score is marked up by editing the file — dynamics, hairpins, slurs, articulation — and the
	only way to tell whether a marking was right is to hear it. Going back to the module and
	choosing the file again between every edit makes that loop long enough that nobody follows a
	musical idea through it. So the module watches the file it loaded, and reads it again when it
	changes: save the file and the next bar is already playing the new version.

	WHEN IT WAS LAST WRITTEN, and when it was last looked at. Looking costs a stat, which is
	nothing, but there is no reason to do it every frame. */
	/** WHEN IT WAS LAST WRITTEN AND HOW BIG IT WAS. Both, because the time a file system reports
	is only good to the second on some of them, and two saves inside one second would otherwise
	look like none. */
	long long fileWhen = 0;
	long long fileSize = -1;
	double lookedAt = 0.0;

	SongWidget(SongModule* module) {
		setModule(module);
		build(module ? module->rows : DEFAULT_ROWS);
	}

	/** THE WHOLE PANEL, FOR A GIVEN NUMBER OF ROWS, and again when that number changes.

	A module that can be six rows or twelve is two panels, not one panel with some of it hidden:
	the width differs, the rows are in one column or two, and the name displays are a different
	height. So the widget is built from nothing each time, which is what a constructor does
	anyway — this is that constructor, callable twice.

	RACK DOES NOT MOVE THE NEIGHBOURS when a module changes width, so growing one may overlap
	whatever is to its right until it is dragged clear. Shrinking leaves a gap. */
	void build(int rows) {
		rowsShown = math::clamp(rows, ROWS_PER_COLUMN, MAX_ROWS);
		SongModule* module = dynamic_cast<SongModule*>(this->module);
		clearChildren();
		panel = NULL;
		titleDisplay = NULL;
		columns[0] = columns[1] = NULL;
		layout = songLayout(rowsShown);
		layoutApplyUser(CHART_SLUG, layout);
		box.size.x = mm2px(panelW(rowsShown));
		panel = new Panel;
		addChild(panel);

		titleDisplay = new SongTitle;
		titleDisplay->module = module;
		layoutPlaceDisplay(this, layout, "d.title", titleDisplay);
		// WHERE THE WORD "LOAD" GOES, taken from where the button actually is rather than
		// written down twice: move the button in the layout and the word follows it.
		if (const Item* plate = layout.find("d.title")) {
			if (const Item* button = layout.find("p.load"))
				titleDisplay->loadCx = mm2px(button->x - plate->x);
		}


		// The names line up with the jacks, in each display's own coordinates.
		for (int c = 0; c < columnsFor(rowsShown); c++) {
			SongRows* rows = new SongRows;
			columns[c] = rows;
			rows->module = module;
			rows->first = c * ROWS_PER_COLUMN;
			const char* key = c ? "d.rows2" : "d.rows";
			const Item* area = layout.find(key);
			for (int i = 0; i < ROWS_PER_COLUMN; i++) {
				const Item* jack = layout.find(string::f("out.track%d", rows->first + i + 1));
				const float y = jack ? jack->y : ROW_TOP + i * ROW_STEP;
				rows->rowY[i] = mm2px(y - (area ? area->y : ROW_TOP - ROW_STEP / 2.f));
			}
			layoutPlaceDisplay(this, layout, key, rows);
		}

		layoutBuild(this, panel, layout);
		if (panel)
			panel->box.size.x = box.size.x;
		// PUT BACK WHERE IT WAS, now that it is a different width: Rack wants telling, or the
		// module is left overlapping its neighbour with no way to be dragged out.
		if (module && APP->scene && APP->scene->rack && parent)
			APP->scene->rack->setModulePosForce(this, box.pos);
	}

	/** A score window belongs to the module that opened it and goes when that module goes. */
	void onRemove(const RemoveEvent& e) override {
		if (module)
			px::scoreWindowCloseFor(module->id);
		ModuleWidget::onRemove(e);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, CHART_SLUG);
		SongModule* m = dynamic_cast<SongModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		if (m->haveSong.load()) {
			menu->addChild(createMenuItem("Show the score", "", [=]() {
				if (titleDisplay)
					titleDisplay->showScore();
			}));
		}
		if (!m->filePath.empty())
			menu->addChild(createMenuLabel(m->filePath));
		// THE WAY OUT OF A LOOP WITH THE WINDOW CLOSED. The loop outlives the window, so the
		// module has to be able to stop it on its own.
		{
			int from = -1, to = -1;
			if (m->looping(from, to)) {
				menu->addChild(createMenuItem(to > from
					? string::f("Stop looping bars %d to %d", from, to)
					: string::f("Stop looping bar %d", from), "", [=]() {
					scoreFollowSetLoop(m->id, -1, -1, false);
				}));
			}
		}

		// A MUSICXML FILE FROM SOMEWHERE ELSE, which is how the engraving was built and is still
		// the way to read a score this plugin did not load.
		menu->addChild(createMenuItem("Show a MusicXML file…", "", [=]() {
			osdialog_filters* filters = osdialog_filters_parse(
				"MusicXML:musicxml,xml,mxl");
			// THE SAME FOLDER SCORES ARE LOADED FROM. See LastFolder.hpp.
			const std::string start = px::lastFolderFor("score", m->filePath);
			char* chosen = osdialog_file(OSDIALOG_OPEN, start.empty() ? NULL : start.c_str(),
				NULL, filters);
			osdialog_filters_free(filters);
			if (!chosen)
				return;
			const std::string path = chosen;
			std::free(chosen);
			px::rememberFolder("score", path);
			std::string title = path;
			const size_t slash = title.find_last_of('/');
			if (slash != std::string::npos)
				title = title.substr(slash + 1);
			px::scoreWindowShow(path, title, m->id);
		}));
		menu->addChild(new ui::MenuSeparator);
		// SIX ROWS OR TWELVE. Twelve puts a second column beside the first, each row keeping its
		// mute, its gain, its name and its MPX output, and the panel grows by three units.
		menu->addChild(createSubmenuItem("Rows", string::f("%d", m->rows), [=](ui::Menu* sub) {
			const int counts[] = {6, 12};
			for (int c : counts)
				sub->addChild(createCheckMenuItem(string::f("%d rows", c), "",
					[=]() { return m->rows == c; }, [=]() { m->setRowCount(c); }));
		}));
		// HOW THE ORNAMENTS ARE PLAYED: what the score was taken for and why, and the styles to
		// choose instead. See OrnamentStyle.hpp and docs/ornament-styles.md.
		if (m->haveSong.load()) {
			const Playing& L = m->loaded[m->live.load()];
			if (L.hasStyle) {
				const StyleGuess guess = L.styleGuess;
				menu->addChild(createSubmenuItem("Ornaments played as",
					ornamentStyleName(L.song.ornamentStyle), [=](ui::Menu* sub) {
						std::string said = guess.found
							? std::string(ornamentStyleName(guess.style)) + ", because " + guess.because
							: std::string("Later Romantic, because ") + guess.because;
						sub->addChild(createMenuLabel(said));
						sub->addChild(createCheckMenuItem("As worked out from the score", "",
							[=]() { return m->styleChoice.load() < 0; },
							[=]() { m->restyle(-1); }));
						sub->addChild(new ui::MenuSeparator);
						for (int s = 0; s < STYLE_COUNT; s++)
							sub->addChild(createCheckMenuItem(ornamentStyleName(s), "",
								[=]() { return m->styleChoice.load() == s; },
								[=]() { m->restyle(s); }));
					}));
			}
		}
		menu->addChild(createCheckMenuItem("Send the score's sustain pedal", "",
			[=]() { return m->sendPedal.load(); },
			[=]() { m->sendPedal = !m->sendPedal.load(); }));
		menu->addChild(createCheckMenuItem("Pedal the passages the score leaves unmarked", "",
			[=]() { return m->fillPedal.load(); },
			// The place in the list is a hint the walk corrects from anywhere, so it is not reset
			// from this thread.
			[=]() { m->fillPedal = !m->fillPedal.load(); }));
	}

	/** Magenta means the link works: see mpxIn, which says why at length. */
	void step() override {
		ModuleWidget::step();
		// THE ROW COUNT, CHANGED FROM THE MENU OR LOADED WITH A PATCH: the panel is built again.
		if (SongModule* sm = dynamic_cast<SongModule*>(module)) {
			if (sm->rows != rowsShown)
				build(sm->rows);
		}
#if CHART_AUDIO
		// In the module browser there is no module. The synthesiser runs while a cable can hear
		// it, which is checked every frame since one can be plugged in at any time.
		if (SongModule* sm = dynamic_cast<SongModule*>(module))
			sm->updateBand();
#endif
		if (!module)
			return;

		// THE FILE ITSELF, WATCHED. See fileWhen.
		{
			SongModule* sm = dynamic_cast<SongModule*>(module);
			const double now = system::getTime();
			if (sm && !sm->filePath.empty() && now - lookedAt > 0.5) {
				lookedAt = now;
				struct stat about;
				if (::stat(sm->filePath.c_str(), &about) == 0) {
					const long long written = (long long) about.st_mtime;
					const long long big = (long long) about.st_size;
					if (fileSize < 0) {
						fileWhen = written;     // The first look is not a change.
						fileSize = big;
					}
					else if (written != fileWhen || big != fileSize) {
						fileWhen = written;
						fileSize = big;
						std::string why;
						// A FAILURE IS SILENT HERE. A file being written is a file half there,
						// and complaining about it would put a dialogue on the screen every time
						// an editor saves. The next look finds it whole.
						sm->setSongFile(sm->filePath, &why);
					}
				}
			}
		}

		// THE LOAD BUTTON, WATCHED RATHER THAN WIRED. A panel button is a parameter, and a
		// parameter cannot open a file chooser — that has to happen where the window is. So the
		// press is noticed here, on its rise, which is the same way mpxChart opens its chart.
		{
			const float now = module->params[SongModule::P_LOAD].getValue();
			if (now > 0.5f && lastLoad <= 0.5f && titleDisplay)
				titleDisplay->offerLoad();
			lastLoad = now;
		}

		for (int r = 0; r < ROWS; r++) {
			PortWidget* port = getOutput(SongModule::O_TRACK + r);
			if (!port)
				continue;
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace CHART_NS
} // namespace px


Model* CHART_MODEL = createModel<px::CHART_NS::SongModule, px::CHART_NS::SongWidget>(CHART_SLUG);
