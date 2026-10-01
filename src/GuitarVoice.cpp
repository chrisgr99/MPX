/** See GuitarVoice.hpp. */
#include "GuitarVoice.hpp"

#include <cmath>

namespace px {


/** Below this an envelope has finished: about -100 dB, under anything audible. */
static const float SILENT = 0.00001f;

/** The natural logarithm of a thousand: a fall of sixty decibels, in the units an exponential
fall is measured in. */
static const float LN_1000 = 6.9077553f;


void VoiceEnvelope::strike() {
	stage = ATTACK;
}


void VoiceEnvelope::end() {
	if (stage != IDLE)
		stage = RELEASE;
}


float VoiceEnvelope::step(float dt) {
	switch (stage) {
		case IDLE:
			value = 0.f;
			break;
		case ATTACK:
			value += (attack > 0.f) ? dt / attack : 1.f;
			if (value >= 1.f) {
				value = 1.f;
				stage = DECAY;
			}
			break;
		case DECAY:
			// An exponential fall, which is what a string's does: a fixed fraction of what is
			// left in each moment, so the decay control is a time to fall sixty decibels.
			value *= std::exp(-LN_1000 * dt / std::fmax(decay, 0.001f));
			if (value < SILENT) {
				value = 0.f;
				stage = IDLE;
			}
			break;
		case RELEASE:
			value *= std::exp(-LN_1000 * dt / std::fmax(release, 0.001f));
			if (value < SILENT) {
				value = 0.f;
				stage = IDLE;
			}
			break;
	}
	return value;
}


void GuitarVoices::times(float attack, float decay, float release) {
	for (int v = 0; v < VOICES; v++) {
		envelope[v].attack = attack;
		envelope[v].decay = decay;
		envelope[v].release = release;
	}
}


void GuitarVoices::filterTimes(float attack, float decay) {
	for (int v = 0; v < VOICES; v++) {
		filterEnvelope[v].attack = attack;
		filterEnvelope[v].decay = decay;
		filterEnvelope[v].release = decay;
	}
}


void GuitarVoices::strike(int voice, float l) {
	if (voice < 0 || voice >= VOICES)
		return;
	level[voice] = l;
	envelope[voice].strike();
	filterEnvelope[voice].strike();
}


void GuitarVoices::setLevel(int voice, float l) {
	if (voice >= 0 && voice < VOICES)
		level[voice] = l;
}


void GuitarVoices::end(int voice) {
	if (voice >= 0 && voice < VOICES)
		envelope[voice].end();
}


void GuitarVoices::silence() {
	for (int v = 0; v < VOICES; v++) {
		envelope[v].stage = VoiceEnvelope::IDLE;
		envelope[v].value = 0.f;
		filterEnvelope[v].stage = VoiceEnvelope::IDLE;
		filterEnvelope[v].value = 0.f;
		level[v] = 0.f;
		first[v].reset();
		second[v].reset();
	}
}


float GuitarVoices::cutoffOf(int v) const {
	// EVERYTHING THAT MOVES THE CUTOFF, IN OCTAVES, added: the string's pitch as far as key
	// tracking takes it, and how far the performer's brightness is from an ordinary note's.
	const float octaves = filter.keyTracking * pitch[v]
		+ (timbre[v] - TIMBRE_OPEN) * TIMBRE_OCTAVES
		+ filter.envelopeDepth * filterEnvelope[v].value;
	return filter.cutoff * std::pow(2.f, octaves);
}


void GuitarVoices::process(const float* in, int count, float dt, float* left, float* right) {
	float l = 0.f, r = 0.f;
	for (int v = 0; v < VOICES; v++) {
		if (!envelope[v].sounding())
			continue;
		const float gain = envelope[v].step(dt) * level[v];
		filterEnvelope[v].step(dt);
		float sample = 0.f;
		if (count == 1)
			sample = in[0];
		else if (v < count)
			sample = in[v];
		// THE STRING'S OWN FILTER. Held below nine tenths of the top of the band, where the
		// filter's tuning stops being one, and above the bottom of hearing.
		if (filter.on) {
			const float rate = 1.f / dt;
			const float hz = std::fmax(20.f, std::fmin(cutoffOf(v), 0.45f * rate));
			const float g = std::tan(3.14159265f * hz * dt);
			// A PEAK NO SHARPER THAN A Q OF TEN at the top of the resonance control, about
			// twenty decibels above a flat response. Sharper whistles on every string at once,
			// which is not a guitar.
			const float k = 1.41421f
				- 1.31421f * std::fmax(0.f, std::fmin(1.f, filter.resonance));
			sample = first[v].process(sample, g, k);
			if (filter.steep)
				sample = second[v].process(sample, g, 1.41421f);
		}
		sample *= gain;
		// EQUAL POWER, so a string moved across the field stays as loud: a quarter turn of a
		// circle from the left to the right, with the middle at 0.707 on each side.
		const float p = std::fmax(-1.f, std::fmin(1.f, pan[v]));
		const float angle = (p + 1.f) * 0.7853982f;
		l += sample * std::cos(angle);
		r += sample * std::sin(angle);
	}
	*left = l;
	*right = r;
}


} // namespace px
