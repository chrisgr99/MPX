/** DOES A DRUM TRACK REACH A DRUM KIT?

The reader marks a drum track and SoundCore asks for bank 128, which is where a SoundFont keeps
its kits. Everything between those two is inside Rack, so when the drums come out sounding like a
keyboard there is nowhere to look. This runs the engine on its own: load the bank, select a kit
the way SoundCore does, and ask the synthesiser what the channel actually ended up playing.

	make drumsound
*/
#include "../src/FluidEngine.hpp"

#include <fluidsynth.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;

int main(int argc, char** argv) {
	const std::string path = (argc > 1) ? argv[1]
		: std::string(getenv("HOME")) + "/Library/Application Support/Rack2/DreamerMPX/banks/"
			"GeneralUser-GS.sf2";
	FluidEngine engine;
	std::string why;
	if (!engine.start(48000.0, 16, 128)) {
		std::printf("could not start the synthesiser\n");
		return 1;
	}
	if (!engine.loadBank(path)) {
		std::printf("could not load %s\n", path.c_str());
		return 1;
	}
	(void) why;
	const std::vector<FluidPreset>& list = engine.presets();
	int kits = 0;
	for (size_t i = 0; i < list.size(); i++)
		if (list[i].bank == 128)
			kits++;
	std::printf("presets seen by the engine: %d, of which drum kits: %d\n",
		(int) list.size(), kits);

	// THE WAY SoundCore ACTUALLY DOES IT: six parts of several voices each, so a part's notes
	// are spread over channels far apart — part p voice v plays on v * parts + p — and the synth
	// is made with as many channels as that needs and one audio group per part.
	{
		const int parts = 6, voices = 12;
		engine.stop();
		const int chans = ((parts * voices + 15) / 16) * 16;
		if (!engine.start(48000.0, chans, 256 + 32 * parts, parts)) {
			std::printf("could not start with %d channels and %d groups\n", chans, parts);
			return 1;
		}
		if (!engine.loadBank(path)) {
			std::printf("could not reload the bank\n");
			return 1;
		}
		std::printf("\n%d parts, %d voices, %d channels, %d groups\n", parts, voices, chans, parts);
		for (int part = 0; part < parts; part++) {
			if (part != 3)
				continue;        // The row a drum track might land on.
			int wrong = 0;
			for (int v = 0; v < voices; v++) {
				const int ch = v * parts + part;
				engine.program(ch, 128, 0);
				fluid_preset_t* got = (fluid_preset_t*) engine.channelPreset(ch);
				const int bank = got ? (int) fluid_preset_get_banknum(got) : -1;
				if (bank != 128) {
					wrong++;
					std::printf("  part %d voice %d is on channel %-3d -> %s (bank %d)\n",
						part, v, ch, got ? fluid_preset_get_name(got) : "NOTHING", bank);
				}
			}
			std::printf("  part %d: %d of %d channels did NOT take the drum kit\n",
				part, wrong, voices);
		}
	}

	// THE WAY SoundCore DOES IT: bank 128, the program the file named.
	for (int wanted = 0; wanted <= 1; wanted++) {
		const int ch = 3;      // Not channel 9: a drum part can land on any of them.
		engine.program(ch, 128, wanted);
		fluid_preset_t* got = (fluid_preset_t*) engine.channelPreset(ch);
		std::printf("  asked for bank 128 program %d on channel %d -> %s\n", wanted, ch,
			got ? fluid_preset_get_name(got) : "NOTHING");
		if (got)
			std::printf("    which is bank %d program %d\n",
				(int) fluid_preset_get_banknum(got), (int) fluid_preset_get_num(got));
		// AND DOES IT MAKE A NOISE. Key 38 is the snare in every General MIDI kit.
		engine.noteOn(ch, 38, 100);
		float l[512], r[512];
		double peak = 0.0;
		for (int b = 0; b < 8; b++) {
			engine.render(l, r, 512);
			for (int i = 0; i < 512; i++)
				peak = std::fmax(peak, std::fabs((double) l[i]));
		}
		engine.noteOff(ch, 38);
		std::printf("    key 38 peaks at %.4f\n", peak);
	}
	return 0;
}
