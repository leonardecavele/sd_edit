`sd_edit` is a small C command-line tool for `.wav` editing. It uses libsndfile for audio I/O and applies effects through ordered presets.

This repository serves solely as a sandbox for experimenting with agentic development environments, such as JetBrains Air. It is not intended to become a complete or production-ready project.

### Preset model

Presets are arrays of effect segments:

```c
typedef struct
{
    int name;
    int prmtr;
    int mix;
    int length;
} Segment;

typedef struct
{
    int nb_seg;
    Segment *segments;
} Preset;
```

Segment fields:

- `name`: effect ID
- `prmtr`: effect parameter
- `mix`: wet/dry percentage
- `length`: duration in samples

The codebase also stores deterministic random preset recipes:

```c
typedef struct
{
    int randomness;
    unsigned int seed;
} PresetRecipe;
```

`PresetRecipe` is persisted and reused. `Preset` is materialized for a specific input file.

### Effects

Effect IDs stay centralized in `include/effects.h` and `src/effects.c`.

- `1`: pitch
- `2`: shuffle
- `3`: gain
- `4`: reverse
- `5`: echo
- `6`: distortion

### Implemented CLI

The executable exposes:

- `help`
- `do <file.wav>`
- `play <file.wav>`
- `preset random [randomness]`
- `preset load <name>`
- `preset save <name>`
- `show preset [name]`
- `exit`
- `quit`

Typical flow:

1. `preset random [randomness]` or `preset load <name>`
2. `show preset`
3. `do <file.wav>`
4. `preset save <name>` if needed

### Preset storage

Saved preset recipes are stored in `presets.txt` with this format:

```text
name|randomness|seed
```

If the same name appears several times, the latest valid entry wins.

### Build and run

Prerequisites:

- `gcc`
- GNU `make`
- a Windows-compatible shell environment

The repository bundles libsndfile in `libsndfile-1.2.2-win64/`.

Build:

```sh
make
```

Clean:

```sh
make clean
```

Run:

```sh
make run
```

or:

```sh
sd_edit.exe
```

Useful direct commands:

```sh
sd_edit.exe help
sd_edit.exe play audio\test.wav
```

### Project layout

- `src/main.c`: CLI, prompt, file I/O, command dispatch
- `src/preset.c`: preset parsing, storage, random generation, materialization
- `src/effects.c`: effect catalog and implementations
- `include/effects.h`: effect declarations
- `include/preset.h`: preset data structures and APIs
- `audio/`: sample audio files

### Scope

- `.wav` only
- libsndfile-compatible file I/O
- preset-first workflow
- small, testable CLI changes

### After Thoughts

This experiment showed me that agentic development works better when the architecture is defined before implementation. 

Next time, I would:  
* Plan the modules and their responsibilities early, making it easier to divide the work cleanly between multiple AI agents.
* Design the actual code structure, not just the project’s high-level logical components. This would keep files focused and prevent the codebase from ending up as a few 3,000-line files.
* Assign dedicated agents to review code produced by other agents, focusing on correctness, consistency, and maintainability.
