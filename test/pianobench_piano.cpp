/** The piano half of pianobench: mpxPiano itself, compiled in, behind a few plain functions so
the bench can drive it without seeing its class. See pianobench.cpp. */
#include "../src/mpxPiano.cpp"

using namespace px;

void* benchPianoMake() {
	libraryAcquire();
	return new PianoModule;
}

/** Waits for the samples, as the module's own window would while it loads. */
bool benchPianoWaitForSamples(double seconds) {
	const double until = system::getTime() + seconds;
	while (system::getTime() < until) {
		libraryStep();
		if (gLib.bank.load())
			return true;
		system::sleep(0.05);
	}
	return false;
}

void benchPianoLink(void* m, int slot, uint32_t generation) {
	((PianoModule*) m)->link(&slot, &generation, 1);
}

void benchPianoParam(void* m, int id, float value) {
	((PianoModule*) m)->params[id].setValue(value);
}

void benchPianoPolyphony(void* m, int n) {
	((PianoModule*) m)->polyphony = n;
}

void benchPianoProcess(void* m, float sampleRate, int64_t frame, float* left, float* right) {
	PianoModule* p = (PianoModule*) m;
	Module::ProcessArgs args;
	args.sampleRate = sampleRate;
	args.sampleTime = 1.f / sampleRate;
	args.frame = frame;
	p->process(args);
	*left = p->outputs[PianoModule::O_L].getVoltage();
	*right = p->outputs[PianoModule::O_R].getVoltage();
}

/** What the voice pool did since last asked: notes taken from strings still sounding, fades cut
short, silent notes let go, and strings sounding now. */
void benchPianoPool(void* m, int* took, int* cut, int* letGo, int* sounding) {
	PianoModule* p = (PianoModule*) m;
	*took = p->tookSince;
	*cut = p->cutSince;
	*letGo = p->letGoSince;
	int n = 0;
	for (int i = 0; i < STRING_VOICES; i++)
		if (p->voices[i].active)
			n++;
	*sounding = n;
}

void benchPianoHammer(void* m, float amount) {
	((PianoModule*) m)->hammerNoise = amount;
}

/** Every voice, by slot: whether it is sounding, and if so its key, part, gain and level now. */
int benchPianoSlots(void* m, int* active, int* keys, int* parts, float* amps, float* levels, int* fading,
		int most) {
	PianoModule* p = (PianoModule*) m;
	int i = 0;
	for (; i < MAX_VOICES && i < most; i++) {
		const PianoVoice& v = p->voices[i];
		active[i] = v.active ? 1 : 0;
		keys[i] = v.key;
		parts[i] = v.part;
		amps[i] = v.amp * v.env;
		// THE RECORDING, folded into the key for the trace: which sample, and the layer's velocity.
		if (v.region)
			keys[i] = v.key + 1000 * (v.region->sample + 1) + 0 * v.vel;
		levels[i] = v.follow;
		fading[i] = v.releasing ? 1 : 0;
	}
	return i;
}

/** Every voice now sounding: its key, its part, and whether it is fading. */
int benchPianoVoices(void* m, int* keys, int* parts, int* fading, int most) {
	PianoModule* p = (PianoModule*) m;
	int n = 0;
	for (int i = 0; i < MAX_VOICES && n < most; i++) {
		if (!p->voices[i].active)
			continue;
		keys[n] = p->voices[i].key;
		parts[n] = p->voices[i].part;
		fading[n] = p->voices[i].releasing ? 1 : 0;
		n++;
	}
	return n;
}
