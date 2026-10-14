#pragma once
/** THE LIPS: phase 3 of docs/brass.md.

ONE MASS ON A SPRING, BLOWN OPEN. The lip's displacement y obeys

    M (y'' + (omega / Q) y' + omega^2 y) = S dp + F

where dp is the mouth's pressure less the cup's, which pushes the lips open (they strike outward), S the
lip's area that pressure acts on, omega its resonance, Q its quality, and F what the other lip does when
they meet. The opening between them is H0 + y, nought where they are shut. (Adachi and Sato 1995, 1996;
Harrison et al. 2015; research/brass/physics-and-methods.md, section 2.)

THE FLOW INTO THE CUP is Bernoulli's through the opening — its width times its height times the square
root of twice dp over the air's density, signed with dp — and the flow the moving lip sweeps along with
it, S times y'. The jet loses all its energy in the cup, as Hirschberg and Vergez and Rodet assume.

WHEN THE LIPS MEET (H0 + y below nought) they stiffen and damp: three times the stiffness acts on how far
they are pressed together, and the quality falls to a half (Adachi and Sato 1996, Table I).

ONE NUMBER FOR THE EMBOUCHURE. The lip's resonance is what the player sets; its mass falls and its
stiffness rises with it as Elliott and Bowsher found, the vibrating mass inversely proportional to the
resonance (Adachi and Sato 1996, section II): M = mass at the reference resonance times the reference over
the resonance.

STEPPED WITH THE AIR COLUMN. The displacement is stepped from the last step's pressure difference; then the
flow and the pressure difference over the step are solved together with the cup, whose pressure after the
step is a straight line in the flow (BrassWaveguide::begin), so the lips feel the cup within the step: a
quadratic in the square root of the pressure difference, solved exactly.

NO RACK IN HERE. */
#include "BrassBore.hpp"

#include <cmath>

namespace px {


struct BrassLips {
	/** ASSEMBLED FIELD BY FIELD. The values are Harrison-Harsley's (2018) as Willemsen et al. (2021) used
	them for the trombone, with Adachi and Sato's closure and a quality chosen between theirs (3) and
	Velut et al.'s (7). */
	struct Shape {
		double massAtReference = 5.37e-5;   ///< kilograms
		double referenceHz = 300.0;
		double quality = 5.0, qualityShut = 0.5;
		double area = 1.46e-5;              ///< square metres pressure acts on
		double width = 0.010;               ///< metres
		double restOpening = 0.29e-3;       ///< metres, the opening at rest
		double shutStiffness = 3.0;         ///< times the lip's own, on how far they are pressed together
	};
	void setup(const Shape& s, const BrassAir& air, double rate);

	/** THE PLAYER'S TWO CONTROLS: the lip's resonance, hertz, and the pressure in the mouth, pascals. */
	void setResonance(double hz);
	void setMouthPressure(double pascals) { mouth = pascals; }

	/** ONE STEP, given the cup: its pressure now and after the step as a straight line in the flow. The
	flow into the cup over the step, cubic metres a second. */
	double step(double cupNow, double cupWithout, double cupPerFlow);

	double opening() const { return shape.restOpening + y; }
	double pressureDifference() const { return lastDp; }

private:
	Shape shape;
	BrassAir air;
	double k = 1.0 / 48000.0;
	double y = 0.0, yPrev = 0.0, lastDp = 0.0;
	double mouth = 0.0;
	double omega = 2.0 * 3.14159265358979 * 200.0, mass = 1e-4;
};


/** THE LIPS AS A HINGED BLOCK THAT SWINGS AND STRETCHES: Adachi and Sato (1996, the ATR preprint TR-H-0177).

WHY A SECOND MODEL. The one mass blown open (BrassLips) plays only above the air column's resonance: on F3
it could not go below the resonance at all, where trombonists bend a note about a tone below it (Boutin et
al. 2015, 2020), and of 48 combinations of quality, mass, width and opening only two started a note below
5 kPa, 7.6 and 8.6 per cent sharp. Players' lips move in two directions at once (Boutin et al. 2015).

THE LIP, seen from the side, is a parallelogram ABCD: AB, its base, a free joint at the mouthpiece's rim;
BC and AD its length l, which stretches; AB and DC its thickness d, rigid. Its tip C is at xi = (x, y), x
along the flow into the mouthpiece and y across it from where the two lips meet, the joint B at a fixed
xi_joint, so l = |xi - xi_joint|. Its mass m at its middle moves half as far as the tip:

    (1/2) m xi'' = -(1/2) (sqrt(m k) / Q) xi' - (1/2) k (xi - xi_rest) + F_dp + F_Bernoulli

where F_dp = b (p0 - p) (xi - xi_joint)^perp, (A_x, A_y)^perp being (-A_y, A_x): the mouth's pressure less
the cup's, on the lip's length, at right angles to it — mostly downstream, the swing that blows it open;
and F_Bernoulli = b d p_lip in y, the pressure in the opening on the lip's inner face, which falls as the
air rushes through and so draws the lips together — the stretching that lets a note sound below the
resonance. b is the lip's breadth. Shut (y below nought), the lips press back with three times k on y, as
Adachi and Sato's equation has it, and Q falls to a half — gradually, as the corners meet (below).

THE LIPS SHUT FROM THE CORNERS. Real lips do not meet along their whole breadth at once: the opening is
an eye shape that narrows as it thins, so that its area falls smoothly to nought (Vergez and Rodet 2000, a
lip length depending on the opening through a hyperbolic tangent; Fletcher and Tarnopolsky 1999, an area
growing as the height to the power 1.5; Bromage et al. 2010, as its square at low notes). Here the open
share of the breadth is w = tanh(y / y_c), y_c the corners' height, so the opening's area is 2 b y w:
2 b y^2 / y_c while the lips are nearly shut, 2 b y once they are well apart. The damping rises with the
shut share, 1 / Q = w / Q_open + (1 - w) / Q_shut, as Vergez and Rodet let theirs vary with the closure.
With the breadth shut and the damping changed at a stroke instead, the flow had a corner and the force a
jump each time the lips met, which the samples could not carry: between the harmonics of a held note at
48 kHz there was power 24 to 33 dB under the harmonics below 2 kHz, now 44 to 66 (make brasslips
ARGS=aliasing); most of it came from the damping's jump.

THE FLOW. The opening's area is S = 2 b y w. Through it the acoustic flow U obeys, with the jet losing its energy in the
cup and recovering a little pressure where it widens into the cup's area S_cup,

    p0 - p = rho U |U| (1 / (2 S^2) - 1 / (S_cup S) + 1 / S_cup^2) + (rho d / S) dU/dt

and the lip sweeps a flow b ((x - x_joint) y' - (y - y_joint) x') into the cup as it moves. The pressure in
the opening is the cup's less rho U^2 (1 / (S_cup S) - 1 / S_cup^2); on the inner face it acts on the open
share w of the breadth, and the mouth's pressure on the shut share, so the force does not jump as they meet.

THE EMBOUCHURE is the resonance f: m = mass / ((2 pi)^2 f) and k = mass f, Adachi and Sato's 1.5 for a
trumpet; mass inversely and stiffness directly proportional to f, after Elliott and Bowsher.

STEPPED as BrassLips is: the lip from the last step's forces, then the flow solved with the cup's pressure
as a straight line in it — a quadratic in the flow, whose root is taken in the form that stays finite as
the opening closes. */
struct BrassHingedLips {
	/** ASSEMBLED FIELD BY FIELD. Adachi and Sato's Table I values for a trumpet are breadth 7 mm,
	thickness 2 mm, joint (0, 4 mm), rest (1 mm, 0 to 0.02 mm), cup entry 2.3 square centimetres, Q 3 and
	0.5, mass 1.5. The defaults here are the trombone's: their thickness and joint times the ratio of the
	mouthpieces' rims, 25.5 mm to 16.8, about one and a half; the breadth 14 mm, the mass 0.9 and the
	corners' height 0.1 mm, chosen against the benchmark (research/brass/trombone-benchmark.md, B2 and B4)
	from sweeps of mass 0.5 to 1.5, breadth 10.5 to 17.5 mm and corners 0 to 0.4 mm (make brasslips
	ARGS=sweep, ARGS=corners): F3 starts at 940 Pa and B flat 3 at 1140, so that mf falls near the 1.3 kPa
	trombonists use. Lighter lips started lower but opened to 400 square millimetres and more at 5.4 kPa,
	where F3 left its resonance; higher corners cost more in threshold than they gained against aliasing;
	with Adachi and Sato's 1.5 and 10.5 mm the notes started at 1440 and 2020. */
	struct Shape {
		double breadth = 0.014, thickness = 0.003;
		double jointX = 0.0, jointY = 0.006;
		double restX = 0.0015, restY = 0.0;
		double cupArea = 5.1e-4;
		double quality = 3.0, qualityShut = 0.5;
		double mass = 0.9;   ///< m (2 pi)^2 f and k / f, kilograms a second, newtons a metre a hertz
		double shutStiffness = 3.0;
		/** The corners' height y_c, metres: under it the opening narrows as it thins, and the damping rises
		towards the shut lip's. Nought shuts the whole breadth at once. */
		double cornerHeight = 0.1e-3;
	};
	void setup(const Shape& s, const BrassAir& air, double rate);
	void setResonance(double hz);
	void setMouthPressure(double pascals) { mouth = pascals; }
	double step(double cupNow, double cupWithout, double cupPerFlow);
	/** The opening's area, square metres. */
	double opening() const { return areaAt(y); }
	bool blendDamping = true;

private:
	Shape shape;
	BrassAir air;
	double k = 1.0 / 48000.0;
	double x = 0.0, y = 0.0, xPrev = 0.0, yPrev = 0.0;
	double flow = 0.0, inOpening = 0.0;
	double mouth = 0.0;
	double m = 1e-4, stiffness = 100.0;
	/** The open share of the breadth at half-opening `h`. */
	double openShare(double h) const {
		if (h <= 0.0)
			return 0.0;
		return shape.cornerHeight > 0.0 ? std::tanh(h / shape.cornerHeight) : 1.0;
	}
	double areaAt(double h) const { return 2.0 * shape.breadth * std::fmax(0.0, h) * openShare(h); }
};


} // namespace px
