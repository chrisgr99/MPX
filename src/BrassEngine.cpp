/** See BrassEngine.hpp. */
#include "BrassEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace px {


/** How far ahead the thread renders while notes are announced, short of how far ahead they are; and with
nothing announced, seconds. */
static const double AHEAD_SHORT = 1.0, AHEAD_UNANNOUNCED = 0.03;
/** IN HAND TO REHEARSE: this much more than its longest rehearsal lately, and this many times as much;
"lately" forgetting by a factor e every this many seconds of music. */
static const double REHEARSE_SPARE = 0.5, REHEARSE_TIMES = 1.5, LATELY = 30.0;
/** How long after the last announcement the player stops rendering far ahead, seconds. */
static const double ANNOUNCED_LATELY = 5.0;
/** The output's fade on a withdrawal, and how long it stays quiet after the skip while the note the
player was playing dies away, seconds. */
static const double FADE_SECONDS = 0.01, QUIET_AFTER = 0.06;
/** Samples rendered at a time between looks at the audio thread. */
static const int BLOCK = 64;


/** ONE CREW FOR EVERY TROMBONE, taking turns: a rehearsal's trials shared over all but two of the
machine's threads, the audio thread's and the renderer's. */
std::shared_ptr<Crew> brassSharedCrew() {
	static std::mutex lock;
	static std::weak_ptr<Crew> shared;
	std::lock_guard<std::mutex> hold(lock);
	std::shared_ptr<Crew> crew = shared.lock();
	if (!crew) {
		const int cores = (int) std::thread::hardware_concurrency();
		crew = std::make_shared<Crew>(std::max(0, std::min(10, cores - 3)));
		shared = crew;
	}
	return crew;
}


bool BrassEngine::Inbox::push(const Message& m) {
	const uint32_t w = written.load(std::memory_order_relaxed);
	if (w - read.load(std::memory_order_acquire) >= N)
		return false;
	items[w % N] = m;
	written.store(w + 1, std::memory_order_release);
	return true;
}


bool BrassEngine::Inbox::pop(Message& m) {
	const uint32_t r = read.load(std::memory_order_relaxed);
	if (r == written.load(std::memory_order_acquire))
		return false;
	m = items[r % N];
	read.store(r + 1, std::memory_order_release);
	return true;
}


BrassEngine::~BrassEngine() {
	stopping.store(true);
	if (thread.joinable())
		thread.join();
}


void BrassEngine::setup(const Setup& s) {
	made = s;
	BrassInstrument::Design design = s.design;
	design.rate = s.rate;
	if (s.economy)
		design.steepening = false;
	player.setup(design, *s.table, s.habits);
	player.keepDecisions(256);
	if (s.fidelity == LIVE)
		return;
	player.setRehearsing(true);
	player.setFewerWays(s.fidelity == FEWER_WAYS);
	// THE RING holds what is rendered ahead, with room to spare.
	size_t n = 1;
	while ((double) n < (s.announcedAhead + 1.0) * s.rate)
		n *= 2;
	ring.assign(n, 0.f);
	mask = (uint64_t) n - 1;
	crew = brassSharedCrew();
	thread = std::thread([this]() { work(); });
}


void BrassEngine::take(const Message& m, uint64_t now) {
	const int64_t clock = player.clock();
	switch (m.kind) {
	case Message::ANNOUNCE:
		player.add(m.note, std::max<int64_t>(clock, (int64_t) m.at), true);
		lastAnnounced = now;
		everAnnounced = true;
		break;
	case Message::NOW:
		player.add(m.note, clock, false);
		break;
	case Message::END:
		player.release();
		break;
	case Message::WITHDRAW:
		player.withdraw();
		everAnnounced = false;
		break;
	}
}


void BrassEngine::announce(const BrassPlayer::Note& n, int64_t ahead) {
	Message m;
	m.kind = Message::ANNOUNCE;
	m.note = n;
	if (made.fidelity == LIVE) {
		// Nothing rehearsed and nothing rendered ahead: the note goes straight to the player, which keeps
		// it until it is due.
		m.at = (uint64_t) (player.clock() + std::max<int64_t>(0, ahead));
		take(m, (uint64_t) player.clock());
		return;
	}
	if (holding()) {
		if (heldCount < HELD_MOST) {
			held[heldCount] = m;
			heldDue[heldCount] = std::max<int64_t>(0, ahead);
			heldCount++;
		}
		return;
	}
	// The player's sample for it: the one heard now, and as far on as it is ahead.
	m.at = heard.load(std::memory_order_relaxed) + (uint64_t) std::max<int64_t>(0, ahead);
	inbox.push(m);
}


void BrassEngine::playNow(const BrassPlayer::Note& n) {
	Message m;
	m.kind = Message::NOW;
	m.note = n;
	if (made.fidelity == LIVE)
		take(m, (uint64_t) player.clock());
	else if (holding()) {
		if (heldCount < HELD_MOST) {
			held[heldCount] = m;
			heldDue[heldCount] = 0;
			heldCount++;
		}
	}
	else
		inbox.push(m);
}


void BrassEngine::endNow() {
	Message m;
	m.kind = Message::END;
	if (made.fidelity == LIVE)
		take(m, (uint64_t) player.clock());
	else
		inbox.push(m);
}


void BrassEngine::withdraw() {
	Message m;
	m.kind = Message::WITHDRAW;
	if (made.fidelity == LIVE) {
		take(m, (uint64_t) player.clock());
		return;
	}
	// Faded out at once, whatever the thread is doing, until it has skipped what it rendered ahead; and
	// what was held back for an earlier skip forgotten with the rest.
	heldCount = 0;
	if (inbox.push(m))
		withdrawalsAsked++;
}


float BrassEngine::process() {
	if (made.fidelity == LIVE)
		return (float) player.process();
	const float step = (float) (1.0 / (FADE_SECONDS * made.rate));
	uint64_t h = heard.load(std::memory_order_relaxed);
	const bool waiting = withdrawalsAsked != 0 && waitingForSkip();
	if (!waiting && quietFor < 0) {
		// THE SKIP: what was rendered ahead of the withdrawal is not heard; and what was held back is sent
		// from where it lands.
		h = std::max(h, skipTo.load(std::memory_order_acquire));
		heard.store(h, std::memory_order_release);
		quietFor = (int64_t) (QUIET_AFTER * made.rate);
		for (int i = 0; i < heldCount; i++) {
			Message m = held[i];
			m.at = h + (uint64_t) std::max<int64_t>(0, heldDue[i]);
			inbox.push(m);
		}
		heldCount = 0;
	}
	if (waiting) {
		quietFor = -1;
		for (int i = 0; i < heldCount; i++)
			heldDue[i]--;
	}
	float v = 0.f;
	if (h < written.load(std::memory_order_acquire))
		v = ring[h & mask];
	else
		silentCount.fetch_add(1, std::memory_order_relaxed);
	heard.store(h + 1, std::memory_order_release);
	if (quietFor > 0)
		quietFor--;
	const bool quiet = waiting || quietFor != 0;
	fade = quiet ? std::max(0.f, fade - step) : std::min(1.f, fade + step);
	return v * fade;
}


void BrassEngine::work() {
	player.setCrew(crew.get());
	const double rate = made.rate;
	const uint64_t shortAhead = (uint64_t) ((made.announcedAhead - AHEAD_SHORT) * rate);
	const uint64_t unannouncedAhead = (uint64_t) (AHEAD_UNANNOUNCED * rate);
	// The longest a block of samples has taken lately, seconds of computing: a block with a rehearsal in it.
	// A second to begin with.
	double longest = 1.0;
	const double forget = std::exp(-(double) BLOCK / rate / LATELY);
	const uint64_t lately = (uint64_t) (ANNOUNCED_LATELY * rate);
	uint64_t w = written.load(std::memory_order_relaxed);
	while (!stopping.load(std::memory_order_relaxed)) {
		uint64_t h = heard.load(std::memory_order_acquire);
		Message m;
		while (inbox.pop(m)) {
			take(m, h);
			if (m.kind == Message::WITHDRAW) {
				// Everything the player renders from here is the music after the withdrawal; behind what is
				// heard, it skips to that.
				while (w < heard.load(std::memory_order_acquire)) {
					player.process();
					w++;
				}
				written.store(w, std::memory_order_release);
				skipTo.store(w, std::memory_order_release);
				withdrawalsDone.fetch_add(1, std::memory_order_acq_rel);
			}
		}
		const bool announcing = everAnnounced && h < lastAnnounced + lately;
		const uint64_t ahead = announcing ? shortAhead : unannouncedAhead;
		if (w >= h + ahead) {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			continue;
		}
		// SHORT OF TIME, nothing new rehearsed: a rehearsal that ran out of what is in hand would be heard as
		// silence.
		const bool inHand = (double) (w - h) / rate > REHEARSE_SPARE + REHEARSE_TIMES * longest;
		player.setRehearsing(inHand);
		const int before = player.unrehearsed();
		const auto began = std::chrono::steady_clock::now();
		for (int i = 0; i < BLOCK && !stopping.load(std::memory_order_relaxed); i++) {
			const float v = (float) player.process();
			// Behind what is heard, a sample is computed and let go.
			if (w >= heard.load(std::memory_order_relaxed))
				ring[w & mask] = v;
			w++;
			written.store(w, std::memory_order_release);
		}
		if (!inHand)
			hurriedCount.fetch_add(player.unrehearsed() - before, std::memory_order_relaxed);
		longest = std::max(std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(), longest * forget);
	}
}


} // namespace px
