/** See BowedInstrument.hpp. */
#include "BowedInstrument.hpp"

#include <cmath>

namespace px {


static BowedBody::Mode mode(float hz, float q, float mobility, float radiativity) {
	BowedBody::Mode m;
	m.hz = hz;
	m.q = q;
	m.mobility = mobility;
	m.radiativity = radiativity;
	return m;
}


/** THE VIOLIN, from research/bowed-strings/. Strings: the Thomastik Dominant set Pickering measured,
on a 328 mm string length.

ITS VOICE. Radiativity is Bissinger's: the sound pressure averaged over a sphere per newton at the
bridge, measured at 1.2 m and here taken to a metre (times 1.2). The signature modes' frequencies are
the means over seventeen violins (Bissinger 2008a), their damping the fourteen-violin averages
(Bissinger 2012), the B1 modes' radiativity his averages and A0's read off the ensemble figure; CBR's
and A1's levels are judgement, CBR a weak radiator and A1 very variable. Mobilities read off
Woodhouse 2005's measured violin. Above 630 Hz the modes are dense, two in a hundred hertz at Q 50,
the density and damping that agree with Woodhouse's modal overlap for a violin being played; their
radiativity follows Bissinger's seventeen-violin average band by band to 3,875 Hz, where his
measurements stop, and falls 8.5 dB an octave above, as the measured bridge mobility does. The bridge
hill is in that curve already, the peak at 2.4 kHz, so there is no bridge filter on top of it. Of the
first four hundred ways of scattering the upper modes, the one whose bands come closest to
Bissinger's average: within 0.8 dB in the median and 2.6 at worst, closer than any one real violin.

ITS BRIDGE, as the strings feel it: the four signature modes that move it. A1 is the air and moves
nothing at the bridge. */
BowedInstrument::Design BowedInstrument::violin() {
	Design d;
	d.strings = {{196.00f, 0.34f}, {293.66f, 0.23f}, {440.00f, 0.19f}, {659.26f, 0.17f}};
	d.length = 0.328f;
	d.voice.modes = {mode(275.f, 20.f, 0.022f, 0.444f),     // A0, the air in the box
	                 mode(407.f, 40.f, 0.08f, 0.121f),      // CBR
	                 mode(469.f, 50.f, 0.f, 0.383f),        // A1, air again
	                 mode(475.f, 41.f, 0.17f, 1.212f),      // B1-
	                 mode(541.f, 43.f, 0.19f, 1.140f)};     // B1+
	d.voice.dense.fromHz = 630.f;
	d.voice.dense.toHz = 10000.f;
	d.voice.dense.perHz = 0.02f;
	d.voice.dense.loss = 0.02f;
	d.voice.dense.radiativity = {{625.f, -10.5f}, {875.f, -7.1f}, {1125.f, -6.6f}, {1375.f, -7.3f},
	                             {1625.f, -6.0f}, {1875.f, -3.9f}, {2125.f, -2.6f}, {2375.f, -1.0f},
	                             {2625.f, -2.6f}, {2875.f, -3.2f}, {3125.f, -4.8f}, {3375.f, -4.3f},
	                             {3625.f, -4.3f}, {3875.f, -4.8f}, {7750.f, -13.3f}, {10000.f, -16.4f}};
	d.voice.dense.mobility = {{630.f, -24.f}, {1000.f, -24.f}, {1500.f, -20.f}, {2400.f, -16.f},
	                          {3000.f, -18.f}, {5000.f, -30.f}, {10000.f, -41.f}};
	d.voice.dense.seed = 44u;
	d.voice.bridge.hz = 0.f;
	d.bridge.modes = {mode(275.f, 20.f, 0.022f, 0.f), mode(407.f, 40.f, 0.08f, 0.f),
	                  mode(475.f, 41.f, 0.17f, 0.f), mode(541.f, 43.f, 0.19f, 0.f)};
	d.bridge.dense.perHz = 0.f;
	d.bridge.bridge.hz = 0.f;
	return d;
}


/** THE CELLO, PROVISIONAL. Strings: the D'Addario Kaplan set, on a 690 mm string length.

ITS VOICE. Measured: the air mode near 97 Hz, the strongest body mode at 175 Hz with Q 18 and a
mobility of 0.056 m/(s N), the one that makes a real cello's wolf (Zhang and Woodhouse 2014), and peaks at 140,
200, 209 and 281 Hz. Not measured anywhere that could be found: a cello's radiativity, the damping of
all but the wolf mode, and the density of its upper modes, here two and a half times the violin's
for a body about twice its size. Their levels are judgement — the upper modes shaped as the measured
mobility is, times a radiation efficiency that rises up to about 1.5 kHz — to be settled by ear.

ITS BRIDGE, as the strings feel it: the six low modes. */
BowedInstrument::Design BowedInstrument::cello() {
	Design d;
	d.strings = {{65.41f, 1.46f}, {98.00f, 1.00f}, {146.83f, 0.67f}, {220.00f, 0.56f}};
	d.length = 0.69f;
	d.voice.modes = {mode(97.f, 20.f, 0.01f, 0.50f),        // A0
	                 mode(140.f, 30.f, 0.03f, 0.71f),       // B1-
	                 mode(175.f, 18.f, 0.056f, 1.0f),       // the wolf's mode
	                 mode(200.f, 30.f, 0.02f, 0.50f),
	                 mode(209.f, 30.f, 0.02f, 0.40f),
	                 mode(281.f, 30.f, 0.015f, 0.40f)};
	d.voice.dense.fromHz = 320.f;
	d.voice.dense.toHz = 8000.f;
	d.voice.dense.perHz = 0.05f;
	d.voice.dense.loss = 0.025f;
	d.voice.dense.radiativity = {{320.f, -16.f}, {600.f, -12.f}, {1000.f, -8.f}, {2000.f, -8.f},
	                             {4000.f, -20.f}, {8000.f, -32.f}};
	d.voice.dense.mobility = {{320.f, -28.f}, {430.f, -28.f}, {1000.f, -28.f}, {2000.f, -30.f},
	                          {4000.f, -44.f}, {8000.f, -55.f}};
	d.voice.dense.seed = 2207u;
	d.voice.bridge.hz = 0.f;
	for (const BowedBody::Mode& m : d.voice.modes)
		d.bridge.modes.push_back(mode(m.hz, m.q, m.mobility, 0.f));
	d.bridge.dense.perHz = 0.f;
	d.bridge.bridge.hz = 0.f;
	return d;
}


void BowedInstrument::setDesign(const Design& d) {
	wanted = d;
	strs.assign(d.strings.size(), BowedString());
	stoppedAt.assign(d.strings.size(), 0.f);
	bowing.assign(d.strings.size(), 0);
	setRate(engine, factor);
}


void BowedInstrument::setRate(float engineRate, int oversampling) {
	engine = engineRate;
	factor = oversampling < 1 ? 1 : oversampling;
	const float rate = engine * factor;
	for (size_t i = 0; i < strs.size(); i++) {
		strs[i].setSampleRate(rate);
		strs[i].setImpedance(wanted.strings[i].impedance);
		tuneString((int) i, stoppedAt[i] > 0.f ? stoppedAt[i] : wanted.strings[i].openHz);
	}
	voice.setSampleRate(engine);
	voice.setDesign(wanted.voice);
	coupler.setSampleRate(rate);
	coupler.setDesign(wanted.bridge);
	down.setFactor(factor);
}


/** A STRING AT A PITCH: the fundamental at Q 500, as strings on instruments measure, whatever the
note; the string's own losses as the benches set them. */
void BowedInstrument::tuneString(int i, float hz) {
	BowedString& s = strs[(size_t) i];
	s.setFrequency(hz);
	s.setDecay(6.91f * 500.f / (3.14159265f * hz));
	s.setDamping(0.3f);
}


int BowedInstrument::stringFor(float hz) const {
	int best = 0;
	for (size_t i = 0; i < wanted.strings.size(); i++)
		if (wanted.strings[i].openHz <= hz * 1.0005f)
			best = (int) i;
	return best;
}


void BowedInstrument::stop(int string, float hz) {
	if (string < 0 || string >= (int) strs.size())
		return;
	const float open = wanted.strings[(size_t) string].openHz;
	const bool open1 = std::fabs(hz / open - 1.f) < 0.0005f || hz < open;
	stoppedAt[(size_t) string] = open1 ? 0.f : hz;
	strs[(size_t) string].setStopped(!open1);
	tuneString(string, open1 ? open : hz);
}


void BowedInstrument::bow(int string, float force, float speed, float fromBridge) {
	if (string < 0 || string >= (int) strs.size())
		return;
	const float open = wanted.strings[(size_t) string].openHz;
	const float hz = stoppedAt[(size_t) string] > 0.f ? stoppedAt[(size_t) string] : open;
	// The finger shortens the string: the bow at the same distance from the bridge is a larger
	// share of what is left.
	const float vibrating = wanted.length * open / hz;
	BowedString& s = strs[(size_t) string];
	s.setContact(fromBridge / vibrating);
	s.setBow(force, speed);
	bowing[(size_t) string] = force > 0.f;
}


void BowedInstrument::pluck(int string, float level, float fromBridge) {
	if (string < 0 || string >= (int) strs.size())
		return;
	const float open = wanted.strings[(size_t) string].openHz;
	const float hz = stoppedAt[(size_t) string] > 0.f ? stoppedAt[(size_t) string] : open;
	strs[(size_t) string].setContact(fromBridge / (wanted.length * open / hz));
	strs[(size_t) string].pluck(level, 0.4f);
}


/** ONE SAMPLE AT THE ENGINE'S RATE, `factor` at the strings'. At each: every string's push on a
still bridge; the bridge's motion under all of them at once,

	v = (A sum F0 + c) / (1 + A sum Z),

A how fast the bridge moves per newton this sample, c how fast it would move unpushed, F0 each
string's push on a still bridge and Z its impedance, since a string pushes Z v the less when the
bridge gives way by v; then every string finished with that motion; and the force left on the bridge
carried down to the engine's rate for the body's voice. Nothing here is a delay. */
float BowedInstrument::process() {
	float sound = lastSound;
	for (int k = 0; k < factor; k++)
		step(&sound);
	return sound;
}


bool BowedInstrument::step(float* sound) {
	{
		// A string that does not feel the bridge pushes on it all the same, and gives nothing back.
		double push = 0.0, give = 0.0;
		for (size_t i = 0; i < strs.size(); i++) {
			push += strs[i].pushOnStillBridge();
			if (bowedFeels || !bowing[i])
				give += strs[i].impedance();
		}
		double v = 0.0;
		if (sympathetic) {
			const double a = coupler.velocityPerNewton(), c = coupler.velocityUnforced();
			v = (a * push + c) / (1.0 + a * give);
		}
		const double force = push - give * v;
		if (sympathetic)
			coupler.process((float) force);
		for (size_t i = 0; i < strs.size(); i++)
			strs[i].finishWithBridge((bowedFeels || !bowing[i]) ? (float) v : 0.f);
		lastBridge = v;
		lastForce = force;
		float slow;
		if (down.push((float) force, &slow)) {
			lastSound = voice.process(slow);
			*sound = lastSound;
			return true;
		}
	}
	return false;
}


} // namespace px
