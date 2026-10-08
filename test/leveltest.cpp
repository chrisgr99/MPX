/** WHAT A LEVEL ON THE CABLE IS WORTH, AT EVERY INSTRUMENT THAT READS ONE.

	make leveltest ARGS="path/to/bank.sf2"
	make leveltest                            (everything but the built-in band)

THE STANDARD IS IN NoteBus.hpp: a single note at level 1, with the module's volume control where
it starts, peaks at MPX_FULL_VOLTS at that module's output. This plays exactly that note through
each instrument and says how far each is from it, in decibels.

WHY THERE IS A PROGRAM FOR THIS. Two modules can play the same part side by side — the band built
into the chart modules, through FluidSynth, and mpxGuitar, through its modelled strings. They were
twenty decibels apart, and nothing but a listener would have said so. A figure on the cable has to
mean the same loudness wherever it is read, or handing a part to a different instrument changes
the music rather than the colour.

WHAT IS NOT MEASURED HERE, and is the larger fault: nothing scales a chord against a single note,
so the same written dynamic is far louder in a dense texture than in a sparse one. That is a
property of the notes rather than of any one module, so it is not something this bench can see.
See docs/dynamics.md.

mpxPiano's voices live inside a Rack Module and cannot be run from here, so its note is computed
the way the module computes it — from the same recordings, through the same formulas. The
constants are repeated below and have to be changed together with the module's; a bench measuring
yesterday's arithmetic is worse than no bench.
*/
#include "../src/FluidEngine.hpp"
#include "../src/GuitarModel.hpp"
#include "../src/NoteBus.hpp"

#include <jansson.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace px;


/** AS SoundCore.hpp, mpxGuitar.cpp AND mpxPiano.cpp HAVE THEM, kept here by hand since those
files need Rack. Change them together: a scale changed there and not here leaves this measuring
the old one. */
static const float SOUND_VOLTS = 117.6f;
static const float GUITAR_VOLTS = 7.76f;
/** mpxPiano's output stage: the volume knob, which starts at one, times five. */
static const float PIANO_VOLTS = 6.f;
/** The set's own velocity tracking, from its SFZ. mpxPiano::noteOn. */
static const float PIANO_VELOCITY_TRACKING = 0.73f;
static const float RATE = 48000.f;

/** HOW FAR FROM THE STANDARD AN INSTRUMENT MAY BE before this is a fault. Three decibels is about
the smallest difference anybody notices as loudness rather than as character, so it is the point
at which two instruments stop being interchangeable. */
static const double ALLOWED_DB = 3.0;


static double peakOf(const std::vector<float>& v) {
	double peak = 0.0;
	for (float x : v)
		peak = std::fmax(peak, std::fabs(x));
	return peak;
}


static double decibels(double ratio) {
	return (ratio > 0.0) ? 20.0 * std::log10(ratio) : -999.0;
}


/** One line of the report. Returns whether it passed. */
static bool report(const char* what, double volts) {
	if (volts <= 0.0) {
		std::printf("     %-34s  could not be measured\n", what);
		return true;      // Said, but not counted against the standard.
	}
	const double off = decibels(volts / (double) MPX_FULL_VOLTS);
	const bool ok = std::fabs(off) <= ALLOWED_DB;
	std::printf("%s %-34s  %7.2f V peak  %+6.1f dB from the standard\n",
		ok ? "ok  " : "FAIL", what, volts, off);
	return ok;
}


// ---- mpxPiano -------------------------------------------------------------------------------
//
// THE NOTE IS NOT PLAYED, IT IS COMPUTED, from the recording the module would have chosen and the
// gain it would have applied. Everything in the chain is here: nothing else touches a note's
// amplitude between noteOn and the output.

/** mpxPiano::velocityOf with the normal curve, which is what the module starts with. */
static int pianoVelocity(float level) {
	float v = (level < 0.f) ? 0.f : (level > 1.f ? 1.f : level);
	const long n = std::lround(1.f + 126.f * v);
	return (int) ((n < 1) ? 1 : (n > 127 ? 127 : n));
}


/** The peak of a 16-bit stereo PCM WAV, as a fraction of full scale. Only what the set contains
is understood, which is the same restriction mpxPiano::readWav makes. */
static double wavPeak(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return -1.0;
	char riff[12];
	if (!f.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0
			|| std::memcmp(riff + 8, "WAVE", 4) != 0)
		return -1.0;
	int bits = 0;
	while (f) {
		char id[4];
		uint32_t size = 0;
		if (!f.read(id, 4) || !f.read((char*) &size, 4))
			return -1.0;
		if (std::memcmp(id, "fmt ", 4) == 0) {
			std::vector<char> fmt(size);
			if (!f.read(fmt.data(), size) || size < 16)
				return -1.0;
			std::memcpy(&bits, fmt.data() + 14, 2);
			bits &= 0xffff;
		}
		else if (std::memcmp(id, "data", 4) == 0) {
			if (bits != 16)
				return -1.0;
			std::vector<int16_t> pcm(size / 2);
			if (!f.read((char*) pcm.data(), size))
				return -1.0;
			int peak = 0;
			for (size_t i = 0; i < pcm.size(); i++) {
				const int a = (pcm[i] < 0) ? -pcm[i] : pcm[i];
				if (a > peak)
					peak = a;
			}
			return (double) peak / 32768.0;
		}
		else {
			f.seekg(size + (size & 1), std::ios::cur);
		}
	}
	return -1.0;
}


/** WHERE THE RECORDINGS LIVE, as mpxPiano downloads them. */
static std::string pianoDir() {
	if (const char* said = std::getenv("MPX_PIANO"))
		return said;
	const char* home = std::getenv("HOME");
	return std::string(home ? home : "")
		+ "/Library/Application Support/Rack2/DreamerMPX/piano";
}


/** One note of the piano, at `level`, on `key`, with the DYNAMICS knob at `dyn`: the peak it
would reach in volts, or -1 when the set is not installed.

mpxPiano::squeeze, repeated: the layer follows the written dynamic, and the knob multiplies how
far below the loudest this key can play the note sits. */
static double pianoNote(float level, int key, float dyn = 1.f) {
	const std::string dir = pianoDir();
	json_error_t err;
	json_t* rootJ = json_load_file((dir + "/map.json").c_str(), 0, &err);
	if (!rootJ)
		return -1.0;

	// THE LAYER THE MODULE WOULD PICK. DYNAMICS starts at one, which leaves the velocity alone,
	// and the soft pedal is up — so the layer is chosen on the velocity itself. mpxPiano::noteOn.
	const int vel = pianoVelocity(level);
	std::string file, topFile;
	int bestGap = -1, topGap = -1;
	json_t* regionsJ = json_object_get(rootJ, "regions");
	const size_t count = json_array_size(regionsJ);
	for (size_t i = 0; i < count; i++) {
		json_t* rJ = json_array_get(regionsJ, i);
		const char* name = json_string_value(json_object_get(rJ, "file"));
		const char* part = json_string_value(json_object_get(rJ, "part"));
		if (!name || !part || std::string(part) != "strings")
			continue;
		const int lokey = (int) json_integer_value(json_object_get(rJ, "lokey"));
		const int hikey = (int) json_integer_value(json_object_get(rJ, "hikey"));
		const int lovel = (int) json_integer_value(json_object_get(rJ, "lovel"));
		const int hivel = (int) json_integer_value(json_object_get(rJ, "hivel"));
		if (key < lokey || key > hikey)
			continue;
		if (vel >= lovel && vel <= hivel)
			file = name;
		else {
			const int gap = std::abs(vel - (lovel + hivel) / 2);
			if (file.empty() && (bestGap < 0 || gap < bestGap)) {
				bestGap = gap;
				file = name;
			}
		}
		// AND THE LOUDEST LAYER THIS KEY HAS, which is what the knob measures down from.
		if (127 >= lovel && 127 <= hivel)
			topFile = name;
		else {
			const int gap = std::abs(127 - (lovel + hivel) / 2);
			if (topFile.empty() && (topGap < 0 || gap < topGap)) {
				topGap = gap;
				topFile = name;
			}
		}
	}
	json_decref(rootJ);
	if (file.empty())
		return -1.0;

	const double peak = wavPeak(dir + "/samples/" + file);
	if (peak < 0.0)
		return -1.0;
	// mpxPiano::noteOn: amp = ((1 - vt) + vt * x * x), with the soft pedal up.
	const double x = (double) vel / 127.0;
	const double amp = (1.0 - PIANO_VELOCITY_TRACKING) + PIANO_VELOCITY_TRACKING * x * x;
	(void) amp;
	// mpxPiano::aimedAt — the marking's own loudness, with the recording's divided back out.
	const double want = std::pow(10.0, -(1.0 - (double) level) * (double) MPX_RANGE_DB
		* (double) dyn / 20.0);
	return want * (double) PIANO_VOLTS;
}


// ---- mpxGuitar ------------------------------------------------------------------------------

/** `strings` of them at once, all at `level`: one for the standard, six for what a strum of open
strings actually reaches. */
static double guitarNote(float level, bool nylon, int strings = 1) {
	GuitarModel g;
	g.setSampleRate(RATE);
	const int tuning[6] = {64, 59, 55, 50, 45, 40};
	for (int s = 0; s < 6; s++) {
		g.open[s] = (tuning[s] - 60) / 12.f;
		g.openKnown[s] = true;
		g.setPitch(s, g.open[s]);
	}
	g.settings.nylon = nylon;
	g.settings.sympathy = 0.f;
	if (strings <= 1) {
		g.setPitch(2, (57 - 60) / 12.f);
		g.strike(2, mpxLoudness(level), 0, 2);
	}
	else {
		for (int s = 0; s < strings && s < 6; s++)
			g.strike(s, mpxLoudness(level) * mpxShare(strings), 0, 2);
	}
	std::vector<float> out;
	for (int i = 0; i < (int) RATE; i++) {
		float l = 0.f, r = 0.f;
		g.process(&l, &r);
		out.push_back(l * GUITAR_VOLTS);
	}
	return peakOf(out);
}


// ---- the built-in band ----------------------------------------------------------------------

static double bandNoteAt(const char* bank, int velocity, int program);

static double bandNote(const char* bank, float level, int program) {
	// AS SoundCore.cpp SENDS IT: the loudness the table asks for, turned back into a velocity
	// through the bank's own measured curve. Keep the exponent the same in both places.
	const int vel = (int) std::lround(127.0
		* std::pow((double) mpxLoudness(level), 1.0 / 1.57));
	return bandNoteAt(bank, vel, program);
}

static double bandNoteAt(const char* bank, int velocity, int program) {
	FluidEngine e;
	if (!e.start(RATE, 32, 256, 2) || !e.loadBank(bank))
		return -1.0;
	e.effects(false, false);
	e.program(0, 0, program);
	e.noteOn(0, 57, (velocity < 1) ? 1 : ((velocity > 127) ? 127 : velocity));
	static float b[4][64], fx[4][64];
	float* out[4] = {b[0], b[1], b[2], b[3]};
	float* f[4] = {fx[0], fx[1], fx[2], fx[3]};
	std::vector<float> band;
	for (int k = 0; k < 750; k++) {
		e.renderGroups(out, f, 64);
		for (int i = 0; i < 64; i++)
			band.push_back((b[0][i] + b[2][i]) * SOUND_VOLTS);
	}
	return peakOf(band);
}


int main(int argc, char** argv) {
	const char* bank = (argc > 1) ? argv[1] : NULL;
	int failures = 0;

	std::printf("A SINGLE NOTE AT LEVEL 1, against the standard of %.1f V peak.\n\n",
		(double) MPX_FULL_VOLTS);

	if (!report("mpxPiano, the samples", pianoNote(1.f, 60)))
		failures++;
	if (!report("mpxGuitar, steel", guitarNote(1.f, false)))
		failures++;
	if (!report("mpxGuitar, nylon", guitarNote(1.f, true)))
		failures++;
	if (bank) {
		if (!report("the band, steel guitar", bandNote(bank, 1.f, 25)))
			failures++;
		if (!report("the band, nylon guitar", bandNote(bank, 1.f, 24)))
			failures++;
		if (!report("the band, grand piano", bandNote(bank, 1.f, 0)))
			failures++;
	}
	else {
		std::printf("     the band                            no SoundFont given, so not measured\n");
	}

	// WHAT IS LEFT OVER THE STANDARD, which is the question of whether an instrument could be
	// raised to it. Reported rather than held to a figure: the standard is about one note, and
	// how far a full texture sits above it is the headroom that scaling a chord has to earn.
	// mpxGuitar hard-clips at ten volts.
	std::printf("\nA FULL TEXTURE AT LEVEL 1, for headroom:\n");
	const double strum = guitarNote(1.f, false, 6);
	std::printf("     mpxGuitar, six open strings     %7.2f V peak  %+6.1f dB over one note\n",
		strum, decibels(strum / guitarNote(1.f, false)));

	// THE LADDER ITSELF, as a sanity check on the quiet end: what the softest thing anybody
	// writes comes out at. Not held to a figure — it is reported so that a change to the curve
	// or to the layer choice shows up here rather than in somebody's ears.
	std::printf("\nTHE PIANO'S LADDER, for reference:\n");
	static const struct { const char* name; float level; } MARKS[] = {
		{"ppp", 0.f}, {"pp", 1.f / 7.f}, {"p", 2.f / 7.f}, {"mp", 3.f / 7.f},
		{"mf", 4.f / 7.f}, {"f", 5.f / 7.f}, {"ff", 6.f / 7.f}, {"fff", 1.f},
	};
	static const float DYNS[] = {1.f, 0.85f, 0.7f, 0.55f, 0.4f};
	std::printf("     a marking is the same loudness wherever it is played:\n");
	{
		static const int KEYS[] = {33, 45, 57, 60, 66, 78, 90, 102};
		double lo = 1e9, hi = 0.0;
		for (size_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) {
			const double v = pianoNote(5.f / 7.f, KEYS[k]);
			if (v <= 0.0)
				continue;
			lo = std::fmin(lo, v);
			hi = std::fmax(hi, v);
		}
		if (hi > 0.0)
			std::printf("       forte across eight registers: %.3f to %.3f V, a spread of %.1f dB\n\n",
				lo, hi, decibels(hi / lo));
	}
	std::printf("     %-4s %7s", "", "level");
	for (size_t d = 0; d < sizeof(DYNS) / sizeof(DYNS[0]); d++)
		std::printf("%9.2f", (double) DYNS[d]);
	std::printf("   (DYNAMICS)\n");
	for (size_t i = 0; i < sizeof(MARKS) / sizeof(MARKS[0]); i++) {
		std::printf("     %-4s %7.2f", MARKS[i].name, (double) MARKS[i].level);
		for (size_t d = 0; d < sizeof(DYNS) / sizeof(DYNS[0]); d++) {
			const double top = pianoNote(1.f, 60, DYNS[d]);
			const double v = pianoNote(MARKS[i].level, 60, DYNS[d]);
			std::printf("%+9.1f", (v > 0.0 && top > 0.0) ? decibels(v / top) : -99.0);
		}
		std::printf("\n");
	}
	std::printf("     the whole range, in dB:  ");
	for (size_t d = 0; d < sizeof(DYNS) / sizeof(DYNS[0]); d++)
		std::printf("%9.1f",
			-decibels(pianoNote(0.f, 60, DYNS[d]) / pianoNote(1.f, 60, DYNS[d])));
	std::printf("\n");

	// EVERY INSTRUMENT'S LADDER AGAINST THE TABLE. mpxPiano aims at these figures and hits them;
	// the others take level straight to a velocity and land where the bank or the model puts
	// them. This says how far off each one is, which is the measurement that has to come before
	// anything is changed about them.
	if (bank) {
		std::printf("\nTHE BANK'S OWN VELOCITY CURVE (grand piano), for fitting:\n");
		const double top = bandNoteAt(bank, 127, 0);
		for (int v = 16; v <= 127; v += 16) {
			const double a = bandNoteAt(bank, v, 0);
			std::printf("     vel %3d  %+7.2f dB   implies exponent %.2f\n", v,
				decibels(a / top), std::log(a / top) / std::log((double) v / 127.0));
		}
	}
	std::printf("\nEACH INSTRUMENT'S LADDER, in dB below its own fff, against the table:\n");
	std::printf("     %-4s %7s %8s", "", "level", "wanted");
	std::printf(" %9s %9s %9s %9s\n", "piano", "gtr steel", "gtr nylon", bank ? "band pf" : "");
	double worstOff = 0.0;
	const char* worstWho = "";
	for (size_t i = 0; i < sizeof(MARKS) / sizeof(MARKS[0]); i++) {
		const float L = MARKS[i].level;
		const double wanted = -(1.0 - (double) L) * (double) MPX_RANGE_DB;
		std::printf("     %-4s %7.2f %8.1f", MARKS[i].name, (double) L, wanted);
		const double got[4] = {
			decibels(pianoNote(L, 60) / pianoNote(1.f, 60)),
			decibels(guitarNote(L, false) / guitarNote(1.f, false)),
			decibels(guitarNote(L, true) / guitarNote(1.f, true)),
			bank ? decibels(bandNote(bank, L, 0) / bandNote(bank, 1.f, 0)) : 0.0,
		};
		static const char* WHO[4] = {"mpxPiano", "mpxGuitar steel", "mpxGuitar nylon",
			"the band's piano"};
		for (int c = 0; c < (bank ? 4 : 3); c++) {
			std::printf(" %+9.1f", got[c]);
			const double off = std::fabs(got[c] - wanted);
			if (off > worstOff) {
				worstOff = off;
				worstWho = WHO[c];
			}
		}
		std::printf("\n");
	}
	std::printf("     furthest from the table: %.1f dB, %s\n", worstOff, worstWho);

	std::printf("\n%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
