## 1. Goal

Make random presets sample-aware and deterministic: the saved preset becomes a small recipe of constants, and the concrete segment count/effect sequence is generated from both `randomness` and the input sample length when `do <file.wav>` runs.

## 2. Approach

The current contract in [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A3506%2C%22second%22%3A3881%7D%2C%22lines%22%3A%7B%22first%22%3A181%2C%22second%22%3A192%7D%7D&root=C%3A) and the implementation in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A10664%2C%22second%22%3A11726%7D%2C%22lines%22%3A%7B%22first%22%3A551%2C%22second%22%3A591%7D%7D&root=C%3A) hard-code `randomness + 1` segments before any audio is loaded, so they cannot satisfy duration-based scaling. The lowest-risk change is to keep `Segment` and `Preset` as the transient execution model from [preset.h](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/include/preset.h?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A57%2C%22second%22%3A761%7D%2C%22lines%22%3A%7B%22first%22%3A5%2C%22second%22%3A27%7D%7D&root=C%3A), but add a small persisted recipe type such as `PresetRecipe { randomness, seed }` and materialize the real segments only after [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A) has `SF_INFO.frames` and `SF_INFO.channels`. That preserves the existing effect-application loop while matching your requested model: presets are deterministic constants, not pre-baked sequential segment lists.

## 3. File Changes

- Modify [preset.h](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/include/preset.h?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A57%2C%22second%22%3A761%7D%2C%22lines%22%3A%7B%22first%22%3A5%2C%22second%22%3A27%7D%7D&root=C%3A) (lines 6-28): keep `Segment` and `Preset` for materialized execution, add a new persisted recipe struct and replace the direct random-preset API with recipe generation/materialization/save/load declarations.

- Modify [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A142%2C%22second%22%3A528%7D%2C%22lines%22%3A%7B%22first%22%3A10%2C%22second%22%3A19%7D%7D&root=C%3A) and [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A10664%2C%22second%22%3A11726%7D%2C%22lines%22%3A%7B%22first%22%3A551%2C%22second%22%3A591%7D%7D&root=C%3A) (lines 11-20 and 552-592): remove the fixed length-independent segment-count logic, add a local deterministic PRNG, add the duration/randomness curve helpers, materialize concrete segments from sample length, and switch preset persistence to the new recipe format.

- Modify [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A1499%2C%22second%22%3A1628%7D%2C%22lines%22%3A%7B%22first%22%3A50%2C%22second%22%3A55%7D%7D&root=C%3A), [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A41098%2C%22second%22%3A41681%7D%2C%22lines%22%3A%7B%22first%22%3A1745%2C%22second%22%3A1768%7D%7D&root=C%3A), [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A46444%2C%22second%22%3A47548%7D%2C%22lines%22%3A%7B%22first%22%3A1957%2C%22second%22%3A1999%7D%7D&root=C%3A), and [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A) (lines 51-56, 1746-1769, 1958-2000, 2327-2469): store an active recipe in session state, update prompt/show output so it describes recipe metadata truthfully, and move preset materialization into the `do` flow after audio metadata is loaded.

- Modify [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A3506%2C%22second%22%3A3881%7D%2C%22lines%22%3A%7B%22first%22%3A181%2C%22second%22%3A192%7D%7D&root=C%3A), [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A4827%2C%22second%22%3A5242%7D%2C%22lines%22%3A%7B%22first%22%3A236%2C%22second%22%3A247%7D%7D&root=C%3A), and [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A7000%2C%22second%22%3A8321%7D%2C%22lines%22%3A%7B%22first%22%3A309%2C%22second%22%3A356%7D%7D&root=C%3A) (lines 182-193, 237-248, 310-357): replace the old `randomness + 1` rules, explain that `do` materializes from sample length, document the new recipe persistence format, and add concrete examples for the new curve.

- Modify [presets.txt](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/presets.txt?type=file&root=C%3A): remove the legacy stored segment-list example so the repository no longer ships data that the new loader will reject.

## 4. Implementation Steps

### Task 1: Introduce a deterministic recipe model

1. In [preset.h](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/include/preset.h?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A57%2C%22second%22%3A761%7D%2C%22lines%22%3A%7B%22first%22%3A5%2C%22second%22%3A27%7D%7D&root=C%3A), add `PresetRecipe` with the minimal deterministic constants needed for replay, starting with `randomness` and `seed`, and keep `Preset` as the concrete segment array used only during processing.

2. In [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A10664%2C%22second%22%3A11726%7D%2C%22lines%22%3A%7B%22first%22%3A551%2C%22second%22%3A591%7D%7D&root=C%3A), replace `generate_random_preset` with two explicit stages: `generate_random_recipe` for creating a saved recipe and `materialize_preset_from_recipe` for deriving a concrete segment array once the file length is known.

3. Still in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A142%2C%22second%22%3A528%7D%2C%22lines%22%3A%7B%22first%22%3A10%2C%22second%22%3A19%7D%7D&root=C%3A), add a local deterministic PRNG helper so recipe replay does not depend on the process-global `srand(time(NULL))` from [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A).

### Task 2: Implement the sample-length-aware segment-count curve

4. In [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A10664%2C%22second%22%3A11726%7D%2C%22lines%22%3A%7B%22first%22%3A551%2C%22second%22%3A591%7D%7D&root=C%3A), centralize a helper that converts `frames` and `sample_rate` into duration seconds, then maps `duration_seconds + randomness` to a base density curve. The implementation should be tuned so the low end matches your examples (`~2s @ randomness 0 => 1 segment`, `~5s @ randomness 0 => 2 segments`) and the high end lands around `40..60` segments for `~30s @ randomness 10`.

5. Build a per-randomness fluctuation band on top of that base count: compute a deterministic `[min_segments, max_segments]` window from the curve, collapse the window for very small counts so the short-file cases do not oscillate, then pick the concrete count with the recipe-local PRNG.

6. Materialize segment lengths from total input frames instead of the old `11025..88200` placeholder lengths from [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A7000%2C%22second%22%3A8321%7D%2C%22lines%22%3A%7B%22first%22%3A309%2C%22second%22%3A356%7D%7D&root=C%3A): split `info.frames` across the computed segment count with deterministic jitter, keep each `segment.length` positive, and normalize the remainder so the materialized preset covers the full file.

7. Continue generating effect IDs, parameters, and mixes in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A10664%2C%22second%22%3A11726%7D%2C%22lines%22%3A%7B%22first%22%3A551%2C%22second%22%3A591%7D%7D&root=C%3A), but drive them from the recipe-local PRNG so the same recipe always yields the same sequence for the same sample duration.

### Task 3: Rewire the CLI to use recipes

8. Replace the session’s concrete-preset ownership in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A1499%2C%22second%22%3A1628%7D%2C%22lines%22%3A%7B%22first%22%3A50%2C%22second%22%3A55%7D%7D&root=C%3A) with recipe-centric state. If display context is still needed, store lightweight metadata such as recipe name, randomness, and seed rather than a fake precomputed segment list.

9. Update the prompt formatter in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A41098%2C%22second%22%3A41681%7D%2C%22lines%22%3A%7B%22first%22%3A1745%2C%22second%22%3A1768%7D%7D&root=C%3A) and the preset printer in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A46444%2C%22second%22%3A47548%7D%2C%22lines%22%3A%7B%22first%22%3A1957%2C%22second%22%3A1999%7D%7D&root=C%3A) so `show preset` reports recipe constants, and `do` reports the concrete materialized segment count after generation.

10. Refactor `process_audio_file`, `handle_do_command`, and `handle_preset_random_command` inside [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A) so the flow becomes: validate recipe -> load audio -> materialize concrete `Preset` from `SF_INFO` -> apply effects -> save output -> free the temporary `Preset`.

11. Switch `preset save`, `preset load`, and `show preset <name>` in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A) to the new recipe parser/serializer so saved presets round-trip as deterministic constants.

### Task 4: Update the spec and bundled fixture

12. Rewrite the command and persistence sections in [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A3506%2C%22second%22%3A3881%7D%2C%22lines%22%3A%7B%22first%22%3A181%2C%22second%22%3A192%7D%7D&root=C%3A), [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A4827%2C%22second%22%3A5242%7D%2C%22lines%22%3A%7B%22first%22%3A236%2C%22second%22%3A247%7D%7D&root=C%3A), and [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A7000%2C%22second%22%3A8321%7D%2C%22lines%22%3A%7B%22first%22%3A309%2C%22second%22%3A356%7D%7D&root=C%3A) to match the new model: recipes are saved, concrete segments are derived at `do` time, and the segment-count curve depends on both duration and randomness.

13. Replace the legacy example line in [presets.txt](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/presets.txt?type=file&root=C%3A) with either an empty file or a single recipe-format example such as `name|randomness|seed`, so manual local tests do not start with stale data.

## 5. Acceptance Criteria

- `preset random 0` no longer creates a persisted concrete segment list; it creates a deterministic recipe containing at least `randomness` and a saved seed, and `show preset` reports recipe metadata rather than a fake fixed segment array.

- Running `preset random 0` followed by `do audio\test.wav` on [test.wav](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/audio/test.wav?type=file&root=C%3A) (~4.894 s at 44.1 kHz) materializes exactly 2 segments.

- Running the same `random_0` recipe on a ~2-second `.wav` materializes exactly 1 segment.

- Running a `random_10` recipe on a ~30-second `.wav` materializes a concrete segment count inside the `40..60` band, not a fixed `11`-segment cap.

- Saving a recipe with `preset save <name>` and reloading it with `preset load <name>` preserves the exact `randomness` and `seed`; applying the reloaded recipe to the same input file produces the same segment count, segment definitions, and output audio bytes.

- Materialized `segment.length` values sum to the input frame count, so the generated preset covers the full sample duration instead of leaving arbitrary tail audio untouched.

- The new loader expects only the new recipe format in [presets.txt](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/presets.txt?type=file&root=C%3A); legacy segment-list lines are no longer considered valid persisted presets.

## 6. Verification Steps

1. Build with `make clean all` using the existing targets from [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A379%2C%22second%22%3A557%7D%2C%22lines%22%3A%7B%22first%22%3A18%2C%22second%22%3A29%7D%7D&root=C%3A).

2. Start the interactive prompt with `make run`, then execute:
   - `preset random 0`
   - `show preset`
   - `do audio\test.wav`

   Verify that the preset display shows recipe constants and that the materialization report for [test.wav](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/audio/test.wav?type=file&root=C%3A) prints 2 segments.

3. Still in the prompt, run:
   - `preset save low`
   - `preset load low`
   - `do audio\test.wav`

   Compare the two generated outputs with [sndfile-cmp.exe](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/bin/sndfile-cmp.exe?type=file&root=C%3A) to confirm deterministic replay of the same saved recipe.

4. Create a longer verification fixture from the bundled sample using [sndfile-concat.exe](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/bin/sndfile-concat.exe?type=file&root=C%3A), for example:

   `libsndfile-1.2.2-win64\bin\sndfile-concat.exe audio\test.wav audio\test.wav audio\test.wav audio\test.wav audio\test.wav audio\test.wav audio\test_30s.wav`

   Then run `preset random 10` and `do audio\test_30s.wav`, and verify the printed segment count is between 40 and 60.

5. Edge-case checks:
   - a very short input still materializes at least 1 segment;
   - segment lengths stay positive after rounding;
   - repeated `do` calls with the same saved recipe and same source file remain byte-identical;
   - loading an old `name|segment_count|definition` line from [presets.txt](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/presets.txt?type=file&root=C%3A) is rejected or skipped cleanly.

## 7. Risks & Mitigations

- Format breakage: the repository currently ships an old segment-list line in [presets.txt](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/presets.txt?type=file&root=C%3A). Mitigation: switch the parser to the recipe format only, refresh the bundled file, and document the break clearly in [README.md](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/README.md?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A7000%2C%22second%22%3A8321%7D%2C%22lines%22%3A%7B%22first%22%3A309%2C%22second%22%3A356%7D%7D&root=C%3A).

- False determinism: if materialization keeps using the process-global `rand()`, a saved recipe will not replay reliably. Mitigation: isolate recipe generation behind a local PRNG in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A142%2C%22second%22%3A528%7D%2C%22lines%22%3A%7B%22first%22%3A10%2C%22second%22%3A19%7D%7D&root=C%3A) and use the global RNG only when first minting a new seed.

- UI truthfulness: the current prompt and `show preset` code in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A41098%2C%22second%22%3A41681%7D%2C%22lines%22%3A%7B%22first%22%3A1745%2C%22second%22%3A1768%7D%7D&root=C%3A) and [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A46444%2C%22second%22%3A47548%7D%2C%22lines%22%3A%7B%22first%22%3A1957%2C%22second%22%3A1999%7D%7D&root=C%3A) assume a concrete preset already exists. Mitigation: make those displays recipe-centric and print the actual materialized segment count only after `do` has audio length context.

- Rounding errors in duration coverage: naive frame splitting can create zero-length segments or leave unassigned frames. Mitigation: partition total frames with minimum-length guards and explicit remainder redistribution before passing the result into `apply_preset_to_samples` in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A55682%2C%22second%22%3A59377%7D%2C%22lines%22%3A%7B%22first%22%3A2326%2C%22second%22%3A2468%7D%7D&root=C%3A).