/** mpxPiano — the Salamander Grand Piano, played from an MPX cable. See docs/piano.md.

ONLY AN MPX CABLE IN. Ordinary Rack signals reach it through toMPX, which already turns pitch,
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
	bool releasing = false;
	bool undamped = false;
	float timbre = 0.5f;
	float follow = 0.f, peak = 0.f;
	float lpL = 0.f, lpR = 0.f;
	int key = 60, vel = 64;
};


struct PianoModule : Module, NoteSink {
	enum ParamId {
		P_VOLUME,
		P_RELEASE,
		P_HAMMER,
		P_PEDAL,
		P_BRIGHT,
		P_DYNAMICS,
		P_DAMPING,
		P_SUSTAIN,
		P_SOFT,
		NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_L, O_R, NUM_OUTPUTS };
	enum LightId { NUM_LIGHTS };

	BusReader reader;
	std::atomic<bool> relink{false};
	std::atomic<int> wantCount{0};
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];

	PianoVoice voices[MAX_VOICES];
	const PianoBank* bankSeen = NULL;
	float sustainWas = 0.f;

	// Menu settings, kept with the patch.
	int curve = 1;              // 0 soft, 1 normal, 2 hard
	float reference = 440.f;
	bool stretch = true;
	int polyphony = 32;

	PianoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_VOLUME, 0.f, 2.f, 1.f, "Volume", "%", 0.f, 100.f);
		configParam(P_RELEASE, 0.f, 2.f, 1.f, "Release — the strings' resonance as a key comes up",
			"%", 0.f, 100.f);
		configParam(P_HAMMER, 0.f, 2.f, 1.f, "Hammer — the key and hammer noise as a key comes up",
			"%", 0.f, 100.f);
		configParam(P_PEDAL, 0.f, 2.f, 1.f, "Pedal — the noise of the sustain pedal", "%", 0.f, 100.f);
		configParam(P_BRIGHT, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);
		configParam(P_DYNAMICS, 0.f, 1.f, 1.f,
			"Dynamics — how far soft and loud notes differ", "%", 0.f, 100.f);
		configParam(P_DAMPING, 0.f, 1.f, 0.8f,
			"Damping — how long a note takes to die once its key is up", " s", 40.f, 0.05f);
		configSwitch(P_SUSTAIN, 0.f, 1.f, 0.f, "Sustain pedal", {"Up", "Down"});
		configSwitch(P_SOFT, 0.f, 1.f, 0.f, "Soft pedal", {"Up", "Down"});
		configInput(I_MPX, "MPX in — takes an MPX output only");
		configOutput(O_L, "Left");
		configOutput(O_R, "Right");
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

	PianoVoice* allocate(bool noise) {
		const int lo = noise ? MAX_VOICES - NOISE_VOICES : 0;
		const int hi = noise ? MAX_VOICES : std::min(polyphony, MAX_VOICES - NOISE_VOICES);
		PianoVoice* quietest = NULL;
		for (int i = lo; i < hi; i++) {
			if (!voices[i].active)
				return &voices[i];
			// THE QUIETEST IS TAKEN when all are in use: a piano's notes decay, so the one that
			// has faded most is the one least missed.
			if (!quietest || voices[i].follow * voices[i].env < quietest->follow * quietest->env)
				quietest = &voices[i];
		}
		return quietest;
	}

	void startOneShot(const PianoRegion* r, float gain, int part, int key, float sampleRate) {
		const PianoBank* bank = gLib.bank.load();
		if (!r || !bank || gain <= 0.0001f)
			return;
		PianoVoice* v = allocate(true);
		if (!v)
			return;
		*v = PianoVoice();
		v->active = true;
		v->region = r;
		v->sample = &bank->samples[r->sample];
		v->part = part;
		v->amp = gain * r->gain;
		v->key = key;
		// Pitched like the strings when the recording is one per zone, as the resonances are; the
		// hammer noise is one recording per key and is played as it was recorded.
		const float m = (part == PART_RESONANCE) ? noteOf(((float) key - 60.f) / 12.f) : (float) r->key;
		v->step = std::pow(2.0, (m - r->key) / 12.0) * SAMPLES_RATE / sampleRate;
	}

	void noteOn(int64_t source, float pitchV, float level, float sampleRate, float soft) {
		const PianoBank* bank = gLib.bank.load();
		if (!bank)
			return;
		const float m = noteOf(pitchV);
		const int key = math::clamp((int) std::lround(m), 21, 108);
		int vel = velocityOf(level);
		// DYNAMICS narrows the spread about the middle, for the layer chosen and for the level
		// alike, so turned down every note sounds much alike whatever its velocity.
		const float dyn = params[P_DYNAMICS].getValue();
		float layerVel = 64.f + (vel - 64.f) * dyn;
		// THE SOFT PEDAL, imitated: the set has no una corda recording, so it favours the softer
		// layers here and darkens the tone in the mix.
		layerVel *= 1.f - 0.35f * soft;
		const int lv = math::clamp((int) std::lround(layerVel), 1, 127);
		const PianoRegion* r = bank->find(PART_STRINGS, key, lv);
		if (!r)
			return;
		PianoVoice* v = allocate(false);
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
		v->undamped = r->undamped;
		v->key = key;
		v->vel = lv;
		// The set's own velocity tracking, from its SFZ: seventy-three per cent.
		const float vt = 0.73f;
		const float x = lv / 127.f;
		v->amp = ((1.f - vt) + vt * x * x) * (1.f - 0.2f * soft);
		aim(*v, sampleRate);
	}

	/** The damper falls: the note starts to die, and the strings' release resonance sounds at a
	level that follows how loud the note still is. */
	void damp(PianoVoice& v, float sampleRate, float sustain) {
		if (v.releasing || v.undamped)
			return;
		const float k = params[P_DAMPING].getValue();
		float seconds = 0.05f * std::pow(40.f, k);
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
		startOneShot(bank->find(PART_RESONANCE, v.key, v.vel), gain, PART_RESONANCE, v.key, sampleRate);
	}

	void noteOff(int64_t source, float sampleRate, float sustain) {
		const PianoBank* bank = gLib.bank.load();
		for (PianoVoice& v : voices) {
			if (!v.active || v.part != PART_STRINGS || v.source != source || !v.keyDown)
				continue;
			v.keyDown = false;
			// The key coming up is heard whatever the pedal is doing.
			if (bank)
				startOneShot(bank->find(PART_HAMMER, v.key, v.vel), params[P_HAMMER].getValue() * v.amp,
					PART_HAMMER, v.key, sampleRate);
			if (sustain < 0.95f)
				damp(v, sampleRate, sustain);
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

	void silenceAll() {
		for (PianoVoice& v : voices)
			v.active = false;
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

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
		const float sustain = std::max(busSustain, params[P_SUSTAIN].getValue());
		const float soft = std::max(busSoft, params[P_SOFT].getValue());
		const float sr = args.sampleRate;

		if (bank) {
			Event e;
			while (reader.next(e)) {
				if (e.kind == Event::ON)
					noteOn(e.handle, e.pitch, e.level, sr, soft);
				else if (e.kind == Event::OFF)
					noteOff(e.handle, sr, sustain);
				else
					update(e, sr);
			}

			// THE PEDAL MOVING. Down is heard as the pedal's own noise; up lets every damper fall
			// on the notes whose keys are already up.
			const bool wasDown = sustainWas >= 0.5f, isDown = sustain >= 0.5f;
			if (isDown != wasDown) {
				startOneShot(bank->pedal(isDown ? PART_PEDAL_DOWN : PART_PEDAL_UP, random::uniform()),
					params[P_PEDAL].getValue(), isDown ? PART_PEDAL_DOWN : PART_PEDAL_UP, 60, sr);
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
			outL += L;
			outR += R;
		}

		const float volume = params[P_VOLUME].getValue() * 5.f;
		outputs[O_L].setVoltage(math::clamp(outL * volume, -12.f, 12.f));
		outputs[O_R].setVoltage(math::clamp(outR * volume, -12.f, 12.f));
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "curve", json_integer(curve));
		json_object_set_new(rootJ, "reference", json_real(reference));
		json_object_set_new(rootJ, "stretch", json_boolean(stretch));
		json_object_set_new(rootJ, "polyphony", json_integer(polyphony));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "curve")) curve = math::clamp((int) json_integer_value(j), 0, 2);
		if (json_t* j = json_object_get(rootJ, "reference")) reference = json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "stretch")) stretch = json_is_true(j);
		if (json_t* j = json_object_get(rootJ, "polyphony"))
			polyphony = math::clamp((int) json_integer_value(j), 8, MAX_VOICES - NOISE_VOICES);
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

static Layout pianoLayout() {
	Layout L;
	L.hp = 8.f;
	L.title = "mpxPiano";
	L.titleAbove = "DREAMER DEVELOPMENT";

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
	auto latch = [&](const char* key, float x, float y, int id, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "latch";
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
	knob("p.hammer", COL_B, 64.5f, PianoModule::P_HAMMER, "HAMMER", 0.f);
	knob("p.pedal", COL_A, 82.f, PianoModule::P_PEDAL, "PEDAL", 0.f);

	// The pedals by hand, beside the pedal noise they set off.
	latch("p.sustain", PAIR_L, 82.f, PianoModule::P_SUSTAIN, "SUST");
	latch("p.soft", PAIR_R, 82.f, PianoModule::P_SOFT, "SOFT");

	// THE SAMPLES: a Download button until they are installed, then the credit. See PianoStatus.
	Item status;
	status.key = "d.status"; status.kind = Item::DISPLAY; status.x = 3.f; status.y = 94.f;
	status.w = PANEL_W - 6.f; status.h = 12.f;
	L.items.push_back(status);

	// One cable in, a stereo pair out, the pair under the two pedals.
	jack("in.mpx", Item::PORT_IN, 10.5f, PianoModule::I_MPX, "mpx IN", NOTE_CABLE);
	jack("out.l", Item::PORT_OUT, PAIR_L, PianoModule::O_L, "L", SIG_AUDIO);
	jack("out.r", Item::PORT_OUT, PAIR_R, PianoModule::O_R, "R", SIG_AUDIO);

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
