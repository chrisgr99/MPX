# MPX — see README.md
#
#   make                 build the plugin
#   make install         build and copy into Rack's user plugins folder
#   make dist            build a .vcvplugin package for distribution

RACK_DIR ?= ../Rack-SDK

SOURCES += $(wildcard src/*.cpp)

# NO res DIRECTORY. Every panel is drawn in code, so the plugin ships no artwork. Listing a res
# directory that is not in the repository fails on a clean checkout, which is what the VCV
# Library builds from.
DISTRIBUTABLES += $(wildcard LICENSE*)

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

.PHONY: test
