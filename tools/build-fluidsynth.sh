#!/bin/sh
# FluidSynth, built as a static library for mpxSound to link into the plugin.
#
# WHY A SCRIPT AND NOT A SUBMODULE. dep/ is not in the repository — it holds built things — so
# this is how it is made again on another machine or after it is cleaned out. Run it once:
#
#     tools/build-fluidsynth.sh
#
# WHAT IS TURNED OFF, AND WHY. A module inside a host has no business owning an audio device, a
# MIDI device, a network socket or threads of its own, and it does not need to read or write sound
# files. What is left is the synthesis, which is the only part wanted. GLib is not in the list
# because FluidSynth stopped needing it in 2.6 — before that it would have been the hardest part
# of this to carry to Windows.
#
# The version is pinned. A synthesiser changing under a plugin is a patch that sounds different
# for no reason anybody can see.
set -e

VERSION=v2.6.1
HERE=$(cd "$(dirname "$0")/.." && pwd)
DEP="$HERE/dep"
SRC="$DEP/fluidsynth-src"

mkdir -p "$DEP"
if [ ! -d "$SRC" ]; then
	git clone --depth 1 --branch "$VERSION" https://github.com/FluidSynth/fluidsynth.git "$SRC"
fi

cmake -S "$SRC" -B "$DEP/build-fluidsynth" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_SHARED_LIBS=off \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DCMAKE_INSTALL_PREFIX="$DEP" \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 \
	-Denable-alsa=off -Denable-aufile=off -Denable-dbus=off -Denable-jack=off \
	-Denable-ladspa=off -Denable-libsndfile=off -Denable-midishare=off -Denable-network=off \
	-Denable-oss=off -Denable-pulseaudio=off -Denable-pipewire=off -Denable-readline=off \
	-Denable-sdl3=off -Denable-coreaudio=off -Denable-coremidi=off -Denable-framework=off \
	-Denable-dsound=off -Denable-wasapi=off -Denable-waveout=off -Denable-winmidi=off \
	-Denable-openmp=off -Denable-native-dls=off -Denable-signalsmith=off \
	-Denable-threads=off -Denable-ipv6=off

cmake --build "$DEP/build-fluidsynth" -j8
cmake --install "$DEP/build-fluidsynth"

echo
echo "built $(ls -l "$DEP/lib/libfluidsynth.a" | awk '{print $5}') bytes into dep/lib"
