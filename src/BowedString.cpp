/** See BowedString.hpp. */
#include "BowedString.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const float PI = 3.14159265358979f;

/** THE LEAST A SEGMENT MAY BE, in samples. Four-point Lagrange wants a delay of about two to read
between the samples it has, so a contact point closer to the bridge than this cannot be had and is
held out to it. On a cello's low C there are 367 samples each way at 48 kHz and no bow position is
out of reach; on a violin's top string there are 36, and anything under about 0.055 of the length
is. That is the sample rate problem, and the figure the oversampling schedule has to beat. */
static const float MIN_SEGMENT = 2.f;

/** How much the nut sends back. Not quite all of it: a stopped string against a finger loses more
than an open one against the nut, and nothing here is perfectly rigid. */
static const float NUT_REFLECT = 0.995f;


BowedString::BowedString() {}


void BowedString::setSampleRate(float r) {
	if (r > 0.f && r != rate) {
		rate = r;
		dirty = true;
	}
}


void BowedString::setFrequency(float hz) {
	hz = std::fmax(20.f, std::fmin(hz, rate * 0.25f));
	if (hz != freq) {
		freq = hz;
		dirty = true;
	}
}


void BowedString::setDecay(float seconds) {
	seconds = std::fmax(0.01f, seconds);
	if (seconds != decay) {
		decay = seconds;
		dirty = true;
	}
}


void BowedString::setDamping(float amount) {
	amount = std::fmax(0.f, std::fmin(1.f, amount));
	if (amount != damping) {
		damping = amount;
		dirty = true;
	}
}


void BowedString::setStiffness(float amount) {
	amount = std::fmax(0.f, std::fmin(1.5f, amount));
	if (amount != stiffness) {
		stiffness = amount;
		dirty = true;
	}
}


void BowedString::setContact(float b) {
	b = std::fmax(0.005f, std::fmin(0.5f, b));
	if (b != beta) {
		beta = b;
		dirty = true;
	}
}


/** A FIRST-ORDER ALLPASS'S DELAY at a frequency, in samples. GuitarString's. */
static float allpassDelay(float c, float w) {
	const float num = std::atan2(-std::sin(w), c + std::cos(w));
	const float den = std::atan2(-c * std::sin(w), 1.f + c * std::cos(w));
	return -(num - den) / w;
}


/** Four-point Lagrange weights, for a whole part one below the floor so the fraction lies between
one and two, where the interpolator is most accurate. GuitarString's. */
static void lagrange(float f, float h[4]) {
	h[0] = -(f - 1.f) * (f - 2.f) * (f - 3.f) / 6.f;
	h[1] = f * (f - 2.f) * (f - 3.f) / 2.f;
	h[2] = -f * (f - 1.f) * (f - 3.f) / 2.f;
	h[3] = f * (f - 1.f) * (f - 2.f) / 6.f;
}


/** What such a read does to a sine at `w`: its delay in samples and its gain. */
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


/** THE LOOP, WORKED OUT FOR THE NOTE AND THE CONTACT POINT.

The round trip is twice each segment, because every one is crossed once going and once coming
back, plus whatever the bridge reflection's filters delay a sine at the fundamental. So the sum of
the two segments is half of what GuitarString solves for a single line, and the only new thing is
dividing it by the contact point.

SOLVED, NOT ASSUMED. Four interpolated reads each put their own small phase error into the loop,
and the four are not equal because the segments are not. The total is measured at the fundamental
and the error shared back between the segments in proportion, twice over, which is what keeps the
string in tune wherever the bow is standing.

WHERE IT RUNS OUT. A contact point near the bridge wants a segment of a sample or two, and the
read cannot be done in less than about two. Rather than detune the string the short segment is
held at the floor and the long one takes the rest: the pitch stays right, the contact point is not
where it was asked for, and `contactClamped` says so. */
void BowedString::tune() {
	dirty = false;
	const float period = rate / freq;
	const float w = 2.f * PI * freq / rate;

	// A one-pole lowpass in the bridge reflection, as GuitarString's loss filter.
	lossS = 0.02f + 0.73f * damping;
	const float denRe = 1.f - lossS * std::cos(w);
	const float denIm = lossS * std::sin(w);
	const float lossDelay = std::atan2(denIm, denRe) / w;
	const float lossGain = (1.f - lossS) / std::sqrt(denRe * denRe + denIm * denIm);

	// Stiffness, worked out for the note so the eighth partial sits the same number of cents
	// sharp whatever is played, as in GuitarString. Only when the pitch has moved a quarter tone.
	if (stiffness != dispersionStiff
			|| std::fabs(std::log2(freq / std::fmax(dispersionFor, 1.f))) > 1.f / 24.f) {
		dispersionStiff = stiffness;
		dispersionFor = freq;
		const float target = 6.f * stiffness;
		auto centsAt = [&](float a) {
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
		while (dispersionA < 0.f && (float) DISPERSION_STAGES * allpassDelay(dispersionA, w)
				> period * 0.5f)
			dispersionA *= 0.9f;
	}
	const float dispersionDelay = (float) DISPERSION_STAGES * allpassDelay(dispersionA, w);

	// What the four reads have to give between them, and how it divides.
	const float want = period - lossDelay - dispersionDelay;
	float half = std::fmax(2.f * MIN_SEGMENT, want) * 0.5f;
	float db = beta * half;
	float dn = half - db;
	clamped = false;
	if (db < MIN_SEGMENT) {
		db = MIN_SEGMENT;
		dn = half - db;
		clamped = true;
	} else if (dn < MIN_SEGMENT) {
		dn = MIN_SEGMENT;
		db = half - dn;
		clamped = true;
	}
	const float shareB = db / (db + dn);

	// Two passes over the whole loop, the error shared between the segments as their lengths are.
	float readGain = 1.f;
	for (int pass = 0; pass < 2; pass++) {
		float gotB = db, gotN = dn, magB = 1.f, magN = 1.f;
		lagrangeResponse(std::fmax(MIN_SEGMENT, db), w, &gotB, &magB);
		lagrangeResponse(std::fmax(MIN_SEGMENT, dn), w, &gotN, &magN);
		readGain = magB * magB * magN * magN;
		const float err = want - 2.f * (gotB + gotN);
		db += shareB * err * 0.5f;
		dn += (1.f - shareB) * err * 0.5f;
		if (db < MIN_SEGMENT) { dn += db - MIN_SEGMENT; db = MIN_SEGMENT; clamped = true; }
		if (dn < MIN_SEGMENT) { db += dn - MIN_SEGMENT; dn = MIN_SEGMENT; clamped = true; }
	}
	const float room = (float) MAX_DELAY - 4.f;
	toBridgeDelay = std::fmax(MIN_SEGMENT, std::fmin(db, room));
	toNutDelay = std::fmax(MIN_SEGMENT, std::fmin(dn, room));
	shortest = std::fmin(toBridgeDelay, toNutDelay);

	// Sixty decibels in `decay` seconds is a thousandth over decay x freq round trips. A round
	// trip meets each end once, not twice: bow to bridge, back to bow, on to the nut, back
	// again. Counting the nut's reflection for each of the two segments it borders left a
	// string asked for six seconds ringing for nearly nine.
	const float perPass = std::pow(0.001f, 1.f / (decay * freq));
	const float passive = lossGain * readGain * NUT_REFLECT;
	gain = std::fmin(perPass / std::fmax(passive, 1e-6f), 0.99999f);
}


float BowedString::random() {
	noise ^= noise << 13;
	noise ^= noise >> 17;
	noise ^= noise << 5;
	return (float) noise / 2147483648.f - 1.f;
}


float BowedString::read(const float* line, float d) const {
	const int base = (int) std::floor(d) - 1;
	float h[4];
	lagrange(d - (float) base, h);
	float out = 0.f;
	for (int j = 0; j < 4; j++) {
		int at = write - base - j;
		while (at < 0)
			at += MAX_DELAY;
		out += h[j] * line[at];
	}
	return out;
}


/** PIZZICATO, which is the only way to set the string going until there is a bow — and wanted in
its own right, so it is built here rather than added later. GuitarString's triangle, rounded by
the finger and started where it crosses nought for the same reasons; half of it goes into each
outgoing wave, because a string let go at a point sends the same shape both ways. */
void BowedString::pluck(float level, float hardness) {
	if (dirty)
		tune();
	const float period = rate / freq;
	const int n = std::max(2, std::min((int) std::lround(period), (int) MAX_DELAY));
	hardness = std::fmax(0.f, std::fmin(1.5f, hardness));

	float shape[MAX_DELAY];
	for (int i = 0; i < n; i++) {
		const float x = (float) i / (float) n;
		shape[i] = (x < beta) ? x / beta : (1.f - x) / (1.f - beta);
	}
	const float a = std::fmin(1.f, 0.02f * std::pow(47.5f, hardness));
	float lp = shape[n - 1];
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < n; i++) {
			lp += a * (shape[i] - lp);
			if (pass == 1)
				excite[i] = lp;
		}
	const float click = 0.04f * hardness;
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
	int start = 0;
	float best = 1e9f;
	for (int i = 0; i < n; i++) {
		const float x = excite[i], y = excite[(i + 1) % n];
		if (x < 0.f && y >= 0.f && std::fabs(y) < best) {
			best = std::fabs(y);
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
	const float scale = 0.5f * std::fmax(0.f, std::fmin(1.f, level)) / peak;
	for (int i = 0; i < n; i++)
		excite[i] *= scale;
	exciteLength = n;
	excitePos = 0;
}


void BowedString::silence() {
	for (int i = 0; i < MAX_DELAY; i++)
		bowToBridge[i] = bridgeToBow[i] = bowToNut[i] = nutToBow[i] = 0.f;
	lastOut = atBridge = 0.f;
	for (int i = 0; i < DISPERSION_STAGES; i++)
		dIn[i] = dOut[i] = 0.f;
	dcIn = dcOut = 0.f;
	exciteLength = excitePos = 0;
}


/** ONE SAMPLE, ROUND THE WHOLE STRING.

Read what has arrived at each of the three places something happens, then write what leaves them.
Reading everything before writing anything is what makes the four lines one loop rather than four,
and is why a segment may be as short as the interpolator allows without the order of the writes
mattering.

THE BOW JUNCTION IS TRANSPARENT in this phase. What arrives from one side leaves towards the
other, which is a string with nothing touching it. The friction goes here in phase 2, as a
velocity added to both outgoing waves. */
float BowedString::process() {
	if (dirty)
		tune();

	const float arrivingAtBridge = read(bowToBridge, toBridgeDelay);
	const float arrivingAtNut = read(bowToNut, toNutDelay);
	const float fromBridge = read(bridgeToBow, toBridgeDelay);
	const float fromNut = read(nutToBow, toNutDelay);
	atBridge = arrivingAtBridge;

	// THE BRIDGE: the loss filter, the stiffness allpasses, the round trip's gain, and the sign
	// change a reflection from a near-rigid end makes.
	const float lossy = (1.f - lossS) * arrivingAtBridge + lossS * lastOut;
	lastOut = lossy;
	float stiff = lossy;
	for (int i = 0; i < DISPERSION_STAGES; i++) {
		const float y = dispersionA * stiff + dIn[i] - dispersionA * dOut[i];
		dIn[i] = stiff;
		dOut[i] = y;
		stiff = y;
	}
	bridgeToBow[write] = -gain * stiff;

	// THE NUT: a reflection and almost nothing else.
	nutToBow[write] = -NUT_REFLECT * arrivingAtNut;

	// THE BOW: straight through, plus the pluck while one is going in.
	float push = 0.f;
	if (excitePos < exciteLength)
		push = excite[excitePos++];
	bowToBridge[write] = fromNut + push;
	bowToNut[write] = fromBridge + push;

	if (++write >= MAX_DELAY)
		write = 0;

	// A DC blocker, so nothing the finger's noise left in the loop reaches the output.
	const float y = atBridge - dcIn + 0.995f * dcOut;
	dcIn = atBridge;
	dcOut = y;
	return y;
}


} // namespace px
