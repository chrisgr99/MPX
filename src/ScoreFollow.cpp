#include "ScoreFollow.hpp"

namespace px {


/** THE SLOTS, taken by the first module to publish and given up when it goes. A linear walk of
sixteen atomics, every sample, which is nothing beside what a sample of audio already costs — and
the walk stops at the slot it wants, which for the only module publishing is the first one. */
static ScoreFollow gFollow[MAX_FOLLOWED];


/** The slot this module has, taking a free one where it has none yet. Minus one only when every
slot is spoken for, which needs sixteen score modules in a patch. */
static ScoreFollow* slotFor(int64_t moduleId, bool claim) {
	for (int i = 0; i < MAX_FOLLOWED; i++) {
		if (gFollow[i].moduleId.load(std::memory_order_acquire) == moduleId)
			return &gFollow[i];
	}
	if (!claim)
		return NULL;
	for (int i = 0; i < MAX_FOLLOWED; i++) {
		int64_t empty = -1;
		if (gFollow[i].moduleId.compare_exchange_strong(empty, moduleId))
			return &gFollow[i];
	}
	return NULL;
}


void scoreFollowPublish(int64_t moduleId, int bar, float through, bool running) {
	ScoreFollow* f = slotFor(moduleId, true);
	if (!f)
		return;
	f->bar.store(bar, std::memory_order_relaxed);
	f->through.store(through, std::memory_order_relaxed);
	f->running.store(running, std::memory_order_relaxed);
}


bool scoreFollowRead(int64_t moduleId, int& bar, float& through, bool& running) {
	const ScoreFollow* f = slotFor(moduleId, false);
	if (!f)
		return false;
	bar = f->bar.load(std::memory_order_relaxed);
	through = f->through.load(std::memory_order_relaxed);
	running = f->running.load(std::memory_order_relaxed);
	return true;
}


void scoreFollowSetLoop(int64_t moduleId, int from, int to, bool seek) {
	ScoreFollow* f = slotFor(moduleId, true);
	if (!f)
		return;
	// THE END BEFORE THE START, so a module that catches the two between stores sees a loop that
	// is empty rather than one that runs to the wrong place.
	f->loopTo.store(to, std::memory_order_relaxed);
	f->loopFrom.store(from, std::memory_order_release);
	if (seek)
		f->wantSeek.store(true, std::memory_order_release);
}


bool scoreFollowLoop(int64_t moduleId, int& from, int& to, bool* takeSeek) {
	ScoreFollow* f = slotFor(moduleId, false);
	if (!f)
		return false;
	from = f->loopFrom.load(std::memory_order_acquire);
	to = f->loopTo.load(std::memory_order_relaxed);
	if (takeSeek)
		*takeSeek = f->wantSeek.exchange(false, std::memory_order_acq_rel);
	return from >= 0 && to >= from;
}


void scoreFollowGoTo(int64_t moduleId, int bar) {
	ScoreFollow* f = slotFor(moduleId, true);
	if (f)
		f->goTo.store(bar, std::memory_order_release);
}


int scoreFollowTakeGoTo(int64_t moduleId) {
	ScoreFollow* f = slotFor(moduleId, false);
	return f ? f->goTo.exchange(-1, std::memory_order_acq_rel) : -1;
}


void scoreFollowTransport(int64_t moduleId, int what) {
	ScoreFollow* f = slotFor(moduleId, true);
	if (f)
		f->wantTransport.store(what, std::memory_order_release);
}


int scoreFollowTakeTransport(int64_t moduleId) {
	ScoreFollow* f = slotFor(moduleId, false);
	return f ? f->wantTransport.exchange(0, std::memory_order_acq_rel) : 0;
}


void scoreFollowDrop(int64_t moduleId) {
	ScoreFollow* f = slotFor(moduleId, false);
	if (!f)
		return;
	f->bar.store(-1, std::memory_order_relaxed);
	f->loopFrom.store(-1, std::memory_order_relaxed);
	f->loopTo.store(-1, std::memory_order_relaxed);
	f->wantSeek.store(false, std::memory_order_relaxed);
	f->goTo.store(-1, std::memory_order_relaxed);
	f->moduleId.store(-1, std::memory_order_release);
}


} // namespace px
