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

# FLUIDSYNTH, built into dep by tools/build-fluidsynth.sh and linked statically: the SoundFont
# player inside mpxFluidSynth. Built without audio drivers, MIDI drivers, libsndfile, LADSPA, readline,
# networking or threads of its own, and without GLib, which it stopped needing in 2.6.
#
# TESTED WITH TARGET_MACHINE AND NOT ARCH_MAC, because arch.mk has not been included yet — which
# is the same reason the test above it is written that way. A framework missed here still works,
# since the host has already loaded it and the undefined symbol is looked up at load; a static
# library missed here does not, and the module crashes the first time it is created.
FLAGS += -I dep/include
ifneq (,$(findstring -darwin,$(TARGET_MACHINE)))
	LDFLAGS += dep/lib/libfluidsynth.a -framework CoreFoundation
endif
ifneq (,$(findstring -linux,$(TARGET_MACHINE)))
	LDFLAGS += dep/lib/libfluidsynth.a
endif
ifneq (,$(findstring -mingw,$(TARGET_MACHINE)))
	LDFLAGS += dep/lib/libfluidsynth.a -lws2_32 -lole32
endif

include $(RACK_DIR)/plugin.mk

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
	@c++ -std=c++11 -O1 -Wall test/gptest.cpp src/GuitarPro.cpp src/GpTimeline.cpp -o build/gptest
	@./build/gptest $(ARGS)


# Repeats, endings and jumps: the played order, against songs written in the test itself.
navtest:
	@c++ -std=c++11 -O1 -Wall test/navtest.cpp src/GuitarPro.cpp src/GpTimeline.cpp \
		-o build/navtest
	@./build/navtest

# What each articulation does, one at a time, with the humanising turned off.
performtest:
	@c++ -std=c++11 -O1 -Wall test/performtest.cpp src/Perform.cpp -o build/performtest
	@./build/performtest

# The SoundFont engine. No Rack in it either, so it runs against a bank from the command line.
soundtest:
	@c++ -std=c++11 -O1 -Wall -I dep/include test/soundtest.cpp src/FluidEngine.cpp \
		dep/lib/libfluidsynth.a -framework CoreFoundation -o build/soundtest
	@./build/soundtest $(ARGS)


.PHONY: test presets phrasesim gptest navtest performtest soundtest
