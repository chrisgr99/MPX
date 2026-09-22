#include "plugin.hpp"


Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMpxOut);
	p->addModel(modelMpxIn);
	p->addModel(modelEuclid);
	p->addModel(modelProgression);
	p->addModel(modelMonitor);
	p->addModel(modelChart);
	p->addModel(modelMpxComp);
	p->addModel(modelPolyToStereo);
	p->addModel(modelMpxArp);
	p->addModel(modelMpxRand);
	p->addModel(modelMpxMelody);
	p->addModel(modelMpxMelodyVoice);
	p->addModel(modelMpxPhrase);
	p->addModel(modelMpxPiano);
}
