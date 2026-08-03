# ZIMA-CAD

Prototype of a constructor-focused 3D CAD application based on OpenCascade,
Python and Qt.

ZIMA-CAD uses OpenCascade for CAD geometry, but is developing its own
high-performance rendering engine instead of relying on OpenCascade
visualization.

Development plans and major feature milestones are tracked in
[ROADMAP.md](ROADMAP.md).

## License

ZIMA-CAD is free software licensed under the GNU General Public License,
version 3 or (at your option) any later version (`GPL-3.0-or-later`).
See [LICENSE](LICENSE) for the license notice and warranty disclaimer.

## Run

```powershell
conda activate zima-cad
python main.py
```

With unpacked Windows runtime:

```powershell
.\zima-cad.bat
.\zima-cad.bat D:\PRACE
.\zima-cad.bat D:\PRACE\dil.prtz
.\zima-cad.bat --working-directory D:\PRACE
```

Linux launch syntax uses the same arguments:

```bash
./zima-cad /home/user/PRACE
./zima-cad /home/user/PRACE/dil.prtz
./zima-cad --working-directory /home/user/PRACE
```

The application opens one main window with multiple document tabs:

- container tree on the left
- native OpenGL 3D viewer or 2D drawing workspace in the centre
- empty startup with no open document
- part files with the `.prtz` extension
- assembly files with the `.asmz` extension and inserted `.prtz` instances
- drawing files with the `.drwz` extension, multiple sheets and linked model
  views
- container tree with Origin, entities, sketches and first test solids

Current mouse controls, reference selection and the Properties workflow are
described in the Czech [user manual](doc/UZIVATELSKY_MANUAL.md).

The Assembly workflow supports inserted part instances, live placement,
planar/offset, concentric-axis and angular mates, degree-of-freedom-aware mate
types, editable in-view mate dimensions, expanded source-part trees,
in-context part editing and assembly-only extruded or revolved cuts applied to
selected components. Part geometry remains unchanged by assembly cuts. Stable
semantic topology naming is implemented for Box/Wedge faces and for Extrusion
and segment-profile Revolve faces, edges and vertices. External Sketch
references use those identities and survive supported parent recomputation;
planar container orientation references use them as well. The initial Part
`Fuse`/`Cut` subset propagates supported ancestry and reports splits/merges
explicitly. New section edges/vertices use ZIMA-owned adjacency identities;
circular Extrusion, closed-spline Extrusion/full Revolve, mixed nested
segment/arc/spline Extrusion and repeated center-arc Revolve cuts are supported.
Three-level Extrusion and partial-Revolve nesting (outer loop, hole and island)
is supported with deterministic cap fragments. General multi-island Boolean
chains now preserve supported ancestry through repeated cuts; disconnected
solids can also be joined by an additive bridge before a subsequent cut.
Crossing cylindrical Extrusion and full center-arc Revolve cuts preserve curved
intersection ancestry; a closed-spline Extrusion cut preserves the same ancestry
when crossing the cylinder. Unjoined additive multi-body results and the remaining
Assembly cases are not yet
production-safe. A disconnected additive feature or non-intersecting cut
preserves the last valid Part body and is reported as invalid.

Assembly face selections use a canonical `AssemblyFaceRef` containing both the
component instance ID and the source Part `FaceRef`. Legacy index/string
descriptors are intentionally unsupported. Planar mates on Boolean-generated
faces survive source Part regeneration and report `MISSING` when the source
feature is suppressed.

The Drawing workspace supports `.drwz` documents linked to either a part or an
assembly, multiple independently sized sheets, A4 through A0 paper outlines,
first- and third-angle projected views in eight 45-degree placement directions,
persistent parent/child alignment, movable view captions, per-view display
styles, model/component colors and initial associative linear dimensions.
Shaded drawing views use interpolated model normals and a cached software
Z-buffer; line views share topology, silhouette and hidden-edge classification
with the native model renderer. A4 is fixed to portrait orientation; A3 through
A0 are fixed to landscape. Drawing space uses a bottom-right origin with
positive X to the left and positive Y upward.

## Container Model

Each `Container` owns a mandatory system `Origin` and at most one user entity:
Point, Axis, Sketch or Solid. Additional geometry is created in another Container.
The creation commands and `.prtz` format validation enforce this rule.

## Portable Layout

ZIMA-CAD always loads its base configuration from the application directory.
The startup directory, a directory supplied as an argument, or the parent of a
supplied `.prtz`, `.asmz` or `.drwz` file becomes the working directory. If
that directory contains `config.ini`, it is loaded as a local override. A
supplied ZIMA-CAD document is also opened automatically.

Planned application folder:

```text
ZIMA-CAD/
  zima-cad.exe              Windows launcher
  zima-cad                  Linux launcher
  app/                      packaged application files or future C++ binaries
  Projects/                 user part, assembly and drawing files
  config/                   application configuration and editable libraries
    materials/              categorized material cards such as S235.matz
    templates/              starter part, assembly and drawing templates
  resources/
    icons/
    branding/               app icon, symbol, splash and About artwork
      source/               source artwork and retained design variants
    templates/
    materials/
  runtime/
    windows/                Windows Python, Qt, OpenCascade and DLL libraries
    linux/                  Linux Python, Qt, OpenCascade and .so libraries
```

Source code stays cross-platform. Packaged runtimes are platform-specific:
Windows and Linux need their own executable and native OpenCascade/Qt
libraries. Shared data such as `.prtz`, `.asmz`, `.drwz`, templates, materials
and settings should stay platform-independent.

During prototype development the Python package still lives in `zima_cad/`.
The `app/` directory is reserved for the packaged application layout and for a
possible future C++ implementation.

## Configuration

Base application settings are stored in:

```text
config/config.ini
```

A project may contain a partial local configuration:

```text
PRACE/
  config.ini
  materials/
  templates/
  dil.prtz
```

The local file overrides only its non-empty values. Missing or empty values
inherit from the base application configuration. A relative path is resolved
from the configuration file in which that path was defined. This allows a
project to use local materials while continuing to use application templates
and localization.

Editable CAD libraries live under `config/`:

```text
config/materials/
config/templates/
```

`config/materials` is intended for categorized material `.matz` files with
physical parameters. A selected material card is copied into the document; an
opened `.prtz` file therefore does not depend on or search the material library.
The concrete cards contain representative values at 20 °C for CAD defaults and
preliminary calculations; the supplier certificate remains authoritative.
`config/templates` is intended for starter documents with prepared user
parameters and document defaults.

Current language setting:

```ini
[Application]
Language=cs

[Paths]
Materials=materials
Templates=templates
```

Paths may be absolute or relative, but relative paths with `/` are recommended
for projects shared between Windows and Linux. Global Settings can export the
displayed configuration with **Save As**. Exporting does not activate the new
file and does not change the working directory.

## File Versions

Whenever ZIMA-CAD replaces an existing file, it keeps the previous contents
beside that file using an incrementing numeric suffix:

```text
Drzak.prtz
Drzak.prtz.1
Drzak.prtz.2

config.ini
config.ini.1
```

The rule applies to every format written by ZIMA-CAD, including documents,
configuration, materials, drawings and future exports. A newly created file
has no archive. ZIMA-CAD neither displays nor manages the numeric archive
files; cleanup is the responsibility of ZIMA-PTC-Cleaner.

New contents are first written and validated in the destination directory.
Only then is the previous file archived and the temporary file atomically
installed under the current name.

## User Parameters

User parameters are document metadata for drawings, title blocks and bills of
materials. Parameter keys stay language-independent. Labels and values may be
localized.

```ini
[UserParameters]
Order=nazev, norma, material, mnozstvi

[UserParameterLabels]
nazev\cs=Nazev
nazev\en=Name

[UserParameterValues]
nazev\cs=KORPUS
nazev\en=BODY
norma=CSN EN 10131
```

A value without language, such as `norma=...`, is shared by all languages.

## Runtime Packaging

Windows runtime is built from the current Conda environment:

```powershell
.\tools\pack-windows-runtime.ps1
```

This creates:

```text
runtime/windows/zima-cad-runtime-windows.zip
```

Unpacking is a separate step because the runtime is large:

```powershell
.\tools\unpack-windows-runtime.ps1 -Force
```

The unpacked runtime goes to:

```text
runtime/windows/python/
```

The unpack script uses Windows `tar` instead of PowerShell `Expand-Archive`
because it handles the large runtime ZIP much better.

Linux runtime must be built on Linux or WSL because it contains Linux native
`.so` libraries. It cannot be created correctly from Windows.

Build it from the `zima-cad` Conda environment and unpack it with:

```bash
tools/pack-linux-runtime.sh
tools/unpack-linux-runtime.sh --force
```

This creates the ignored archive
`runtime/linux/zima-cad-runtime-linux.tar.gz` and installs the portable runtime
under `runtime/linux/python/`. Start the packaged application with:

```bash
./zima-cad
```
