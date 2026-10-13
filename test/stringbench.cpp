/** mpxScore playing into the bowed modules — and mpxPiano, if asked — from a command line, timed.

WHY THERE IS A PROGRAM FOR THIS. Rack's meter showed the violin and the viola at eighteen to twenty
per cent while the bench of the player alone (make bowplayer ARGS="quartet ...") measured nine. Here
the real modules — the score, mpxViolin, mpxViola, mpxCello, the piano — are made by their Models as
Rack makes them and run sample by sample as its engine runs them, each one's process() timed as its
meter times it, so the module's own path is measured and not the player's alone.

    make stringbench ARGS="score.mxl FROM_BAR SECONDS [violin=ROW] [viola=ROW] [cello=ROW] [piano=ROW] [realtime]
                          [economy] [rehearsal=full|two|guessed|live]"

A ROW is the score's row the part is on: mpxScore puts the parts with the most notes on the first
rows. realtime runs in blocks of 256 samples and waits for the clock between them, as an audio
interface does, so that the planning threads compete for the cores as they do in Rack; without it
everything runs as fast as it can.
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


int main(int argc, char** argv) {
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
	};
	std::vector<Played> strings;
	int pianoRow = -1;
	bool realtime = false, economy = false;
	int rehearsal = 2;
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
	return 0;
}
