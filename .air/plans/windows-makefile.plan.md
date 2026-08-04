## 1. Goal

Make [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) runnable from a native Windows GNU Make environment by removing Unix-only shell assumptions, wiring libsndfile’s DLL directory into the Windows process PATH, and preserving the current default build/run workflow.

## 2. Approach

Replace the bash-specific path conversion and runtime recipes in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A138%2C%22second%22%3A399%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A16%7D%7D&root=C%3A) with Windows-native GNU Make recipes that explicitly use `cmd.exe`, quoted `PATH` mutation, and Windows path normalization via make functions instead of `sed`. Keep the existing compile/link flow and executable name unchanged so the change stays limited to build orchestration, while ensuring the runtime still loads [sndfile.dll](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/bin/sndfile.dll?type=file&root=C%3A) and exercises the current hard-coded audio pipeline in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A133%2C%22second%22%3A521%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A19%7D%7D&root=C%3A).

## 3. File Changes

- **Modify** [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A)
  - Replace the Unix-specific drive/path conversion and default run recipe currently in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A138%2C%22second%22%3A399%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A16%7D%7D&root=C%3A) with Windows-native variables and recipes.
  - Keep the existing compile/link rules in the same file, but run them under an explicit Windows shell so `gcc` is invoked without requiring a POSIX shell.
  - Replace the Unix cleanup rule in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A488%2C%22second%22%3A515%7D%2C%22lines%22%3A%7B%22first%22%3A24%2C%22second%22%3A25%7D%7D&root=C%3A) with Windows-safe deletion logic that tolerates missing files.

- **No code changes** to [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A)
  - The makefile changes should continue to launch the existing hard-coded `audio/test.wav` → `audio/modified.wav` processing path shown in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A133%2C%22second%22%3A521%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A19%7D%7D&root=C%3A).

## 4. Implementation Steps

### Task 1: Replace Unix-only shell assumptions in the build script

1. In [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A), add explicit Windows shell settings near the top (`SHELL := cmd.exe` and matching shell flags) so recipes run under a native Windows shell instead of relying on a POSIX-compatible environment.
2. In [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A138%2C%22second%22%3A399%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A16%7D%7D&root=C%3A), replace `PROJECT_DIR := $(shell ... sed ...)` with make-native Windows path variables such as a normalized `CURDIR_WIN` and a derived DLL/bin directory variable.
3. Rework the default execution recipe in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A138%2C%22second%22%3A399%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A16%7D%7D&root=C%3A) so it uses Windows syntax like `set "PATH=...;%PATH%" && main.exe`, preserving the current default behavior of building and then launching the tool.
4. Keep the existing object compilation and link targets in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) intact unless quoting is needed for Windows shell parsing.

### Task 2: Make cleanup Windows-safe

5. Add a Windows-safe cleanup strategy in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A488%2C%22second%22%3A515%7D%2C%22lines%22%3A%7B%22first%22%3A24%2C%22second%22%3A25%7D%7D&root=C%3A) that deletes `main.exe`, `src/main.o`, and `src/effects.o` using Windows commands and does not fail if one or more artifacts are already absent.
6. Normalize any object-file references needed by `clean` inside [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) so Windows deletion commands do not misinterpret forward slashes in file paths.

## 5. Acceptance Criteria

- Running the default target from Windows GNU Make no longer depends on `sed`, `export`, `./binary`, or `rm` anywhere in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A).
- `make main.exe` or `mingw32-make main.exe` from the repository root produces `main.exe` by compiling [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&root=C%3A) and [effects.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/effects.c?type=file&root=C%3A) with the bundled libsndfile headers and libraries.
- `make` or `mingw32-make` from Windows builds and launches `main.exe` successfully, with [sndfile.dll](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/libsndfile-1.2.2-win64/bin/sndfile.dll?type=file&root=C%3A) available through the process PATH so the program can open `audio/test.wav` and write `audio/modified.wav` as defined in [main.c](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/src/main.c?type=file&linesData=%7B%22range%22%3A%7B%22first%22%3A133%2C%22second%22%3A521%7D%2C%22lines%22%3A%7B%22first%22%3A9%2C%22second%22%3A19%7D%7D&root=C%3A).
- `make clean` or `mingw32-make clean` removes `main.exe` and the object files without error, even when those artifacts do not exist before the command starts.

## 6. Verification Steps

1. Run `mingw32-make clean` (or `make clean` if GNU Make is installed under `make`) from the repository root to verify the cleanup rule works in a Windows shell.
2. Run `mingw32-make main.exe` to verify compile and link steps succeed without Unix shell utilities.
3. Run `mingw32-make` to verify the default target prepends the bundled libsndfile `bin` directory to PATH and launches the executable successfully.
4. Confirm the run updates `audio/modified.wav` and does not emit a missing-DLL error for `sndfile.dll`.
5. Re-run `mingw32-make clean` to confirm the cleanup rule is idempotent.

## 7. Risks & Mitigations

- **Risk:** Some Windows `gcc` distributions may not resolve `-lsndfile` against the bundled import library in `libsndfile-1.2.2-win64/lib` even after the shell fixes.
  - **Mitigation:** If the first Windows build still fails at link time, switch the `LIB` definition in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A) from `-L... -lsndfile` to the explicit import-library file path in the same directory as a follow-up adjustment.
- **Risk:** Windows cleanup commands can mis-handle forward-slash paths derived from `$(SRC:.c=.o)`.
  - **Mitigation:** Add a Windows-normalized artifact list specifically for the `clean` recipe in [makefile](air-file://lu08fchae4k6k31pofld/Users/leona/air/sd_edit/makefile?type=file&root=C%3A), rather than reusing the Unix-form object list directly.
- **Risk:** The developer may have GNU Make installed as `mingw32-make.exe` instead of `make.exe`.
  - **Mitigation:** Verification should document both command names; the makefile itself stays GNU Make-compatible and does not depend on which executable name the local toolchain uses.