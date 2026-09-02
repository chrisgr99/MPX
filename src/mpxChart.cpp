#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "IReal.hpp"

#include <osdialog.h>

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace px {


/** One song in the library: what it is called, and the text it came from.

THE CHUNK IS THE STORAGE FORMAT. A song's own slice of the iReal payload re-parses in
microseconds, so nothing about the parsed chart is ever written down — no cells serialised, no
version to migrate, and no way for the stored form to disagree with the parser. It is also
small enough to keep inside a patch, which is what makes a patch open on a machine that has
never seen your playlists. */
struct LibraryEntry {
	std::string playlist;
	std::string title;
	std::string composer;
	std::string style;
	std::string chunk;
};

static std::vector<LibraryEntry> gLibrary;
static bool gLibraryLoaded = false;


static std::string libraryPath() {
	return asset::user("DreamerMPX/charts.json");
}


static void libraryLoad() {
	gLibraryLoaded = true;
	FILE* file = std::fopen(libraryPath().c_str(), "r");
	if (!file)
		return;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!rootJ)
		return;
	json_t* songsJ = json_object_get(rootJ, "songs");
	if (json_is_array(songsJ)) {
		size_t i;
		json_t* songJ;
		json_array_foreach(songsJ, i, songJ) {
			LibraryEntry e;
			auto str = [&](const char* key) {
				json_t* j = json_object_get(songJ, key);
				return json_is_string(j) ? std::string(json_string_value(j)) : std::string();
			};
			e.playlist = str("playlist");
			e.title = str("title");
			e.composer = str("composer");
			e.style = str("style");
			e.chunk = str("chunk");
			if (!e.chunk.empty())
				gLibrary.push_back(e);
		}
	}
	json_decref(rootJ);
}


static void librarySave() {
	json_t* songsJ = json_array();
	for (const LibraryEntry& e : gLibrary) {
		json_t* songJ = json_object();
		json_object_set_new(songJ, "playlist", json_string(e.playlist.c_str()));
		json_object_set_new(songJ, "title", json_string(e.title.c_str()));
		json_object_set_new(songJ, "composer", json_string(e.composer.c_str()));
		json_object_set_new(songJ, "style", json_string(e.style.c_str()));
		json_object_set_new(songJ, "chunk", json_string(e.chunk.c_str()));
		json_array_append_new(songsJ, songJ);
	}
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "songs", songsJ);
	system::createDirectories(asset::user("DreamerMPX"));
	FILE* file = std::fopen(libraryPath().c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(1));
		std::fclose(file);
	}
	json_decref(rootJ);
}


/** Reads an iReal Pro HTML export into the library. Returns how many songs were added, and
replaces any playlist of the same name rather than growing a second copy of it. */
static int libraryImport(const std::string& path) {
	if (!gLibraryLoaded)
		libraryLoad();

	std::string html;
	{
		FILE* file = std::fopen(path.c_str(), "rb");
		if (!file)
			return 0;
		char buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0)
			html.append(buf, n);
		std::fclose(file);
	}

	const std::string payload = irealPayloadFromHtml(html);
	if (payload.empty())
		return 0;

	// The songs are kept as their own chunks, so the library holds what the file held.
	std::string name;
	std::vector<std::string> chunks;
	{
		size_t i = 0;
		while (true) {
			const size_t at = payload.find("===", i);
			if (at == std::string::npos) {
				chunks.push_back(payload.substr(i));
				break;
			}
			chunks.push_back(payload.substr(i, at - i));
			i = at + 3;
		}
		if (chunks.size() > 1) {
			name = chunks.back();
			chunks.pop_back();
		}
	}
	if (name.empty())
		name = system::getStem(path);

	gLibrary.erase(std::remove_if(gLibrary.begin(), gLibrary.end(),
		[&](const LibraryEntry& e) { return e.playlist == name; }), gLibrary.end());

	int added = 0;
	for (const std::string& chunk : chunks) {
		const Song song = irealParseSong(chunk);
		if (song.title.empty() || song.cells.empty())
			continue;
		LibraryEntry e;
		e.playlist = name;
		e.title = song.title;
		e.composer = song.composer;
		e.style = song.style;
		e.chunk = chunk;
		gLibrary.push_back(e);
		added++;
	}
	librarySave();
	return added;
}


struct ChartModule : Module, NoteSource {
	enum ParamId {
		P_TRANSPOSE,
		P_TEMPO,
		NUM_PARAMS
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		NUM_OUTPUTS
	};
	enum LightId {
		L_BEAT,
		NUM_LIGHTS
	};

	int slot = -1;
	uint32_t generation = 0;
	double beats = 0.0;
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	float beatSeconds = 0.5f;
	float sinceLastPulse = 0.f;

	/** WHAT IS LOADED, IN TWO COPIES. Choosing a song happens on the drawing thread while the
	audio thread is reading the one already there, so the new one is built beside it and the
	index is then moved. Nothing is freed, so a reader part way through the old one finishes
	against memory that is still its own. */
	struct Loaded {
		Song song;
		Expansion expansion;
	};
	Loaded loaded[2];
	std::atomic<int> live{0};
	std::atomic<bool> haveSong{false};
	/** Kept so the patch can carry the song rather than a reference to one. */
	std::string chunk;
	std::string playlist;
	std::atomic<int> playingSpan{-1};

	ChartModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_TRANSPOSE, -11.f, 11.f, 0.f, "Transpose", " semitones");
		paramQuantities[P_TRANSPOSE]->snapEnabled = true;
		configParam(P_TEMPO, 30.f, 300.f, 120.f, "Tempo (when no clock is patched)", " bpm");
		configInput(I_CLOCK, "Clock");
		configInput(I_RESET, "Reset");
		configOutput(O_MPX, "MPX note out");
		slot = busClaim(&generation);
	}

	~ChartModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	void onReset() override {
		beats = 0.0;
	}

	/** Main thread. Parses the chunk into the copy that is not being read, then moves over. */
	void setSong(const std::string& newChunk, const std::string& fromPlaylist) {
		const int spare = 1 - live.load();
		loaded[spare].song = irealParseSong(newChunk);
		loaded[spare].expansion = irealExpand(loaded[spare].song);
		chunk = newChunk;
		playlist = fromPlaylist;
		live.store(spare);
		haveSong.store(!loaded[spare].expansion.spans.empty());
		beats = 0.0;
	}

	const Loaded& current() {
		return loaded[live.load()];
	}

	Key playingKey() {
		Key key = current().song.key;
		const int shift = (int) std::round(params[P_TRANSPOSE].getValue());
		key.tonic = (int8_t) (((key.tonic + shift) % 12 + 12) % 12);
		return key;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		// THE SONG ITSELF, not a reference to one. A patch that pointed at a library entry
		// would open to silence on a machine that had never imported that playlist.
		json_object_set_new(rootJ, "chunk", json_string(chunk.c_str()));
		json_object_set_new(rootJ, "playlist", json_string(playlist.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* chunkJ = json_object_get(rootJ, "chunk");
		json_t* playlistJ = json_object_get(rootJ, "playlist");
		if (json_is_string(chunkJ) && json_string_value(chunkJ)[0] != '\0') {
			setSong(json_string_value(chunkJ),
				json_is_string(playlistJ) ? json_string_value(playlistJ) : "");
		}
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		if (slot < 0 || !haveSong.load())
			return;

		if (resetTrigger.process(inputs[I_RESET].getVoltage(), 0.1f, 1.f))
			beats = 0.0;

		sinceLastPulse += args.sampleTime;
		if (inputs[I_CLOCK].isConnected()) {
			if (clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f)) {
				if (sinceLastPulse > 0.001f && sinceLastPulse < 10.f)
					beatSeconds = sinceLastPulse;
				sinceLastPulse = 0.f;
				beats = std::floor(beats) + 1.0;
			}
			else if (beatSeconds > 0.f) {
				beats += args.sampleTime / beatSeconds;
			}
		}
		else {
			beatSeconds = 60.f / std::fmax(1.f, params[P_TEMPO].getValue());
			beats += args.sampleTime / beatSeconds;
		}

		const Loaded& L = loaded[live.load()];
		const Expansion& e = L.expansion;
		if (e.spans.empty() || e.totalBeats <= 0.f)
			return;

		const double pos = std::fmod(std::fmod(beats, (double) e.totalBeats)
			+ e.totalBeats, (double) e.totalBeats);

		int at = 0;
		for (int i = 0; i < (int) e.spans.size(); i++) {
			if (pos < e.spans[i].endBeat) {
				at = i;
				break;
			}
		}

		const int beatsPerBar = (L.song.unit == 4 && L.song.beats == 3) ? 3 : 4;

		Harmony h;
		h.valid = true;
		h.key = playingKey();
		h.current = e.spans[at].chord;
		h.next = e.spans[(at + 1) % e.spans.size()].chord;
		h.after = e.spans[(at + 2) % e.spans.size()].chord;
		h.beatsToNext = (float) (e.spans[at].endBeat - pos);
		h.beat = pos;
		h.cycleBeats = e.totalBeats;
		h.barBeats = (uint8_t) beatsPerBar;
		h.barUnit = 4;
		h.bar = (int) (pos / beatsPerBar);
		h.beatInBar = (float) (pos - h.bar * beatsPerBar);
		busPublishHarmony(slot, h);

		playingSpan.store(at);
		lights[L_BEAT].setBrightness(std::fmod(pos, 1.0) < 0.25 ? 1.f : 0.f);
	}
};


/** The chart, expanded, four bars to a line.

THE EXPANDED FORM, NOT THE FOLDED ONE. Drawing repeat marks, endings and section brackets where
they belong is the seven hundred lines this deliberately does not write. Written out flat there
is nothing to lay out — a bar is a bar — and the whole of the music is on screen in the order it
is played, which for reading along is arguably better anyway. */
struct ChartDisplay : widget::Widget {
	ChartModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> body =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!body || body->handle < 0)
			return;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x35, 0x3c, 0x47));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		if (!module || !module->haveSong.load()) {
			nvgFontFaceId(args.vg, body->handle);
			nvgFontSize(args.vg, 11.f);
			nvgFillColor(args.vg, nvgRGB(0x6a, 0x72, 0x7e));
			nvgText(args.vg, 8.f, 20.f, "No chart loaded.", NULL);
			nvgText(args.vg, 8.f, 36.f, "Right-click to import an iReal Pro", NULL);
			nvgText(args.vg, 8.f, 50.f, "playlist, then choose a song.", NULL);
			return;
		}

		const ChartModule::Loaded& L = module->current();
		const Song& song = L.song;
		const Expansion& e = L.expansion;
		const Key key = module->playingKey();
		const int at = module->playingSpan.load();

		// ---- the head ----
		nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle : body->handle);
		nvgFontSize(args.vg, 13.f);
		nvgFillColor(args.vg, PANEL_INK);
		nvgText(args.vg, 8.f, 13.f, song.title.c_str(), NULL);

		nvgFontFaceId(args.vg, body->handle);
		nvgFontSize(args.vg, 9.f);
		nvgFillColor(args.vg, PANEL_DIM);
		char head[128];
		std::snprintf(head, sizeof(head), "%s%s%s   %s %s   %d/%d   %d bars",
			song.composer.c_str(), song.composer.empty() ? "" : " — ", song.style.c_str(),
			pitchClassNameIn(key.tonic, key), key.minor ? "minor" : "major",
			song.beats, song.unit, e.bars);
		nvgText(args.vg, 8.f, 26.f, head, NULL);

		if (!song.supported) {
			nvgFillColor(args.vg, nvgRGB(0xff, 0x9a, 0x3c));
			char why[96];
			std::snprintf(why, sizeof(why), "played as 4/4: %s", song.why.c_str());
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x - 8.f, 13.f, why, NULL);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}

		// ---- the bars ----
		const float left = 6.f;
		const float cell = (box.size.x - 12.f) / 4.f;
		const float top = 44.f;
		const float lineHeight = 20.f;
		const int beatsPerBar = (song.unit == 4 && song.beats == 3) ? 3 : 4;

		// Which bar each span belongs to, so several chords share one cell.
		const int lines = (int) ((box.size.y - top - 4.f) / lineHeight);
		const int barsShown = lines * 4;
		int firstBar = 0;
		if (e.bars > barsShown && at >= 0 && at < (int) e.spans.size()) {
			// Scrolled by whole LINES rather than by bars, so the chart does not jitter
			// sideways in the reading — a line at a time is how the eye follows one.
			const int playingBar = (int) (e.spans[at].startBeat / beatsPerBar);
			const int playingLine = playingBar / 4;
			const int firstLine = clamp(playingLine - lines / 2, 0,
				std::max(0, (e.bars + 3) / 4 - lines));
			firstBar = firstLine * 4;
		}

		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int b = firstBar; b < e.bars && b < firstBar + barsShown; b++) {
			const int shown = b - firstBar;
			const float x = left + (shown % 4) * cell;
			const float y = top + (shown / 4) * lineHeight;

			// Every chord that starts in this bar.
			int count = 0;
			int firstIdx = -1;
			bool playingHere = false;
			for (int i = 0; i < (int) e.spans.size(); i++) {
				const int bar = (int) (e.spans[i].startBeat / beatsPerBar);
				if (bar != b)
					continue;
				if (firstIdx < 0)
					firstIdx = i;
				if (i == at)
					playingHere = true;
				count++;
			}

			if (playingHere) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x + 1.f, y - 8.f, cell - 2.f, 17.f, 3.f);
				nvgFillColor(args.vg, nvgRGBA(0xff, 0x3c, 0xc8, 0x2e));
				nvgFill(args.vg);
			}

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x, y - 8.f);
			nvgLineTo(args.vg, x, y + 9.f);
			nvgStrokeColor(args.vg, nvgRGB(0x3a, 0x41, 0x4c));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			// The section letter, above the bar that opens it.
			if (firstIdx >= 0 && e.spans[firstIdx].section != 0) {
				nvgFontSize(args.vg, 8.f);
				nvgFillColor(args.vg, nvgRGB(0x3d, 0xd6, 0x8c));
				char sec[4] = {e.spans[firstIdx].section, 0};
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, x + 3.f, y - 12.f, sec, NULL);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			}

			nvgFontSize(args.vg, count > 2 ? 8.f : 10.f);
			for (int k = 0; k < count; k++) {
				const int i = firstIdx + k;
				const float cx = x + cell * (k + 0.5f) / count;
				nvgFillColor(args.vg, i == at ? nvgRGB(0xff, 0x3c, 0xc8) : PANEL_INK);
				nvgText(args.vg, cx, y,
					e.spans[i].noChord ? "N.C." : chordLetter(e.spans[i].chord, key).c_str(),
					NULL);
			}
		}
		// The final barline.
		{
			const int last = std::min(e.bars, firstBar + barsShown) - firstBar;
			if (last > 0) {
				const int col = (last - 1) % 4;
				const float x = left + (col + 1) * cell;
				const float y = top + ((last - 1) / 4) * lineHeight;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, x, y - 8.f);
				nvgLineTo(args.vg, x, y + 9.f);
				nvgStrokeColor(args.vg, nvgRGB(0x3a, 0x41, 0x4c));
				nvgStroke(args.vg);
			}
		}
	}
};


// ---- panel -------------------------------------------------------------------------------

static Layout chartLayout() {
	Layout L;
	L.hp = 22.f;
	L.title = "mpxChart";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id,
			const std::string& name, const char* style = "knob") {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = style;
		L.items.push_back(i);
		label(key + ".label", x, y + 9.f, name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.f, name, Panel::CENTRE, size > 0.f, size, key);
	};

	knob("p.transpose", 16.f, 114.f, ChartModule::P_TRANSPOSE, "TRANSPOSE");
	knob("p.tempo", 42.f, 114.f, ChartModule::P_TEMPO, "TEMPO");
	jack("in.clock", Item::PORT_IN, 62.f, 114.f, ChartModule::I_CLOCK, "clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, 78.f, 114.f, ChartModule::I_RESET, "reset", SIG_GATE);
	jack("out.mpx", Item::PORT_OUT, 99.f, 114.f, ChartModule::O_MPX, "mpxOut", NOTE_CABLE, 12.f);

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = ChartModule::L_BEAT;
	lamp.x = 90.f; lamp.y = 110.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}


struct ChartWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ChartWidget(ChartModule* module) {
		setModule(module);
		layout = chartLayout();
		layoutApplyUser("mpxChart", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		ChartDisplay* display = new ChartDisplay;
		display->module = module;
		display->box.pos = mm2px(math::Vec(3.f, 13.f));
		display->box.size = mm2px(math::Vec(105.f, 89.f));
		addChild(display);
	}

	/** The songs of one playlist, behind a letter each, because a menu of fourteen hundred is
	not a menu. */
	void addPlaylistMenu(ui::Menu* menu, ChartModule* chart, const std::string& playlist) {
		std::vector<char> letters;
		for (const LibraryEntry& e : gLibrary) {
			if (e.playlist != playlist || e.title.empty())
				continue;
			const char c = (char) std::toupper((unsigned char) e.title[0]);
			if (std::find(letters.begin(), letters.end(), c) == letters.end())
				letters.push_back(c);
		}
		std::sort(letters.begin(), letters.end());
		for (char c : letters) {
			menu->addChild(createSubmenuItem(std::string(1, c), "",
				[=](ui::Menu* sub) {
					for (const LibraryEntry& e : gLibrary) {
						if (e.playlist != playlist || e.title.empty())
							continue;
						if ((char) std::toupper((unsigned char) e.title[0]) != c)
							continue;
						const std::string chunk = e.chunk;
						const std::string list = e.playlist;
						sub->addChild(createCheckMenuItem(e.title, e.composer,
							[=]() { return chart->chunk == chunk; },
							[=]() { chart->setSong(chunk, list); }));
					}
				}));
		}
	}

	void appendContextMenu(ui::Menu* menu) override {
		ChartModule* chart = dynamic_cast<ChartModule*>(module);
		if (!chart)
			return;
		if (!gLibraryLoaded)
			libraryLoad();

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Charts"));

		menu->addChild(createMenuItem("Import an iReal Pro playlist\u2026", "", []() {
			// The HTML export iReal Pro produces, which is what people actually have.
			osdialog_filters* filters = osdialog_filters_parse("HTML:html,htm");
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			const int added = libraryImport(path);
			std::free(path);
			if (added == 0) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"No songs were found in that file.\n\n"
					"It should be an iReal Pro playlist exported as HTML.");
			}
		}));

		// The playlists that have been imported, each behind its own submenu.
		std::vector<std::string> playlists;
		for (const LibraryEntry& e : gLibrary) {
			if (std::find(playlists.begin(), playlists.end(), e.playlist) == playlists.end())
				playlists.push_back(e.playlist);
		}
		std::sort(playlists.begin(), playlists.end());
		for (const std::string& playlist : playlists) {
			int count = 0;
			for (const LibraryEntry& e : gLibrary) {
				if (e.playlist == playlist)
					count++;
			}
			menu->addChild(createSubmenuItem(playlist, string::f("%d", count),
				[this, chart, playlist](ui::Menu* sub) { addPlaylistMenu(sub, chart, playlist); }));
		}

		if (playlists.empty())
			menu->addChild(createMenuLabel("Nothing imported yet"));

		layoutAppendMenu(menu, this, panel, &layout, "mpxChart");
	}

	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		PortWidget* port = getOutput(ChartModule::O_MPX);
		if (!port)
			return;
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
			engine::Cable* cable = cw->getCable();
			if (cable && isMPXInput(cable->inputModule, cable->inputId))
				cw->color = NOTE_CABLE;
		}
	}
};


} // namespace px


Model* modelChart = createModel<px::ChartModule, px::ChartWidget>("mpxChart");
