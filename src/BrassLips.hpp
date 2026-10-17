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
48 kHz there was power 24 to 33 dB under the harmonics below 2 kHz, now 44 to 65 (make brasslips
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
	mouthpieces' rims, 25.5 mm to 16.8, about one and a half; the breadth 14 mm and the mass 0.9, chosen
	against the benchmark (research/brass/trombone-benchmark.md, B2 and B4) from sweeps of mass 0.5 to 1.5
	and breadth 10.5 to 17.5 mm (make brasslips ARGS=sweep). Lighter lips started lower but opened to 400
	square millimetres and more at 5.4 kPa, where F3 left its resonance; with Adachi and Sato's 1.5 and
	10.5 mm the notes started at 1440 and 2020.

	THE CORNERS FIRM AS THE PLAYER BLOWS HARDER, for the sound's upper harmonics, which come from how sharply
	the flow is cut off as the lips meet: 0.1 mm high up to the lips' threshold, 0.02 mm from 1.3 times it,
	the lips just meeting at rest (make brasstimbre). With corners of 0.1 mm throughout, F4 at mf never shut
	and its 6th to 12th harmonics were 27 dB under the Iowa recording's, its 13th to 20th 63; with 0.02 mm
	throughout, mf matched but pp was 17 to 28 dB too bright, and pressed together at rest, as the
	embouchure might seem to ask, more so — and pressed 0.3 mm the lips did not sound at pp at all. Firming,
	B flat 2 to B flat 3 at mf are within 4 dB of the recordings in every group of harmonics and F4 within
	8, pp as dark as before. The sharp corners alias at 48 kHz, 35 to 48 dB under the harmonics, so the
	trombone is worked out at twice the rate (BrassInstrument::Design::oversampling), where it is 46 to 64
	dB under. */
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
		double cornerHeight = 0.02e-3;
		/** THE EMBOUCHURE FIRMING AS THE PLAYER BLOWS HARDER: where `cornerSoft` is more than nought, the
		corners are that high while the mouth's pressure is up to `firmFrom` times the lips' threshold and
		`cornerHeight` from `firmTo` times it, between them on a straight line in the logarithms. The
		threshold is taken as `thresholdPerHz` pascals for each hertz of the lips' resonance: the tables find
		about 3.1 for each of the bore's, the lips at about 0.85 of it. */
		double cornerSoft = 0.1e-3, firmFrom = 1.0, firmTo = 1.3, thresholdPerHz = 3.6;
	};
	void setup(const Shape& s, const BrassAir& air, double rate);
	void setResonance(double hz);
	/** THE LIPS' MASS AND QUALITY OPEN changed while playing, as Shape has them: a player's lips firmer or looser
	from note to note. */
	/** THEIR GAP AT REST changed while playing, as Shape::restY: a player's lips set further apart or pressed
	closer from note to note. */
	void setRestGap(double y) { shape.restY = y; }
	/** THE GAP AT REST HELD OPEN BY THE BREATH: lips set apart at rest are apart because the player blows
	them so. With the note's breath, pascals, given here, a gap of more than nought is whole while the
	pressure behind the lips is at least four fifths of that, and closes in proportion below it — shut
	with no air, as lips are. Held at a fixed gap with the tongue shut instead, A3's lips at pp, 0.09 mm
	apart, let the surge in the instrument push air through them as it came back from the bell, and the
	air through them rang at a few kilohertz, which the bell made a click (A3 to B3 at pp). Nought, a fixed
	gap; lips pressed together at rest stay so. */
	void setGapBreath(double pascals) { gapBreath = pascals; }
	void setMassAndQuality(double mass, double quality) {
		shape.mass = mass;
		shape.quality = quality;
		setResonance(lipHz);
	}
	void setMouthPressure(double pascals) { mouth = pascals; }
	/** THE TONGUE between the vocal tract and the lips, nought shut to one open: what the tract holds back
	reaches the lips in that share, and the flow through them drives the tract in that share. Shut, the lips
	are cut off from the tract as from the breath (BrassInstrument scales the breath itself). */
	void setTongue(double open) { tongueShare = std::fmin(1.0, std::fmax(0.0, open)); }
	/** THE VOCAL TRACT, as the lips feel it from the mouth's side: the player's mouth, throat and tongue as
	three resonances, the formants, between the lungs' steady pressure and the lips. The air the lips let
	through draws on each, and the pressure in the mouth at the lips is the lungs' less what the three hold
	back:

	    p_mouth = p_lungs - sum over k of Z_k U,  Z_k(s) = R_k (s w_k / Q_k) / (s^2 + s w_k / Q_k + w_k^2)

	each a resonance whose impedance is R_k, its strength, pascal seconds a cubic metre, at its frequency w_k,
	and falls away either side by its quality Q_k; by the bilinear transform, its frequency prewarped. A
	formant near the note lets the lips give more of their flow to the bore at the same breath (Fréour et
	al. 2015, an artificial trombone player with a vocal tract under active control), and players feel it
	pull the pitch, steady it and tip the note onto another resonance. Strength nought, none: the mouth's
	pressure the lungs'. Three more, three to five, the chest's below the open throat: the windpipe and the
	lungs, whose resonances in speech are near 600, 1400 and 2200 Hz in men, broad for the lungs' losses. */
	struct Formant {
		double hz = 500.0, strength = 0.0, quality = 6.0;
	};
	void setFormant(int k, const Formant& f);
	double step(double cupNow, double cupWithout, double cupPerFlow);
	/** The opening's area, square metres. */
	double opening() const { return areaAt(y); }
	/** For tracing: the lip's tip across (y) and along (x), metres; the flow through the opening, cubic metres
	a second; and the pressure in the opening on the lip's inner face, pascals. */
	double tipY() const { return y; }
	double tipX() const { return x; }
	double flowThrough() const { return flow; }
	double openingPressure() const { return inOpening; }
	/** The pressure in the mouth at the lips now, pascals: the lungs' less the vocal tract's. */
	double mouthAtLips() const { return mouth - tongueShare * tractHeld; }
	bool blendDamping = true;

private:
	Shape shape;
	BrassAir air;
	double k = 1.0 / 48000.0;
	double x = 0.0, y = 0.0, xPrev = 0.0, yPrev = 0.0;
	double flow = 0.0, inOpening = 0.0;
	double mouth = 0.0;
	/** Each formant's biquad, y = b0 u + b2 u'' - a1 y' - a2 y'', u the flow and y the pressure it holds back;
	and what the three held back at the last step, pascals. */
	struct Resonance {
		bool on = false;
		double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
		double u1 = 0.0, u2 = 0.0, y1 = 0.0, y2 = 0.0;
	};
	Resonance tract[6];
	double tractHeld = 0.0, tongueShare = 1.0, gapBreath = 0.0;
	double m = 1e-4, stiffness = 100.0;
	/** The lips' resonance, hertz, and the corners' height now, metres. */
	double lipHz = 200.0, corner = 0.02e-3;
	/** The open share of the breadth at half-opening `h`. */
	double openShare(double h) const {
		if (h <= 0.0)
			return 0.0;
		return corner > 0.0 ? std::tanh(h / corner) : 1.0;
	}
	double areaAt(double h) const { return 2.0 * shape.breadth * std::fmax(0.0, h) * openShare(h); }
	double k_() const { return k; }
};


} // namespace px
