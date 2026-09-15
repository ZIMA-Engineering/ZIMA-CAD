# ZIMA-CAD

ZIMA-CAD is developed as a C++/Qt application with OCCT used only for explicit
solid-model calculations. The active implementation lives in [`cpp/`](cpp/).

## Build and run

The C++ build requires CMake 3.24+, a C++20 compiler, Qt 6.5+ (Core, Gui,
Widgets, OpenGL, OpenGLWidgets and Svg), OpenCASCADE 7.9+, and nlohmann_json
3.11+. On Windows, use the pinned vcpkg setup through the repository script:

```powershell
./tools/build-windows.ps1 -Configuration Release
./zima-cad.bat -w Projects
```

Linux dependency setup is pending replacement of the removed Conda SDK. The
`linux-runtime-*` presets still reference that absent directory and are not a
ready-to-run recipe. Follow the [Linux handoff](doc/LINUX_RELEASE_HANDOFF.md)
on Linux to select compatible native Qt/OCCT dependencies and verify the build.
Do not lower dependency version checks to fit an older installed kernel.

The root launchers `zima-cad` and `zima-cad.bat` start the C++ application.
They prefer a Release build and fall back to the Debug build.
On Windows, run `tools/create-windows-shortcut.ps1` after building to create
(or refresh) the desktop shortcut. It targets the GUI executable directly and
opens no console. The batch launcher starts the application without waiting;
for command-line verification invoke the executable directly.
Run `tools/register-windows-file-types.ps1` to register double-click opening
for the current Windows user. Every external file launch and Window > New
Window starts an independent, numbered process. See
[Multiple instances](doc/MULTIPLE_INSTANCES.md).

The planned release form is one self-contained portable Linux and Windows
archive with its complete runtime, resources, directory layout and portable
settings. The distribution contract is documented in
[`doc/PORTABLE_RELEASE.md`](doc/PORTABLE_RELEASE.md).

## Command line

The `zima-cad-cli` executable runs the shared CAD commands without a desktop window.
It still uses Qt for graphics and export, including an offscreen platform.
The Windows build script builds it alongside the desktop application:

```powershell
./tools/build-windows.ps1 -Configuration Release
& ./build/cpp-windows-release/zima-cad-cli.exe --help
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --command "documents"
```

Use repeated `--command`, a UTF-8 `--script`, or `--stdin`. Each command returns
one JSON object on stdout; diagnostics use stderr. Batches stop on the first
error by default and never save implicitly. `box.create`, `box.get` and `box.set`
create, inspect and resize a Box through the same model transaction as its GUI
properties, including locks and Undo/Redo. The same create/get/set commands are
available for cylinder, sphere, cone, pyramid and wedge. Command dimensions are
explicitly in mm.
See [CLI usage and configuration](doc/CAD_COMMAND_LINE.md).

The desktop console also supports `codex` mode. Configure your own ChatGPT account
in **Settings > AI**. Requests follow the active Part, Assembly or Drawing tab;
changes are reviewed inline and executed through the same CAD commands.
See [AI console setup and behavior](doc/AI_CONSOLE.md).

## Native application and runtime

The Python implementation was removed on 2026-09-15 at the user's request.
Historical source remains in Git; it is not a supported application or release
input. C++ is the only product implementation. Python may still be used as a
developer tool, without becoming an application dependency.

The old Conda runtime was removed after Windows verification. The Linux
development presets still require replacement of their obsolete dependency
path; a fresh Linux build awaits that native SDK setup. See the
[versioned distribution and cleanup plan](doc/DISTRIBUTION_CLEANUP_PLAN.md).

## Verification

```bash
cmake --build build/cpp-debug
ctest --test-dir build/cpp-debug --output-on-failure
```

See [native architecture](doc/CXX_ARCHITECTURE.md),
[native behavior](doc/NATIVE_BEHAVIOR_CONTRACT.md) and
[Linux release handoff](doc/LINUX_RELEASE_HANDOFF.md).

Current modeling tools and interaction contracts:

- [User manual](doc/UZIVATELSKY_MANUAL.md)
- [Holes from Sketch segments](doc/HOLES.md)
- [Automatic/manual work planes](doc/WORK_PLANES.md)
- [Assembly rotation arm](doc/ASSEMBLY_ROTATION_HANDLE.md)
- [Shared GUI/CLI command coverage](doc/CAD_COMMAND_COVERAGE.md)
- [View geometry measurement and persisted measurements](doc/MEASUREMENT.md)
- [IGES/DXF import into Parts and Assemblies](doc/IGES_DXF_IMPORT.md)
- [STEP import and export](doc/STEP_IMPORT_EXPORT.md)
- [Multibody Part and Boolean operations](doc/MULTIBODY_AND_BOOLEANS.md)
- [Mirror and Pattern: linked body and component copies](doc/MIRROR_AND_PATTERN.md)
- [Save As: model and drawing copies](doc/DOCUMENT_COPY.md)
- [Surface Extrusion and Revolution](doc/SURFACE_PROFILES.md)
- [Application tools, Insert menu and document tabs](doc/APPLICATION_TOOLS.md)
- [Sheet Metal defaults and planned Bend](doc/SHEET_METAL.md)
- [3D Sweep: Loft, Thin, and trajectory references](doc/3D_CURVE_AND_SWEEP.md)
- [Threaded openings](doc/THREADED_OPENING.md)
- [External shaft threads](doc/SHAFT_THREAD.md)
- [Helical Sweep](doc/HELICAL_SWEEP.md)
- [2D Sweep: planar paths, Loft, and Thin](doc/SWEEP_2D.md)
- [View and Tree highlighting](doc/MODELING_INTERACTION.md)
- [History reordering](doc/HISTORY_TREE_REORDER.md)
- [Sketch constraint activity](doc/SKETCH_CONSTRAINT_ACTIVITY.md)
- [Drawing templates, embedded PNG/SVG logos, and BOM regions](doc/DRAWINGS.md)
- [Numeric value locks and one-shot reference capture](doc/NUMERIC_VALUE_LOCKS.md)
- [Czech, English, German, French, and Russian UI translations](doc/LOCALIZATION.md)
- [Mass, material density, document units, and Parameters](doc/PHYSICAL_PROPERTIES.md)

All project documentation must be maintained in English. See the binding
[documentation language rule](AGENTS.md#documentation-language-mandatory).
