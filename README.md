# ZIMA-CAD

ZIMA-CAD is developed as a C++/Qt application with OCCT used only for explicit
solid-model calculations. The active implementation lives in [`cpp/`](cpp/).

## Implemented capabilities and remaining work

**Family Table is implemented**, including native Part/Assembly variants, shared
parent-file storage, dimension and presence overrides, Assembly insertion and
replacement, and Drawing variant selection. See the [Family Table guide](doc/FAMILY_TABLE.md).

The application also includes parametric Sketcher, multibody Parts, combined
Extrusion/Revolution Features, 2D/3D/helical Sweeps, surface tools, sheet-metal
modeling and unfolding, Assemblies, technical Drawings, Relations, native
libraries, and shared GUI/CLI commands. Each feature has documented boundaries;
this list does not claim every future extension or solver case is complete.

See [current project status](doc/PROJECT_STATUS.md) for the maintained distinction
between implemented tools, development changes awaiting release, and planned work.
[ROADMAP.md](ROADMAP.md) records the current order and dated design decisions.
The current [Feature direction verification](doc/verification/20261022-feature-side-reversal.md)
documents physical side reversal with stable parameter panels and logical
Start/End, together with the factory and release acceptance boundary.

## Build and run

The C++ build requires CMake 3.24+, a C++20 compiler, Qt 6.5+ (Core, Gui,
Widgets, OpenGL, OpenGLWidgets, Svg and Network, plus CorePrivate), OpenSSL 3,
OpenCASCADE 8.0+, and nlohmann_json 3.11+. On Windows, use the pinned vcpkg setup through the repository script:

```powershell
./tools/build-windows.ps1 -Configuration Release
./zima-cad.bat -w Projects
```

Linux `linux-runtime-*` presets use the native SDK under `build/native-sdk/occt-8.0.1`;
the historical preset names do not imply a Python runtime. Provision the native
Qt/OCCT dependencies on the supported Linux host before configuring. Follow the [Linux handoff](doc/LINUX_RELEASE_HANDOFF.md)
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

Signed portable releases carry their own native runtime, resources and source.
Windows and Linux may publish independently; a Windows archive does not provide
a Linux runtime. User configuration and projects live outside version directories.
See [native distribution](doc/NATIVE_DISTRIBUTION.md),
[updates](doc/UPDATES.md) and the [user manual](doc/USER_MANUAL.md).

The latest published signed Windows release before the current candidate is
[2026102501](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026102501).
Its Windows package, signature, GUI, public downloads and trusted updater
discovery are verified. The current `2026102601` candidate retains the shared
OCCT 8.0.1 pin and is described in its
[release record](doc/releases/2026102601.md); that record does not claim
publication until the signed assets and public downloads pass.
The [current status](doc/PROJECT_STATUS.md) separates released scope and remaining
work. Linux verification follows the separate handoff.

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
error by default and never save implicitly. Sketch and profile commands create,
inspect and edit geometry through the same model transactions as the GUI,
including locks and Undo/Redo. Dimensions are explicitly in millimetres.

Part Modeling uses one [Feature command](doc/UNIFIED_FEATURE_TYPES.md) with
Point, Axis, Plane, Sketch and combined Extrusion / Revolution types.
The former six basic solid primitives have been removed, including their
commands and native-file support.
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

The old Conda runtime was removed. Native Linux releases have separate
verification records in the Linux handoff; this Windows release audit does not
claim a new Linux build or desktop test. See the
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

- [User manual](doc/USER_MANUAL.md)
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
- [Family Table: native Part/Assembly variants and Drawing selection](doc/FAMILY_TABLE.md)
- [Surface Extrusion and Revolution](doc/SURFACE_PROFILES.md)
- [Application tools, Insert menu and document tabs](doc/APPLICATION_TOOLS.md)
- [Sheet Metal, bends, unfolding and refolding](doc/SHEET_METAL.md)
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
