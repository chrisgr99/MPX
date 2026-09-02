#include "plugin.hpp"


Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelFromMPX);
	p->addModel(modelToMPX);
	p->addModel(modelEuclid);
}
