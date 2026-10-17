#pragma once
/** THE TROMBONE'S AIR COLUMN AS TRAVELLING WAVES WHERE IT IS A CYLINDER: the second of the two ways phase
2 of docs/brass.md builds it.

WHERE THE BORE FLARES, A GRID; WHERE IT DOES NOT, DELAY LINES. Two thirds of a trombone is the slide and
the gooseneck, tubes of one radius each, in which a wave travels unchanged but for the wall's losses: a
pair of delay lines carries it exactly, at the cost of reading two of them, however long the tube. Only
the backbore and leadpipe at one end and the tuning slide and bell at the other change their radius
along their length, and those are BrassGrids, 38 and about 100 points at 44.1 kHz against the whole
bore's 350. The grids meet the delay lines at ports (BrassGrid::Setup::startIsPort, endIsPort).

THE SLIDE IS A LENGTH. Its outer tubes are delay lines whose length is the slide's extension, read
between samples by third-order Lagrange interpolation, which keeps no state and so does not click when
the length moves, as an allpass interpolator does (trombone.md, 2.1). Each outer tube keeps 2 cm taken
from the inner tube beside it, so that it is never shorter than two samples while the slide is closed:
it moves the step in radius between them by that much, which nothing below several kilohertz can hear.

THE STEPS IN RADIUS ARE KEPT. The slide's tubes differ slightly, 6.9 mm inner, 7.2 outer, 7.4 the crook;
made one radius, the resonances moved by up to 30 cents (measured against BrassBore's calculation), so
each tube is its own pair of delay lines and they scatter where they meet, as Kelly and Lochbaum's do.

THE STEEPENED WAVE TO THE BELL RUNS BESIDE THE LINEAR ONE (BrassSteepeningDelay, Setup::steepening).
Through the slide the wave going to the bell is carried twice: as it would travel at no amplitude, which the
bell reflects back to the lips, and steepened, which is what the listener hears; at each junction the
steepened wave meets the same wave coming back as the linear one. So the loop that sets up the oscillation is
linear, as Hirschberg et al. (1996) found admissible. With the steepened wave reflected back to the lips
instead, loud notes went flat by up to 9 cents and, from about 5 kPa on F3, locked into a cycle repeating to
the last digit at a whole number of samples (273 at 48 kHz, 546 at 96).

THE LISTENER hears the wave entering the bell — the steepened one where it steepens — through the bell and
its radiation to where they are, as one filter (BrassListenerFilter, designed at setup from the bell's own
grid; BrassRadiation.hpp). The bell is fixed and linear, so the filter is exact but for its length.

THE WALL'S LOSSES OVER THE WHOLE RUN OF TUBES are applied once each way, as the wave leaves the run: the
half-order derivative of BrassGrid as the same sum of high-passes, each turned into the exponential of
itself by a Padé approximant — a first-order shelf, unity at nought, as much below it above its corner as
the losses there ask — and the shelves cascaded. Recomputed when the slide moves.

NO RACK IN HERE. */
#include "BrassBore.hpp"
#include "BrassGrid.hpp"
#include "BrassRadiation.hpp"

#include <cstdint>
#include <vector>

namespace px {


/** A DELAY LINE read between samples by third-order Lagrange interpolation: no shorter than two samples. */
struct BrassDelay {
	void setup(int size);
	/** The value written `delay` samples ago, between samples, before this step's write. */
	double read(double delay) const;
	void write(double x) {
		line[(size_t) at] = x;
		at = (at + 1) & mask;
	}

private:
	std::vector<double> line;
	int at = 0, mask = 0;
};


/** A DELAY LINE IN WHICH A LOUD WAVE STEEPENS: phase 4 of docs/brass.md.

THE SPEED OF A WAVE DEPENDS ON ITS OWN PRESSURE. In a plane wave the air moves with velocity p / (rho c),
and the sound travels at c plus beta times that, beta = (gamma + 1) / 2, about 1.2: the crests a little
faster, the troughs a little slower, so that over a long cylinder each crest gains on the trough ahead of
it and the front between them steepens, at fortissimo nearly into a shock by the end of a trombone's slide
(Hirschberg, Gilbert, Msallam and Wijnands 1996). This is where a brass instrument's brassiness comes from,
not the lips (Stevenson et al. 2009).

EACH SAMPLE ARRIVES WHEN ITS OWN SPEED SAYS: written at time n, it arrives at n + D / (1 + beta p /
(rho c^2)) samples, D the tube's delay. The pressure that sets the speed is the wave's sound, its slow mean
(the steady flow's part, under about 10 Hz) taken off. (Vergez and Rodet 2000; Msallam et al. 2000;
trombone.md, 4.4.)

A SAMPLE THAT WOULD OVERTAKE THE ONE AHEAD OF IT JOINS IT: the front has become a shock. The samples that
meet travel on together at the mean of their speeds, arriving at the mean of the times each would have
arrived alone — sticky particles, which give the entropy solution of Burgers' equation (E, Rykov and Sinai
1996), the shock moving at the mean of the speeds either side of it as Rankine and Hugoniot's condition
asks. Held to the time of the slower sample instead, the shock lost where it fell between two samples in
each of the slide's seven tubes, and a loud note's period locked to a whole number of samples (F3 at
5.4 kPa, exactly 273 at 48 kHz).

WHAT COMES OUT AT EACH STEP IS THE MEAN OF THE WAVE OVER THAT STEP: the curve through the arrivals — a
cubic through four of them, Catmull and Rom's on their uneven times, or a straight line between two that
arrive within half a step of each other — integrated from one step to the next. A shock arriving between
two steps is shared between them by where it falls. Read at the step instead, it landed whole on the next
one, and the samples could not say when it came. The mean is centred half a step back, so each sample is sent half a
step sooner; it costs a little of the top octave, 1.5 dB at 15 kHz at 48 kHz.

ONLY THE WAVE GOING TO THE BELL, AND OUT OF THE LOOP, as Hirschberg et al. found admissible: the
nonlinearity matters for what reaches the listener and can be left out of the loop that sets up the
oscillation (BrassWaveguide keeps a linear wave to the bell beside the steepened one; see there). */
struct BrassSteepeningDelay {
	/** Room for `size` samples in flight; `beta` over rho c^2, per pascal. */
	void setup(int size, double steepness, double rate);
	/** THIS STEP'S SAMPLE, travelling `delay` samples at rest. */
	void write(double x, double delay);
	/** WHAT ARRIVED OVER THE STEP JUST ENDED, its mean, then the clock moves on a step. */
	double read();

private:
	/** The integral of the curve between arrivals `i` and `i` + 1, from `from` to `to`. */
	double span(int64_t i, double from, double to) const;
	std::vector<double> arrival, value;
	/** The groups of samples travelling together, oldest to newest, each with the sum of the times its
	samples would have arrived alone. */
	struct Cluster {
		int64_t first = 0;
		int count = 1;
		double sumOwn = 0.0;
	};
	std::vector<Cluster> clusters;
	int64_t oldest = 0, newest = 0;
	int mask = 0;
	int64_t head = 0, cursor = 0;
	double now = 0.0, steep = 0.0, slow = 0.0, slowPole = 0.0;
};


/** THE WALL'S LOSSES ALONG A TUBE as a cascade of first-order shelves (see the header). */
struct BrassLossShelves {
	/** For a run of tube whose losses add up to `beta`, seconds to the half: the sum over its pieces of each
	one's length times (sqrt(nu) / r) (1 + (gamma - 1) / sqrt(Pr)) / c. */
	void design(double beta, const std::vector<double>& xi, const std::vector<double>& mu, double rate);
	/** THE SHELVES PLANNED for the longest run the tube can be, `betaMost`, each of the half-order terms
	split into as many as that needs: a redesign that split one into more while sounding could not carry
	over what the shelves held, and clicked (the slide out from A3 to B3). */
	void plan(double betaMost, const std::vector<double>& mu);
	double process(double x);

private:
	std::vector<int> planned;
	struct Shelf {
		double b0 = 1.0, b1 = 0.0, a1 = 0.0, x1 = 0.0, y1 = 0.0;
	};
	std::vector<Shelf> shelves;
};


struct BrassWaveguide {
	/** ASSEMBLED FIELD BY FIELD. */
	struct Setup {
		BrassAir air;
		double rate = 44100.0;
		/** The half-order derivative's memory variables, in the grids and the shelves. */
		int lossTerms = 4;
		/** The slide's extension from first position and the tuning slide's pull, metres. */
		double extension = 0.0, tuningPull = 0.0;
		/** THE WAVE GOING TO THE BELL STEEPENS (BrassSteepeningDelay); off, it travels as it would at no
		amplitude. */
		bool steepening = true;
		/** Where the listener is (BrassRadiation.hpp). */
		BrassListener listener;
		/** THE INSTRUMENT: the trombone, with its slide; or the trumpet, with its valves, set as `trumpet` says
		(BrassBore::trumpetTimed; its pull in place of `tuningPull`). */
		enum Instrument { TROMBONE, TRUMPET } instrument = TROMBONE;
		BrassTrumpetSetting trumpet;
	};
	void setup(const Setup& s);

	/** THE SLIDE MOVED, `extension` metres from first position: the outer tubes' lengths, and the losses
	unless `losses` is false — the lengths alone, for each step of a moving slide, the losses changing slowly
	enough to follow it more seldom. */
	void setExtension(double extension, bool losses = true);
	/** A VALVE MOVED, the first nought: `down` nought up, one pressed, between them part-way, the air going
	both ways in proportion. */
	void setValve(int valve, double down);
	/** THE FIRST AND THIRD VALVES' SLIDES, each lengthening its loop twice over, metres. */
	void setValveSlides(double slide1, double slide3);

	/** ONE STEP: `flow` cubic metres a second into the cup; the cup's pressure after it, pascals. */
	double step(double flow);
	/** THE SAME IN TWO HALVES, for the lips (BrassGrid::begin, end). */
	void begin(double* cupWithout, double* cupPerFlow);
	double end(double flow);
	/** The cup's pressure now, pascals. */
	double cupPressure() const { return cupNow; }
	/** At the bell's mouth, in the loop: the wave at no amplitude. */
	double mouthPressure() const { return back.mouthPressure(); }
	double radiatedFlow() const { return back.radiatedFlow(); }
	/** THE PRESSURE AT THE LISTENER after the last step, pascals. */
	double listenerPressure() const { return heardNow; }
	/** The wave going into the bell, pascals, before the bell and the listener: for tracing. */
	double bellIn() const { return heardIn; }
	/** The wave entering the bell that the listener's filter takes, for listening from elsewhere. */
	double bellInput() const { return heardIn; }
	int listenerTaps() const { return listener.taps(); }
	const std::vector<double>& listenerCoefficients() const { return listener.coefficients(); }
	int gridPoints() const { return front.points() + back.points(); }

private:
	BrassAir air;
	double rate = 44100.0;
	BrassGrid front, back;
	BrassListenerFilter listener;
	double heardNow = 0.0, heardIn = 0.0;
	struct Tube {
		double length = 0.0, radius = 0.0;
		bool outer = false;
		/** A valve's path: the share of the air it takes, by how far the valve is down; one elsewhere. */
		double share = 1.0;
		BrassDelay forward, backward;
		BrassSteepeningDelay steepening;
		double delay = 2.0;
	};
	bool steepens = true;
	std::vector<Tube> tubes;
	/** THE RUN AS STAGES, mouthpiece to bell, each one tube or a valve's two paths side by side — its straight
	path first, then its loop — the tubes of each in order in `tubes`. */
	struct Stage {
		int first = 0, count = 1, valve = -1;
	};
	std::vector<Stage> stages;
	/** The valves' loops past their straight paths, and the slides', metres. */
	double loops[3] = {0.0, 0.0, 0.0}, valveSlides[3] = {0.0, 0.0, 0.0};
	/** Each junction between two stages of one tube each: the pressure reflection, for a wave going on from
	the stage before it. */
	std::vector<double> reflection;
	std::vector<double> xi, mu;
	BrassLossShelves lossOn, lossBack, lossHeard;
	std::vector<double> arrivingRight, arrivingLeft, heardRight;
	double cupNow = 0.0;
	void retune();
};


} // namespace px
