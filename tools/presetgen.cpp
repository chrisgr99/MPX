/** Writes mpxPhrase's factory presets.
 *
 *   presetgen <version> <output folder>
 *
 * WHY A PROGRAM AND NOT TWO FILES. Rack's own Preset menu is what offers SONG and JAZZ, and a
 * preset is a list of parameter values by number. Those values are the two styles, and the two
 * styles are what `make phrasetest` runs its census over. Typing them into a pair of files would
 * mean the thing that ships and the thing that was measured could drift apart without anybody
 * noticing. So they are generated from phraseStyle, once, at build time.
 *
 * NO RACK IN IT. It writes JSON with fprintf, which is all a preset is.
 */
#include "../src/PhraseParams.hpp"
#include "../src/Phrasing.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace px;

static bool writePreset(const std::string& path, const char* version, const float* v) {
	FILE* f = fopen(path.c_str(), "w");
	if (!f) {
		fprintf(stderr, "presetgen: cannot write %s\n", path.c_str());
		return false;
	}
	fprintf(f, "{\n  \"plugin\": \"DreamerMPX\",\n  \"model\": \"mpxPhrase\",\n");
	fprintf(f, "  \"version\": \"%s\",\n  \"params\": [\n", version);
	for (int i = 0; i < PHP_LEN; i++)
		fprintf(f, "    {\"id\": %d, \"value\": %.6g}%s\n", i, (double) v[i],
			i + 1 < PHP_LEN ? "," : "");
	fprintf(f, "  ]\n}\n");
	fclose(f);
	return true;
}

int main(int argc, char** argv) {
	const char* version = argc > 1 ? argv[1] : "2.0.0";
	const std::string dir = argc > 2 ? argv[2] : "presets/mpxPhrase";

	int written = 0;
	for (int style = 0; style < NUM_PHRASE_STYLES; style++) {
		// THE DEFAULTS FIRST, then the style over them: a style has an opinion about eleven of the
		// controls and none about the note, the seed or the ones not built yet, and a preset has
		// to carry a value for every parameter whether or not the style cares about it.
		float v[PHP_LEN];
		phraseParamDefaults(v);
		PhraseControls c;
		phraseStyle(style, c);
		phraseParamsFrom(c, v);

		// CAPITALISED, BECAUSE THE FILE NAME IS WHAT RACK PUTS IN THE MENU.
		std::string name = phraseStyleName(style);
		if (!name.empty())
			name[0] = (char) toupper(name[0]);
		if (writePreset(dir + "/" + name + ".vcvm", version, v))
			written++;
	}
	printf("presetgen: wrote %d presets to %s\n", written, dir.c_str());
	return written == NUM_PHRASE_STYLES ? 0 : 1;
}
