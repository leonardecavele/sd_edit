# AGENTS.md

This repository targets a small C command-line tool for `.wav` editing only. The README is the product definition. When there is a conflict between the current code and the intended behavior, optimize changes toward the README objective, not toward preserving prototype shortcuts.

## Product objective

Build a mini console in C that accepts several audio-related commands, applies effects to `.wav` files, and supports reusable presets made of effect segments.

## Functional target from README

The intended command set is:

- `do <file.wav>`
  - load a wav file
  - generate a random preset
  - apply it
  - save the resulting file
- `play <file.wav>`
  - play the wav file
- `save preset <name>`
  - save the current preset to a presets file
- `do <file.wav> <preset_name>`
  - apply an existing preset to a wav file
  - save the resulting file
- `preset <preset_definition>`
  - create a preset explicitly from user input

## Audio and preset model

The README defines presets as an array of segments. Preserve that direction in future work.

```c
typedef struct {
    int name;
    int prmtr;
    int mix;
    int length;
} Segment;

typedef struct {
    int nb_seg;
    Segment* segments;
} Preset;
```

Interpretation:

- `name`: effect identifier
- `prmtr`: effect parameter, for example pitch intensity
- `mix`: wet/dry percentage
- `length`: segment duration in samples

## Effect roadmap

- Effect `1`: pitch
- Effect `2`: not defined yet

If new effects are added, keep the effect ID mapping explicit and centralized.

## Current implementation status

The current code is still a prototype and does not yet match the target CLI:

- `src/main.c` uses hard-coded file paths
- there is no command parsing yet
- there is no preset parser or preset persistence yet
- current processing is a direct read/process/write loop
- implemented effects are utility-style functions in `src/effects.c`
- `shuffle_chunks` is currently the active transformation

## Development priorities

Prefer work in this order unless the task explicitly says otherwise:

1. Make the program accept CLI arguments instead of hard-coded file names.
2. Define in-memory `Segment` and `Preset` structures in code.
3. Implement `do`, `preset`, and `save preset` flows.
4. Add preset serialization/deserialization, likely via `presets.txt`.
5. Add `play` support only after the core edit/apply/save loop is stable.
6. Expand effects after the preset pipeline exists.

## Change guidance

- Keep the tool `.wav`-only unless the user explicitly changes scope.
- Prefer small, testable steps toward the README command interface.
- Do not add broad abstractions before the preset model is used by the CLI.
- If adding an effect, expose it through the preset/effect ID model, not only as a one-off function call from `main`.
- When changing file I/O, preserve compatibility with libsndfile.
- Fix obvious correctness issues encountered on the way, especially around file open checks and output handling.

## Repository layout

- `src/main.c`: entry point and current processing loop
- `src/effects.c`: audio effect implementations
- `include/effects.h`: effect declarations
- `audio/`: sample input/output files
- `libsndfile-1.2.2-win64/`: bundled libsndfile
- `makefile`: current build script

## Build notes

- Compiler target is `gcc`
- The bundled libsndfile is part of the expected setup
- The current `makefile` mixes Unix-style shell usage with a Windows workspace, so build execution may require a compatible shell environment

## Validation expectations

- After edits, verify the project still builds
- For command-path changes, test with real `.wav` files from `audio/`
- For preset work, validate both random preset generation and named preset reuse
