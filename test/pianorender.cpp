/** PLAYS THE MODELLED PIANO AND WRITES IT TO A FILE, so it can be listened to.

	make pianorender

WHY A RENDERER AND NOT JUST THE MODULE. Two reasons, and the second is the better one. A sound can
be heard here without opening a rack, which is quicker; and the path from a note to a blow — a key
number, a hammer speed, a damper let down on release — is exercised outside Rack, where a fault in
it is a thing to read rather than a silence to guess at. The bench measures the instrument; this
listens to it.

IT PLAYS THE SAME ARITHMETIC THE MODULE PLAYS. The mapping from level to hammer speed and the
output scaling are the module's own numbers, written here once more rather than shared, because
sharing them would mean this file reaching into a Rack module and it builds without Rack. If they
drift apart the sound here stops matching the sound there, which is a thing to watch. */
#include "../src/PianoBoard.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace px;

static const double SR = 48000.0;

/** The module's own mapping, and the module's own scaling. */
static float speedOf(float level) {
	return 0.2f * std::pow(30.f, level < 0.f ? 0.f : (level > 1.f ? 1.f : level));
}
static const double OUT_SCALE = 0.085;


/** A sixteen-bit stereo WAV, which is the one format everything can open. The same signal in both
channels: the instrument has one bridge and no soundboard yet, and phase 6 is where a piano stops
being a point and starts being a room. */
static bool writeWav(const std::string& path, const std::vector<float>& x) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	const uint32_t frames = (uint32_t) x.size();
	const uint32_t dataBytes = frames * 2u * 2u;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + dataBytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);                       // PCM
	u16(2);                       // stereo
	u32((uint32_t) SR);
	u32((uint32_t) SR * 2u * 2u); // bytes a second
	u16(4);                       // bytes a frame
	u16(16);
	std::fwrite("data", 1, 4, f);
	u32(dataBytes);
	for (float v : x) {
		// Ten volts is the top of what a Rack module sends, so that is what full scale means here.
		const double clamped = (v > 10.f) ? 10.f : ((v < -10.f) ? -10.f : v);
		const int16_t s = (int16_t) std::lround(clamped * 3276.7);
		u16((uint16_t) s);
		u16((uint16_t) s);
	}
	std::fclose(f);
	return true;
}


/** One thing to play: when, which key, how hard, and how long the key is held. */
struct Press {
	double at;
	int midi;
	float level;
	double held;
};

/** A key pressed too slowly for the hammer to reach the string: the damper lifts and nothing
sounds. A thing a pianist does and a sample set cannot. */
struct Hold {
	double at;
	int midi;
	double held;
};

struct Piece {
	const char* name;
	std::vector<Press> presses;
	std::vector<Hold> holds;
	std::vector<std::pair<double, float> > sustain;   // when, how far down
	std::vector<std::pair<double, float> > soft;
	double seconds;
};


static void render(const Piece& piece) {
	PianoBoard board;
	board.setSampleRate((float) SR);
	board.silence();

	// Everything that has to happen, in the order it happens.
	struct Moment { double at; int what; int midi; float level; };
	std::vector<Moment> moments;
	for (const Press& p : piece.presses) {
		moments.push_back(Moment{p.at, 0, p.midi, p.level});
		moments.push_back(Moment{p.at + p.held, 1, p.midi, 0.f});
	}
	for (const Hold& h : piece.holds) {
		moments.push_back(Moment{h.at, 4, h.midi, 0.f});
		moments.push_back(Moment{h.at + h.held, 1, h.midi, 0.f});
	}
	for (const auto& s : piece.sustain)
		moments.push_back(Moment{s.first, 2, 0, s.second});
	for (const auto& s : piece.soft)
		moments.push_back(Moment{s.first, 3, 0, s.second});
	std::sort(moments.begin(), moments.end(),
		[](const Moment& a, const Moment& b) { return a.at < b.at; });

	std::vector<float> out((size_t) (piece.seconds * SR));
	size_t next = 0;
	double peak = 0.0;
	for (size_t i = 0; i < out.size(); i++) {
		const double now = (double) i / SR;
		while (next < moments.size() && moments[next].at <= now) {
			const Moment& m = moments[next++];
			if (m.what == 0)
				board.hit(m.midi, speedOf(m.level));
			else if (m.what == 1)
				board.release(m.midi);
			else if (m.what == 2)
				board.setSustain(m.level);
			else if (m.what == 3)
				board.setSoft(m.level);
			else
				board.hold(m.midi);
		}
		const double v = (double) board.process() * OUT_SCALE;
		out[i] = (float) v;
		peak = std::max(peak, std::fabs(v));
	}

	// WHAT A LISTENER SHOULD NOT HAVE TO FIND: a direct offset, which wastes headroom and thumps
	// when a note starts, and anything that is not a number at all.
	double sum = 0.0;
	int notNumbers = 0;
	for (float v : out) {
		if (!std::isfinite(v))
			notNumbers++;
		else
			sum += (double) v;
	}
	const double offset = sum / (double) out.size();

	const std::string path = std::string("build/") + piece.name + ".wav";
	if (!writeWav(path, out)) {
		std::printf("  could not write %s\n", path.c_str());
		return;
	}
	std::printf("  %-28s %6.1f s   peak %5.2f V   offset %+.2e %s%s\n", piece.name,
		piece.seconds, peak, offset, notNumbers ? "NOT A NUMBER " : "", path.c_str());
}


int main() {
	std::printf("RENDERING THE MODELLED PIANO. Write-ups are in docs/piano-model.md; these are\n");
	std::printf("for listening to.\n\n");

	std::vector<Piece> pieces;

	// ONE NOTE AT FIVE LEVELS, which is the thing to listen to first: a piano brightens as it is
	// played harder and does not merely get louder, and that is the hammer's doing.
	{
		Piece p;
		p.name = "one-note-at-five-levels";
		for (int i = 0; i < 5; i++)
			p.presses.push_back(Press{i * 1.6, 60, 0.1f + 0.22f * i, 1.2});
		p.seconds = 9.5;
		pieces.push_back(p);
	}

	// THE COMPASS, so the bass and the treble can be heard against each other.
	{
		Piece p;
		p.name = "up-the-keyboard";
		const int keys[8] = {21, 33, 45, 57, 69, 81, 93, 105};
		for (int i = 0; i < 8; i++)
			p.presses.push_back(Press{i * 1.1, keys[i], 0.6f, 0.9});
		p.seconds = 12.0;
		pieces.push_back(p);
	}

	// A CHORD, HELD AND THEN RELEASED, with and without the pedal, so the dampers can be heard
	// arriving — and heard not arriving.
	{
		Piece p;
		p.name = "chord-released-then-pedalled";
		const int chord[4] = {48, 55, 60, 64};
		for (int k = 0; k < 4; k++)
			p.presses.push_back(Press{0.0 + 0.02 * k, chord[k], 0.65f, 1.5});
		for (int k = 0; k < 4; k++)
			p.presses.push_back(Press{4.0 + 0.02 * k, chord[k], 0.65f, 1.5});
		p.sustain.push_back(std::make_pair(3.8, 1.f));
		p.seconds = 12.0;
		pieces.push_back(p);
	}

	// HALF PEDAL, the thing a switch cannot do: the same chord let go under four positions.
	{
		Piece p;
		p.name = "the-pedal-by-quarters";
		for (int j = 0; j < 4; j++) {
			const double at = j * 2.6;
			p.sustain.push_back(std::make_pair(at - 0.1, j / 3.f));
			p.presses.push_back(Press{at, 64, 0.6f, 0.8});
			p.presses.push_back(Press{at + 0.01, 71, 0.6f, 0.8});
		}
		p.seconds = 12.0;
		pieces.push_back(p);
	}

	// THE SILENT CHORD, which is the thing this instrument can do that a sample set cannot: hold
	// three keys without sounding them, strike a bass note, and let the bass note go.
	{
		Piece p;
		p.name = "a-chord-held-silently";
		// THREE KEYS PRESSED AND NOTHING SOUNDS. Then a bass note is struck and let go, and what
		// is left is the chord, which was never played.
		const int chord[3] = {60, 64, 67};
		for (int k = 0; k < 3; k++)
			p.holds.push_back(Hold{0.2, chord[k], 9.0});
		p.presses.push_back(Press{1.0, 36, 0.95f, 0.8});
		p.seconds = 10.0;
		pieces.push_back(p);
	}

	// THE SOFT PEDAL, the same phrase twice.
	{
		Piece p;
		p.name = "the-soft-pedal";
		for (int j = 0; j < 2; j++) {
			const double at = j * 3.0;
			p.soft.push_back(std::make_pair(at - 0.2, j == 0 ? 0.f : 1.f));
			const int notes[4] = {60, 64, 67, 72};
			for (int k = 0; k < 4; k++)
				p.presses.push_back(Press{at + 0.4 * k, notes[k], 0.7f, 0.35});
		}
		p.seconds = 8.0;
		pieces.push_back(p);
	}

	// A FEW BARS, so it can be heard as music rather than as measurements.
	{
		Piece p;
		p.name = "a-few-bars";
		const int melody[12] = {72, 71, 67, 64, 67, 71, 72, 76, 74, 71, 67, 64};
		const int bass[6] = {36, 43, 36, 41, 36, 40};
		for (int i = 0; i < 12; i++)
			p.presses.push_back(Press{0.5 + 0.42 * i, melody[i], 0.55f + 0.1f * (i % 3), 0.38});
		for (int i = 0; i < 6; i++)
			p.presses.push_back(Press{0.5 + 0.84 * i, bass[i], 0.5f, 0.8});
		p.sustain.push_back(std::make_pair(0.4, 1.f));
		p.sustain.push_back(std::make_pair(2.9, 0.f));
		p.sustain.push_back(std::make_pair(3.0, 1.f));
		p.sustain.push_back(std::make_pair(5.4, 0.f));
		p.sustain.push_back(std::make_pair(5.5, 1.f));
		p.seconds = 12.0;
		pieces.push_back(p);
	}

	for (const Piece& p : pieces)
		render(p);

	std::printf("\nThe level mapping and the output scaling are the module's own numbers.\n");
	return 0;
}
