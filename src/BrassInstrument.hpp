#pragma once
/** THE TROMBONE AS A PLAYER HOLDS IT: phase 6 of docs/brass.md.

The air column (BrassWaveguide), the lips (BrassHingedLips), the slide, the breath and the tongue, stepped
together one sample at a time, and what the listener hears out. The player (BrassPlayer) only moves the
controls; everything that sounds is here.

THE SLIDE is the extension from first position, metres. While it moves, the column is told every eighth of
a millisecond — its delays and its losses — so a glissando is continuous.

THE BREATH is the pressure in the mouth behind the tongue, pascals; THE TONGUE a valve between it and the
lips, nought shut against the teeth and one clear. The lips feel the breath in proportion to how far the
tongue is open: shut, they feel nothing and stop; released over a few milliseconds, they feel all of it at
once, which is what gives a tongued attack its edge (Hruška et al. 2015: a step of about 2 kPa for a sharp
mezzo-forte attack). Half open, the legato tongue, it softens the join while the slide moves.

THE LIPS are set by their resonance, hertz, as BrassHingedLips takes them.

NO RACK IN HERE. */
#include "BrassLips.hpp"
#include "BrassWaveguide.hpp"

#include <cmath>
#include <cstdint>

namespace px {


struct BrassInstrument {
	/** ASSEMBLED FIELD BY FIELD. */
	struct Design {
		BrassAir air = BrassAir(20.0);
		double rate = 48000.0;
		/** The tuning slide's pull, metres: 55 mm puts F3, B flat 3 and A flat 4 in first position within
		7 cents of A440 (src/TromboneTable.cpp was found with it). */
		double tuningPull = 0.055;
		BrassListener listener;
		bool steepening = true;
		BrassHingedLips::Shape lips;
		/** HOW MANY TIMES FASTER THAN `rate` the trombone is worked out: the lips' sharp corners alias at
		48 kHz (BrassHingedLips::Shape). Each sample is the mean of that many, low passed under 20 kHz. */
		int oversampling = 2;
		/** BREATH NOISE: the air through the lips is turbulent, and the recordings carry its noise between
		their harmonics, 26 to 45 dB under the whole, which also moves each cycle a little from the last. The
		flow through the lips is given up to this share of itself as noise, low passed at `breathNoiseHz`;
		the bore and the bell shape it as they shape the note. Turbulence grows far faster than the flow: as
		the square of the mouth's pressure, the whole share at `breathNoiseFull` pascals and at most half as
		much again. With the share alone, a tenth gave the recordings' noise at ff but 10 dB too much at mf
		and 15 to 25 at pp; grown with the breath over the lips' threshold instead, B flat 2, whose mf is two
		and a half times its threshold where B flat 3's is one and a half, had 10 dB too much at mf and ff
		(make brasstimbre). At 0.15, the noise between the harmonics is within a few decibels of the
		recordings' at mf and ff on most notes. Nought for none. */
		double breathNoise = 0.15, breathNoiseHz = 3000.0, breathNoiseFull = 2500.0;
	};
	void setup(const Design& d);
	const Design& design() const { return made; }

	/** THE PLAYER'S CONTROLS, each held until changed. */
	void setSlide(double metres);
	void setLips(double hz) {
		lipsSet = hz;
		lips.setResonance(hz);
	}
	void setBreath(double pascals) { breath = pascals; }
	/** The breath noise's share, as Design::breathNoise, changed while playing. */
	void setBreathNoise(double share) { made.breathNoise = std::fmax(0.0, share); }
	/** The lips' mass and quality open, as Design::lips has them, changed while playing. */
	void setLipMassAndQuality(double mass, double quality) { lips.setMassAndQuality(mass, quality); }
	/** The lips' gap at rest, metres, as Design::lips.restY has it, changed while playing. */
	void setLipGap(double metres) { lips.setRestGap(metres); }
	/** The note's breath, pascals, which holds a gap at rest open (BrassHingedLips::setGapBreath). */
	void setLipGapBreath(double pascals) { lips.setGapBreath(pascals); }
	void setTongue(double open) { tongue = open; }
	/** THE VOCAL TRACT's formants, nought to two, and the chest's resonances, three to five
	(BrassHingedLips::setFormant). */
	void setFormant(int k, double hz, double strength, double quality) {
		BrassHingedLips::Formant f;
		f.hz = hz;
		f.strength = strength;
		f.quality = quality;
		lips.setFormant(k, f);
	}
	/** The wave going into the bell, for tracing (BrassWaveguide::bellIn). */
	double bellInNow() const { return column.bellIn(); }
	/** The lips, for tracing (BrassHingedLips). */
	const BrassHingedLips& lipsNow() const { return lips; }
	/** The pressure in the mouth at the lips, pascals. */
	double mouthAtLips() const { return lips.mouthAtLips(); }

	/** ONE SAMPLE: the pressure at the listener, pascals. Should the model ever blow up — a number no longer
	finite — it starts again from rest, its controls as they were. */
	double step();

	double slide() const { return slideNow; }
	double lipsHz() const { return lipsSet; }
	double cupPressure() const { return column.cupPressure(); }
	double breathNow() const { return breath; }
	double tongueNow() const { return tongue; }
	/** The lips' opening's area now, square metres: nought shut. */
	double lipOpening() const { return lips.opening(); }

private:
	Design made;
	BrassWaveguide column;
	BrassHingedLips lips;
	double slideNow = 0.0, slideTold = 0.0, slideLengthTold = 0.0;
	int sinceTold = 0, tellEvery = 6;
	double breath = 0.0, tongue = 1.0, lipsSet = 100.0;
	/** THE LOW PASS FROM THE INNER RATE TO `rate`: its taps, and the inner samples it has seen, twice over so
	that a run of them can be read without wrapping. */
	std::vector<double> lowPass, inner;
	int innerAt = 0;
	/** The breath noise: its random sequence, the same every time it starts, and its low pass. */
	uint64_t noiseState = 0x9e3779b97f4a7c15ull;
	double noiseNow = 0.0, noisePole = 0.0;
	void designLowPass();
};


} // namespace px
