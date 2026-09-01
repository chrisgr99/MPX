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
