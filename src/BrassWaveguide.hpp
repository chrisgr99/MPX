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

THE WALL'S LOSSES OVER THE WHOLE RUN OF TUBES are applied once each way, as the wave leaves the run: the
half-order derivative of BrassGrid as the same sum of high-passes, each turned into the exponential of
itself by a Padé approximant — a first-order shelf, unity at nought, as much below it above its corner as
the losses there ask — and the shelves cascaded. Recomputed when the slide moves.

NO RACK IN HERE. */
#include "BrassBore.hpp"
#include "BrassGrid.hpp"

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


/** THE WALL'S LOSSES ALONG A TUBE as a cascade of first-order shelves (see the header). */
struct BrassLossShelves {
	/** For a run of tube whose losses add up to `beta`, seconds to the half: the sum over its pieces of each
	one's length times (sqrt(nu) / r) (1 + (gamma - 1) / sqrt(Pr)) / c. */
	void design(double beta, const std::vector<double>& xi, const std::vector<double>& mu, double rate);
	double process(double x);

private:
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
	};
	void setup(const Setup& s);

	/** THE SLIDE MOVED, `extension` metres from first position: the outer tubes' lengths and the losses. */
	void setExtension(double extension);

	/** ONE STEP: `flow` cubic metres a second into the cup; the cup's pressure after it, pascals. */
	double step(double flow);
	double mouthPressure() const { return back.mouthPressure(); }
	int gridPoints() const { return front.points() + back.points(); }

private:
	BrassAir air;
	double rate = 44100.0;
	BrassGrid front, back;
	struct Tube {
		double length = 0.0, radius = 0.0;
		bool outer = false;
		BrassDelay forward, backward;
		double delay = 2.0;
	};
	std::vector<Tube> tubes;
	/** Each junction's pressure reflection, for a wave going on from the tube before it. */
	std::vector<double> reflection;
	std::vector<double> xi, mu;
	BrassLossShelves lossOn, lossBack;
	std::vector<double> arrivingRight, arrivingLeft;
	double baseLength[2] = {0.0, 0.0};
	void retune();
};


} // namespace px
