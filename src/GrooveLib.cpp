/** See GrooveLib.hpp. */
#include "GrooveLib.hpp"

#include <algorithm>
#include <jansson.h>

namespace px {


const float GROOVE_PITCH[GP_PARTS] = {
	-2.000f,   // kick, 36
	-1.833f,   // snare, 38
	-1.917f,   // side stick, 37
	-1.750f,   // hand clap, 39
	-1.500f,   // closed hat, 42
	-1.167f,   // open hat, 46
	-1.250f,   // low tom, 45
	-0.833f,   // high tom, 50
	-0.750f,   // ride, 51
	-0.917f,   // crash, 49
};

static const char* PART_WORDS[GP_PARTS] = {
	"kick", "snare", "stick", "clap", "hat", "open", "tomlo", "tomhi", "ride", "crash",
};


const char* groovePartName(int part) {
	if (part < 0 || part >= GP_PARTS)
		return "kick";
	return PART_WORDS[part];
}


int groovePartFor(const std::string& word) {
	for (int i = 0; i < GP_PARTS; i++) {
		if (word == PART_WORDS[i])
			return i;
	}
	return -1;
}


/** THE ONE THAT IS ALWAYS THERE. A module with no library at all would sit silent with no way of
saying why; this at least plays something recognisable. */
static Groove builtIn() {
	Groove g;
	g.name = "Straight eights";
	g.genre = "Basic";
	g.bars = 1;
	g.beats = 4;
	const float hats[8] = {0.f, 0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f};
	for (int i = 0; i < 8; i++) {
		GrooveHit h;
		h.beat = hats[i];
		h.part = GP_HAT;
		h.weight = (i % 2 == 0) ? 0.7f : 0.5f;
		g.hits.push_back(h);
	}
	GrooveHit k1; k1.beat = 0.f; k1.part = GP_KICK; k1.weight = 1.f;
	GrooveHit k2; k2.beat = 2.f; k2.part = GP_KICK; k2.weight = 0.85f;
	GrooveHit s1; s1.beat = 1.f; s1.part = GP_SNARE; s1.weight = 1.f;
	GrooveHit s2; s2.beat = 3.f; s2.part = GP_SNARE; s2.weight = 1.f;
	g.hits.push_back(k1);
	g.hits.push_back(k2);
	g.hits.push_back(s1);
	g.hits.push_back(s2);
	return g;
}


static bool readHits(json_t* arrayJ, std::vector<GrooveHit>& out) {
	if (!json_is_array(arrayJ))
		return false;
	size_t i;
	json_t* hitJ;
	json_array_foreach(arrayJ, i, hitJ) {
		if (!json_is_object(hitJ))
			continue;
		GrooveHit h;
		if (json_t* j = json_object_get(hitJ, "bar"))
			h.bar = (int) json_integer_value(j);
		if (json_t* j = json_object_get(hitJ, "beat"))
			h.beat = (float) json_number_value(j);
		if (json_t* j = json_object_get(hitJ, "weight"))
			h.weight = (float) json_number_value(j);
		const char* word = json_string_value(json_object_get(hitJ, "part"));
		const int part = word ? groovePartFor(word) : -1;
		if (part < 0)
			continue;   // a part nobody has heard of is left out rather than guessed at
		h.part = part;
		out.push_back(h);
	}
	return !out.empty();
}


static void readFile(const std::string& path, std::vector<Groove>& into) {
	json_error_t err;
	json_t* rootJ = json_load_file(path.c_str(), 0, &err);
	if (!rootJ) {
		WARN("mpxGroove: %s line %d: %s", path.c_str(), err.line, err.text);
		return;
	}
	// A file is an array of grooves, or one groove on its own.
	std::vector<json_t*> each;
	if (json_is_array(rootJ)) {
		size_t i;
		json_t* g;
		json_array_foreach(rootJ, i, g)
			each.push_back(g);
	}
	else {
		each.push_back(rootJ);
	}
	for (json_t* gj : each) {
		Groove g;
		if (const char* s = json_string_value(json_object_get(gj, "name")))
			g.name = s;
		if (const char* s = json_string_value(json_object_get(gj, "genre")))
			g.genre = s;
		if (json_t* j = json_object_get(gj, "bars"))
			g.bars = std::max(1, (int) json_integer_value(j));
		if (json_t* j = json_object_get(gj, "beats"))
			g.beats = std::max(1, (int) json_integer_value(j));
		if (json_t* j = json_object_get(gj, "swing"))
			g.swing = (float) json_number_value(j);
		if (!readHits(json_object_get(gj, "hits"), g.hits))
			continue;   // a groove with no strokes is not a groove
		readHits(json_object_get(gj, "fill"), g.fill);
		if (g.name.empty())
			g.name = "Unnamed";
		// A GROOVE OF THE SAME NAME REPLACES the one already there, which is what makes the user
		// folder an override of ours rather than a second copy of it.
		bool replaced = false;
		for (Groove& had : into) {
			if (had.name == g.name && had.genre == g.genre) {
				had = g;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			into.push_back(g);
	}
	json_decref(rootJ);
}


static void readFolder(const std::string& folder, std::vector<Groove>& into) {
	if (!system::isDirectory(folder))
		return;
	std::vector<std::string> entries = system::getEntries(folder);
	std::sort(entries.begin(), entries.end());
	for (const std::string& path : entries) {
		if (system::getExtension(path) == ".json")
			readFile(path, into);
	}
}


const std::vector<Groove>& grooveLibrary() {
	static std::vector<Groove> library;
	static bool loaded = false;
	if (loaded)
		return library;
	loaded = true;
	readFolder(asset::plugin(pluginInstance, "grooves"), library);
	readFolder(asset::user("DreamerMPX/grooves"), library);
	if (library.empty())
		library.push_back(builtIn());
	std::sort(library.begin(), library.end(), [](const Groove& a, const Groove& b) {
		if (a.genre != b.genre)
			return a.genre < b.genre;
		return a.name < b.name;
	});
	INFO("mpxGroove: %d grooves", (int) library.size());
	return library;
}


int grooveCount() {
	return (int) grooveLibrary().size();
}


const Groove& grooveAt(int index) {
	const std::vector<Groove>& lib = grooveLibrary();
	if (index < 0)
		index = 0;
	if (index >= (int) lib.size())
		index = (int) lib.size() - 1;
	return lib[index];
}


} // namespace px
