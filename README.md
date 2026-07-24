# ZIMA-CAD

Prototype of a constructor-focused 3D CAD application based on OpenCascade,
Python and Qt.

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

The first prototype opens one main window:

- object tree on the left
- OpenCascade 3D viewer on the right
- empty startup with no open document
- part files with the `.prtz` extension
- object tree with Origin, objects, sketches and first test solids

## Object Model

Each `Object` owns a mandatory system `Origin` and at most one user entity:
Point, Axis, Sketch or Solid. Additional geometry is created in another Object.
The creation commands and `.prtz` format validation enforce this rule.

## Portable Layout

ZIMA-CAD always loads its base configuration from the application directory.
The startup directory, a directory supplied as an argument, or the parent of a
supplied `.prtz` file becomes the working directory. If that directory contains
`config.ini`, it is loaded as a local override. A supplied `.prtz` file is also
opened automatically.

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
