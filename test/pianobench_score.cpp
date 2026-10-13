/** The score half of pianobench: mpxScore itself, compiled in as mpxScore.cpp compiles it. */
#define CHART_ROWS 6
#define CHART_AUDIO 1
#define CHART_NS score6
#define CHART_SLUG "mpxScore"
#define CHART_TITLE "mpxScore"
#define CHART_MODEL modelMpxScore
#include "../src/GuitarChart.hpp"

using px::score6::SongModule;

void* benchScoreMake(const char* path, std::string* why) {
	SongModule* m = new SongModule;
	m->id = 424242;
	if (!m->setSongFile(path, why)) {
		delete m;
		return NULL;
	}
	m->params[SongModule::P_PLAY].setValue(0.f);
	return m;
}

int benchScoreSlot(void* m, uint32_t* generation, int row) {
	return ((SongModule*) m)->busSlotFor(SongModule::O_TRACK + row, generation);
}

/** To the first pass through a bar as the page numbers it, and playing. */
void benchScoreStart(void* m, int bar) {
	SongModule* s = (SongModule*) m;
	if (bar > 0)
		px::scoreFollowGoTo(s->id, bar);
	s->params[SongModule::P_PLAY].setValue(1.f);
}

void benchScoreProcess(void* m, float sampleRate, int64_t frame) {
	Module::ProcessArgs args;
	args.sampleRate = sampleRate;
	args.sampleTime = 1.f / sampleRate;
	args.frame = frame;
	((SongModule*) m)->process(args);
}

/** The bar now playing, as the page numbers it, or -1. */
int benchScoreBar(void* m) {
	int bar = -1;
	float through = 0.f;
	bool running = false;
	px::scoreFollowRead(((SongModule*) m)->id, bar, through, running);
	return bar;
}
