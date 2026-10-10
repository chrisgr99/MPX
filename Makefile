# MPX — see README.md
#
#   make                 build the plugin
#   make install         build and copy into Rack's user plugins folder
#   make dist            build a .vcvplugin package for distribution

RACK_DIR ?= ../Rack-SDK

SOURCES += $(wildcard src/*.cpp)

# WHICH PLATFORM, decided before the SDK is included. ARCH_MAC is set by the SDK's arch.mk, which
# is pulled in by plugin.mk at the bottom of this file — too late to choose sources with. The same
# test arch.mk makes is made here instead, honouring CROSS_COMPILE so that a Windows or Linux build
# cross-compiled on a Mac is still seen as Windows or Linux.
ifdef CROSS_COMPILE
	TARGET_MACHINE := $(CROSS_COMPILE)
else
	TARGET_MACHINE := $(shell $(CC) -dumpmachine)
endif

# OBJECTIVE-C++ ONLY ON MACOS. Reading a picture off the clipboard means asking the system's
# pasteboard, which is Cocoa. The plain C++ file beside it answers the same question with "no"
# everywhere else, so nothing above this layer has to know which platform it is on.
ifneq (,$(findstring -darwin,$(TARGET_MACHINE)))
    SOURCES += $(wildcard src/*.mm)
endif

# THE ONLY THING IN res IS TYPE. Every panel is drawn in code and the plugin ships no artwork,
# but a chord chart needs music symbols — the major-seventh triangle, the diminished circle, the
# segno and coda, the measure-repeat marks — and those are glyphs, not shapes to be approximated
# with a few strokes. Petaluma is Steinberg's handwritten music font, under the Open Font
# Licence, which is redistributed here with its licence beside it.
DISTRIBUTABLES += res
DISTRIBUTABLES += help
# THE GROOVE LIBRARY IS NOT SHIPPED. It is read by mpxGroove, which is parked on the `parked`
# branch along with the rest of the modules that generate a part from a chord grid; the files
# stay here because the module comes back if that work is taken up again.

DISTRIBUTABLES += $(wildcard LICENSE*)

# The pasteboard is Cocoa, so the framework is named explicitly. Tested the same way as the
# sources above, since arch.mk has not been included yet and ARCH_MAC is therefore not set here.
ifneq (,$(findstring -darwin,$(TARGET_MACHINE)))
	LDFLAGS += -framework Cocoa
endif

# ---- WHAT THE PLUGIN IS BUILT ON TOP OF -------------------------------------------------------
#
# Two libraries, neither of them in this repository: FluidSynth plays a SoundFont, and Verovio
# engraves a score. They are built into dep/ by the rules at the foot of this file, which is the
# arrangement Rack's own dep.mk is for — the library's build machine runs `make cleandep`,
# `make dep` and `make dist` in that order, in a container whose cross compilers it hands us.
#
# So a clean checkout builds, here and there, and neither library is carried in the repository.
DEPS += $(DEP_PATH)/lib/libfluidsynth.a
DEPS += $(DEP_PATH)/lib/libverovio.a

# VEROVIO'S HEADERS INCLUDE EACH OTHER WITHOUT A PREFIX, because its own build puts every one of
# these directories on the include path. Ours has to as well, or the first header pulls in a
# second that cannot be found.
FLAGS += -I dep/include
FLAGS += -I dep/include/verovio -I dep/include/verovio/vrv -I dep/include/verovio/crc
FLAGS += -I dep/include/verovio/json -I dep/include/verovio/midi -I dep/include/verovio/pugi
FLAGS += -I dep/include/verovio/zip -I dep/include/verovio/tuning-library
LDFLAGS += $(DEP_PATH)/lib/libverovio.a $(DEP_PATH)/lib/libfluidsynth.a

# A framework each library wants, named explicitly on the Mac. Tested with TARGET_MACHINE and not
# ARCH_MAC, because arch.mk has not been included yet — the same reason as the test above.
ifneq (,$(findstring -darwin,$(TARGET_MACHINE)))
	LDFLAGS += -framework CoreFoundation
endif
ifneq (,$(findstring -mingw,$(TARGET_MACHINE)))
	LDFLAGS += -lws2_32 -lole32
endif

include $(RACK_DIR)/plugin.mk

# C++20 FOR THE WHOLE PLUGIN, not for the one file that needs it. Verovio is written in C++20 and
# Rack's SDK compiles plugins at C++11; mixing two standards in one binary works until the day a
# type crosses the boundary and does not. The SDK's own flag comes first and this one wins, and
# everything here compiled at C++20 without a single change.
CXXFLAGS += -std=c++20


# DEVELOPMENT INSTALL, and why it is not `make install`.
#
# `make install` copies a .vcvplugin PACKAGE into the plugins folder, and Rack unpacks it on
# startup. Copy one while Rack is running and it can be cleared at shutdown without ever being
# unpacked — so the next start loads the old build, having silently thrown the new one away.
# That cost an afternoon of "why has nothing changed".
#
# This writes the built files straight into the folder Rack loads, and removes any package that
# might otherwise overwrite them with something older. Quit Rack, run this, start Rack.
RACK_USER_DIR ?= $(HOME)/Library/Application Support/Rack2
PLUGIN_DIR = $(RACK_USER_DIR)/plugins-mac-arm64/$(SLUG)

# REMOVED THEN WRITTEN, never copied over. Copying onto an existing dylib rewrites the same
# file in place, and macOS holds that file's code signature against its cached pages: the pages
# change, the signature does not match, and the kernel kills Rack the moment it tries to load
# it — "Code Signature Invalid", before any of our code runs.
#
# Unlinking first means the new file is a new file, with nothing cached against it. It is then
# signed ad-hoc, which is what Apple Silicon requires of any library it is asked to load.
dev: $(TARGET)
	@rm -f "$(RACK_USER_DIR)/plugins-mac-arm64/"$(SLUG)-*.vcvplugin
	@codesign --force --sign - $(TARGET) 2>/dev/null || true
	@mkdir -p "$(PLUGIN_DIR)"
	@rm -f "$(PLUGIN_DIR)/plugin.dylib"
	@cp $(TARGET) "$(PLUGIN_DIR)/plugin.dylib"
	@cp plugin.json "$(PLUGIN_DIR)/"
	@rm -rf "$(PLUGIN_DIR)/grooves"
	@rm -rf "$(PLUGIN_DIR)/res"
	@cp -R res "$(PLUGIN_DIR)/"
	# BOTH PRESET SETS BELONGED TO PARKED MODULES, so there may be no presets folder at all.
	@rm -rf "$(PLUGIN_DIR)/presets"
	@cp -R presets "$(PLUGIN_DIR)/" 2>/dev/null || true
	@rm -rf "$(PLUGIN_DIR)/help"
	@cp -R help "$(PLUGIN_DIR)/" 2>/dev/null || true
	@cp LICENSE "$(PLUGIN_DIR)/" 2>/dev/null || true
	@xattr -c "$(PLUGIN_DIR)/plugin.dylib" 2>/dev/null || true
	@codesign -v "$(PLUGIN_DIR)/plugin.dylib" && echo "signature valid"
	@echo "installed to $(PLUGIN_DIR)"

.PHONY: dev

# THE PARSER'S OWN TEST, built without Rack because the parser has no Rack in it. Two thousand
# real charts in a second says more than one chart patched up and listened to.
test:
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		test/irealtest.cpp src/IReal.cpp src/Chord.cpp -o build/irealtest \
		-L$(RACK_DIR) -lRack 2>&1 | head -20
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/irealtest \
		"$(HOME)/ProgrammingProjects/GXW/Blues 50.html" \
		"$(HOME)/ProgrammingProjects/GXW/Pop 400.html" \
		"$(HOME)/ProgrammingProjects/GXW/Brazilian 220.html" \
		"$(HOME)/ProgrammingProjects/GXW/Jazz 1460.html"

charttest:
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		test/charttest.cpp src/ChartLayout.cpp src/IReal.cpp src/Chord.cpp \
		-o build/charttest -L$(RACK_DIR) -lRack 2>&1 | head -20
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/charttest $(ARGS) \
		"$(HOME)/ProgrammingProjects/GXW/Blues 50.html" \
		"$(HOME)/ProgrammingProjects/GXW/Pop 400.html" \
		"$(HOME)/ProgrammingProjects/GXW/Brazilian 220.html" \
		"$(HOME)/ProgrammingProjects/GXW/Jazz 1460.html"


phrasetest:
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		test/phrasetest.cpp src/Phrasing.cpp src/ChartLayout.cpp src/IReal.cpp src/Chord.cpp \
		-o build/phrasetest -L$(RACK_DIR) -lRack
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/phrasetest $(ARGS) \
		"$(HOME)/ProgrammingProjects/GXW/Blues 50.html" \
		"$(HOME)/ProgrammingProjects/GXW/Pop 400.html" \
		"$(HOME)/ProgrammingProjects/GXW/Brazilian 220.html" \
		"$(HOME)/ProgrammingProjects/GXW/Jazz 1460.html"

# THE FACTORY PRESETS, GENERATED. Rack's own Preset menu is what offers SONG and JAZZ, and their
# values are the two measured styles — see tools/presetgen.cpp for why they are not typed in.
presets:
	@mkdir -p build presets/mpxPhrase presets/mpxMelodyVoice
	@rm -f presets/mpxPhrase/*.vcvm presets/mpxMelodyVoice/*.vcvm
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		tools/presetgen.cpp src/Phrasing.cpp src/MelodyVoice.cpp src/Melodic.cpp src/Chord.cpp \
		src/ChartLayout.cpp -o build/presetgen -L$(RACK_DIR) -lRack
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/presetgen "$(VERSION)" presets

# WHAT A PATCH WOULD PLAY, IN WORDS. The chart's phrasing, the phrase generator and the voice's
# choice of note, run on the settings saved in a patch — see tools/phrasesim.cpp.
#   make phrasesim                    the patch in Downloads, its own chart
#   make phrasesim EXAMPLE=1 NOTES=1  the first built-in example, every note listed
PATCH ?= $(HOME)/Downloads/mpxphrase-musical.vcv
EXAMPLE ?= 0
NOTES ?= 0
phrasesim:
	@mkdir -p build
	@zstd -q -d -c "$(PATCH)" | tar -xO patch.json > build/sim-patch.json
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		tools/phrasesim.cpp src/Phrasing.cpp src/ChartLayout.cpp src/IReal.cpp src/Chord.cpp \
		src/Melodic.cpp src/MelodyVoice.cpp -o build/phrasesim -L$(RACK_DIR) -lRack
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/phrasesim build/sim-patch.json $(EXAMPLE) $(NOTES)

melodytest:
	@c++ -std=c++11 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		test/melodytest.cpp src/Melodic.cpp src/MelodyVoice.cpp src/Chord.cpp \
		-o build/melodytest -L$(RACK_DIR) -lRack
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/melodytest $(ARGS)

# Reading Guitar Pro files. No Rack in it, so it builds on its own and runs against a folder of
# real files: make gptest ARGS="~/Downloads/*.gp*"
gptest:
	@c++ -std=c++11 -O1 -Wall test/gptest.cpp src/GuitarPro.cpp src/GpTimeline.cpp src/OrnamentStyle.cpp src/Composers.cpp -o build/gptest
	@./build/gptest $(ARGS)


# Repeats, endings and jumps: the played order, against songs written in the test itself.
navtest:
	@c++ -std=c++11 -O1 -Wall test/navtest.cpp src/GuitarPro.cpp src/GpTimeline.cpp src/OrnamentStyle.cpp src/Composers.cpp \
		-o build/navtest
	@./build/navtest

# HOW LONG VEROVIO TAKES AND HOW MANY PAGES IT MAKES, without Rack's window. Drawing needs an
# OpenGL context and this has none, so it exercises everything up to the drawing — see
# test/scoretest.cpp.
#   make scoretest ARGS="a-file.musicxml"
scoretest:
	@mkdir -p build
	@c++ -std=c++20 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-I dep/include -I dep/include/verovio -I dep/include/verovio/vrv \
		-I dep/include/verovio/json -I dep/include/verovio/pugi \
		-I dep/include/verovio/crc -I dep/include/verovio/midi \
		-I dep/include/verovio/zip -I dep/include/verovio/tuning-library \
		test/scoretest.cpp src/Score.cpp src/ScoreDraw.cpp -o build/scoretest \
		dep/lib/libverovio.a -L$(RACK_DIR) -lRack
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/scoretest $(ARGS)

# GUITAR PRO WRITTEN OUT AS MUSICXML, and whether the bars add up. No Rack and no Verovio: the
# converter is plain C++ over the song model. See test/gpxmltest.cpp.
#   make gpxmltest ARGS="$(HOME)/Downloads/*.gp5"
#   make gpxmltest ARGS="-o /tmp/one.musicxml -t 0 a-file.gp4"
gpxmltest:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -Wextra -Wno-unused-parameter \
		test/gpxmltest.cpp src/GpMusicXML.cpp src/GuitarPro.cpp -o build/gpxmltest
	@./build/gpxmltest $(ARGS)

# WHAT A SCORE SENDS DOWN THE CABLE, as plain text, for the release testbed to play.
#   make scorenotes ARGS="a-piece.mxl 20" > notes.txt
# BARS, PICKUPS AND NUMBERING. What the reader made of a score's bar lines: which bar the page
# calls bar one, how long a pickup is, and where each played bar falls on the clock. A pickup is
# numbered nought and is shorter than the metre says, and both of those were wrong once.
#   make barcheck ARGS="a-score.mxl"
# EVERY MARK A SCORE CARRIES, AND WHETHER WE PLAY IT. Ornaments were being found one at a time by
# ear, and a mark read and ignored sounds exactly like one that was never there.
#   make marksused ARGS="$(HOME)/Downloads/*.mxl"
# NOTES THAT ARE PLAYED AND CANNOT BE HEARD: buried inside the same pitch, doubled at one moment,
# or far quieter than what surrounds them. Three faults running were notes buried rather than
# missing, which is the hardest kind to hear.
#   make buried ARGS="$(HOME)/Downloads/*.mxl"
buried:
	@mkdir -p build
	@c++ -std=c++11 -O2 -w -I dep/include/verovio/pugi \
		test/buried.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/buried
	@./build/buried $(ARGS)

marksused:
	@mkdir -p build
	@c++ -std=c++11 -O1 -w -I dep/include/verovio/pugi \
		test/marksused.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/marksused
	@./build/marksused $(ARGS)

barcheck:
	@mkdir -p build
	@c++ -std=c++11 -O1 -w -I dep/include/verovio/pugi \
		test/barcheck.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/barcheck
	@./build/barcheck $(ARGS)

# WHAT A DRUM TRACK LOOKS LIKE LEAVING THE READER: whether it is marked percussion, and whether
# its notes are General MIDI kit numbers.
#   make drumcheck ARGS="$(HOME)/Downloads/*.gp5"
drumcheck:
	@mkdir -p build
	@c++ -std=c++11 -O1 -w -I dep/include/verovio/pugi \
		test/drumcheck.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/drumcheck
	@./build/drumcheck $(ARGS)

# AND WHETHER A KIT CAN BE SELECTED AND SOUNDED AT ALL, at the size the chart modules run the
# synthesiser at. Drums come out of bank 128; a channel left on its startup default plays them on
# a piano, which is what a missing instrument sounds like.
#   make drumsound
drumsound:
	@mkdir -p build
	@c++ -std=c++11 -O1 -w -I dep/include test/drumsound.cpp src/FluidEngine.cpp \
		dep/lib/libfluidsynth.a -o build/drumsound -framework CoreAudio \
		-framework CoreFoundation -framework CoreMIDI -framework AudioUnit -framework AudioToolbox
	@./build/drumsound $(ARGS)

scorenotes:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall -I dep/include/verovio/pugi \
		test/scorenotes.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/scorenotes
	@./build/scorenotes $(ARGS)

# mpxPiano's RELEASE ENGINE, RUN OUTSIDE RACK. Its voice engine lives inside a Rack Module where
# nothing but Rack can reach it, and three changes to its damping were reported as working when
# they were not. A thing that cannot be run cannot be verified.
#   make pianorelease ARGS="notes.txt out.wav 20 0.5"
pianorelease:
	@mkdir -p build
	@python3 test/salamander-release.py $(ARGS)




# WHAT COMES IN WHEN A SCORE IS LOADED, by whichever reader the file calls for. The module's own
# path, without the module. See test/scoreread.cpp.
#   make scoreread ARGS="a-score.mxl"
scoreread:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/scoreread.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/scoreread
	@./build/scoreread $(ARGS)

# WHAT THE DERIVED HARMONY SAYS, bar by bar: the pitches a score implies and how much each one
# matters, which is what a generative module is given when there are no chord symbols to give it.
# See test/harmony.cpp.
#   make harmony ARGS="a-score.musicxml"
harmony:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/harmony.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpTimeline.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/harmony
	@./build/harmony $(ARGS)

# THE SHAPE OF A SCORE: bars, metre, repeats, what each voice is doing and where each one enters.
# The map two people need in order to say where a marking goes. See test/structure.cpp.
#   make structure ARGS="a-score.musicxml"
structure:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/structure.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/structure
	@./build/structure $(ARGS)

# A SCORE SAVED AS MUSICXML, every part of it, so it can be marked up with dynamics, hairpins,
# slurs and articulation that the reader then honours. See test/tomusicxml.cpp.
#   make tomusicxml ARGS="a-song.gp5 marked.musicxml"
tomusicxml:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/tomusicxml.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpMusicXML.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/tomusicxml
	@./build/tomusicxml $(ARGS)

# THE MUSICXML READER AGAINST THE WRITER: a track written out and read back should come home as
# the notes that went. Needs no file from outside. See test/mxtest.cpp.
#   make mxtest ARGS="a-file.gp4"
mxtest:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/mxtest.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GpMusicXML.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/mxtest
	@./build/mxtest $(ARGS)

# WHAT A FILE SAYS ABOUT TECHNIQUE: palm mutes, let ring, strums and dead notes, per track and
# per string. For answering "is it not playing this, or is it not in the file".
#   make pmcheck ARGS="a-file.gp4"
pmcheck:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall test/pmcheck.cpp src/GuitarPro.cpp -o build/pmcheck
	@./build/pmcheck $(ARGS)

# HOW OFTEN A TRANSCRIBER MARKS A STRUM AT ALL, across every file given. The answer is three
# chords in a thousand, which is why an unmarked chord is strummed anyway — see GpTimeline.cpp.
#   make brushscan ARGS="$(HOME)/Downloads/*.gp*"
brushscan:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall test/brushscan.cpp src/GuitarPro.cpp -o build/brushscan
	@./build/brushscan $(ARGS)

# WHETHER ANYTHING IS LEFT RINGING: how much sounds at once, and whether one string is ever
# asked to sound two notes together. A guitar has six strings, so more than six at a moment means
# something is ringing that a hand would have stopped. See test/ringcheck.cpp.
#   make ringcheck ARGS="a-file.gp4 0"
ringcheck:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall test/ringcheck.cpp src/GpTimeline.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GuitarPro.cpp \
		-o build/ringcheck
	@./build/ringcheck $(ARGS)

# WHICH WAY THE HAND GOES, bar by bar: how many notes fall in down-strokes and how many in ups.
#   make strumcheck ARGS="a-file.gp5 0"
strumcheck:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall test/strumcheck.cpp src/GpTimeline.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GuitarPro.cpp \
		-o build/strumcheck
	@./build/strumcheck $(ARGS)

# A PAGE OF A SCORE AS AN IMAGE, drawn by the real renderer into a hidden window. The only way to
# see what the score window will look like without the score window. See test/scorerender.cpp.
#   make scorerender ARGS="a-file.musicxml"
scorerender:
	@mkdir -p build
	@c++ -std=c++20 -O1 -Wall -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-I dep/include -I dep/include/verovio -I dep/include/verovio/vrv \
		-I dep/include/verovio/json -I dep/include/verovio/pugi \
		-I dep/include/verovio/crc -I dep/include/verovio/midi \
		-I dep/include/verovio/zip -I dep/include/verovio/tuning-library \
		test/scorerender.cpp src/Score.cpp src/ScoreDraw.cpp src/GuitarPro.cpp \
		-o build/scorerender \
		dep/lib/libverovio.a -L$(RACK_DIR) -lRack -framework OpenGL
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/scorerender $(ARGS)
	@sips -s format png build/score.ppm --out build/score.png >/dev/null 2>&1 \
		&& echo "wrote build/score.png"

# What each articulation does, one at a time, with the humanising turned off.
performtest:
	@c++ -std=c++11 -O1 -Wall test/performtest.cpp src/Perform.cpp -o build/performtest
	@./build/performtest

# One modelled guitar string: its tuning, decay and pick, measured, and a few files to listen to.
stringtest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/stringtest.cpp src/GuitarString.cpp -o build/stringtest
	@./build/stringtest

# The modelled guitar's articulations: palm mutes, dead notes, harmonics, hammer-ons, damping.
guitartest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/guitartest.cpp src/GuitarModel.cpp src/GuitarString.cpp -o build/guitartest
	@./build/guitartest

# The built-in band's level against mpxGuitar's, the same note through each.
leveltest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall -I dep/include -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		test/leveltest.cpp src/FluidEngine.cpp src/GuitarModel.cpp \
		src/GuitarString.cpp dep/lib/libfluidsynth.a -framework CoreFoundation \
		-L$(RACK_DIR) -lRack -o build/leveltest
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/leveltest $(ARGS)

# One modelled bowed string: tuning, the split loop, decay, moving the contact point. No Rack.
bowtest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/bowtest.cpp src/BowedString.cpp -o build/bowtest
	@./build/bowtest

# Where the bowed string plays and how its notes start: Schelleng's and Guettler's diagrams, phase 3
# of docs/bowed-strings.md. ARGS names one section: buildup, firstslip, guettler, attacks,
# schelleng, measured or listen.
bowplay:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/bowplay.cpp src/BowedString.cpp -o build/bowplay
	@./build/bowplay $(ARGS)

# The fretting hand: the shapes it finds against the familiar ones. No Rack in it.
fretboardtest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/fretboardtest.cpp src/Fretboard.cpp -o build/fretboardtest
	@./build/fretboardtest

# The finger picker's two hands, played through a progression. No Rack in them.
pickertest:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/pickertest.cpp src/FingerPicking.cpp src/Fretboard.cpp -o build/pickertest
	@./build/pickertest

# The same, through the performer and the modelled guitar, written to build/ to be listened to.
pickrender:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/pickrender.cpp src/FingerPicking.cpp src/Fretboard.cpp \
		src/Perform.cpp src/GuitarModel.cpp src/GuitarString.cpp -o build/pickrender
	@./build/pickrender

# mpxGuitarArticulation's envelopes and mix. No Rack in them.
voicetest:
	@c++ -std=c++11 -O1 -Wall test/voicetest.cpp src/GuitarVoice.cpp -o build/voicetest
	@./build/voicetest

# The SoundFont engine. No Rack in it either, so it runs against a bank from the command line.
soundtest:
	@c++ -std=c++11 -O1 -Wall -I dep/include test/soundtest.cpp src/FluidEngine.cpp \
		dep/lib/libfluidsynth.a -framework CoreFoundation -o build/soundtest
	@./build/soundtest $(ARGS)


# ---- building those two ------------------------------------------------------------------------
#
# PINNED, BOTH OF THEM. A synthesiser or an engraver changing under a plugin is a patch that
# sounds or looks different for no reason anybody can see.
FLUIDSYNTH_VERSION := v2.6.1
VEROVIO_VERSION := version-6.3.0

# WITHOUT ANYTHING A MODULE INSIDE A HOST HAS NO BUSINESS OWNING: audio drivers, MIDI drivers,
# sound files, LADSPA, readline, networking, threads of its own. What is left is the synthesis.
# Without GLib too, which it stopped needing in 2.6 — before that it would have been the hardest
# part of this to carry to Windows.
$(DEP_PATH)/lib/libfluidsynth.a:
	cd $(DEP_PATH) && test -d fluidsynth-src || git clone --depth 1 \
		--branch $(FLUIDSYNTH_VERSION) https://github.com/FluidSynth/fluidsynth.git fluidsynth-src
	$(CMAKE) -S $(DEP_PATH)/fluidsynth-src -B $(DEP_PATH)/build-fluidsynth \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=off \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-Denable-alsa=off -Denable-aufile=off -Denable-dbus=off -Denable-jack=off \
		-Denable-ladspa=off -Denable-libsndfile=off -Denable-midishare=off -Denable-network=off \
		-Denable-oss=off -Denable-pulseaudio=off -Denable-pipewire=off -Denable-readline=off \
		-Denable-sdl3=off -Denable-coreaudio=off -Denable-coremidi=off -Denable-framework=off \
		-Denable-dsound=off -Denable-wasapi=off -Denable-waveout=off -Denable-winmidi=off \
		-Denable-openmp=off -Denable-native-dls=off -Denable-signalsmith=off \
		-Denable-threads=off -Denable-ipv6=off
	cmake --build $(DEP_PATH)/build-fluidsynth -j 8
	cmake --install $(DEP_PATH)/build-fluidsynth

# VEROVIO READS EIGHT NOTATION FORMATS AND WE WANT ONE. Humdrum, ABC, Plaine and Easie, GABC and
# DARMS all go, which is most of the size. Editor support cannot: a file that is still compiled
# refers to a class the option removes, and the build fails without it.
#
# ONE WORD IS PATCHED. Verovio's CMake builds a SHARED library when asked for a library at all,
# and a plugin cannot carry a dynamic library of its own — Rack loads it from a folder the host
# owns, and nothing there is on any library path. Changed here rather than in a fork, so the
# source stays exactly what the tag says.
# NO DEBUG INFORMATION IN EITHER OF THEM. Rack's dependency flags carry -g, which is right for
# the plugin's own code and absurd for these: Verovio's archive came out at 492 megabytes with it
# and 17 without, and nobody is going to step through an engraver in a debugger.
DEP_NO_G := $(filter-out -g,$(DEP_CXXFLAGS))
DEP_NO_G_LD := $(filter-out -g,$(DEP_LDFLAGS))

# AND VEROVIO NEEDS A NEWER MAC THAN THE REST OF THE PLUGIN. It reads its glyphs with
# std::filesystem, which Apple's library marks unavailable before macOS 10.15, while Rack's
# dependency rules build everything for 10.9. So this one library is built for 10.15 — which
# makes 10.15 the floor for the whole plugin, since a plugin that will not load is no use to
# anybody on 10.13. Catalina is from 2019.
VEROVIO_CXXFLAGS := $(DEP_NO_G)
VEROVIO_LDFLAGS := $(DEP_NO_G_LD)
ifdef ARCH_MAC
VEROVIO_CXXFLAGS := $(subst -mmacosx-version-min=10.9,-mmacosx-version-min=10.15,$(VEROVIO_CXXFLAGS))
VEROVIO_LDFLAGS := $(subst -mmacosx-version-min=10.9,-mmacosx-version-min=10.15,$(VEROVIO_LDFLAGS))
VEROVIO_CMAKE := -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
endif

$(DEP_PATH)/lib/libfluidsynth.a: export CXXFLAGS := $(DEP_NO_G)
$(DEP_PATH)/lib/libfluidsynth.a: export CFLAGS := $(filter-out -g,$(DEP_CFLAGS))
$(DEP_PATH)/lib/libverovio.a: export CXXFLAGS := $(VEROVIO_CXXFLAGS)
$(DEP_PATH)/lib/libverovio.a: export CFLAGS := $(filter-out -g,$(DEP_CFLAGS))
$(DEP_PATH)/lib/libverovio.a: export LDFLAGS := $(VEROVIO_LDFLAGS)

$(DEP_PATH)/lib/libverovio.a:
	cd $(DEP_PATH) && test -d verovio-src || git clone --depth 1 \
		--branch $(VEROVIO_VERSION) https://github.com/rism-digital/verovio.git verovio-src
	$(SED) 's/add_library(verovio SHARED/add_library(verovio STATIC/' \
		$(DEP_PATH)/verovio-src/cmake/CMakeLists.txt
	$(CMAKE) $(VEROVIO_CMAKE) -S $(DEP_PATH)/verovio-src/cmake -B $(DEP_PATH)/build-verovio \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_AS_LIBRARY=ON \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DNO_HUMDRUM_SUPPORT=ON -DNO_ABC_SUPPORT=ON -DNO_PAE_SUPPORT=ON \
		-DNO_GABC_SUPPORT=ON -DNO_DARMS_SUPPORT=ON
	cmake --build $(DEP_PATH)/build-verovio -j 8
	mkdir -p $(DEP_PATH)/lib $(DEP_PATH)/include/verovio
	cp $(DEP_PATH)/build-verovio/libverovio.a $(DEP_PATH)/lib/
	cp -R $(DEP_PATH)/verovio-src/include/* $(DEP_PATH)/include/verovio/
	cp -R $(DEP_PATH)/verovio-src/libmei/dist/* $(DEP_PATH)/include/verovio/
	cp -R $(DEP_PATH)/verovio-src/libmei/addons/* $(DEP_PATH)/include/verovio/
	# THE GLYPHS ARE READ AT RUN TIME, so they ship rather than compile in. Bravura is needed
	# whichever font is drawn, because bounding boxes are measured against it; Leipzig is what is
	# drawn; the text metrics are for lettering. Nothing else of the eleven megabytes is used.
	rm -rf res/verovio
	mkdir -p res/verovio
	cp -R $(DEP_PATH)/verovio-src/data/Bravura res/verovio/
	cp -R $(DEP_PATH)/verovio-src/data/Leipzig res/verovio/
	cp -R $(DEP_PATH)/verovio-src/data/text res/verovio/
	cp $(DEP_PATH)/verovio-src/data/Bravura.xml res/verovio/
	cp $(DEP_PATH)/verovio-src/data/Leipzig.xml res/verovio/
#	THE TUNING TABLE, which names the microtonal accidentals. Without it Verovio complains on
#	every score that carries one and draws nothing in its place. Four kilobytes.
	cp $(DEP_PATH)/verovio-src/data/tuning-glyphnames.json res/verovio/


.PHONY: styletest pianobench bowtest bowplay scorenotes pianorelease lossprobe pathread test presets phrasesim gptest navtest performtest soundtest voicetest stringtest guitartest leveltest fretboardtest pickertest pickrender

# DOES THE ANTI-ALIASED PATH READING ACTUALLY WORK. A known tone is painted round a circle and
# read at rates from far below the table's resolution to far above it; averaging the arc is
# compared against sampling a point on the same signal.
pathread:
	@mkdir -p build
	@c++ -std=c++11 -O2 -Wall test/pathread.cpp src/Paths.cpp src/PictureRead.cpp \
		-o build/pathread
	@./build/pathread $(ARGS)




# mpxScore playing into mpxPiano from a command line, with the clicks found. See test/pianobench.cpp.
BENCH_OBJS = $(filter-out build/src/plugin.cpp.o build/src/mpxPiano.cpp.o build/src/mpxScore.cpp.o,$(wildcard build/src/*.o))
pianobench:
	@mkdir -p build
	@$(CXX) -std=c++20 -O2 -g $(BENCH_FLAGS) -I dep/include -I dep/include/verovio -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-Wno-deprecated-this-capture -march=armv8-a+fp+simd -funsafe-math-optimizations \
		test/pianobench.cpp test/pianobench_piano.cpp test/pianobench_score.cpp $(BENCH_OBJS) \
		dep/lib/libverovio.a dep/lib/libfluidsynth.a -framework Cocoa -framework CoreFoundation \
		-L$(RACK_DIR) -lRack -o build/pianobench
	@DYLD_LIBRARY_PATH=$(RACK_DIR) ./build/pianobench $(ARGS)

# What style each score's ornaments would be played in, and why. See test/styletest.cpp.
styletest:
	@mkdir -p build
	@c++ -std=c++11 -O1 -Wall -I dep/include/verovio/pugi \
		test/styletest.cpp src/MusicXML.cpp src/OrnamentStyle.cpp src/Composers.cpp src/GuitarPro.cpp \
		$(DEP_PATH)/verovio-src/src/pugi/pugixml.cpp -o build/styletest
	@./build/styletest $(ARGS)
