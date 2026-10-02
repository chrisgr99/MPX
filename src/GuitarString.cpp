/** See GuitarString.hpp. */
#include "GuitarString.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const float PI = 3.14159265358979f;


GuitarString::GuitarString() {}


void GuitarString::setSampleRate(float r) {
	if (r > 0.f && r != rate) {
		rate = r;
		dirty = true;
	}
}


void GuitarString::setFrequency(float hz) {
	hz = std::fmax(20.f, std::fmin(hz, rate * 0.25f));
	if (hz != freq) {
		freq = hz;
		dirty = true;
	}
}


void GuitarString::setDecay(float seconds) {
	seconds = std::fmax(0.01f, seconds);
	if (seconds != decay) {
		decay = seconds;
		dirty = true;
	}
}


void GuitarString::setDamping(float amount) {
	amount = std::fmax(0.f, std::fmin(1.f, amount));
	if (amount != damping) {
		damping = amount;
		dirty = true;
	}
}


void GuitarString::setStiffness(float amount) {
	// Up to half again past a real steel string, for hearing plainly what stiffness does.
	amount = std::fmax(0.f, std::fmin(1.5f, amount));
	if (amount != stiffness) {
		stiffness = amount;
		dirty = true;
	}
}


/** A FIRST-ORDER ALLPASS'S DELAY at a frequency, in samples, for coefficient `c`. */
static float allpassDelay(float c, float w) {
	const float num = std::atan2(-std::sin(w), c + std::cos(w));
	const float den = std::atan2(-c * std::sin(w), 1.f + c * std::cos(w));
	return -(num - den) / w;
}


/** READING BETWEEN SAMPLES, by four-point Lagrange interpolation: the weights for a delay whose
whole part `base` is one less than its floor, so the fraction `f` lies between one and two, where
the interpolator is most accurate. */
static void lagrange(float f, float h[4]) {
	h[0] = -(f - 1.f) * (f - 2.f) * (f - 3.f) / 6.f;
	h[1] = f * (f - 2.f) * (f - 3.f) / 2.f;
	h[2] = -f * (f - 1.f) * (f - 3.f) / 2.f;
	h[3] = f * (f - 1.f) * (f - 2.f) / 6.f;
}

/** What that read does to a sine at `w`: its delay in samples and its gain. */
static void lagrangeResponse(float d, float w, float* delay, float* magnitude) {
	const int base = (int) std::floor(d) - 1;
	float h[4];
	lagrange(d - (float) base, h);
	float re = 0.f, im = 0.f;
	for (int j = 0; j < 4; j++) {
		re += h[j] * std::cos(w * (float) (base + j));
		im -= h[j] * std::sin(w * (float) (base + j));
	}
	*delay = -std::atan2(im, re) / w;
	if (*delay < 0.f)
		*delay += 2.f * PI / w;
	*magnitude = std::sqrt(re * re + im * im);
}


/** THE LOOP, WORKED OUT FOR THE NOTE.

The period in samples has to be the whole loop's delay AT THE FUNDAMENTAL: the delay line, read
between samples, plus what the loss filter and the stiffness allpasses each delay a sine at that
frequency. Those are not constants — each delays a high note by less than a low one — so they are
measured at the frequency rather than assumed, and the read solved for in two passes. That is the
difference between a top string in tune and one a few cents flat.

READ BETWEEN SAMPLES RATHER THAN THROUGH AN ALLPASS. A fractional allpass is exact for a held note
but has a memory, and every time a slide or a bend carried the delay across a whole sample its
coefficient jumped and the string clicked. An interpolated read has no memory, so the delay can
move as smoothly as the pitch does.

The gain is worked out the same way: what is left after a pass has to be the loss the decay asks
for, so the loss filter's and the read's own attenuations at the fundamental are divided back
out. */
void GuitarString::tune() {
	dirty = false;
	const float period = rate / freq;
	const float w = 2.f * PI * freq / rate;

	// A ONE-POLE LOWPASS, y = (1 - a) x + a y[-1]. The plain Karplus-Strong two-point average
	// was tried first and barely touches anything below a few kilohertz, so damping did almost
	// nothing to the partials a guitar is made of. This takes the upper partials down steadily
	// pass after pass, as a string's own losses do.
	lossS = 0.02f + 0.73f * damping;
	const float denRe = 1.f - lossS * std::cos(w);
	const float denIm = lossS * std::sin(w);
	const float lossDelay = std::atan2(denIm, denRe) / w;
	const float lossGain = (1.f - lossS) / std::sqrt(denRe * denRe + denIm * denIm);

	// STIFFNESS: allpasses whose delay falls with frequency, so the upper partials come round the
	// loop a little sooner and sit a little sharp. A real string's stiffness puts its partials
	// sharp by about the same number of cents whatever the note, and a fixed filter does not —
	// one that sharpened a low E's upper partials audibly put a high E's hundreds of cents out —
	// so the coefficient is worked out for the note: at full stiffness, the eighth partial six
	// cents sharp. Again only when the pitch has moved a quarter tone, so a bend stays cheap.
	if (stiffness != dispersionStiff
			|| std::fabs(std::log2(freq / std::fmax(dispersionFor, 1.f))) > 1.f / 24.f) {
		dispersionStiff = stiffness;
		dispersionFor = freq;
		const float target = 6.f * stiffness;
		auto centsAt = [&](float a) {
			// Where the eighth partial lands: w times the loop's delay at w is 2 pi times 8.
			const float rest = period - (float) DISPERSION_STAGES * allpassDelay(a, w);
			float w8 = 8.f * w;
			for (int i = 0; i < 8; i++)
				w8 = 2.f * PI * 8.f / (rest + (float) DISPERSION_STAGES * allpassDelay(a, w8));
			return 1200.f * std::log2(w8 / (8.f * w));
		};
		float lo = -0.95f, hi = 0.f;
		if (target <= 0.f)
			lo = 0.f;
		for (int i = 0; i < 16 && target > 0.f; i++) {
			const float mid = 0.5f * (lo + hi);
			if (centsAt(mid) > target)
				lo = mid;
			else
				hi = mid;
		}
		dispersionA = 0.5f * (lo + hi);
		// Never more delay than the loop has to give.
		while (dispersionA < 0.f && (float) DISPERSION_STAGES * allpassDelay(dispersionA, w)
				> period * 0.5f)
			dispersionA *= 0.9f;
	}
	const float dispersionDelay = (float) DISPERSION_STAGES * allpassDelay(dispersionA, w);

	// The read, solved for: what is left of the period, corrected by what the interpolator
	// itself does to the fundamental.
	const float want = period - lossDelay - dispersionDelay;
	float d = want;
	float readGain = 1.f;
	for (int pass = 0; pass < 2; pass++) {
		d = std::fmax(2.f, std::fmin(d, (float) MAX_DELAY - 4.f));
		float actual = d;
		lagrangeResponse(d, w, &actual, &readGain);
		d += want - actual;
	}
	delay = std::fmax(2.f, std::fmin(d, (float) MAX_DELAY - 4.f));

	// Sixty decibels in `decay` seconds is a thousandth of the amplitude over decay x freq passes.
	const float perPass = std::pow(0.001f, 1.f / (decay * freq));
	gain = std::fmin(perPass / std::fmax(lossGain * readGain, 1e-6f), 0.99999f);
}


float GuitarString::random() {
	noise ^= noise << 13;
	noise ^= noise >> 17;
	noise ^= noise << 5;
	return (float) noise / 2147483648.f - 1.f;
}


/** THE PICK, as the string's own shape when it is let go: a triangle, its apex where the string
was pulled aside.

A TRIANGLE AND NOT A BURST OF NOISE. Noise was used first, filtered and combed into shape, and it
puts about as much into the upper partials as into the lower ones — the plucky, harpsichord-like
sound noise-started strings are known for, and most of why the guitar sounded tinny. A string
pulled aside at a point holds a triangle, whose partials fall away as the square of their number,
so most of its energy is in the low ones, as a guitar's is. The pick's place needs no comb: a
triangle with its apex at a point has no partials with a node there, of itself.

- ROUNDED by the pick's hardness. A fingertip has width and rounds the apex, which takes the upper
  partials down further; a plectrum leaves it sharp. A one-pole low-pass run round the period,
  twice so that it has settled where the period wraps.
- A LITTLE NOISE for a hard pick: the click of a plectrum leaving the string.
- Its mean taken away, so the loop does not start with an offset in it, and scaled so its peak is
  the note's level.
- STARTED WHERE IT CROSSES NOUGHT. The pick repeats once a period, so it may begin anywhere in its
  cycle; begun at the triangle's corner, its mean taken away left it starting at about half its
  height, and the sound stepped from silence to there in one sample. A step holds every frequency
  at once, and every pluck began with a broad, clicky thump, the body's middle resonances ringing
  with it. Begun where it crosses nought, rising, the note starts from silence smoothly. */
/** A STRING STRUCK WHILE IT RINGS IS CAUGHT FIRST. A pick or fingertip meets the string, stops it
at that point and lets it go again from where it pulled it, so what the string was doing mostly
goes and the new note sounds at its own level. Added to the loop as it was, the new pluck met the
old vibration at whatever point of its cycle it happened to be, and could land against it: a C
restruck a quarter of a second after it was first picked came out up to 5 dB weaker in its first
30 ms and 7 dB weaker over the next 100, which sounds like the note cut short with nothing after
it. Caught to a quarter, the restrike is within a decibel or two of a fresh pluck, whenever it
comes. */
static const float RESTRIKE_KEEP = 0.25f;

void GuitarString::pluck(float level, float hardness, float position, bool caught) {
	if (dirty)
		tune();
	const float period = rate / freq;
	const int n = std::max(2, std::min((int) std::lround(period), (int) MAX_DELAY));
	// UP TO HALF AGAIN PAST A HARD PLECTRUM, for hearing plainly what hardness does: the shape is
	// already all but unrounded at one, so beyond it the click grows, to three times as loud.
	hardness = std::fmax(0.f, std::fmin(1.5f, hardness));
	position = std::fmax(0.02f, std::fmin(0.5f, position));

	float shape[MAX_DELAY];
	for (int i = 0; i < n; i++) {
		const float x = (float) i / (float) n;
		shape[i] = (x < position) ? x / position : (1.f - x) / (1.f - position);
	}
	// One pole, over a wide range: at full hardness nearly nothing is taken away; at none, the
	// apex is rounded down to a couple of hundred hertz, as a fingertip's is.
	const float a = std::fmin(1.f, 0.02f * std::pow(47.5f, hardness));
	float lp = shape[n - 1];
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < n; i++) {
			lp += a * (shape[i] - lp);
			if (pass == 1)
				excite[i] = lp;
		}
	}
	const float click = 0.04f * hardness * (1.f + 4.f * std::fmax(0.f, hardness - 1.f));
	float mean = 0.f;
	for (int i = 0; i < n; i++) {
		excite[i] += click * random();
		mean += excite[i];
	}
	mean /= (float) n;
	float peak = 1e-6f;
	for (int i = 0; i < n; i++) {
		excite[i] -= mean;
		peak = std::fmax(peak, std::fabs(excite[i]));
	}
	// The rising crossing nearest nought: where one sample is below and the next at or above.
	int start = 0;
	float best = 1e9f;
	for (int i = 0; i < n; i++) {
		const float a = excite[i], b = excite[(i + 1) % n];
		if (a < 0.f && b >= 0.f && std::fabs(b) < best) {
			best = std::fabs(b);
			start = (i + 1) % n;
		}
	}
	if (start != 0) {
		float turned[MAX_DELAY];
		for (int i = 0; i < n; i++)
			turned[i] = excite[(start + i) % n];
		for (int i = 0; i < n; i++)
			excite[i] = turned[i];
	}
	const float scale = std::fmax(0.f, std::fmin(1.f, level)) / peak;
	for (int i = 0; i < n; i++)
		excite[i] *= scale;
	exciteLength = n;
	excitePos = 0;
	catchGain = caught ? RESTRIKE_KEEP : 1.f;
}


void GuitarString::silence() {
	for (int i = 0; i < MAX_DELAY; i++)
		line[i] = 0.f;
	lastOut = lastRead = driven = 0.f;
	for (int i = 0; i < DISPERSION_STAGES; i++)
		dIn[i] = dOut[i] = 0.f;
	dcIn = dcOut = 0.f;
	exciteLength = excitePos = 0;
}


float GuitarString::process() {
	if (dirty)
		tune();

	// What went in one loop ago, read between samples.
	const int base = (int) std::floor(delay) - 1;
	float h[4];
	lagrange(delay - (float) base, h);
	float out = 0.f;
	for (int j = 0; j < 4; j++) {
		int at = write - base - j;
		if (at < 0)
			at += MAX_DELAY;
		out += h[j] * line[at];
	}
	lastRead = out;

	// The loss: a one-pole lowpass, then the pass's gain.
	const float lossy = (1.f - lossS) * out + lossS * lastOut;
	lastOut = lossy;

	// Stiffness: first-order allpasses in a row.
	const float a = dispersionA;
	float stiff = lossy;
	for (int i = 0; i < DISPERSION_STAGES; i++) {
		const float y = a * stiff + dIn[i] - a * dOut[i];
		dIn[i] = stiff;
		dOut[i] = y;
		stiff = y;
	}

	float in = gain * stiff + driven;
	driven = 0.f;
	if (excitePos < exciteLength) {
		in = in * catchGain + excite[excitePos++];
	}
	line[write] = in;
	if (++write >= MAX_DELAY)
		write = 0;

	// A DC blocker, so nothing the noise left behind in the loop reaches the output.
	const float y = out - dcIn + 0.995f * dcOut;
	dcIn = out;
	dcOut = y;
	return y;
}


} // namespace px
