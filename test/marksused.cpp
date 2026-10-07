/** EVERY MARK A SCORE CARRIES, AND WHETHER WE DO ANYTHING WITH IT.

WHY THERE IS A PROGRAM FOR THIS. Ornaments were being found one at a time, by ear: a trill heard
missing in one bar, a short one in another, and no way to know what else was being passed over in
silence. A mark that is read and ignored sounds exactly like a mark that was never there.

So this counts every element inside <notations> and <ornaments> across whatever files it is given,
and says for each whether the reader acts on it. What it cannot say is whether what we do with one
is RIGHT — only that something is done.

	make marksused ARGS="$HOME/Downloads/*.mxl"
*/
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "pugixml.hpp"

/** WHAT THE READER ACTS ON. Kept here beside the census rather than inferred, so that adding a
mark to the reader and forgetting this list shows up as a mark reported unplayed. */
static const char* HANDLED[] = {
	"accent", "strong-accent", "staccato", "staccatissimo", "tenuto", "slur", "tied", "tie",
	"arpeggiate", "tremolo", "trill-mark", "dynamics", "fermata-ignored-deliberately",
};

static bool handled(const std::string& name) {
	for (size_t i = 0; i < sizeof(HANDLED) / sizeof(HANDLED[0]); i++) {
		if (name == HANDLED[i])
			return true;
	}
	return false;
}

static void walk(const pugi::xml_node& node, std::map<std::string, int>& seen) {
	for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
		if (c.type() != pugi::node_element)
			continue;
		seen[c.name()]++;
		walk(c, seen);
	}
}

int main(int argc, char** argv) {
	std::map<std::string, int> all;
	std::map<std::string, std::set<std::string> > where;
	for (int a = 1; a < argc; a++) {
		std::ifstream f(argv[a], std::ios::binary);
		if (!f)
			continue;
		std::stringstream buf;
		buf << f.rdbuf();
		std::string text = buf.str();
		// A COMPRESSED FILE IS A ZIP, opened the way the reader opens one.
		if (text.size() > 4 && text[0] == 'P' && text[1] == 'K') {
			const std::vector<uint8_t> zip(text.begin(), text.end());
			std::vector<uint8_t> got;
			if (!px::gpZipEntry(zip, "score.xml", got)) {
				std::vector<uint8_t> container;
				std::string inside = "score.xml";
				if (px::gpZipEntry(zip, "META-INF/container.xml", container)) {
					const std::string c((const char*) container.data(), container.size());
					const size_t at = c.find("full-path");
					const size_t q1 = (at == std::string::npos) ? at : c.find('"', at);
					const size_t q2 = (q1 == std::string::npos) ? q1 : c.find('"', q1 + 1);
					if (q2 != std::string::npos)
						inside = c.substr(q1 + 1, q2 - q1 - 1);
				}
				if (!px::gpZipEntry(zip, inside, got))
					continue;
			}
			text.assign((const char*) got.data(), got.size());
		}
		pugi::xml_document doc;
		if (!doc.load_buffer(text.data(), text.size()))
			continue;
		const char* slash = std::strrchr(argv[a], '/');
		const std::string name = slash ? slash + 1 : argv[a];
		std::map<std::string, int> seen;
		for (pugi::xpath_node n : doc.select_nodes("//notations"))
			walk(n.node(), seen);
		for (std::map<std::string, int>::const_iterator i = seen.begin(); i != seen.end(); ++i) {
			all[i->first] += i->second;
			where[i->first].insert(name);
		}
	}
	std::printf("%-22s %8s  %5s  %s\n", "mark", "count", "files", "played");
	for (std::map<std::string, int>::const_iterator i = all.begin(); i != all.end(); ++i) {
		std::printf("%-22s %8d  %5d  %s\n", i->first.c_str(), i->second,
			(int) where[i->first].size(), handled(i->first) ? "yes" : "NO");
	}
	return 0;
}
