/** mpxScore playing into the bowed modules — and mpxPiano, if asked — from a command line, timed.

WHY THERE IS A PROGRAM FOR THIS. Rack's meter showed the violin and the viola at eighteen to twenty
per cent while the bench of the player alone (make bowplayer ARGS="quartet ...") measured nine. Here
the real modules — the score, mpxViolin, mpxViola, mpxCello, the piano — are made by their Models as
Rack makes them and run sample by sample as its engine runs them, each one's process() timed as its
meter times it, so the module's own path is measured and not the player's alone.

    make stringbench ARGS="score.mxl FROM_BAR SECONDS [violin=ROW] [viola=ROW] [cello=ROW] [piano=ROW] [realtime]
                          [economy] [rehearsal=full|two|guessed|live] [raw]"

A ROW is the score's row the part is on: mpxScore puts the parts with the most notes on the first
rows. realtime runs in blocks of 256 samples and waits for the clock between them, as an audio
interface does, so that the planning threads compete for the cores as they do in Rack; without it
everything runs as fast as it can. raw writes each module's output to build/stringbench-NAME.raw, as
floats, so that two builds can be compared sample for sample.
*/
#include <rack.hpp>
#include "../src/BowedModule.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace rack;

void* benchPianoMake();
bool benchPianoWaitForSamples(double seconds);
void benchPianoLink(void* m, int slot, uint32_t generation);
void benchPianoProcess(void* m, float sampleRate, int64_t frame, float* left, float* right);
void* benchScoreMake(const char* path, std::string* why);
int benchScoreSlot(void* m, uint32_t* generation, int row);
void benchScoreStart(void* m, int bar);
void benchScoreProcess(void* m, float sampleRate, int64_t frame);

Plugin* pluginInstance = NULL;

static const float RATE = 48000.f;
static const int BLOCK = 256;

/** PLAYED FROM ITS PORTS, with no cable: make stringbench ARGS="ports violin|viola|cello". A gate on
C4, the pitch moved to E4 and G4 while it stays up (two slurs), let go; a louder gate on A4 with a
trill-sized wobble left alone; a three-channel chord, a player each, its top channel then slurred up
while the others hold; three channels coming in one after another and going in reverse, each player
following its own channel's gate alone; four channels in unison on A4, a small section; written to build/stringbench-ports-NAME.wav at its own peak. */
static int portsSection(const std::string& name) {
	Model* model = name == "viola" ? modelMpxViola : (name == "cello" ? modelMpxCello : modelMpxViolin);
	px::BowedModule* m = dynamic_cast<px::BowedModule*>(model->createModule());
	m->build();
	const float down = name == "cello" ? -1.f : (name == "viola" ? -0.f : 0.f);
	Module::ProcessArgs args;
	args.sampleRate = RATE;
	args.sampleTime = 1.f / RATE;
	std::vector<float> out;
	auto& gate = m->inputs[px::BowedModule::I_GATE];
	auto& pitch = m->inputs[px::BowedModule::I_PITCH];
	auto& level = m->inputs[px::BowedModule::I_LEVEL];
	double busy = 0.0;
	// AS A CABLE WOULD: Rack keeps an input's channels at nought until one is connected.
	gate.channels = pitch.channels = level.channels = 1;
	auto run = [&](double seconds, int gates, const float* g, int pitches, const float* p, float l) {
		gate.channels = (uint8_t) gates;
		pitch.channels = (uint8_t) pitches;
		for (int c = 0; c < gates; c++)
			gate.setVoltage(g[c], c);
		for (int c = 0; c < pitches; c++)
			pitch.setVoltage(p[c] + down, c);
		level.setVoltage(l);
		for (size_t i = 0; i < (size_t) (seconds * RATE); i++) {
			args.frame = (int64_t) out.size();
			const auto a = std::chrono::steady_clock::now();
			m->process(args);
			busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
			out.push_back(m->outputs[px::BowedModule::O_OUT].getVoltage());
			APP->engine->stepBlock(1);
			if (out.size() % BLOCK == 0)
				m->build();
		}
	};
	const float up[4] = {10.f, 10.f, 10.f, 10.f}, none[4] = {0.f, 0.f, 0.f, 0.f};
	const float c4[1] = {0.f}, e4[1] = {4.f / 12.f}, g4[1] = {7.f / 12.f}, a4[1] = {9.f / 12.f}, a4w[1] = {9.2f / 12.f};
	const float chord[3] = {7.f / 12.f, 4.f / 12.f, 0.f}, moved[3] = {9.f / 12.f, 4.f / 12.f, 0.f};
	const float under[3] = {0.f, -3.f / 12.f, -7.f / 12.f};
	const float first[3] = {10.f, 0.f, 0.f}, two[3] = {10.f, 10.f, 0.f};
	const float unison[4] = {9.f / 12.f, 9.f / 12.f, 9.f / 12.f, 9.f / 12.f};
	run(0.3, 1, none, 1, c4, 4.f);
	run(0.8, 1, up, 1, c4, 4.f);
	run(0.6, 1, up, 1, e4, 4.f);
	run(0.8, 1, up, 1, g4, 4.f);
	run(0.4, 1, none, 1, g4, 4.f);
	run(0.4, 1, up, 1, a4, 7.f);
	run(0.4, 1, up, 1, a4w, 7.f);
	run(0.4, 1, up, 1, a4, 7.f);
	run(0.4, 1, none, 1, a4, 7.f);
	run(1.2, 3, up, 3, chord, 5.f);
	run(1.0, 3, up, 3, moved, 5.f);
	run(0.8, 3, none, 3, moved, 5.f);
	run(0.5, 3, first, 3, under, 5.f);
	run(0.5, 3, two, 3, under, 5.f);
	run(1.0, 3, up, 3, under, 5.f);
	run(0.5, 3, two, 3, under, 5.f);
	run(0.5, 3, first, 3, under, 5.f);
	run(0.8, 3, none, 3, under, 5.f);
	run(2.0, 4, up, 4, unison, 5.f);
	run(1.0, 4, none, 4, unison, 5.f);
	float peak = 1e-9f;
	for (float v : out)
		peak = std::max(peak, std::fabs(v));
	const std::string path = "build/stringbench-ports-" + name + ".wav";
	FILE* f = std::fopen(path.c_str(), "wb");
	const uint32_t n = (uint32_t) out.size(), rate = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + 2 * n); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(2 * n);
	for (float v : out) {
		const int16_t s = (int16_t) std::lround(32767.0 * 0.89 * v / peak);
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
	// The level by tenths of a second, to see the notes and the silences.
	std::printf("%s from its ports, peak %.2f V; level by tenths of a second, dB:\n ", name.c_str(), peak);
	for (size_t a = 0; a + 4800 <= out.size(); a += 4800) {
		double e = 0.0;
		for (size_t i = a; i < a + 4800; i++)
			e += out[i] * out[i];
		std::printf(" %.0f", 10.0 * std::log10(e / 4800.0 + 1e-12));
	}
	std::printf("\nprocess() %.1f%% of real time\nwrote %s\n", 100.0 * busy / ((double) out.size() / RATE), path.c_str());
	return 0;
}


int main(int argc, char** argv) {
	if (argc >= 3 && std::string(argv[1]) == "ports") {
		setvbuf(stdout, NULL, _IONBF, 0);
		const char* home = std::getenv("HOME");
		asset::userDir = std::string(home ? home : "") + "/Library/Application Support/Rack2";
		random::init();
		Context* context = new Context;
		contextSet(context);
		context->engine = new engine::Engine;
		return portsSection(argv[2]);
	}
	if (argc < 4) {
		std::printf("usage: stringbench score.mxl FROM_BAR SECONDS [violin=ROW] [viola=ROW] [cello=ROW] [piano=ROW] [realtime]\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	const char* home = std::getenv("HOME");
	asset::userDir = std::string(home ? home : "") + "/Library/Application Support/Rack2";
	random::init();
	Context* context = new Context;
	contextSet(context);
	context->engine = new engine::Engine;

	struct Played {
		std::string name;
		px::BowedModule* module = nullptr;
		int row = -1;
		double busy = 0.0, worstBlock = 0.0;
		std::vector<float> out;
	};
	std::vector<Played> strings;
	int pianoRow = -1;
	bool realtime = false, economy = false, raw = false;
	int rehearsal = 3;
	const char* const REHEARSAL[4] = {"full", "two", "guessed", "live"};
	for (int a = 4; a < argc; a++) {
		const std::string arg = argv[a];
		const size_t eq = arg.find('=');
		const std::string key = arg.substr(0, eq);
		const int row = eq == std::string::npos ? -1 : std::atoi(arg.c_str() + eq + 1);
		Model* model = key == "violin" ? modelMpxViolin : (key == "viola" ? modelMpxViola
			: (key == "cello" ? modelMpxCello : nullptr));
		if (model) {
			Played p;
			p.name = key;
			p.row = row;
			p.module = dynamic_cast<px::BowedModule*>(model->createModule());
			strings.push_back(p);
		}
		else if (key == "piano")
			pianoRow = row;
		else if (key == "realtime")
			realtime = true;
		else if (key == "economy")
			economy = true;
		else if (key == "raw")
			raw = true;
		else if (key == "rehearsal") {
			const std::string r = arg.substr(eq + 1);
			rehearsal = r == "full" ? 0 : (r == "two" ? 1 : (r == "live" ? 3 : 2));
		}
	}
	// ECONOMY ASKED FOR, as the right-click menu asks for it: the engine built again at the next step.
	for (Played& p : strings) {
		p.module->setEconomy(economy);
		p.module->setRehearsing(rehearsal);
		p.module->build();
	}

	std::string why;
	void* score = benchScoreMake(argv[1], &why);
	if (!score) {
		std::printf("the score could not be read: %s\n", why.c_str());
		return 1;
	}
	for (Played& p : strings) {
		uint32_t generation = 0;
		const int slot = benchScoreSlot(score, &generation, p.row);
		p.module->link(&slot, &generation, 1);
	}
	void* piano = nullptr;
	double pianoBusy = 0.0;
	if (pianoRow >= 0) {
		piano = benchPianoMake();
		std::printf("loading the piano's samples...\n");
		if (!benchPianoWaitForSamples(300.0)) {
			std::printf("the samples did not load\n");
			return 1;
		}
		uint32_t generation = 0;
		const int slot = benchScoreSlot(score, &generation, pianoRow);
		benchPianoLink(piano, slot, generation);
	}
	benchScoreStart(score, std::atoi(argv[2]));

	const size_t n = (size_t) (std::atof(argv[3]) * RATE);
	Module::ProcessArgs args;
	args.sampleRate = RATE;
	args.sampleTime = 1.f / RATE;
	const auto from = std::chrono::steady_clock::now();
	std::vector<double> blockBusy(strings.size(), 0.0);
	for (size_t i = 0; i < n; i++) {
		args.frame = (int64_t) i;
		benchScoreProcess(score, RATE, (int64_t) i);
		for (size_t k = 0; k < strings.size(); k++) {
			const auto a = std::chrono::steady_clock::now();
			strings[k].module->process(args);
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
			strings[k].busy += took;
			if (raw)
				strings[k].out.push_back(strings[k].module->outputs[px::BowedModule::O_OUT].getVoltage());
			blockBusy[k] += took;
			if (took > 0.001 && std::getenv("SPIKES"))
				std::printf("  %-7s %8.3f s  one sample took %6.2f ms\n", strings[k].name.c_str(), (double) i / RATE,
					took * 1000.0);
		}
		if (piano) {
			float l, r;
			const auto a = std::chrono::steady_clock::now();
			benchPianoProcess(piano, RATE, (int64_t) i, &l, &r);
			pianoBusy += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
		}
		// RACK'S FRAME MOVED ON, as its engine moves it after each sample: what the score stamps its
		// announcements with and the modules count time by.
		APP->engine->stepBlock(1);
		if ((i + 1) % BLOCK == 0) {
			// THE WIDGETS' STEP, on what would be the main thread: the engines rebuilt for the
			// rate and the part's range when those are known.
			for (size_t k = 0; k < strings.size(); k++) {
				strings[k].module->build();
				strings[k].worstBlock = std::max(strings[k].worstBlock, blockBusy[k] * RATE / BLOCK);
				blockBusy[k] = 0.0;
			}
			if (realtime) {
				const double due = (double) (i + 1) / RATE;
				while (std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count() < due)
					std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
		}
	}
	const double seconds = (double) n / RATE;
	std::printf("\n%.0f s of %s from bar %s%s%s, rehearsal %s, each module's process() as a share of real time:\n",
		seconds, argv[1], argv[2], realtime ? ", in real time" : "", economy ? ", economy" : "", REHEARSAL[rehearsal]);
	for (const Played& p : strings)
		std::printf("  %-7s %5.1f%%   the worst block of %d samples %5.1f%%\n", p.name.c_str(),
			100.0 * p.busy / seconds, BLOCK, 100.0 * p.worstBlock);
	if (piano)
		std::printf("  piano   %5.1f%%\n", 100.0 * pianoBusy / seconds);
	for (const Played& p : strings) {
		if (!raw)
			continue;
		FILE* f = std::fopen(("build/stringbench-" + p.name + ".raw").c_str(), "wb");
		std::fwrite(p.out.data(), sizeof(float), p.out.size(), f);
		std::fclose(f);
	}
	return 0;
}
