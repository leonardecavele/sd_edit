## 1. Goal
Make the Windows `make` build compile the complete wav tool successfully and stop the default build from auto-running the interactive executable.

## 2. Approach
The build break is in the build script, not the current CLI source. [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) line 6 only includes `src/main.c` and `src/effects.c`, while [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A) calls preset functions that are implemented in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&root=C%3A). The first fix is therefore to add the preset translation unit to the build graph and keep `clean` in sync; then simplify the default target so `make` builds only, which avoids blocking on the no-argument interactive mode in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A) around lines 812-845.

## 3. File Changes
- Modify [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A)
  - Current issue at lines 6-7: `SRC`/`OBJ` omit `src/preset.c`, so the link step at lines 23-24 cannot resolve preset symbols referenced from [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A) lines 34, 45, 341, 550, 588, 625, and 637.
  - Current issue at lines 20-21: the `all` target runs `main.exe` immediately after linking, which triggers interactive mode from [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A) when no CLI arguments are provided.
  - Current issue at lines 29-32: `clean` only deletes two object files and will be stale once `src/preset.c` is part of the build.

## 4. Implementation Steps
### Task 1: Fix the build graph
1. Update [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) line 6 so `SRC` includes `src/preset.c` alongside `src/main.c` and `src/effects.c`.
2. Keep [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) line 7 object derivation intact so the new `src/preset.o` is linked automatically at lines 23-24.
3. Verify the preset dependency rationale against [preset.h](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/include/preset.h?type=file&root=C%3A) lines 20-26 and the implementations in [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&root=C%3A) lines 166-488.

### Task 2: Make the default target build-only
1. Replace the `all` recipe in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) lines 20-21 so `make` stops after producing `main.exe`.
2. If runtime PATH setup is still needed for manual execution, move that behavior into an explicit `run` target in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) rather than coupling it to `all`.
3. Preserve the current bundled libsndfile path variables in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) lines 12-18 unless the next verification step proves MinGW cannot consume `sndfile.lib`.

### Task 3: Keep cleanup and verification aligned
1. Update [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) lines 29-32 so `clean` removes all generated objects, including `src/preset.o`.
2. Prefer deriving cleanup from `$(OBJ)` instead of hard-coding two object paths, so the build script stays correct as sources change.
3. After the build script is corrected, run `make clean` and `make`; if the next failure is a libsndfile link error from [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) line 18, adjust only the library flag or documented toolchain expectation rather than changing audio code.

## 5. Acceptance Criteria
- `make` compiles and links `main.exe` without undefined-reference errors for preset functions declared in [preset.h](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/include/preset.h?type=file&root=C%3A) lines 20-26.
- `make` does not launch the program automatically; it exits after a successful build.
- `make clean` removes every object generated from the source list in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) lines 6-7.
- Manual execution of the built binary still works with the bundled libsndfile DLL on a test file such as [test.wav](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/audio/test.wav?type=file&root=C%3A).

## 6. Verification Steps
1. Run `make clean` from the repository root.
2. Run `make` and confirm the link step includes the object built from [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&root=C%3A).
3. Run `main.exe do audio/test.wav` and confirm it creates a processed wav output next to [test.wav](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/audio/test.wav?type=file&root=C%3A).
4. Run `main.exe preset 1,120,50,44100` followed by `main.exe save preset p1` and then `main.exe do audio/test.wav p1` to validate the preset path that depends on [preset.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/preset.c?type=file&root=C%3A).
5. If `make` fails at the libsndfile stage after the preset object is added, capture the exact linker message and reconcile it with the bundled library packaging in [sndfile.lib](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/lib/sndfile.lib?type=file&root=C%3A).

## 7. Risks & Mitigations
- Risk: after fixing the missing preset object, MinGW `gcc` may reject the bundled [sndfile.lib](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/lib/sndfile.lib?type=file&root=C%3A) import library. Mitigation: validate the exact linker error after Task 1 and, if needed, adjust only the link flag or documented compiler expectation in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A).
- Risk: keeping runtime execution inside `all` masks successful builds by dropping into the interactive console from [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A). Mitigation: isolate runtime behavior in a separate target.
- Risk: hard-coded cleanup in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) will regress again as sources are added. Mitigation: derive cleanup from `$(OBJ)` rather than enumerating file names manually.