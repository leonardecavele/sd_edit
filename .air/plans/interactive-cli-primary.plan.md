# Goal

Make the program behave and read like an interactive wav mini-console first: launching it should clearly lead into a command prompt workflow, `help` should describe that workflow completely, and the README should document the prompt-based usage as the primary interface without regressing the existing preset/audio pipeline.

Assumption: keep the current direct argv commands as compatibility shortcuts, because they already exist in `src/main.c` lines 832-885 and still match the README product objective. If you want an interactive-only binary instead, the same plan applies except Task 2 would remove that compatibility path instead of preserving it.

# Approach

The current implementation already has the core pieces needed for this change: a shared command dispatcher in `src/main.c` lines 707-778, an interactive loop in lines 780-829, and session-local preset state in lines 21-47. The pragmatic path is to keep those working audio/preset handlers intact, tighten the REPL parsing and help surface around them, and then rewrite the README sections that still frame the tool as command-line-first rather than prompt-first.

This keeps the change localized to the command surface instead of touching `src/preset.c` or `src/effects.c`, which already provide the preset model, persistence, and effect catalog the interactive shell depends on.

# File Changes

- Modify `src/main.c` (lines 139-196, 224-246, 707-885)
  - Responsibility today: command tokenization, help output, command dispatch, interactive loop, and startup path.
  - Planned change: make the REPL-facing command parsing more robust, ensure `help` describes all prompt-usable commands, and keep interactive/default startup behavior and optional argv compatibility consistent.

- Modify `README.md` (lines 36-56, 76-109, 111-199, 324-406)
  - Responsibility today: product objective, build/run instructions, command reference, interactive-mode notes, and architecture notes.
  - Planned change: rewrite usage/docs so the first-class workflow is `start program -> get prompt -> run commands`, explicitly document `help`, and align the narrative with the actual session-state behavior already implemented.

- No changes planned for `src/preset.c` (lines 218-490), `include/preset.h` (lines 6-26), `src/effects.c` (lines 15-396), or `include/effects.h` (lines 6-32)
  - Reason: the preset model, named-preset persistence, random-preset generation, and centralized effect catalog already support the requested interactive CLI; the requested work is on the shell/help/docs surface.

# Implementation Steps

## Task 1: Align the command/help surface in `src/main.c`

1. Update `print_usage()` in `src/main.c` lines 224-246 so the output reads as an interactive command reference, not only as `main.exe ...` examples.
   - Add explicit entries for `help`, `show preset`, `exit`, and `quit`.
   - Keep the existing `do`, `play`, `preset`, and `save preset` usage strings, but phrase them as commands available at the prompt.
   - Keep the effect catalog listing from `get_effect_catalog()` so help remains the single runtime source of truth for effect IDs.

2. Keep `execute_tokens()` in `src/main.c` lines 707-778 as the shared dispatcher for both prompt commands and any retained argv shortcuts.
   - Ensure unknown-command handling routes through the same updated help text.
   - Preserve the current handlers for `do`, `play`, `preset`, `save preset`, and `show preset` so the processing path remains unchanged.

## Task 2: Harden the interactive parser and startup flow in `src/main.c`

3. Refine the interactive parsing path in `src/main.c` lines 780-829 so `preset` handling is case-insensitive and preserves the full remainder of the input line as the definition.
   - Today, the special-case branch uses `strncmp(command, "preset ", 7) == 0` at lines 814-818, which only matches lowercase `preset` and can diverge from the otherwise case-insensitive command handling.
   - Replace that special case with first-command detection that matches the existing `equals_ignore_case()` behavior and passes the untouched remainder of the line to `handle_preset_command()`.
   - Keep quoted-path tokenization for other commands by leaving `split_command_line()` in lines 139-196 as the general path for non-`preset` commands.

4. Simplify `run_cli()` / startup flow in `src/main.c` lines 832-896 so the interactive entry path stays explicit and consistent with the updated help/docs.
   - Keep the no-argument behavior delegating to `run_interactive_console()`.
   - If argv compatibility is retained, isolate it as a shortcut path that still reuses `execute_tokens()` and the same help text.
   - If interactive-only is chosen later, this is the place to remove direct argv dispatch cleanly without touching the audio/preset handlers.

## Task 3: Rewrite the README around the interactive workflow

5. Update the implementation summary in `README.md` lines 36-56 to make the interactive console and `help` command part of the primary interface description, not an add-on note.
   - Keep the product objective intact.
   - Remove any implication that one-shot argv commands are the main operating mode if the implementation keeps the REPL-first UX.

6. Rewrite build/run and usage sections in `README.md` lines 76-109 and 111-199 so the first documented path is launching the program and entering commands at the prompt.
   - Show `make run` and/or `main.exe` as the startup step.
   - Include prompt-level examples for `help`, `do audio\\test.wav`, `preset ...`, `save preset <name>`, `show preset`, and `exit`.
   - If argv shortcuts remain supported, move them into a secondary “direct invocation” or “shortcut mode” subsection rather than the main usage flow.

7. Update architecture/session-state notes in `README.md` lines 324-406 so they explicitly explain why interactive sessions matter for `current_preset`.
   - Keep the existing explanation that preset state is process-local.
   - Tie `save preset <name>` and `show preset` to that session model.
   - Ensure the README’s command list matches the final `print_usage()` output closely enough that future drift is obvious.

# Acceptance Criteria

- Launching the program with no arguments still opens the prompt and continues accepting commands until `exit` or `quit` is entered.
- Entering `help` at the prompt prints all supported prompt commands: `do`, `play`, `preset`, `save preset`, `show preset`, `help`, `exit`, and `quit`.
- Interactive `preset` input accepts the entire definition after the command name, including definitions containing internal spaces such as `PRESET 1,120,50,44100; 5,8000,40,44100`.
- Existing handlers for `.wav` validation, preset persistence, random preset generation, and effect application remain unchanged in behavior.
- The README’s primary usage path shows an interactive session, not only one-shot command examples.
- If argv compatibility is retained, `main.exe help` and `main.exe do audio\\test.wav` still work and are documented as shortcuts rather than the main workflow.

# Verification Steps

1. Build the project with `make`.
2. Start the interactive shell with `make run` (or `main.exe` with the libsndfile runtime on `PATH`).
3. Manual prompt checks:
   - `help`
   - `show preset`
   - `preset 1,120,50,44100`
   - `PRESET 1,120,50,44100; 5,8000,40,44100`
   - `save preset demo`
   - `do audio\\test.wav demo`
   - `do audio\\test.wav`
   - `exit`
4. If argv compatibility is kept, also verify:
   - `main.exe help`
   - `main.exe do audio\\test.wav`
5. Confirm that the generated output files still use the existing suffix logic from `build_output_path()` in `src/main.c` lines 297-332 and that README examples match observed runtime behavior.

# Risks & Mitigations

- Risk: adjusting the REPL parser can break edge cases for quoted file paths or existing command splitting because both interactive and argv paths converge in `execute_tokens()` (`src/main.c` lines 707-778).
  - Mitigation: keep the shared handlers untouched and limit parser changes to how the REPL extracts the command word plus raw remainder for `preset`.

- Risk: `help` output and README content can drift again after the change, especially because the README is intentionally detailed (`README.md` lines 111-199 and 324-406).
  - Mitigation: base the README command list and examples directly on the final `print_usage()` output and the actual interactive commands supported by `execute_tokens()`.

- Risk: removing argv execution later would be a behavior change for anyone using direct invocation.
  - Mitigation: treat interactive-only mode as a deliberate follow-up scope choice centered in `run_cli()` (`src/main.c` lines 832-885), not as an incidental cleanup mixed into parser/help updates.