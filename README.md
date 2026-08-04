# sd_edit

`sd_edit` is a small C mini-console for `.wav` editing. It uses libsndfile for audio I/O and models processing as a preset made of ordered effect segments.

The README serves two purposes in this repository:

- it keeps the original product objective visible
- it documents the current implementation exactly as the code behaves today

That split matters here because the code already exposes more commands, more effect IDs, and more runtime details than the original prototype note described.

## Overview

The core preset model is:

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

A preset is an ordered array of segments. Each segment selects one effect, one effect parameter, one wet/dry mix value, and one segment length.

The tool is `.wav`-only. The current implementation reads an input file into memory, applies each preset segment in order to consecutive regions of the sample buffer, and writes a new `.wav` file.

## Product objective vs current implementation

The original product draft defined this target command set:

- `do <file.wav>`
- `do <file.wav> <preset_name>`
- `play <file.wav>`
- `preset <preset_definition>`
- `save preset <name>`

It also only explicitly defined effect `1 = pitch`, while leaving later effect IDs open.

The current implementation keeps that direction, but already adds:

- an interactive REPL when no arguments are provided
- `help`
- `show preset`
- a centralized effect catalog with IDs `1` through `6`
- session-local preset state used by `show preset` and `save preset`

The sections below document the current code behavior without changing the product objective.

## Compilation

### Prerequisites

- `gcc`
- GNU `make`
- a Windows-compatible shell environment, because the makefile uses `cmd.exe`, `set`, and `del`

The repository already bundles libsndfile in `libsndfile-1.2.2-win64/`. The build expects that directory to stay in place.

### Build commands

Build the executable:

```sh
make
```

Run the program through the makefile wrapper:

```sh
make run ARGS="do audio\\test.wav"
```

Clean the executable and object files:

```sh
make clean
```

### Runtime PATH behavior

The `run` target prepends the bundled `libsndfile-1.2.2-win64\\bin` directory to `PATH` before launching `main.exe`.

That matters for two reasons:

- `main.exe` needs the libsndfile runtime, including `sndfile.dll`
- `play` launches the bundled `sndfile-play.exe`, which also depends on that runtime

For this repository, `make run` is the safest documented execution path on Windows because it sets that runtime path for the current process automatically.

### Direct execution

After `make`, you can run `main.exe` directly:

```sh
main.exe help
main.exe do audio\\test.wav
main.exe play audio\\test.wav
```

If you do that outside `make run`, the libsndfile runtime still has to be reachable. In practice, `sndfile.dll` must be available through `PATH`, and `play` also expects the bundled `sndfile-play.exe` at `.\libsndfile-1.2.2-win64\bin\sndfile-play.exe`.

## Usage

### Command reference

`do <file.wav>`

- validates that the input ends in `.wav`
- loads the file once to inspect audio metadata
- generates a random preset
- stores that preset as the current in-memory preset for the running process
- reloads the file, applies the preset segments in order, and writes `<input>_random.wav`

`do <file.wav> <preset_name>`

- validates that the input ends in `.wav`
- loads the latest matching named preset from `presets.txt`
- stores that preset as the current in-memory preset for the running process
- applies it to the input file and writes `<input>_<preset_name>.wav` after suffix sanitization

`preset <effect,parameter,mix,length;...>`

- parses an explicit preset definition
- replaces the current in-memory preset for the running process
- prints the parsed preset back to the console

`save preset <name>`

- appends the current in-memory preset to `presets.txt`
- fails if no preset has been created or loaded in the current process

`play <file.wav>`

- validates that the input ends in `.wav`
- resolves the full path to the bundled `sndfile-play.exe`
- resolves the full path to the requested audio file
- waits for the external player process to finish

`help`

- prints usage plus the currently registered effect catalog

`show preset`

- prints the current in-memory preset
- prints `No current preset.` when no preset has been loaded in the current process

### Interactive mode

Running `main.exe` with no arguments starts a REPL-style console:

```sh
make run
```

You will get a prompt like:

```text
wav mini-console
Type 'help' for usage and 'exit' to quit.
wav>
```

Inside interactive mode:

- `exit` and `quit` leave the console
- `preset ...` is handled specially so the rest of the line is preserved as the preset definition
- `show preset` is useful because the process keeps one current preset in memory
- `save preset <name>` only works after `preset ...` or `do ...` has set that current preset in the same process

Example interactive flow:

```text
preset 1,120,50,44100
save preset p1
show preset
```

### Session state limitation

Preset state is process-local. Separate command-line invocations do not share `current_preset`.

That means this does not work as a two-step workflow across separate runs:

```text
main.exe preset 1,120,50,44100
main.exe save preset p1
```

The second command starts a fresh process, so there is no current preset to save. Use interactive mode when you want to create or load a preset and then save it in the same session.

## Preset format

### Segment fields

Each segment contains:

- `name`: effect ID
- `prmtr`: effect-specific parameter
- `mix`: wet/dry percentage from `0` to `100`
- `length`: segment duration used by the processing loop

The command-line preset grammar is:

```text
effect,parameter,mix,length;effect,parameter,mix,length;...
```

Examples that match the parser:

```text
1,120,50,44100
1,120,50,44100;2,2048,100,22050
4,0,100,22050;5,8000,40,44100
```

Parser rules enforced by the code:

- segments are separated with `;`
- each segment must contain exactly four integers
- surrounding whitespace is tolerated
- effect IDs must exist in the effect catalog
- `mix` must be between `0` and `100`
- `length` must be greater than `0`

### Length interpretation

The original draft described `length` as a sample count. The current processing loop applies each segment as `segment.length * channels`, so in the implementation today `length` behaves as a frame count.

For a mono file, frames and samples are the same. For a stereo file, `44100` means `44100` frames, which becomes `88200` raw samples during processing.

## Preset persistence

Named presets are stored in `presets.txt` in this format:

```text
name|segment_count|definition
```

Example:

```text
p1|1|1,120,50,44100
```

Current persistence behavior:

- `save preset <name>` opens `presets.txt` in append mode
- every save writes one new line
- there is no in-place update or deduplication
- preset names cannot contain `|`, carriage returns, or newlines

When loading a named preset:

- the loader scans the whole file from top to bottom
- invalid lines are skipped
- the parsed segment count must match the stored `segment_count`
- if the same preset name appears multiple times, the latest valid matching entry wins

In other words, duplicate preset names resolve to the last valid saved line for that name.

## Output behavior

### Input and processing rules

- only `.wav` inputs are accepted
- the program loads the entire file into memory before processing
- preset segments are applied in order to consecutive regions of the sample buffer
- each segment uses its own `name`, `prmtr`, `mix`, and `length`
- if a segment would run past the end of the file, it is truncated to the remaining audio
- if the preset covers less than the full file, the remaining audio is left unchanged

### Output naming

The output name is built from the input path plus a sanitized suffix:

```text
<input_without_.wav>_<suffix>.wav
```

Suffix behavior:

- letters and digits are lowercased and kept
- `-` and `_` are kept
- all other characters become `_`
- an empty suffix falls back to `processed`

Examples:

- `do audio\\test.wav` writes `audio\\test_random.wav`
- `do audio\\test.wav p1` writes `audio\\test_p1.wav`

## Supported effects

The current effect catalog is centralized in `include/effects.h` and `src/effects.c`:

- `1 = pitch`
- `2 = shuffle`
- `3 = gain`
- `4 = reverse`
- `5 = echo`
- `6 = distortion`

Effect parameter usage in the current implementation:

- `1 pitch`: `prmtr` is pitch percent, `mix` is used
- `2 shuffle`: `prmtr` is chunk size, `mix` is ignored by the implementation
- `3 gain`: `prmtr` is gain percent, `mix` is used
- `4 reverse`: `prmtr` and `mix` are not used by the implementation
- `5 echo`: `prmtr` is delay in samples, `mix` is used
- `6 distortion`: `prmtr` is drive percent, `mix` is used

This is broader than the original roadmap note, which only explicitly reserved `1 = pitch` and left later IDs open.

## Architecture

### Responsibility split

- `src/main.c`
  - process-local session state
  - CLI parsing and interactive console
  - usage/help output
  - audio file loading and saving
  - output-path generation
  - preset application over the audio buffer
  - external playback launch

- `src/preset.c`
  - `Preset` memory management
  - preset-definition parsing
  - named preset serialization to `presets.txt`
  - named preset loading from `presets.txt`
  - random preset generation

- `src/effects.c`
  - centralized effect catalog
  - effect-name lookup and support checks
  - effect dispatch by ID
  - effect implementations

- `include/preset.h`
  - shared `Segment` and `Preset` data model
  - preset API declarations

- `include/effects.h`
  - effect ID enum
  - effect catalog descriptor type
  - effect API declarations

### Execution flow

Random `do` path:

1. validate the `.wav` path
2. load the audio once to collect frame count, sample rate, and channel count
3. generate a random preset with `1` to `4` segments using the centralized effect catalog
4. store that preset as the current session preset
5. reload the audio for processing
6. apply segments in order to consecutive parts of the buffer
7. save the result with the `_random.wav` suffix

Named-preset `do` path:

1. validate the `.wav` path
2. load the latest matching named preset from `presets.txt`
3. store that preset as the current session preset
4. load the audio
5. apply segments in order
6. save the result with a suffix derived from the preset name

`play` path:

1. validate the `.wav` path
2. resolve the absolute path to `.\libsndfile-1.2.2-win64\bin\sndfile-play.exe`
3. resolve the absolute path to the input file
4. spawn the bundled player and wait for it to exit

## Repository layout

- `src/main.c`: entry point, CLI, interactive console, audio I/O, playback launch
- `src/preset.c`: preset parsing, storage, loading, random generation
- `src/effects.c`: effect catalog and effect implementations
- `include/preset.h`: preset structs and preset API
- `include/effects.h`: effect IDs and effect API
- `audio/`: sample input and output files used during local testing
- `presets.txt`: append-only named preset store
- `libsndfile-1.2.2-win64/`: bundled libsndfile headers, libraries, DLLs, and `sndfile-play.exe`
- `makefile`: build, run, and clean targets

## Current limitations

- `.wav` is the only supported format
- preset state is not shared across separate program invocations
- named preset storage is plain text and append-only
- output processing is in-memory, not streaming
- `play` depends on the bundled Windows player and runtime DLL setup
- the README still describes a product roadmap, while the code reflects the current working implementation
