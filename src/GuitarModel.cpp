/** See GuitarModel.hpp. */
#include "GuitarModel.hpp"

#include <cmath>

namespace px {


/** WHAT THE HANDS DO, as settings of the string. Palm mute and dead note from listening to the
behaviour they describe rather than from any measurement. A palm-muted string is the light mute of
folk and Travis picking: the attack kept, the top dulled but not smothered, and the note gone in
about 0.7 s, a muffled thump with some body. At 0.35 s, its damping at 0.9 and its pick at 0.6 of
the hardness, it was a string killed outright, 20 dB down a tenth of a second in. A dead note is
the pick and almost nothing after. */
static const float PALM_SUSTAIN = 0.7f, PALM_DAMPING = 0.7f, PALM_HARDNESS = 0.8f;
static const float DEAD_SUSTAIN = 0.03f, DEAD_DAMPING = 1.f;
/** A HAND COMING DOWN ON A STRING: a tenth of a second to fall 60 dB, the top first. */
static const float END_SUSTAIN = 0.1f, END_DAMPING = 0.8f;
/** A finger landing on or leaving a ringing string puts in a soft knock, not a pick. */
static const float KNOCK_LEVEL = 0.35f, KNOCK_HARDNESS = 0.25f;
/** A harmonic rings purer than the stopped note, so its partials die more evenly. */
static const float HARMONIC_DAMPING = 0.15f;
/** THE TAPER AND THE FRETTING FINGER at their tops. The open low E is the reference, and each
octave above it rings this many octaves of time shorter at the taper's top: 1.2 is 0.44 of the
octave below. A fretted note at full fret damping rings half as long as open and loses its upper
partials as if damping were 0.3 higher. From what is widely written about guitars ringing — the
bass strings for many seconds, the top string at the twelfth fret for two or three, the open
strings clearer than the fretted — rather than from measurement. */
static const float TAPER_OCTAVES = 1.2f;
static const float LOW_E_VOLTS = (40.f - 60.f) / 12.f;
static const float FRET_SUSTAIN_LOSS = 0.5f, FRET_EXTRA_DAMPING = 0.3f;
/** Middle C, which nought volts is. */
static const float MIDDLE_C = 261.6256f;
/** HOW LONG A STRING IS LEFT ALONE before it rings freely in sympathy: half a second after its
note ended, which is about when a hand has moved off it. */
static const float FREE_AFTER = 0.5f;
/** HOW MUCH OF THE BRIDGE A FREE STRING HEARS, each sample. Small: sympathy is a halo round
the note, tens of decibels below it, and it builds up only in a string tuned to one of the
played note's partials. */
static const float SYMPATHY = 0.0005f;
/** THE SQUEAK: how loud at most, where its band sits, and how fast the finger has to move for it
to be at full — volts a second, so four is four octaves a second, a quick slide. The level is
the quietest at which the test can tell a sliding string from a still one above the string's own
upper partials; by ear it may want to be lower. */
static const float SQUEAK_LEVEL = 0.3f, SQUEAK_HZ = 2800.f, SQUEAK_Q = 1.5f, SQUEAK_FULL = 4.f;


/** THE MODES: centre, Q, and level, for a dreadnought-sized steel-string. The air resonance
and the top's first two modes are the strongest; above them each is quieter and sharper. From
the published measurements of guitar bodies in outline rather than from any one instrument. */
static const float BODY_MODES[GuitarBody::MODES][3] = {
	{102.f, 12.f, 1.00f},     // the air in the body, through the soundhole
	{195.f, 15.f, 0.80f},     // the top plate's first mode
	{230.f, 15.f, 0.50f},     // the back's, coupled to it
	{390.f, 18.f, 0.45f},
	{470.f, 20.f, 0.35f},
	{580.f, 22.f, 0.30f},
	{840.f, 25.f, 0.25f},
	{1050.f, 25.f, 0.20f},
	{1500.f, 30.f, 0.12f},
	{2400.f, 30.f, 0.08f},
};
/** What of the strings goes straight through at full body, under the resonances. */
static const float BODY_DIRECT = 0.75f;
/** The resonances together, scaled so the full body is about as loud as the bare strings: a
strummed chord comes out within a decibel of them, measured by the test. Most of a string's
energy is in partials above the strongest modes, so the direct share has to stay large. */
static const float BODY_LEVEL = 1.6f;


GuitarBody::GuitarBody() {
	for (int i = 0; i < MODES; i++) {
		modes[i].hz = BODY_MODES[i][0];
		modes[i].q = BODY_MODES[i][1];
		modes[i].gain = BODY_MODES[i][2];
	}
	setSampleRate(48000.f);
}


/** WHERE THE BODY STOPS RADIATING: a steel-string's top carries up to about 4.5 kHz, a
classical's softer one to about 2.8 kHz. The bare strings carried everything, and three quarters
of the full body was the strings passing straight through, so turning the body up warmed the
sound without making it any less bright. */
static const float RADIATION_STEEL_HZ = 4500.f, RADIATION_NYLON_HZ = 2800.f;


void GuitarBody::setRadiation(float hz) {
	if (hz == radiationHz || rate <= 0.f)
		return;
	radiationHz = hz;
	const float g = std::tan(3.14159265f * std::fmin(hz, 0.45f * rate) / rate);
	radK = 1.41421f;
	radA1 = 1.f / (1.f + g * (g + radK));
	radA2 = g * radA1;
	radA3 = g * radA2;
}


void GuitarBody::setSampleRate(float r) {
	if (r == rate)
		return;
	rate = r;
	const float hz = (radiationHz > 0.f) ? radiationHz : RADIATION_STEEL_HZ;
	radiationHz = 0.f;
	setRadiation(hz);
	for (int i = 0; i < MODES; i++) {
		Mode& m = modes[i];
		m.g = std::tan(3.14159265f * std::fmin(m.hz, 0.45f * rate) / rate);
		m.k = 1.f / m.q;
		m.a1 = 1.f / (1.f + m.g * (m.g + m.k));
		m.a2 = m.g * m.a1;
		m.a3 = m.g * m.a2;
	}
}


void GuitarBody::reset() {
	for (int i = 0; i < MODES; i++)
		modes[i].ic1 = modes[i].ic2 = 0.f;
	radIc1 = radIc2 = 0.f;
}


/** NYLON AGAINST STEEL: a nylon string is perhaps a sixth as stiff, loses its upper partials much
sooner, and is played with the flesh of the finger rather than a pick. */
static const float NYLON_STIFFNESS = 0.15f, NYLON_EXTRA_DAMPING = 0.45f, NYLON_HARDNESS = 0.6f;


float GuitarModel::stringDamping() const {
	return settings.nylon && !settings.electric
		? std::fmin(1.f, settings.damping + NYLON_EXTRA_DAMPING) : settings.damping;
}


float GuitarModel::stringStiffness() const {
	return settings.nylon && !settings.electric
		? settings.stiffness * NYLON_STIFFNESS : settings.stiffness;
}


float GuitarModel::stringHardness() const {
	return settings.nylon && !settings.electric
		? settings.hardness * NYLON_HARDNESS : settings.hardness;
}


float GuitarBody::process(float in, float amount) {
	if (amount <= 0.f)
		return in;
	float res = 0.f;
	for (int i = 0; i < MODES; i++) {
		Mode& m = modes[i];
		const float v3 = in - m.ic2;
		const float v1 = m.a1 * m.ic1 + m.a2 * v3;
		const float v2 = m.ic2 + m.a2 * m.ic1 + m.a3 * v3;
		m.ic1 = 2.f * v1 - m.ic1;
		m.ic2 = 2.f * v2 - m.ic2;
		// The band-pass is v1; times k, its peak is one.
		res += m.gain * m.k * v1;
	}
	float full = BODY_DIRECT * in + BODY_LEVEL * res;
	// What the body radiates: its highs rolled away.
	const float v3 = full - radIc2;
	const float v1 = radA1 * radIc1 + radA2 * v3;
	const float v2 = radIc2 + radA2 * radIc1 + radA3 * v3;
	radIc1 = 2.f * v1 - radIc1;
	radIc2 = 2.f * v2 - radIc2;
	full = v2;
	return in + amount * (full - in);
}


void ToneFilter::set(float hz, float q, float rate) {
	g = std::tan(3.14159265f * std::fmin(hz, 0.45f * rate) / rate);
	k = 1.f / q;
	a1 = 1.f / (1.f + g * (g + k));
	a2 = g * a1;
	a3 = g * a2;
}


float ToneFilter::lowpass(float in) {
	const float v3 = in - ic2;
	const float v1 = a1 * ic1 + a2 * v3;
	const float v2 = ic2 + a2 * ic1 + a3 * v3;
	ic1 = 2.f * v1 - ic1;
	ic2 = 2.f * v2 - ic2;
	return v2;
}


float ToneFilter::bandpass(float in) {
	const float v3 = in - ic2;
	const float v1 = a1 * ic1 + a2 * v3;
	const float v2 = ic2 + a2 * ic1 + a3 * v3;
	ic1 = 2.f * v1 - ic1;
	ic2 = 2.f * v2 - ic2;
	return k * v1;
}


float ToneFilter::highpass(float in) {
	const float v3 = in - ic2;
	const float v1 = a1 * ic1 + a2 * v3;
	const float v2 = ic2 + a2 * ic1 + a3 * v3;
	ic1 = 2.f * v1 - ic1;
	ic2 = 2.f * v2 - ic2;
	return in - k * v1 - v2;
}


/** THE AMPLIFIER'S DRIVE across its knob: from 1, where only a chord played hard breaks up, to
12, where everything does. */
static const float DRIVE_MIN = 1.f, DRIVE_RATIO = 18.f;
/** Where the tone stack's three bands sit. */
static const float BASS_HZ = 100.f, MIDDLE_HZ = 500.f, MIDDLE_Q = 0.7f, TREBLE_HZ = 3000.f;
/** The guitar's tone knob: two one-pole low-passes, from 800 Hz all the way down to open. One
was tried and took barely 8 dB off an octave above, which is not the dark a guitar's tone knob
turned right down gives. */
static const float TONE_MIN_HZ = 300.f;
/** Where the speaker rolls off. */
static const float SPEAKER_LOW_HZ = 70.f, SPEAKER_HIGH_HZ = 5000.f;
/** The pickup's coil resonance: a gentle peak, then a roll-off above it. */
static const float COIL_HZ = 3200.f, COIL_Q = 2.5f;
/** Where the pickup sits, across the body knob: the bridge pickup's place to the neck's, as
fractions of the string from the bridge. */
static const float PICKUP_BRIDGE = 0.06f, PICKUP_NECK = 0.25f;
/** The electric's level against the acoustic's, so the switch changes the sound and not the
volume: measured on a strummed chord by the test. */
static const float ELECTRIC_TRIM = 4.0f;
/** What of the pickups' signal reaches the amplifier: see where it is applied. */
static const float PICKUP_LEVEL = 0.18f;


void GuitarAmp::setSampleRate(float r) {
	if (r == rate)
		return;
	rate = r;
	speakerLow.set(SPEAKER_LOW_HZ, 0.707f, rate);
	speakerHigh.set(SPEAKER_HIGH_HZ, 0.707f, rate);
	bassShelf.set(BASS_HZ, 0.707f, rate);
	middlePeak.set(MIDDLE_HZ, MIDDLE_Q, rate);
	trebleShelf.set(TREBLE_HZ, 0.707f, rate);
}


static float gainOf(float db) {
	return std::pow(10.f, db / 20.f);
}


float GuitarAmp::process(float in, const AmpSettings& s) {
	// SOFT SATURATION with unity gain for a small signal: tanh of the driven signal, divided
	// back by the drive, so playing lightly comes through at about the same level and playing
	// hard, or driving it, rounds it off and then breaks it up.
	const float drive = DRIVE_MIN * std::pow(DRIVE_RATIO, std::fmax(0.f, std::fmin(1.f, s.drive)));
	float y = std::tanh(drive * in) / std::sqrt(drive);
	// THE TONE STACK: each band its own filter, its share of the signal added in or taken away.
	y += (gainOf(s.bass) - 1.f) * bassShelf.lowpass(y);
	y += (gainOf(s.middle) - 1.f) * middlePeak.bandpass(y);
	y += (gainOf(s.treble) - 1.f) * trebleShelf.highpass(y);
	y = speakerLow.highpass(y);
	y = speakerHigh.lowpass(y);
	return y;
}


void GuitarAmp::reset() {
	speakerLow.reset();
	speakerHigh.reset();
	bassShelf.reset();
	middlePeak.reset();
	trebleShelf.reset();
}


void GuitarModel::setSampleRate(float rate) {
	sampleTime = 1.f / rate;
	ampLeft.setSampleRate(rate);
	ampRight.setSampleRate(rate);
	for (int s = 0; s < STRINGS; s++)
		coil[s].set(COIL_HZ, COIL_Q, rate);
	for (int s = 0; s < STRINGS; s++)
		strings[s].setSampleRate(rate);
	bodyLeft.setSampleRate(rate);
	bodyRight.setSampleRate(rate);
}


int GuitarModel::harmonicAt(int fret) {
	if (fret <= 0)
		return 0;
	// Where the finger is, as a fraction of the string from the nut, and the smallest partial
	// with a node there.
	const float at = 1.f - std::pow(2.f, -(float) fret / 12.f);
	for (int n = 2; n <= 8; n++) {
		const float k = at * (float) n;
		if (std::fabs(k - std::round(k)) < 0.04f)
			return n;
	}
	return 0;
}


/** How many strings the instrument has: as many as have an open pitch. */
int GuitarModel::openCount() const {
	int n = 0;
	for (int s = 0; s < STRINGS; s++)
		if (openKnown[s])
			n = s + 1;
	return n;
}


/** A HAND ON THE STRING: it is not free, and whether its pitch is about to slide. A slide into or
out of a note slides on this note; a legato or shift slide slides on the way to the next. */
void GuitarModel::touch(int s, uint32_t technique) {
	setFree(s, false);
	idle[s] = 0.f;
	ended[s] = false;
	sliding[s] = slideNext[s] || (technique & (GT_SLIDE_IN_BELOW | GT_SLIDE_IN_ABOVE
		| GT_SLIDE_OUT_DOWN | GT_SLIDE_OUT_UP)) != 0;
	slideNext[s] = (technique & (GT_LEGATO_SLIDE | GT_SHIFT_SLIDE)) != 0;
}


/** Free: tuned to its open pitch and undamped, so it can ring with what the bridge brings it. */
void GuitarModel::setFree(int s, bool on) {
	if (free[s] == on)
		return;
	free[s] = on;
	if (on) {
		strings[s].setDecay(ringTime(open[s], 0));
		strings[s].setDamping(stringDamping());
		strings[s].setFrequency(MIDDLE_C * std::pow(2.f, open[s]));
	}
}


float GuitarModel::ringTime(float volts, int fret) const {
	const float above = std::fmax(0.f, volts - LOW_E_VOLTS);
	float seconds = settings.sustain * std::pow(2.f, -TAPER_OCTAVES * settings.taper * above);
	if (fret > 0)
		seconds *= 1.f - FRET_SUSTAIN_LOSS * settings.fretDamping;
	return seconds;
}


float GuitarModel::ringDamping(int fret) const {
	float damping = stringDamping();
	if (fret > 0)
		damping = std::fmin(1.f, damping + FRET_EXTRA_DAMPING * settings.fretDamping);
	return damping;
}


void GuitarModel::strike(int s, float l, uint32_t technique, int fret) {
	if (s < 0 || s >= STRINGS)
		return;
	touch(s, technique);
	GuitarString& g = strings[s];
	g.setStiffness(stringStiffness());
	// AN ORDINARY NOTE rings by its pitch and by whether a finger stops it; a harmonic is not
	// stopped by the finger touching it, so it rings as the open string does.
	const bool harmonic = (technique & (GT_HARMONIC | GT_ARTIFICIAL_HARMONIC)) != 0;
	const int stopped = harmonic ? 0 : fret;
	float sustain = ringTime(last[s], stopped), damping = ringDamping(stopped);
	float hardness = stringHardness();
	float position = settings.pickPosition;
	offset[s] = 0.f;

	if (technique & GT_DEAD_NOTE) {
		sustain = DEAD_SUSTAIN;
		damping = DEAD_DAMPING;
	}
	else if (technique & GT_PALM_MUTE) {
		sustain = PALM_SUSTAIN;
		damping = PALM_DAMPING;
		hardness *= PALM_HARDNESS;
	}
	if (technique & GT_HARMONIC) {
		// THE STRING SOUNDS THE PARTIAL WITH A NODE UNDER THE FINGER, above the open string
		// rather than above the fret: the seventh-fret harmonic is an octave and a fifth over
		// the open note, not a fifth. Measured from the stopped pitch, so a bend still moves it.
		const int n = harmonicAt(fret);
		if (n > 0 && openKnown[s])
			offset[s] = open[s] + std::log2((float) n) - last[s];
		damping = HARMONIC_DAMPING;
	}
	else if (technique & GT_ARTIFICIAL_HARMONIC) {
		offset[s] = 1.f;       // An octave over the stopped note, the commonest.
		damping = HARMONIC_DAMPING;
	}

	g.setDecay(sustain);
	g.setDamping(damping);
	g.setFrequency(MIDDLE_C * std::pow(2.f, last[s] + offset[s]));
	strikeLevel[s] = level[s] = l;
	g.pluck(l, hardness, position);
}


void GuitarModel::legato(int s, float l, uint32_t technique) {
	if (s < 0 || s >= STRINGS)
		return;
	touch(s, technique);
	strikeLevel[s] = level[s] = std::fmax(l, 0.0001f);
	if (technique & (GT_HAMMER_ON | GT_PULL_OFF | GT_TAPPED))
		strings[s].pluck(l * KNOCK_LEVEL, KNOCK_HARDNESS, settings.pickPosition, false);
}


void GuitarModel::setLevel(int s, float l) {
	if (s >= 0 && s < STRINGS)
		level[s] = l;
}


void GuitarModel::end(int s) {
	if (s < 0 || s >= STRINGS)
		return;
	strings[s].setDecay(END_SUSTAIN);
	strings[s].setDamping(END_DAMPING);
	ended[s] = true;
	idle[s] = 0.f;
	sliding[s] = false;
}


void GuitarModel::setPitch(int s, float volts) {
	if (s < 0 || s >= STRINGS)
		return;
	// THE FINGER'S SPEED along the string, while it slides on one of the wound strings — the
	// lower half of the instrument.
	const float speed = std::fabs(volts - last[s]) / sampleTime;
	const int count = openCount();
	const bool wound = count > 0 && s >= count / 2;
	const float want = (settings.fingerNoise > 0.f && sliding[s] && wound)
		? std::fmin(speed / SQUEAK_FULL, 1.f) : 0.f;
	// Smoothed over a few milliseconds, since the performer moves the pitch in steps.
	squeak[s] += (want - squeak[s]) * std::fmin(1.f, sampleTime * 200.f);
	last[s] = volts;
	// A free string is open, whatever pitch the performer last left the voice at.
	if (!free[s])
		strings[s].setFrequency(MIDDLE_C * std::pow(2.f, volts + offset[s]));
}


void GuitarModel::silence() {
	for (int s = 0; s < STRINGS; s++) {
		strings[s].silence();
		strikeLevel[s] = level[s] = offset[s] = squeak[s] = 0.f;
		squeakIc1[s] = squeakIc2[s] = 0.f;
		free[s] = sliding[s] = slideNext[s] = ended[s] = false;
		idle[s] = 0.f;
	}
	bodyLeft.reset();
	bodyRight.reset();
	ampLeft.reset();
	ampRight.reset();
	toneLeft[0] = toneLeft[1] = toneRight[0] = toneRight[1] = 0.f;
	for (int s = 0; s < STRINGS; s++) {
		coil[s].reset();
		for (int i = 0; i < PICKUP_HISTORY; i++)
			history[s][i] = 0.f;
	}
}


void GuitarModel::process(float* left, float* right) {
	// WHO IS FREE: a string whose note ended a while ago, or that has never been played, and
	// whose open pitch is known.
	for (int s = 0; s < STRINGS; s++) {
		if (!openKnown[s] || settings.sympathy <= 0.f) {
			setFree(s, false);
			continue;
		}
		if (ended[s] || strikeLevel[s] <= 0.f)
			idle[s] += sampleTime;
		if (!free[s] && (strikeLevel[s] <= 0.f || (ended[s] && idle[s] >= FREE_AFTER)))
			setFree(s, true);
	}
	// THE BRIDGE: what the strings being played are doing there. Only those, and only the free
	// strings hear it, so no two strings feed each other round a loop that could grow.
	float bridge = 0.f;
	for (int s = 0; s < STRINGS; s++)
		if (!free[s])
			bridge += strings[s].bridge();
	if (settings.sympathy > 0.f)
		for (int s = 0; s < STRINGS; s++)
			if (free[s])
				strings[s].drive(2.f * settings.sympathy * SYMPATHY * bridge);

	float l = 0.f, r = 0.f;
	const float pickupAt = PICKUP_BRIDGE + (PICKUP_NECK - PICKUP_BRIDGE)
		* std::fmax(0.f, std::fmin(1.f, settings.body));
	for (int s = 0; s < STRINGS; s++) {
		float v = strings[s].process();
		if (settings.electric) {
			// THE PICKUP HEARS THE STRING AT A POINT, so it cannot hear the partials with a node
			// there: the string less itself a fraction of a period earlier, which is a comb with
			// its notches on exactly those partials. Near the bridge few are lost and the sound
			// is bright and thin; at the neck more, and it is round and warm.
			history[s][historyAt] = v;
			const float back = pickupAt * strings[s].sampleRate() / strings[s].frequency();
			const float at = (float) historyAt - std::fmin(back, (float) PICKUP_HISTORY - 2.f);
			const float wrapped = (at < 0.f) ? at + (float) PICKUP_HISTORY : at;
			const int i0 = (int) wrapped;
			const float f = wrapped - (float) i0;
			const float earlier = history[s][i0] * (1.f - f)
				+ history[s][(i0 + 1) % PICKUP_HISTORY] * f;
			v = coil[s].lowpass(v - earlier);
		}
		// THE SQUEAK, a band of noise riding on the string while its finger slides.
		if (squeak[s] > 0.0001f) {
			noise ^= noise << 13;
			noise ^= noise >> 17;
			noise ^= noise << 5;
			const float white = (float) noise / 2147483648.f - 1.f;
			const float g = std::tan(3.14159265f * SQUEAK_HZ * sampleTime);
			const float k = 1.f / SQUEAK_Q;
			const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
			const float v3 = white - squeakIc2[s];
			const float v1 = a1 * squeakIc1[s] + a2 * v3;
			const float v2 = squeakIc2[s] + a2 * squeakIc1[s] + a3 * v3;
			squeakIc1[s] = 2.f * v1 - squeakIc1[s];
			squeakIc2[s] = 2.f * v2 - squeakIc2[s];
			v += 2.f * settings.fingerNoise * SQUEAK_LEVEL * squeak[s] * strikeLevel[s] * k * v1;
		}
		// What the performer does to a note's loudness while it sounds — a tremolo's pulse, a
		// slide's fade — relative to how hard it was struck.
		if (strikeLevel[s] > 0.f)
			v *= std::fmin(level[s] / strikeLevel[s], 2.f);
		// Equal power, the middle at 0.707 on each side.
		const float p = std::fmax(-1.f, std::fmin(1.f, pan[s]));
		const float angle = (p + 1.f) * 0.7853982f;
		l += v * std::cos(angle);
		r += v * std::sin(angle);
	}
	if (++historyAt >= PICKUP_HISTORY)
		historyAt = 0;

	// THE BODY, on each side, for the acoustic; the amplifier for the electric. The strings are
	// placed before either, so a chord keeps its spread.
	if (settings.electric) {
		// THE PICKUP'S LEVEL INTO THE AMPLIFIER. The strings arrived at the amplifier at their
		// own level, a single note peaking near 0.9 and a chord near 2.5, where its saturation
		// flattens everything above about one: the electric was driven hard at every setting of
		// the drive knob, and what the tone knob took away the amplifier put back. Taken down to
		// where a note is clean with no drive and a chord just short of breaking up.
		l *= PICKUP_LEVEL;
		r *= PICKUP_LEVEL;
		// THE GUITAR'S TONE KNOB, before the amplifier as it is on an instrument.
		const float hz = TONE_MIN_HZ * std::pow(20000.f / TONE_MIN_HZ,
			std::fmax(0.f, std::fmin(1.f, settings.tone)));
		const float a = 1.f - std::exp(-2.f * 3.14159265f * hz * sampleTime);
		toneLeft[0] += a * (l - toneLeft[0]);
		toneLeft[1] += a * (toneLeft[0] - toneLeft[1]);
		toneRight[0] += a * (r - toneRight[0]);
		toneRight[1] += a * (toneRight[0] - toneRight[1]);
		l = ELECTRIC_TRIM * ampLeft.process(toneLeft[1], settings.amp);
		r = ELECTRIC_TRIM * ampRight.process(toneRight[1], settings.amp);
	}
	else {
		const float radiation = settings.nylon ? RADIATION_NYLON_HZ : RADIATION_STEEL_HZ;
		bodyLeft.setRadiation(radiation);
		bodyRight.setRadiation(radiation);
		l = bodyLeft.process(l, settings.body);
		r = bodyRight.process(r, settings.body);
	}
	*left = l;
	*right = r;
}


} // namespace px
