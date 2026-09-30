# Hearing a change as you make it

A knob moved now is heard several seconds later, when the next phrase begins. That is long enough that you cannot tell which knob caused what you are hearing, so the settings drift away from anything musical as you work: you make a change, hear something wrong, change something else, and the two are not connected. With about fifty knobs across the chain, tuning by ear is not possible at all.

## The rule

A phrase is a function of the harmony, the seed and the settings. So when a setting changes, the phrase is built again from its start with the new value, and playing carries on from wherever it has got to.

Nothing rewinds. The chart runs on, everything else in the patch keeps its place, and what you hear from the next note onwards is what you would have heard had the knob been there all along. It is not a restart and not a new phrase: same seed, same harmony, same phrase, one thing different — which is what makes two settings comparable by ear.

## Why it is possible

Both generators are already pure.

`phraseGenerate(ask, controls, out)` has no state at all: the phrase's length and metre, the tempo, the cadence, the chord changes inside it, the seed, the phrase before it and how it is to begin all arrive in the ask. Given the same ask and the same controls it produces the same pattern, which is what the command-line tests rely on.

`voiceNoteFor(harmony, settings, previous, dice, …)` chooses one note from the note before it and a draw between nought and one. Unpatched, that draw is `melodyDraw` — a hash of the seeds, the position in the cycle and how far into the phrase the note falls. It is not a running random generator, so the same note in the same phrase draws the same number however many times it is asked.

That is the whole of it: rebuilding a phrase is calling the same two functions again with the ask that was used and the controls as they are now.

## What has to be kept

At the start of every phrase, each module keeps what it would need to make that phrase again:

- the `PhraseAsk` it was given, including the chord changes inside the phrase;
- a copy of the phrase before it, which REPEAT restates from;
- for each voice, the note it was on when the phrase began, and the phrase's echo bookkeeping;
- the harmony at each of the phrase's chord changes.

All of it is fixed-size — a `PhrasePattern` is 256 notes — so a rebuild allocates nothing.

## Where the work happens

A knob moves on the UI thread; the notes are made on the audio thread. A rebuild is a few hundred notes of work, and a fraction of a second to a second of lag after a knob moves is acceptable — so the rebuild is done on the UI thread, into a spare pattern, and published with an atomic swap. The audio thread picks the new pattern up at the next note boundary.

A note already sounding finishes as it was. Nothing that has already been sent to an instrument can be taken back, so a change takes effect from the next note.

While a knob is being turned continuously, each move supersedes the last: only the most recent settings are ever built, and a rebuild in progress for an older position is abandoned.

## Which modules

mpxPhrase and mpxMelody with its voices first. Between them they hold the rhythm and the note choice, which is where the fifty knobs are and where tuning by ear matters most.

mpxComp and the other players follow, on the same rule.

## What it is not

It does not move the chart, so it cannot be used to hear a change in a phrase that has already gone past. For that there is a tune mode, later: a switch that loops the last two or four bars of chords so the same material comes round every couple of seconds and two settings can be compared on it. The rest of the patch keeps playing the song meanwhile, so that mode is for setting a voice up rather than for listening to the arrangement.

It does not change what a recording holds. A take recorded while tuning shows the rebuilt phrases, which is what was heard.
