#pragma once
/** THE TROMBONE AS A MODULE PLAYS IT: phase 7 of docs/brass.md.

REHEARSING TAKES TIME. Before a note met for the first time the player tries its start or its slur sixty
to a hundred ways on copies of the trombone (BrassPlayer.hpp), about a second of computing even shared over
the machine's threads; a sample is twenty microseconds. So at Fidelity levels 1 and 2 the whole player —
the trombone, the deciding and the rehearsing — runs on a thread of its own, RENDERING AHEAD of what is
heard into a ring that the audio thread only reads from.

HOW FAR AHEAD. A score on the cable announces each note 4.5 s before it sounds (MPX_ANNOUNCE_AHEAD). While
notes are being announced the player renders up to a second short of that, 3.5 s ahead, and a rehearsal
comes out of that store. It times its rehearsals: with less in hand than half a second more than one and a
half times the longest of them lately, it plays each note met for the first time without rehearsing, from
memory or by its habits, until it has caught up, since a rehearsal that ran past what was in hand would be
heard as silence. A note nobody announced can only be
played once it arrives: after five seconds with nothing announced the player renders only 30 ms ahead —
more than the most samples Rack asks for at once — so such a note sounds 30 ms late. One that arrives
while the player is far ahead is played when the player gets to it.

LEVEL 3, LIVE, has no thread: the player runs on the audio thread, nothing is rehearsed or needed ahead,
and a note sounds the sample it arrives.

FALLING BEHIND, on a busy machine: what is missing is played as silence, and the player skips what it
was too late for. WITHDRAWN, the music stopped or taken back: the output fades out at once, the player
ends what it was playing and forgets what it was told, and what it had rendered ahead is skipped.

NO RACK IN HERE, so a bench can drive it. */
#include "BrassPlayer.hpp"
#include "BrassTable.hpp"
#include "Crew.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace px {


/** ONE CREW FOR EVERY TROMBONE, taking turns: shared by the engines' rehearsals and mpxTrombone's tuning. */
std::shared_ptr<Crew> brassSharedCrew();


struct BrassEngine {
	/** FIDELITY: every way of each join tried, a third of them, or none and no thread. */
	enum Fidelity { EVERY_WAY, FEWER_WAYS, LIVE };

	/** WHAT AN ENGINE IS SET UP FOR, assembled field by field. ECONOMY: the steepening of the wave on its
	way down the bore left out (BrassWaveguide), for less computing and less brassiness at the loudest. */
	struct Setup {
		BrassInstrument::Design design;
		BrassPlayer::Habits habits;
		const BrassTable* table = nullptr;
		double rate = 48000.0;
		Fidelity fidelity = FEWER_WAYS;
		bool economy = false;
		/** How far ahead the cable announces, seconds. */
		double announcedAhead = 4.5;
	};
	BrassEngine() = default;
	~BrassEngine();
	/** Main thread, once. */
	void setup(const Setup& s);

	/** Whether it plays live: no thread, nothing rendered ahead. */
	bool live() const { return made.fidelity == LIVE; }

	/** AUDIO THREAD. A note `ahead` samples from now. */
	void announce(const BrassPlayer::Note& n, int64_t ahead);
	/** A note now, which nobody announced. */
	void playNow(const BrassPlayer::Note& n);
	/** The note sounding ends now: a key let go. */
	void endNow();
	/** Everything told forgotten, the note sounding ended: the music stopped or taken back. */
	void withdraw();
	/** One sample, pascals a metre away. */
	float process();

	/** FOR BENCHES: samples played as silence because the player was behind, and notes decided without
	rehearsing because it was short of time. */
	uint64_t silent() const { return silentCount.load(std::memory_order_relaxed); }
	int hurried() const { return hurriedCount.load(std::memory_order_relaxed); }

private:
	struct Message {
		enum Kind { ANNOUNCE, NOW, END, WITHDRAW } kind = ANNOUNCE;
		BrassPlayer::Note note;
		/** The sample of the player's it starts at, for ANNOUNCE. */
		uint64_t at = 0;
	};
	/** A RING BETWEEN TWO THREADS, one writing and one reading. */
	struct Inbox {
		static const uint32_t N = 1024;
		Message items[N];
		std::atomic<uint32_t> written{0}, read{0};
		bool push(const Message& m);
		bool pop(Message& m);
	};

	void work();
	/** A message carried out on the player: by the thread, or on the audio thread at the live level. */
	void take(const Message& m, uint64_t now);

	Setup made;
	BrassPlayer player;
	std::shared_ptr<Crew> crew;
	std::thread thread;
	std::atomic<bool> stopping{false};
	Inbox inbox;

	/** THE RING: samples from the player, by its sample number; `written` up to which the thread has put
	them, `heard` up to which the audio thread has taken them. */
	std::vector<float> ring;
	uint64_t mask = 0;
	std::atomic<uint64_t> written{0}, heard{0};
	/** THE SKIP AFTER A WITHDRAWAL: the withdrawals asked for, by the audio thread; and, by the thread, the
	last it carried out and where it had rendered to then, which the audio thread skips to. */
	uint32_t withdrawalsAsked = 0;
	std::atomic<uint32_t> withdrawalsDone{0};
	std::atomic<uint64_t> skipTo{0};
	/** The audio thread's: the output's fade, and how long it stays quiet after a skip. */
	float fade = 1.f;
	int64_t quietFor = 0;
	/** NOTES HELD BACK while a skip is waited for, each with the samples until it is due, counted down: sent
	once the skip is made, from where it lands. */
	static const int HELD_MOST = 256;
	Message held[HELD_MOST];
	int64_t heldDue[HELD_MOST] = {};
	int heldCount = 0;
	bool waitingForSkip() const { return withdrawalsDone.load(std::memory_order_acquire) != withdrawalsAsked; }
	/** Whether notes are held back: until the audio thread has made the skip, not only the thread. */
	bool holding() const { return waitingForSkip() || quietFor < 0; }
	/** The thread's: the sample of the last announcement. */
	uint64_t lastAnnounced = 0;
	bool everAnnounced = false;

	std::atomic<uint64_t> silentCount{0};
	std::atomic<int> hurriedCount{0};
};


} // namespace px
