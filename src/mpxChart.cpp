#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "ChartLayout.hpp"
#include "IReal.hpp"

#include <osdialog.h>

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace px {


/** One song in the library: what it is called, and the text it came from.

THE CHUNK IS THE STORAGE FORMAT. A song's own slice of the iReal payload re-parses in
microseconds, so nothing about the parsed chart is ever written down — no cells serialised, no
version to migrate, and no way for the stored form to disagree with the parser. It is also
small enough to keep inside a patch, which is what makes a patch open on a machine that has
never seen your playlists. */
struct LibraryEntry {
	std::string playlist;
	std::string title;
	std::string composer;
	std::string style;
	std::string chunk;
};

static std::vector<LibraryEntry> gLibrary;
static bool gLibraryLoaded = false;


static std::string libraryPath() {
	return asset::user("DreamerMPX/charts.json");
}


static void libraryLoad() {
	gLibraryLoaded = true;
	FILE* file = std::fopen(libraryPath().c_str(), "r");
	if (!file)
		return;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!rootJ)
		return;
	json_t* songsJ = json_object_get(rootJ, "songs");
	if (json_is_array(songsJ)) {
		size_t i;
		json_t* songJ;
		json_array_foreach(songsJ, i, songJ) {
			LibraryEntry e;
			auto str = [&](const char* key) {
				json_t* j = json_object_get(songJ, key);
				return json_is_string(j) ? std::string(json_string_value(j)) : std::string();
			};
			e.playlist = str("playlist");
			e.title = str("title");
			e.composer = str("composer");
			e.style = str("style");
			e.chunk = str("chunk");
			if (!e.chunk.empty())
				gLibrary.push_back(e);
		}
	}
	json_decref(rootJ);
}


static void librarySave() {
	json_t* songsJ = json_array();
	for (const LibraryEntry& e : gLibrary) {
		json_t* songJ = json_object();
		json_object_set_new(songJ, "playlist", json_string(e.playlist.c_str()));
		json_object_set_new(songJ, "title", json_string(e.title.c_str()));
		json_object_set_new(songJ, "composer", json_string(e.composer.c_str()));
		json_object_set_new(songJ, "style", json_string(e.style.c_str()));
		json_object_set_new(songJ, "chunk", json_string(e.chunk.c_str()));
		json_array_append_new(songsJ, songJ);
	}
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "songs", songsJ);
	system::createDirectories(asset::user("DreamerMPX"));
	FILE* file = std::fopen(libraryPath().c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(1));
		std::fclose(file);
	}
	json_decref(rootJ);
}


/** Reads an iReal Pro HTML export into the library. Returns how many songs were added, and
replaces any playlist of the same name rather than growing a second copy of it. */
static int libraryImport(const std::string& path) {
	if (!gLibraryLoaded)
		libraryLoad();

	std::string html;
	{
		FILE* file = std::fopen(path.c_str(), "rb");
		if (!file)
			return 0;
		char buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0)
			html.append(buf, n);
		std::fclose(file);
	}

	const std::string payload = irealPayloadFromHtml(html);
	if (payload.empty())
		return 0;

	// The songs are kept as their own chunks, so the library holds what the file held.
	std::string name;
	std::vector<std::string> chunks;
	{
		size_t i = 0;
		while (true) {
			const size_t at = payload.find("===", i);
			if (at == std::string::npos) {
				chunks.push_back(payload.substr(i));
				break;
			}
			chunks.push_back(payload.substr(i, at - i));
			i = at + 3;
		}
		if (chunks.size() > 1) {
			name = chunks.back();
			chunks.pop_back();
		}
	}
	if (name.empty())
		name = system::getStem(path);

	gLibrary.erase(std::remove_if(gLibrary.begin(), gLibrary.end(),
		[&](const LibraryEntry& e) { return e.playlist == name; }), gLibrary.end());

	int added = 0;
	for (const std::string& chunk : chunks) {
		const Song song = irealParseSong(chunk);
		if (song.title.empty() || song.cells.empty())
			continue;
		LibraryEntry e;
		e.playlist = name;
		e.title = song.title;
		e.composer = song.composer;
		e.style = song.style;
		e.chunk = chunk;
		gLibrary.push_back(e);
		added++;
	}
	librarySave();
	return added;
}


struct ChartModule : Module, NoteSource {
	enum ParamId {
		P_TRANSPOSE,
		P_TEMPO,
		P_PLAY,
		P_REWIND,
		/** How the chords are written. A PARAMETER rather than a setting of the display, so it
		is saved with the patch, shows up in Rack's own menu, and can be mapped — and so that it
		is plainly part of the patch rather than a preference hidden in a window. */
		P_CHORD_MODE,
		P_OPEN,
		NUM_PARAMS
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		/** THE CHART, FOR A RACK THAT KNOWS NOTHING OF MPX.

		Everything below is an ordinary voltage that ordinary modules already understand, so the
		chart drives a patch built entirely out of other people's modules. Rack has no way of
		carrying a chord AS A CHORD — nothing transmits "D minor seven, two beats left" — but it
		has two well-worn ways of carrying the notes, and this speaks both. */
		O_CHORD,      /**< The chord's tones as polyphonic V/Oct. */
		O_ROOT,       /**< Its root, on its own. */
		O_PES_CHORD,  /**< The chord as a Poly External Scale. */
		O_PES_SCALE,  /**< The key as a Poly External Scale. */
		NUM_OUTPUTS
	};
	enum LightId {
		L_BEAT,
		NUM_LIGHTS
	};

	int slot = -1;
	uint32_t generation = 0;
	double beats = 0.0;
	dsp::SchmittTrigger clockTrigger, resetTrigger, rewindTrigger;
	float beatSeconds = 0.5f;
	float sinceLastPulse = 0.f;

	/** WHAT IS LOADED, IN TWO COPIES. Choosing a song happens on the drawing thread while the
	audio thread is reading the one already there, so the new one is built beside it and the
	index is then moved. Nothing is freed, so a reader part way through the old one finishes
	against memory that is still its own. */
	struct Loaded {
		Song song;
		Expansion expansion;
		/** The same chart laid out AS WRITTEN, for reading, and the order it is played in.
		Both come from the written bars, so what is heard and what is lit cannot disagree. */
		std::vector<ChartBar> chartBars;
		ChartPlayback playback;
		/** The same walk with only one section's bars kept, or a copy of the whole when no
		section is chosen. This is what is actually played. */
		ChartPlayback playing;
	};
	Loaded loaded[2];
	std::atomic<int> live{0};
	std::atomic<bool> haveSong{false};
	/** Kept so the patch can carry the song rather than a reference to one. */
	std::string chunk;
	std::string playlist;
	std::atomic<int> playingSpan{-1};
	/** WHICH WRITTEN BAR IS SOUNDING, for the window to light, and which time round it is.
	The bar is an index into chartBars, so the same bar lights again on the second pass — which
	is what a repeat means and what somebody reading along expects to see. */
	std::atomic<int> playingBar{-1};
	std::atomic<int> playingPass{0};
	/** THE CHORD ACTUALLY SOUNDING, packed into one word so the drawing thread can read it
	without a lock.

	Not the chord written in the bar. A measure holding a repeat mark has no chord of its own
	and the ear is hearing the one before it, so a readout that shows the bar's own slots shows
	nothing exactly when the music is at its plainest. This is what came out of the resolver,
	which is what is being played. */
	std::atomic<int32_t> soundingChord{-1};
	/** THE TEMPO ACTUALLY IN FORCE, for the readout — which is not always the knob. With a
	clock patched the knob is ignored and the rate is whatever the clock is doing, so the
	readout follows the clock and the knob stops being the answer. */
	std::atomic<float> soundingBpm{120.f};

	static int32_t packChord(const Chord& chord) {
		return (int32_t) ((chord.valid ? 1 : 0) << 24)
			| ((chord.degree & 0xff) << 16)
			| (((chord.accidental + 1) & 0xff) << 8)
			| (chord.quality & 0xff);
	}

	static Chord unpackChord(int32_t packed) {
		Chord out;
		if (packed < 0)
			return out;
		out.valid = ((packed >> 24) & 0xff) != 0;
		out.degree = (int8_t) ((packed >> 16) & 0xff);
		out.accidental = (int8_t) (((packed >> 8) & 0xff) - 1);
		out.quality = (uint8_t) (packed & 0xff);
		return out;
	}
	/** WHICH SECTION IS CHOSEN, as its letter, nought being the whole chart.

	A letter rather than a range of bars: a letter recurs, and choosing A means every A. It is
	also what survives a save, since the bars are rebuilt from the chart but the letter still
	names the same music. */
	char section = 0;

	/** How the chords are written: 0 letters, 1 degrees. Read from the parameter, so there is
	one answer rather than two that can differ. */
	int chordMode() {
		return params[P_CHORD_MODE].getValue() > 0.5f ? 1 : 0;
	}

	ChartModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_TRANSPOSE, -11.f, 11.f, 0.f, "Transpose", " semitones");
		paramQuantities[P_TRANSPOSE]->snapEnabled = true;
		// JUST "TEMPO". The name is read in a pop-up beside the pointer while the knob is
		// being turned, and a caveat in brackets is clutter there — at that moment the reader
		// wants the number, not a note about a case that may not even apply. That the clock
		// input overrides this belongs in the manual and in the clock port's own name.
		configParam(P_TEMPO, 30.f, 300.f, 120.f, "Tempo", " bpm");
		configSwitch(P_PLAY, 0.f, 1.f, 1.f, "Play", {"Stopped", "Playing"});
		configButton(P_REWIND, "Rewind to the start");
		// LETTER NAMES is the term, not "chord symbols": a Roman numeral IS a chord symbol,
		// so calling one of the two by the name of both would say nothing. The pair a musician
		// uses is letter names against Roman numerals — C minor seven against two minor seven.
		configSwitch(P_CHORD_MODE, 0.f, 1.f, 0.f, "Chord symbols",
			{"Letter names", "Roman numerals"});
		configButton(P_OPEN, "Open the chart");
		configInput(I_CLOCK, "Clock, which overrides the tempo knob");
		configInput(I_RESET, "Reset");
		configOutput(O_MPX, "MPX note out");
		configOutput(O_CHORD, "Chord tones as polyphonic V/Oct");
		configOutput(O_ROOT, "Root as V/Oct");
		configOutput(O_PES_CHORD, "Notes of the current chord, as a Poly External Scale");
		configOutput(O_PES_SCALE, "Notes of the key, as a Poly External Scale");
		slot = busClaim(&generation);
	}

	~ChartModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	void onReset() override {
		beats = 0.0;
	}

	/** THE PLAY HEAD MOVES TO A MEASURE, which is what clicking one in the chart asks for.

	A written bar can be played several times over, so the one meant is the next occurrence at
	or after where the music is now — reading along and pressing a bar ahead of the cursor
	should go forward to it rather than back to its first pass half a chorus ago. */
	void jumpToBar(int bar) {
		const Loaded& L = current();
		const ChartPlayback& pb = L.playing;
		if (pb.timeline.empty() || pb.totalBeats <= 0.f)
			return;
		const double here = std::fmod(std::fmod(beats, (double) pb.totalBeats)
			+ pb.totalBeats, (double) pb.totalBeats);
		int best = -1;
		for (size_t i = 0; i < pb.timeline.size(); i++) {
			if (pb.timeline[i].bar != bar)
				continue;
			if (best < 0)
				best = (int) i;                       // the first, as a fallback
			if (pb.timeline[i].startBeat >= here) {
				best = (int) i;                       // the next one from here
				break;
			}
		}
		if (best < 0)
			return;
		beats = pb.timeline[best].startBeat;
		playingIndex = best;
	}

	/** Back to the top AND STOPPED, which is what pressing rewind means on any transport: you
	are not asking to hear the first bar go by, you are putting the tape back to the start.

	The reset JACK is a different thing and does not stop. It is a sync signal — something in
	the patch saying "here is the top of the form" — and a clock that reset the music and then
	silenced it would be useless. */
	void rewindAndStop() {
		params[P_PLAY].setValue(0.f);
		rewind();
	}

	/** Back to the top. Safe from the drawing thread: the audio thread reads these each block
	and a beat count set to nought part way through one is a beat count set to nought. */
	void rewind() {
		beats = 0.0;
		playingIndex = 0;
	}

	/** Main thread. Restricts play to one section's label, or to the whole chart for nought.

	The filtered walk is built into the copy that is NOT being read and then swapped in, the same
	way a new song is — the audio thread must never see a half-built timeline. */
	void setSection(char letter) {
		const int liveNow = live.load();
		const int spare = 1 - liveNow;
		loaded[spare] = loaded[liveNow];
		loaded[spare].playing = chartPlaybackForLabel(loaded[spare].chartBars,
			loaded[spare].playback, letter);
		section = letter;
		live.store(spare);
		beats = 0.0;
		playingIndex = 0;
	}

	/** Main thread. Parses the chunk into the copy that is not being read, then moves over. */
	void setSong(const std::string& newChunk, const std::string& fromPlaylist) {
		const int spare = 1 - live.load();
		loaded[spare].song = irealParseSong(newChunk);
		loaded[spare].expansion = irealExpand(loaded[spare].song);
		loaded[spare].chartBars = chartLayout(loaded[spare].song);
		loaded[spare].playback = chartPlayback(loaded[spare].chartBars);
		loaded[spare].playing = loaded[spare].playback;
		chunk = newChunk;
		playlist = fromPlaylist;
		live.store(spare);
		haveSong.store(!loaded[spare].playback.timeline.empty());
		// A new song knows nothing of the old one's sections.
		section = 0;
		// AND IT ARRIVES STOPPED, AT THE TOP. A chart swapped under a running transport
		// carries on from wherever the beat count happened to be, in the middle of a piece
		// nobody has looked at yet — and the harmony it publishes changes key and chord in the
		// same instant. Loading a chart is the start of reading it, not of playing it.
		rewindAndStop();
	}

	const Loaded& current() {
		return loaded[live.load()];
	}

	Key playingKey() {
		Key key = current().song.key;
		const int shift = (int) std::round(params[P_TRANSPOSE].getValue());
		key.tonic = (int8_t) (((key.tonic + shift) % 12 + 12) % 12);
		return key;
	}

	/** The key the chart SOUNDS in, which is the written key moved by the transpose. */
	Key soundingKey() {
		return playingKey();
	}

	/** Puts the chart into a named key by working out the transpose it needs. Whole semitones
	only, and the nearest way round, so the knob never runs to its end for a small move. */
	void setSoundingTonic(int pitchClass) {
		const int from = ((current().song.key.tonic % 12) + 12) % 12;
		int shift = ((pitchClass - from) % 12 + 12) % 12;
		if (shift > 6)
			shift -= 12;
		params[P_TRANSPOSE].setValue((float) shift);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		// THE SONG ITSELF, not a reference to one. A patch that pointed at a library entry
		// would open to silence on a machine that had never imported that playlist.
		json_object_set_new(rootJ, "chunk", json_string(chunk.c_str()));
		json_object_set_new(rootJ, "playlist", json_string(playlist.c_str()));
		if (section != 0) {
			const char letter[2] = {section, 0};
			json_object_set_new(rootJ, "section", json_string(letter));
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Patches written before the mode became a parameter.
		if (json_t* modeJ = json_object_get(rootJ, "chordMode"))
			params[P_CHORD_MODE].setValue(json_integer_value(modeJ) ? 1.f : 0.f);
		json_t* chunkJ = json_object_get(rootJ, "chunk");
		json_t* playlistJ = json_object_get(rootJ, "playlist");
		if (json_is_string(chunkJ) && json_string_value(chunkJ)[0] != '\0') {
			setSong(json_string_value(chunkJ),
				json_is_string(playlistJ) ? json_string_value(playlistJ) : "");
			// After the song, since the sections come from it. A letter this chart does not
			// have leaves the whole chart playing rather than nothing.
			json_t* sectionJ = json_object_get(rootJ, "section");
			if (json_is_string(sectionJ) && json_string_value(sectionJ)[0] != '\0')
				setSection(json_string_value(sectionJ)[0]);
		}
	}

	/** Where the walk left off, so finding the played bar costs a step rather than a search. */
	int playingIndex = 0;

	/** THE CHORD SOUNDING at `within` beats into played bar `at`, and how long until the next.

	A bar's slots divide it equally, which is what a chart means by writing two chords in one
	measure. A measure-repeat mark carries no chord of its own, so the answer comes from the bar
	it stands for — walked back through the PLAYED order rather than the written one, because
	"the previous bar" means the one you just heard. */
	bool chordAt(const Loaded& L, int at, float within, Chord& out, float& toNext) {
		const ChartPlayback& pb = L.playing;
		if (at < 0 || at >= (int) pb.timeline.size())
			return false;

		int steps = 0;
		int here = at;
		float offset = within;
		while (steps++ < 64) {
			const ChartBar& bar = L.chartBars[pb.timeline[here].bar];
			const int count = (int) bar.slots.size();
			bool simile = false;
			for (const ChartSlot& slot : bar.slots)
				simile = simile || slot.simile != ChartSlot::SIMILE_NONE;

			if (count > 0 && !simile) {
				const float share = (float) bar.beats / (float) count;
				int k = (int) (offset / std::fmax(0.001f, share));
				k = clamp(k, 0, count - 1);
				// A blank slot holds whatever was sounding before it.
				while (k > 0 && bar.slots[k].empty)
					k--;
				if (bar.slots[k].empty || !bar.slots[k].chord.valid)
					return false;
				out = bar.slots[k].chord;
				toNext = share * (float) (k + 1) - offset;
				return true;
			}
			// A simile: ask the bar before it, at the same place within the bar.
			here = (here - 1 + (int) pb.timeline.size()) % (int) pb.timeline.size();
			offset = std::fmin(offset, (float) L.chartBars[pb.timeline[here].bar].beats - 0.01f);
		}
		return false;
	}

	/** The chord `ahead` changes from now, for the two the harmony carries as what is coming. */
	void chordAhead(const Loaded& L, int at, float within, int ahead, Chord& out,
		float& toNext, bool& noChord) {

		const ChartPlayback& pb = L.playing;
		Chord current;
		float ignore = 0.f;
		if (!chordAt(L, at, within, current, ignore))
			return;
		out = current;
		int found = 0;
		int here = at;
		float offset = within;
		for (int guard = 0; guard < 512 && found < ahead; guard++) {
			const ChartBar& bar = L.chartBars[pb.timeline[here].bar];
			const int count = std::max(1, (int) bar.slots.size());
			const float share = (float) bar.beats / (float) count;
			offset += share;
			if (offset >= (float) bar.beats - 0.001f) {
				here = (here + 1) % (int) pb.timeline.size();
				offset = 0.f;
			}
			Chord next;
			if (!chordAt(L, here, offset, next, ignore))
				continue;
			if (next.degree != out.degree || next.accidental != out.accidental
				|| next.quality != out.quality) {
				out = next;
				found++;
			}
		}
		(void) toNext;
		(void) noChord;
	}

	/** A Poly External Scale: twelve channels, one per semitone from C, nought volts for a
	semitone that is out and eight for one that is in, with ten on the tonic if it is known.
	Aria Salvatrice's format, which several quantizers read. */
	void writePES(int outputId, const int* pitchClasses, int count, int tonic) {
		Output& out = outputs[outputId];
		out.setChannels(12);
		for (int i = 0; i < 12; i++)
			out.setVoltage(0.f, i);
		for (int i = 0; i < count; i++)
			out.setVoltage(8.f, ((pitchClasses[i] % 12) + 12) % 12);
		if (tonic >= 0)
			out.setVoltage(10.f, ((tonic % 12) + 12) % 12);
	}

	/** The chord as pitches, voiced close: the root in the octave that starts at nought volts,
	and every tone above it placed above the one before, which is how a chord is played rather
	than how a set of pitch classes is listed. */
	void writeChord(const Chord& chord, const Key& key) {
		int classes[8];
		const int count = chordPitchClasses(chord, key, classes);
		outputs[O_CHORD].setChannels(std::max(1, count));
		const int root = chordRootPitchClass(chord, key);
		float last = -10.f;
		for (int i = 0; i < count; i++) {
			float v = (float) (((classes[i] - root) % 12 + 12) % 12) / 12.f + root / 12.f;
			while (v <= last)
				v += 1.f;
			last = v;
			outputs[O_CHORD].setVoltage(v, i);
		}
		if (count == 0)
			outputs[O_CHORD].setVoltage(0.f, 0);
		outputs[O_ROOT].setChannels(1);
		outputs[O_ROOT].setVoltage((float) root / 12.f);

		writePES(O_PES_CHORD, classes, count, root);

		int scale[7];
		scalePitchClasses(key, scale);
		writePES(O_PES_SCALE, scale, 7, key.tonic);
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		if (slot < 0 || !haveSong.load())
			return;

		if (resetTrigger.process(inputs[I_RESET].getVoltage(), 0.1f, 1.f))
			rewind();
		if (rewindTrigger.process(params[P_REWIND].getValue(), 0.1f, 1.f))
			rewindAndStop();

		// STOPPED MEANS STOPPED, whatever the clock is doing. It is a transport rather than a
		// mute: nothing advances while it is off.
		//
		// But the harmony HOLDS. Everything downstream still knows the chord you stopped on,
		// which is what you want when you stop mid-take to patch something — going silent would
		// leave anything following the changes with nothing to follow. So the publishing below
		// carries on and only the clock stands still.
		const bool running = params[P_PLAY].getValue() > 0.5f;

		sinceLastPulse += args.sampleTime;
		if (!running) {
			// Held where it is. The clock's own timing is still followed so that starting again
			// picks up in step rather than at whatever phase the button was pressed.
			sinceLastPulse = std::fmin(sinceLastPulse, 10.f);
			clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f);
		}
		else if (inputs[I_CLOCK].isConnected()) {
			if (clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f)) {
				if (sinceLastPulse > 0.001f && sinceLastPulse < 10.f)
					beatSeconds = sinceLastPulse;
				sinceLastPulse = 0.f;
				beats = std::floor(beats) + 1.0;
			}
			else if (beatSeconds > 0.f) {
				beats += args.sampleTime / beatSeconds;
			}
		}
		else {
			// Taken from the knob whether or not the transport is running, so the readout is
			// right while stopped.
			beatSeconds = 60.f / std::fmax(1.f, params[P_TEMPO].getValue());
			if (running)
				beats += args.sampleTime / beatSeconds;
		}
		soundingBpm.store(60.f / std::fmax(0.0001f, beatSeconds));

		const Loaded& L = loaded[live.load()];
		const ChartPlayback& pb = L.playing;
		if (pb.timeline.empty() || pb.totalBeats <= 0.f)
			return;

		const double length = pb.totalBeats;
		const double pos = std::fmod(std::fmod(beats, length) + length, length);

		// Which played bar the music is in. A walk rather than a search: the position moves by
		// a fraction of a beat between frames, so the answer is almost always where it was.
		int at = playingIndex;
		if (at < 0 || at >= (int) pb.timeline.size())
			at = 0;
		for (int guard = 0; guard < (int) pb.timeline.size(); guard++) {
			if (pos >= pb.timeline[at].startBeat && pos < pb.timeline[at].endBeat)
				break;
			at = (at + 1) % (int) pb.timeline.size();
		}
		playingIndex = at;

		const ChartBar& bar = L.chartBars[pb.timeline[at].bar];
		const float within = (float) (pos - pb.timeline[at].startBeat);

		Harmony h;
		h.valid = true;
		h.key = playingKey();
		float toNext = 0.f;
		if (!chordAt(L, at, within, h.current, toNext))
			return;
		float ignore = 0.f;
		bool ignoreNC = false;
		chordAhead(L, at, within, 1, h.next, ignore, ignoreNC);
		chordAhead(L, at, within, 2, h.after, ignore, ignoreNC);
		h.beatsToNext = toNext;
		h.beat = pos;
		h.cycleBeats = (float) length;
		h.barBeats = (uint8_t) bar.beats;
		h.barUnit = (uint8_t) (L.song.unit > 0 ? L.song.unit : 4);
		h.bar = at;
		h.beatInBar = within;
		busPublishHarmony(slot, h);
		// The same chord, for anything that is not an MPX module.
		writeChord(h.current, h.key);

		playingBar.store(pb.timeline[at].bar);
		soundingChord.store(packChord(h.current));
		// Which time round this written bar is: how many earlier entries of the timeline name
		// the same bar. Cheap, and only wanted for a readout.
		{
			int pass = 1;
			for (int k = 0; k < at; k++) {
				if (pb.timeline[k].bar == pb.timeline[at].bar)
					pass++;
			}
			playingPass.store(pass);
		}
		lights[L_BEAT].setBrightness(
			(running && std::fmod(pos, 1.0) < 0.25) ? 1.f : 0.f);
	}
};


/** The chart, expanded, four bars to a line.

THE EXPANDED FORM, NOT THE FOLDED ONE. Drawing repeat marks, endings and section brackets where
they belong is the seven hundred lines this deliberately does not write. Written out flat there
is nothing to lay out — a bar is a bar — and the whole of the music is on screen in the order it
is played, which for reading along is arguably better anyway. */
/** WHERE YOU ARE, on the face. Not the chart — that is in the window, and a chart small enough
to fit a module is a chart nobody can read.

What is left is what you glance at while patching: what is loaded, which section is playing, the
measure, which time round it is, and the chord sounding now. */
struct ChartDisplay : widget::OpaqueWidget {
	ChartModule* module = NULL;

	/** WHERE THE TITLE IS, AND WHERE THE KEY IS, so a press can find them. Both are strips the
	full width of the display, since a wider target is a better one — the whole line is
	clickable, not only the button beside it.

	AND EACH HAS A BUTTON, at the left edge. The lines were clickable before and looked exactly
	like the two below them that are not; a control has to say it is one. */
	static const int BTN = 13;

	math::Rect titleBox() {
		return math::Rect(math::Vec(2.f, 2.f), math::Vec(box.size.x - 4.f, 30.f));
	}

	math::Rect keyBox() {
		return math::Rect(math::Vec(2.f, 33.f), math::Vec(box.size.x - 4.f, 14.f));
	}

	math::Rect titleButton() {
		return math::Rect(math::Vec(4.f, 10.f), math::Vec(BTN, BTN));
	}

	math::Rect keyButton() {
		return math::Rect(math::Vec(4.f, 33.f), math::Vec(BTN, BTN));
	}

	void drawButton(const DrawArgs& args, const math::Rect& r) {
		nvgSave(args.vg);
		nvgTranslate(args.vg, r.pos.x, r.pos.y);
		drawRaisedButton(args.vg, r.size, false, false);
		nvgRestore(args.vg);
	}

	void onButton(const ButtonEvent& e) override;

	/** Faint under the pointer, so the two lines that can be pressed look different from the
	three that cannot. */
	void drawHover(const DrawArgs& args, const math::Rect& r) {
		if (!r.contains(APP->scene->mousePos.minus(getAbsoluteOffset(math::Vec()))))
			return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 2.f);
		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x16));
		nvgFill(args.vg);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> body =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!body || body->handle < 0)
			return;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x35, 0x3c, 0x47));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		drawHover(args, titleBox());
		if (module && module->haveSong.load()) {
			drawHover(args, keyBox());
			drawButton(args, keyButton());
		}
		drawButton(args, titleButton());

		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		if (!module || !module->haveSong.load()) {
			nvgFontFaceId(args.vg, body->handle);
			nvgFontSize(args.vg, 11.f);
			nvgFillColor(args.vg, PANEL_INK);
			nvgFontSize(args.vg, 9.f);
			nvgText(args.vg, 22.f, 16.f, "No chart loaded.", NULL);
			nvgText(args.vg, 6.f, 36.f, "Press the button to choose", NULL);
			nvgText(args.vg, 6.f, 48.f, "a chart, or import a playlist.", NULL);
			return;
		}

		const ChartModule::Loaded& L = module->current();
		const Key key = module->playingKey();

		// THE TITLE OVER TWO LINES. Eight HP is about twenty-six characters of this size, and
		// a great many titles are longer than that; broken at a space it takes almost anything,
		// and a title cut off with an ellipsis is a title you cannot read.
		nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle : body->handle);
		nvgFontSize(args.vg, 13.f);
		nvgFillColor(args.vg, PANEL_INK);
		{
			const std::string& title = L.song.title;
			const float room = box.size.x - 28.f;
			if (nvgTextBounds(args.vg, 0, 0, title.c_str(), NULL, NULL) <= room)
				nvgText(args.vg, 22.f, 13.f, title.c_str(), NULL);
			else {
				// The last space that still fits, so the break falls between words.
				size_t at = std::string::npos;
				for (size_t i = 0; i < title.size(); i++) {
					if (title[i] != ' ')
						continue;
					const std::string head = title.substr(0, i);
					if (nvgTextBounds(args.vg, 0, 0, head.c_str(), NULL, NULL) <= room)
						at = i;
					else
						break;
				}
				if (at == std::string::npos)
					at = title.size() / 2;
				nvgText(args.vg, 22.f, 13.f, title.substr(0, at).c_str(), NULL);
				nvgText(args.vg, 22.f, 27.f, title.substr(at + 1).c_str(), NULL);
			}
		}

		nvgFontFaceId(args.vg, body->handle);
		nvgFontSize(args.vg, 8.5f);
		nvgFillColor(args.vg, PANEL_INK);
		{
			char head[160];
			std::snprintf(head, sizeof(head), "%s %s  \u25be    %d/%d",
				pitchClassNameIn(key.tonic, key), key.minor ? "minor" : "major",
				L.song.beats, L.song.unit);
			nvgText(args.vg, 22.f, 41.f, head, NULL);
		}

		const int at = module->playingBar.load();
		if (at < 0 || at >= (int) L.chartBars.size())
			return;

		// What is playing, over TWO LINES: what is chosen, then where inside it. On one line
		// it ran off the edge of the display as soon as a pass number appeared, and a line that
		// only sometimes fits is a line that does not fit.
		{
			const int pass = module->playingPass.load();
			nvgFontSize(args.vg, 9.5f);
			nvgFillColor(args.vg, PANEL_INK);
			if (module->section != 0) {
				char line[32];
				std::snprintf(line, sizeof(line), "section %c", module->section);
				nvgText(args.vg, 6.f, 53.f, line, NULL);
			}
			else
				nvgText(args.vg, 6.f, 53.f, "whole chart", NULL);

			char line[48];
			if (pass > 1)
				std::snprintf(line, sizeof(line), "measure %d   pass %d", at + 1, pass);
			else
				std::snprintf(line, sizeof(line), "measure %d", at + 1);
			nvgText(args.vg, 6.f, 65.f, line, NULL);
		}

		// The chord sounding now, large — TAKEN FROM THE PLAYER, not from the bar. A measure
		// with a repeat mark in it carries no chord of its own; what you are hearing is the one
		// before, and that is what should be on the face.
		{
			std::string text;
			// THE SAME CHOICE AS THE CHART. It is one switch, so it has to reach both: the
			// readout was always spelling the chord as a letter, which meant setting the chart
			// to degrees left the face disagreeing with the window about the same chord.
			const Chord sounding = ChartModule::unpackChord(module->soundingChord.load());
			if (sounding.valid) {
				text = (module->chordMode() == 1) ? chordRoman(sounding)
					: chordLetter(sounding, key);
			}
			if (!text.empty()) {
				nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle
					: body->handle);
				// A shade smaller than it was, to pay for the second line above it.
				nvgFontSize(args.vg, 25.f);
				nvgFillColor(args.vg, PANEL_INK);
				nvgText(args.vg, 6.f, 88.f, text.c_str(), NULL);
			}
		}
	}
};


/** THE TEMPO, IN NUMBERS, above the knob that sets it.

A knob's position is a poor way to read a tempo — a few degrees is several beats a minute, and
the one thing anybody wants to know about a tempo is the number. Green because it is a reading
rather than a control: nothing on this panel is green except the things the module is telling
you.

IT SHOWS WHAT IS IN FORCE, not what the knob says. Patch a clock and the knob is ignored, so the
readout follows the clock — otherwise it would sit there confidently reporting a tempo nothing
is playing at. */
struct ChartTempoDisplay : widget::Widget {
	ChartModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.5f);
		nvgFillColor(args.vg, nvgRGB(0x0e, 0x14, 0x11));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x2a, 0x3a, 0x31));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		if (!face || face->handle < 0)
			return;

		const float bpm = module ? module->soundingBpm.load() : 120.f;
		char text[24];
		std::snprintf(text, sizeof(text), "%.1f", clamp(bpm, 0.f, 9999.f));

		// THE NUMBER, AND NOTHING ELSE. It sits directly above a knob labelled TEMPO, so
		// writing BPM after it says what the label above already says — and it was taking a
		// third of the plate to do it, which left the number itself short of room.
		nvgFontFaceId(args.vg, face->handle);
		nvgFillColor(args.vg, nvgRGB(0x3d, 0xe0, 0x7a));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFontSize(args.vg, box.size.y * 0.72f);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text, NULL);
	}
};


static void chartWindowShow(ChartModule* module);

// ---- the chart window ------------------------------------------------------------------------

/** THE CHART, AT A SIZE YOU CAN READ, in a window of its own.

Not on the module's face. A chord chart is a page: it wants to be a page wide and several inches
tall, and a module that shape is a module nobody would place. So the face carries only where you
are in the music, and the reading happens here.

A CHILD OF THE SCENE rather than of the rack, like Rack's own dialogues, so it holds still while
the rack is scrolled and zoomed underneath it. It is modeless — every control on every module
goes on working while it is open, because you set a tempo and patch a cable while reading the
chart, not instead of reading it.

EVERYTHING SCALES FROM THE BAR. A bar is a quarter of the window's width, always four to a line,
and every size below — the chord, the small quality above the baseline, the row height, the
margins — is a fraction of that. So the window is one control: drag it wider and the whole chart
grows, drag it narrower and it shrinks, and the layout never changes shape while you do it. Four
to a line rather than eight or sixteen because the barlines then run straight down the page,
which is what makes a chart readable at a glance.
*/
static const float CW_MIN_W = 320.f;
static const float CW_MIN_H = 200.f;
static const float CW_TITLE = 22.f;
static const float CW_GRIP = 14.f;
/** How far in from an edge still counts as taking hold of it. Generous, because an edge is a
line and a line is a hard thing to hit. */
static const float CW_EDGE_GRAB = 7.f;

/** WHITE, ALL OF IT, and not a soft white.

Grey on dark is what a chart looks like on paper and it is the wrong choice here. Everything
drawn in this window is being read — barlines as much as chords, because a barline is what tells
you where a measure ends — and a rule at two thirds brightness is not read, it is guessed at.
Anything worth drawing in a chart is worth drawing at full strength.

The window's own frame too: a border that has to be hunted for is not a border. */
static const NVGcolor CW_BACK = nvgRGB(0x12, 0x14, 0x18);
/** A plain dark grey, a shade up from the chart's own ground so the bar reads as a separate
strip without becoming a second colour in the window. */
static const NVGcolor CW_BAND = nvgRGB(0x2e, 0x2e, 0x2e);
static const NVGcolor CW_INK = nvgRGB(0xff, 0xff, 0xff);
static const NVGcolor CW_EDGE = CW_INK;
static const NVGcolor CW_RULE = CW_INK;
static const NVGcolor CW_SECTION = CW_INK;
/** The chosen section. THE ONE COLOUR IN THE WINDOW, and it earns it: everything else is
white, so the orange is unmistakably the answer to "what will play". */
static const NVGcolor CW_CHOSEN = nvgRGB(0xff, 0x9a, 0x2c);


/** THE TWO FACES A CHART IS SET IN.

Petaluma is Steinberg's handwritten music font, and PetalumaScript its matching text face — the
pair a hand-copied lead sheet is set in, which is what an iReal chart looks like. Shipped with
the plugin under the Open Font Licence rather than asked of the system, so the chart looks the
same on every machine.

Loaded through Rack, which caches by path, so asking for them every frame costs a lookup. */
static std::shared_ptr<window::Font> chartTextFont() {
	std::shared_ptr<window::Font> font =
		APP->window->loadFont(asset::plugin(pluginInstance, "res/PetalumaScript.otf"));
	if (font && font->handle >= 0)
		return font;
	// A missing font is not a reason to draw nothing.
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}

static std::shared_ptr<window::Font> chartMusicFont() {
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/Petaluma.otf"));
}


struct ChartWindow : widget::OpaqueWidget {
	ChartModule* module = NULL;
	/** How far the chart is scrolled, in pixels of the drawn chart. */
	float scroll = 0.f;
	/** 0 nothing, 1 moving the window, 2 resizing it. */
	int drag = 0;
	/** Which edges the resize took hold of. See edgesAt. */
	int edges = 0;
	/** How tall the whole chart came out last time it was drawn, so scrolling knows its limit
	without laying the chart out twice. */
	float contentHeight = 0.f;

	ChartWindow() {
		box.size = math::Vec(720.f, 460.f);
		box.pos = math::Vec(120.f, 80.f);
	}

	/** THE CHART'S GEOMETRY, worked out in one place.

	Drawing needs it and so does clicking, and if the two computed it separately they would
	disagree the first time either changed — which shows up as a letter that highlights under the
	pointer and does nothing when pressed. */
	struct Metrics {
		float pad, barW, rowH, chordSize, markSize, sectionSize, headH, top;
	};

	Metrics metrics() {
		Metrics m;
		m.pad = std::fmax(8.f, box.size.x * 0.022f);
		m.barW = (box.size.x - m.pad * 2.f) / 4.f;
		m.rowH = m.barW * 0.46f;
		m.chordSize = m.barW * 0.225f;
		m.markSize = m.barW * 0.135f;
		m.sectionSize = m.barW * 0.185f;
		// A LINE ABOVE THE MUSIC, holding the transport and the tune's name and nothing else.
		// Everything that CHANGES the chart — which song, which playlist, which key, how the
		// chords are written — is set on the module, where it is plainly part of the patch and
		// is saved with it. Starting and stopping is not a change to the patch, and is wanted
		// with the eye on the page.
		m.headH = 0.f;   // Filled in below, once the metrics it depends on are known.
		m.headH = std::fmax(TBTN + 10.f, m.rowH * 0.62f);
		m.top = body().pos.y + m.headH - scroll;
		return m;
	}

	/** The box a section letter is drawn in — and the only part of the chart a click can take,
	so an ordinary press anywhere else in the music does nothing at all. */
	math::Rect letterBox(const Metrics& m, int row, int col) {
		return math::Rect(
			math::Vec(m.pad + col * m.barW, m.top + row * m.rowH),
			math::Vec(m.sectionSize * 1.6f, m.rowH * 0.34f));
	}

	/** The bar whose section letter lies under `pos`, or nought. */
	char letterAt(math::Vec pos) {
		if (!module || !module->haveSong.load())
			return 0;
		const std::vector<ChartBar>& bars = module->current().chartBars;
		const std::vector<std::vector<RowCell> > rows = chartRows(bars, 4);
		const Metrics m = metrics();
		for (size_t r = 0; r < rows.size(); r++) {
			for (size_t c = 0; c < rows[r].size(); c++) {
				if (rows[r][c].empty)
					continue;
				const ChartBar& bar = bars[rows[r][c].bar];
				if (bar.section == 0)
					continue;
				if (letterBox(m, (int) r, (int) c).contains(pos))
					return bar.section;
			}
		}
		return 0;
	}

	/** The area the chart is drawn in, inside the frame and under the title bar. */	/** The area the chart is drawn in, inside the frame and under the title bar. */
	math::Rect body() {
		return math::Rect(math::Vec(0.f, CW_TITLE),
			math::Vec(box.size.x, std::fmax(10.f, box.size.y - CW_TITLE)));
	}

	/** THE TRANSPORT, REPEATED IN THE WINDOW. Reading the chart is exactly when you want to
	start it, stop it and take it back to the top, and the module may be anywhere on the rack —
	possibly behind the window. The same two parameters, so the two pairs cannot disagree. */
	static const int TBTN = 17;

	/** ON THE CHART, not on the title bar. The transport belongs with the music: the title bar
	is the window's furniture — its name, its close cross, the thing you drag it by — and the
	eye reading a chart is at the top of the page, not up in the frame. They sit in front of the
	tune's name, so the line reads as "play this". */
	/** A HEADER DOES NOT SCROLL. It subtracted the scroll like everything else, so scrolling
	the chart slid the transport and the tune's name up under the title bar and the music was
	drawn over the top of them. The head stays; only the music moves under it.

	Its height is whatever the buttons need or whatever the bar width suggests, whichever is
	larger — the buttons are a fixed size and the rest of the window is not, so at a small size
	the proportional answer is smaller than the thing it has to hold. */
	float headHeight() {
		return std::fmax(TBTN + 10.f, metrics().rowH * 0.62f);
	}

	float headTop() {
		return body().pos.y + (headHeight() - TBTN) / 2.f;
	}

	math::Rect playBox() {
		return math::Rect(math::Vec(metrics().pad, headTop()), math::Vec(TBTN, TBTN));
	}

	math::Rect rewindBox() {
		return math::Rect(math::Vec(metrics().pad + TBTN + 4.f, headTop()),
			math::Vec(TBTN, TBTN));
	}

	/** The bar the pointer is on, or -1. Only while a chart is loaded. */
	int barAt(math::Vec pos) {
		if (!module || !module->haveSong.load())
			return -1;
		const std::vector<ChartBar>& bars = module->current().chartBars;
		const std::vector<std::vector<RowCell> > rows = chartRows(bars, 4);
		const Metrics m = metrics();
		for (size_t r = 0; r < rows.size(); r++) {
			const float top = m.top + r * m.rowH;
			if (pos.y < top || pos.y >= top + m.rowH)
				continue;
			for (size_t c = 0; c < rows[r].size(); c++) {
				if (rows[r][c].empty)
					continue;
				const float x = m.pad + c * m.barW;
				if (pos.x >= x && pos.x < x + m.barW)
					return rows[r][c].bar;
			}
		}
		return -1;
	}

	math::Rect closeBox() {
		return math::Rect(math::Vec(box.size.x - CW_TITLE, 0.f),
			math::Vec(CW_TITLE, CW_TITLE));
	}

	math::Rect gripBox() {
		return math::Rect(math::Vec(box.size.x - CW_GRIP, box.size.y - CW_GRIP),
			math::Vec(CW_GRIP, CW_GRIP));
	}

	/** WHICH EDGES A PRESS TAKES HOLD OF: left 1, right 2, top 4, bottom 8, and a corner is
	two of them at once. Every edge and not only the corner, because reaching for one particular
	corner is a small target and there is no reason for it to be the only one. */
	int edgesAt(math::Vec pos) {
		int mask = 0;
		if (pos.x <= CW_EDGE_GRAB)
			mask |= 1;
		if (pos.x >= box.size.x - CW_EDGE_GRAB)
			mask |= 2;
		if (pos.y <= CW_EDGE_GRAB)
			mask |= 4;
		if (pos.y >= box.size.y - CW_EDGE_GRAB)
			mask |= 8;
		return mask;
	}

	/** THE POINTER SAYS WHAT THE EDGE WILL DO. An edge that resizes and does not say so is an
	edge nobody finds: there is nothing to see, so the only way to learn it is there is to press
	on the border and notice.

	Asked for every frame the pointer is over the window, and put back on the way out. The shape
	is cached, so asking every frame costs a comparison. */
	static void setCursor(int shape) {
		static int current = -1;
		if (shape == current || !APP || !APP->window || !APP->window->win)
			return;
		current = shape;
		static GLFWcursor* cursors[8] = {NULL};
		static const int SHAPES[8] = {
			GLFW_ARROW_CURSOR, GLFW_RESIZE_EW_CURSOR, GLFW_RESIZE_NS_CURSOR,
			GLFW_RESIZE_NWSE_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_ALL_CURSOR,
			GLFW_ARROW_CURSOR, GLFW_ARROW_CURSOR,
		};
		for (int i = 0; i < 8; i++) {
			if (SHAPES[i] != shape)
				continue;
			if (!cursors[i])
				cursors[i] = glfwCreateStandardCursor(shape);
			glfwSetCursor(APP->window->win, cursors[i]);
			return;
		}
	}

	/** The shape an edge mask wants. */
	static int cursorFor(int mask) {
		if ((mask & 1 && mask & 4) || (mask & 2 && mask & 8))
			return GLFW_RESIZE_NWSE_CURSOR;
		if ((mask & 2 && mask & 4) || (mask & 1 && mask & 8))
			return GLFW_RESIZE_NESW_CURSOR;
		if (mask & (1 | 2))
			return GLFW_RESIZE_EW_CURSOR;
		if (mask & (4 | 8))
			return GLFW_RESIZE_NS_CURSOR;
		return GLFW_ARROW_CURSOR;
	}

	/** TAKES THE EVENT AND ENDS THE WALK.

	Consuming an event is not enough to keep it. Rack's dispatch walks the scene's children and
	keeps walking until something stops PROPAGATION — consuming only records who took it — so a
	handler that consumed and returned let the click reach the rack behind as well. Every press
	on this window was also a press on whatever module was underneath it, which is why dragging
	the window dragged a module with it.

	OpaqueWidget does both, which is why the paths that fall through to it were always right; it
	is the early returns that were not. */
	static void claim(const ButtonEvent& e, widget::Widget* by) {
		e.consume(by);
		e.stopPropagating();
	}

	void onHover(const HoverEvent& e) override {
		int mask = edgesAt(e.pos);
		if (!mask && gripBox().contains(e.pos))
			mask = 2 | 8;
		setCursor(cursorFor(mask));
		OpaqueWidget::onHover(e);
	}

	void onLeave(const LeaveEvent& e) override {
		setCursor(GLFW_ARROW_CURSOR);
		OpaqueWidget::onLeave(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (closeBox().contains(e.pos)) {
				requestDelete();
				claim(e, this);
				return;
			}
			// The edges are tested before the title bar, so the top edge resizes and the rest
			// of the bar moves.
			const int mask = edgesAt(e.pos);
			if (mask != 0 || gripBox().contains(e.pos)) {
				drag = 2;
				edges = mask ? mask : (2 | 8);
				claim(e, this);
				return;
			}
			if (module && playBox().contains(e.pos)) {
				Param& play = module->params[ChartModule::P_PLAY];
				play.setValue(play.getValue() > 0.5f ? 0.f : 1.f);
				claim(e, this);
				return;
			}
			if (module && rewindBox().contains(e.pos)) {
				module->rewindAndStop();
				claim(e, this);
				return;
			}
			if (e.pos.y < CW_TITLE) {
				drag = 1;
				claim(e, this);
				return;
			}
			// A section letter chooses that section, and choosing the one already chosen puts
			// the whole chart back.
			const char letter = letterAt(e.pos);
			if (letter != 0 && module) {
				module->setSection(module->section == letter ? 0 : letter);
				claim(e, this);
				return;
			}

			// ANYWHERE ELSE IN THE MUSIC MOVES THE PLAY HEAD THERE. Reading along and wanting
			// to hear a particular bar is the commonest thing anybody does with a chart in
			// front of them, and pointing at it is how you would ask a band.
			const int bar = barAt(e.pos);
			if (bar >= 0 && module) {
				module->jumpToBar(bar);
				claim(e, this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		// Rack's mouse deltas are in scene pixels already, since this is a child of the scene.
		if (drag == 1)
			box.pos = box.pos.plus(e.mouseDelta);
		else if (drag == 2) {
			// A left or top edge moves the window as it resizes it, so the OPPOSITE edge holds
			// still — which is what dragging an edge means everywhere else.
			if (edges & 1) {
				const float want = std::fmax(CW_MIN_W, box.size.x - e.mouseDelta.x);
				box.pos.x += box.size.x - want;
				box.size.x = want;
			}
			if (edges & 2)
				box.size.x = std::fmax(CW_MIN_W, box.size.x + e.mouseDelta.x);
			if (edges & 4) {
				const float want = std::fmax(CW_MIN_H, box.size.y - e.mouseDelta.y);
				box.pos.y += box.size.y - want;
				box.size.y = want;
			}
			if (edges & 8)
				box.size.y = std::fmax(CW_MIN_H, box.size.y + e.mouseDelta.y);
		}
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		drag = 0;
		OpaqueWidget::onDragEnd(e);
	}

	/** ESCAPE CLOSES IT, wherever the pointer is and whatever is selected.

	ASKED OF THE KEYBOARD, not waited for as an event. Rack sends a key to the selected widget,
	or failing that to whatever the pointer is over, and neither is this window when somebody
	opens the chart, glances at it, and reaches for Escape with the pointer still out on the
	rack — which is the whole way this window is meant to be used. There is no way for a plugin
	to be told about a key it was not the target of, so the window asks each frame instead.

	Not while a menu is open: Escape belongs to the menu then, and closing the window underneath
	it would be answering a key that was addressed to something else. Nor while a text field has
	the keyboard, for the same reason. */
	bool escapeWasDown = false;

	void step() override {
		widget::OpaqueWidget::step();
		if (!APP->window || !APP->window->win)
			return;
		const bool down =
			glfwGetKey(APP->window->win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
		const bool wasDown = escapeWasDown;
		escapeWasDown = down;
		if (!down || wasDown)
			return;
		if (dynamic_cast<ui::TextField*>(APP->event->getSelectedWidget()))
			return;
		for (widget::Widget* child : APP->scene->children) {
			ui::MenuOverlay* overlay = dynamic_cast<ui::MenuOverlay*>(child);
			if (overlay && overlay->visible && !overlay->requestedDelete)
				return;
		}
		requestDelete();
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			e.consume(this);
			e.stopPropagating();
			requestDelete();
			return;
		}
		widget::OpaqueWidget::onHoverKey(e);
	}

	/** Puts a row in the middle of the view, as far as the chart's length allows. */
	void scrollToRow(int row) {
		const Metrics m = metrics();
		const float want = row * m.rowH - (body().size.y - m.headH) / 2.f + m.rowH / 2.f;
		scroll = std::fmax(0.f, want);
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		const float view = body().size.y;
		scroll = math::clamp(scroll - e.scrollDelta.y,
			0.f, std::fmax(0.f, contentHeight - view));
		e.consume(this);
		e.stopPropagating();
	}

	/** Sets one chord the way a chart sets it: the root full size on the baseline, and the
	accidental and the quality small and raised beside it.

	TWO FACES. The letters and numbers come from the text face and the symbols — the triangle,
	the circle, the flats and sharps — from Petaluma, which is a music font and draws them as
	they are drawn on a chart rather than as whatever the text face happens to have at that
	codepoint. The runs arrive already divided; this only has to pick the face. */
	void drawChord(NVGcontext* vg, float x, float y, float size, const ChordText& text,
		NVGcolor ink) {

		std::shared_ptr<window::Font> text_ = chartTextFont();
		std::shared_ptr<window::Font> music = chartMusicFont();
		if (!text_ || text_->handle < 0)
			return;

		nvgFillColor(vg, ink);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		const float small = size * 0.76f;
		// The music face is drawn at a staff size rather than a text size: SMuFL glyphs are cut
		// against a four-space staff, so a codepoint set at the same size as the letters beside
		// it comes out half again too large.
		const float musicScale = 0.62f;
		const float raise = size * 0.22f;

		// Measured first, so the whole symbol sits centred in the measure rather than starting
		// at its middle.
		float w = 0.f;
		for (size_t i = 0; i < text.parts.size(); i++) {
			const ChordRun& run = text.parts[i];
			const float s = run.raised ? small : size;
			if (run.music && music && music->handle >= 0) {
				nvgFontFaceId(vg, music->handle);
				nvgFontSize(vg, s * musicScale);
			}
			else {
				nvgFontFaceId(vg, text_->handle);
				nvgFontSize(vg, s);
			}
			w += nvgTextBounds(vg, 0, 0, run.text.c_str(), NULL, NULL);
		}

		float at = x - w / 2.f;
		for (size_t i = 0; i < text.parts.size(); i++) {
			const ChordRun& run = text.parts[i];
			const float s = run.raised ? small : size;
			const float baseline = run.raised ? (y - raise) : y;
			if (run.music && music && music->handle >= 0) {
				nvgFontFaceId(vg, music->handle);
				nvgFontSize(vg, s * musicScale);
				// A music glyph sits on the staff's middle line, not on a text baseline, so it
				// is dropped by its own half-height to line up with the letters.
				at = nvgText(vg, at, baseline + s * musicScale * 0.30f, run.text.c_str(), NULL);
			}
			else {
				nvgFontFaceId(vg, text_->handle);
				nvgFontSize(vg, s);
				at = nvgText(vg, at, baseline, run.text.c_str(), NULL);
			}
		}
	}

	/** The measure-repeat mark, from the music font. It is a real glyph in Petaluma — a stroke
	with a dot either side, and the two-bar form with its numeral above — so it is set rather
	than approximated with a few lines. */
	void drawSimile(NVGcontext* vg, float x, float y, float size, bool twoBars) {
		std::shared_ptr<window::Font> music = chartMusicFont();
		if (!music || music->handle < 0)
			return;
		nvgFontFaceId(vg, music->handle);
		nvgFontSize(vg, size * 1.4f);
		nvgFillColor(vg, CW_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(vg, x, y, twoBars ? "\ue501" : "\ue500", NULL);
	}

	/** A small chevron pointing down: this name opens a list. */
	void drawChevron(NVGcontext* vg, float x, float y, float r) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x - r, y - r * 0.45f);
		nvgLineTo(vg, x, y + r * 0.55f);
		nvgLineTo(vg, x + r, y - r * 0.45f);
		nvgStrokeColor(vg, CW_INK);
		nvgStrokeWidth(vg, std::fmax(1.2f, r * 0.34f));
		nvgLineCap(vg, NVG_ROUND);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
	}

	/** The two dots of a repeat mark, against a barline. */
	void drawRepeatDots(NVGcontext* vg, float x, float y, float h, float r) {
		nvgBeginPath(vg);
		nvgCircle(vg, x, y - h * 0.18f, r);
		nvgCircle(vg, x, y + h * 0.18f, r);
		nvgFillColor(vg, CW_RULE);
		nvgFill(vg);
	}

	/** Rack calls this when the window leaves the scene, however it left — the close cross, or
	the whole scene being torn down at quit. The one place that can be sure. */
	void onRemove(const RemoveEvent& e) override;


	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		std::shared_ptr<window::Font> body_ =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!face || face->handle < 0 || !body_ || body_->handle < 0)
			return;

		// ---- the frame ----
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.f);
		nvgFillColor(args.vg, CW_BACK);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, CW_EDGE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, CW_TITLE, 5.f);
		nvgRect(args.vg, 0, CW_TITLE - 6.f, box.size.x, 6.f);
		nvgFillColor(args.vg, CW_BAND);
		nvgFill(args.vg);

		// The rule under the bar, which is what separates it from the chart rather than the
		// change of shade alone.
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, CW_TITLE);
		nvgLineTo(args.vg, box.size.x, CW_TITLE);
		nvgStrokeColor(args.vg, CW_INK);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);

		const bool haveSong = module && module->haveSong.load();
		// SET TO THE BAR, not to a number. The capitals fill the strip bar a little air top and
		// bottom, so the title is the size the bar can carry rather than a size chosen once and
		// left behind when the bar changed.
		//
		// THE MODULE'S NAME, NOT THE SONG'S. The song is named at the top of the chart, where
		// it is also the way to another one; naming it here as well would be the same text
		// twice in two inches of window.
		nvgFontFaceId(args.vg, face->handle);
		nvgFontSize(args.vg, CW_TITLE * 0.92f);
		nvgFillColor(args.vg, CW_INK);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, rewindBox().pos.x + TBTN + 9.f, CW_TITLE / 2.f, "mpxChart", NULL);

		if (haveSong)
			drawChart(args, chartTextFont(), body_);
		else {
			nvgFontFaceId(args.vg, body_->handle);
			nvgFontSize(args.vg, 12.f);
			nvgFillColor(args.vg, CW_INK);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, 12.f, CW_TITLE + 24.f,
				"No chart loaded. Choose one on the module.", NULL);
		}

		// The resize grip: three short strokes in the corner, which is what a corner that can
		// be dragged looks like everywhere else.
		{
			const math::Rect r = gripBox();
			nvgStrokeColor(args.vg, CW_EDGE);
			nvgStrokeWidth(args.vg, 1.2f);
			for (int i = 1; i <= 3; i++) {
				const float d = i * 3.5f;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, r.pos.x + r.size.x - d, r.pos.y + r.size.y - 1.f);
				nvgLineTo(args.vg, r.pos.x + r.size.x - 1.f, r.pos.y + r.size.y - d);
				nvgStroke(args.vg);
			}
		}

		Widget::draw(args);
	}

	void drawChart(const DrawArgs& args, std::shared_ptr<window::Font> face,
		std::shared_ptr<window::Font> body_) {

		const Song& song = module->current().song;
		const std::vector<ChartBar>& bars = module->current().chartBars;
		if (bars.empty())
			return;
		const std::vector<std::vector<RowCell> > rows = chartRows(bars, 4);
		const Key key = module->playingKey();
		const int playingBar = module->playingBar.load();
		const char chosen = module->section;

		// EVERY BAR OF EVERY OCCURRENCE of the chosen letter, so all of them glow at once and
		// it is plain that choosing A took all three A's rather than the first.
		std::vector<bool> inSection(bars.size(), false);
		std::vector<bool> sectionStarts(bars.size(), false);
		std::vector<bool> sectionEnds(bars.size(), false);
		if (chosen != 0) {
			const std::vector<ChartSection> ranges = chartRangesForLabel(bars, chosen);
			for (size_t r = 0; r < ranges.size(); r++) {
				for (int b = ranges[r].first; b <= ranges[r].last
					&& b < (int) bars.size(); b++) {
					inSection[b] = true;
				}
				sectionStarts[ranges[r].first] = true;
				if (ranges[r].last < (int) bars.size())
					sectionEnds[ranges[r].last] = true;
			}
		}

		const math::Rect view = body();

		// EVERY SIZE COMES FROM THE BAR'S WIDTH. See the note at the top of the file, and
		// metrics() for the numbers, which clicking shares.
		const Metrics m = metrics();
		const float pad = m.pad;
		const float barW = m.barW;
		const float rowH = m.rowH;
		const float chordSize = m.chordSize;
		const float markSize = m.markSize;
		const float sectionSize = m.sectionSize;
		const float headH = m.headH;

		// Held inside its range here rather than only where the wheel turns, so making the
		// window taller pulls the chart back down instead of leaving a gap under it.
		contentHeight = headH + rows.size() * rowH + rowH * 0.5f;
		scroll = math::clamp(scroll, 0.f, std::fmax(0.f, contentHeight - view.size.y));

		// The transport, drawn from the module's own parameters so the two pairs always agree.
		{
			const bool playing = module && module->params[ChartModule::P_PLAY]
				.getValue() > 0.5f;
			const math::Rect play = playBox();
			nvgSave(args.vg);
			nvgTranslate(args.vg, play.pos.x, play.pos.y);
			drawRaisedButton(args.vg, play.size, playing, playing);
			drawPlayGlyph(args.vg, play.size, playing);
			nvgRestore(args.vg);

			const math::Rect rewind = rewindBox();
			nvgSave(args.vg);
			nvgTranslate(args.vg, rewind.pos.x, rewind.pos.y);
			drawRaisedButton(args.vg, rewind.size, false, false);
			drawRewindGlyph(args.vg, rewind.size);
			nvgRestore(args.vg);
		}

		// The close cross.
		{
			const math::Rect r = closeBox();
			const float m = 6.f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, r.pos.x + m, r.pos.y + m);
			nvgLineTo(args.vg, r.pos.x + r.size.x - m, r.pos.y + r.size.y - m);
			nvgMoveTo(args.vg, r.pos.x + r.size.x - m, r.pos.y + m);
			nvgLineTo(args.vg, r.pos.x + m, r.pos.y + r.size.y - m);
			nvgStrokeColor(args.vg, CW_INK);
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}


		// The tune's name, after the transport, so the line reads as "play this".
		{
			nvgFontFaceId(args.vg, face->handle);
			nvgFontSize(args.vg, m.markSize * 1.35f);
			nvgFillColor(args.vg, CW_INK);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			std::string name = song.title;
			if (!song.composer.empty())
				name += "   " + song.composer;
			nvgText(args.vg, rewindBox().pos.x + TBTN + m.markSize * 0.8f,
				headTop() + TBTN / 2.f, name.c_str(), NULL);
		}


		// THE MUSIC IS CLIPPED BELOW THE HEAD, so a row scrolling up stops at the header
		// rather than running through it.
		nvgSave(args.vg);
		nvgScissor(args.vg, view.pos.x, view.pos.y + headH,
			view.size.x, std::fmax(1.f, view.size.y - headH));

		float y = view.pos.y + headH - scroll;

		// The head: who wrote it, what key, what meter.
		nvgFontFaceId(args.vg, body_->handle);
		nvgFontSize(args.vg, markSize);
		nvgFillColor(args.vg, CW_INK);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		{
			char head[192];
			std::snprintf(head, sizeof(head), "%s%s%s   %s %s   %d/%d",
				song.composer.c_str(), song.composer.empty() ? "" : "  —  ",
				song.style.c_str(), pitchClassNameIn(key.tonic, key),
				key.minor ? "minor" : "major", song.beats, song.unit);
			nvgText(args.vg, pad, view.pos.y + headH * 0.45f - scroll, head, NULL);
		}

		for (size_t r = 0; r < rows.size(); r++) {
			const std::vector<RowCell>& row = rows[r];
			const float top = y + r * rowH;
			const float mid = top + rowH * 0.55f;
			// Rows above or below the window are laid out but not painted.
			if (top + rowH < view.pos.y || top > view.pos.y + view.size.y)
				continue;

			for (size_t c = 0; c < row.size(); c++) {
				if (row[c].empty)
					continue;
				const ChartBar& bar = bars[row[c].bar];
				const float x = pad + c * barW;

				// THE SECTION THAT IS CHOSEN: a rule over the bar, turned down at the true
				// ends of each run so it reads as a bracket rather than as a row of dashes.
				// A DIFFERENT MARK from the one that follows the music — one says what will be
				// played and the other says what is being played, and they are on screen at the
				// same time.
				if (chosen != 0 && inSection[row[c].bar]) {
					const float ry = top + rowH * 0.115f;
					const float cap = rowH * 0.11f;
					nvgBeginPath(args.vg);
					if (sectionStarts[row[c].bar])
						nvgMoveTo(args.vg, x + 1.f, ry + cap);
					else
						nvgMoveTo(args.vg, x, ry);
					nvgLineTo(args.vg, x + 1.f, ry);
					nvgLineTo(args.vg, x + barW - 1.f, ry);
					if (sectionEnds[row[c].bar])
						nvgLineTo(args.vg, x + barW - 1.f, ry + cap);
					nvgStrokeColor(args.vg, CW_CHOSEN);
					nvgStrokeWidth(args.vg, rowH * 0.045f);
					nvgStroke(args.vg);
				}

				// THE MEASURE BEING PLAYED. A block behind the whole bar rather than a mark
				// beside it: at the small end of the window a mark is a few pixels and the
				// whole point is to be able to see where you are without looking for it. The
				// same bar lights again on the next pass through a repeat, which is what a
				// repeat means.
				if (row[c].bar == playingBar) {
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, x + 1.f, top + rowH * 0.24f,
						barW - 2.f, rowH * 0.66f, rowH * 0.06f);
					nvgFillColor(args.vg, nvgRGBA(0xff, 0x3c, 0xc8, 0x55));
					nvgFill(args.vg);
				}

				// The barline on the left of the bar, and the repeat dots if it opens one.
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, x, mid - rowH * 0.30f);
				nvgLineTo(args.vg, x, mid + rowH * 0.30f);
				nvgStrokeColor(args.vg, CW_RULE);
				nvgStrokeWidth(args.vg, bar.repeatOpen ? rowH * 0.055f : rowH * 0.022f);
				nvgStroke(args.vg);
				if (bar.repeatOpen)
					drawRepeatDots(args.vg, x + rowH * 0.15f, mid, rowH, rowH * 0.055f);

				// The right edge, when this bar carries one.
				if (bar.repeatClose || bar.end || bar.doubleRight
					|| c + 1 == row.size()) {
					const float rx = x + barW;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, rx, mid - rowH * 0.30f);
					nvgLineTo(args.vg, rx, mid + rowH * 0.30f);
					nvgStrokeWidth(args.vg,
						(bar.repeatClose || bar.end) ? rowH * 0.055f : rowH * 0.022f);
					nvgStroke(args.vg);
					if (bar.repeatClose || bar.end || bar.doubleRight) {
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, rx - rowH * 0.075f, mid - rowH * 0.30f);
						nvgLineTo(args.vg, rx - rowH * 0.075f, mid + rowH * 0.30f);
						nvgStrokeWidth(args.vg, rowH * 0.022f);
						nvgStroke(args.vg);
					}
					if (bar.repeatClose)
						drawRepeatDots(args.vg, rx - rowH * 0.18f, mid, rowH, rowH * 0.055f);
				}

				// The section letter, above the bar it opens.
				if (bar.section != 0) {
					char label[4] = {bar.section, 0};
					nvgFontFaceId(args.vg, face->handle);
					nvgFontSize(args.vg, sectionSize);
					nvgFillColor(args.vg,
						(chosen != 0 && bar.section == chosen) ? CW_CHOSEN : CW_SECTION);
					nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgText(args.vg, x + rowH * 0.06f, top + rowH * 0.17f, label, NULL);
				}

				// The ending bracket: a rule over the bar with a downturn at its start.
				if (bar.ending > 0) {
					const float ey = top + rowH * 0.20f;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x, ey + rowH * 0.13f);
					nvgLineTo(args.vg, x, ey);
					nvgLineTo(args.vg, x + barW, ey);
					nvgStrokeColor(args.vg, CW_RULE);
					nvgStrokeWidth(args.vg, rowH * 0.030f);
					nvgStroke(args.vg);
					char n[8];
					std::snprintf(n, sizeof(n), "%d.", bar.ending);
					nvgFontFaceId(args.vg, body_->handle);
					nvgFontSize(args.vg, markSize);
					nvgFillColor(args.vg, CW_INK);
					nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgText(args.vg, x + 4.f, ey + rowH * 0.10f, n, NULL);
				}

				// A meter change is announced where it happens.
				if (bar.timeBeats > 0 && bar.index > 0) {
					char ts[12];
					std::snprintf(ts, sizeof(ts), "%d/%d", bar.timeBeats, bar.timeUnit);
					nvgFontFaceId(args.vg, face->handle);
					nvgFontSize(args.vg, markSize);
					nvgFillColor(args.vg, CW_INK);
					nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgText(args.vg, x + 4.f, mid, ts, NULL);
				}

				// The signs, above the bar beside the section letter.
				{
					// THE SIGNS ARE SIGNS, the instructions are words. A segno and a coda are
					// drawn symbols every musician reads without translating; "D.C. al Fine"
					// is a sentence and stays one.
					std::string marks;
					std::string signs;
					if (bar.segno)
						signs += "\ue047";
					if (bar.coda)
						signs += "\ue048";
					if (bar.fine)
						marks += "Fine ";
					if (bar.passes > 0) {
						char buf[12];
						std::snprintf(buf, sizeof(buf), "%dx ", bar.passes);
						marks += buf;
					}
					if (bar.nav.from != ChartNav::NAV_NONE) {
						marks += (bar.nav.from == ChartNav::NAV_DS) ? "D.S." : "D.C.";
						if (bar.nav.target == ChartNav::TO_CODA)
							marks += " al Coda";
						else if (bar.nav.target == ChartNav::TO_FINE)
							marks += " al Fine";
					}
					float right = x + barW - rowH * 0.08f;
					if (!marks.empty()) {
						nvgFontFaceId(args.vg, body_->handle);
						nvgFontSize(args.vg, markSize);
						nvgFillColor(args.vg, CW_INK);
						nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
						nvgText(args.vg, right, top + rowH * 0.17f, marks.c_str(), NULL);
						right -= nvgTextBounds(args.vg, 0, 0, marks.c_str(), NULL, NULL)
							+ rowH * 0.06f;
					}
					if (!signs.empty()) {
						std::shared_ptr<window::Font> music = chartMusicFont();
						if (music && music->handle >= 0) {
							nvgFontFaceId(args.vg, music->handle);
							nvgFontSize(args.vg, markSize * 1.9f);
							nvgFillColor(args.vg, CW_INK);
							nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
							nvgText(args.vg, right, top + rowH * 0.22f, signs.c_str(), NULL);
						}
					}
				}

				// The chords, spread across the bar.
				const int count = (int) bar.slots.size();
				for (int k = 0; k < count; k++) {
					const ChartSlot& slot = bar.slots[k];
					if (slot.empty)
						continue;
					const float cx = x + barW * (k + 0.5f) / count;
					const float size = chordSize * (count > 2 ? 0.78f : 1.f);

					if (slot.simile != ChartSlot::SIMILE_NONE) {
						nvgFontFaceId(args.vg, body_->handle);
						nvgFontSize(args.vg, size);
						nvgFillColor(args.vg, CW_INK);
						nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
						nvgText(args.vg, cx, mid,
							slot.simile == ChartSlot::SIMILE_TWO ? "≕" : "⁄.",
							NULL);
						continue;
					}
					if (slot.noChord || slot.plain) {
						nvgFontFaceId(args.vg, face->handle);
						nvgFontSize(args.vg, size * 0.8f);
						nvgFillColor(args.vg, CW_INK);
						nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
						nvgText(args.vg, cx, mid,
							slot.noChord ? "N.C." : (slot.raw.empty() ? "?" : slot.raw.c_str()),
							NULL);
						continue;
					}
					drawChord(args.vg, cx, mid, size,
						module->chordMode() == 1 ? chordTextRoman(slot.chord)
							: chordTextLetter(slot.chord, key),
						CW_INK);
				}
			}
		}

		// The scrollbar, and only when there is somewhere to scroll to.
		if (contentHeight > view.size.y) {
			const float track = view.size.y - 8.f;
			const float thumb = std::fmax(24.f, track * view.size.y / contentHeight);
			const float at = (track - thumb) * scroll / (contentHeight - view.size.y);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, box.size.x - 7.f, view.pos.y + 4.f + at, 4.f, thumb, 2.f);
			nvgFillColor(args.vg, CW_EDGE);
			nvgFill(args.vg);
		}

		nvgRestore(args.vg);
	}
};


static ChartWindow* gChartWindow = NULL;

/** THE CHART PICKER: import, then every playlist, then its songs behind their initials.

A playlist runs to fourteen hundred songs and a menu of fourteen hundred is not a menu; behind
a playlist and an initial it is twenty-something entries, which is. */
/** THE CHART PICKER: the way to import, and the way to every chart there is.

Import at the top, then each playlist, then its songs behind their initials. A playlist runs to
fourteen hundred songs and a menu of fourteen hundred is not a menu; behind a playlist and an
initial it is twenty-something entries, which is. */
static void chartShowSongMenu(ChartModule* module) {
	if (!module)
		return;
	if (!gLibraryLoaded)
		libraryLoad();
	ChartModule* self = module;

	ui::Menu* menu = createMenu();
	menu->addChild(createMenuItem("Import an iReal Pro playlist\u2026", "", []() {
		// The HTML export iReal Pro produces, which is what people actually have.
		osdialog_filters* filters = osdialog_filters_parse("HTML:html,htm");
		char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
		osdialog_filters_free(filters);
		if (!path)
			return;
		const int added = libraryImport(path);
		std::free(path);
		if (added == 0) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				"No songs were found in that file.\n\n"
				"It should be an iReal Pro playlist exported as HTML.");
		}
	}));
	menu->addChild(new ui::MenuSeparator);

	// The playlists there are, each opening straight onto its songs — so choosing a chart from
	// a playlist you have not been in is two presses rather than a change of playlist and then
	// a hunt for the song.
	std::vector<std::string> names;
	for (const LibraryEntry& e : gLibrary) {
		bool seen = false;
		for (const std::string& had : names)
			seen = seen || (had == e.playlist);
		if (!seen)
			names.push_back(e.playlist);
	}
	if (names.empty()) {
		menu->addChild(createMenuLabel("Nothing imported yet"));
		return;
	}
	for (size_t i = 0; i < names.size(); i++) {
		const std::string name = names[i];
		int count = 0;
		for (const LibraryEntry& e : gLibrary)
			count += (e.playlist == name) ? 1 : 0;
		menu->addChild(createSubmenuItem(name, string::f("%d", count),
			[self, name](ui::Menu* sub) {
				for (char c = 'A'; c <= 'Z'; c++) {
					int n = 0;
					for (const LibraryEntry& e : gLibrary) {
						if (e.playlist == name && !e.title.empty()
							&& std::toupper((unsigned char) e.title[0]) == c) {
							n++;
						}
					}
					if (n == 0)
						continue;
					sub->addChild(createSubmenuItem(std::string(1, c), string::f("%d", n),
						[self, name, c](ui::Menu* songs) {
							for (const LibraryEntry& e : gLibrary) {
								if (e.playlist != name || e.title.empty()
									|| std::toupper((unsigned char) e.title[0]) != c) {
									continue;
								}
								const std::string chunk = e.chunk;
								songs->addChild(createMenuItem(e.title, e.composer,
									[self, chunk, name]() { self->setSong(chunk, name); }));
							}
						}));
				}
			}));
	}
}


static void chartShowKeyMenu(ChartModule* module) {
	if (!module || !module->haveSong.load())
		return;
	ChartModule* self = module;
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel("Key"));
	// NAMED, NOT NUMBERED. A musician asks for E flat, not for three semitones down, and the
	// transpose that gets there is arithmetic nobody should be asked to do.
	for (int pc = 0; pc < 12; pc++) {
		const Key key = self->soundingKey();
		Key named = key;
		named.tonic = (int8_t) pc;
		const std::string name = std::string(pitchClassNameIn(pc, named))
			+ (key.minor ? " minor" : " major");
		menu->addChild(createCheckMenuItem(name, "",
			[self, pc]() {
				return ((self->soundingKey().tonic % 12) + 12) % 12 == pc;
			},
			[self, pc]() { self->setSoundingTonic(pc); }));
	}
}


void ChartDisplay::onButton(const ButtonEvent& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
		if (titleBox().contains(e.pos)) {
			chartShowSongMenu(module);
			e.consume(this);
			e.stopPropagating();
			return;
		}
		if (module->haveSong.load() && keyBox().contains(e.pos)) {
			chartShowKeyMenu(module);
			e.consume(this);
			e.stopPropagating();
			return;
		}
	}
	OpaqueWidget::onButton(e);
}


void ChartWindow::onRemove(const RemoveEvent& e) {
	// A window closed under the pointer sends no leave event, and the resize arrow would be
	// left on the rack with nothing to click to get it back.
	setCursor(GLFW_ARROW_CURSOR);
	if (gChartWindow == this)
		gChartWindow = NULL;
	widget::OpaqueWidget::onRemove(e);
}

/** Puts the playing measure in view, without moving anything else. */
static void chartWindowScrollToPlaying();

void chartWindowShow(ChartModule* module) {
	if (gChartWindow) {
		// Already up: bring it to the front rather than opening a second one.
		APP->scene->removeChild(gChartWindow);
		APP->scene->addChild(gChartWindow);
		gChartWindow->module = module;
		return;
	}
	gChartWindow = new ChartWindow;
	gChartWindow->module = module;
	APP->scene->addChild(gChartWindow);
	// OPENED ON THE MUSIC, not at the top of the page. A chart of any length opened at bar one
	// while the band is at bar forty shows you the one thing you did not open it to see.
	chartWindowScrollToPlaying();
}


static void chartWindowScrollToPlaying() {
	if (!gChartWindow || !gChartWindow->module)
		return;
	ChartModule* module = gChartWindow->module;
	if (!module->haveSong.load())
		return;
	const std::vector<ChartBar>& bars = module->current().chartBars;
	const int at = module->playingBar.load();
	if (at < 0 || bars.empty())
		return;
	const std::vector<std::vector<RowCell> > rows = chartRows(bars, 4);
	for (size_t r = 0; r < rows.size(); r++) {
		for (size_t c = 0; c < rows[r].size(); c++) {
			if (rows[r][c].empty || rows[r][c].bar != at)
				continue;
			gChartWindow->scrollToRow((int) r);
			return;
		}
	}
}


// ---- panel -----------------------------------------------------------------------------------

/** TEN HP. The chart is read in the window, so the face carries only what you glance at while
patching — what is loaded, where you are in it — and the controls that decide what is played.

Ten rather than eight because eight held it all and held it tightly: the readout, two lamps, a
button, a knob, two more buttons and three jacks in forty millimetres left nothing between one
row and the next, and a panel with no air in it is a panel you have to look at twice.

The transpose is NOT a knob here. Choosing a key is naming one — E flat, not three semitones —
so it is the key line of the readout, which opens a menu of the twelve. */
static Layout chartLayout() {
	Layout L;
	L.hp = 10.f;
	L.title = "mpxChart";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id,
			const std::string& name, const char* style = "knob") {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = style;
		L.items.push_back(i);
		// CLOSE UNDER THE CONTROL. A knob is about ten millimetres across, so its edge is five
		// below its centre; a name set nine below sat four clear of it and read as a caption
		// for the panel rather than for the knob. Two millimetres is a label; four is a gap.
		label(key + ".label", x, y + (std::string(style) == "knob.large" ? 9.5f : 7.f),
			name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.5f, name, Panel::CENTRE, size > 0.f, size, key);
	};

	// SIDE BY SIDE, the button on the left and the choice on the right, both in the same band
	// of the panel. Two small controls stacked one above the other wasted the width the module
	// had just been given.
	Item open;
	open.key = "p.open"; open.kind = Item::PARAM; open.id = ChartModule::P_OPEN;
	open.style = "button"; open.x = 11.f; open.y = 54.f;
	L.items.push_back(open);
	label("p.open.label", 11.f, 60.f, "CHART", Panel::CENTRE, true, 0.f, "p.open");

	// How the chords are written: a vertical pair, names to the left, which is the shape a
	// radio choice has everywhere else and reads down rather than across.
	Item mode;
	mode.key = "p.mode"; mode.kind = Item::PARAM; mode.id = ChartModule::P_CHORD_MODE;
	mode.style = "lamps"; mode.x = 24.f; mode.y = 50.f;
	mode.pitch = 8.f; mode.names = {"LETTER", "ROMAN"};
	mode.horizontal = false;
	mode.labelSide = Panel::LEFT;
	L.items.push_back(mode);

	// THE TWO DISPLAYS, in the layout rather than placed in code, so the editor can move them
	// like anything else. The widgets themselves are made by the module and handed over.
	auto display = [&](const std::string& key, float x, float y, float w, float h) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY;
		i.x = x; i.y = y; i.w = w; i.h = h;
		L.items.push_back(i);
	};
	display("d.readout", 2.5f, 13.f, 46.f, 35.f);
	// NARROW, because it holds five characters at most — "300.0" — and eighteen millimetres of
	// plate around eleven of number is a frame looking for something to hold.
	// CENTRED ON THE KNOB IT BELONGS TO. Thirteen wide about a centre of 12.5, so its corner
	// is at 6 — a display is placed by its corner, which is the arithmetic worth writing down
	// rather than working out again next time.
	display("d.bpm", 6.f, 67.75f, 13.f, 6.5f);

	// LARGER, AND LOWER. Tempo is the knob on this panel that gets turned, and it had the same
	// body as everything else; it is the large one now, with the reading it sets written above
	// it. Moved down to make that room.
	knob("p.tempo", 12.5f, 83.5f, ChartModule::P_TEMPO, "TEMPO", "knob.large");

	Item play;
	play.key = "p.play"; play.kind = Item::PARAM; play.id = ChartModule::P_PLAY;
	play.style = "transport.play"; play.x = 30.f; play.y = 71.5f;
	L.items.push_back(play);
	label("p.play.label", 30.f, 78.5f, "PLAY", Panel::CENTRE, true, 0.f, "p.play");

	Item rewind;
	rewind.key = "p.rewind"; rewind.kind = Item::PARAM; rewind.id = ChartModule::P_REWIND;
	rewind.style = "transport.rewind"; rewind.x = 41.f; rewind.y = 71.5f;
	L.items.push_back(rewind);
	label("p.rewind.label", 41.f, 78.5f, "REWIND", Panel::CENTRE, true, 0.f, "p.rewind");

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = ChartModule::L_BEAT;
	// Beside the reading rather than out on its own: the number says the tempo and the lamp
	// beats it.
	lamp.x = 23.f; lamp.y = 71.f;
	L.items.push_back(lamp);

	// THREE ROWS. The inputs, then the chart as pitches, then the chart as scales — grouped so
	// that the pair in one format looks like a pair, and so that nobody reaches for the twelve
	// on-or-off flags when they wanted notes they can hear.
	// THREE COLUMNS AT FIFTEEN MILLIMETRES, which is the arrangement worked out in the panel
	// editor and squared up here: the two inputs down the left, the two ways of hearing a chord
	// down the middle, and the MPX bundle above the two that are its plainer equivalents.
	jack("out.mpx", Item::PORT_OUT, 42.5f, 89.f, ChartModule::O_MPX, "mpxOut", NOTE_CABLE, 8.f);

	jack("in.clock", Item::PORT_IN, 12.5f, 102.f, ChartModule::I_CLOCK, "clock", SIG_GATE);
	jack("out.chord", Item::PORT_OUT, 27.5f, 102.f, ChartModule::O_CHORD, "chord", SIG_PITCH);
	jack("out.root", Item::PORT_OUT, 42.5f, 102.f, ChartModule::O_ROOT, "root", SIG_PITCH);

	jack("in.reset", Item::PORT_IN, 12.5f, 114.f, ChartModule::I_RESET, "reset", SIG_GATE);
	jack("out.pesChord", Item::PORT_OUT, 27.5f, 114.f, ChartModule::O_PES_CHORD,
		"PES chord", SIG_CV);
	jack("out.pesScale", Item::PORT_OUT, 42.5f, 114.f, ChartModule::O_PES_SCALE,
		"PES scale", SIG_CV);

	L.bindOffsets();
	return L;
}


struct ChartWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ChartWidget(ChartModule* module) {
		setModule(module);
		layout = chartLayout();
		layoutApplyUser("mpxChart", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		// Made here, placed by the layout — so where they sit is one answer, in one file, and
		// the editor can change it.
		ChartDisplay* display = new ChartDisplay;
		display->module = module;
		layoutPlaceDisplay(this, layout, "d.readout", display);

		ChartTempoDisplay* bpm = new ChartTempoDisplay;
		bpm->module = module;
		layoutPlaceDisplay(this, layout, "d.bpm", bpm);
	}

	/** The songs of one playlist, behind a letter each, because a menu of fourteen hundred is
	not a menu. */
	void addPlaylistMenu(ui::Menu* menu, ChartModule* chart, const std::string& playlist) {
		std::vector<char> letters;
		for (const LibraryEntry& e : gLibrary) {
			if (e.playlist != playlist || e.title.empty())
				continue;
			const char c = (char) std::toupper((unsigned char) e.title[0]);
			if (std::find(letters.begin(), letters.end(), c) == letters.end())
				letters.push_back(c);
		}
		std::sort(letters.begin(), letters.end());
		for (char c : letters) {
			menu->addChild(createSubmenuItem(std::string(1, c), "",
				[=](ui::Menu* sub) {
					for (const LibraryEntry& e : gLibrary) {
						if (e.playlist != playlist || e.title.empty())
							continue;
						if ((char) std::toupper((unsigned char) e.title[0]) != c)
							continue;
						const std::string chunk = e.chunk;
						const std::string list = e.playlist;
						sub->addChild(createCheckMenuItem(e.title, e.composer,
							[=]() { return chart->chunk == chunk; },
							[=]() { chart->setSong(chunk, list); }));
					}
				}));
		}
	}

	void appendContextMenu(ui::Menu* menu) override {
		ChartModule* chart = dynamic_cast<ChartModule*>(module);
		if (!chart)
			return;
		if (!gLibraryLoaded)
			libraryLoad();

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Charts"));

		menu->addChild(createMenuItem("Import an iReal Pro playlist\u2026", "", []() {
			// The HTML export iReal Pro produces, which is what people actually have.
			osdialog_filters* filters = osdialog_filters_parse("HTML:html,htm");
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			const int added = libraryImport(path);
			std::free(path);
			if (added == 0) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"No songs were found in that file.\n\n"
					"It should be an iReal Pro playlist exported as HTML.");
			}
		}));

		// The playlists that have been imported, each behind its own submenu.
		std::vector<std::string> playlists;
		for (const LibraryEntry& e : gLibrary) {
			if (std::find(playlists.begin(), playlists.end(), e.playlist) == playlists.end())
				playlists.push_back(e.playlist);
		}
		std::sort(playlists.begin(), playlists.end());
		for (const std::string& playlist : playlists) {
			int count = 0;
			for (const LibraryEntry& e : gLibrary) {
				if (e.playlist == playlist)
					count++;
			}
			menu->addChild(createSubmenuItem(playlist, string::f("%d", count),
				[this, chart, playlist](ui::Menu* sub) { addPlaylistMenu(sub, chart, playlist); }));
		}

		if (playlists.empty())
			menu->addChild(createMenuLabel("Nothing imported yet"));

		layoutAppendMenu(menu, this, panel, &layout, "mpxChart");
	}

	/** What the open button read on the last frame.

	NOT A SCHMITT TRIGGER, which is what this was and why the button took two presses. Rack's
	trigger starts UNINITIALIZED and fires only on a LOW to HIGH move, so the first press merely
	told it that high is where we are; the second was the first one it could call a rise. A
	button that needs pressing twice is a broken button, and the fix is to remember the previous
	value rather than to hold a state machine that begins by not knowing anything. */
	float lastOpen = 0.f;

	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		ChartModule* chart = dynamic_cast<ChartModule*>(module);
		if (chart) {
			const float now = chart->params[ChartModule::P_OPEN].getValue();
			if (now > 0.5f && lastOpen <= 0.5f) {
				// OPENING THE CHART CHANGES NOTHING. It was stopping the transport and winding
				// it back, on the reasoning that opening the chart is the start of choosing
				// something — but looking at the music while it plays is the commonest reason
				// to open it, and a window that silences the patch to show you where you are
				// has answered a question nobody asked. LOADING a chart still stops, because
				// that genuinely does change what is playing.
				chartWindowShow(chart);
			}
			lastOpen = now;
		}
		PortWidget* port = getOutput(ChartModule::O_MPX);
		if (!port)
			return;
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
			engine::Cable* cable = cw->getCable();
			if (cable && isMPXInput(cable->inputModule, cable->inputId))
				cw->color = NOTE_CABLE;
		}
	}
};


} // namespace px


Model* modelChart = createModel<px::ChartModule, px::ChartWidget>("mpxChart");
