#pragma once
/** THE PLAYER: notes in, a bowed instrument played. Phase 7 of docs/bowed-strings.md.

THE SCORE SAYS WHAT IS WRITTEN, THE PLAYER DECIDES WHAT THE BOW AND THE LEFT HAND DO, THE INSTRUMENT
DECIDES WHAT THAT SOUNDS LIKE. This is the middle of the three: which string, which way the bow goes,
how fast, how hard and where, when the bow changes and how, where the finger goes and whether it
vibrates. What it decides it decides from the music and from measurements of players; where the
measurements ran out, by ear.

IT READS AHEAD. A bow change begins before the note it belongs to — the bow slows to a stop at the
end of the last note — so the player has to know a note is coming before it sounds, as a player
reading a part does. Notes are given to it with the moment they start, ahead of time; it plans each
one a little before then (PREPARE) and plays the plan. A note that arrives too late to be planned
ahead is played as well as it can be from where things stand.

IT REHEARSES. Under the friction curve almost no plain attack starts cleanly, and a bow change made
the same way every time leaves the string slipping several times a period for up to a few hundred
milliseconds (research/bowed-strings/bow-changes.md). So before a note starts from rest the player
tries attacks on a copy of the instrument as it stands, and before a bow change it tries nine ways of
changing on a copy, and plays the one that is back to one slip a period soonest. The copy is exact,
so what was rehearsed is what is played.

ON TWO THREADS IN A MODULE. Rehearsing takes far longer than a sample, so a module runs two copies:
a PLANNER on a thread of its own, deciding and rehearsing ahead through the notes the cable announces,
and a PLAYING copy on the audio thread carrying out what the planner decided. Each decision — a note
started, or the bow lifted at a note's end — is numbered, and kept by the planner as a Plan: the
actions it scheduled and the player's bookkeeping after it. The playing copy, reaching the same
decision at the same sample, takes the plan instead of deciding; where none is there in time, or it
does not fit, it decides for itself without rehearsing, from the starts rehearsal found for notes
like it, and the planner starts again from the playing copy as it then stands. See BowedEngine.hpp.

NO RACK IN HERE, so a bench can play a score through it and measure what happens. */
#include "BowedInstrument.hpp"
#include "BowedRehearsal.hpp"
#include "BowedTable.hpp"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace px {


struct Crew;

struct BowedPlayer {
	/** ONE NOTE, as a score gives it, assembled field by field. */
	struct Note {
		float hz = 440.f;
		/** HOW LOUD, on the cable's ladder: ppp nought, each marking a seventh, fff one. */
		float level = 4.f / 7.f;
		/** How long it is written, seconds. */
		float seconds = 0.5f;
		/** RUNS INTO THE NEXT NOTE IN THE SAME BOW: a slur over it that does not end on it. */
		bool slurred = false;
		/** HOW LONG THE BOW STROKE IT BEGINS OR CONTINUES STILL HAS TO LAST: from this note's start to
		the end of the last note under its slur, seconds; below nought where it is under no slur or
		the source cannot say. What the bow is shared out against. */
		float slurSeconds = -1.f;
		bool staccato = false, tenuto = false, accent = false;
		/** A BOW MARKED IN THE PART: one a down-bow, minus one an up-bow, nought none. */
		int bow = 0;
		/** Whether it falls on the first beat of a bar, where a stroke from the string begins down. */
		bool downbeat = false;
		/** WHAT THE PART SAYS ABOUT VIBRATO, which is usually nothing: then the player decides. */
		enum Vibrato : uint8_t { UNSAID, SENZA, ORDINARY, MOLTO };
		uint8_t vibrato = UNSAID;
		/** HELD UNTIL LET GO: it sounds until endNow, however long it is written, which is then only what
		the bow is drawn for. For a gate, which says nothing of how long a note will be. */
		bool held = false;
		/** JOINED UNDER ONE BOW TO THE NOTE BEFORE, said by this note rather than by that one: a held
		note's pitch moving while its gate stays up. */
		bool fromSlur = false;
	};

	/** WHEN THE MUSIC WAS WRITTEN, for the habits that changed with it — vibrato most of all. */
	enum Era : uint8_t { BAROQUE, CLASSICAL, ROMANTIC, MODERN };

	/** REHEARSING A FEW: in place of the shortlist found on the string alone and the six glides, only
	these tried on the instrument and chosen among as rehearsal chooses — bites, each its force over the
	note's, its acceleration as twelve times the logarithm of its share of what Guettler's limits allow
	and its place as a share of the planned one (1 where none is given); and glides, each its length,
	seconds, and the share of the force it starts from. None of either, the shortlist and the six.
	Assembled field by field. */
	struct FewStarts {
		std::vector<float> bites, accels, shares;
		std::vector<float> glideRamps, glideFloors;
		bool any() const { return !bites.empty() || !glideRamps.empty(); }
	};

	/** THE PLAYER'S HABITS, assembled field by field. Every number is a measurement of players or
	was chosen by ear; see each. violin() and cello() give the two that have been listened to. */
	struct Habits {
		/** THE BOW'S SPEED FOR A DYNAMIC, metres a second, `speedPiano` at piano to `speedForte` at
		forte, straight on a logarithmic scale against the ladder. THE LOUDNESS IS IN THE SPEED: a
		string's sound is the bow's speed over its distance from the bridge, and the force only has to
		keep the string speaking. Pressing harder for a louder note, as the measured players' forte does
		(Askenfelt: 0.4 to 0.5 N at piano, 1.7 to 2.3 N at forte, on strings near 0.2 kg/s), makes the
		short length between the bow and the bridge ring at its own pitch, far above the note's — a
		squeak, heard on nearly every forte note — and putting the loudness in the speed almost removed it
		(docs/bowed-strings.md, phase 7). Within the 0.15 to 0.4 m/s the measured cellists and violinists
		drew at. */
		float speedPiano = 0.15f, speedForte = 0.4f;
		/** HOW MUCH HAIR THERE IS TO DRAW, metres. */
		float bowLength = 0.65f;
		/** THE FORCE FOR A DYNAMIC, as a multiple of the string's wave impedance at that dynamic's speed,
		and in proportion to the speed the bow is actually drawn at: twice the impedance or so, where the
		bow-to-bridge ripple stays 20 dB and more below the note. Between and beyond piano and forte,
		straight on a logarithmic scale against the ladder, held within `forceLeast` and `forceMost`. */
		float forcePiano = 1.8f, forceForte = 2.5f, forceLeast = 0.9f, forceMost = 4.f;
		/** WHERE THE BOW IS FOR A DYNAMIC, as a fraction of the vibrating length, so the bow comes nearer
		the bridge as the finger shortens the string — by ear, on the violin's A string, the stopped
		notes then sounded as full as the open one — and in proportion to the bow's speed against the
		dynamic's, so that a stroke drawn faster or slower is as loud. Askenfelt's piano at 42 mm of the
		328 mm violin string; no nearer the bridge than 0.09, chosen by ear: nearer, the forte squeaked. */
		float betaPiano = 0.128f, betaForte = 0.09f, betaLeast = 0.09f, betaMost = 0.16f;
		/** PIANISSIMO, ITS OWN: the speed, the force (in impedances) and the place at pp, where the
		lines from piano down meet, rather than the piano-to-forte line carried on below piano. Carried
		on, pianissimo is a bow crawling at 0.11 m/s or less, pressed near the bridge, and on the
		violin's D string that rasps: each period unlike the next, the bow neither gripping nor letting
		go cleanly, heard through the Fauré's quiet bars. Drawn a little faster, lighter, and a little
		over the fingerboard, the same notes are as clean as at forte and still quieter than piano,
		since the loudness is in the speed over the distance from the bridge. Negative carries the line
		on, as before. */
		float speedPianissimo = 0.13f, forcePianissimo = 1.2f, betaPianissimo = 0.16f;
		/** VIBRATO: how fast, how wide either way, how long after the note starts it begins and how
		long it takes to come in. By ear on the violin, 5.8 Hz and 20 cents, and the cello, 5.5 Hz and
		15 cents, within the measured 5.5 to 6.5 Hz and 10 to 35 cents and the cello's narrower than
		the violin's (research/bowed-strings/vibrato-sources.md). */
		float vibratoHz = 5.8f, vibratoCents = 20.f, vibratoDelay = 0.08f, vibratoFade = 0.15f;
		Era era = ROMANTIC;
		/** THE BOW CHANGES REHEARSED: how long the bow takes to slow into the turn and to speed up out
		of it, each tried, the force falling with the speed to nothing at the turn. By ear, against
		slower changes that kept a quarter or half of the force: those squeaked as the bow slowed. */
		float changeSeconds[3] = {0.01f, 0.02f, 0.04f};
		/** EVENNESS: how far the player evens out the instrument's own unevenness from note to note —
		the body radiating some pitches far more than others, and a heavier string pushing harder on
		the bridge — by drawing a note it expects to come out weak a little faster, or under a slur a
		little nearer the bridge, and a strong one slower or further off, as a player does by ear. One
		evens it fully, within `evenMostDb` either way; nought plays each note as the instrument gives
		it. See evenFor. */
		float evenness = 0.f, evenMostDb = 4.f;
		/** THE SLOWEST A STRONG NOTE IS DRAWN to quieten it, as a share of the dynamic's speed; what is
		left of the quietening moves the bow further from the bridge instead. A slow bow on a heavy
		string falls out of one slip a period: the cello's F2, drawn at 0.13 m/s to quieten it by 4
		dB, slipped once every two. */
		float evenSlowest = 0.8f;
		/** HOW LONG THE BITE INTO A NOTE FROM THE STRING TAKES TO EASE OFF, seconds, once four regular
		periods are heard. A bitten note is pressed until it has eased, and pressed it is rough: each
		period unlike the next. It was 0.8 s, chosen by ear on the cello's open C and G in phase 3 on
		single notes from rest, and a note from the string at piano or mezzo-piano stayed rough for most
		of a second, longer than most notes last; across the violin's strings and markings 50 ms left the
		fewest rough (make bowplayer ARGS="rough violin ease=..."): a player presses for the catch and
		lets go within a few periods. */
		float biteEase = 0.05f;
		/** THE BITE PLAYED WITHOUT REHEARSING, a start from the string guessed (STARTS_GUESSED, see
		Rehearsing): its force over the note's, and its acceleration as twelve times the logarithm of its
		share of what Guettler's limits allow (see rehearseShortlist). A moderate bite: firmer ones caught
		the string sooner and were heard as harsh; softer ones too often failed to settle. */
		float guessBite = 1.59f, guessAccel = -41.f;
		/** THE BOW CHANGE PLAYED WITHOUT REHEARSING (LIVE, and any note nobody announced): how long the bow
		takes to turn, seconds, with no extra press — what the instrument's rehearsal chose most often,
		10 ms on the violin (59 per cent of its changes) and the viola (42), 20 ms on the cello (43). */
		float guessTurn = 0.01f;
		/** THE TWO STARTS TRIED (TWO_STARTS, see Rehearsing): a glide over 20 ms from a fifth of the
		force, and the bite the instrument's full rehearsal chose most often across the decisions logged
		over the Fauré and the Brahms trio — for the violin and the viola a light one, a little under the
		note's force, at 1.4 of the planned distance from the bridge and fast. By ear very nearly as good as
		full rehearsal, at a quarter to a half of its computing. */
		FewStarts twoStarts = {{1.19f}, {-19.f}, {1.4f}, {0.02f}, {0.2f}};
		/** SEEDS THE SMALL VARIATIONS, so a part plays the same way twice. */
		uint32_t seed = 1u;
	};
	static Habits violinHabits();
	static Habits violaHabits();
	static Habits celloHabits();

	/** HOW THE PLAYER IS USED. SOLO decides everything itself, rehearsing as it goes: a bench, or a part
	played at leisure. PLANNER and PLAYING are the two copies a module runs (see above). */
	enum Mode : uint8_t { SOLO, PLANNER, PLAYING };

	/** THE INSTRUMENT, and the rates: the engine's, and the highest note the part reaches, which
	sets how many times faster the strings run (BowedString::oversampling). */
	void setInstrument(const BowedInstrument::Design& design, float engineRate, float highestHz);
	void setHabits(const Habits& h) { habits = h; }
	/** THE OTHER STRINGS RINGING IN SYMPATHY, on by default: off for a bench that wants the bowed
	string alone. */
	void setSympathetic(bool on) { live.inst.setSympathetic(on); }
	/** EVERY DECISION REHEARSED, WRITTEN DOWN: one line of JSON a decision, appended to `file` — the
	situation (the instrument, named here, the string, the note, the marking, how the bow is to be
	drawn, what came before, how strongly the string is still ringing and how warm its rosin is) and
	every way of starting or changing that was tried, with how each came out and which was played. For
	building a table of what to play without rehearsing (see docs/bowed-strings.md). Everything it
	reads it reads from copies, so writing it changes nothing that is played. Null writes nothing. */
	void setLog(std::FILE* file, const std::string& instrument) {
		logFile = file;
		logName = instrument;
	}
	/** THE TABLE OF STARTS this instrument plays from where it does not rehearse (see BowedTable.hpp),
	or null for the starts rehearsal found earlier for notes like each one. */
	void setTable(std::shared_ptr<const BowedTable> t) { table = t; }
	/** HOW MUCH IS PLAYED FROM THE TABLE: only what there is no time to rehearse; the bow changes too,
	where the table has the situation, with the starts from the string still rehearsed; or everything,
	with no rehearsing at all. */
	enum TableUse { WHEN_NO_TIME, CHANGES, EVERYTHING };
	void setTableUse(TableUse u) { tableUse = u; }
	void setFewStarts(const FewStarts& f) { few = f; }
	/** HOW MUCH EACH START FROM THE STRING IS REHEARSED. FULL: the shortlist found on the string alone,
	up to twelve of it and six glides tried on the instrument. TWO_STARTS: the habit's two starts tried
	on the instrument (Habits::twoStarts). STARTS_GUESSED: none tried, the habit's guessed bite played
	(Habits::guessBite). At these three every bow change is rehearsed. LIVE: nothing rehearsed — the
	guessed bite, and each bow change turned as Habits::guessTurn — so nothing need be known ahead and a
	module needs no planner. After setHabits. */
	enum Rehearsing : uint8_t { FULL, TWO_STARTS, STARTS_GUESSED, LIVE };
	void setRehearsing(Rehearsing r) {
		few = r == TWO_STARTS ? habits.twoStarts : FewStarts();
		guessStarts = guessOnly = r == STARTS_GUESSED || r == LIVE;
		nothingRehearsed = r == LIVE;
	}
	/** WHERE THE REHEARSING'S TIME WENT, seconds of computing, for a bench: running the copy up to the
	note, the shortlist on the string alone, the bites and the glides tried on the instrument, and the
	bow changes; and how many starts from the string took the shortlist's second, wider search. */
	struct Spent {
		double runUp = 0.0, shortlist = 0.0, bites = 0.0, glides = 0.0, changes = 0.0;
		int starts = 0, widened = 0;
	};
	const Spent& spent() const { return spentOn; }
	/** EVERY START NOT REHEARSED THE GUESS'S BITE (Habits::guessBite), the table aside: for comparison. */
	void setGuessOnly(bool on) { guessOnly = on; }
	/** A SLURRED CHANGE OF A LARGE INTERVAL SMOOTHED: the finger slides to the new note over `slide`
	seconds, smoothly, and the old note lingers, fading out over `linger` seconds as the new one fades
	in (BowedInstrument::linger), for changes of `from` semitones or more: every harmonic of a note
	switched at once to the next's is heard, dry, as a blip of pink noise. Off, the finger's usual
	LANDING. Assembled field by field. */
	struct SlurSmoothing {
		bool on = true;
		float slide = 0.015f, linger = 0.04f, from = 3.f;
	};
	void setSlurSmoothing(const SlurSmoothing& s) { slurSmoothing = s; }
	/** STARTS FROM THE STRING NEVER REHEARSED, bow changes still rehearsed: for comparing starts alone. */
	void setGuessStarts(bool on) { guessStarts = on; }
	/** WHETHER EACH NOTE'S SLIPS ARE COUNTED AS IT PLAYS, into `played`, for a bench's report: a module
	has no use for them. */
	void setWatching(bool on) { watchingOn = on; }
	/** A STRING NOBODY IS PLAYING STOPS when it has rung down: see BowedInstrument::setQuietStringsRest. */
	void setQuietStringsRest(bool on) { live.inst.setQuietStringsRest(on); }
	/** THE VIBRATO'S WIDTH, a share of what the player decides: one as decided, nought none. */
	void setVibratoScale(float k) { live.vibratoScale = k; }
	const BowedInstrument& instrument() const { return live.inst; }

	/** A NOTE STARTING `at` SECONDS on the player's clock. Notes are given in the order they start;
	the further ahead, the better the player can prepare. */
	void add(const Note& n, double at);
	/** The same, at a sample of the strings' clock. */
	void addAt(const Note& n, int64_t start);
	/** THE BOW LIFTED NOW from the note sounding: a note held from a keyboard, let go. */
	void endNow();
	/** EVERYTHING ANNOUNCED AND NOT YET BEGUN, FORGOTTEN: the notes waiting, and what was decided for
	notes that have not started, back to how things stood after the last one that has. */
	void withdraw();
	/** The strings' clock, samples. */
	int64_t clock() const { return live.clock; }
	/** Whether the instrument is at rest, not being stepped (see step). */
	bool resting() const { return live.asleep; }

	/** ONE SAMPLE AT THE ENGINE'S RATE: the sound a metre away, pascals. */
	float process();
	/** The player's clock, seconds. */
	double now() const { return (double) live.clock / rate; }
	/** The rate the strings run at: the engine's, times the oversampling. */
	double stringRate() const { return rate; }

	/** WHAT THE PLAYER DID WITH EACH NOTE, for a bench. */
	struct Played {
		double at = 0.0;
		float hz = 0.f;
		/** The note's dynamic, as given. */
		float level = 0.f;
		int string = 0;
		enum Kind : uint8_t { FRESH, CHANGE, LEGATO, CROSS } kind = FRESH;
		int direction = 1;
		float force = 0.f, speed = 0.f, distance = 0.f, vibratoCents = 0.f;
		/** The bow change it chose, for a CHANGE: how long the bow takes to slow and to speed up again,
		and the share of the force left at the turn. */
		float changeSeconds = 0.f, changeForce = 1.f;
		/** Periods from the restart until one slip a period, as rehearsed; -1 never. */
		double settled = -1.0;
		/** A bow change made inside a slur, because the bow would have run short. */
		bool hidden = false;
		/** Where its start came from the table of starts: 'b' a bite, 'g' a glide, 'c' a bow change. */
		char fromTable = 0;
		/** String samples simulated to rehearse it, beyond the shortlist of a fresh attack. */
		int64_t rehearsed = 0;
		/** HOW THE STRING ACTUALLY MOVED through the note's body, from 60 ms after it began to its
		end, as played: slips a period, and the share of the gaps between slips that were a period
		within a tenth. Helmholtz motion is one and one; a squawk is neither. */
		float slipsPerPeriod = 0.f, regular = 0.f;
		int64_t bodyFrom = 0, bodyTo = 0;
		int slipCount = 0, regularGaps = 0, gaps = 0;
		int64_t lastSlip = -1;
		/** WHEN IT SPOKE, as played: seconds from the bow's moving to the fourth regular gap between
		slips running; -1 not yet, or never. Measured on every note, rehearsed or not. */
		double spoke = -1.0;
		int64_t movedAt = 0, spokeLastSlip = -1;
		int spokeRun = 0;
		/** AS HEARD: the note as played judged as rehearsal judges a trial (attackSettles) — its squeak
		in the first 150 ms, its click, and its roughness from the fourth period after the first slip
		to ROUGH_UNTIL — on the string's push on the bridge, dB; 99 not heard. Every note but a slurred
		one, rehearsed or not. */
		double heardSqueak = 99.0, heardClick = 99.0, heardRough = 99.0;
	};
	std::vector<Played> played;
	/** Fills in how each note's string moved, from the notes planned so far; process() keeps it. */
	void watchPlayed();
	/** Hears each note's start as played, for the `heard` figures; process() keeps it. */
	void hearPlayed();

	/** How far ahead of a note it is planned, seconds: longer than the longest bow change takes to
	slow down. */
	static constexpr double PREPARE = 0.08;

private:
	// ---- what happens, sample by sample -------------------------------------------------------

	/** A VALUE MOVING STRAIGHT TO A TARGET over a number of samples, or holding. */
	struct Ramp {
		float value = 0.f, step = 0.f;
		int64_t left = 0;
		void to(float target, int64_t samples) {
			if (samples <= 0) { value = target; left = 0; step = 0.f; return; }
			step = (target - value) / (float) samples;
			left = samples;
		}
		void advance() { if (left > 0) { value += step; left--; } }
	};

	/** ONE STRING AS THE PLAYER HAS IT: the bow on it, the finger, the vibrato, and an attack being
	eased off. */
	struct Voice {
		Ramp force, speed, distance;
		float fingerHz = 0.f;
		bool stopped = false;
		/** THE FORCE FOLLOWING THE BOW'S SPEED, through a bow change: whole at `followSpeed` and above,
		down to `followFloor` of it as the bow stands still. */
		bool follow = false;
		float followForce = 0.f, followFloor = 0.25f, followSpeed = 0.25f;
		/** A FINGER LANDING: the pitch moving from `glideFrom` to the finger's over `glideLength`
		samples from `glideAt`, as the string is pressed down. */
		float glideFrom = 0.f;
		int64_t glideAt = 0, glideLength = 0;
		/** The pitch moving along a raised cosine rather than straight: a slurred change smoothed. */
		bool glideSmooth = false;
		float vibratoHz = 0.f, vibratoCents = 0.f;
		int64_t vibratoFrom = 0, vibratoFade = 1;
		// AN ATTACK: the bow speeding up at `accel` to `target`, the force held at the bite until
		// four regular periods are heard, then eased to `settle` over EASE.
		bool attacking = false, easing = false;
		/** How long the bite eases over, samples. */
		int64_t easeSamples = 0;
		/** When the bow began to follow its speed with its force, for the floor's landing. */
		int64_t followFrom = 0;
		/** HOW STRONGLY THE STRING RINGS: the mean square of its push on the bridge over about the last
		20 ms, for the table of starts. Nothing played depends on it. */
		double ringing = 0.0;
		float accel = 0.f, target = 0.f, settle = 0.f;
		int64_t attackFrom = 0;
		std::vector<int64_t> slips;
		bool wasStuck = true;
		SlipWatch watch;
	};

	/** SOMETHING THE PLAYER DOES AT A SAMPLE. */
	struct Action {
		int64_t at = 0;
		enum Kind : uint8_t { FORCE, SPEED, DISTANCE, FINGER, VIBRATO, ATTACK, FOLLOW } kind = FORCE;
		int string = 0;
		float value = 0.f;       /**< the target, the finger's pitch, or the vibrato's width */
		int64_t samples = 0;     /**< how long a ramp takes */
		float a = 0.f, b = 0.f;  /**< ATTACK: acceleration and the force it eases to; VIBRATO: rate;
		                         FOLLOW: the share of the force left at a standstill, and the speed
		                         at which the force is whole */
		float d = 0.f;           /**< ATTACK: the force it starts at, the bite */
		int64_t c = 0;           /**< VIBRATO: samples to come in; ATTACK: samples the bite eases over */
		/** The decision it belongs to, for withdraw(). */
		int64_t owner = -1;
	};

	/** EVERYTHING THAT CHANGES AS IT PLAYS: what a rehearsal copies. */
	struct State {
		BowedInstrument inst;
		Voice voices[4];
		std::vector<Action> actions;   // in time order
		int64_t clock = 0;
		/** HOW WIDE THE VIBRATO IS against what was decided: a control turned while the part plays,
		heard at once rather than when the notes already decided have gone by. */
		float vibratoScale = 1.f;
		/** AT REST, NOT STEPPED (see step): no bow on any string, the sound under QUIET for SLEEP_AFTER,
		and nothing to do until the next action. `phase` counts the strings' samples since the last
		engine sample, so engine samples go on coming every `factor`; `quiet` the engine samples it has
		been quiet; `slept` the strings' samples not stepped. */
		bool asleep = false;
		int phase = 0;
		int64_t quiet = 0, slept = 0;
	};

public:
	/** THE PLAYER'S BOOKKEEPING: where the bow is, which way it went, the note it is on — everything a
	decision changes besides the actions it schedules. */
	struct Line {
		double bowAt = 0.3;
		int direction = -1;
		double strokeShare = 1.0;
		bool anyLast = false;
		Note last;
		int lastString = 0;
		int64_t lastStart = 0, lastEnd = 0;
		float lastForce = 0.f, lastSpeed = 0.f;
		bool lastReleased = true;
		int64_t releaseAt = -1;
		uint32_t random = 1u;
	};

	/** WHAT REHEARSAL FOUND FOR A NOTE, kept for a note like it that has to be played without: a start
	from the string, glided or bitten, or a bow change, how quick and how pressed. */
	struct Remembered {
		bool known = false;
		bool glide = false;
		float ramp = 0.f, floor = 0.f;
		float betaShare = 1.f, bite = 1.f, accel = 1.f;
		float down = 0.02f, press = 1.f;
	};

	/** ONE DECISION, as a planner made it, for the playing copy to carry out. */
	struct Plan {
		uint32_t generation = 0;
		int64_t serial = 0;
		/** A bow lifted at a note's end rather than a note begun. */
		bool release = false;
		/** The note's start, or the moment the bow lifts; and the note's pitch. */
		int64_t start = 0;
		float hz = 0.f;
		std::vector<Action> actions;
		Line line;
		Played record;
		bool learned = false;
		int learnedAt = -1;
		Remembered value;
	};

	/** WHERE PLANS COME FROM AND GO: the module's engine, between the two threads. */
	struct Planning {
		virtual ~Planning() {}
		/** PLAYING: the plan for decision `serial` of `generation`, or null where there is none in
		time. Given back with spent() once used. */
		virtual Plan* planFor(uint32_t generation, int64_t serial) = 0;
		virtual void spent(Plan* p) = 0;
		/** PLANNER: a decision made. */
		virtual void decided(Plan* p) = 0;
	};
	void setMode(Mode m, Planning* p) { mode = m; planning = p; }
	/** THREADS TO REHEARSE ON: the trials of a note's start shared out over them, the choice the same as
	on one. Null rehearses on the calling thread alone. */
	void setCrew(Crew* c) { crew = c; }
	Mode modeNow() const { return mode; }
	/** THE PLAYING COPY DECIDED SOMETHING ITSELF since this was last cleared: the planner has to start
	again from it. */
	bool lost = false;
	uint32_t generation = 0;
	/** Decisions made so far, and so the number of the next. */
	int64_t decisions = 0;

private:

	State live;
	Habits habits;
	float engine = 48000.f;
	double rate = 48000.0;
	int factor = 1;
	uint32_t random = 1u;

	/** A note waiting to be planned. */
	struct Pending {
		Note note;
		int64_t start = 0;
	};
	std::deque<Pending> upcoming;

	Mode mode = SOLO;
	Planning* planning = nullptr;
	std::shared_ptr<const BowedTable> table;
	TableUse tableUse = WHEN_NO_TIME;
	FewStarts few;
	Spent spentOn;
	bool guessOnly = false, guessStarts = false, nothingRehearsed = false;
	SlurSmoothing slurSmoothing;
	std::FILE* logFile = nullptr;
	std::string logName;
	bool watchingOn = true;
	/** A START BEING HEARD: the string's push on the bridge from the bow's moving, and when it first
	let go. */
	struct Hearing {
		int64_t from = 0, firstSlip = -1, until = 0;
		int string = 0;
		float hz = 0.f, cents = 0.f;
		std::vector<float> push;
		SlipWatch watch;
		bool was = true;
	};
	std::deque<Hearing> hearings;
	size_t heardUpTo = 0;
	Crew* crew = nullptr;
	/** f(0) to f(n - 1), over the crew where there is one. */
	void share(int n, const std::function<void(int)>& f) const;
	/** Deciding without rehearsing, from what rehearsal found for notes like this one. */
	bool quick = false;
	/** A PLANNER'S DECISION BEING KEPT: the actions scheduled on the live state, and what rehearsal
	found. */
	bool capturing = false;
	std::vector<Action> captured;
	bool learnedNow = false;
	int learnedIndex = -1;
	Remembered learnedValue;
	/** WHAT REHEARSAL HAS FOUND, by string, semitone above the open string, level step, and a start
	from the string or a bow change. */
	static constexpr int MEMORY_SEMITONES = 48;
	std::vector<Remembered> memory;
	static int memoryIndex(int string, int semitone, int level, int change) {
		return ((string * MEMORY_SEMITONES + semitone) * 8 + level) * 2 + change;
	}
	int memoryIndexFor(float hz, float level, int string, int change) const;
	/** The note's semitones above its string's open pitch, and the marking's step, as the table has them. */
	int semitoneOf(float hz, int string) const;
	static int levelStepOf(float level);
	/** The nearest note to `index` rehearsal has found something for, or null. */
	const Remembered* recall(int index) const;
	void remember(int index, const Remembered& r);
	/** EACH DECISION'S MOMENT AND THE BOOKKEEPING BEFORE IT, for withdraw(). */
	struct Decided {
		int64_t serial = 0, start = 0;
		Line before;
	};
	std::deque<Decided> history;
	/** The serial actions are tagged with as they are scheduled. */
	int64_t owner = -1;
	void decide(const Pending* p, int64_t releaseWhen);
	Line takeLine() const;
	void giveLine(const Line& l);

	// ---- the bow and the line, as the player keeps track of them --------------------------------
	/** Where along the hair the bow is, metres from the frog, and which way it last went. */
	double bowAt = 0.3;
	int direction = -1;
	/** HOW MUCH FASTER THAN ITS HABIT THE STROKE WAS DRAWN, to share out its bow, which every note under
	its slur keeps against its own habit. */
	double strokeShare = 1.0;
	/** THE LAST NOTE PLANNED, which the next one follows from. */
	bool anyLast = false;
	/** The note whose body is being watched, and whether its string was stuck last sample. */
	size_t watching = 0;
	bool watchWasStuck = true;
	SlipWatch watchSlip;
	/** STUCK OR NOT, as the friction can say: the junction's own flag under the friction curve, which
	marks the release exactly; the flybacks under friction that creeps or flickers. */
	static bool stuckNow(const BowedString& s, SlipWatch& w, float force, float speed) {
		const bool flyback = w.update(s, force, speed);
		return (s.bristling() || s.heating()) ? !flyback : s.sticking();
	}
	Note last;
	int lastString = 0;
	int64_t lastStart = 0, lastEnd = 0;
	float lastForce = 0.f, lastSpeed = 0.f;
	bool lastReleased = true;
	int64_t releaseAt = -1;

	void schedule(State& s, const Action& a);
	/** One sample at the strings' rate on a state: true and the sound when an engine sample is
	ready. */
	static bool step(State& s, double rate, float* sound);
	static void apply(State& s, const Action& a, double rate);
	static void applyDue(State& s, double rate);

	void plan(const Pending& p);
	void planRelease(int64_t at);
	int stringFor(float hz, bool longWithVibrato, int followsFrom) const;
	float forceFor(float level, int string) const;
	/** The habitual bow speed for a dynamic. */
	float speedFor(float level) const;
	float betaFor(float level) const;
	/** HOW MUCH LOUDER OR QUIETER THAN THE INSTRUMENT'S MIDDLE a note at `hz` on `string` comes out
	for the same bowing, decibels: the string's impedance, which sets how hard it pushes on the
	bridge, and the body's radiation at the note and its harmonics, a bowed string's falling as one
	over the harmonic's number, weighted as the ear weights them (A-weighting). */
	double expectedDb(float hz, int string) const;
	/** The factor the dynamic's speed is drawn at for evenness: see Habits::evenness. */
	double evenFor(float hz, int string) const;
	/** The middle of expectedDb over the instrument's first position on every string, set with it. */
	double evenRef = 0.0;
	float vibratoFor(const Note& n, int string) const;
	float noise();

	bool vibratoWanted(const Note& n) const;

	/** HOW SOON A CHANGE SETTLES: a copy of a state run for `samples`, and the periods from
	`restart` until the slips on `string` are a period apart from there to the end of the run, at
	least four of them, or 999. */
	double settles(State s, int string, float hz, int64_t restart, int64_t samples,
	               double* roughness = nullptr, double* squeak = nullptr, float cents = 0.f) const;
	/** HOW SOON AN ATTACK SETTLES, as phase 6 confirmed one: periods from the first slip after
	`from` until the slips are a period apart from there to the end, the run ending twenty periods
	after the first slip or 1.5 s on; -1 if never. `late`, the seconds from `from` to the first slip.
	`rough`, if asked, how rough the string's push on the bridge is from the fourth period after the
	first slip to ROUGH_UNTIL after `from` (see roughnessOf), the run going on until then. `click`, if
	asked, how much brighter the push's first CLICK_SECONDS is than what follows it up to 150 ms, dB
	above 4 kHz: a start brighter than the note it becomes is heard as a pluck. `rise`, if asked, how
	far the push from 60 to 100 ms is under its level from 200 to ROUGH_UNTIL, dB: a note that speaks on
	time but swells in over most of a second is heard as a weak one. `envelope`, if asked, the power of
	the string's vibration on the bridge in 10 ms windows every ENVELOPE_HOP from `from` to ROUGH_UNTIL,
	nothing before the first slip. */
	int attackSettles(State s, int string, float hz, int64_t from, int64_t* simulated,
	                  double* squeak = nullptr, float cents = 0.f, double* late = nullptr,
	                  double* rough = nullptr, double* click = nullptr, double* rise = nullptr,
	                  std::vector<double>* envelope = nullptr) const;
};


} // namespace px
