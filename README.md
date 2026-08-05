# sd_edit

`sd_edit` is a small C console tool for `.wav` editing. It uses libsndfile for audio I/O and applies effects through ordered presets.

The repository now treats presets in two layers:

- a persisted recipe, which is small and deterministic
- a materialized preset, which is generated only when `do <file.wav>` knows the input duration

## Core preset model

The concrete execution model is still an array of segments:

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

Each segment selects:

- one effect ID
- one effect parameter
- one wet/dry mix percentage
- one segment length in frames

Persisted random presets are stored as recipes:

```c
typedef struct
{
    int randomness;
    unsigned int seed;
} PresetRecipe;
```

`PresetRecipe` is what `preset random`, `preset save`, `preset load`, and `show preset` operate on. `Preset` is what `do <file.wav>` materializes and applies.

The tool is `.wav`-only.

## Command model

Supported commands:

- `help`
- `do <file.wav>`
- `play <file.wav>`
- `preset random [randomness]`
- `preset load <name>`
- `preset save <name>`
- `show preset [name]`
- `exit`
- `quit`

The active session keeps exactly one current preset recipe in memory.

Typical flow:

1. `preset random [randomness]` or `preset load <name>`
2. `show preset`
3. `do <file.wav>`
4. optionally `preset save <name>`

`do` does not use a pre-baked segment list anymore. It derives the concrete segment count, segment lengths, effect IDs, and parameters from:

- the recipe `randomness`
- the recipe `seed`
- the input file duration and audio metadata

## Build and run

### Prerequisites

- `gcc`
- GNU `make`
- a Windows-compatible shell environment, because the makefile uses `cmd.exe`, `set`, and `del`

The repository bundles libsndfile in `libsndfile-1.2.2-win64/`.

### Build

```sh
make
```

Clean outputs:

```sh
make clean
```

Run through the makefile wrapper:

```sh
make run
```

The wrapper prepends `libsndfile-1.2.2-win64\bin` to `PATH` and launches `sd_edit.exe`.

### Direct execution

```sh
sd_edit.exe
```

Useful one-shot commands:

```sh
sd_edit.exe help
sd_edit.exe show preset demo
sd_edit.exe play audio\test.wav
```

`do <file.wav>` still depends on the current in-process recipe, so it is normally used from the interactive prompt.

## Interactive console

Start the prompt with:

```sh
make run
```

or:

```sh
sd_edit.exe
```

When no recipe is active, the prompt is:

```text
sd_edit>
```

When a recipe is active, the prompt shows recipe metadata:

```text
random_5 [randomness=5 seed=123456789] sd_edit>
demo [randomness=5 seed=123456789] sd_edit>
```

Tab completion still covers:

- top-level command names
- `.wav` paths after `do ` and `play `
- `random`, `load`, and `save` after `preset `
- randomness values `0` through `10` after `preset random `
- saved preset names after `preset load `, `preset save `, and `show preset `

## Command behavior

### `preset random [randomness]`

Creates a deterministic recipe, not a stored segment list.

Rules:

- omitted `randomness` defaults to `5`
- `randomness` must be an integer in `0..10`
- a new unsigned `seed` is minted when the recipe is created
- the session name is `random_<randomness>`
- invalid values do not replace the current recipe

Examples:

- `preset random`
- `preset random 0`
- `preset random 10`

### `show preset`

Prints the current recipe metadata:

```text
Preset recipe 'random_5':
  randomness=5
  seed=123456789
  materialization=derived at do-time from input duration and recipe seed
```

It does not print a fake precomputed segment list.

### `show preset <name>`

Loads and prints the latest valid stored recipe named `<name>` from `presets.txt` without replacing the current session recipe.

### `preset save <name>`

Appends the current recipe to `presets.txt`.

Rules:

- fails if there is no current recipe
- saves only deterministic constants
- after a successful save, the active display name becomes `<name>`

### `preset load <name>`

Loads the latest valid stored recipe named `<name>` from `presets.txt`, stores it as the current session recipe, prints it, and updates the prompt prefix.

### `do <file.wav>`

Applies the current session recipe to the input file and writes a processed `.wav` output.

Rules:

- fails if there is no current recipe
- only accepts `.wav` input
- loads the audio first
- materializes a concrete `Preset` only after input metadata is known
- computes segment lengths so they sum to the full input frame count
- applies segments in order across the whole file
- saves with a suffix derived from the current recipe display name

Example:

```text
sd_edit> preset random 0
Generated preset recipe 'random_0'.
Preset recipe 'random_0':
  randomness=0
  seed=123456789
  materialization=derived at do-time from input duration and recipe seed
random_0 [randomness=0 seed=123456789] sd_edit> do audio\test.wav
Materialized 2 segment(s) for 4.894 second(s) of audio from recipe 'random_0'.
Saved processed audio to audio\test_random_0.wav
```

### `play <file.wav>`

Launches the bundled `sndfile-play.exe` relative to `sd_edit.exe` and waits for it to exit.

### `exit` / `quit`

Leaves the interactive console.

## Sample-aware random recipe behavior

Random preset materialization is based on a duration-aware density curve.

At the low end:

- about `2s` with `randomness=0` materializes `1` segment
- about `5s` with `randomness=0` materializes `2` segments

At the high end:

- about `30s` with `randomness=10` materializes inside a `40..60` segment band

The exact concrete count is chosen deterministically from a recipe-local PRNG. Very small counts collapse to a single exact value so short files do not oscillate.

The concrete segment lengths are then split across the full file with deterministic jitter, while keeping every segment length positive and making the total match the full input frame count.

That means:

- the same recipe on the same input file replays identically
- the same recipe on a different duration can materialize a different concrete preset
- the concrete preset is not persisted; it is regenerated when `do` runs

## Preset storage

### Stored format

Named presets in `presets.txt` now use:

```text
name|randomness|seed
```

Example:

```text
demo|5|123456789
```

Rules:

- saves append one new line
- there is no in-place update or deduplication
- names cannot contain `|`, carriage returns, or newlines
- invalid lines are skipped while loading and while collecting completion suggestions
- if the same name appears multiple times, the latest valid matching line wins
- legacy `name|segment_count|definition` lines are no longer considered valid stored presets

### Determinism contract

For the same:

- recipe name
- recipe `randomness`
- recipe `seed`
- input audio bytes

the program should materialize the same concrete segments and produce the same output bytes.

## Output naming

Outputs are written as:

```text
<input_without_.wav>_<suffix>.wav
```

Suffix rules:

- letters and digits are lowercased and kept
- `-` and `_` are kept
- all other characters become `_`
- an empty suffix falls back to `processed`
- if the target already exists, the program retries with `1`, `2`, and so on appended

## Supported effects

The effect catalog is centralized in `include/effects.h` and `src/effects.c`:

- `1 = pitch`
- `2 = shuffle`
- `3 = gain`
- `4 = reverse`
- `5 = echo`
- `6 = distortion`

Current parameter behavior:

- `1 pitch`: `prmtr` is pitch percent, `mix` is used
- `2 shuffle`: `prmtr` is chunk size in samples, `mix` is ignored
- `3 gain`: `prmtr` is gain percent, `mix` is used
- `4 reverse`: `prmtr` and `mix` are ignored
- `5 echo`: `prmtr` is delay in samples, `mix` is used
- `6 distortion`: `prmtr` is drive percent, `mix` is used

## Architecture

- `src/main.c`
  - session state
  - prompt and command dispatch
  - audio file loading and saving
  - recipe-driven processing flow
  - playback launch

- `src/preset.c`
  - `Preset` memory management
  - stored recipe parsing
  - named recipe save/load/list operations
  - deterministic recipe generation
  - sample-aware preset materialization

- `src/effects.c`
  - effect catalog
  - effect dispatch
  - effect implementations

## Current limitations

- `.wav` is the only supported format
- preset recipe state is process-local
- named preset storage is plain text and append-only
- processing is in-memory, not streaming
- playback depends on the bundled Windows player and a working libsndfile runtime
