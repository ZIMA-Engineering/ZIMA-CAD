# ZIMA-CAD

ZIMA-CAD is developed as a C++/Qt application with OCCT used only for explicit
solid-model calculations. The active implementation lives in [`cpp/`](cpp/).

## Build and run

The C++ build requires CMake 3.24+, a C++20 compiler, Qt 6.5+ (Core, Gui,
Widgets, OpenGL, OpenGLWidgets and Svg), OpenCASCADE 7.9+, and nlohmann_json
3.11+. The runtime preset below uses the repository runtime. When building
against system packages, check their versions first; lowering the OCCT check
does not establish support for OCCT 7.8.

Existence checks on forward-declared dialog types use `QPointer::isNull()`.
This avoids an incomplete-type compilation error in Qt versions whose
implicit pointer conversion requires the complete dialog definition. The
Measurement and Appearance command sources were checked with system Qt 6.8.2;
this check does not certify a complete Debian build or an older OCCT kernel.

```bash
cmake --preset linux-runtime-debug -S cpp
cmake --build build/cpp-debug --target zima-cad-cpp
./zima-cad -w Projects
```

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

## Frozen Python reference

The former Python implementation is frozen in [`archive/python/`](archive/python/).
It is retained only as a runnable behavioural and visual reference; new
features and fixes belong exclusively to C++.

```bash
./archive/python/run.sh -w Projects
```

Shared configuration, resources, project data and the bundled runtime remain
at repository root and are supplied to the archived application by its
launcher. Historical Python documentation is preserved as
[`archive/python/README.md`](archive/python/README.md).

## Verification

```bash
cmake --build build/cpp-debug
ctest --test-dir build/cpp-debug --output-on-failure
```

Architecture and migration notes are in [`doc/`](doc/).

Current modeling tools and interaction contracts:

- [Uživatelský manuál](doc/UZIVATELSKY_MANUAL.md)
- [Měření geometrie ve View a uložená měření](doc/MEASUREMENT.md)
- [Import IGES a DXF do Partu a sestav](doc/IGES_DXF_IMPORT.md)
- [Import a export STEP](doc/STEP_IMPORT_EXPORT.md)
- [Multi-body Part and Boolean operations](doc/MULTIBODY_AND_BOOLEANS.md)
- [Zrcadlo a Pole: linked body and component copies](doc/MIRROR_AND_PATTERN.md)
- [Save As: model and drawing copies](doc/DOCUMENT_COPY.md)
- [3D tažení (3D Sweep): Loft, Thin and trajectory references](doc/3D_CURVE_AND_SWEEP.md)
- [Threaded openings](doc/THREADED_OPENING.md)
- [External shaft threads](doc/SHAFT_THREAD.md)
- [H-tažení (Helix Sweep)](doc/HELICAL_SWEEP.md)
- [2D tažení (2D Sweep): rovinná dráha, Loft a Thin](doc/SWEEP_2D.md)
- [View and Tree highlighting](doc/MODELING_INTERACTION.md)
- [History reordering](doc/HISTORY_TREE_REORDER.md)
- [Sketch constraint activity](doc/SKETCH_CONSTRAINT_ACTIVITY.md)
- [Drawing templates, embedded PNG/SVG logos and BOM regions](doc/DRAWINGS.md)
- [Numeric value locks and one-shot reference capture](doc/NUMERIC_VALUE_LOCKS.md)
- [Czech, English, German, French and Russian UI translations](doc/LOCALIZATION.md)
- [Mass, material density, document units and Parameters](doc/PHYSICAL_PROPERTIES.md)
