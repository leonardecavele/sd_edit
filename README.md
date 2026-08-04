# sd_edit

`sd_edit` is a small C mini-console for `.wav` editing. It uses libsndfile for audio I/O and models processing as a preset made of ordered effect segments.

This repository also serves as a small vehicle for testing agentic IDE workflows.

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

The current implementation keeps that direction, but now exposes it primarily through an interactive prompt opened by running the program with no command arguments. The same command words are still accepted as direct one-shot shortcuts.

Current implementation details that extend the original draft:

- interactive prompt-first workflow
- `help`
- `show preset`
- `exit` and `quit`
- a centralized effect catalog with IDs `1` through `6`
- session-local preset state used by `preset`, `do`, `show preset`, and `save preset`

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
make run
```

This is the primary workflow. It launches `main.exe` with no command arguments, which opens the interactive prompt.

If you want to run a direct shortcut through the wrapper instead, pass `ARGS`:

```sh
make run ARGS="do audio\\test.wav"
```

Clean the executable and object files:

```sh
make clean
```

### Runtime PATH behavior

At startup, `main.exe` now tries to load `sndfile.dll` in this order:

- first from the bundled repository path relative to the executable:
  `libsndfile-1.2.2-win64\\bin\\sndfile.dll`
- then from the current process `PATH`

If neither location works, the program reaches `main()` and prints a clear CLI error that mentions `sndfile.dll`, the expected bundled path, and the suggested fix.

The `run` target still prepends the bundled `libsndfile-1.2.2-win64\\bin` directory to `PATH` before launching `main.exe`, so it remains a convenient wrapper, but it is no longer the only thing preventing an opaque Windows loader failure before program startup.

### Direct execution

After `make`, you can start the interactive console directly:

```sh
main.exe
```

Shortcut mode is still available for one-shot commands:

```sh
main.exe help
main.exe do audio\\test.wav
main.exe play audio\\test.wav
```

Direct execution no longer depends on pre-configuring `PATH` just to get past process startup. When the bundled runtime is present in the repository layout, `main.exe` loads it automatically. If the bundled DLL is missing and `PATH` also does not provide `sndfile.dll`, the program prints a controlled error instead of failing before `main()`.

## Usage

### Primary workflow: interactive prompt

Start the program and work from the prompt:

```sh
make run
```

or, after building:

```sh
main.exe
```

You will get a prompt like:

```text
wav mini-console
Type 'help' for commands and 'exit' or 'quit' to leave.
wav>
```

Run `help` at the prompt to print the full command list plus the currently registered effect catalog.

The prompt also supports `Tab` completion. It completes:

- command words in the first token
- `.wav` paths after `do ` and `play `
- the `preset` subcommand after `save ` and `show `
- saved preset names after `do <file.wav> ` and `save preset `

When there are multiple matches, the first `Tab` extends to the longest common prefix and repeated `Tab` presses cycle concrete matches in place. The `preset <definition>` grammar stays free-form, so `Tab` does not try to complete numeric segment definitions.

Supported prompt commands:

- `help`
  - prints the interactive command reference and current effect catalog

- `do <file.wav>`
  - validates that the input ends in `.wav`
  - loads the file once to inspect audio metadata
  - generates a random preset
  - stores that preset as the current in-memory preset for the running process
  - reloads the file, applies the preset segments in order, and writes `<input>_random.wav`

- `do <file.wav> <preset_name>`
  - validates that the input ends in `.wav`
  - loads the latest matching named preset from `presets.txt`
  - stores that preset as the current in-memory preset for the running process
  - applies it to the input file and writes `<input>_<preset_name>.wav` after suffix sanitization

- `play <file.wav>`
  - validates that the input ends in `.wav`
  - resolves the full path to the bundled `sndfile-play.exe` relative to `main.exe`
  - resolves the full path to the requested audio file
  - waits for the external player process to finish

- `preset <effect,parameter,mix,length;...>`
  - parses an explicit preset definition
  - replaces the current in-memory preset for the running process
  - preserves the full definition text after the command name, including internal spaces
  - prints the parsed preset back to the console

- `save preset <name>`
  - appends the current in-memory preset to `presets.txt`
  - fails if no preset has been created or loaded in the current process

- `show preset`
  - prints the current in-memory preset
  - prints `No current preset.` when no preset has been loaded in the current process

- `exit`
  - leaves the console

- `quit`
  - leaves the console

Example interactive flow:

```text
wav> help
wav> show preset
wav> preset 1,120,50,44100
wav> PRESET 1,120,50,44100; 5,8000,40,44100
wav> save preset demo
wav> do audio\test.wav demo
wav> do audio\test.wav
wav> exit
```

The prompt is the intended way to chain commands that depend on session state. In particular, `show preset` and `save preset <name>` only operate on the current in-memory preset held by the running process.

If a path contains spaces, start the token with a quote before using `Tab`, for example `do "path with spaces\\te`.

### Direct invocation shortcuts

The same shared command dispatcher also accepts direct argv shortcuts for one-shot use:

```sh
main.exe help
main.exe do audio\\test.wav
main.exe do audio\\test.wav demo
main.exe play audio\\test.wav
```

Direct `preset` is also supported. If the definition contains spaces, quote it so the shell passes the full text as command arguments:

```sh
main.exe preset "1,120,50,44100; 5,8000,40,44100"
```

### Session state limitation

Preset state is process-local. Separate command-line invocations do not share `current_preset`.

That means this does not work as a two-step workflow across separate runs:

```text
main.exe preset 1,120,50,44100
main.exe save preset p1
```

The second command starts a fresh process, so there is no current preset to save. Use the interactive prompt when you want to create or load a preset and then save it in the same session.

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
- `Tab` completion reads the same store, but free-form names are still allowed if you keep typing manually

When loading a named preset:

- the loader scans the whole file from top to bottom
- invalid lines are skipped
- the parsed segment count must match the stored `segment_count`
- if the same preset name appears multiple times, the latest valid matching entry wins

Prompt completion follows the same validation rules. Invalid lines do not contribute suggestions, and duplicate preset names appear at most once in the suggestion set using the latest valid stored definition.

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
- if that output path already exists, the program retries with `1`, `2`, and so on appended to the suffix until it finds a free filename

Examples:

- `do audio\\test.wav` writes `audio\\test_random.wav`
- `do audio\\test.wav p1` writes `audio\\test_p1.wav`
- if `audio\\test_random.wav` already exists, the next random run writes `audio\\test_random1.wav`

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
  - prompt parsing, direct shortcut parsing, and interactive console
  - shared usage/help output
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

### Session model

The program keeps exactly one `current_preset` in memory per process.

That matters most in the interactive console:

- `preset ...` replaces the current session preset
- `do <file.wav>` and `do <file.wav> <preset_name>` both update the current session preset
- `show preset` reads the current session preset
- `save preset <name>` persists the current session preset

A new direct invocation such as `main.exe save preset demo` starts with an empty session, so it cannot see preset state created by an earlier process.

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
2. resolve the absolute path to the bundled `libsndfile-1.2.2-win64\bin\sndfile-play.exe` relative to `main.exe`
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
- `play` still depends on the bundled Windows player and a working libsndfile runtime, even though both are now resolved more robustly at startup
