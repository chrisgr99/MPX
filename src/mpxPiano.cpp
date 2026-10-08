/** mpxPiano — the Salamander Grand Piano, played from an MPX cable. See docs/piano.md.

ONLY AN MPX CABLE IN. Ordinary Rack signals reach it through mpxIn, which already turns pitch,
gate, level and timbre into notes and now carries the two pedals as well. That keeps this module to
the piano: its sound, its pedals, and where its samples come from.

THE SAMPLES ARE NOT IN THE PLUGIN. They are several hundred megabytes, and a plugin that size would
be the largest in the library for the sake of one module. The Download button fetches them, once,
into the Rack user folder, where every mpxPiano in every patch shares them. */
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <network.hpp>
#include <osdialog.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>


namespace px {


// ---- where the samples come from ---------------------------------------------------------------

/** A release on a repository of our own, because the original is compressed with xz and Rack can
only unpack zstd. The files inside are Alexander Holm's, unaltered. */
static const char* SAMPLES_URL = "https://github.com/chrisgr99/MPX-Piano-Samples/releases/download/"
	"v1/mpx-piano-samples-v1.tar.zst";
static const char* SAMPLES_SIZE = "542 MB";
/** Every file the map names. Fewer than this after unpacking is a download that did not finish. */
static const int SAMPLES_EXPECTED = 641;
/** The rate the set was recorded down to. Played at any other engine rate by stepping through it
faster or slower, the same arithmetic that pitches a note. */
static const float SAMPLES_RATE = 44100.f;


static std::string pianoDir() {
	return asset::user("DreamerMPX/piano");
}


// ---- the samples in memory ---------------------------------------------------------------------

/** One recording, stereo, held as the 16-bit numbers it was stored as rather than converted to
floating point — half the memory, and the conversion costs nothing when it is done per sample. */
struct PianoSample {
	std::vector<int16_t> data;   // interleaved left, right
	int frames = 0;
};

enum Part {
	PART_STRINGS,
	PART_RESONANCE,
	PART_HAMMER,
	PART_PEDAL_DOWN,
	PART_PEDAL_UP,
};

/** One entry of the map: a recording and the keys and velocities it answers. */
struct PianoRegion {
	int part = PART_STRINGS;
	int lokey = 0, hikey = 127, key = 60;
	int lovel = 1, hivel = 127;
	/** The top of the keyboard has no dampers, so those strings ring on after the key comes up. */
	bool undamped = false;
	float gain = 1.f;
	/** HOW LOUD THIS RECORDING ACTUALLY IS, as a fraction of full scale, measured when it is
	read. The set's sixteen velocity layers are separate performances and span about twenty-four
	decibels between them, so how loud a note comes out is mostly a property of which recording
	was chosen and not of any formula. Without this figure the module cannot know how loud a note
	it is about to play will be, and so cannot reason about its own dynamic range. See DYNAMICS. */
	float peak = 1.f;
	int sample = -1;
};

struct PianoBank {
	std::vector<PianoSample> samples;
	std::vector<PianoRegion> regions;
	int layers = 16;

	const PianoRegion* find(int part, int key, int vel) const {
		const PianoRegion* nearest = NULL;
		for (const PianoRegion& r : regions) {
			if (r.part != part || key < r.lokey || key > r.hikey)
				continue;
			if (vel >= r.lovel && vel <= r.hivel)
				return &r;
			// A velocity no layer claims — a gap left by loading fewer layers — takes the
			// nearest one rather than silence.
			if (!nearest || std::abs(vel - (r.lovel + r.hivel) / 2)
					< std::abs(vel - (nearest->lovel + nearest->hivel) / 2))
				nearest = &r;
		}
		return nearest;
	}

	const PianoRegion* pedal(int part, float choice) const {
		std::vector<const PianoRegion*> found;
		for (const PianoRegion& r : regions)
			if (r.part == part)
				found.push_back(&r);
		if (found.empty())
			return NULL;
		return found[std::min((int) found.size() - 1, (int) (choice * found.size()))];
	}
};


/** Reads a 16-bit stereo WAV. Only what the set contains is understood: PCM, two channels, 16 bits.
Anything else is refused, since a file that parses wrongly plays as noise at full volume. */
static bool readWav(const std::string& path, PianoSample& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	char riff[12];
	if (!f.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
		return false;
	int channels = 0, bits = 0;
	while (f) {
		char id[4];
		uint32_t size = 0;
		if (!f.read(id, 4) || !f.read((char*) &size, 4))
			break;
		if (std::memcmp(id, "fmt ", 4) == 0) {
			std::vector<char> fmt(size);
			if (!f.read(fmt.data(), size) || size < 16)
				return false;
			uint16_t format, ch, bps;
			std::memcpy(&format, fmt.data(), 2);
			std::memcpy(&ch, fmt.data() + 2, 2);
			std::memcpy(&bps, fmt.data() + 14, 2);
			if (format != 1)
				return false;
			channels = ch;
			bits = bps;
		}
		else if (std::memcmp(id, "data", 4) == 0) {
			if (channels != 2 || bits != 16)
				return false;
			out.frames = size / 4;
			out.data.resize((size_t) out.frames * 2);
			return (bool) f.read((char*) out.data.data(), (std::streamsize) out.frames * 4);
		}
		else {
			f.seekg(size + (size & 1), std::ios::cur);
		}
	}
	return false;
}


/** The velocity layer a file holds, from its name: "A0v8.wav" is layer 8. Nought for a file that is
not a string recording. */
static int layerOf(const std::string& file) {
	const size_t v = file.rfind('v');
	if (v == std::string::npos)
		return 0;
	return std::atoi(file.c_str() + v + 1);
}


/** Loads the set, keeping only `layers` of the sixteen velocity layers of the strings.

FEWER LAYERS, EVENLY SPACED: eight keeps every other one, four keeps 4, 8, 12 and 16, so the
loudest is always there and the soft end is covered too. The velocities of the layers left out are
handed to the kept layer just above them, so every velocity still sounds. */
static PianoBank* loadBank(const std::string& dir, int layers, std::atomic<float>* progress) {
	json_error_t err;
	json_t* rootJ = json_load_file((dir + "/map.json").c_str(), 0, &err);
	if (!rootJ)
		return NULL;
	PianoBank* bank = new PianoBank;
	bank->layers = layers;
	const int keepEvery = (layers >= 16) ? 1 : (layers >= 8) ? 2 : 4;

	json_t* regionsJ = json_object_get(rootJ, "regions");
	const size_t count = json_array_size(regionsJ);
	for (size_t i = 0; i < count; i++) {
		json_t* rJ = json_array_get(regionsJ, i);
		const char* file = json_string_value(json_object_get(rJ, "file"));
		const char* partS = json_string_value(json_object_get(rJ, "part"));
		if (!file || !partS)
			continue;
		PianoRegion r;
		const std::string part = partS;
		if (part == "strings")
			r.part = PART_STRINGS;
		else if (part == "resonance")
			r.part = PART_RESONANCE;
		else if (part == "hammer")
			r.part = PART_HAMMER;
		else if (part == "pedal") {
			const char* action = json_string_value(json_object_get(rJ, "action"));
			r.part = (action && std::string(action) == "up") ? PART_PEDAL_UP : PART_PEDAL_DOWN;
		}
		else
			continue;
		if (r.part == PART_STRINGS && layerOf(file) % keepEvery != 0)
			continue;
		if (json_t* j = json_object_get(rJ, "lokey")) r.lokey = json_integer_value(j);
		if (json_t* j = json_object_get(rJ, "hikey")) r.hikey = json_integer_value(j);
		if (json_t* j = json_object_get(rJ, "key")) r.key = json_integer_value(j);
		if (json_t* j = json_object_get(rJ, "lovel")) r.lovel = json_integer_value(j);
		if (json_t* j = json_object_get(rJ, "hivel")) r.hivel = json_integer_value(j);
		r.undamped = json_is_true(json_object_get(rJ, "undamped"));
		if (json_t* j = json_object_get(rJ, "db"))
			r.gain = std::pow(10.f, (float) json_number_value(j) / 20.f);

		PianoSample s;
		if (!readWav(dir + "/samples/" + file, s)) {
			WARN("mpxPiano: could not read %s", file);
			continue;
		}
		// HOW LOUD THE RECORDING IS, measured once here rather than guessed at per note. Only the
		// first half second is looked at: a piano note's peak is in its attack, and scanning
		// whole samples would add seconds to a load that already takes a while.
		{
			const size_t look = std::min(s.data.size(), (size_t) (SAMPLES_RATE / 2) * 2);
			int loudest = 0;
			for (size_t k = 0; k < look; k++) {
				const int a = (s.data[k] < 0) ? -s.data[k] : s.data[k];
				if (a > loudest)
					loudest = a;
			}
			r.peak = (float) loudest / 32768.f;
			if (r.peak < 0.0001f)
				r.peak = 0.0001f;
		}
		r.sample = (int) bank->samples.size();
		bank->samples.push_back(std::move(s));
		bank->regions.push_back(r);
		if (progress)
			progress->store((float) (i + 1) / (float) count, std::memory_order_relaxed);
	}
	json_decref(rootJ);

	// Hand the velocities of the layers left out to the layer above them.
	if (keepEvery > 1) {
		for (PianoRegion& r : bank->regions) {
			if (r.part != PART_STRINGS)
				continue;
			int below = 0;
			for (const PianoRegion& o : bank->regions)
				if (o.part == PART_STRINGS && o.lokey == r.lokey && o.hivel < r.lovel)
					below = std::max(below, o.hivel);
			r.lovel = below + 1;
		}
	}
	INFO("mpxPiano: loaded %d recordings, %d velocity layers", (int) bank->samples.size(), layers);
	return bank;
}


// ---- the one shared copy -----------------------------------------------------------------------

/** EVERY mpxPiano SHARES ONE COPY. Loaded when the first one arrives, freed when the last one goes,
so a second instance costs only its own voices — which is what makes it unnecessary to stop
anybody adding one.

The audio thread reads `bank` and nothing else. A new bank is built on a worker thread, handed
over through `pending`, and swapped in on the UI thread; the old one is kept for a moment before
it is freed, so an audio thread still halfway through a sample from it never reads freed memory. */
struct Library {
	enum State { ABSENT, DOWNLOADING, UNPACKING, LOADING, READY, FAILED };
	std::atomic<int> state{ABSENT};
	std::atomic<float> progress{0.f};
	/** The download's progress, written by Rack's downloader through a plain pointer. */
	float downloadProgress = 0.f;
	std::atomic<PianoBank*> bank{NULL};
	std::atomic<PianoBank*> pending{NULL};
	std::atomic<bool> busy{false};
	std::mutex messageMutex;
	std::string message;
	int users = 0;
	int layers = 16;
	struct Grave { PianoBank* bank; double at; };
	std::vector<Grave> graveyard;

	void setMessage(const std::string& m) {
		std::lock_guard<std::mutex> lock(messageMutex);
		message = m;
	}
	std::string getMessage() {
		std::lock_guard<std::mutex> lock(messageMutex);
		return message;
	}
};

static Library gLib;


static int countSamples(const std::string& dir) {
	int n = 0;
	// ASKED BEFORE LISTED. Rack's listing throws on a folder that is not there, and before the
	// first download it is not.
	if (!system::isDirectory(dir + "/samples"))
		return 0;
	for (const std::string& p : system::getEntries(dir + "/samples"))
		if (string::lowercase(system::getExtension(p)) == ".wav")
			n++;
	return n;
}


static bool samplesInstalled() {
	const std::string dir = pianoDir();
	return system::isFile(dir + "/map.json") && countSamples(dir) >= SAMPLES_EXPECTED;
}


static void startLoad() {
	bool expected = false;
	if (!gLib.busy.compare_exchange_strong(expected, true))
		return;
	const int layers = gLib.layers;
	gLib.state = Library::LOADING;
	gLib.progress = 0.f;
	std::thread([layers]() {
		PianoBank* bank = loadBank(pianoDir(), layers, &gLib.progress);
		if (bank) {
			gLib.pending.store(bank);
			gLib.state = Library::READY;
		}
		else {
			gLib.setMessage("The samples could not be read. Use the menu to download them again.");
			gLib.state = Library::FAILED;
		}
		gLib.busy = false;
	}).detach();
}


static void startDownload() {
	bool expected = false;
	if (!gLib.busy.compare_exchange_strong(expected, true))
		return;
	gLib.state = Library::DOWNLOADING;
	gLib.downloadProgress = 0.f;
	const int layers = gLib.layers;
	std::thread([layers]() {
		const std::string dir = pianoDir();
		const std::string archive = dir + "/download.tar.zst";
		system::createDirectories(dir);
		if (!network::requestDownload(SAMPLES_URL, archive, &gLib.downloadProgress)) {
			gLib.setMessage("The download failed. Check the connection and press Download again.");
			gLib.state = Library::FAILED;
			gLib.busy = false;
			return;
		}
		gLib.state = Library::UNPACKING;
		try {
			system::unarchiveToDirectory(archive, dir);
		}
		catch (std::exception& e) {
			WARN("mpxPiano: unpacking failed: %s", e.what());
			gLib.setMessage("The download could not be unpacked. Press Download again.");
			gLib.state = Library::FAILED;
			gLib.busy = false;
			return;
		}
		system::remove(archive);
		// CHECKED, NOT ASSUMED. A download cut off partway unpacks into fewer files than the map
		// names, and a piano with holes in it is worse than one that says it is not there.
		if (countSamples(dir) < SAMPLES_EXPECTED || !system::isFile(dir + "/map.json")) {
			gLib.setMessage("The download was incomplete. Press Download again.");
			gLib.state = Library::FAILED;
			gLib.busy = false;
			return;
		}
		gLib.state = Library::LOADING;
		gLib.progress = 0.f;
		PianoBank* bank = loadBank(dir, layers, &gLib.progress);
		if (bank) {
			gLib.pending.store(bank);
			gLib.state = Library::READY;
		}
		else {
			gLib.setMessage("The samples could not be read. Press Download again.");
			gLib.state = Library::FAILED;
		}
		gLib.busy = false;
	}).detach();
}


/** UI thread, every frame, from any mpxPiano: swaps in a finished bank and frees old ones. */
static void libraryStep() {
	if (PianoBank* fresh = gLib.pending.exchange(NULL)) {
		PianoBank* old = gLib.bank.exchange(fresh);
		if (old)
			gLib.graveyard.push_back({old, system::getTime()});
	}
	const double now = system::getTime();
	for (size_t i = 0; i < gLib.graveyard.size();) {
		if (now - gLib.graveyard[i].at > 2.0) {
			delete gLib.graveyard[i].bank;
			gLib.graveyard.erase(gLib.graveyard.begin() + i);
		}
		else
			i++;
	}
}


static void libraryAcquire() {
	gLib.users++;
	if (gLib.bank.load() || gLib.busy.load())
		return;
	if (samplesInstalled())
		startLoad();
	else
		gLib.state = Library::ABSENT;
}


static void libraryRelease() {
	if (--gLib.users > 0)
		return;
	// THE LAST ONE HAS GONE: the samples go with it, rather than sitting in memory for a
	// patch that no longer plays them.
	if (PianoBank* old = gLib.bank.exchange(NULL))
		gLib.graveyard.push_back({old, system::getTime()});
	if (gLib.state == Library::READY)
		gLib.state = Library::ABSENT;
}


// ---- the module --------------------------------------------------------------------------------

/** THE MOST NOTES TAKEN AS ONE CHORD. Ten fingers, and a few more for a hand that has just
rolled one, after which balancing them against each other stops meaning anything. */
static const int MAX_CHORD = 16;

static const int MAX_VOICES = 96;
/** Voices kept back for the mechanical sounds, so a chord never loses a string to a key noise. */
static const int NOISE_VOICES = 24;

struct PianoVoice {
	bool active = false;
	const PianoRegion* region = NULL;
	const PianoSample* sample = NULL;
	int part = PART_STRINGS;
	int64_t source = 0;
	double pos = 0.0;
	double step = 1.0;
	float pitchV = 0.f, bendV = 0.f;
	float amp = 1.f;
	float env = 1.f;
	float decay = 1.f;          // per-sample envelope multiplier while releasing
	bool keyDown = false;
	/** HOW MANY SAMPLES THE KEY HAS LEFT BEFORE IT COMES UP OF ITS OWN ACCORD, or nought for "wait
	to be told". A source reading from a score says how long a note is when it starts it and sends
	nothing afterwards; see `noteOn`. */
	int64_t endsIn = 0;
	bool releasing = false;
	bool undamped = false;
	float timbre = 0.5f;
	float follow = 0.f, peak = 0.f;
	float lpL = 0.f, lpR = 0.f;
	int key = 60, vel = 64;
	/** HOW LONG AGO THIS WAS STRUCK, in samples. A voice is chosen to be taken by how loud it is
	and a note that has only just been struck is the quietest thing in the pool — its envelope has
	not climbed yet — so without this the newest note is always the first one taken. See
	`allocate`, and PROTECT_SECONDS for the measurement that found it. */
	int64_t struckAgo = 0;
};


struct PianoModule : Module, NoteSink, NoteSource {
	enum ParamId {
		P_VOLUME,
		P_RELEASE,
		P_EMPHASIS,
		P_PEDAL,
		P_BRIGHT,
		P_DYNAMICS,
		P_DAMPING,
		P_SUSTAIN,
		P_SOFT,
		NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_L, O_R, O_MPX, NUM_OUTPUTS };
	enum LightId { NUM_LIGHTS };

	BusReader reader;
	std::atomic<bool> relink{false};
	std::atomic<int> wantCount{0};
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];

	PianoVoice voices[MAX_VOICES];
	const PianoBank* bankSeen = NULL;
	float sustainWas = 0.f;
	/** Whether the log has been told that keys are coming up by their own length. */
	bool saidLifting = false;

	// Menu settings, kept with the patch.
	int curve = 1;              // 0 soft, 1 normal, 2 hard
	float reference = 440.f;
	bool stretch = true;
	/** THE CABLE OUT. What the piano plays, as notes, so the playing can drive something else.

	THIS MODULE IS A PIANIST AS WELL AS A PIANO. The voicing of a chord, the melody leading the
	notes under it, the legato overlap and the pedalling by the metre are all decisions about
	PLAYING, and until now they could only be heard through these samples. Published here, they
	are notes like any others: through mpxOut they will drive an oscillator, and anything else on
	the cable can read, watch or vary them.

	WHAT GOES OUT IS WHAT IS PLAYED, not what arrived. Each note is published at the moment it
	actually speaks, so the twenty milliseconds by which an inner note follows the melody are in
	the timing itself and need no describing; the level is the voiced one and the length is the
	one the legato gave it. */
	int slot = -1;
	uint32_t generation = 0;

	/** HOW FAR UNDER THE MELODY AN ACCOMPANIMENT IS PLAYED, as a fraction of its written level.

	ONE AT ALL IS NOT A SETTING OF TASTE. A fugue has no accompaniment: its voices are equal, and
	holding the lower ones back is simply wrong. A nocturne is the opposite. So this is off for
	counterpoint and strong for a Romantic texture, and nothing can tell which from the notes. */
	float melodyBalance = 0.35f;

	int polyphony = 32;
	/** HOW LOUD THE PEDAL'S OWN MECHANISM IS. A knob until the knob was wanted for the pedalling
	itself, and a thing nobody adjusts twice, so it lives in the menu now. */
	float pedalNoise = 1.f;
	/** HOW LOUD THE KEY COMING UP IS. A knob until the knob was wanted for the emphasis, and a
	thing nobody adjusts twice: the mechanical noises are a hundredth of what the metre does to a
	performance. Beside the pedal's own noise in the menu now. */
	float hammerNoise = 1.f;

	/** HOW FAR A SLURRED NOTE IS HELD PAST ITS WRITTEN END, in seconds. A pianist's finger overlap
	is a few tens of milliseconds whatever the note is worth; forty is in the middle of what is
	measured of real playing. The written notes of a score do not overlap at all — in the Ballade
	consecutive melody notes butt exactly end to end — so all of it has to come from here. */
	static constexpr float LEGATO_OVERLAP = 0.04f;

	/** HOW A CHORD IS SPREAD IN TIME.

	A PIANIST DOES NOT STRIKE A CHORD AT ONE INSTANT, and the way they miss is not random. The
	melody note sounds BEFORE the rest, by twenty to thirty milliseconds; it is the best documented
	asynchrony in piano playing, and it is not untidiness — a note arriving a little early is heard
	as a line of its own rather than as part of a block, which is how a player makes a tune stand
	out of a chord.

	AND IT FOLLOWS FROM HOW HARD EACH NOTE IS STRUCK. A key pressed harder accelerates its hammer
	faster and reaches the string sooner: the time from key to string runs from a few tens of
	milliseconds at a loud attack to well over a hundred at a soft one. So the notes played hardest
	simply arrive first, whether the player means it or not, and the measured lead tracks how much
	louder the melody is than what is under it.

	WHICH MEANS THIS IS NOT A SEPARATE INVENTION. The chord is already voiced — the tune at full,
	the bass a little under it, the inner notes held back — so the order falls out of the levels
	themselves: the tune first, the bass just after it, the inner voices last. Fifty milliseconds
	per unit of level puts the gap between a full melody note and an inner one at about twenty,
	which is where the measurements sit.

	NOTHING MOVES THAT IS NOT IN A CHORD. The delay is measured from the loudest note of the chord,
	so that note falls exactly on the beat and a single note is never moved at all: the rhythm is
	where it was written and only the inside of a chord is rearranged. */
	static constexpr float SPREAD_PER_LEVEL = 0.05f;
	/** No note of a chord lags further than this, however quiet it has been voiced. */
	static constexpr float SPREAD_MOST = 0.035f;
	/** A LITTLE MORE BETWEEN THE INNER NOTES OF A BIG CHORD, bottom to top, so that four or five
	of them voiced alike do not land in one instant after all. Pianists spread a big chord. */
	static constexpr float SPREAD_INNER = 0.0015f;

	/** HOW A PIANIST BALANCES THE HANDS.

	THE VOICING ABOVE BALANCES NOTES STRUCK TOGETHER, which is only half of it. A left-hand chord
	falling between two melody notes is a group of its own, so its top note was taken for the tune
	and given full weight with two or three more notes underneath it: measured on the Chopin, a
	four-note accompaniment chord came out 3.7 dB ABOVE a melody note at the same written dynamic,
	where a pianist puts it five to ten below. Fourteen decibels the wrong way.

	A PIANIST BALANCES LINES, NOT NOTES. The melody is projected and everything under it is held
	back, all the time and not only when they happen to sound together. That is the most basic
	fact about piano texture and the thing that makes an accompaniment an accompaniment.

	THE MELODY IS THE TOP OF THE TEXTURE, which needs nothing the cable does not already carry.
	Measured on the Ballade: of 462 moments where only the left hand plays, 455 are entirely below
	the melody last heard — two are not. The same 99% holds where the hands coincide.

	AND IT HAS TO BE ABLE TO FOLL0W THE MUSIC DOWN. The top is kept as a decaying maximum rather
	than a high-water mark: any note raises it to at least its own pitch, and it falls steadily
	when nothing higher arrives. A passage with no right hand at all therefore becomes its own
	melody within a second or two, so a bass line alone is not crushed by a tune that stopped
	playing. */
	float melodyTop = 60.f;
	/** HOW FAST THE TOP OF THE TEXTURE IS FORGOTTEN, in semitones per quarter note.

	MEASURED IN MUSIC, NOT IN SECONDS. A second is four beats of a fast piece and less than one of
	a slow one, so forgetting at so many semitones a second forgets a Largo's melody in the middle
	of its first note and holds a Presto's long after it has moved on. The cable carries the
	played position in quarter notes, so the fall is driven by how far the MUSIC has moved: it
	follows a tempo change, and it stops when the transport stops, which is right — a melody does
	not fade from the ear because somebody pressed pause. */
	static constexpr float MELODY_FALL_PER_BEAT = 3.f;
	/** WHERE NOTHING PUBLISHES A BEAT — a keyboard played by hand — there is no music to measure,
	so the clock is all there is. Six semitones a second is an octave in two, which is what this
	was before there was anything better. */
	static constexpr float MELODY_FALL_CLOCK = 6.f;
	/** The played position last looked at, to see how far the music has moved since. */
	double beatWas = 0.0;
	bool haveBeat = false;
	int beatLook = 0;
	/** A note this many semitones under the melody is accompaniment; one within the first is the
	melody itself. Between the two it is mixed, so a line crossing the melody does not jump. */
	static constexpr float MELODY_NEAR = 2.f;
	static constexpr float MELODY_FAR = 12.f;

	/** HOW LONG THE FOOT IS OFF THE FLOOR when it changes the pedal: long enough for the dampers
	to touch and clear what was ringing, short enough that the note being played through the
	change is not cut. The same fifty milliseconds mpxScore uses for a written pedal change. */
	static constexpr float LIFT_SECONDS = 0.05f;
	float autoLift = 0.f;
	double steppedWas = -1e18;
	/** HOW LONG THE CABLE'S OWN PEDAL HAS BEEN UP. While a score is pedalling for itself the knob
	stays out of its way; once the score has left the pedal up for two bars it plainly is not
	pedalling here, and the knob takes over. */
	float cableQuiet = 0.f;

	PianoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_VOLUME, 0.f, 2.f, 1.f, "Volume", "%", 0.f, 100.f);
		configParam(P_RELEASE, 0.f, 2.f, 1.f, "Release",
			"%", 0.f, 100.f);
		// WHERE THE WEIGHT OF THE BAR GOES. Two-sided about the middle: see metre.
		configParam(P_EMPHASIS, -1.f, 1.f, 0.f, "Emphasis", "%", 0.f, 100.f);
		// PEDALLING BY THE METRE. The positions are an axis from driest to wettest, measured
		// against the pedal marks of two Chopin pieces: see pedalFor and docs/piano.md.
		configSwitch(P_PEDAL, 0.f, 5.f, 0.f, "Pedalling",
			{"Off", "Every beat", "Every half bar", "Every bar", "Every two bars", "Held down"});
		configParam(P_BRIGHT, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);
		configParam(P_DYNAMICS, 0.f, 1.f, 1.f,
			"Dynamics", "%", 0.f, 100.f);
		// HOW LONG A DAMPER TAKES TO STOP THE STRING, which is about a tenth of a second on a real
		// piano and was nearly a whole one here.
		//
		// THE RANGE AND THE DEFAULT WERE BOTH WRONG. It ran from a twentieth of a second to two
		// seconds, with the default at four fifths of the travel — which is 0.95 seconds, so every
		// note rang for most of a second after the key came up. Worse, the whole useful part of
		// the sweep lay in the bottom half: from the middle upward it went from a third of a
		// second to two, and every setting up there sounds like every other. It read as a knob
		// that did nothing.
		//
		// A fiftieth of a second to four fifths, with the default in the middle of the travel at
		// about an eighth of a second — a damper rather than a long fade.
		//
		// CLOCKWISE IS MORE DAMPING, which is the way round the word means and was the way round
		// it was not. The number underneath is how long the damper takes to fall, so turning it up
		// made the note last LONGER: a control called DAMPING that damped less the further it was
		// turned. The travel is reversed rather than the control renamed, because what somebody
		// wants from a thing called damping is more of it.
		//
		// The display shows the time, so its base and multiplier are reversed with the travel:
		// four fifths of a second times a fortieth to the power of the knob runs from 0.8 down to
		// 0.02, which is what the tooltip now reads.
		configParam(P_DAMPING, 0.f, 1.f, 0.5f,
			"Damping", " s", 1.f / 40.f, 0.8f);
		// HELD DOWN WHILE THE BUTTON IS HELD, as a pedal is. Latching them meant a pedal left
		// down by a click made days ago, and a foot is the one control on a piano nobody would
		// expect to stay where it was put.
		configButton(P_SUSTAIN, "Sustain pedal");
		configButton(P_SOFT, "Soft pedal");
		configInput(I_MPX, "MPX note");
		configOutput(O_L, "Left");
		configOutput(O_R, "Right");
		configOutput(O_MPX, "MPX note, as played");

		slot = busClaim(&generation);
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i] = -1;
			wantGenerations[i] = 0;
		}
	}

	bool isMPXInputId(int id) override {
		return id == I_MPX;
	}

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++)
			same = (wantSlots[i].load() == slots[i] && wantGenerations[i].load() == generations[i]);
		if (same)
			return;
		for (int i = 0; i < n; i++) {
			wantSlots[i] = slots[i];
			wantGenerations[i] = generations[i];
		}
		wantCount = n;
		relink = true;
	}

	~PianoModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* generation) override {
		if (outputId != O_MPX)
			return -1;
		if (generation)
			*generation = this->generation;
		return slot;
	}

	void onReset() override {
		curve = 1;
		reference = 440.f;
		stretch = true;
		polyphony = 32;
	}

	// ---- turning a note into a voice ----

	/** Where on the keyboard, in semitones from C-1, with the reference pitch and the stretch. */
	float noteOf(float pitchV) const {
		float m = 60.f + 12.f * pitchV + 12.f * std::log2(reference / 440.f);
		// STRETCH TUNING, as a piano tuner leaves it: slightly flat at the bottom and sharp at the
		// top. An exactly tempered piano sounds faintly wrong to anyone who knows the instrument.
		if (stretch) {
			const float d = m - 69.f;
			m += math::clamp(0.00035f * d * d * d, -40.f, 40.f) / 100.f;
		}
		return m;
	}

	int velocityOf(float level) const {
		float v = math::clamp(level, 0.f, 1.f);
		if (curve == 0)
			v = std::pow(v, 0.6f);
		else if (curve == 2)
			v = std::pow(v, 1.6f);
		return math::clamp((int) std::lround(1.f + 126.f * v), 1, 127);
	}

	void aim(PianoVoice& v, float sampleRate) {
		if (!v.region)
			return;
		const float m = noteOf(v.pitchV + v.bendV);
		v.step = std::pow(2.0, (m - v.region->key) / 12.0) * SAMPLES_RATE / sampleRate;
	}

	/** HOW LONG A NOTE IS SAFE FROM BEING TAKEN, in seconds.

	A VOICE IS CHOSEN BY HOW LOUD IT IS, AND A NOTE THAT HAS JUST BEEN STRUCK IS SILENT. `follow`
	is the voice's own output, smoothed over twenty milliseconds, so for the first few hundredths
	of a second a note that is about to be the loudest thing in the room measures as the quietest
	voice in the pool — and is therefore the first one taken when the next note arrives.

	WHAT THAT WAS MISTAKEN FOR. The two small notes finishing the Nocturne's trill were reported
	as not being played at all. They were being played; the log showed the melody note after them
	taken ten milliseconds after it started, by an accompaniment note at a third of its level. A
	note cannot be judged on its loudness before it has made any. */
	static constexpr float PROTECT_SECONDS = 0.05f;

	/** WHERE THE OUTPUT STOPS GETTING LOUDER, in volts, and how quickly it gives way and comes
	back. Ten is mpxGuitar's figure for the same thing, which is twice Rack's nominal level for
	audio: by the time a piano is there, something has piled up. A millisecond to take hold is
	fast enough that only the very front of a transient passes; a sixth of a second to let go is
	slow enough that the gain moving is not itself heard. */
	static constexpr float LIMIT_VOLTS = 10.f;
	static constexpr float LIMIT_ATTACK = 0.001f;
	static constexpr float LIMIT_RELEASE = 0.15f;
	/** How much the limiter is holding back now, one when it is doing nothing. */
	float limitGain = 1.f;

	/** HOW MUCH LOUDER A STRING CAN GET FROM BEING STRUCK AGAIN while it is still sounding, as a
	multiple of what the new strike alone would give. A repeatedly struck string reaches a steady
	amplitude rather than growing without limit, and a half again is about four decibels — enough
	for a sustained trill to bloom under the pedal, not enough to swallow what follows it. */
	static constexpr float BLOOM_MOST = 1.5f;

	/** HOW LONG THE OLD SOUND ON A RE-STRUCK STRING TAKES TO GET OUT OF THE WAY, in seconds.
	Long enough that it is a fade and not a step — a step is a click — and short enough that it
	is gone well before the next note of an ornament arrives. Four milliseconds is sixty decibels
	down in about a fiftieth of a second and inaudible as a separate event. */
	static constexpr float RESTRIKE_FADE = 0.004f;

	/** HOW LONG A ROLLED CHORD TAKES, per note, in seconds.

	THE SCORE DOES NOT SAY. MusicXML's arpeggiate sign carries a direction and nothing else — no
	speed — so how long the hand takes over it is the player's, which is where it belongs. Thirty
	five milliseconds a note puts a four-note roll a little over a tenth of a second, which reads
	as a roll rather than as a chord played badly, and a ballad's spread chords want about that. */
	static constexpr float ROLL_PER_NOTE = 0.035f;

	PianoVoice* allocate(bool noise, float sampleRate) {
		const int lo = noise ? MAX_VOICES - NOISE_VOICES : 0;
		const int hi = noise ? MAX_VOICES : std::min(polyphony, MAX_VOICES - NOISE_VOICES);
		const int64_t protect = (int64_t) ((double) PROTECT_SECONDS * (double) sampleRate);
		PianoVoice* quietest = NULL;
		PianoVoice* anything = NULL;
		for (int i = lo; i < hi; i++) {
			if (!voices[i].active)
				return &voices[i];
			const float heard = voices[i].follow * voices[i].env;
			// THE QUIETEST IS TAKEN when all are in use: a piano's notes decay, so the one that
			// has faded most is the one least missed. A note too new to have been heard yet is
			// passed over, unless every voice is that new and something has to go.
			if (!anything || heard < anything->follow * anything->env)
				anything = &voices[i];
			if (voices[i].struckAgo < protect)
				continue;
			if (!quietest || heard < quietest->follow * quietest->env)
				quietest = &voices[i];
		}
		return quietest ? quietest : anything;
	}

/** THE LEVELS THE SET ITSELF ASKS FOR, which were lost on the way in.

THE SFZ PUTS EACH KIND OF RECORDING IN A GROUP WITH ITS OWN VOLUME, and map.json kept none of
them: every region came through at unity. The strings are unity in the SFZ too, so they were
right by accident; everything else was not.

	the key-release noise   volume=-37      and was being played at nought
	the release resonance   volume=-4
	the pedal               volume=-20 down, -19 up

WHAT THAT SOUNDED LIKE, and it is why the DAMPING knob appeared dead through three attempts at
it. The key-release noise is a mechanical sound with no damper to answer to, so the knob does not
touch it and should not — and at seventy times the level the set asks for it was LOUDER THAN THE
NOTE: measured at 73.9 decibels against the note's own 68.4, and at the same level whatever the
knob was set to. Every released key fired one. So the loudest thing in the instrument was the one
thing the damper could not reach, and the strings underneath it were damping correctly the whole
time, exactly as they appeared not to be.

Taken from original.sfz, which ships beside the samples, rather than tuned by ear. */
static float partGain(int part) {
	switch (part) {
		case PART_HAMMER:
			return 0.0141f;        // -37 dB
		case PART_RESONANCE:
			return 0.631f;         // -4 dB
		case PART_PEDAL_DOWN:
		case PART_PEDAL_UP:
			return 0.1f;           // -20 dB
		default:
			return 1.f;
	}
}


	/** `dies` is how long this one-shot is allowed to last, in seconds, or nought to let the
	recording play out as it was recorded. */
	void startOneShot(const PianoRegion* r, float gain, int part, int key, float sampleRate,
			float dies = 0.f) {
		const PianoBank* bank = gLib.bank.load();
		if (!r || !bank || gain <= 0.0001f)
			return;
		PianoVoice* v = allocate(true, sampleRate);
		if (!v)
			return;
		*v = PianoVoice();
		v->active = true;
		v->region = r;
		v->sample = &bank->samples[r->sample];
		v->part = part;
		v->amp = gain * r->gain * partGain(part);
		v->key = key;
		// Pitched like the strings when the recording is one per zone, as the resonances are; the
		// hammer noise is one recording per key and is played as it was recorded.
		const float m = (part == PART_RESONANCE) ? noteOf(((float) key - 60.f) / 12.f) : (float) r->key;
		v->step = std::pow(2.0, (m - r->key) / 12.0) * SAMPLES_RATE / sampleRate;
		if (dies > 0.f) {
			v->releasing = true;
			v->decay = std::exp(-6.9f / (dies * sampleRate));
		}
	}

	/** LETS A KEY UP, by whatever told it to: a note-off, or its own length running out. */
	void liftKey(PianoVoice& v, float sampleRate, float sustain, bool heard) {
		v.keyDown = false;
		v.endsIn = 0;
		const PianoBank* bank = gLib.bank.load();
		// The key coming up is heard whatever the pedal is doing.
		if (heard && bank)
			startOneShot(bank->find(PART_HAMMER, v.key, v.vel),
				hammerNoise * v.amp, PART_HAMMER, v.key, sampleRate);
		if (sustain < 0.95f)
			damp(v, sampleRate, sustain);
	}

	/** THE KEYS WHOSE TIME IS UP.

	WHY THIS EXISTS AT ALL, which is worth setting down because its absence was taken for a fault
	in the samples. The cable's protocol lets a source say how long a note is at the moment it
	starts it instead of sending a note-off later, and a source reading from a score knows the
	length and uses it: mpxScore, mpxGuitarChart and mpxGuitarChart6 send an Event::ON with a
	duration and NOTHING afterwards — there is not one Event::OFF in GuitarChart.hpp. This module
	released only on a note-off, so driven from a score it never damped a single note: they rang
	until the sample ran out or until one of thirty-two voices was taken for something else.

	WHAT THAT WAS MISTAKEN FOR. A Bach fugue played through it sustained into a smear, which reads
	as a sampled piano that will not stop — and the DAMPING knob appeared to do nothing, which it
	did not, because the thing it adjusts was never reached. mpxGuitar has always read e.duration;
	this did not, and neither did the modelled piano until the same fault was found there. */
	void liftDueKeys(float sampleRate, float sustain) {
		for (PianoVoice& v : voices) {
			if (!v.active || v.part != PART_STRINGS || !v.keyDown || v.endsIn <= 0)
				continue;
			if (--v.endsIn == 0) {
				// SAID ONCE, IN THE LOG, because "it compiled" is not evidence that it runs. The
				// first version of this read the duration into a field that was never written —
				// a replacement that silently matched nothing — so the countdown skipped every
				// voice and not one note was damped. It built cleanly and the fault was found by
				// someone listening to it. One line in the log says the mechanism is alive.
				if (!saidLifting) {
					saidLifting = true;
					INFO("mpxPiano: letting keys up by their own length (first at key %d, %.0f ms)",
						v.key, 1000.0 * (double) v.endsIn / (double) sampleRate);
				}
				liftKey(v, sampleRate, sustain, true);
			}
		}
	}

	/** HOW AN ARTICULATION IS PLAYED ON A PIANO. There is one way to sound a piano note — a
	hammer at a speed — so every mark a score carries reaches the instrument as some mixture of
	how hard the key is struck and how long the finger stays on it, and nothing else. An accent
	is a faster hammer, which is both louder and brighter because the recorded layers are. A
	staccato is the same hammer and an early release. A note under a slur is held until the next
	one is down, which is the overlap that makes a line sound joined rather than typed. */
	void shapeByMark(uint32_t technique, float& level, float& duration) const {
		if (technique & Event::HEAVY_ACCENT)
			level = std::min(1.f, level * 1.45f);
		else if (technique & Event::ACCENT)
			level = std::min(1.f, level * 1.25f);
		if (duration <= 0.f)
			return;
		if (technique & Event::STACCATO) {
			duration *= 0.45f;
			return;
		}
		// LEGATO IS AN OVERLAP OF TIME, NOT OF PROPORTION.
		//
		// A FINGER HOLDS ON FOR A MOMENT after the next note is down, and that moment is about
		// the same however long the note is worth — a pianist does not hold a minim ten times as
		// far past the next note as a quaver. Stretching by a fraction of the note did exactly
		// that, and backwards: a note in a fast run got fifteen milliseconds, where the join
		// matters most, and a long note sixty, where it hardly matters at all.
		//
		// AND IT NEVER REACHES THE NOTE AFTER NEXT. Capped against the note's own length as well,
		// since a very short note stretched by a fixed time would run past its neighbour and
		// through the one beyond it.
		if (technique & Event::SLURRED)
			duration += std::min(LEGATO_OVERLAP, duration * 0.35f);
	}

	/** HOW A CHORD IS BALANCED AGAINST A SINGLE NOTE.

	THREE NOTES STRUCK TOGETHER ARE FOUR AND EIGHT TENTHS OF A DECIBEL LOUDER than one, and six are
	nearly eight — which is why a line that alternates single notes and chords comes out lopsided.
	In the Ballade a single note sits next to a chord a thousand times.

	A PIANIST DOES NOT DIVIDE BY THREE. A chord really is louder than a single note, and should be.
	But two things hold it back that nothing here was doing. The arm's weight is shared out among
	the fingers, so the same gesture across three keys gives each key less than one finger takes
	alone. And a pianist VOICES a chord: the note carrying the tune is played firmly and the inner
	notes are held back, which is one of the most characteristic things about good playing and the
	reason a real chord does not swamp the line.

	SO THE TOP NOTE KEEPS ITS LEVEL, the lowest keeps most of its weight, since the bass is the
	harmony's foundation and is played firmly, and everything between is held back. A three-note
	chord then comes out about three decibels above a single note instead of nearly five, and a
	six-note chord about five instead of eight: still louder, which is right, but no longer
	drowning what it is answering.

	WHAT COUNTS AS A CHORD is whatever is struck in the same sample, which is what reaches here in
	one go. A rolled chord arrives spread over tens of milliseconds and is left alone, which is
	also right: it is played note by note. */
	/** How much of its written level a note keeps, for where it sits under the melody. */
	float underMelody(float midi) const {
		if (melodyBalance >= 0.999f)
			return 1.f;
		const float below = melodyTop - midi;
		if (below <= MELODY_NEAR)
			return 1.f;
		if (below >= MELODY_FAR)
			return melodyBalance;
		const float how = (below - MELODY_NEAR) / (MELODY_FAR - MELODY_NEAR);
		return 1.f + (melodyBalance - 1.f) * how;
	}

	static float voicing(int rank, int count) {
		if (count < 2)
			return 1.f;
		if (rank == count - 1)
			return 1.f;         // The top note, which is the one being listened to.
		if (rank == 0)
			return 0.8f;        // The bass, which carries the harmony.
		return 0.6f;            // Everything in between.
	}

	/** This instrument's share of a chord's dynamic is the shared one: see mpxShare, which
	every instrument on the cable uses, and which carries the measurements it was settled by.
	Here it rides to noteOn as a gain and never touches `level` — put through the level it would
	move the note down the decibel scale instead of quietening it, and a six-note chord at forte
	came out quieter than a single forte note. */
	static float density(int count) {
		return mpxShare(count);
	}

	/** HOW STRONG A POSITION IN THE BAR IS, one for a downbeat down to a fifth for anything
	finer than an eighth off the beat. The ladder Phrasing.cpp already uses to decide where a
	phrase may break, which is the same hierarchy read for a different purpose. */
	static float metricStrength(float beatsIntoBar, int barBeats) {
		const float eps = 1e-3f;
		if (beatsIntoBar < eps)
			return 1.f;
		const int whole = (int) std::lround(beatsIntoBar);
		if (std::fabs(beatsIntoBar - (float) whole) < eps) {
			if (barBeats % 2 == 0 && whole == barBeats / 2)
				return 0.7f;       // The middle of an even bar carries the second weight.
			return 0.5f;
		}
		const float withinBeat = beatsIntoBar - std::floor(beatsIntoBar);
		if (std::fabs(withinBeat - 0.5f) < eps)
			return 0.3f;
		return 0.2f;
	}

	/** AN ORDINARY BEAT, which the emphasis is measured either side of. */
	static constexpr float METRE_PLAIN = 0.5f;
	/** HOW FAR THE EMPHASIS REACHES at the end of its travel, in decibels per unit of strength.
	A downbeat is half a unit above an ordinary beat, so four gives it two decibels — about what
	a player puts there, and small beside the voicing and the melody lead, which is where it
	belongs. Heard as a pulse rather than felt as one means it is too strong. */
	static constexpr float METRE_DB = 4.f;
	/** AND HOW MUCH LONGER a leaned-on note is held, as a fraction of its length. An agogic
	accent: players give a strong beat a little more time as well as a little more weight, and
	it is arguably the larger half of what stops a performance sounding typed. Only the length
	is touched and never the onset — moving a note in time would walk away from the score's own
	cursor and from every other instrument reading the same bar. */
	static constexpr float METRE_LONGER = 0.09f;

	/** WHERE THE WEIGHT OF THE BAR GOES, as a gain and a stretch on one note.

	NOT A DYNAMIC MARKING, AND NOT IN THE SCORE. A score says forte once and means it for twenty
	bars; what a player does inside those bars is lean very slightly on the strong positions, and
	nobody writes it down because every player already knows. Its absence is a good part of why a
	sequenced performance sounds mechanical even when every marked dynamic is right.

	TWO-SIDED ABOUT THE MIDDLE. Clockwise leans on the bar — the downbeat, then the middle of an
	even bar — and lets the positions between beats fall back. Anticlockwise does the reverse and
	leans on what falls between the beats, which is what a syncopated line wants and what makes
	the same rule useful for music that is written against the metre rather than along it. In the
	middle it does nothing at all.

	IT DOES NOT ARGUE WITH THE SCORE. A note the composer has already accented is left alone
	entirely: the weight has been placed by hand there, and a rule that knows only the bar line
	has nothing to add and a good deal to spoil — on a syncopation, which is exactly where accents
	are written, it would be pulling the other way. */
	void metre(const Harmony& h, uint32_t technique, float& gain, float& duration) {
		const float set = params[P_EMPHASIS].getValue();
		if (std::fabs(set) < 0.01f || h.barBeats < 1)
			return;
		if (technique & (Event::ACCENT | Event::HEAVY_ACCENT))
			return;
		// THE BAR POSITION IN BEATS OF THE WRITTEN KIND, since beatInBar is in quarter notes and
		// a bar of six eight does not have six quarter-note beats in it.
		const float beats = h.beatInBar * (float) h.barUnit / 4.f;
		const float away = metricStrength(beats, (int) h.barBeats) - METRE_PLAIN;
		gain *= std::pow(10.f, away * METRE_DB * set / 20.f);
		if (duration > 0.f)
			duration *= 1.f + away * METRE_LONGER * set;
	}

	/** HOW LOUD THIS NOTE IS TO BE, as a gain on the recording about to be played.

	A MARKING NAMES A NUMBER OF DECIBELS, and the instrument's job is to produce it. See
	MPX_RANGE_DB: level is a decibel scale, forte is a fixed distance below full, and it is that
	distance in every score — a piece that never rises above mezzo-forte stays quieter than one
	that reaches fff, because that is what the composer wrote.

	THE LAYER STILL FOLLOWS THE MARKING, so a pianissimo keeps the tone of a pianissimo: which
	recording is chosen is a question about colour, and how loud it then plays is this. That
	division is what makes the DYNAMICS knob a clean control — narrowing the range moves the
	loudness and leaves the colour where it was.

	AND IT IS WHY THE REGISTER NO LONGER MATTERS. The Salamander set was recorded note by note
	and never normalised: the same marking varied by seven and a half decibels across the
	keyboard, with three point eight of that between A3 and middle C alone, which are neighbours.
	A piece whose loud writing happened to sit around F sharp 4 came out four decibels under one
	whose loud writing sat on middle C, both marked forte. Dividing each recording's own measured
	peak back out removes all of it, because the note is aimed at a figure rather than left where
	the performance landed.

	WHAT THIS REPLACED was the set's velocity tracking, seventy-three per cent, taken from its
	SFZ. That is a reasonable guess at how a piano behaves and it is not a target; it could not
	say what a note would come out at, only how it would scale.

	THIS NEEDS TO KNOW HOW LOUD THE NOTE WILL BE before playing it, which is why every recording
	is measured as it is read. See PianoRegion::peak. */
	float aimedAt(float level, const PianoRegion* r) {
		if (!r || r->peak <= 0.f)
			return 0.f;
		// WHAT THE MARKING IS WORTH, from the one table every instrument reads, narrowed by the
		// knob. One at fff, and that is MPX_FULL_VOLTS out once the volume stage has it.
		const float want = mpxLoudness(level, params[P_DYNAMICS].getValue());
		// AND THE RECORDING'S OWN LOUDNESS DIVIDED BACK OUT, which is what makes the marking a
		// number rather than an accident of which performance got sampled.
		return want / r->peak;
	}

	/** `share` is a gain on the string's amplitude and nothing else: see density, and the comment
	on v->amp for why it cannot be folded into `level`. One for a note standing on its own. */
	void noteOn(int64_t source, float pitchV, float level, float sampleRate, float soft,
			float duration, float share = 1.f) {
		// AND OUT ON THE CABLE, as played: see `slot`. Before anything that could return early,
		// so that what goes out does not depend on the samples being installed or on a region
		// being found for this key.
		if (slot >= 0) {
			Event out;
			out.kind = Event::ON;
			out.handle = source;
			out.pitch = pitchV;
			out.level = math::clamp(level, 0.f, 1.f);
			out.duration = duration;
			busPush(slot, out);
		}
		const PianoBank* bank = gLib.bank.load();
		if (!bank)
			return;
		const float m = noteOf(pitchV);
		const int key = math::clamp((int) std::lround(m), 21, 108);
		int vel = velocityOf(level);
		// THE LAYER FOLLOWS WHAT WAS WRITTEN, and DYNAMICS no longer touches it: see the gain
		// worked out below.
		float layerVel = (float) vel;
		// THE SOFT PEDAL, imitated: the set has no una corda recording, so it favours the softer
		// layers here and darkens the tone in the mix.
		layerVel *= 1.f - 0.35f * soft;
		const int lv = math::clamp((int) std::lround(layerVel), 1, 127);
		const PianoRegion* r = bank->find(PART_STRINGS, key, lv);
		if (!r)
			return;
		// ONE KEY IS ONE STRING, so a key struck again takes over the string it is already
		// sounding instead of adding a second one beside it.
		//
		// WHAT THIS WAS BEFORE AND WHAT IT COST. Every strike took a fresh voice, and with the
		// pedal down nothing is damped, so a trill accumulated one ringing string per strike: the
		// log of two bars of the Nocturne showed twelve copies of G5 and eleven of F5 sounding at
		// once, thirty of the thirty-two voices gone, and the two small notes that finish the
		// trill struck underneath all of it. They were inaudible, and the fault was reported as
		// the ornament not being played.
		//
		// IT IS ALSO NOT HOW A STRING BEHAVES. A hammer meeting a string that is already moving
		// partly re-excites and partly disturbs the motion that is there; what comes out is about
		// another note's worth of amplitude, not another note added to the last eleven. Nor do
		// the copies sum cleanly — the same recording started at eighty-millisecond intervals
		// combs against itself, which is why a trill came out as a blur rather than as a trill.
		//
		// THE RING IS KEPT, THOUGH, because a pedalled trill does bloom and should: what is still
		// sounding on the string is carried into the new strike rather than thrown away. The
		// bloom under a pedal properly belongs to the OTHER strings — every undamped string in
		// the instrument picking up energy from this one — and that is a separate piece of work;
		// see docs/piano.md. This keeps the growth in the one place a single string can give it.
		// THE STRING'S PRESENT SOUND IS FADED OUT, NOT CUT OFF.
		//
		// A RE-STRIKE CANNOT SIMPLY REWIND THE RECORDING. Taking the voice over and setting its
		// position back to nought jumps the output from wherever the waveform happened to be to
		// the start of a fresh attack, and a step in a waveform is a click. It is loudest where
		// re-strikes are closest: an inverted mordent in the Beethoven strikes F5 four times
		// inside a seventh of a second, and every one of them clicked.
		//
		// SO THE OLD SOUND IS GIVEN A FEW MILLISECONDS TO GET OUT OF THE WAY and the new note
		// starts on a voice of its own. The string still does not accumulate — the tail is
		// sixty decibels down before the next ornament note arrives and frees itself — so a
		// trill holds two voices rather than the twenty-three it used to.
		float carried = 0.f;
		for (int i = 0; i < MAX_VOICES - NOISE_VOICES; i++) {
			PianoVoice& old = voices[i];
			if (!old.active || old.part != PART_STRINGS || old.key != key)
				continue;
			// HOW MUCH OF IT IS LEFT, by the voice's own output against its loudest: the same
			// measure the release resonance is given. See damp.
			const float left = (old.peak > 0.f) ? math::clamp(old.follow / old.peak, 0.f, 1.f)
				: 0.f;
			carried = old.amp * left;
			old.keyDown = false;
			old.endsIn = 0;
			old.releasing = true;
			old.decay = std::exp(-6.9f / (RESTRIKE_FADE * sampleRate));
			break;
		}
		PianoVoice* v = allocate(false, sampleRate);
		if (!v)
			return;
		*v = PianoVoice();
		v->active = true;
		v->region = r;
		v->sample = &bank->samples[r->sample];
		v->part = PART_STRINGS;
		v->source = source;
		v->pitchV = pitchV;
		v->keyDown = true;
		// A DURATION IS THE KEY'S OWN AFFAIR; nought means somebody else will say when. See
		// liftDueKeys for why this matters and what its absence was mistaken for.
		v->endsIn = (duration > 0.f) ? (int64_t) ((double) duration * (double) sampleRate) : 0;
		v->undamped = r->undamped;
		v->key = key;
		v->vel = lv;
		// WHAT THE MARKING IS WORTH, aimed at rather than arrived at: see aimedAt.
		//
		// THE CHORD'S SHARE AND THE SOFT PEDAL ARE GAINS HERE and not smaller levels, which is
		// the whole of why the share is carried this far down. A level does not scale amplitude,
		// it names a loudness — halve it and the note does not halve, it drops to whatever the
		// table says that marking is worth. The first version of the share divided the level,
		// and a six-note chord at forte came out quieter than a single forte note, which is the
		// opposite of the fault it was written to cure.
		const float fresh = aimedAt(level, r) * (1.f - 0.2f * soft) * share;
		// WHAT WAS STILL RINGING, ADDED TO THE NEW STRIKE, up to a ceiling: a string driven over
		// and over settles at an amplitude a little above one strike's, it does not keep growing.
		// Nought for a key that was not already sounding, which is almost every note.
		v->amp = std::min(fresh * BLOOM_MOST, fresh + carried);
		aim(*v, sampleRate);
	}

	/** The damper falls: the note starts to die, and the strings' release resonance sounds at a
	level that follows how loud the note still is. */
	void damp(PianoVoice& v, float sampleRate, float sustain) {
		if (v.releasing || v.undamped)
			return;
		// CLOCKWISE IS MORE DAMPING: the knob up means the damper falls FASTER. See configParam.
		const float k = params[P_DAMPING].getValue();
		float seconds = 0.8f * std::pow(1.f / 40.f, k);
		// Half-pedalling: a pedal part way down lets the note ring longer, fully down holds it.
		seconds /= std::max(0.05f, 1.f - sustain);
		v.releasing = true;
		v.decay = std::exp(-6.9f / (seconds * sampleRate));

		const PianoBank* bank = gLib.bank.load();
		if (!bank)
			return;
		// A RELEASE FOLLOWS THE NOTE IT ENDS: loud while the string is still ringing hard, faint
		// once it has died away. The note's loudness is already tracked for choosing which note
		// to take, so this costs nothing further.
		const float remaining = (v.peak > 0.f) ? math::clamp(v.follow / v.peak, 0.f, 1.f) : 0.f;
		const float gain = params[P_RELEASE].getValue() * v.amp * remaining;
		// A DAMPER STOPS THE RESONANCE AS WELL AS THE NOTE, which is why this one-shot is given
		// the damper's own time and not left to play out.
		//
		// WHAT IT DID BEFORE, and what that was mistaken for. These recordings are two and eight
		// tenths of a second long, and one was started at full level every time a key came up and
		// allowed to run to its end. In a fugue, with a hundred and eighteen notes released
		// inside twenty seconds, something like twenty of them overlap at every moment — a
		// continuous wash that no setting of the DAMPING knob could touch, because the knob sets
		// the string voices' envelope and never reached these. So the knob appeared to do nothing
		// at either extreme while the strings underneath it were in fact damping correctly.
		//
		// Three times the damper's own time: a damper that stops a string in a fiftieth of a
		// second leaves a thud rather than a ring, which is what a released key sounds like, and
		// one let down slowly leaves the resonance that a slow damper really does leave.
		startOneShot(bank->find(PART_RESONANCE, v.key, v.vel), gain, PART_RESONANCE, v.key,
			sampleRate, 3.f * seconds);
	}

	/** PEDALLING THE PIECE ITSELF, where the score does not.

	MOST SCORES ARE UNDER-MARKED. The Chopin Ballade marks 189 of its 262 bars and leaves the rest
	bare, and played literally those bars are dry: every note is stopped a fraction of a second
	after its written end, which is not how anybody plays a piano.

	WHAT DECIDES WHERE THE FOOT GOES. Harmony, measured against the pedal marks of the Ballade and
	the Nocturne, does not. A bass-note change finds 98 per cent of the real lifts but fires seven
	times too often, because the left hand arpeggiates inside one harmony; chroma novelty, which is
	the standard harmony-change detector, found 57 per cent of them with a quarter of its guesses
	right, and was beaten by changing the pedal on every bar line and nothing else. The metre does
	the work: 97 per cent of the real lifts land exactly on a beat, and they come at one every two
	seconds in both pieces whatever the harmony is doing.

	SO THE KNOB IS AN AXIS OF HOW OFTEN, from driest to wettest, and the ear picks the point. What
	each position caught of the real lifts, over 497 seconds of the Ballade and 192 of the
	Nocturne:

	    every beat        94%, 90%     safest; four times as many lifts as a pianist makes
	    every half bar    89%, 80%     closest to the real pedalling
	    every bar         54%, 49%     holds through half the real changes, which smears

	A missed lift is the bad error, since it holds the pedal through a harmony change. Lifting too
	often only sounds drier than intended, so the axis is safe at one end and muddy at the other.

	NO LOOKAHEAD IS NEEDED. A pianist's foot is reactive — it lifts as the new harmony sounds and
	takes the pedal again straight after — so a rule that only ever acts at the moment a beat
	arrives can reach the same places as the marks. The beat comes off the cable as state, so this
	works for anything that publishes one and not only for a loaded file. */
	float pedalFor(float sampleTime) {
		const int how = (int) std::lround(params[P_PEDAL].getValue());
		if (how <= 0)
			return 0.f;
		if (how >= 5)
			return 1.f;
		Harmony h;
		// NOTHING PUBLISHING A BEAT means there is no metre to pedal against. Holding the pedal
		// down would be a guess at what the music is doing; doing nothing is not.
		if (!reader.harmony(h) || h.barBeats < 1)
			return 0.f;
		if (h.holding) {
			autoLift = 0.f;
			return 0.f;
		}
		// WHICH UNIT THIS POSITION LIFTS ON, in beats.
		const float unit = (how == 1) ? 1.f
			: (how == 2) ? std::max(1.f, (float) h.barBeats / 2.f)
			: (how == 3) ? (float) h.barBeats
			: 2.f * (float) h.barBeats;
		// A LIFT WHEN THE MUSIC CROSSES ONE. The cable counts the place in the bar in QUARTER
		// notes, because a count that changed its unit with the signature would jump; the time
		// signature counts beats. In six-eight those are not the same number, so the position is
		// put into beats before it is measured against a unit that is also in beats.
		const double beatsIn = (double) h.beatInBar * (double) h.barUnit / 4.0;
		const double place = (double) h.bar * (double) h.barBeats + beatsIn;
		const double step = std::floor(place / (double) unit);
		if (steppedWas != step) {
			if (steppedWas != -1e18)
				autoLift = LIFT_SECONDS;
			steppedWas = step;
		}
		if (autoLift > 0.f) {
			autoLift -= sampleTime;
			return 0.f;
		}
		return 1.f;
	}

	/** Every key up at once, as though each had been released: the pedal still decides how long
	the strings ring. */
	void allKeysUp(float sampleRate) {
		const float sustain = std::max(0.f, params[P_SUSTAIN].getValue());
		for (PianoVoice& v : voices) {
			if (!v.active || v.part != PART_STRINGS || !v.keyDown)
				continue;
			v.keyDown = false;
			if (sustain < 0.95f)
				damp(v, sampleRate, sustain);
		}
	}

	/** TOLD TO LET A KEY UP, which takes precedence over whatever duration it was given: a source
	that sends both means the second one. */
	void noteOff(int64_t source, float sampleRate, float sustain) {
		for (PianoVoice& v : voices) {
			if (!v.active || v.part != PART_STRINGS || v.source != source || !v.keyDown)
				continue;
			liftKey(v, sampleRate, sustain, true);
		}
	}

	void update(const Event& e, float sampleRate) {
		for (PianoVoice& v : voices) {
			if (!v.active || v.part != PART_STRINGS || v.source != e.handle)
				continue;
			if (e.lane == LANE_BEND) {
				v.bendV = e.value;
				aim(v, sampleRate);
			}
			// TIMBRE IS THIS NOTE'S BRIGHTNESS. Pressure is left alone: nothing on a piano
			// answers it once a key is down.
			else if (e.lane == LANE_TIMBRE)
				v.timbre = math::clamp(e.value, 0.f, 1.f);
		}
	}

	/** A NOTE WAITING ITS TURN IN A CHORD. See SPREAD_PER_LEVEL. */
	struct Waiting {
		int64_t handle = 0;
		float pitch = 0.f, level = 0.f, duration = 0.f, share = 1.f;
		int64_t in = 0;         /**< Samples still to wait. */
		bool active = false;
	};
	Waiting waiting[MAX_CHORD * 2];

	/** Puts a note by for a moment. False where there is no room, and then it speaks at once,
	since a note played flat with the rest of the chord is better than one not played. */
	bool holdBack(int64_t handle, float pitch, float level, float duration, float late, float sr,
			float share) {
		for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); i++) {
			if (waiting[i].active)
				continue;
			waiting[i].handle = handle;
			waiting[i].pitch = pitch;
			waiting[i].level = level;
			waiting[i].duration = duration;
			waiting[i].share = share;
			waiting[i].in = (int64_t) std::lround((double) late * (double) sr);
			waiting[i].active = waiting[i].in > 0;
			return waiting[i].active;
		}
		return false;
	}

	/** Every sample: the ones whose moment has come. */
	void speakWaiting(float sr, float soft) {
		for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); i++) {
			if (!waiting[i].active)
				continue;
			if (--waiting[i].in > 0)
				continue;
			waiting[i].active = false;
			noteOn(waiting[i].handle, waiting[i].pitch, waiting[i].level, sr, soft,
				waiting[i].duration, waiting[i].share);
		}
	}

	void silenceAll() {
		for (PianoVoice& v : voices)
			v.active = false;
		for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); i++)
			waiting[i].active = false;
	}

	bool attachedWas = false;

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		// THE CABLE GOING IS EVERY KEY COMING UP. A note ends when its note-off arrives, and a
		// cable pulled out — or muted, which takes the cable out — never sends one, so whatever
		// was sounding rang on for ever with its key still down. Unplugging a piano is not a way
		// to hold a chord.
		const bool attached = reader.attached();
		if (!attached && attachedWas) {
			allKeysUp(args.sampleRate);
			// AND THE NOTES STILL WAITING THEIR TURN IN A CHORD GO WITH IT. Unplugging a piano
			// should not be followed a moment later by two more notes.
			for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); i++)
				waiting[i].active = false;
		}
		attachedWas = attached;

		// A NEW SET OF SAMPLES — a different number of layers, or a fresh download — invalidates
		// every voice, which point into the old one. They stop rather than read freed memory.
		const PianoBank* bank = gLib.bank.load();
		if (bank != bankSeen) {
			silenceAll();
			bankSeen = bank;
		}

		// The pedals: whichever is further down, the button or the cable.
		float busSustain = 0.f, busSoft = 0.f;
		reader.pedals(busSustain, busSoft);
		// THE KNOB STAYS OUT OF A PEDALLED SCORE'S WAY. While the cable's pedal is moving, the
		// score is pedalling for itself and knows better than any rule here; once it has left the
		// pedal up for a couple of bars it is plainly not pedalling this passage, and the knob
		// takes over. The Ballade needs exactly this: Chopin's own marks from bar 32, and
		// pedalling of our own through bars 9 to 25, with nothing to set either way.
		if (busSustain > 0.5f)
			cableQuiet = 0.f;
		else
			cableQuiet = std::min(cableQuiet + args.sampleTime, 100.f);
		// TWO BARS OF SILENCE FROM THE CABLE, measured in time rather than in bars, since a bar
		// is only known when something publishes one. Four seconds is two bars of most things.
		const float selfPedal = (cableQuiet >= 4.f) ? pedalFor(args.sampleTime) : 0.f;
		const float sustain = std::max(std::max(busSustain, selfPedal),
			params[P_SUSTAIN].getValue());
		const float soft = std::max(busSoft, params[P_SOFT].getValue());
		const float sr = args.sampleRate;

		if (bank) {
			Event e;
			// EVERYTHING STRUCK AT ONCE IS TAKEN AT ONCE, so a chord can be balanced as a chord
			// rather than as a handful of notes that happen to coincide. See voicing.
			Event struck[MAX_CHORD];
			int chord = 0;
			while (reader.next(e)) {
				if (e.kind == Event::ON) {
					if (chord < MAX_CHORD)
						struck[chord++] = e;
					else {
						// More notes at one instant than any hand could play: the rest are
						// started unbalanced rather than dropped. Shared out by density all
						// the same, since there are certainly at least this many sounding, and
						// a texture this thick is exactly where the sum runs away.
						float level = e.level, duration = e.duration;
						shapeByMark(e.technique, level, duration);
						noteOn(e.handle, e.pitch, level, sr, soft, duration,
							density(MAX_CHORD));
					}
				}
				else if (e.kind == Event::OFF)
					noteOff(e.handle, sr, sustain);
				else
					update(e, sr);
			}
			if (chord > 0) {
				// IN PITCH ORDER, since which note is the top one is the whole question. A short
				// insertion sort: there are never more than a handful.
				for (int a = 1; a < chord; a++) {
					Event hold = struck[a];
					int b = a - 1;
					for (; b >= 0 && struck[b].pitch > hold.pitch; b--)
						struck[b + 1] = struck[b];
					struck[b + 1] = hold;
				}
				// WHAT IS SOUNDING NOW RAISES THE MELODY, before anything is weighed against
				// it: a melody note and the chord under it arrive together, and the note is the
				// melody rather than being measured against where the melody used to be.
				for (int a = 0; a < chord; a++)
					melodyTop = std::fmax(melodyTop, noteOf(struck[a].pitch));

				float levels[MAX_CHORD], lengths[MAX_CHORD], loudest = 0.f;
				for (int a = 0; a < chord; a++) {
					// WHAT THE SCORE WROTE OVER THE NOTE, turned into a hammer speed and a
					// finger's length: see shapeByMark.
					levels[a] = struck[a].level;
					lengths[a] = struck[a].duration;
					shapeByMark(struck[a].technique, levels[a], lengths[a]);
					levels[a] *= voicing(a, chord) * underMelody(noteOf(struck[a].pitch));
					loudest = std::fmax(loudest, levels[a]);
				}
				// HOW THE CHORD IS BALANCED WITHIN ITSELF IS SETTLED BY NOW. How loud the whole
				// of it is rides along to noteOn as a gain and does not touch these levels,
				// which is what keeps it out of two things that are worked out from them: the
				// velocity layer each note is played from, and the order the notes speak in,
				// which comes from the gaps between their levels. See density.
				const float share = density(chord);
				// AND WHERE IN THE BAR THE CHORD FALLS, which is one answer for all of it: the
				// notes were struck together, so they are in the same place in the metre.
				Harmony now;
				const bool timed = reader.harmony(now);
				// A ROLLED CHORD IS NOT A CHORD THAT MISSED. The wavy stroke down the front of
				// one says to play it from the bottom up — or from the top down where the arrow
				// points that way — and it is a gesture, not an untidiness, so it overrides the
				// spread entirely. The melody lead would otherwise fight it: that puts the top
				// note first and a roll puts it last.
				//
				// BY THIS MODULE'S OWN PITCH ORDER AND NOT BY strumIndex, which is counted
				// within one voice of one staff. A pianist rolls a chord through both hands and
				// the two staves arrive as separate beats, each numbering its own notes from
				// nought, so the published index would start again halfway up the chord. What
				// is wanted is the order across everything struck at this instant, which is
				// exactly the order these have already been sorted into.
				int rolling = 0;
				for (int a = 0; a < chord; a++)
					if (struck[a].strum != 0)
						rolling = struck[a].strum;

				for (int a = 0; a < chord; a++) {
					// WHEN EACH NOTE OF THE CHORD SPEAKS: see the constants above.
					float late;
					if (rolling != 0 && chord > 1) {
						const int step = (rolling > 0) ? a : (chord - 1 - a);
						late = (float) step * ROLL_PER_NOTE;
					}
					else {
						late = (loudest - levels[a]) * SPREAD_PER_LEVEL;
						if (chord >= 4 && a < chord - 1)
							late += (float) a * SPREAD_INNER;
						late = math::clamp(late, 0.f, SPREAD_MOST);
					}
					// A NOTE THAT SPEAKS LATE STILL STOPS WHEN IT WAS WRITTEN TO, so the delay
					// comes off its length rather than pushing its end along with it.
					float held = lengths[a];
					if (held > 0.f)
						held = std::fmax(0.02f, held - late);
					// THE LEAN OF THE BAR, as a gain beside the chord's share and a little more
					// time on the note. See metre.
					float weight = share;
					if (timed)
						metre(now, struck[a].technique, weight, held);
					if (late <= 0.f || chord < 2) {
						noteOn(struck[a].handle, struck[a].pitch, levels[a], sr, soft, held,
							weight);
						continue;
					}
					if (!holdBack(struck[a].handle, struck[a].pitch, levels[a], held, late, sr,
							weight))
						noteOn(struck[a].handle, struck[a].pitch, levels[a], sr, soft, held,
							weight);
				}
			}

			// THE PEDAL MOVING. Down is heard as the pedal's own noise; up lets every damper fall
			// on the notes whose keys are already up.
			const bool wasDown = sustainWas >= 0.5f, isDown = sustain >= 0.5f;
			if (isDown != wasDown) {
				startOneShot(bank->pedal(isDown ? PART_PEDAL_DOWN : PART_PEDAL_UP, random::uniform()),
					pedalNoise, isDown ? PART_PEDAL_DOWN : PART_PEDAL_UP, 60, sr);
				if (!isDown)
					for (PianoVoice& v : voices)
						if (v.active && v.part == PART_STRINGS && !v.keyDown)
							damp(v, sr, sustain);
			}
		}
		else {
			Event e;
			while (reader.next(e)) {}
		}
		// THE PEDALS AS THE PIANO HAS THEM, which is the button, the cable in, and whatever the
		// PEDALLING knob is doing — so a synthesiser on the far end is pedalled the same way this
		// piano is. Published every sample, as state, for the reason NoteBus.hpp gives.
		if (slot >= 0) {
			busPublishPedals(slot, sustain, soft);
			outputs[O_MPX].setVoltage(busFlashVolts(slot));
		}

		// THE TOP OF THE TEXTURE, FORGOTTEN AS THE MUSIC MOVES ON: see MELODY_FALL_PER_BEAT.
		// Asked of the cable every so many samples rather than every one, since the harmony is a
		// structure read under a seqlock and the fall over thirty-two samples is a thousandth of
		// a semitone either way.
		if (--beatLook <= 0) {
			beatLook = 32;
			Harmony h;
			if (reader.harmony(h)) {
				// A SEEK, A LOOP OR A FRESH START moves the position by more than any music
				// does in thirty-two samples; nothing is forgotten for it.
				const double moved = h.beat - beatWas;
				beatWas = h.beat;
				if (haveBeat && moved > 0.0 && moved < 1.0)
					melodyTop -= MELODY_FALL_PER_BEAT * (float) moved;
				haveBeat = true;
			}
			else {
				haveBeat = false;
				melodyTop -= MELODY_FALL_CLOCK * args.sampleTime * 32.f;
			}
		}

		// The notes of a chord that were put by for a moment, and then the keys that come up by
		// themselves — both every sample, since a damper landing a buffer late is audibly late on
		// a sixteenth and so is a melody note.
		if (bank)
			speakWaiting(sr, soft);
		// The keys that come up by themselves, every sample: a damper landing a buffer late is
		// audibly late on a sixteenth.
		liftDueKeys(sr, sustain);

		sustainWas = sustain;

		// ---- the mix ----
		// The three mechanical levels are applied as each sound starts, from its knob, so a knob
		// turned while a sound is ringing affects the next one rather than jumping this one.
		const float bright = params[P_BRIGHT].getValue();
		const float followK = 1.f - std::exp(-1.f / (0.02f * sr));
		float outL = 0.f, outR = 0.f;
		for (PianoVoice& v : voices) {
			if (!v.active)
				continue;
			const PianoSample& s = *v.sample;
			const int i = (int) v.pos;
			if (i + 2 >= s.frames || v.env < 0.0001f) {
				v.active = false;
				continue;
			}
			// FOUR-POINT HERMITE between the stored samples: linear interpolation dulls the top
			// of a piano audibly when a sample is pitched a long way.
			const float t = (float) (v.pos - i);
			const int16_t* d = s.data.data();
			const int i0 = std::max(0, i - 1);
			float lr[2];
			for (int c = 0; c < 2; c++) {
				const float y0 = d[i0 * 2 + c], y1 = d[i * 2 + c];
				const float y2 = d[(i + 1) * 2 + c], y3 = d[(i + 2) * 2 + c];
				const float c1 = 0.5f * (y2 - y0);
				const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
				const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
				lr[c] = (((c3 * t + c2) * t + c1) * t + y1) / 32768.f;
			}
			v.pos += v.step;

			float g = v.amp * v.env;
			if (v.releasing)
				v.env *= v.decay;
			float L = lr[0] * g, R = lr[1] * g;

			// BRIGHTNESS: the knob, this note's own timbre, and the soft pedal darkening it.
			if (v.part == PART_STRINGS || v.part == PART_RESONANCE) {
				const float b = math::clamp(bright + (v.timbre - 0.5f) * 0.8f - 0.25f * soft, 0.f, 1.f);
				if (b < 0.995f) {
					const float fc = 300.f * std::pow(20000.f / 300.f, b);
					const float a = 1.f - std::exp(-2.f * (float) M_PI * fc / sr);
					v.lpL += (L - v.lpL) * a;
					v.lpR += (R - v.lpR) * a;
					L = v.lpL;
					R = v.lpR;
				}
			}

			const float level = std::fabs(L) + std::fabs(R);
			v.follow += (level - v.follow) * followK;
			v.peak = std::max(v.peak, v.follow);
			// HOW LONG THIS ONE HAS BEEN GOING, which is what keeps a note that has not been
			// heard yet from being the one taken for the next. See PROTECT_SECONDS.
			v.struckAgo++;
			outL += L;
			outR += R;
		}

		// MPX_FULL_VOLTS at the knob's default: see NoteBus.hpp.
		const float volume = params[P_VOLUME].getValue() * MPX_FULL_VOLTS;
		float left = outL * volume, right = outR * volume;

		// THE LAST FEW DECIBELS, CAUGHT RATHER THAN SQUARED OFF.
		//
		// WHAT WAS HERE WAS A HARD CLAMP at twelve volts, which is not a limiter: it flattens the
		// top of the waveform and what that sounds like is distortion, arriving exactly on the
		// loudest chord of the piece and on nothing else.
		//
		// IT IS A SAFETY NET AND NOT A TONE CONTROL. Measured over the scores in use, the loudest
		// attack reaches about six volts, so this never acts on an ordinary chord. What it is for
		// is the pile-up: the pedal holds the dampers off, notes keep their voices, and twenty of
		// them ringing together go somewhere no single marking asked for.
		//
		// BOTH CHANNELS BY THE SAME AMOUNT, so a loud note on one side cannot pull the image
		// across. Fast to take hold and slow to let go, which is what makes it inaudible: coming
		// off the gain quickly is what a listener hears as pumping.
		const float peak = std::fmax(std::fabs(left), std::fabs(right));
		const float want = (peak > LIMIT_VOLTS) ? LIMIT_VOLTS / peak : 1.f;
		const float towards = (want < limitGain)
			? (1.f - std::exp(-1.f / (LIMIT_ATTACK * sr)))
			: (1.f - std::exp(-1.f / (LIMIT_RELEASE * sr)));
		limitGain += (want - limitGain) * towards;
		left *= limitGain;
		right *= limitGain;

		// AND THE CLAMP STAYS, behind it. Nothing with an attack time is instantaneous, so the
		// first millisecond of a transient still gets through; this is what it gets through to.
		outputs[O_L].setVoltage(math::clamp(left, -12.f, 12.f));
		outputs[O_R].setVoltage(math::clamp(right, -12.f, 12.f));
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "curve", json_integer(curve));
		json_object_set_new(rootJ, "reference", json_real(reference));
		json_object_set_new(rootJ, "stretch", json_boolean(stretch));
		json_object_set_new(rootJ, "polyphony", json_integer(polyphony));
		json_object_set_new(rootJ, "pedalNoise", json_real((double) pedalNoise));
		json_object_set_new(rootJ, "hammerNoise", json_real((double) hammerNoise));
		json_object_set_new(rootJ, "melodyBalance", json_real((double) melodyBalance));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "curve")) curve = math::clamp((int) json_integer_value(j), 0, 2);
		if (json_t* j = json_object_get(rootJ, "reference")) reference = json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "stretch")) stretch = json_is_true(j);
		if (json_t* j = json_object_get(rootJ, "polyphony"))
			polyphony = math::clamp((int) json_integer_value(j), 8, MAX_VOICES - NOISE_VOICES);
		if (json_t* j = json_object_get(rootJ, "pedalNoise"))
			pedalNoise = math::clamp((float) json_number_value(j), 0.f, 2.f);
		if (json_t* j = json_object_get(rootJ, "hammerNoise"))
			hammerNoise = math::clamp((float) json_number_value(j), 0.f, 2.f);
		if (json_t* j = json_object_get(rootJ, "melodyBalance"))
			melodyBalance = math::clamp((float) json_number_value(j), 0.1f, 1.f);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

/** THE SAMPLES, IN ONE PLACE ON THE PANEL. Until they are installed this is the button that fetches
them, labelled DOWNLOAD LIBRARY; while they come in it is a progress bar; once they are loaded it is
the credit to the man who recorded them. One strip rather than a button and a caption, because the
button has nothing to do once the download is done and the credit has nothing to say before it. */
struct PianoStatus : widget::OpaqueWidget {
	bool pressable() const {
		const int st = gLib.state.load();
		return !gLib.busy.load() && (st == Library::ABSENT || st == Library::FAILED);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && pressable()) {
			startDownload();
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	/** Text set as large as will fit, from `largest` down, wrapped to the strip's width. */
	void fitted(NVGcontext* vg, const std::string& text, float largest, NVGcolor color) {
		const float w = box.size.x - 6.f;
		float size = largest;
		float bounds[4];
		for (; size > 5.f; size -= 0.5f) {
			nvgFontSize(vg, size);
			nvgTextBoxBounds(vg, 0.f, 0.f, w, text.c_str(), NULL, bounds);
			if (bounds[3] - bounds[1] <= box.size.y - 4.f)
				break;
		}
		const float h = bounds[3] - bounds[1];
		nvgFillColor(vg, color);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		// Twice, as the panel's own lettering is drawn: see Panel::crisp.
		for (int i = 0; i < 2; i++)
			nvgTextBox(vg, 3.f, std::round((box.size.y - h) / 2.f), w, text.c_str(), NULL);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgTextLineHeight(vg, 1.1f);
		const int st = gLib.state.load();

		if (pressable()) {
			// A BUTTON, drawn as one: a raised face with a rim, so it reads as something to press
			// rather than as a message.
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
			nvgFillColor(vg, nvgRGB(0x2a, 0x2f, 0x36));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGB(0x7d, 0xff, 0xaa));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			const std::string text = (st == Library::FAILED)
				? gLib.getMessage() + "\nDOWNLOAD LIBRARY"
				: std::string("DOWNLOAD LIBRARY\n") + SAMPLES_SIZE;
			fitted(vg, text, 9.f, nvgRGB(0xff, 0xff, 0xff));
			return;
		}

		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.f, 0.f, box.size.x, box.size.y, 2.f);
		nvgFillColor(vg, nvgRGB(0x12, 0x14, 0x18));
		nvgFill(vg);

		float progress = -1.f;
		std::string text;
		if (st == Library::DOWNLOADING) {
			progress = gLib.downloadProgress;
			text = string::f("Downloading %d%%", (int) std::lround(progress * 100.f));
		}
		else if (st == Library::UNPACKING)
			text = "Unpacking";
		else if (st == Library::LOADING) {
			progress = gLib.progress.load();
			text = string::f("Loading %d%%", (int) std::lround(progress * 100.f));
		}
		else
			// THE CREDIT, as the licence asks, sized to the strip.
			text = "Salamander Grand Piano by Alexander Holm, CC-BY 3.0";

		if (progress >= 0.f) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 0.f, 0.f, box.size.x * math::clamp(progress, 0.f, 1.f), box.size.y, 2.f);
			nvgFillColor(vg, nvgRGBA(0x3d, 0xe0, 0x7a, 0x60));
			nvgFill(vg);
		}
		fitted(vg, text, 8.f, nvgRGB(0xe6, 0xe6, 0xe6));
	}
};


/** THE PANEL AS IT WAS EDITED BY HAND, evened up: two columns set symmetrically about the middle,
the pedal latches over the output jacks, and the status strip across the width where the Download
button was. Eight HP. */
static const float PANEL_W = 8.f * RACK_GRID_WIDTH * 25.4f / 75.f;   // 40.64 mm
static const float COL_A = 12.1f;
static const float COL_B = 28.5f;
static const float PAIR_L = 24.f;       // Sustain over the left output
static const float PAIR_R = 33.f;       // Soft over the right
/** HOW FAR THE OUTER TWO JACKS SIT FROM THE EDGE OF THE PANEL. Four across eight units is close
work: a port is 8.7mm wide, so this leaves about a millimetre and a half of panel outside each of
the outer two. */
static const float JACK_EDGE = 6.f;

static Layout pianoLayout() {
	Layout L;
	L.hp = 8.f;
	L.title = "mpxPiano";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const char* owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const char* key, float x, float y, int id, const char* name, float diameter) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
		i.diameter = diameter;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.f, name, Panel::CENTRE, true, 0.f, key);
	};
	auto press = [&](const char* key, float x, float y, int id, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "button";
		i.diameter = 6.6f;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.f, name, Panel::CENTRE, true, 8.f, key);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, int id, const char* name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = 119.5f; i.ring = ring;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, 111.5f, name, Panel::CENTRE, true, 0.f, key);
	};

	// THE SOUND: how loud, how much the playing matters, how bright, and how long a note lasts.
	knob("p.volume", COL_A, 20.5f, PianoModule::P_VOLUME, "VOLUME", 11.f);
	knob("p.dynamics", COL_B, 20.5f, PianoModule::P_DYNAMICS, "DYNAMICS", 0.f);
	knob("p.bright", COL_A, 39.f, PianoModule::P_BRIGHT, "BRIGHT", 0.f);
	knob("p.damping", COL_B, 39.f, PianoModule::P_DAMPING, "DAMPING", 0.f);

	// THE MECHANISM, each a level of its own, so the piano can be made realistic or clean.
	label("h.mechanism", PANEL_W / 2.f, 54.5f, "MECHANICAL SOUNDS", Panel::CENTRE, true);
	knob("p.release", COL_A, 64.5f, PianoModule::P_RELEASE, "RELEASE", 0.f);
	knob("p.emphasis", COL_B, 64.5f, PianoModule::P_EMPHASIS, "EMPHASIS", 0.f);
	knob("p.pedal", COL_A, 82.f, PianoModule::P_PEDAL, "PEDALLING", 0.f);

	// The pedals by hand, beside the pedal noise they set off.
	press("p.sustain", PAIR_L, 82.f, PianoModule::P_SUSTAIN, "SUST");
	press("p.soft", PAIR_R, 82.f, PianoModule::P_SOFT, "SOFT");

	// THE SAMPLES: a Download button until they are installed, then the credit. See PianoStatus.
	Item status;
	status.key = "d.status"; status.kind = Item::DISPLAY; status.x = 3.f; status.y = 94.f;
	status.w = PANEL_W - 6.f; status.h = 12.f;
	L.items.push_back(status);

	// One cable in, a stereo pair out, the pair under the two pedals.
	// FOUR ACROSS THE FOOT OF THE PANEL, evenly spaced: the notes in, the stereo pair, and the
	// notes out again as they were played. See PianoModule::slot.
	for (int i = 0; i < 4; i++) {
		const float x = JACK_EDGE + (PANEL_W - 2.f * JACK_EDGE) * (float) i / 3.f;
		switch (i) {
			case 0: jack("in.mpx", Item::PORT_IN, x, PianoModule::I_MPX, "mpx IN", NOTE_CABLE); break;
			case 1: jack("out.l", Item::PORT_OUT, x, PianoModule::O_L, "L", SIG_AUDIO); break;
			case 2: jack("out.r", Item::PORT_OUT, x, PianoModule::O_R, "R", SIG_AUDIO); break;
			default: jack("out.mpx", Item::PORT_OUT, x, PianoModule::O_MPX, "mpx OUT", NOTE_CABLE);
		}
	}

	L.bindOffsets();
	return L;
}


struct PianoWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	bool counted = false;

	PianoWidget(PianoModule* module) {
		setModule(module);
		layout = pianoLayout();
		layoutApplyUser("mpxPiano", layout);
		panel = new Panel;
		addChild(panel);
		PianoStatus* status = new PianoStatus;
		layoutPlaceDisplay(this, layout, "d.status", status);
		layoutBuild(this, panel, layout);
		// A PREVIEW IN THE MODULE BROWSER HAS NO MODULE, and must not load half a gigabyte.
		if (module) {
			counted = true;
			libraryAcquire();
		}
	}

	~PianoWidget() {
		if (counted)
			libraryRelease();
	}

	void step() override {
		ModuleWidget::step();
		libraryStep();
		PianoModule* m = dynamic_cast<PianoModule*>(module);
		if (!m)
			return;

		PortWidget* port = getInput(PianoModule::I_MPX);
		int slots[MAX_UPSTREAM];
		uint32_t gens[MAX_UPSTREAM];
		int n = 0;
		if (port) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (!cable || n >= MAX_UPSTREAM)
					continue;
				uint32_t g = 0;
				const int slot = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (slot >= 0) {
					slots[n] = slot;
					gens[n] = g;
					n++;
					cw->color = NOTE_CABLE;
				}
			}
		}
		m->link(slots, gens, n);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPiano");
		PianoModule* m = dynamic_cast<PianoModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createSubmenuItem("Velocity curve", "", [=](ui::Menu* sub) {
			const char* names[3] = {"Soft", "Normal", "Hard"};
			for (int i = 0; i < 3; i++)
				sub->addChild(createCheckMenuItem(names[i], "", [=]() { return m->curve == i; },
					[=]() { m->curve = i; }));
		}));
		menu->addChild(createSubmenuItem("Reference pitch", string::f("%.0f Hz", m->reference),
			[=](ui::Menu* sub) {
				const float refs[] = {415.f, 432.f, 435.f, 440.f, 442.f, 444.f};
				for (float r : refs)
					sub->addChild(createCheckMenuItem(string::f("%.0f Hz", r), "",
						[=]() { return std::fabs(m->reference - r) < 0.5f; }, [=]() { m->reference = r; }));
			}));
		menu->addChild(createBoolPtrMenuItem("Stretch tuning", "", &m->stretch));
		// OFF IS A REAL SETTING, not the absence of one: a fugue's voices are equal and holding
		// the lower ones back is wrong, however right it is for a nocturne.
		menu->addChild(createSubmenuItem("Melody balance",
			[&]() {
				if (m->melodyBalance >= 0.999f) return "Off";
				if (m->melodyBalance >= 0.5f) return "Light";
				if (m->melodyBalance >= 0.3f) return "Normal";
				return "Strong";
			}(), [=](ui::Menu* sub) {
				static const struct { const char* name; float level; } HOW[] = {
					{"Off — every voice equal, for counterpoint", 1.f},
					{"Light", 0.6f},
					{"Normal", 0.35f},
					{"Strong", 0.2f},
				};
				for (size_t i = 0; i < sizeof(HOW) / sizeof(HOW[0]); i++) {
					const float level = HOW[i].level;
					sub->addChild(createCheckMenuItem(HOW[i].name, "",
						[=]() { return std::fabs(m->melodyBalance - level) < 0.01f; },
						[=]() { m->melodyBalance = level; }));
				}
			}));
		menu->addChild(createSubmenuItem("Pedal mechanism level",
			string::f("%.0f%%", m->pedalNoise * 100.f), [=](ui::Menu* sub) {
				const float levels[] = {0.f, 0.5f, 1.f, 1.5f, 2.f};
				for (float v : levels)
					sub->addChild(createCheckMenuItem(string::f("%.0f%%", v * 100.f), "",
						[=]() { return std::fabs(m->pedalNoise - v) < 0.01f; },
						[=]() { m->pedalNoise = v; }));
			}));
		// THE KEY COMING UP, which had the knob the emphasis now has. A mechanical noise is a
		// hundredth of what the metre does to a performance, and nobody sets it twice.
		menu->addChild(createSubmenuItem("Key noise",
			string::f("%.0f%%", m->hammerNoise * 100.f), [=](ui::Menu* sub) {
				const float levels[] = {0.f, 0.5f, 1.f, 1.5f, 2.f};
				for (float v : levels)
					sub->addChild(createCheckMenuItem(string::f("%.0f%%", v * 100.f), "",
						[=]() { return std::fabs(m->hammerNoise - v) < 0.01f; },
						[=]() { m->hammerNoise = v; }));
			}));
		menu->addChild(createSubmenuItem("Polyphony", string::f("%d", m->polyphony), [=](ui::Menu* sub) {
			const int counts[] = {16, 32, 48, 64, 72};
			for (int c : counts)
				sub->addChild(createCheckMenuItem(string::f("%d notes", c), "",
					[=]() { return m->polyphony == c; }, [=]() { m->polyphony = c; }));
		}));
		menu->addChild(createSubmenuItem("Velocity layers loaded", string::f("%d", gLib.layers),
			[=](ui::Menu* sub) {
				const int counts[] = {4, 8, 16};
				for (int c : counts)
					sub->addChild(createCheckMenuItem(string::f("%d layers", c), "",
						[=]() { return gLib.layers == c; },
						[=]() {
							if (gLib.layers == c)
								return;
							gLib.layers = c;
							if (samplesInstalled())
								startLoad();
						}));
			}));
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Samples: Salamander Grand Piano, Alexander Holm, CC-BY 3.0"));
		menu->addChild(createMenuItem("Show the sample folder", "", []() {
			system::openDirectory(pianoDir());
		}));
		menu->addChild(createMenuItem("Download the samples again", "", []() {
			if (!gLib.busy.load())
				startDownload();
		}));
		menu->addChild(createMenuItem("Remove the samples", "", []() {
			if (gLib.busy.load())
				return;
			if (!osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK_CANCEL,
					"Remove the piano samples from this computer? They can be downloaded again."))
				return;
			if (PianoBank* old = gLib.bank.exchange(NULL))
				gLib.graveyard.push_back({old, system::getTime()});
			system::removeRecursively(pianoDir());
			gLib.state = Library::ABSENT;
		}));
	}
};


} // namespace px


Model* modelMpxPiano = createModel<px::PianoModule, px::PianoWidget>("mpxPiano");
