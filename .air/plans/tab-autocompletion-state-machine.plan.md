# 1. Goal

Add interactive Tab autocompletion to the prompt so the first token completes command words and later tokens complete `preset` subcommands, saved preset names, or `.wav` paths depending on command context, without changing direct argv execution.

# 2. Approach

Implement the feature inside the existing Windows prompt path in `src/main.c:1160-1210`, replacing the `fgets`-only reader with a small character-by-character line editor that can intercept Tab. Drive completions from an explicit parse-state enum derived from the current buffer and reuse preset-store knowledge from `src/preset.c:321-415` rather than reparsing `presets.txt` ad hoc in `main.c`.

Avoid external libraries such as readline; the repo is already Windows-specific (`windows.h`, `_spawnl`) and the current build in `makefile:1-36` has no dependency hook for them.

# 3. File Changes

- `src/main.c` (Modify; around lines 1-10, 340-477, 505-535, 1160-1210)
  - add console-input includes/checks (`conio.h` / `io.h` or Win32 console mode detection)
  - define completion state machine and token parse structs
  - add candidate collection and line redraw helpers
  - replace interactive `fgets` loop with completion-aware reader
  - optionally mention Tab behavior in `print_usage()`
- `include/preset.h` (Modify; lines 20-26)
  - expose preset-name enumeration/free helpers for autocomplete
- `src/preset.c` (Modify; around lines 17-67 and 321-415)
  - add preset-store scan utilities and public preset-name listing that honors existing validation
- `README.md` (Modify; around lines 145-153, 155-197, 199-210, 286-312)
  - document Tab completion behavior, contexts, and duplicate-name behavior when suggestions are sourced from `presets.txt`

# 4. Implementation Steps

## Task 1: Add preset-name discovery in the preset module

- In `include/preset.h:20-26`, declare `list_named_presets(...)` and a matching free helper so the console can request current preset names without understanding the storage format.
- In `src/preset.c:321-415`, factor the existing load scan into reusable logic that:
  - reads `presets.txt` line by line,
  - rejects malformed or invalid definitions using the same `parse_preset_definition()` path already used by `load_named_preset()`,
  - keeps one entry per preset name with latest-valid-wins semantics matching `README.md:286-312`,
  - returns a dynamically allocated string list suitable for prefix matching.
- Keep `save_named_preset()` and `load_named_preset()` behavior unchanged from `src/preset.c:274-415`; the new API is read-only support for completion.

## Task 2: Add a completion parse state machine

- In `src/main.c:340-477`, introduce a small parse result struct that records:
  - normalized token count,
  - whether the buffer ends with whitespace,
  - current token start/end,
  - active quote character if the user is editing inside quotes,
  - resolved completion context.
- Define an enum in `src/main.c` for contexts such as:
  - `COMPLETE_COMMAND`
  - `COMPLETE_DO_FILE`
  - `COMPLETE_DO_PRESET`
  - `COMPLETE_PLAY_FILE`
  - `COMPLETE_SAVE_SUBCOMMAND`
  - `COMPLETE_SAVE_PRESET_NAME`
  - `COMPLETE_SHOW_SUBCOMMAND`
  - `COMPLETE_NONE`
  - `COMPLETE_INVALID`
- Mirror the existing quote behavior from `split_command_line()` in `src/main.c:420-477` so completion and execution interpret spaces and quotes the same way.
- Treat `preset <definition>` as `COMPLETE_NONE`; the definition grammar is free-form numeric text and does not benefit from candidate suggestion.

## Task 3: Build context-specific candidate providers

- In `src/main.c:401-597`, add:
  - a static command catalog for `help`, `do`, `play`, `preset`, `save`, `show`, `exit`, and `quit` to keep completion aligned with `print_usage()` in `src/main.c:505-535` and `execute_tokens()` in `src/main.c:1087-1157`,
  - a `save/show` subcommand provider that only offers `preset`,
  - a preset-name provider backed by the new `list_named_presets(PRESET_STORE_PATH, ...)`,
  - a `.wav` path provider that enumerates matching filesystem entries with `FindFirstFileA` and `FindNextFileA`, filters final results to `.wav`, and keeps directories as drill-down candidates with a trailing separator.
- Make all prefix matching case-insensitive to stay consistent with existing command comparisons in `src/main.c:340-399` and with normal Windows filesystem behavior.
- Preserve arbitrary user input: if no candidates match, leave the buffer unchanged.

## Task 4: Replace the prompt reader while keeping command execution intact

- In `src/main.c:1160-1210`, replace the `fgets` loop with a `read_interactive_line()` helper that reads one key at a time, bounded by `MAX_COMMAND_LENGTH` from `src/main.c:18`.
- Support the minimum editing set needed for a usable prompt:
  - printable characters insert into the buffer,
  - Backspace deletes the previous character,
  - Enter finalizes the line,
  - Tab invokes completion,
  - any non-Tab edit resets the current completion cycle.
- Keep a small completion session cache in `src/main.c` so repeated Tab on the same prefix and context cycles through the current match set instead of recomputing unrelated matches each time.
- On a single match, replace the active token and append a trailing space for terminal tokens (`do`, `play`, preset names, command words). For directory matches, append `\\` and keep the cursor in the same token.
- On multiple matches, first extend to the longest common prefix; if the user presses Tab again without changing the buffer, cycle through concrete matches in place.
- Leave `execute_tokens()` (`src/main.c:1087-1157`) and `run_direct_command_shortcut()` (`src/main.c:1212-1274`) unchanged so only interactive input behavior changes, not the dispatcher itself.

## Task 5: Preserve non-console behavior and document the feature

- Add a console or TTY check in `src/main.c:1160-1210`; if stdin is not a real console, fall back to the current `fgets` path so redirected input does not regress.
- Update `print_usage()` in `src/main.c:505-535` to mention Tab completion in prompt mode.
- Update `README.md:145-210` to explain what Tab completes:
  - commands in the first position,
  - `.wav` paths after `do` and `play`,
  - saved preset names after `do <file.wav> ` and optionally after `save preset `,
  - `preset` after `save ` and `show `.

# 5. Acceptance Criteria

- In prompt mode (`main.exe` or `make run` per `README.md:73-90`), pressing Tab on the first token autocompletes or cycles through the command words implemented in `src/main.c:1094-1155`.
- `sa<Tab>` resolves to `save ` and `show <Tab>` / `save <Tab>` resolve the second token to `preset`.
- After `do ` or `play `, Tab only suggests filesystem matches that can lead to `.wav` inputs; non-`.wav` files are never inserted as final completions.
- With the repository’s current sample file in `audio/test.wav`, `do a<Tab>` or `play a<Tab>` can complete to the sample `.wav` path from the workspace.
- After `do audio\\test.wav `, Tab suggests saved preset names from `presets.txt`; with the current repository data (`p1|1|1,120,50,44100` in `presets.txt:1`), `p<Tab>` resolves to `p1`.
- Invalid or duplicate preset lines in `presets.txt` do not produce misleading suggestions; the suggestion list follows the same validation and latest-valid-wins rules as `load_named_preset()` in `src/preset.c:321-415`.
- `preset <effect,parameter,mix,length;...>`, `do <file.wav>`, `do <file.wav> <preset_name>`, `play <file.wav>`, `save preset <name>`, `show preset`, `exit`, and `quit` still execute through the existing dispatcher with the same semantics as before.
- Direct argv shortcuts such as `main.exe do audio\\test.wav` and `main.exe preset "1,120,50,44100"` still work unchanged because the autocompletion logic is limited to the interactive prompt path.

# 6. Verification Steps

- Build the project with `make` as documented in `README.md:71-77`.
- Launch prompt mode with `make run` or `main.exe` as documented in `README.md:79-90` and `README.md:131-143`.
- Manual prompt checks:
  1. Type `d` then Tab; confirm the line becomes `do `.
  2. Type `pl` then Tab; confirm the line becomes `play `.
  3. Type `save ` then Tab; confirm the line becomes `save preset `.
  4. Type `show ` then Tab; confirm the line becomes `show preset `.
  5. Type `do a` then Tab; confirm the line completes toward `audio\\test.wav`.
  6. Type `play a` then Tab; confirm the same `.wav` completion behavior.
  7. Type `do audio\\test.wav p` then Tab; confirm `p1` completes from `presets.txt`.
  8. Run `preset 1,120,50,44100`, then `save preset demo`, then start `do audio\\test.wav ` and press Tab repeatedly; confirm preset completion now includes both `demo` and `p1`, and repeated Tab cycles them.
  9. Press Enter on `do audio\\test.wav p1`; confirm the named-preset execution path still runs.
  10. Press Enter on `do audio\\test.wav`; confirm the random-preset execution path still runs.
- If a `.wav` file in a directory with spaces is available, manually test a quoted path prefix such as `do "path with spaces\\t` plus Tab to confirm the parser does not break quoted token handling inherited from `split_command_line()`.

# 7. Risks & Mitigations

- Risk: replacing `fgets` in `src/main.c:1160-1210` with `_getch()`-style input can break redirected stdin or non-console launches.
  - Mitigation: gate the custom editor behind a console or TTY detection check and retain the current `fgets` reader as fallback.
- Risk: naive preset-name completion could disagree with actual load behavior when `presets.txt` contains duplicates or invalid lines.
  - Mitigation: implement name enumeration in `src/preset.c:321-415`, reusing the same parsing and validation rules as the loader instead of scanning in `main.c`.
- Risk: Windows path completion is easy to get wrong around directories, separators, and quoted tokens.
  - Mitigation: keep completion token parsing quote-aware, treat directories and final `.wav` files separately, and centralize replacement and redraw logic in one helper rather than scattering string surgery through `run_interactive_console()`.
- Risk: `src/main.c` is already large (`1277+` lines before this change), so bolting raw completion logic directly into the loop can make it harder to maintain.
  - Mitigation: group the new code behind a small set of static structs and functions near the existing tokenizer helpers in `src/main.c:340-477` and keep `execute_tokens()` untouched as the single command executor.
