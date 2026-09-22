
# Pop chart transcription format

One JSON file per song: `research/pop/<slug>.json`. It records rhythm, pitch and prosody only. Never write lyric words into the file: the songs are copyrighted. Every syllable is described by its stress and word class instead.

```json
{
  "title": "A Thousand Miles",
  "artist": "Vanessa Carlton",
  "style": "MED",
  "meter": [4, 4],
  "key": "Bb",
  "pages": [3, 4],
  "phrases": [
    {
      "section": "verse",
      "notes": [
        {"bar": 1, "beat": 0.5, "dur": 0.5, "pitch": 65, "tie": false, "syl": "S", "word": "c", "wend": true, "punct": ""}
      ],
      "restAfter": 2.5
    }
  ]
}
```

Fields:

- **bar**: bar number counted from the first sung bar of the song (1 = the bar where the voice first sings; a pickup before it is bar 0). Keep counting through the sung part in written order; do not unfold repeats.
- **beat**: onset within the bar in quarter-note beats from the bar line, starting at 0. An eighth after beat one is 0.5; the last sixteenth of a 4/4 bar is 3.75. Triplets as thirds (0.333, 0.667).
- **dur**: the sounding length in quarter-note beats, including every tie. A note tied over the bar line is ONE note with the total length. Dotted quarter = 1.5.
- **pitch**: MIDI number (middle C = 60), using the key signature and accidentals. Your best reading; the pitch matters less than the rhythm.
- **tie**: true when the note's length crosses a beat boundary by a tie or a dotted value starting off the beat (an anticipation held across the beat).
- **syl**: "S" a stressed syllable (the stressed syllable of a word, or a one-syllable content word), "u" an unstressed syllable (unstressed part of a word, or a one-syllable function word such as the, a, and, to, I, my, of, in), "m" a melisma note (the same syllable carried on to a new pitch, shown by a slur with no new syllable).
- **word**: "c" content word (noun, main verb, adjective, adverb, interjection), "f" function word (article, pronoun, preposition, conjunction, auxiliary). For "m" notes repeat the syllable's word class.
- **wend**: true on the last syllable of a word.
- **punct**: punctuation printed after the syllable: "," "." "?" "!" or "".
- **restAfter**: beats of silence after the phrase's last note ends before the next sung note (0 if none; a large number if the section ends).
- **section**: "intro", "verse", "prechorus", "chorus", "bridge", "hook", "outro" as the chart marks or as is evident.

A PHRASE is a run of sung notes ended by a rest of an eighth or longer, or by line-ending punctuation followed by a new line of text. Where lyrics are stacked (several verses), use the FIRST line only. Skip instrumental lines (marked PIANO, GUITAR, etc.) entirely.

## Chords

Added to the same file as a top-level `"chords"` array, one entry per chord symbol printed above the staff, in written order, for every bar the transcribed notes cover:

```json
"chords": [ {"bar": 1, "beat": 0, "symbol": "Bbm7"}, {"bar": 1, "beat": 2, "symbol": "Eb6"} ]
```

- **bar** and **beat** use exactly the same numbering as the notes in the file (bar 1 is the bar where the voice first sings; a pickup bar is 0; beats in quarter notes from 0). Align by finding the bar where the file's first notes are, and check that the chord changes fall where the notes say they should.
- **symbol** as printed, in plain text: `m` for minor, `maj7`, `7`, `dim7` (for °7), `m7b5` (for ø), `sus4`, `6`, slash bass as `/B`. `N.C.` for no chord.
- A chord continues until the next entry. Include the chord in force at bar 1 beat 0 even if it was printed earlier.
