# sd_edit

`sd_edit` is a small C console tool for `.wav` editing. It uses libsndfile for audio I/O and models processing as an ordered preset made of effect segments.

This README is the product definition for the repository and documents the code as it behaves now.

## Core preset model

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

A preset is an ordered array of segments. Each segment selects:

- one effect ID
- one effect parameter
- one wet/dry mix percentage
- one segment length

The tool is `.wav`-only.

## Command model

The console now uses a session-first workflow.

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

The intended flow is:

1. start with no preset loaded
2. load or generate the active preset explicitly
3. apply the active preset with `do <file.wav>`
4. optionally save it for reuse

`do` no longer generates or loads presets on demand.

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

This produces:

```text
sd_edit.exe
```

Clean build outputs:

```sh
make clean
```

Run through the makefile wrapper:

```sh
make run
```

The wrapper prepends the bundled `libsndfile-1.2.2-win64\bin` directory to `PATH` and launches `sd_edit.exe`.

### Direct execution

After building:

```sh
sd_edit.exe
```

Useful one-shot direct commands:

```sh
sd_edit.exe help
sd_edit.exe play audio\test.wav
sd_edit.exe show preset demo
```

`do <file.wav>` depends on the current in-process preset, so it is normally used from the interactive prompt rather than as a one-shot direct command.

### Runtime DLL behavior

At startup, `sd_edit.exe` tries to load `sndfile.dll` in this order:

1. from the bundled repository path relative to the executable:
   `libsndfile-1.2.2-win64\bin\sndfile.dll`
2. from the current process `PATH`

If both fail, the program prints a controlled error instead of failing before `main()`.

## Interactive console

Start the prompt with:

```sh
make run
```

or:

```sh
sd_edit.exe
```

The startup banner is:

```text
sd_edit
Type 'help' for commands and 'exit' or 'quit' to leave.
sd_edit>
```

### Prompt behavior

When no preset is active, the prompt is just:

```text
sd_edit>
```

When a preset is active, the prompt shows the preset name and prompt stats before `sd_edit>`:

```text
random_5:[segments=6 length=247143]sd_edit>
demo:[segments=6 length=247143]sd_edit>
```

On a Windows console, the preset name is shown in green and the stats are shown in red. When colored redraw is unavailable, the same text is printed without color.

### Tab completion

The prompt supports `Tab` completion for:

- top-level command names
- `.wav` paths after `do ` and `play `
- `random`, `load`, and `save` after `preset `
- randomness values `0` through `10` after `preset random `
- saved preset names after `preset load `, `preset save `, and `show preset `

When there are multiple matches, the first `Tab` extends to the longest common prefix and repeated `Tab` presses cycle through matches.

If a path contains spaces, start the token with a quote before using `Tab`.

## Command behavior

### `help`

Prints the command list and the current effect catalog.

### `preset random [randomness]`

Creates the current in-memory preset without loading any audio file first.

Rules:

- omitted `randomness` defaults to `5`
- `randomness` must be an integer in `0..10`
- the number maps directly to the segment count as `randomness + 1`
- `0` creates `1` segment
- `10` creates `11` segments
- invalid values do not replace the current preset

The generated preset receives a synthetic session name:

```text
random_<randomness>
```

Examples:

- `preset random`
- `preset random 0`
- `preset random 10`

### `preset load <name>`

Loads the latest valid preset named `<name>` from `presets.txt`, stores it as the current session preset, prints it, and updates the prompt prefix to that preset name.

### `preset save <name>`

Appends the current session preset to `presets.txt`.

Rules:

- fails if there is no current preset in the running process
- preserves the existing append-only storage format
- after a successful save, the active preset display name becomes `<name>`

### `show preset`

Prints the current session preset.

If no preset is active, it prints:

```text
No current preset.
```

### `show preset <name>`

Loads and prints the latest valid stored preset named `<name>` from `presets.txt` without replacing the current session preset.

This is read-only named lookup.

### `do <file.wav>`

Applies the current session preset to the input file and writes a processed `.wav` output.

Rules:

- fails if there is no current preset
- only accepts `.wav` input
- uses the current preset display name as the output suffix
- applies segments in order to consecutive parts of the sample buffer
- truncates a segment if it would run past end-of-file
- leaves any uncovered tail audio unchanged

Examples:

- after `preset random 5`, `do audio\test.wav` writes `audio\test_random_5.wav`
- after `preset load demo`, `do audio\test.wav` writes `audio\test_demo.wav`

### `play <file.wav>`

Launches the bundled `sndfile-play.exe` relative to `sd_edit.exe` and waits for it to exit.

### `exit` / `quit`

Leave the interactive console.

## Example interactive session

```text
sd_edit> show preset
No current preset.
sd_edit> preset random
Generated preset 'random_5'.
Preset 'random_5' with 6 segment(s):
  1. effect=...
random_5:[segments=6 length=...]sd_edit> do audio\test.wav
Saved processed audio to audio\test_random_5.wav
random_5:[segments=6 length=...]sd_edit> preset save demo
Saved current preset as 'demo' to presets.txt
demo:[segments=6 length=...]sd_edit> show preset demo
Preset 'demo' with 6 segment(s):
  1. effect=...
demo:[segments=6 length=...]sd_edit> exit
```

## Session model

The program keeps exactly one current preset in memory per process.

That affects command behavior directly:

- `preset random [randomness]` replaces the current session preset
- `preset load <name>` replaces the current session preset
- `preset save <name>` persists the current session preset
- `show preset` reads the current session preset
- `show preset <name>` does not replace the current session preset
- `do <file.wav>` uses the current session preset as-is

Preset state is not shared across separate invocations.

That means this does not work:

```text
sd_edit.exe preset random 5
sd_edit.exe do audio\test.wav
```

The second command starts a fresh process with no current preset loaded. Use the interactive prompt when you want to chain `preset random` or `preset load` into `do`.

## Preset format and persistence

### Stored format

Named presets are stored in `presets.txt` as:

```text
name|segment_count|definition
```

Example:

```text
demo|2|1,120,50,44100;5,8000,40,22050
```

Persistence rules:

- saves append one new line
- there is no in-place update or deduplication
- names cannot contain `|`, carriage returns, or newlines
- invalid lines are skipped while loading and while collecting completion suggestions
- if the same name appears multiple times, the latest valid matching line wins when loading

### Definition syntax

Stored preset definitions still use:

```text
effect,parameter,mix,length;effect,parameter,mix,length;...
```

Parser rules:

- segments are separated with `;`
- each segment must contain exactly four integers
- surrounding whitespace is tolerated
- effect IDs must exist in the effect catalog
- `mix` must be in `0..100`
- `length` must be greater than `0`

### Length interpretation

`length` is consumed by the processing loop as a frame count. During processing, each segment spans:

```text
segment.length * channels
```

raw samples in the current file.

Randomly generated presets use positive default frame lengths in the range `11025..88200`. This keeps presets serializable before any audio file has been selected. If a generated segment is longer than the remaining input, processing truncates it at EOF.

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
- if the target path already exists, the program retries with `1`, `2`, and so on appended

Examples:

- active preset `random_5` -> `audio\test_random_5.wav`
- active preset `demo` -> `audio\test_demo.wav`
- if `audio\test_demo.wav` already exists, the next save becomes `audio\test_demo1.wav`

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
- `2 shuffle`: `prmtr` is chunk size, `mix` is ignored
- `3 gain`: `prmtr` is gain percent, `mix` is used
- `4 reverse`: `prmtr` and `mix` are ignored
- `5 echo`: `prmtr` is delay in samples, `mix` is used
- `6 distortion`: `prmtr` is drive percent, `mix` is used

## Architecture

### Responsibility split

- `src/main.c`
  - process-local session state
  - interactive console and direct command dispatch
  - prompt rendering and completion
  - audio file loading and saving
  - preset application over the audio buffer
  - playback launch

- `src/preset.c`
  - `Preset` memory management
  - preset parsing
  - named preset save/load/list operations
  - random preset generation

- `src/effects.c`
  - effect catalog
  - effect dispatch
  - effect implementations

- `include/preset.h`
  - shared preset structs and preset API

- `include/effects.h`
  - effect IDs and effect API

### Execution flow

`preset random [randomness]`:

1. validate the optional randomness value
2. generate `randomness + 1` segments from the centralized effect catalog
3. assign random parameters, mixes, and positive default lengths
4. store the preset in the current session under `random_<randomness>`
5. print the preset and update the prompt

`preset load <name>`:

1. scan `presets.txt`
2. keep the latest valid matching entry
3. store it as the current session preset
4. print it and update the prompt

`do <file.wav>`:

1. validate that a current preset exists
2. validate the `.wav` path
3. load the audio into memory
4. apply preset segments in order
5. save the result with a suffix derived from the current preset name

`show preset <name>`:

1. scan `presets.txt`
2. keep the latest valid matching entry
3. print it
4. leave the current session unchanged

`play <file.wav>`:

1. validate the `.wav` path
2. resolve the bundled `libsndfile-1.2.2-win64\bin\sndfile-play.exe` relative to `sd_edit.exe`
3. resolve the absolute path to the input file
4. spawn the player and wait for it to exit

## Repository layout

- `src/main.c`: CLI, prompt, audio I/O, preset application, playback
- `src/preset.c`: preset parsing, storage, loading, random generation
- `src/effects.c`: effect catalog and effect implementations
- `include/preset.h`: preset structs and preset API
- `include/effects.h`: effect IDs and effect API
- `audio/`: sample input and output files used for local testing
- `presets.txt`: append-only named preset store
- `libsndfile-1.2.2-win64/`: bundled libsndfile and `sndfile-play.exe`
- `makefile`: build, run, and clean targets

## Current limitations

- `.wav` is the only supported format
- preset state is process-local
- named preset storage is plain text and append-only
- processing is in-memory, not streaming
- playback depends on the bundled Windows player and a working libsndfile runtime
