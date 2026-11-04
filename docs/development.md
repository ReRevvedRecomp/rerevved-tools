# Development

ReRevved Tools uses C++23 and CMake. Run commands from the repository root.
Build output belongs in `out/build/<preset>`, as set by
[CMakePresets.json](../CMakePresets.json).

## Prerequisites

- CMake 3.25 or newer, Ninja, and Git.
- A C++23 compiler and standard library with `std::expected` and
  `std::ios::noreplace` support.
- PowerShell 7 (`pwsh`) for `scripts/verify.ps1`.
- `clang-format` 22.x on `PATH`. The script rejects other major versions.
- OpenGL development libraries and GLFW's platform development dependencies.
- A display and OpenGL 3.3 core context for the application and its smoke frame.

Windows presets use `clang` and `clang++` from `PATH`, with their Windows SDK
and C++ standard library dependencies. Linux uses `clang-22` and `clang++-22`.
The [checks workflow](../.github/workflows/checks.yml) lists its Linux packages:
`build-essential`, `libgl1-mesa-dev`, `libwayland-dev`, `libxkbcommon-dev`,
`ninja-build`, `xorg-dev`, and `xvfb`, in addition to Clang 22 and
installation tools. Python is used there to install clang-format, not by
ReRevved Tools itself.

The first configure needs access to the pinned GLFW, Dear ImGui, dr_libs, stb,
Inter, and Lucide sources through CMake FetchContent. Exact revisions, and
SHA-256 hashes for the font archives, live in
[dependencies.cmake](../cmake/dependencies.cmake). Terms are recorded in
[Dependency licenses](../licenses/README.md). Parser tests are GPU free, but
this CMake project still configures the GUI dependencies and requires OpenGL.

## Presets

| Preset | Configuration | Executable |
|---|---|---|
| `win-amd64-debug` | Windows AMD64 Debug | `out/build/win-amd64-debug/rerevved-tools.exe` |
| `win-amd64-release` | Windows AMD64 Release | `out/build/win-amd64-release/rerevved-tools.exe` |
| `linux-amd64-debug` | Linux AMD64 Debug | `out/build/linux-amd64-debug/rerevved-tools` |

The build also produces `rerevved-map-suite` in the same directory. There is no
installer or CMake install rule. Run the executables from their build directory.
The Windows Release preset links the C++ runtime statically, so its executable
needs no Visual C++ redistributable.

## Releases

A push to `main` whose `project()` version in `CMakeLists.txt` has no tag
publishes that version through the
[release workflow](../.github/workflows/release.yml). To release, raise the
version. The workflow verifies the Windows Release preset, then attaches the
zip that `scripts\package.ps1` makes from `out/build/win-amd64-release`, which
holds `rerevved-tools.exe`, `LICENSE`, `README.md` and `licenses/`. It writes to
`out/package` and refuses to replace an existing package.

## Build and check

For Windows, run both presets from a PowerShell session:

```powershell
pwsh -File .\scripts\verify.ps1 -Preset win-amd64-debug
pwsh -File .\scripts\verify.ps1 -Preset win-amd64-release
```

For Linux with a desktop session:

```sh
pwsh -File ./scripts/verify.ps1 -Preset linux-amd64-debug
```

For a headless Linux session with Xvfb installed, the workflow uses:

```sh
xvfb-run -a pwsh ./scripts/verify.ps1 -Preset linux-amd64-debug
```

The script first checks the clang-format version, then runs configure, build,
CTest, the hidden app smoke run with `README.md` as its input, the scripted
input smoke runs, formatting checks over `api`, `src`, and `tests`, and
`git diff --check`. The final diff check assumes a Git checkout, not a plain
source archive.

The smoke runs check application startup, drawing, and the scripted map flows
described in [Testing](testing.md). They do not assert that every command line
file parsed or that every format can preview. Use the
[synthetic tests and desktop checklist](testing.md) for those boundaries.

Use `-SkipAppSmoke` when a display or OpenGL context is unavailable, and report
that omission. A run that skips the smoke stages is not a full application check.

## Diagnose one stage

```powershell
cmake --preset win-amd64-debug
cmake --build --preset win-amd64-debug
ctest --preset win-amd64-debug
.\out\build\win-amd64-debug\rerevved-tools.exe --smoke-test .\README.md
```

Replace the preset consistently for another configuration. To regenerate the
configure state and rebuild targets:

```powershell
cmake --fresh --preset win-amd64-debug
cmake --build --preset win-amd64-debug --clean-first
ctest --preset win-amd64-debug
```

These commands are individual diagnostics, not replacements for the remaining
verification stages.

## Continuous integration

The Checks workflow runs on pull requests, pushes to `main`, and manual
dispatch. It checks Windows Debug with `-SkipAppSmoke` and Linux Debug under
Xvfb with app smoke enabled. It does not run the Windows Release preset or
publish a release. A green Windows job is not evidence of a Windows GUI smoke
check.

## Pull requests

Contributions are GPL-3.0-only. Open pull requests against `main`. Do not submit
retail files, extracted assets, captures, decoder output, build output,
credentials, or machine paths.

Read [Architecture](architecture.md) for ownership,
[File format support](file-formats.md) for parser contracts, and the
[style guide](style-guide.md) before changing behavior. Keep parsers and extraction
independent of the desktop interface. Check every read against the supplied byte
range and leave source content unchanged. Preserve unknown fields until
reproducible evidence gives them a stable meaning. Support format claims with a
public specification, a documented observation with an explicit input range, or a
reproducible parser result. Keep dependency revisions pinned and review their
licenses when changing them. Update the guide that owns a changed fact rather than
repeating it across documents.

Run the [checks](#build-and-check). Explain the change, the checks run, and what
they do not cover. A build or synthetic test is not proof that a map plays
correctly in the game.

Commit subjects use `type: imperative summary`, at most 50 characters, with no
body or parentheses, a lowercase type and a lowercase opening verb.
