# Ornament styles

A trill in Bach is not a trill in Brahms. Where it starts, how fast it goes, whether it ends with a turn nobody wrote, how quick a mordent is and how slowly a chord is rolled all changed between 1700 and 1900, and the sources say how. Nothing in a score says which of them it wants — the sign is the same sign — but the score almost always says who wrote it, and that is enough.

The score module works out who wrote a MusicXML file, takes the style from that, and writes every ornament out the way that style plays it. Nothing has to be set. The right-click menu, under **Ornaments played as**, says what the score was taken for and why, and offers the styles to choose instead; a choice is saved with the patch. `src/OrnamentStyle.hpp`, `src/OrnamentStyle.cpp`, `src/Composers.cpp`; `make styletest ARGS="files…"` prints what each file is taken for and why.

A Guitar Pro file has no style and nothing about it changes: its artist is a performer as often as a composer, and its reader has no ornaments to play.

## The styles

Eight, each a row of rules (`OrnamentRules`):

| style | who | trill starts | trill speed | turn added to a trill | mordent note | turn note | roll, between notes | unrolled hand lands with |
|---|---|---|---|---|---|---|---|---|
| Baroque | Bach, Handel, Scarlatti, Couperin, Rameau | above | 11 a second | yes | 40 ms | 70 ms | 50 ms | the first |
| Classical | Haydn, Mozart, Clementi | above | 11 | yes | 40 ms | 65 ms | 40 ms | the top |
| Beethoven | | the note | 12 | no | 45 ms | 65 ms | 40 ms | the first |
| Early Romantic | Hummel, Schubert, Weber, Mendelssohn, Schumann | the note | 13 | no | 45 ms | 70 ms | 40 ms | the first |
| Chopin | | above | 11 | no | 55 ms | 100 ms | 45 ms | the top |
| Later Romantic | Liszt, Brahms, Fauré; and the default | the note | 14 | no | 45 ms | 70 ms | 45 ms | the first |
| Modern | Debussy, Ravel, Schoenberg and after | the note | 16 | no | 40 ms | 60 ms | 25 ms | the first |
| Jazz | ragtime, jazz, popular song | the note | 16 | no | 35 ms, before the beat | 50 ms | 22 ms | the first |

Where a source gives a number it is used; where it only bounds one, the figure is a choice inside the bound. The Beethoven sonatas in use have their own engraver's playing spelled out in hidden notes, which is the one measurement there is: mordents at 45 ms a note, as here.

## Working out who wrote it

Everything the file says that might name the composer is read, each kind trusted as far as it deserves: the composer field three times as much as anything, the creator, the credits printed on the page, the work and movement titles and the file's own name twice, a lyricist or the copyright line once, an arranger not at all. Never one field alone. The scores in use have an arranger in the composer field, the word "Composer" as a placeholder, a template author's name, and on one song the wrong composer entirely.

The text is folded to plain lower-case letters: accents taken off, and the hair spaces that letter-space a credit taken out — the Entertainer file spells its composer B Y S C O T T J O P L I N. Each field is split into clauses at commas, brackets, slashes and "and", "et", "und" and "&", keeping names that contain those words. A clause that is only a placeholder goes; one that starts or ends with a role word — arranged, transcription, lyrics, edited and the rest — goes from that word on, except where "music by" or "words and music by" introduces the composer. A name after "theme of", "hommage à" and the like is the subject and not the composer.

Then, in order:

- **Known titles**, longest first, on everything at once: Moonlight, Tempest and Pathétique beside a sonata or their opus numbers, Waldstein, Appassionata, Für Elise, the Well-Tempered Clavier, The Entertainer and the songbook titles in the files. A title is taken out once matched, so a name inside one is not read as the composer: Le Tombeau de Couperin is Ravel's.
- **Catalogue numbers**, a code and then a number: BWV Bach, HWV Handel, K or KV Mozart (Scarlatti where Scarlatti or Kirkpatrick is named), Hob. Haydn, WoO Beethoven, RV Vivaldi, TWV Telemann, BuxWV Buxtehude, Wq C. P. E. Bach.
- **Names**, against a table of 338 composers with the spellings they appear under, longest spellings first. A long name may be off by a letter or two — "Schoemberg", "FFaure" — but a one-word misspelling counts only where it is the whole field, so an ordinary word near a short name is not taken for it.
- **Dates**: a life span in the text that is somebody's in the table, "(1770 - 1827)", counts for them.

The composer with most points is taken. Where nobody is named, a life span still dates the music, then chord symbols mark a lead sheet and so jazz, then a lone year dates it; where nothing does, the style is Later Romantic, which is how ornaments are generally played now.

Every score in use is taken for the right composer, or, for one lead sheet whose writers the table does not have, for jazz.

## What each style does

### Trills

**Where it starts.** What the file says, if it says: a `start-note` attribute actually written on the trill. MusicXML gives an absent one a default of the main note, and taking that default would overrule every style, so only one that is present counts. Then what is written before it, in every style: a grace note at the pitch above means the trill goes on from the written note; one at the written pitch means it goes on from above, without striking the note twice, as the Chopin National Edition reads it; one below — the prefix from below Chopin and Weber write — means from the written note. Then the note above already sounding and slurred into the trill: it is not struck again and the trill goes on from the written note, the bound trill of the eighteenth-century tables. Otherwise the style: above for Baroque, Classical and Chopin, the written note for the rest.

Beethoven's and Chopin's are disputed. For Beethoven the written note, with Newman and Czerny's fingerings: he wrote a small upper note where he wanted to start above, eighteen of the twenty-four trills in the first movement of Op. 2 No. 3. For Chopin above, with his pupil Mikuli and with Taylor's reading of his habit of writing the prefix from below in full.

**How fast.** The style's speed in the middle of the keyboard, a tenth slower at the bottom and a tenth faster at the top, as Quantz, Leopold Mozart and Türk all have it.

**How it ends.** On the written note; on the note above where a written termination follows, which hands the termination a string that is not already ringing. Baroque and Classical add a turn where none is written — the note below and then the written note — on a trill a beat or longer; the others play one only where it is written.

### Mordents and turns

On the beat, a flick and then the written note held: a mordent is the note, the one below and the note again; an inverted mordent the note, the one above and the note again; a turn the note above, the note, the note below and the note; an inverted turn from below. The quick notes take the style's time each, and share at most the first half of the note between them, so a short note keeps something of itself.

A turn printed after the note — `delayed-turn` — is the note first, held, and the four notes of the turn late, just before the next one, in at most its second half. Never early, which C. P. E. Bach says is what beginners get wrong.

Jazz plays mordents and turns before the beat, as a pickup, with the written note landing on the beat and the stress there.

**The stress is on the first note.** Every note of an ornament after the first is a dynamic marking lighter than the note it decorates, and a mordent's two quick notes two markings lighter, as a flick of the finger is. See `guitar-pro.md`.

### Rolled chords

The whole roll takes the style's time between notes, times the notes it rolls through; that length goes down the cable and the piano spreads the roll over it. When one hand rolls a chord and the other does not, the unrolled notes land with the roll's first note — how this plugin plays it — except where a source says otherwise: with the top note for the Classical style, as Taylor has it for Haydn, and for Chopin, as the Chopin National Edition has it.

## Not done yet

**Grace notes.** Still one way everywhere: a short note on the beat, taking its time from the note after it. The styles disagree here more than anywhere. The Baroque and Classical unslashed grace is a long appoggiatura, half the note or two thirds of a dotted one, on the beat and stressed; Beethoven's is short by default; Chopin's is on the beat with the bass; Schoenberg's and after is before the beat; jazz crushes it against the note.

**The finer rules.** A Baroque or Classical mordent's lower note a semitone by default; the half trill after a falling second; the long mordent on a long note in a slow tempo; only the last of a chain of trills taking a turn; the accelerating trill at a cadenza; Beethoven's trill start read by period or by dissonance against the bass; Chopin's mordent struck with the bass.

## Sources

C. P. E. Bach, *Versuch über die wahre Art das Clavier zu spielen* (1753); Quantz, *Versuch einer Anweisung die Flöte traversiere zu spielen* (1752); Leopold Mozart, *Versuch einer gründlichen Violinschule* (1756); Türk, *Klavierschule* (1789); Clementi, *Introduction to the Art of Playing on the Piano Forte* (1801); Hummel, *Anweisung zum Piano-Forte-Spiel* (1828); Franklin Taylor's ornament articles in Grove's *Dictionary* (1879–1889); Newman, *Beethoven on Beethoven* (1988) and the Newman–Winter exchange; Neumann, *Ornamentation in Baroque and Post-Baroque Music* (1978) and *Ornamentation and Improvisation in Mozart* (1986); Rosenblum, *Performance Practices in Classic Piano Music* (1988); Eigeldinger, *Chopin: Pianist and Teacher*; the Chopin National Edition's performance commentaries; the Henle prefaces to the Beethoven sonatas and to Weber Op. 24; the MusicXML 4.0 reference. Composer dates from the Wikipedia lists of Baroque, Classical-era, Romantic-era and twentieth-century composers and of ragtime composers, checked 9 October 2026.
