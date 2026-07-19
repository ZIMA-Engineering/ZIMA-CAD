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
```

The first prototype opens one main window:

- object tree on the left
- OpenCascade 3D viewer on the right
- empty startup with no open document
- part files with the `.prtz` extension
- object tree with Origin, objects, sketches and first test solids

## Portable Layout

ZIMA-CAD should always resolve paths from its own application directory, not
from the directory where the terminal happens to be opened.

The directory where ZIMA-CAD is started becomes the working directory for new
and opened project files. If that directory contains `config.ini`, it is used as
the application options file for that run.

Planned application folder:

```text
ZIMA-CAD/
  zima-cad.exe              Windows launcher
  zima-cad                  Linux launcher
  app/                      packaged application files or future C++ binaries
  Projects/                 user part, assembly and drawing files
  config/                   application configuration and editable libraries
    materials/              material cards such as S235.ini or S235JR.ini
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

Application settings are stored in:

```text
config/config.ini
```

Editable CAD libraries live under `config/`:

```text
config/materials/
config/templates/
```

`config/materials` is intended for material `.ini` files with physical
parameters. `config/templates` is intended for starter documents with prepared
user parameters and document defaults.

Current language setting:

```ini
[Application]
Language=cs

[Paths]
Materials=materials
Templates=templates
```

Paths may be absolute or relative. Relative paths are resolved from the
directory containing the active `config.ini`.

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
