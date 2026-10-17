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
	p->addModel(modelMpxMelody);
	p->addModel(modelMpxMelodyVoice);
	p->addModel(modelMpxSprites);
	p->addModel(modelMpxPathSprites);
	p->addModel(modelMpxPathSpritePlayer);
	p->addModel(modelPolyToStereo);
	p->addModel(modelMpxArp);
	p->addModel(modelMpxRand);
	p->addModel(modelMpxPiano);
	// THE TWO GUITAR CHARTS ARE DEPRECATED AND HIDDEN, not removed. mpxScore does everything
	// they did — it reads Guitar Pro as well as MusicXML, and its rows are now six or twelve from
	// the menu — so there is nothing left for them to be. A slug is permanent, though: a patch
	// saved with one of these must still open, so they are registered as before and only kept out
	// of the browser. See docs/guitar-pro.md.
	modelMpxGuitarChart->hidden = true;
	modelMpxGuitarChart6->hidden = true;
	p->addModel(modelMpxGuitarChart);
	p->addModel(modelMpxGuitarChart6);
	p->addModel(modelMpxScore);
	p->addModel(modelMpxGuitarArticulation);
	p->addModel(modelMpxGuitar);
	p->addModel(modelMpxViolin);
	p->addModel(modelMpxTrombone);
	p->addModel(modelMpxViola);
	p->addModel(modelMpxCello);
	p->addModel(modelMpxFingerPicker);
	p->addModel(modelMpxGuitarChartExpander);
	p->addModel(modelMpxFluidSynth);
	p->addModel(modelMpxGuitarist);
}
