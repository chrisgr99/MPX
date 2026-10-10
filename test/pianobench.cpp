/** mpxScore playing into mpxPiano, from a command line, with the clicks found.

WHY THERE IS A PROGRAM FOR THIS. A click in the piano is a fault heard in one bar of one piece at
one setting, described from memory afterwards. Here the two real modules are run together sample by
sample exactly as Rack runs them, the passage is written to a file to be listened to, and every
place the output jumps further in one sample than the music around it accounts for is listed with
the bar it is in and what the voice pool was doing at that moment.

    make pianobench ARGS="score.mxl FROM_BAR SECONDS [polyphony] [pID=VALUE ...] [at=SECONDS]"

pID=VALUE sets the piano's parameter ID: 0 volume, 1 release, 2 emphasis, 3 pedalling, 4
brightness, 5 dynamics, 6 damping. at=SECONDS prints every voice, sample by sample, for three
milliseconds either side of that moment.
*/
#include <rack.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace rack;

void* benchPianoMake();
bool benchPianoWaitForSamples(double seconds);
void benchPianoLink(void* m, int slot, uint32_t generation);
void benchPianoPolyphony(void* m, int n);
void benchPianoProcess(void* m, float sampleRate, int64_t frame, float* left, float* right);
void benchPianoParam(void* m, int id, float value);
int benchPianoVoices(void* m, int* keys, int* parts, int* fading, int most);
void benchPianoHammer(void* m, float amount);
int benchPianoSlots(void* m, int* active, int* keys, int* parts, float* amps, float* levels, int* fading,
	int most);
void benchPianoPool(void* m, int* took, int* cut, int* letGo, int* sounding);
void* benchScoreMake(const char* path, std::string* why);
int benchScoreSlot(void* m, uint32_t* generation);
void benchScoreStart(void* m, int bar);
void benchScoreProcess(void* m, float sampleRate, int64_t frame);
int benchScoreBar(void* m);

Plugin* pluginInstance = NULL;

static const float RATE = 48000.f;


static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r,
		float scale) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t frames = (uint32_t) l.size(), bytes = frames * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32((uint32_t) RATE); u32((uint32_t) RATE * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (uint32_t i = 0; i < frames; i++) {
		const int16_t a = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, l[i] * scale)) * 32000.f);
		const int16_t b = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, r[i] * scale)) * 32000.f);
		std::fwrite(&a, 2, 1, f);
		std::fwrite(&b, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}


int main(int argc, char** argv) {
	if (argc < 4) {
		std::printf("usage: pianobench score.mxl FROM_BAR SECONDS [polyphony]\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	const char* home = std::getenv("HOME");
	asset::userDir = std::string(home ? home : "") + "/Library/Application Support/Rack2";
	random::init();
	// AN ENGINE FOR THE NOTE BUS TO ASK THE TIME OF. Nothing is added to it and it is never
	// stepped; the two modules are run by hand below, one sample at a time, as it would run them.
	Context* context = new Context;
	contextSet(context);
	context->engine = new engine::Engine;

	void* piano = benchPianoMake();
	double at = -1.0;
	double sharp = 8.0;
	double traceFrom = -1.0, traceTo = -1.0;
	for (int a = 4; a < argc; a++) {
		const std::string arg = argv[a];
		if (arg.rfind("at=", 0) == 0)
			at = std::atof(arg.c_str() + 3);
		else if (arg.rfind("k=", 0) == 0)
			sharp = std::atof(arg.c_str() + 2);
		else if (arg.rfind("hammer=", 0) == 0)
			benchPianoHammer(piano, (float) std::atof(arg.c_str() + 7));
		else if (arg.rfind("trace=", 0) == 0) {
			traceFrom = std::atof(arg.c_str() + 6);
			traceTo = std::atof(arg.c_str() + arg.find('-') + 1);
		}
		else if (arg[0] == 'p' && arg.find('=') != std::string::npos)
			benchPianoParam(piano, std::atoi(arg.c_str() + 1),
				(float) std::atof(arg.c_str() + arg.find('=') + 1));
		else
			benchPianoPolyphony(piano, std::atoi(arg.c_str()));
	}
	std::printf("loading the piano's samples...\n");
	if (!benchPianoWaitForSamples(300.0)) {
		std::printf("the samples did not load\n");
		return 1;
	}
	std::string why;
	void* score = benchScoreMake(argv[1], &why);
	if (!score) {
		std::printf("the score could not be read: %s\n", why.c_str());
		return 1;
	}
	uint32_t generation = 0;
	const int slot = benchScoreSlot(score, &generation);
	benchPianoLink(piano, slot, generation);
	benchScoreStart(score, std::atoi(argv[2]));

	const size_t n = (size_t) (std::atof(argv[3]) * RATE);
	std::vector<float> L(n), R(n);
	std::vector<int> bar(n), took(n), cut(n), letGo(n), sounding(n);
	int lastTook = 0, lastCut = 0, lastLet = 0;
	// THE TRACE: every voice that starts or stops between two moments, with the bar, its key, what
	// kind of sound it is and how loud. Parts: 0 string, 1 release resonance, 2 key noise, 3 and 4
	// the pedal.
	static int wasActive[512], wasKey[512];
	bool traceStarted = false;
	int act[512], key[512], part[512], fad[512];
	float amp[512], lev[512];
	static const char* PART_NAMES[] = {"string", "resonance", "key noise", "pedal down", "pedal up"};
	for (size_t i = 0; i < n; i++) {
		benchScoreProcess(score, RATE, (int64_t) i);
		benchPianoProcess(piano, RATE, (int64_t) i, &L[i], &R[i]);
		bar[i] = benchScoreBar(score);
		int t, c, g, s;
		benchPianoPool(piano, &t, &c, &g, &s);
		// The counts are reset every ten seconds by the module's own report.
		took[i] = (t >= lastTook) ? t - lastTook : t;
		cut[i] = (c >= lastCut) ? c - lastCut : c;
		letGo[i] = (g >= lastLet) ? g - lastLet : g;
		lastTook = t; lastCut = c; lastLet = g;
		sounding[i] = s;
		if (traceFrom >= 0.0 && (double) i / RATE >= traceFrom && (double) i / RATE <= traceTo) {
			const int slots = benchPianoSlots(piano, act, key, part, amp, lev, fad, 512);
			for (int v = 0; v < slots; v++) {
				const bool starts = traceStarted && act[v] && (!wasActive[v] || key[v] != wasKey[v]);
				const bool stops = traceStarted && !act[v] && wasActive[v];
				if (starts && part[v] >= 0 && part[v] <= 4)
					std::printf("%8.4f s  bar %3d  START %-10s key %3d  recording %4d  gain %.3f\n",
						(double) i / RATE, bar[i], PART_NAMES[part[v]], key[v] % 1000, key[v] / 1000 - 1,
						amp[v]);
				else if (stops)
					std::printf("%8.4f s  bar %3d  stop  slot %3d key %3d\n", (double) i / RATE, bar[i], v,
						wasKey[v]);
				wasActive[v] = act[v];
				wasKey[v] = key[v];
			}
			traceStarted = true;
		}
		if (at >= 0.0 && std::fabs((double) i / RATE - at) < 0.003) {
			int keys[256], parts[256], fading[256];
			const int nv = benchPianoVoices(piano, keys, parts, fading, 256);
			std::printf("%8.5f s  out %8.4f  voices:", (double) i / RATE, L[i] + R[i]);
			for (int k = 0; k < nv; k++)
				std::printf(" %d%s%s", keys[k], parts[k] == 0 ? "" : (std::string("/p") + std::to_string(parts[k])).c_str(),
					fading[k] ? "~" : "");
			std::printf("\n");
		}
	}

	// ---- the clicks ----
	// A JUMP IN ONE SAMPLE that the music around it does not account for: the second difference,
	// against its own typical size over the surrounding twenty milliseconds. A piano's attack is
	// steep but it is steep for many samples running; a click is one sample standing out.
	std::vector<float> d(n, 0.f);
	for (size_t i = 2; i < n; i++)
		d[i] = std::fabs((L[i] + R[i]) - 2.f * (L[i - 1] + R[i - 1]) + (L[i - 2] + R[i - 2]));
	const size_t w = (size_t) (0.01 * RATE);
	std::vector<double> run(n + 1, 0.0);
	for (size_t i = 0; i < n; i++)
		run[i + 1] = run[i] + (double) d[i] * d[i];
	int found = 0;
	size_t lastAt = 0;
	std::printf("\nCLICKS: a one-sample jump over %.0f times the typical step around it\n", sharp);
	for (size_t i = w; i + w < n; i++) {
		const double around = std::sqrt((run[i + w] - run[i - w] - (double) d[i] * d[i]) / (2.0 * w - 1.0));
		if (around <= 0.0 || d[i] < sharp * around || d[i] < 0.002f)
			continue;
		if (found > 0 && i - lastAt < (size_t) (0.02 * RATE))
			continue;
		lastAt = i;
		found++;
		int t = 0, c = 0, g = 0;
		for (size_t k = (i > 240 ? i - 240 : 0); k <= i && k < n; k++) { t += took[k]; c += cut[k]; g += letGo[k]; }
		if (found <= 60)
			std::printf("  %7.3f s  bar %3d  jump %6.3f  %4.1f x  sounding %3d  in the 5 ms before: took %d cut %d let go %d\n",
				i / RATE, bar[i], d[i], d[i] / around, sounding[i], t, c, g);
	}
	// THE LEVEL, per channel, over every twentieth of a second: the loudest sample, and whether it
	// reached the soft clip that starts at MPX_CLIP_FROM volts.
	if (std::getenv("BENCH_LEVELS")) {
		std::printf("\nLEVELS: peak volts per 50 ms, either channel\n");
		const size_t step = (size_t) (0.05 * RATE);
		for (size_t a = 0; a + step <= n; a += step) {
			float p = 0.f;
			int over = 0;
			for (size_t i = a; i < a + step; i++) {
				p = std::fmax(p, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
				if (std::fabs(L[i]) > 8.5f || std::fabs(R[i]) > 8.5f)
					over++;
			}
			std::printf("  %7.2f s  bar %3d  %6.2f V  %5.2f ms in the clip %s\n", a / RATE, bar[a], p,
				1000.0 * over / RATE, p > 8.5f ? "IN THE CLIP" : "");
		}
	}
	int totalTook = 0, totalCut = 0, totalLet = 0, most = 0;
	for (size_t i = 0; i < n; i++) { totalTook += took[i]; totalCut += cut[i]; totalLet += letGo[i]; most = std::max(most, sounding[i]); }
	std::printf("\n%d clicks; over the passage %d notes taken from sounding strings, %d fades cut short, "
		"%d silent notes let go, at most %d strings sounding\n", found, totalTook, totalCut, totalLet, most);
	float peak = 0.f;
	for (size_t i = 0; i < n; i++)
		peak = std::fmax(peak, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
	// A FIXED SCALE when comparing renders, ten volts to full scale, so they differ only in what
	// was changed and not in how each was normalised.
	const char* out = std::getenv("BENCH_WAV");
	writeWav(out ? out : "build/pianobench.wav", L, R,
		std::getenv("BENCH_FIXED") ? 0.1f : 0.9f / std::fmax(peak, 1e-6f));
	return 0;
}
