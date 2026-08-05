## 1. Goal

Align the console with the new session-first command model: start with no preset loaded, manage the active preset explicitly through `preset random [randomness]`, `preset load <name>`, and `preset save <name>`, apply it with `do <file.wav>`, keep `play <file.wav>` / `show preset [name]` / `help` / `exit` / `quit`, rename the tool to `sd_edit`, and show the current preset name plus prompt stats before `sd_edit>` when a preset is active.

## 2. Approach

Keep the existing process-local session and audio I/O pipeline in [src/main.c:46-50], [src/main.c:2005-2228], and [src/preset.c:383-481], but invert the preset lifecycle so `do` becomes a pure apply step instead of generating or loading presets on demand. Reuse the existing `Preset` / `Segment` model in [include/preset.h:6-28] and the centralized effect catalog in [include/effects.h:6-32], while extending session metadata in [src/main.c:46-50] so the active preset has a display name and prompt summary. Assumptions for the implementation: `preset random` defaults to `5` when the optional argument is omitted, `randomness` is validated as an integer in `0..10` and mapped to `1..11` segments, `show preset <name>` is read-only and does not switch the active session preset, and prompt stats are the segment count plus cumulative preset length.

## 3. File Changes

- **Modify** `src/main.c` around [src/main.c:19-25], [src/main.c:46-61], [src/main.c:494-539], [src/main.c:989-1050], [src/main.c:1243-1487], [src/main.c:1665-1853], [src/main.c:1989-2002], [src/main.c:2235-2600]
  - This file owns the session model, command grammar, Tab completion, prompt rendering, usage text, audio apply/save flow, and interactive/direct dispatch.
  - Changes here will remove the old `preset <definition>`, `save preset <name>`, and `do <file.wav> <preset_name>` behavior; add the new preset subcommands; make `do <file.wav>` require a current preset; build the dynamic `sd_edit>` prompt; and rename all user-facing `main.exe` / `wav` strings.

- **Modify** `src/preset.c` around [src/preset.c:11-16], [src/preset.c:137-164], [src/preset.c:327-380], [src/preset.c:383-481], [src/preset.c:547-620]
  - This file already owns preset parsing, persistence, and random generation.
  - Changes here will replace the audio-length-dependent random generator with an audio-independent one that can run before any `.wav` file is chosen, while keeping the existing `presets.txt` serialization and loading behavior intact.

- **Modify** `include/preset.h` around [include/preset.h:4-28]
  - This header exposes the shared preset model and preset APIs.
  - Changes here will update the random-preset function signature to match the new command semantics and keep the preset/session API aligned with `src/preset.c`.

- **Modify** `makefile` around [makefile:6-8], [makefile:23-27], [makefile:32-34]
  - This file controls the produced executable name and the run target.
  - Changes here will rename `main.exe` to `sd_edit.exe` so the build artifact matches the requested console name.

- **Modify** `README.md` around [README.md:38-60], [README.md:133-255], [README.md:297-360], [README.md:385-458]
  - The README is the product definition and currently documents the old command set and prompt samples.
  - Changes here will update the command reference, examples, direct-invocation notes, prompt examples, binary name, and session-state explanation to match the new workflow.

## 4. Implementation Steps

### Task 1: Make the session own an explicit current preset identity

1. In `src/main.c` at [src/main.c:46-50] and [src/main.c:283-294], extend `Session` so it stores both the active `Preset` and a current preset display name, then clear both in `init_session` / `free_session`.
2. In `src/main.c` at [src/main.c:1989-2002], replace `update_current_preset(Session *, const Preset *)` with a helper that copies the preset and also stores a display label such as a loaded preset name or a synthetic random label (for example `random_5`).
3. In `src/main.c` near [src/main.c:1796-1820], refactor the preset-printing helper so it can print either the current session preset or a temporary named preset, including the preset name when known.
4. In `src/main.c` near [src/main.c:1796-1820] or a nearby helper block, add a compact preset-summary formatter that computes the prompt stats from the active preset (assumption: `segments=<n>` plus cumulative `length=<sum>`).

### Task 2: Replace the old preset command grammar with the new workflow

1. In `src/main.c` at [src/main.c:2235-2267], remove the free-form `handle_preset_command` path that parses `preset <effect,parameter,mix,length;...>` and replace it with dedicated handlers for:
   - `preset random [randomness]`
   - `preset load <name>`
   - `preset save <name>`
2. In `src/preset.c` / `include/preset.h` at [src/preset.c:547-620] and [include/preset.h:20-28], change `generate_random_preset` so it no longer depends on `total_frames`, `sample_rate`, or `channels`; instead it validates the `0..10` randomness index, derives a deterministic segment count from it, picks effect IDs from the existing catalog in [include/effects.h:6-32], chooses effect parameters and mix values using the existing helper ranges in [src/preset.c:137-164], and assigns positive default segment lengths so the preset remains serializable before any audio file is loaded.
3. In `src/main.c` at [src/main.c:2269-2291], rename and adapt the save handler so `preset save <name>` reuses `save_named_preset` from [src/preset.c:383-427], errors when the session is empty, and updates the current display name to the saved name after a successful save.
4. In `src/main.c` at [src/main.c:2293-2360], replace the current `do` handler so:
   - `do <file.wav>` is the only accepted `do` form;
   - it errors if `session->has_current_preset == 0`;
   - it uses the current session preset directly instead of generating a random preset or loading one from storage;
   - it uses the current preset display name as the output suffix passed into `process_audio_file`.
5. In `src/main.c` at [src/main.c:2362-2403], keep `play <file.wav>` unchanged except for user-facing `sd_edit.exe` wording in related messages.
6. In `src/main.c` at [src/main.c:2405-2475], rewrite the shared dispatcher so the accepted commands become:
   - `help`
   - `do <file.wav>`
   - `play <file.wav>`
   - `preset random [randomness]`
   - `preset load <name>`
   - `preset save <name>`
   - `show preset [name]`
   - `exit`
   - `quit`
7. In `src/main.c` at [src/main.c:2478-2535], remove the old interactive special-case that forwards the entire `preset` tail as a raw definition string, because `preset` is no longer free-form.
8. In `src/main.c` at [src/main.c:2538-2600], remove the direct-shortcut code that rejoins `preset` arguments into a free-form definition, and let direct invocation use the same tokenized grammar as the interactive prompt.

### Task 3: Keep named preset storage, but make `show preset [name]` read-only for named lookup

1. In `src/main.c` at [src/main.c:2405-2475], implement `show preset` with no third token as “print the current session preset, or `No current preset.` when none is loaded”.
2. In the same dispatcher block at [src/main.c:2405-2475], implement `show preset <name>` by loading the named preset from `presets.txt` through `load_named_preset` in [src/preset.c:430-481], printing it, and freeing the temporary preset without calling the session update helper from [src/main.c:1989-2002].
3. Preserve the existing persistence format from [src/preset.c:219-279] and [src/preset.c:383-481] so saved presets keep using `name|segment_count|definition` in `presets.txt` and existing saved entries remain loadable.

### Task 4: Rework completion so the new grammar is discoverable

1. In `src/main.c` at [src/main.c:494-505], replace the current completion contexts (`COMPLETE_DO_PRESET`, `COMPLETE_SAVE_SUBCOMMAND`, `COMPLETE_SAVE_PRESET_NAME`, `COMPLETE_SHOW_SUBCOMMAND`) with contexts for:
   - `preset` subcommands
   - `preset random` numeric values
   - `preset load` preset names
   - `preset save` preset names
   - `show preset` optional preset names
2. In `src/main.c` at [src/main.c:989-1050], rewrite `parse_completion_state` so:
   - `do` only completes a `.wav` path after the command word;
   - `preset` first completes `random`, `load`, or `save`;
   - `preset random` optionally completes `0` through `10`;
   - `preset load` and `preset save` complete stored preset names;
   - `show` first completes `preset`, then optionally a preset name.
3. In `src/main.c` at [src/main.c:1067-1089] and [src/main.c:1423-1487], update the append-space rules and candidate collectors so they match the new contexts, keep `.wav` discovery through [src/main.c:1325-1420], and continue to source preset names from `list_named_presets` in [src/main.c:1243-1276] / [src/preset.c:484-530].
4. Add a small in-memory numeric catalog (`"0"` through `"10"`) near [src/main.c:52-61] or [src/main.c:1423-1487] so `Tab` completion can suggest valid randomness values.

### Task 5: Replace the static `wav>` prompt with a dynamic colored `sd_edit>` prompt

1. In `src/main.c` at [src/main.c:1665-1689] and [src/main.c:1691-1794], refactor `redraw_prompt_line` / `read_interactive_line` so they no longer depend on the hard-coded prompt string passed from `run_interactive_console`.
2. Build the prompt from the current `Session` each time the line is redrawn:
   - no prefix at all when `session->has_current_preset == 0`;
   - otherwise print `<name>:` in green, `[stats]` in red, and `sd_edit>` in the default console color.
3. Use Win32 console attributes rather than raw ANSI escape sequences, because the existing redraw logic in [src/main.c:1674-1688] counts visible prompt length and would mis-measure escape codes.
4. Keep non-console fallback behavior in [src/main.c:1704-1717] readable by printing the same prompt text without colors when completion support is unavailable.
5. In `src/main.c` at [src/main.c:2486-2497], change the banner from `wav mini-console` to `sd_edit` and update the interactive loop so each prompt render pulls the fresh session state rather than always passing `"wav> "`.

### Task 6: Rename the executable and update all user-facing documentation

1. In `makefile` at [makefile:6-8], rename `OUT = main.exe` to `OUT = sd_edit.exe`, and keep the rest of the target graph aligned in [makefile:23-27] and [makefile:32-34].
2. In `src/main.c`, replace the remaining user-facing `main.exe` / `wav` strings already identified at [src/main.c:145], [src/main.c:1827-1845], [src/main.c:2277-2285], [src/main.c:2384], [src/main.c:2436-2469], and [src/main.c:2486-2497] so help text, errors, examples, and startup output all say `sd_edit` / `sd_edit.exe`.
3. In `README.md` at [README.md:38-60], [README.md:133-255], [README.md:297-360], and [README.md:385-458], rewrite the documented command set, prompt samples, example flows, direct-invocation examples, output naming examples, and session-model explanation so the README matches the new behavior exactly.

## 5. Acceptance Criteria

- Launching the built executable with no arguments opens an interactive prompt whose base label is `sd_edit>` rather than `wav>` or `main.exe` text, and the prompt shows no preset prefix when no preset is loaded.
- `do <file.wav>` fails with a clear error when run before `preset random [randomness]` or `preset load <name>` has populated the current session preset.
- `preset random` with no third token succeeds and behaves as `preset random 5`; `preset random 0` creates exactly 1 segment; `preset random 10` creates exactly 11 segments; `preset random 11` and `preset random abc` fail with usage/validation errors and do not overwrite the current preset.
- After `preset random 5`, the current preset is stored in session, printed to the console, and the next prompt includes a green preset name plus red summary stats before `sd_edit>`.
- `preset load <name>` loads the latest valid matching preset from `presets.txt` using the existing storage rules, stores it as the current session preset, prints it, and updates the prompt prefix to that preset name.
- `preset save <name>` appends the current preset to `presets.txt`; after a successful save, `preset load <name>` restores the same segment count and per-segment fields.
- `show preset` prints the current session preset and `show preset <name>` prints the stored preset named `<name>` without changing the current prompt prefix or current session preset.
- `do audio\test.wav` with an active preset writes a processed `.wav` file whose suffix is derived from the current preset display name, using the existing sanitize/collision logic in [src/main.c:1855-1987].
- `play <file.wav>`, `help`, `exit`, and `quit` continue to work under the renamed `sd_edit` executable.
- Tab completion in the interactive console suggests top-level commands, `.wav` paths after `do` / `play`, `random` / `load` / `save` after `preset`, named presets after `preset load`, `preset save`, and `show preset`, and `0..10` after `preset random`.
- Building through `make` produces `sd_edit.exe` instead of `main.exe`.

## 6. Verification Steps

1. Build the project with `make clean` and then `make`, and confirm that `sd_edit.exe` exists instead of `main.exe`.
2. Start the interactive console with `sd_edit.exe` and verify the startup banner and empty-session prompt.
3. At the prompt, run `help` and confirm that the documented commands are exactly:
   - `do <file.wav>`
   - `play <file.wav>`
   - `preset random [randomness]`
   - `preset load <name>`
   - `preset save <name>`
   - `show preset [name]`
   - `exit`
   - `quit`
4. Run `do audio\test.wav` before loading any preset and verify that it fails with the expected empty-session guidance.
5. Run `preset random`, then `show preset`, and verify that:
   - the preset prints correctly;
   - the prompt now contains the synthetic preset name and stats;
   - `do audio\test.wav` creates a processed output file.
6. Run `preset save demo`, `preset load demo`, and `show preset demo`, and verify that:
   - the save appends a new line to `presets.txt`;
   - the load restores the saved preset into session;
   - `show preset demo` prints the stored preset without changing whatever preset is already active.
7. Run `play audio\test.wav` and confirm that the bundled player still launches successfully.
8. Exercise interactive completion manually:
   - `pre<Tab>` → `preset`
   - `preset l<Tab>` → `preset load`
   - `preset random <Tab>` → suggests `0..10`
   - `show pre<Tab>` → `show preset`
   - `do audio\te<Tab>` → completes `.wav` paths from `audio\`
9. Run `sd_edit.exe help` as a direct invocation and verify that the new help text matches the interactive help.
10. Run `sd_edit.exe show preset demo` and verify that named preset lookup works in a fresh process without requiring a current session preset.

## 7. Risks & Mitigations

- Random preset generation currently depends on audio metadata and total frames in [src/preset.c:547-620], but the new command order requires random preset creation before a file is chosen.
  - Mitigation: make the generator audio-independent, document the default length strategy in `README.md`, and rely on the existing processing rule in [README.md:334-340] / [src/main.c:2144-2185] that truncates segments at EOF and leaves uncovered tail audio unchanged.

- Colored prompt rendering can break history/completion redraw if implemented with ANSI escape codes because [src/main.c:1674-1688] measures prompt length as plain string length.
  - Mitigation: print colors with Win32 console attributes and keep a separate plain-text prompt summary for redraw length accounting.

- The command grammar change is broad enough that stale completion/help text could survive even if the dispatcher is updated.
  - Mitigation: update the command catalog in [src/main.c:52-61], completion parsing in [src/main.c:494-505] and [src/main.c:989-1050], candidate collection in [src/main.c:1423-1487], usage output in [src/main.c:1822-1853], and README docs in [README.md:133-255] together.

- `show preset <name>` could accidentally replace the active preset if it reuses the same code path as `preset load <name>`.
  - Mitigation: load named presets into a temporary `Preset`, print them, free them, and never call the session update helper from [src/main.c:1989-2002] in the `show preset <name>` path.

- Renaming the executable to `sd_edit.exe` can leave inconsistent user-facing references across the code and docs.
  - Mitigation: sweep the exact current references already identified in `makefile`, `src/main.c`, and `README.md` and verify the renamed binary through the build artifact and help output.