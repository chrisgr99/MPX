/** Writes mpxPhrase's factory presets.
 *
 *   presetgen <version> <output folder>
 *
 * WHY A PROGRAM AND NOT FILES. Rack's own Preset menu is what offers them, and a preset is a
 * list of parameter values by number. The phrase presets' values are the styles, and the styles
 * are what `make phrasetest` runs its census over. Typing them into a pair of files would
 * mean the thing that ships and the thing that was measured could drift apart without anybody
 * noticing. So they are generated from phraseStyle, once, at build time.
 *
 * NO RACK IN IT. It writes JSON with fprintf, which is all a preset is.
 */
#include "../src/PhraseParams.hpp"
#include "../src/Phrasing.hpp"
#include "../src/MelodyVoice.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace px;

static bool writeParams(const std::string& path, const char* version, const char* model,
		const float* v, int count) {
	FILE* f = fopen(path.c_str(), "w");
	if (!f) {
		fprintf(stderr, "presetgen: cannot write %s\n", path.c_str());
		return false;
	}
	fprintf(f, "{\n  \"plugin\": \"DreamerMPX\",\n  \"model\": \"%s\",\n", model);
	fprintf(f, "  \"version\": \"%s\",\n  \"params\": [\n", version);
	for (int i = 0; i < count; i++)
		fprintf(f, "    {\"id\": %d, \"value\": %.6g}%s\n", i, (double) v[i],
			i + 1 < count ? "," : "");
	fprintf(f, "  ]\n}\n");
	fclose(f);
	return true;
}

int main(int argc, char** argv) {
	const char* version = argc > 1 ? argv[1] : "2.0.0";
	const std::string dir = argc > 2 ? argv[2] : "presets";

	int written = 0, wanted = 0;
	for (int style = 1; style < NUM_PHRASE_STYLES; style++) {
		// THE DEFAULTS FIRST, then the style over them: a preset has to carry a value for every
		// parameter whether or not the style cares about it. SONG, style nought, is what the module
		// comes up with, and is not written as a preset.
		float v[PHP_LEN];
		phraseParamDefaults(v);
		PhraseControls c;
		phraseStyle(style, c);
		phraseParamsFrom(c, v);
		const std::string name = phraseStyleName(style);
		wanted += 2;
		if (writeParams(dir + "/mpxPhrase/" + name + ".vcvm", version, "mpxPhrase", v, PHP_LEN))
			written++;
		float voice[VOICE_STYLE_PARAMS];
		if (voiceStyle(style, voice)
				&& writeParams(dir + "/mpxMelodyVoice/" + name + ".vcvm", version, "mpxMelodyVoice",
					voice, VOICE_STYLE_PARAMS))
			written++;
	}
	printf("presetgen: wrote %d presets to %s\n", written, dir.c_str());
	return written == wanted ? 0 : 1;
}
