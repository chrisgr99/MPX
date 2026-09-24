# The players: voicing, accompaniment, guitar and groove

## What this is about

mpxComp makes several decisions at once. It chooses which tones of the chord to play, how many voices, where they sit, how far apart they are and how little they may move between chords — and then it plays them, with a pattern, a rate, an accent and a strum. Two of those are different jobs, and only one of them is comp's alone.

Every other module that plays the harmony makes the first set of decisions again, badly. mpxArp builds its own notes from the chord's tones with no register, no spread and no voice leading, which is why a line out of the arp sits wrong against the same chart comp is playing well.

So the voicing moves out of comp, onto the cable, where anything can read it.

## What the cable carries

A voice cable carries note events. Beside them it carries state: the harmony, and the pedals. State is what a module that starts listening in the middle must know at once rather than when it next changes.

The voicing is state of the same kind: the notes the chord has been placed on, in volts, ascending, published when they change.

The chord and the key stay where they are. A module that wants to work the notes out for itself, or that wants the chord rather than the notes — a melody module needs the key — is unaffected. A module that wants the notes takes them and is in step with every other module that took them.

This is why the voicing travels as state rather than as notes. Notes would say what is sounding; state says what the chord is currently placed on, which is a different question and the one a player asks.

## The modules

**mpxVoicing** holds the decisions comp used to make: Voices, Register, Octave span, Note spread, Chord complexity, Root and Voice leading. It reads the harmony, places the chord, publishes the result, and passes everything else along untouched. It plays nothing.

**mpxComp** keeps the playing: Pattern, Hold, Rate, Gate, Strum, Accent, Amount, Humanise, Balance and Pedal. It reads the voicing from the cable. With no voicing on the cable it places the chord itself, exactly as it does now, so a patch made before this sounds the same.

**mpxArp** reads the voicing when there is one and falls back to its own tones when there is not.

**A guitar player**, later, strums a voicing: direction, how far apart the strings are struck, muting, how hard, and bends. It decides nothing about which notes.

**A groove module**, later, holds the styles — swing, bossa, funk, rock, ballad, montuno — plays drums itself, and offers its pattern to the players so that several of them lock together.

## Instruments belong to the voicing

Which notes are available is a property of the instrument rather than of how it is struck. A guitar has six strings, one note each, tuned in fourths with a third in the middle, and a hand that reaches about four frets; a chord that ignores that is not a guitar chord however it is strummed.

So mpxVoicing carries the instrument: Open, Keyboard, Guitar, Bass, Section. Each narrows the search. Guitar also needs a position, since the same chord has a shape at the third fret and another at the eighth, and which one it takes should follow the last one the way voice leading already does.

A patch with a guitar and a piano wants two voicings, so it has two voicing modules, one in each branch. The chart feeds both.

## Bends

A bend is a change to a note that is already sounding, and the cable carries it already: the piano ignores it, mpxOut turns it into pitch.

A string bends up, so a bend down from a note is a pre-bend released, or a slide. The kinds worth having are bend up into the note, strike and bend up, pre-bend and release, and slide. They belong to the top voice, on the thinner strings, and the targets are degrees of the chord — into the third, into the fifth, into the root from the seventh below, and the blues curl of about a quarter tone from the minor third.

Bending is part of the playing rather than of the voicing: it is what the hand does to a note it has already chosen.

## The groove

Rate is a metronome: a step every so many beats, every bar the same. A rhythm is not that, and no player is. The groove module is where the rhythm lives, for the accompaniment and for the line alike, so that two players in one patch lock together instead of each keeping its own time.

### What it holds

**A style.** Swing, bossa, funk, rock, ballad, montuno, skank. A style is a set of hits within a bar at particular positions, each with a weight, rather than an even division of it.

**Feel.** How much swing, and whether the playing sits on the beat, ahead of it or behind it. The second is a timing offset, and it is most of what makes a groove sound like a person.

**Density.** How much of the pattern sounds. A sparse ballad comp and a busy funk comp are the same hits with more or fewer of them played.

**Variation.** How much one bar differs from the last, and whether the end of a phrase is a fill or a turnaround. The chart carries the phrase, so the module knows where in the form it is.

**Drums.** It knows the hits and their weights, so it can play a kit itself. That is what makes it worth having on its own rather than being a setting on something else.

### What travels on the cable

The hits of the current bar, as state: a position in beats, a weight, and a mark for whether the hit is an accent, a ghost or a push. State rather than a stream, because a player has to see a hit that is coming — anticipating a chord by an eighth means knowing where that eighth falls.

The chart keeps the timebase. The groove never says what the beat is, only what happens inside it.

### How the players use it

**mpxComp** gains one entry in its Rate list, Follow groove. Pattern still decides who plays on a hit; the groove decides where the hits are and how hard. Accent takes its weight from the groove rather than from the bar position, so the two stop deciding the same thing differently.

**mpxPhrase** gains the same entry, and the groove becomes a constraint rather than a replacement: notes land on the hits, or deliberately between them, which is what a soloist does against a comp.

### The stages

1. The panel: the module, its controls and its jacks, playing nothing.
2. The styles, as hits and weights, and the drums it plays from them.
3. The rhythm on the cable as state.
4. Follow groove in mpxComp.
5. Feel, density and variation, which are only worth tuning once something is playing.
6. Follow groove in mpxPhrase.

## The order this gets built

1. **The voicing on the cable.** The bus gains the state; mpxVoicing publishes it; comp reads it and falls back without it.
2. **Comp slims down.** The voicing controls come off its panel and it narrows.
3. **The arp reads the voicing.** The audible fix.
4. **The guitar's shapes**, as an instrument setting in mpxVoicing.
5. **The guitar player**, which strums.
6. **Bends and vibrato** on the guitar player.
7. **The groove module**, and one setting on each player: follow the groove, or keep its own rate.
8. **The trimmings**: help entries, port colours, the monitor showing the voicing and the groove.

## What the voicing state holds

- Whether it is valid: something has published one.
- How many voices, up to six.
- The pitch of each, in volts, ascending.
- A count that changes when the notes change, so a reader can tell a new voicing from the same one read again.

It is written when the chord turns over or a setting moves, and read every sample, which is the traffic the harmony's own seqlock already suits.

## Where a module forwards it

A module that reads an upstream and publishes a bus of its own forwards the voicing exactly as it forwards the harmony and the pedals. Dropping it would leave everything after that module voicing for itself, which is the fault this exists to fix.
