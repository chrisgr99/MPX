/** THE TROMBONE PLAYING A SCORE WITH THE PIANO, rendered: phase 6 of docs/brass.md, for listening before the
module exists.

    make brassrender ARGS="score.mxl SECONDS [from=BAR] [trombone=ROW] [piano=ROW] [unrehearsed] [out=NAME]
        [engine=LEVEL] [list] [trace=FROM-TO]"

The score is read by mpxScore itself, as Rack reads it, and the trombone hears its part over the cable as a
module would — each note announced ahead with its dynamic, its length and how it is to be played — the top
note of any chord, played by BrassPlayer, each slur rehearsed unless asked not to; the piano part is played by
mpxPiano from its samples. Neither runs in real time: the rehearsal, shared out over the machine's threads,
takes longer than the music.

AS THE MODULE PLAYS IT, with engine=1, 2 or 3: through BrassEngine at that Fidelity, the notes announced to
it as mpxTrombone announces them and the samples taken from it in time with the clock — the bench waits
for the clock between blocks of 512 — so that what is heard is what the module would play, and how often
it fell behind is printed.

WITH "list", each note the player played, a line each: when, from the start of the render, the pitch, the
marking, how long it is written, how it was marked and joined, where it was played, how hard it was blown
and the gain it was played at for its loudness; and as it was heard, a metre away, its pitch by YIN over
its middle against the written note, in cents, its level there and its loudest 20 ms in its first 150, in
dB SPL.

WITH "trace=FROM-TO", seconds from the start of the render, the trombone alone every 50 ms between them:
its level over 50 ms, dB SPL a metre away, its loudest 5 ms in them, and its pitch by YIN over 40 ms.

THE TROMBONE'S PART is the row whose instrument the score names a trombone, else whose compass lies in the
trombone's; the piano's the other row with notes.

THE BALANCE. The model's dynamics are its own — 72 dB at ppp to 114 at fff a metre away, wider than the
cable's ladder — so the trombone is set, by one gain over the whole excerpt, 3 dB over the piano in its
loudest stretches, as a soloist sits over an accompanist; the gain is printed. A loud trombone's peaks stand
far over its level — the shock fronts of brassiness, ten times its root mean square at fff — so turned down
to fit them under the clip the whole render was too quiet to hear. Instead the two together are set so that
their loudest three seconds are 16 dB under full scale, and a limiter takes down only the moments a peak
would pass 8 V, under the shared soft clip (mpxSoftClip): looking 3 ms ahead, so the gain is down before the
peak arrives, and back over 60 ms. Out at ten volts full scale, to build/Trombone NAME.wav, stereo, the
trombone in the middle; how often and how far the limiter acted is printed. */
#include <rack.hpp>
#include "../src/BrassEngine.hpp"
#include "../src/BrassPlayer.hpp"
#include "../src/BrassTable.hpp"
#include "../src/Crew.hpp"
#include "../src/NoteBus.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
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

/** Volts, an octave to the volt, with nought at middle C. */
static double hzOf(float volts) { return 261.6256 * std::pow(2.0, (double) volts); }

/** A NOTE AS THE CABLE GIVES IT, as the trombone's player takes it. */
static px::BrassPlayer::Note noteOf(const px::Event& e) {
	px::BrassPlayer::Note n;
	n.hz = hzOf(e.pitch);
	n.level = e.level;
	n.seconds = e.duration > 0.f ? e.duration : 0.25f;
	n.slurred = (e.technique & px::Event::SLURRED) != 0;
	n.glissando = (e.technique & px::Event::LEGATO_SLIDE) != 0;
	n.staccato = (e.technique & px::Event::STACCATO) != 0;
	n.tenuto = (e.technique & px::Event::TENUTO) != 0;
	n.accent = (e.technique & px::Event::ACCENT) != 0;
	n.heavy = (e.technique & px::Event::HEAVY_ACCENT) != 0;
	n.ghost = (e.technique & px::Event::GHOST) != 0;
	n.vibrato = e.vibrato == px::Event::VIBRATO_WIDE ? 2 : e.vibrato == px::Event::VIBRATO_SLIGHT ? 1 : 0;
	return n;
}

static void writeStereo(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t n = (uint32_t) l.size(), bytes = n * 4, rate = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (uint32_t i = 0; i < n; i++)
		for (float v : {l[i], r[i]}) {
			const int16_t s = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, v / 10.f)) * 32767.f);
			std::fwrite(&s, 2, 1, f);
		}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}

/** THE LOUDEST 150 ms of a signal, as a root mean square. */
static double loudest(const std::vector<float>& x) {
	const size_t w = (size_t) (0.15 * RATE);
	double run = 0.0, best = 0.0;
	for (size_t i = 0; i < x.size(); i++) {
		run += (double) x[i] * x[i];
		if (i >= w)
			run -= (double) x[i - w] * x[i - w];
		if (i + 1 >= w)
			best = std::max(best, run);
	}
	return std::sqrt(best / (double) w);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("usage: brassrender score.mxl SECONDS [from=BAR] [trombone=ROW] [piano=ROW] [unrehearsed] [out=NAME] [engine=LEVEL] [list] [trace=FROM-TO]\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	const char* home = std::getenv("HOME");
	asset::userDir = std::string(home ? home : "") + "/Library/Application Support/Rack2";
	random::init();
	Context* context = new Context;
	contextSet(context);
	context->engine = new engine::Engine;
	context->engine->setSampleRate(RATE);

	int from = 1, tromboneRow = -1, pianoRow = -1, engineLevel = 0;
	bool listing = false;
	double traceFrom = -1.0, traceTo = -1.0;
	bool rehearse = true;
	std::string name = "render";
	for (int a = 3; a < argc; a++) {
		const std::string arg = argv[a];
		const size_t eq = arg.find('=');
		const std::string key = arg.substr(0, eq), value = eq == std::string::npos ? "" : arg.substr(eq + 1);
		if (key == "from")
			from = std::atoi(value.c_str());
		else if (key == "trombone")
			tromboneRow = std::atoi(value.c_str());
		else if (key == "piano")
			pianoRow = std::atoi(value.c_str());
		else if (key == "unrehearsed")
			rehearse = false;
		else if (key == "out")
			name = value;
		else if (key == "engine")
			engineLevel = std::atoi(value.c_str());
		else if (key == "list")
			listing = true;
		else if (key == "trace") {
			traceFrom = std::atof(value.c_str());
			const size_t dash = value.find('-');
			traceTo = dash == std::string::npos ? traceFrom + 5.0 : std::atof(value.c_str() + dash + 1);
		}
	}

	std::string why;
	void* score = benchScoreMake(argv[1], &why);
	if (!score) {
		std::printf("the score could not be read: %s\n", why.c_str());
		return 1;
	}
	// WHICH ROW IS WHICH, from what the score says of each part once it is playing a moment.
	benchScoreStart(score, from);
	for (int i = 0; i < 64; i++) {
		benchScoreProcess(score, RATE, i);
		APP->engine->stepBlock(1);
	}
	for (int row = 0; row < 4 && (tromboneRow < 0 || pianoRow < 0); row++) {
		uint32_t generation = 0;
		const int slot = benchScoreSlot(score, &generation, row);
		px::BusReader probe;
		probe.add(slot, generation);
		px::Instrument in;
		if (!probe.instrument(in))
			continue;
		std::string named = in.name;
		std::transform(named.begin(), named.end(), named.begin(), [](unsigned char c) { return (char) std::tolower(c); });
		std::printf("  row %d: %s, MIDI %d to %d\n", row, in.name, in.lowest, in.highest);
		const bool trombone = named.find("tromb") != std::string::npos || named.find("posaune") != std::string::npos
			|| (in.lowest >= 34 && in.highest <= 79 && in.highest > 0 && named.find("piano") == std::string::npos);
		if (trombone && tromboneRow < 0)
			tromboneRow = row;
		else if (pianoRow < 0 && in.highest > 0)
			pianoRow = row;
	}
	std::printf("the trombone on row %d, the piano on row %d\n", tromboneRow, pianoRow);
	if (tromboneRow < 0)
		return 1;

	// THE TROMBONE: its player, and a reader of its row.
	px::BrassInstrument::Design design;
	design.rate = RATE;
	px::BrassPlayer player;
	player.setup(design, px::tromboneTable());
	player.setRehearsing(rehearse);
	px::Crew crew(std::max(1, (int) std::thread::hardware_concurrency() - 1));
	player.setCrew(&crew);
	std::unique_ptr<px::BrassEngine> engine;
	if (engineLevel > 0) {
		engine.reset(new px::BrassEngine);
		px::BrassEngine::Setup setup;
		setup.table = &px::tromboneTable();
		setup.rate = RATE;
		setup.fidelity = (px::BrassEngine::Fidelity) std::max(0, std::min(2, engineLevel - 1));
		setup.announcedAhead = px::MPX_ANNOUNCE_AHEAD;
		engine->setup(setup);
	}
	px::BusReader reader;
	{
		uint32_t generation = 0;
		const int slot = benchScoreSlot(score, &generation, tromboneRow);
		reader.add(slot, generation);
	}
	void* piano = nullptr;
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

	const size_t n = (size_t) (std::atof(argv[2]) * RATE);
	std::vector<float> trombone(n, 0.f), pianoL(n, 0.f), pianoR(n, 0.f);
	std::set<int64_t> announced;
	int64_t frame0 = APP->engine->getFrame();
	const auto started = std::chrono::steady_clock::now();
	int notes = 0;
	for (size_t i = 0; i < n; i++) {
		const int64_t frame = APP->engine->getFrame();
		benchScoreProcess(score, RATE, frame);
		// WHAT IS ANNOUNCED: the top note of each group of one moment, at its frame.
		{
			px::Coming c;
			bool have = false;
			px::Coming top;
			auto give = [&]() {
				if (have) {
					if (engine)
						engine->announce(noteOf(top.note), top.frame - frame);
					else
						player.add(noteOf(top.note), std::max<int64_t>(top.frame - frame0, player.clock()));
					notes++;
				}
				have = false;
			};
			while (reader.nextComing(c)) {
				if (c.kind != px::Coming::NOTE)
					continue;
				announced.insert(c.note.handle);
				if (have && c.frame != top.frame)
					give();
				if (!have || c.note.pitch > top.note.pitch) {
					top = c;
					have = true;
				}
			}
			give();
		}
		// WHAT ARRIVES UNANNOUNCED is played now.
		{
			px::Event e;
			bool have = false;
			px::Event top;
			while (reader.next(e)) {
				if (e.kind != px::Event::ON || announced.count(e.handle))
					continue;
				if (!have || e.pitch > top.pitch) {
					top = e;
					have = true;
				}
			}
			if (have) {
				if (engine)
					engine->playNow(noteOf(top));
				else
					player.add(noteOf(top), player.clock());
				notes++;
			}
		}
		if (engine) {
			trombone[i] = engine->process();
			// IN TIME WITH THE CLOCK, a block of 512 at a time, as Rack asks.
			if ((i + 1) % 512 == 0) {
				const auto due = started + std::chrono::duration<double>((double) (i + 1) / RATE);
				std::this_thread::sleep_until(due);
			}
		}
		else
			trombone[i] = (float) player.process();
		if (piano)
			benchPianoProcess(piano, RATE, frame, &pianoL[i], &pianoR[i]);
		APP->engine->stepBlock(1);
		if ((i + 1) % (size_t) (10 * RATE) == 0)
			std::printf("  %3.0f s rendered, %d notes, %.0f s of computing\n", (double) (i + 1) / RATE, notes,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
	}
	// THE BALANCE: the trombone 3 dB over the piano at their loudest.
	std::vector<float> pianoMono(n);
	for (size_t i = 0; i < n; i++)
		pianoMono[i] = 0.5f * (pianoL[i] + pianoR[i]);
	const double t = loudest(trombone), p = piano ? loudest(pianoMono) : 1.0;
	const double gain = t > 0.0 ? std::pow(10.0, 3.0 / 20.0) * p / t : 1.0;
	std::printf("the trombone at %.3f volts a pascal (its loudest 150 ms %.2f Pa, the piano's %.2f V)\n", gain, t, p);
	std::vector<float> left(n), right(n);
	float highest = 0.f;
	for (size_t i = 0; i < n; i++) {
		left[i] = pianoL[i] + (float) gain * trombone[i];
		right[i] = pianoR[i] + (float) gain * trombone[i];
		highest = std::max(highest, std::max(std::fabs(left[i]), std::fabs(right[i])));
	}
	(void) highest;
	// THE LOUDNESS: the loudest three seconds of the two together at 16 dB under ten volts.
	{
		const size_t w = (size_t) (3.0 * RATE);
		double run = 0.0, best = 0.0;
		for (size_t i = 0; i < n; i++) {
			run += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
			if (i >= w)
				run -= 0.5 * ((double) left[i - w] * left[i - w] + (double) right[i - w] * right[i - w]);
			best = std::max(best, run);
		}
		const double loud = std::sqrt(best / (double) std::min(n, w));
		const float g = (float) (10.0 * std::pow(10.0, -16.0 / 20.0) / std::max(1e-9, loud));
		for (size_t i = 0; i < n; i++) {
			left[i] *= g;
			right[i] *= g;
		}
		std::printf("both set %+.1f dB, the loudest three seconds 16 dB under full scale\n", 20.0 * std::log10((double) g));
	}
	// THE LIMITER: the gain each sample needs to keep the next 3 ms under 8 V, taken at once and given back over
	// 60 ms.
	{
		const size_t ahead = (size_t) (0.003 * RATE);
		const double back = std::exp(-1.0 / (0.06 * RATE));
		std::vector<float> need(n, 1.f);
		for (size_t i = 0; i < n; i++) {
			const float peak = std::max(std::fabs(left[i]), std::fabs(right[i]));
			need[i] = peak > 8.f ? 8.f / peak : 1.f;
		}
		// The least gain needed over the next 3 ms, by a running minimum from the end.
		std::vector<float> least(n, 1.f);
		for (size_t i = n; i-- > 0;) {
			float m = need[i];
			for (size_t j = i + 1; j <= i + ahead && j < n; j++)
				m = std::min(m, need[j]);
			least[i] = m;
		}
		double gain = 1.0, deepest = 1.0;
		size_t touched = 0;
		for (size_t i = 0; i < n; i++) {
			gain = std::min((double) least[i], 1.0 - (1.0 - gain) * back);
			if (gain < 0.999)
				touched++;
			deepest = std::min(deepest, gain);
			left[i] = px::mpxSoftClip((float) gain * left[i]);
			right[i] = px::mpxSoftClip((float) gain * right[i]);
		}
		std::printf("the limiter acted on %.1f per cent of the render, at most %.1f dB\n", 100.0 * (double) touched / (double) n,
			-20.0 * std::log10(deepest));
	}
	writeStereo("build/Trombone " + name + ".wav", left, right);
	if (engine) {
		std::printf("through the engine at level %d: %.2f s silent while it was behind, %d notes played untried for want of time\n",
			engineLevel, (double) engine->silent() / RATE, engine->hurried());
		return 0;
	}
	// AND WHAT THE PLAYER DID.
	int joins[5] = {0, 0, 0, 0, 0};
	for (const auto& d : player.decisions())
		joins[d.join]++;
	std::printf("%zu notes played: %d tongued, %d lip slurs, %d natural slurs, %d legato tongues, %d glissandi\n", player.decisions().size(),
		joins[0], joins[1], joins[2], joins[3], joins[4]);
	if (traceFrom >= 0.0) {
		static const char* const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		std::printf("  seconds   level   loudest 5 ms   pitch\n");
		const size_t w = (size_t) (0.05 * RATE), w5 = (size_t) (0.005 * RATE), w40 = (size_t) (0.04 * RATE);
		for (size_t i = (size_t) (traceFrom * RATE); i + w < n && i < (size_t) (traceTo * RATE); i += w) {
			double e = 0.0, loud = 0.0;
			for (size_t j = i; j < i + w; j++)
				e += (double) trombone[j] * trombone[j];
			for (size_t j = i; j + w5 <= i + w; j += w5) {
				double f = 0.0;
				for (size_t q = j; q < j + w5; q++)
					f += (double) trombone[q] * trombone[q];
				loud = std::max(loud, f / (double) w5);
			}
			std::vector<double> x(trombone.begin() + (long) i, trombone.begin() + (long) std::min(n, i + 2 * w40));
			double sure = 0.0;
			const double hz = x.size() >= 2 * w40 ? px::brassPitchOf(x.data(), (int) w40, RATE, 60.0, 900.0, &sure) : 0.0;
			const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
			const int m = (int) std::lround(midi);
			std::printf("  %7.2f   %5.1f   %12.1f   %s%d %+3.0f c, sure %.2f\n", (double) i / RATE, 10.0 * std::log10(e / (double) w / 4e-10),
				10.0 * std::log10(loud / 4e-10), NAME[((m % 12) + 12) % 12], m / 12 - 1, 100.0 * (midi - m), sure);
		}
	}
	if (listing) {
		static const char* const JOIN[5] = {"tongued", "lip slur", "natural slur", "legato tongue", "glissando"};
		static const char* const MARK[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
		static const char* const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		std::printf("  seconds  note  marking  written s  marks         join            position  resonance  mouth Pa  gain dB   heard: cents  level  start's loudest\n");
		for (const auto& d : player.decisions()) {
			// AS HEARD: the middle of the note, from 100 ms in to its end or 400 ms in.
			const size_t a = (size_t) d.start + (size_t) (0.1 * RATE);
			const size_t b = std::min({(size_t) d.end, (size_t) d.start + (size_t) (0.4 * RATE), n});
			double cents = 0.0, levelDb = 0.0, startDb = -100.0;
			if (b > a + (size_t) (0.03 * RATE)) {
				double sure = 0.0, sum = 0.0;
				std::vector<double> x(trombone.begin() + (long) a, trombone.begin() + (long) b);
				const double heard = px::brassPitchOf(x.data(), (int) std::min<size_t>(x.size() / 2, (size_t) (0.04 * RATE)), RATE, 0.55 * d.note.hz,
					1.7 * d.note.hz, &sure);
				cents = 1200.0 * std::log2(heard / d.note.hz);
				for (double v : x)
					sum += v * v;
				levelDb = 10.0 * std::log10(sum / (double) x.size() / (20e-6 * 20e-6));
				const size_t w = (size_t) (0.02 * RATE);
				for (size_t i = (size_t) d.start; i + w < std::min(n, (size_t) d.start + (size_t) (0.15 * RATE)); i += w / 4) {
					double e = 0.0;
					for (size_t j = i; j < i + w; j++)
						e += (double) trombone[j] * trombone[j];
					startDb = std::max(startDb, 10.0 * std::log10(e / (double) w / (20e-6 * 20e-6)));
				}
			}
			const int midi = (int) std::lround(69.0 + 12.0 * std::log2(d.note.hz / 440.0));
			char name[8];
			std::snprintf(name, sizeof name, "%s%d", NAME[midi % 12], midi / 12 - 1);
			std::string marks;
			if (d.note.slurred) marks += "slurred ";
			if (d.note.glissando) marks += "gliss ";
			if (d.note.staccato) marks += "stacc ";
			if (d.note.tenuto) marks += "ten ";
			if (d.note.accent) marks += "accent ";
			if (d.note.heavy) marks += "heavy ";
			const int k = std::max(0, std::min(7, (int) std::lround(7.0 * d.note.level)));
			std::printf("  %7.2f  %-4s  %-3s %.2f  %9.2f  %-12s  %-14s  %8d  %9d  %8.0f  %+7.1f   %+12.0f  %5.1f  %15.1f\n", (double) d.start / RATE, name, MARK[k],
				d.note.level, d.note.seconds, marks.c_str(), JOIN[d.join], d.position, d.resonance, d.mouth, 20.0 * std::log10(d.gain), cents, levelDb,
				startDb);
		}
	}
	return 0;
}
