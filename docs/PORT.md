# How the recreation was done

The shipped VocalWriter 2.0.1 binary is a PowerPC Mach-O built with GCC 3
at `-O0` and with full STABS debug records: every function's name, its
parameters and locals with their types and stack slots, every struct with
its fields and offsets, and the source line of every statement. That makes
the code liftable rather than merely disassemblable: each function can be
turned back into C that uses the original's names, and the result can be
checked against the original instruction by instruction.

## The lifter

`tools/lift.py` decodes each function and executes its basic blocks
symbolically: registers hold expressions, stack slots resolve to the
named locals from STABS, memory accesses resolve to struct fields through
the types, and the `-O0` idioms are recognised -- integer-to-double through
the 0x43300000 magic constant, `fctiwz` through a store and reload,
multiply-high divisions, `srawi`/`addze` signed divisions, post-increment
through a pointer, the compiler's own temporaries. The blocks are then
structured into `if`/`else`, `while`, `for`, `do`/`while`, `switch`
(comparison trees and jump tables), `break` and `continue`; a handful of
loops that GCC laid out in ways no structured statement expresses keep a
`goto`. `tools/genheader.py` emits the original's records, with layout
assertions for the pointer-free ones, and `mksrc.py`, `mkfront.py` and
`mkseq.py` produce the units with the few edits the C compiler needs.

Rules that keep the arithmetic the original's:

- Floating point stays in the width the PowerPC used: `float` where the
  code used `fmuls`/`fadds`/`fdivs`, `double` where it used the double
  forms, with the conversions the compiler inserted. The build uses
  `-ffp-contract=off` (the original never fused a multiply and add) and
  `-fexcess-precision=standard`.
- Float-to-integer conversion saturates like `fctiwz` (`FTOI`); `mulhw` is
  a 64-bit product's high word (`MULHW`); shifts and adds wrap (`-fwrapv`).
- Where the original read a halfword or word through a byte pointer -- the
  song's event records, the dictionary's packed strings -- the port reads
  the bytes in big-endian order (`VW_LD16BE`, `VW_ST32BE`), so the data
  files keep their format.
- One stack slot is modelled: `SayFrame` reads its local `cycleIndex` on
  the first sample of every frame before assigning it. On a Macintosh that
  slot held the stack pointer saved by whichever of `StartNewPhon`,
  `Init_Ctrls_for_New_Phon` or `SaveFrame` ran last at that depth, a
  negative number under Mac OS X's main-thread stack, and the branch it
  takes matters to the breath. `VW_STACK_ADDRESS` and `vw_stack_slot_c0`
  model that (see the top of `src/speech.c`).

## What replaces Mac OS

- `src/tables.c`: the engine's tables came from the `ttvi` resource,
  byte-swapped into memory per element (`vw_load_ttvi`), then carved up by
  the original's own `InitSharedTables`. A small resource fork parser
  (`vw_resource`) replaces the Resource Manager; `vw_load_voices` relocates
  the `mvox` voice bank as the application did.
- `src/macshim.c`: `NewPtr`, `NewHandle`, `SetHandleSize` and the rest of
  the Memory Manager over `malloc`, with guard bytes so an overrun by the
  original's code is reported rather than silent; `SetFPos`/`FSRead` over a
  file loaded into memory, which is all the dictionary search needs.
- `src/synthglue.c`: the Sound Manager, Time Manager and Deferred Task
  calls the sequencer's Macintosh.c glue makes. There is no sound channel:
  rendering pulls buffers with `Synth_GetNextBuffer`, which is what the
  application's own File > Play to Disk export does. Timers fire at once;
  the callbacks reach the caller through the same pointers the application
  installed. `InitSynth` takes the shared tables from `vw_load_ttvi` instead
  of the resource fork.
- `src/bank.c`: the General MIDI bank's `mwav` (wave records and 16-bit
  samples) and `mdef` (instrument and wave-list definitions) resources,
  byte-swapped where the engine indexes them, handed over with
  `Synth_SetWaveBank` and `Synth_SetInstrument` as `main()` did.

Two records the application allocated by its own sizes (`shellVar`,
`synthVars`, `formantVar`) are allocated with `sizeof` here, since pointers
are wider; nothing in the engine depends on their sizes. `SeqHeader`,
`ExtSoundHeader` and `TMTask` keep the original's 2-byte packing.

## The application's play path, decoded

The editor half of the binary is optimised and not lifted; what the port
needs from it was read off the disassembly (`tools/xref.py` lists callers
and callees; the reference repository's `tools/ppcdis.py` disassembles):

- `main()`: `InitSynth`; `Synth_Startup(&svv, 48, 0)`; the `mvox`, `mwav`
  and `mdef` resources; `Synth_SetWaveBank(svv, mwav + table, pcmType,
  voices)` and `Synth_SetInstrument` for each of the `count - 1` instrument
  definitions; `Synth_SetTempoScale(svv, 0x10000)`; the reverb from the
  preferences (`SendReverbParams`: `Synth_SetReverb(svv, room/100, wet/100,
  1 - wet/100)`, the default preset being room 40, wet 24); then
  `Synth_StartMusic`.
- Opening a song gives each vocal track a speech channel
  (`Synth_MakeTrackSpeech`) -- before the track levels are set, which is
  what makes the level reach the voice.
- `StartCurSeq`: `Synth_SetKaraokeTrack(-1)`, then per track
  `Synth_SetPlayTrack` (a track with flag 0x10 is the karaoke track, not
  played), `Synth_TrackToChan(i, -1)`, `Synth_SetTrackLevel(i, level)`; for
  each vocal track `Synth_MakeSpeechData(svv, lyrics, events, handle, &len,
  140)` -- the speech rate is the constant 140 -- with the result hung off
  the track record; `Synth_SetKbdFlags(0)`; a `PlayRec` with the flags 1,
  `ticksPerBeat = 960 >> beatVal`, polyphony 48; `Synth_SeqPlayer`.
- Play to Disk: `Synth_GetNextBuffer` until the sequencer's done callback.

`src/song.c` does exactly that. A song file is a 758-byte `SeqHeader`
followed by the `SeqInfo` (32 `TrackInfo` records) at the offset the header
gives (0x2f6), with every offset inside relative to the `SeqInfo`; the
speech data offsets in a file are stale pointers and are rebuilt at play
time, as the application does. A vocal track's lyrics are 26-byte records
(a 13-byte text and a 13-byte phoneme string, both Pascal strings); its
events are 12 bytes (a 24-bit time, a byte, status, key, velocity, a 24-bit
duration and the lyric index), status 6 for a sung note and 8 for a silent
rest carrying the coming pitch; the tempo track ends with a status-9 mark.
The song's reverb settings sit in its resource fork (`sDat`, the editor's
`SongRez`, at +0x214 and +0x218).

## Verification

- `test/difftest.py`: the synthesiser alone, driven by a script of notes,
  controls and frames, both here and in the interpreter; the 4396-byte
  `formantVar` compared field by field at every snapshot, the samples word
  by word.
- `test/fronttest.py`: `OrthToPhon` over words (dictionary, morphology and
  rules paths), `MakeSpeechData` over sung syllables and over random
  syllable tables, `AdjustBoundryPhons`.
- `test/seqtest.py`: a whole song through the `Synth_*` API on both sides.
  The interpreter needed the XER carry bit for the sequencer's code;
  `tools/patch_interpreter.py` adds it to both of its cores.
- `test/exporttest.py`: the demo songs against the AIFF files the
  application exported. Identical, every sample.

## Known edges

- Odd inputs the application never produces make the original's code
  overrun its buffers (a syllable of nothing but a rest symbol, a
  full-length syllable a boundary rule lengthens); the port reproduces the
  code, and the guarded shim reports the overrun.
- A few return values the original left undefined (`r3` untouched) are
  returned as 0; each is marked in the source.
- Timing callbacks (beat, tempo, karaoke) are delivered immediately rather
  than a few milliseconds late as the Time Manager did; this affects only
  the moment a caller hears about them, not the audio.
