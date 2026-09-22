#pragma once
/** mpxPhrase's parameters, and the values a style gives them.

WHY THIS IS A HEADER AND NOT PART OF THE MODULE. Two things need the same list: the module, and
the program that writes the factory preset files the module ships with. Rack's own preset
mechanism is what offers SONG and JAZZ — there is no style control on the panel — and a preset
file is a list of parameter values by number, so the numbers and the values have to be agreed
somewhere both can read.

AND THE STYLES ARE MEASURED. `phraseStyle` in Phrasing.cpp is what `make phrasetest` runs its
census through, so generating the presets from it rather than typing the numbers into two files
is what keeps what ships equal to what was measured.

NUMBERS ARE APPENDED, NEVER INSERTED. Rack saves a parameter by its number, so a control added
anywhere but the end would misread every patch and every preset saved before it.
*/
#include "Phrasing.hpp"

namespace px {

enum PhraseParam {
	PHP_SUBDIVISION,
	PHP_GROUP,
	PHP_VARY,
	PHP_START,
	PHP_ENDING,
	PHP_PAUSE,
	PHP_PHRASE_PAUSE,
	PHP_HOLD,
	PHP_SILENT,
	PHP_DENSITY,
	PHP_SYNCOPATION,
	PHP_ONCHANGES,
	PHP_LENGTH,
	PHP_DYNAMICS,
	PHP_VARIATION,
	PHP_NOTE,
	PHP_SEED,
	PHP_OWN_SEED,
	/** Milestone five. */
	PHP_REPEAT,
	PHP_CYCLE,
	PHP_SECTIONS,
	PHP_ELIDE,
	/** Milestone seven. */
	PHP_RECORD,
	/** Milestone 4a, the feel. */
	PHP_TRIPLETS,
	/** The loudness shape across a phrase: nought falling from its start, one an arch. */
	PHP_SHAPE,
	/** How often a group restates the rhythm of an earlier group in the phrase. */
	PHP_MOTIF,
	PHP_LEN,
};

/** The parameters a PhraseControls covers, written into an array by number. Every other
parameter is left as it was. */
inline void phraseParamsFrom(const PhraseControls& c, float* v) {
	v[PHP_SUBDIVISION] = (float) c.subdivision;
	v[PHP_GROUP] = c.groupSeconds;
	v[PHP_VARY] = c.vary;
	v[PHP_START] = c.start;
	v[PHP_ENDING] = c.ending;
	v[PHP_PAUSE] = c.pause;
	v[PHP_PHRASE_PAUSE] = c.phrasePause;
	v[PHP_HOLD] = c.hold;
	v[PHP_SILENT] = c.silentPhrases;
	v[PHP_DENSITY] = c.density;
	v[PHP_SYNCOPATION] = c.syncopation;
	v[PHP_ONCHANGES] = c.onChanges;
	v[PHP_LENGTH] = c.length;
	v[PHP_DYNAMICS] = c.dynamics;
	v[PHP_VARIATION] = c.variation;
	v[PHP_REPEAT] = c.repeat;
	v[PHP_CYCLE] = (float) c.cycle;
	v[PHP_SECTIONS] = c.sections;
	v[PHP_ELIDE] = c.elide;
	v[PHP_TRIPLETS] = c.triplets;
	v[PHP_SHAPE] = c.shape;
	v[PHP_MOTIF] = c.motif;
}

/** WHAT THE MODULE COMES UP WITH, which is the SONG style plus the few parameters no style has
an opinion about. The module configures its controls from this rather than from numbers typed
beside each one, so the defaults and the shipped preset cannot drift apart. */
inline void phraseParamDefaults(float* v) {
	PhraseControls c;
	phraseStyle(STYLE_SONG, c);
	phraseParamsFrom(c, v);
	v[PHP_NOTE] = 60.f;          /**< Middle C. */
	v[PHP_SEED] = 0.f;
	v[PHP_OWN_SEED] = 0.f;
	v[PHP_REPEAT] = 0.f;
	v[PHP_CYCLE] = 4.f;
	v[PHP_SECTIONS] = 0.f;
	v[PHP_ELIDE] = 0.f;
	v[PHP_RECORD] = 0.f;
	/** THE MEASURED SHARE: a true triplet event falls on eight per cent of the beats real solos
	play. See the Feel section of docs/phrase.md. */
	v[PHP_TRIPLETS] = 0.08f;
}

} // namespace px
