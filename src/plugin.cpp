#include "plugin.hpp"


Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMpxOut);
	p->addModel(modelMpxIn);
	p->addModel(modelMonitor);
	p->addModel(modelChart);
	p->addModel(modelMpxVoicing);
	p->addModel(modelMpxPattern);
	p->addModel(modelMpxSprites);
	p->addModel(modelPolyToStereo);
	p->addModel(modelMpxArp);
	p->addModel(modelMpxRand);
	p->addModel(modelMpxPiano);
	p->addModel(modelMpxGuitarChart);
	p->addModel(modelMpxGuitarChart6);
	p->addModel(modelMpxGuitarVoice);
	p->addModel(modelMpxGuitarChartExpander);
	p->addModel(modelMpxFluidSynth);
	p->addModel(modelMpxGuitarist);
}
