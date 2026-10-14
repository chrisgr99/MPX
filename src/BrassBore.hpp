#pragma once
/** THE AIR COLUMN OF A BRASS INSTRUMENT, described and worked out in frequency: phase 2 of docs/brass.md.

WHAT IT IS FOR. The two air columns that play in time — a waveguide (BrassWaveguide) and a grid
(BrassGrid) — are checked against this, and this against measurements (research/brass/
trombone-benchmark.md, B1). It is the transmission-line calculation every impedance paper uses: each
section a two-port, multiplied from the bell back to the mouthpiece, at each frequency.

THE LOSSES, ONE WAY FOR ALL THREE. The air at the wall drags (viscosity) and exchanges heat (thermal
conduction) within a thin boundary layer, which in a tube of radius r makes the air heavier and more
compressible by terms in the square root of 1/(j omega):

    density times (1 + 2 sqrt(nu) / (r sqrt(j omega)))
    compressibility times (1 + 2 (gamma - 1) sqrt(nu / Pr) / (r sqrt(j omega)))

the leading terms of Keefe's (1984) expansions, and the Webster-Lokshin equation's in time (Hélie 2003),
so the grid's half-order derivatives and this calculation are the same physics. They give the familiar
attenuation of about 3 times 10 to the minus 5 times the square root of frequency over the radius, a
metre, and the slower travel of low frequencies that lowers the first resonances by a per cent or two.

NO RACK IN HERE. */
#include <complex>
#include <vector>

namespace px {


/** THE AIR INSIDE: its temperature sets everything else. Dry air at one atmosphere. */
struct BrassAir {
	double celsius = 20.0;
	double c = 343.2, rho = 1.2047, eta = 1.814e-5, gamma = 1.4017, prandtl = 0.7179;
	explicit BrassAir(double degrees = 20.0);
	double nu() const { return eta / rho; }
};


/** ONE SECTION OF THE BORE: a frustum of cone from radius `r0` at its mouthpiece end to `r1` at its
bell end, `length` long, metres; a cylinder where the two are equal. */
struct BrassSection {
	double length = 0.0, r0 = 0.0, r1 = 0.0;
};


/** THE MOUTHPIECE, LUMPED: the cup a volume, the throat and backbore an inertance with a resistance in
series, joining the cup to the start of the bore. The lips' flow goes into the cup. */
struct BrassMouthpiece {
	double cupVolume = 5.0e-6;       ///< cubic metres
	double inertance = 1700.0;       ///< kilograms a metre to the fourth: density times length over area
	double resistance = 2.0e5;       ///< pascal seconds a cubic metre
};


/** HOW THE BELL'S MOUTH RADIATES. SILVA: Silva et al.'s (2009) fit to Levine and Schwinger's unflanged
pipe, the best one-dimensional account. PARALLEL_RL: an inertance (the end correction, 0.6133 of the
radius) in parallel with the tube's characteristic resistance, the simplest circuit a grid can carry,
whose radiation resistance at low frequency is half as large again as Levine and Schwinger's. */
enum class BrassRadiation { SILVA, PARALLEL_RL };


/** A WHOLE AIR COLUMN: the mouthpiece, then the sections from the mouthpiece to the bell's mouth. */
struct BrassBore {
	BrassMouthpiece mouthpiece;
	std::vector<BrassSection> sections;
	BrassRadiation radiation = BrassRadiation::SILVA;

	/** THE TENOR TROMBONE OF SMYTH AND SCOTT (2011, Table 2; their bell's Bessel-horn fit, Table 1), the
	slide drawn out `extension` metres from first position — each outer tube that much longer, so the air
	column that much twice over — and the tuning slide pulled out `tuningPull` metres, as a player tunes,
	which lengthens it twice over too. The bell 0.502 m long from radius 10.0 mm to 108 mm. */
	static BrassBore trombone(double extension = 0.0, double tuningPull = 0.0);
	/** THE SAME WITH ITS MOUTHPIECE AND LEADPIPE AS TUBES, not lumped: a cup of 5 cubic centimetres as a
	cylinder 25.5 mm across; a throat 6.7 mm across and 7 mm long; a backbore widening to 11 mm over 70 mm;
	and a leadpipe widening from there to the slide's 13.8 mm over its first 0.23 m, which Smyth and Scott's
	table leaves out. Typical of a tenor's (Bach 6 1/2 AL, a leadpipe as the makers describe them), not
	measured. The lips' flow goes into the cup's open end. */
	static BrassBore tromboneWhole(double extension = 0.0, double tuningPull = 0.0);
	/** THE SAME FOR PLAYING IN TIME: the cup and the throat lumped, as a volume and an inertance — 17 mm of
	them is two points of a grid at 44.1 kHz — and the backbore and the leadpipe tubes as in
	tromboneWhole. */
	static BrassBore tromboneTimed(double extension = 0.0, double tuningPull = 0.0);
	/** NO LUMPED MOUTHPIECE: the flow goes into the first section. */
	bool lumped = true;
	/** THE THROAT'S INERTANCE worked out from its tubes and the air's density, not given. */
	bool inertanceFromGeometry = false;
	double mouthpieceInertance(const BrassAir& air) const;

	double length() const;
	/** The radius at the bell's mouth. */
	double mouthRadius() const { return sections.empty() ? 0.0 : sections.back().r1; }

	/** THE INPUT IMPEDANCE AT THE CUP, pascal seconds a cubic metre, at `hz`: the pressure in the cup for
	a unit of flow into it. */
	std::complex<double> impedance(double hz, const BrassAir& air) const;

	/** THE IMPEDANCE LOOKING INTO THE SECTIONS FROM `first` ON, at the start of section `first`, with the
	radiation at the end; the mouthpiece is not in it. */
	std::complex<double> impedanceFrom(size_t first, double hz, const BrassAir& air) const;
};


/** THE RADIATION IMPEDANCE of a mouth of radius `a`, as BrassBore::radiation says. */
std::complex<double> brassRadiationImpedance(double a, double hz, const BrassAir& air, BrassRadiation how);

/** A LOSSY CYLINDER'S series impedance and shunt admittance a metre, as the header describes. */
void brassLossyLine(double r, double omega, const BrassAir& air, std::complex<double>* series,
                    std::complex<double>* shunt);

/** THE PEAKS of |Z| over `hz` (rising), each refined on a parabola in log magnitude: frequencies and
magnitudes. */
void brassPeaks(const std::vector<double>& hz, const std::vector<double>& magnitude,
                std::vector<double>* peakHz, std::vector<double>* peakMagnitude);


} // namespace px
