# VocalWriter in C

**VocalWriter 2.0.1** (KAE Labs, 2005) was a singing synthesiser for PowerPC
Macs: type lyrics, draw notes, and a modelled vocal tract sings them. This is
that program's synthesiser, recreated in C -- not an approximation of it. The
original's own functions, with their own names, lifted from the shipped
PowerPC binary and verified against it until every sample agrees.

What is here:

- **The synthesiser** (`src/speech.c`): the vocal tract model, the glottal
  source, the phoneme control and the voice controls.
- **The text front end**: the pronunciation dictionary search, the suffix
  morphology and the letter-to-sound rules (`src/orthtophon.c`), and the
  phoneme parser that turns syllables on notes into what the synthesiser
  plays (`src/parsephons.c`).
- **The sequencer and the instruments** (`src/music.c`): the song player,
  the 96-oscillator wavetable synthesiser for the General MIDI tracks, the
  envelopes and vibrato.
- **The reverb** (`src/reverb.c`).
- **The library's public face**: the `Synth_*` API the application called
  (`src/synthapi.c`), and the MIDI file import and track expansion
  (`src/convertsmf.c`, `src/expandtracks.c`).
- **A small API and a command line tool** over all of that
  (`include/vocalwriter.h`, `src/vw.c`).

## How exact

Three differential tests run the same inputs through this code and through
the original machine code (under the PowerPC interpreter in the sibling
VocalWriter repository) and compare: the synthesiser's whole context after
every note and every frame plus the output samples (`test/difftest.py`), the
front end's results for words, sung syllables and random syllable tables
(`test/fronttest.py`), and whole songs through the `Synth_*` API exactly as
the application plays them (`test/seqtest.py`).

Beyond the interpreter, `test/exporttest.py` renders the demo songs and
compares them with the AIFF files VocalWriter itself exported on a
Macintosh. They match sample for sample: Daisy (66 s, voice and instruments
and reverb), its vocal track alone, and Acappella (84 s, four voices).

## What you have to supply

None of VocalWriter is in this repository. Its data files are needed to
make a sound, in a directory given by `--data` or `$VW_DATA` (the default
is `../VocalWriter/assets`):

| file | what | needed for |
|---|---|---|
| `VocalWriter.rsrc` (or the `VocalWriter.app` bundle) | the engine's tables | everything |
| `GMSpeech.rsrc` | the voices | everything |
| `GMBank.rsrc` | the instruments | songs with instrument tracks |
| `EnglishLex` | the pronunciation dictionary | typed lyrics |

The tests additionally need the VocalWriter repository beside this one,
with its interpreter and its `emu/out` exports.

## Build

    sh build.sh          # or: make
    make check           # the differential tests

GCC or Clang, C11. The flags in `build.sh` are part of the port:
`-ffp-contract=off` keeps every multiply-add as two roundings, as the
PowerPC code computed it, and `-fwrapv` gives the wrap-around integer
arithmetic the original relied on. On Windows, msys2's mingw64 GCC works;
put only `mingw64/bin` on the path.

## Use

    vw render Daisy.trk daisy.wav               # a VocalWriter song file
    vw render Daisy.trk hal.wav --solo 1        # one track of it
    vw sing out.wav --lyrics "Dai- sy dai- sy give me your an- swer do" \
        --notes "A4:1.5 F4:1.5 D4:1.5 A3:1.5 E4:0.75 F4:0.75 G4:0.75 r:0.75 D4:1 E4:0.5 F4:0.5 D4:1.5" \
        --bpm 80 --voice 0
    vw phonemes daisy bicycle marriage          # the dictionary and the rules
    vw info Daisy.trk

Output is 44100 Hz, 16-bit stereo, the application's own format. `sing`
builds the same song structure the application builds when lyrics are typed:
the words go through the dictionary, the syllables onto the notes, the
parser inserts the closures and releases and fits the durations, and the
sequencer sings it with the reverb the application would use.

In C, the same through `include/vocalwriter.h`: `vw_engine_open`,
`vw_song_load` or `vw_song_build`, `vw_render_wav` (or `vw_render` with your
own sample sink), `vw_word_syllables`.

## Layout

```
src/        the port; the lifted units say so in their header comments
include/    vw_types.h (the original's records, from its debug symbols),
            vw_engine.h, vw_frontend.h, vw_synth.h (the lifted functions),
            vocalwriter.h (the API)
tools/      lift.py (the lifter), genheader.py, mksrc.py, mkfront.py,
            mkseq.py (the generators), stabs.py, macho.py, ppc.py, xref.py
test/       difftest.py, fronttest.py, seqtest.py, exporttest.py, harness.c
docs/       PORT.md -- how the recreation was done and what it models
```

`src/speech.c`, `orthtophon.c`, `parsephons.c`, `music.c`, `convertsmf.c`,
`expandtracks.c`, `synthapi.c` and `reverb.c` are generated (`make generate`)
and not edited by hand. `src/tables.c`, `macshim.c`, `synthglue.c`, `bank.c`,
`song.c`, `vw_api.c` and `vw.c` are the port's own: loading the data files
in place of the Mac OS Resource Manager, the memory and file calls the front
end makes, the sound and timer calls the sequencer glue makes, and the
songs and the API.

## Credit

VocalWriter is the work of KAE Labs; its data files remain theirs. The
PowerPC interpreter the verification runs on, the analysis of the file
formats and the reference exports are from the VocalWriter repository this
one sits beside.
