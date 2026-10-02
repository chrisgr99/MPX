/** The modelled guitar's articulations, from a command line.

Each check plays one thing a hand does to a string through GuitarModel and measures it against the
same string played plainly: a palm mute dies far sooner, a dead note is all but gone, a harmonic
sounds where its node puts it, a hammer-on keeps the string ringing at its new pitch, and a note
ending stops the string. Then it plays a short phrase into build/, to be listened to.

    make guitartest
*/
#include "../src/GuitarModel.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


static const float RATE = 48000.f;
static int failures = 0;


static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-62s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}


static std::vector<float> play(GuitarModel& g, float seconds) {
	std::vector<float> out((size_t) (seconds * RATE));
	for (float& x : out) {
		float l = 0.f, r = 0.f;
		g.process(&l, &r);
		x = l + r;
	}
	return out;
}


static double rms(const std::vector<float>& x, double from, double to) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	double sum = 0.0;
	for (size_t i = a; i < b; i++)
		sum += (double) x[i] * x[i];
	return std::sqrt(sum / (double) (b - a));
}


static double db(double ratio) {
	return 20.0 * std::log10(std::fmax(ratio, 1e-12));
}


static double pitchOf(const std::vector<float>& x, double from, double to, double expect) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	const double period = RATE / expect;
	const int lo = (int) (period * 0.8), hi = (int) (period * 1.2) + 2;
	std::vector<double> r(hi + 2, 0.0);
	for (int lag = lo - 1; lag <= hi + 1; lag++) {
		double sum = 0.0;
		for (size_t i = a; i + lag < b; i++)
			sum += (double) x[i] * x[i + lag];
		r[lag] = sum;
	}
	int best = lo;
	for (int lag = lo; lag <= hi; lag++)
		if (r[lag] > r[best])
			best = lag;
	const double y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
	return RATE / (best + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2));
}


/** THE FUNDAMENTAL'S OWN FREQUENCY, by a Goertzel filter swept across a semitone either side,
rather than the waveform's period: a stiff string's upper partials are sharp, so the waveform no
longer repeats exactly and matching it against itself is pulled off by them. */
static double fundamentalOf(const std::vector<float>& x, double from, double to, double expect) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	double best = -1.0, bestHz = expect;
	for (double c = -100.0; c <= 100.0; c += 0.25) {
		const double hz = expect * std::pow(2.0, c / 1200.0);
		const double w = 2.0 * M_PI * hz / RATE, k = 2.0 * std::cos(w);
		double s1 = 0.0, s2 = 0.0;
		for (size_t i = a; i < b; i++) {
			const double v = x[i] + k * s1 - s2;
			s2 = s1;
			s1 = v;
		}
		const double p = s1 * s1 + s2 * s2 - k * s1 * s2;
		if (p > best) {
			best = p;
			bestHz = hz;
		}
	}
	return bestHz;
}


/** A guitar in standard tuning, its strings' open pitches known, as the instrument on the cable
gives them. String nought is the highest, as the performer counts. */
static GuitarModel guitar() {
	GuitarModel g;
	g.setSampleRate(RATE);
	const int tuning[6] = {64, 59, 55, 50, 45, 40};
	for (int s = 0; s < 6; s++) {
		g.open[s] = (tuning[s] - 60) / 12.f;
		g.openKnown[s] = true;
		g.setPitch(s, g.open[s]);
	}
	// THE STRING ON ITS OWN unless a check asks for the others: sympathy and finger noise are
	// measured by checks of their own, and would otherwise be measured as part of every note.
	g.settings.sympathy = 0.f;
	g.settings.fingerNoise = 0.f;
	return g;
}


/** Volts for a MIDI note. */
static float volts(int midi) {
	return (midi - 60) / 12.f;
}


int main() {
	const int LOW_E = 5, A = 4;

	// THE PLAIN NOTE, the measure of the others: the open A string.
	GuitarModel plain = guitar();
	plain.strike(A, 0.8f, 0, 0);
	const std::vector<float> ref = play(plain, 1.5f);
	const double refLate = rms(ref, 0.4, 0.5);

	// PALM MUTE: the same string, far quieter half a second on.
	{
		GuitarModel g = guitar();
		g.strike(A, 0.8f, GT_PALM_MUTE, 0);
		const std::vector<float> x = play(g, 1.f);
		const double d = db(rms(x, 0.4, 0.5) / refLate);
		check(d < -30.0, "palm mute: against a plain note, at 0.4 s (dB)", d);
		// A light mute: the attack kept.
		const double attack = db(rms(x, 0.0, 0.05) / rms(ref, 0.0, 0.05));
		check(attack > -4.0, "palm mute: its attack against a plain note's, first 50 ms (dB)", attack);
	}

	// DEAD NOTE: the pick and almost nothing after. The string alone: an acoustic body rings on
	// after a dead note's thump, at its air resonance, as a real one does.
	{
		GuitarModel g = guitar(), plainBare = guitar();
		g.settings.body = 0.f;
		plainBare.settings.body = 0.f;
		g.strike(A, 0.8f, GT_DEAD_NOTE, 0);
		plainBare.strike(A, 0.8f, 0, 0);
		const std::vector<float> x = play(g, 1.f), bare = play(plainBare, 1.f);
		const double d = db(rms(x, 0.15, 0.25) / rms(bare, 0.15, 0.25));
		check(d < -60.0, "dead note: against a plain note, at 0.15 s (dB)", d);
	}

	// HARMONICS: at the twelfth fret, an octave over the open string; at the seventh, an octave
	// and a fifth, not the fifth the fret itself would give.
	{
		const struct { int fret; double ratio; const char* what; } cases[] = {
			{12, 2.0, "harmonic: 12th fret on low E is the octave (cents)"},
			{7, 3.0, "harmonic: 7th fret on low E, an octave and a fifth (cents)"},
			{5, 4.0, "harmonic: 5th fret on low E, two octaves (cents)"},
		};
		for (const auto& c : cases) {
			GuitarModel g = guitar();
			g.setPitch(LOW_E, volts(40 + c.fret));
			g.strike(LOW_E, 0.8f, GT_HARMONIC, c.fret);
			const std::vector<float> x = play(g, 1.f);
			const double want = 82.4069 * c.ratio;
			const double got = 1200.0 * std::log2(fundamentalOf(x, 0.3, 0.9, want) / want);
			check(std::fabs(got) < 3.0, c.what, got);
		}
	}

	// HAMMER-ON: the string keeps ringing, at the new pitch, with only a knock put in.
	{
		GuitarModel g = guitar();
		g.strike(A, 0.8f, 0, 0);
		std::vector<float> x = play(g, 0.5f);
		const double before = rms(x, 0.4, 0.5);
		g.setPitch(A, volts(47));                 // two frets up
		g.legato(A, 0.8f * 0.85f, GT_HAMMER_ON);
		const std::vector<float> y = play(g, 0.5f);
		x.insert(x.end(), y.begin(), y.end());
		const double after = rms(x, 0.55, 0.65);
		check(std::fabs(db(after / before)) < 6.0,
			"hammer-on: the string rings on, not struck again (dB)", db(after / before));
		const double got = 1200.0 * std::log2(fundamentalOf(x, 0.55, 1.0, 123.47) / 123.47);
		check(std::fabs(got) < 3.0, "hammer-on: two frets up, in tune (cents)", got);
	}

	// A NOTE ENDING: the hand comes down and the string stops.
	{
		GuitarModel g = guitar();
		g.strike(A, 0.8f, 0, 0);
		std::vector<float> x = play(g, 0.3f);
		g.end(A);
		const std::vector<float> y = play(g, 0.5f);
		x.insert(x.end(), y.begin(), y.end());
		const double d = db(rms(x, 0.55, 0.65) / rms(ref, 0.55, 0.65));
		check(d < -40.0, "note end: against a ringing note, 0.25 s after (dB)", d);
	}

	// A RESTRUCK STRING after it has been damped rings fully again.
	{
		GuitarModel g = guitar();
		g.strike(A, 0.8f, 0, 0);
		play(g, 0.2f);
		g.end(A);
		play(g, 0.2f);
		g.strike(A, 0.8f, 0, 0);
		const std::vector<float> x = play(g, 0.6f);
		const double d = db(rms(x, 0.4, 0.5) / refLate);
		check(std::fabs(d) < 3.0, "restrike: rings like a fresh note after a damp (dB)", d);
	}

	// ---- sympathy and the finger ----

	// SYMPATHY: an A played on the low E's fifth fret sets the open A string ringing, well below
	// the note; with sympathy off, the A string stays silent. Measured on the A string itself.
	{
		double ringing[2];
		for (int on = 0; on < 2; on++) {
			GuitarModel g = guitar();
			g.settings.sympathy = on ? 0.5f : 0.f;
			g.setPitch(LOW_E, volts(45));
			g.strike(LOW_E, 0.8f, 0, 5);
			double a = 0.0, e = 0.0;
			int n = 0;
			for (int i = 0; i < (int) (2.f * RATE); i++) {
				float l = 0.f, r = 0.f;
				g.process(&l, &r);
				if (i > (int) (1.f * RATE)) {
					a += (double) g.strings[A].bridge() * g.strings[A].bridge();
					e += (double) g.strings[LOW_E].bridge() * g.strings[LOW_E].bridge();
					n++;
				}
			}
			ringing[on] = db(std::sqrt(a / n) / std::sqrt(e / n));
		}
		check(ringing[1] > -50.0 && ringing[1] < -20.0,
			"sympathy: the open A against the A played on the low E (dB)", ringing[1]);
		check(ringing[0] < -100.0, "sympathy: off, the open A is silent (dB)", ringing[0]);
	}

	// SYMPATHY STAYS IN BOUNDS: a whole chord left ringing with every free string listening.
	{
		GuitarModel g = guitar();
		g.settings.sympathy = 0.5f;
		const int chord[3] = {64, 59, 55};
		for (int k = 0; k < 3; k++) {
			g.setPitch(k, volts(chord[k]));
			g.strike(k, 1.f, 0, 0);
		}
		const std::vector<float> x = play(g, 20.f);
		float peak = 0.f, tail = 0.f;
		for (size_t i = 0; i < x.size(); i++) {
			peak = std::fmax(peak, std::fabs(x[i]));
			if (i > x.size() - (size_t) RATE)
				tail = std::fmax(tail, std::fabs(x[i]));
		}
		// Bounded, not quiet: three strings struck as hard as they go peak near four together.
		check(std::isfinite(peak) && peak < 6.f && tail < 0.05f,
			"sympathy: twenty seconds of a ringing chord stays bounded (tail peak)", tail);
	}

	// FINGER NOISE: an octave's slide on a wound string squeaks; the same slide with finger noise
	// off, and a bend the same distance, do not. Measured as energy around 2.8 kHz while moving.
	{
		auto band = [&](bool noise, uint32_t technique) {
			GuitarModel g = guitar();
			g.settings.fingerNoise = noise ? 0.5f : 0.f;
			g.setPitch(A, volts(45));
			g.strike(A, 0.8f, technique, 0);
			std::vector<float> x;
			for (int i = 0; i < (int) (0.8f * RATE); i++) {
				const float t = i / RATE;
				const float semis = (t < 0.3f) ? 0.f : (t < 0.6f ? 12.f * (t - 0.3f) / 0.3f : 12.f);
				g.setPitch(A, volts(45) + semis / 12.f);
				float l = 0.f, r = 0.f;
				g.process(&l, &r);
				x.push_back(l + r);
			}
			// THE SQUEAK'S OWN BAND: a band-pass at 2.8 kHz, an octave wide.
			const double bg = std::tan(M_PI * 2800.0 / RATE), k = 1.0 / 1.4;
			const double a1 = 1.0 / (1.0 + bg * (bg + k)), a2 = bg * a1, a3 = bg * a2;
			double ic1 = 0.0, ic2 = 0.0, sum = 0.0;
			for (size_t i = 0; i < (size_t) (0.55f * RATE); i++) {
				const double v3 = x[i] - ic2;
				const double v1 = a1 * ic1 + a2 * v3;
				const double v2 = ic2 + a2 * ic1 + a3 * v3;
				ic1 = 2.0 * v1 - ic1;
				ic2 = 2.0 * v2 - ic2;
				if (i > (size_t) (0.35f * RATE))
					sum += v1 * v1;
			}
			return std::sqrt(sum);
		};
		const double slide = band(true, GT_SLIDE_OUT_UP), quiet = band(false, GT_SLIDE_OUT_UP);
		const double bend = band(true, 0);
		check(db(slide / quiet) > 3.0, "finger: a slide on a wound string squeaks (dB over none)",
			db(slide / quiet));
		check(std::fabs(db(bend / quiet)) < 1.0, "finger: a bend the same distance does not (dB)",
			db(bend / quiet));
	}

	// ---- the body ----

	// A SINE THROUGH THE BODY: loud at the air resonance, quieter between the modes.
	{
		auto through = [&](float hz) {
			GuitarBody b;
			b.setSampleRate(RATE);
			double sum = 0.0;
			int n = 0;
			for (int i = 0; i < (int) RATE; i++) {
				const float y = b.process(std::sin(2.f * 3.14159265f * hz * i / RATE), 1.f);
				if (i > (int) (RATE / 2)) {
					sum += (double) y * y;
					n++;
				}
			}
			return std::sqrt(sum / n);
		};
		const double d = db(through(102.f) / through(145.f));
		check(d > 6.0, "body: the air resonance over the gap above it (dB)", d);
		const double top = db(through(195.f) / through(300.f));
		check(top > 3.0, "body: the top's first mode over the gap above it (dB)", top);
	}

	// BODY AT NOUGHT is the bare strings, sample for sample: nothing at all done to them.
	{
		GuitarBody b;
		b.setSampleRate(RATE);
		double diff = 0.0;
		uint32_t n = 1u;
		for (int i = 0; i < 48000; i++) {
			n ^= n << 13; n ^= n >> 17; n ^= n << 5;
			const float in = (float) n / 2147483648.f - 1.f;
			diff = std::fmax(diff, std::fabs(b.process(in, 0.f) - in));
		}
		check(diff < 1e-9, "body: at nought, the bare strings (largest difference)", diff);
	}

	// THE FULL BODY is about as loud as the bare strings, so turning it up is not a volume knob.
	{
		GuitarModel a = guitar(), b = guitar();
		a.settings.body = 0.f;
		b.settings.body = 1.f;
		const int chord[6] = {64, 59, 56, 52, 47, 40};
		for (int k = 0; k < 6; k++) {
			a.setPitch(k, volts(chord[k]));
			b.setPitch(k, volts(chord[k]));
			a.strike(k, 0.6f, 0, 0);
			b.strike(k, 0.6f, 0, 0);
		}
		const std::vector<float> x = play(a, 1.f), y = play(b, 1.f);
		const double d = db(rms(y, 0.05, 1.0) / rms(x, 0.05, 1.0));
		check(std::fabs(d) < 3.0, "body: full against bare, a strummed chord (dB)", d);
	}

	// NYLON IS ROUNDER THAN STEEL: half a second after the same pick, its upper partials stand
	// well below the steel-string's against the fundamental.
	{
		auto upper = [&](bool nylon) {
			GuitarModel g = guitar();
			g.settings.nylon = nylon;
			g.strike(A, 0.8f, 0, 0);
			const std::vector<float> x = play(g, 0.8f);
			auto at = [&](int n) {
				const double w = 2.0 * M_PI * 110.0 * n / RATE, c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				for (size_t i = (size_t) (0.5 * RATE); i < x.size(); i++) {
					const double v = x[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::sqrt(std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
			};
			return db((at(8) + at(9) + at(11)) / at(1));
		};
		const double d = upper(false) - upper(true);
		check(d > 6.0, "nylon: upper partials under steel's, half a second on (dB)", d);
	}

	// ---- the electric ----

	// THE PICKUP HEARS A POINT: at the neck, a quarter of the way along, it cannot hear the
	// fourth partial, which has a node there; at the bridge it can.
	{
		auto fourth = [&](float pickup) {
			GuitarModel g = guitar();
			g.settings.electric = true;
			g.settings.body = pickup;
			g.strike(A, 0.3f, 0, 0);
			const std::vector<float> x = play(g, 0.6f);
			const double w1 = 2.0 * M_PI * 110.0 / RATE, w4 = 4.0 * w1;
			auto at = [&](double w) {
				const double c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				for (size_t i = (size_t) (0.1 * RATE); i < (size_t) (0.5 * RATE); i++) {
					const double v = x[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::sqrt(std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
			};
			return db(at(w4) / at(w1));
		};
		const double neck = fourth(1.f), bridge = fourth(0.f);
		check(neck < bridge - 15.0, "pickup: the 4th partial at the neck under the bridge (dB)",
			neck - bridge);
	}

	// THE BRIDGE PICKUP IS THINNER AND BRIGHTER than the neck: its partials from the third to the
	// twelfth stand higher against its first two. Measured on the partials themselves; an
	// average frequency was tried and is ruled by the 2 to 4 kHz region, where both pickups pass
	// about as much, so it barely moved.
	{
		auto bright = [&](float pickup) {
			GuitarModel g = guitar();
			g.settings.electric = true;
			g.settings.body = pickup;
			g.strike(LOW_E, 0.3f, 0, 0);
			const std::vector<float> x = play(g, 0.5f);
			auto power = [&](int n) {
				const double w = 2.0 * M_PI * 82.41 * n / RATE, c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				for (size_t i = (size_t) (0.05 * RATE); i < x.size(); i++) {
					const double v = x[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0);
			};
			double low = 0.0, upper = 0.0;
			for (int n = 1; n <= 2; n++)
				low += power(n);
			for (int n = 3; n <= 12; n++)
				upper += power(n);
			return 10.0 * std::log10(upper / low);
		};
		const double d = bright(0.f) - bright(1.f);
		check(d > 6.0, "pickup: bridge over neck, upper partials against the lowest (dB)", d);
	}

	// THE AMPLIFIER: a quiet sine comes through at its own level and nearly pure; a loud one is
	// rounded, with a clear third harmonic.
	{
		// Flat tone stack and no drive, so what is measured is the saturation alone.
		AmpSettings flat;
		flat.drive = 0.f;
		flat.bass = flat.middle = flat.treble = 0.f;
		auto third = [&](float amplitude, double* gain) {
			GuitarAmp a;
			a.setSampleRate(RATE);
			std::vector<float> y;
			for (int i = 0; i < (int) RATE; i++)
				y.push_back(a.process(amplitude * std::sin(2.f * 3.14159265f * 500.f * i / RATE),
					flat));
			auto at = [&](double hz) {
				const double w = 2.0 * M_PI * hz / RATE, c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				for (size_t i = (size_t) (0.5 * RATE); i < y.size(); i++) {
					const double v = y[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::sqrt(std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0))
					/ (0.25 * RATE);
			};
			*gain = db(at(500.0) / amplitude);
			return db(at(1500.0) / at(500.0));
		};
		double quietGain = 0.0, loudGain = 0.0;
		const double quiet = third(0.05f, &quietGain), loud = third(2.f, &loudGain);
		check(std::fabs(quietGain) < 1.0, "amp: played lightly, at its own level (dB)", quietGain);
		check(quiet < -50.0, "amp: played lightly, nearly pure (3rd harmonic, dB)", quiet);
		check(loud > -30.0, "amp: played hard, broken up (3rd harmonic, dB)", loud);
	}

	// THE TONE STACK as it comes: the middle scooped under the bass and the treble.
	{
		auto through = [&](float hz) {
			GuitarAmp a;
			a.setSampleRate(RATE);
			AmpSettings s;
			s.drive = 0.f;
			double sum = 0.0;
			int n = 0;
			for (int i = 0; i < (int) RATE; i++) {
				const float y = a.process(0.05f * std::sin(2.f * 3.14159265f * hz * i / RATE), s);
				if (i > (int) (RATE / 2)) {
					sum += (double) y * y;
					n++;
				}
			}
			return db(std::sqrt(sum / n) / (0.05 * 0.7071));
		};
		const double bass = through(110.f), middle = through(500.f), treble = through(3000.f);
		check(middle < bass - 5.0 && middle < treble - 5.0,
			"amp: the tone stack scoops the middle (dB under the lower of bass and treble)",
			std::fmin(bass, treble) - middle);
	}

	// THE DRIVE KNOB: the same moderate signal, clean at nought and broken up at full.
	{
		auto third = [&](float drive) {
			GuitarAmp a;
			a.setSampleRate(RATE);
			AmpSettings s;
			s.drive = drive;
			s.bass = s.middle = s.treble = 0.f;
			std::vector<float> y;
			for (int i = 0; i < (int) RATE; i++)
				y.push_back(a.process(0.3f * std::sin(2.f * 3.14159265f * 300.f * i / RATE), s));
			auto at = [&](double hz) {
				const double w = 2.0 * M_PI * hz / RATE, c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				for (size_t i = (size_t) (0.5 * RATE); i < y.size(); i++) {
					const double v = y[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::sqrt(std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
			};
			return db(at(900.0) / at(300.0));
		};
		const double clean = third(0.f), driven = third(1.f);
		check(driven > clean + 15.0, "amp: full drive breaks up a signal nought leaves clean (dB)",
			driven - clean);
	}

	// THE GUITAR'S TONE KNOB: turned down, the upper partials go.
	{
		auto top = [&](float tone) {
			GuitarModel g = guitar();
			g.settings.electric = true;
			g.settings.tone = tone;
			// A plectrum, so there are upper partials for the tone knob to take away: a
			// fingertip's are too faint to measure against.
			g.settings.hardness = 1.f;
			g.strike(A, 0.3f, 0, 0);
			const std::vector<float> x = play(g, 0.06f);
			auto at = [&](int n) {
				const double w = 2.0 * M_PI * 110.0 * n / RATE, c = 2.0 * std::cos(w);
				double s1 = 0.0, s2 = 0.0;
				// HANN-WINDOWED: the upper partials of a string's own pluck are thirty to forty
				// decibels under the fundamental, and an unwindowed measurement smears that much
				// of the fundamental across them.
				const size_t a0 = (size_t) (0.01 * RATE), span = x.size() - a0;
				for (size_t i = a0; i < x.size(); i++) {
					const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) (i - a0) / (span - 1));
					const double v = hann * x[i] + c * s1 - s2;
					s2 = s1;
					s1 = v;
				}
				return std::sqrt(std::fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
			};
			return db(at(12) / at(2));
		};
		// IN THE FIRST FIFTY MILLISECONDS, and the 12th partial: a pluck shaped as a string's own
		// has little in its upper partials, and the strings' losses take what there is within a
		// few tenths of a second, so later and higher there is nothing left to measure.
		const double d = top(1.f) - top(0.f);
		check(d > 6.0, "tone: open against closed, the 12th partial against the 2nd (dB)", d);
	}

	// THE ELECTRIC IS ABOUT AS LOUD AS THE ACOUSTIC, so the switch is not a volume change.
	{
		GuitarModel a = guitar(), b = guitar();
		b.settings.electric = true;
		b.settings.body = 0.5f;
		const int chord[6] = {64, 59, 56, 52, 47, 40};
		for (int k = 0; k < 6; k++) {
			a.setPitch(k, volts(chord[k]));
			b.setPitch(k, volts(chord[k]));
			a.strike(k, 0.6f, 0, 0);
			b.strike(k, 0.6f, 0, 0);
		}
		const std::vector<float> x = play(a, 1.f), y = play(b, 1.f);
		const double d = db(rms(y, 0.05, 1.0) / rms(x, 0.05, 1.0));
		check(std::fabs(d) < 6.0, "electric: against the acoustic, a strummed chord (dB)", d);
	}

	// ---- the taper and the fretting finger ----
	{
		// How far a note falls between its first moments and a second later, in decibels.
		auto fall = [&](float taper, float fretDamping, int string, int midi, int fret) {
			GuitarModel g = guitar();
			g.settings.taper = taper;
			g.settings.fretDamping = fretDamping;
			g.settings.sustain = 4.f;
			g.setPitch(string, volts(midi));
			g.strike(string, 0.8f, 0, fret);
			const std::vector<float> x = play(g, 1.3f);
			return db(rms(x, 1.1, 1.3) / rms(x, 0.05, 0.25));
		};
		const int HIGH_E = 0, D = 3;
		const double lowFlat = fall(0.f, 0.f, LOW_E, 40, 0), highFlat = fall(0.f, 0.f, HIGH_E, 64, 0);
		const double lowTaper = fall(1.f, 0.f, LOW_E, 40, 0), highTaper = fall(1.f, 0.f, HIGH_E, 64, 0);
		check(std::fabs(lowTaper - lowFlat) < 1.0, "taper: the open low E rings as Sustain says",
			lowTaper - lowFlat);
		check(highTaper < highFlat - 6.0, "taper: the open high E falls further in a second, dB",
			highTaper - highFlat);
		const double open = fall(0.f, 1.f, D, 50, 0), fretted = fall(0.f, 1.f, A, 50, 5);
		const double openPlain = fall(0.f, 0.f, D, 50, 0), frettedPlain = fall(0.f, 0.f, A, 50, 5);
		check(fretted < open - 3.0, "fret damping: D fretted on the A string falls further than"
			" the open D, dB", fretted - open);
		check(std::fabs(frettedPlain - openPlain) < 2.0, "fret damping at nought: the two alike, dB",
			frettedPlain - openPlain);
	}

	// ---- every knob at its exaggerated top ----
	for (int kind = 0; kind < 3; kind++) {
		GuitarModel g = guitar();
		g.settings.nylon = kind == 0;
		g.settings.electric = kind == 2;
		g.settings.sustain = 20.f;
		g.settings.hardness = 1.5f;
		g.settings.stiffness = 1.5f;
		g.settings.sympathy = 1.5f;
		g.settings.fingerNoise = 1.5f;
		g.settings.taper = 1.5f;
		g.settings.fretDamping = 1.5f;
		g.settings.body = kind == 2 ? 1.f : 1.5f;
		g.settings.tone = 0.f;
		g.settings.amp.drive = 1.f;
		g.settings.amp.bass = g.settings.amp.middle = g.settings.amp.treble = 18.f;
		g.settings.pickPosition = 0.02f;
		const int chord[6] = {64, 59, 56, 52, 47, 40};
		for (int k = 5; k >= 0; k--) {
			g.setPitch(k, volts(chord[k]));
			g.strike(k, 1.f, 0, k == 0 ? 0 : 2);
		}
		const std::vector<float> x = play(g, 8.f);
		float peak = 0.f;
		bool finite = true;
		for (float v : x) {
			finite = finite && std::isfinite(v);
			peak = std::fmax(peak, std::fabs(v));
		}
		const double late = rms(x, 7.f, 8.f), early = rms(x, 0.1f, 1.1f);
		const char* names[3] = {"nylon: every knob at its top, finite, bounded and dying away",
			"steel: every knob at its top, finite, bounded and dying away",
			"electric: every knob at its top, finite, bounded and dying away"};
		// The electric with Bass, Middle and Treble all at +18 dB adds the three boosts together,
		// about twenty-two times its plain level, so its bound is wider.
		check(finite && peak < (kind == 2 ? 30.f : 20.f) && late < 0.5 * early, names[kind], peak);
		std::printf("      the last second against the first, dB: %g\n", db(late / early));
	}

	// ---- to listen to: a short phrase with each articulation in it ----
	for (int bodied = 0; bodied < 4; bodied++) {
		GuitarModel g = guitar();
		g.settings.body = (bodied == 1 || bodied == 3) ? 1.f : (bodied == 2 ? 0.3f : 0.f);
		g.settings.electric = bodied == 2;
		g.settings.nylon = bodied == 3;
		g.settings.sympathy = 0.5f;
		g.settings.fingerNoise = 0.5f;
		std::vector<float> l, r;
		auto run = [&](float seconds) {
			for (int i = 0; i < (int) (seconds * RATE); i++) {
				float a = 0.f, b = 0.f;
				g.process(&a, &b);
				l.push_back(a * 0.6f);
				r.push_back(b * 0.6f);
			}
		};
		// An open E chord, strummed.
		const int chord[6] = {64, 59, 56, 52, 47, 40};
		for (int k = 5; k >= 0; k--) {
			g.setPitch(k, volts(chord[k]));
			g.strike(k, 0.6f, 0, 0);
			run(0.012f);
		}
		run(1.5f);
		for (int k = 0; k < 6; k++)
			g.end(k);
		run(0.3f);
		// Palm-muted low E, eight times.
		for (int i = 0; i < 8; i++) {
			g.setPitch(LOW_E, volts(40));
			g.strike(LOW_E, 0.8f, GT_PALM_MUTE, 0);
			run(0.15f);
		}
		run(0.3f);
		// A, hammer on to B, pull off to A.
		g.setPitch(A, volts(45));
		g.strike(A, 0.8f, 0, 0);
		run(0.4f);
		g.setPitch(A, volts(47));
		g.legato(A, 0.7f, GT_HAMMER_ON);
		run(0.4f);
		g.setPitch(A, volts(45));
		g.legato(A, 0.6f, GT_PULL_OFF);
		run(0.8f);
		g.end(A);
		run(0.3f);
		// A slide up the A string from the 2nd fret to the 9th, and back.
		g.setPitch(A, volts(47));
		g.strike(A, 0.8f, GT_SLIDE_OUT_UP, 2);
		run(0.3f);
		for (int i = 0; i < (int) (0.25f * RATE); i++) {
			g.setPitch(A, volts(47) + 7.f / 12.f * i / (0.25f * RATE));
			run(1.f / RATE);
		}
		run(0.5f);
		g.legato(A, 0.7f, GT_SLIDE_IN_ABOVE);
		for (int i = 0; i < (int) (0.25f * RATE); i++) {
			g.setPitch(A, volts(54) - 7.f / 12.f * i / (0.25f * RATE));
			run(1.f / RATE);
		}
		run(0.6f);
		g.end(A);
		run(0.3f);
		// Dead notes, then harmonics at the 12th, 7th and 5th frets.
		for (int i = 0; i < 4; i++) {
			g.strike(A, 0.8f, GT_DEAD_NOTE, 0);
			run(0.12f);
		}
		run(0.2f);
		for (int fret : {12, 7, 5}) {
			g.setPitch(LOW_E, volts(40 + fret));
			g.strike(LOW_E, 0.7f, GT_HARMONIC, fret);
			run(0.9f);
		}
		run(1.f);

		const char* path = bodied == 3 ? "build/guitar-phrase-nylon.wav"
			: bodied == 2 ? "build/guitar-phrase-electric.wav"
			: (bodied ? "build/guitar-phrase-body.wav" : "build/guitar-phrase-bare.wav");
		FILE* f = std::fopen(path, "wb");
		if (f) {
			const uint32_t frames = (uint32_t) l.size(), bytes = frames * 4;
			auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
			auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
			std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
			u32(16); u16(1); u16(2); u32((uint32_t) RATE); u32((uint32_t) RATE * 4); u16(4);
			u16(16); std::fwrite("data", 1, 4, f); u32(bytes);
			for (uint32_t i = 0; i < frames; i++) {
				const int16_t a = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, l[i])) * 32000.f);
				const int16_t b = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, r[i])) * 32000.f);
				std::fwrite(&a, 2, 1, f);
				std::fwrite(&b, 2, 1, f);
			}
			std::fclose(f);
			std::printf("wrote %s\n", path);
		}
	}

	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
