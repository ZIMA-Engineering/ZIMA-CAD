# Windows Runtime and Release Builds

This document is the authoritative procedure for ZIMA-CAD Windows runtime and
portable release archives. Use the repository scripts; do not reproduce the
release manually with PowerShell archive cmdlets.

## Incident baseline: 2026-08-17

Two independent packaging failures were corrected in the Windows assets of
`v0.1.9-alpha`.

1. The packaged numerical runtime did not start reliably. The working repair
   selected the OpenBLAS variants of `libblas`, `libcblas` and `liblapack` and
   included `libopenblas` plus `openblas.dll`, `libblas.dll` and `libcblas.dll`.
2. The portable build contained development headers with very long member
   names. A normal Windows extraction directory could push their complete path
   beyond the legacy Windows path budget and extraction failed. Compile-time
   headers, CMake/pkg-config metadata, manuals and PDB files were removed from
   the runtime.

The pre-fix `2026.08.13` build had 58,985 ZIP entries. Its longest member was
219 characters and belonged to a Viskores C++ header below
`Library/include/viskores-1.1/`. Its BLAS provider metadata selected MKL and it
did not contain `openblas.dll`.

The corrected `2026.08.16` build has 18,723 entries and a longest member of 161
characters. The corrected standalone runtime has 16,756 entries and a longest
member of 110 characters.

MKL DLLs may still be present because another dependency owns them. Their mere
presence is not a failure. The release contract is that the `libblas`,
`libcblas` and `liblapack` provider metadata selects OpenBLAS, the OpenBLAS DLLs
exist, and the packaged runtime passes the numerical import smoke test.

## Mandatory release invariants

- Create and smoke-test Windows runtimes only on Windows. A Linux or WSL Conda
  environment contains incompatible native libraries.
- `libblas`, `libcblas` and `liblapack` must use Conda builds whose build string
  contains `openblas`; `libopenblas` must be installed.
- The packaged runtime must contain `openblas.dll`, `libblas.dll` and
  `libcblas.dll` under `Library/bin/`.
- NumPy calculation, PySide6 import, OCCT import and a simple OCCT solid
  calculation must work both before and after packing.
- Runtime ZIP member names may not exceed 140 characters. Complete Windows
  build ZIP member names may not exceed 180 characters. The latter leaves a
  useful margin when the user extracts below a path up to approximately 60
  characters long.
- A release ZIP may not contain unsafe paths, Windows-invalid names,
  case-insensitive collisions, unexpected duplicates, symlinks, PDBs or the
  pruned development trees.
- Perform a complete ZIP CRC test and produce SHA-256 for both archives.
- Build from a committed Git ref with no tracked working-tree changes. Untracked
  project data must never leak into a release.
- Create the complete build below a short staging root such as `C:\zb` and use
  Windows `tar.exe`/libarchive. Do not use `Compress-Archive` or
  `Expand-Archive` for these large trees.
- A candidate archive replaces the previous known-good archive only after all
  validation and smoke tests pass.
- Do not run `conda-unpack` before creating the distributable ZIP. It rewrites
  the environment for one location; the official conda-pack documentation
  warns that an environment cannot be relocated after that step. The packaged
  `zima-cad.bat` runs it once after extraction, at the final application path.
  If a user moves the directory after first launch, extract a fresh copy rather
  than moving the finalized runtime.

## Prepare the Windows Conda environment

From a Miniforge/Conda PowerShell on Windows, select the OpenBLAS variants when
creating or repairing the `zima-cad` environment:

```powershell
conda install -n zima-cad -c conda-forge `
    "libblas=*=*openblas" `
    "libcblas=*=*openblas" `
    "liblapack=*=*openblas" `
    libopenblas
```

The exact package versions may advance. Do not hard-code the incident versions;
the provider and runtime checks are the stable contract.

## Create the standalone runtime

Run from the repository root on Windows:

```powershell
.\tools\pack-windows-runtime.ps1
```

If Conda is not discoverable, provide its command explicitly:

```powershell
.\tools\pack-windows-runtime.ps1 `
    -CondaBat C:\Users\USER\miniforge3\condabin\conda.bat
```

The script:

1. verifies the OpenBLAS Conda provider and DLLs;
2. smoke-tests NumPy, PySide6, OCCT and ZIMA-CAD in the source environment;
3. runs `conda-pack` while excluding runtime-unnecessary development files;
4. validates path safety, path budgets, required files, CRC and SHA-256;
5. extracts the candidate to a temporary short path, runs `conda-unpack`, and
   repeats the import/calculation smoke test;
6. replaces `runtime/windows/zima-cad-runtime-windows.zip` only after every
   previous step succeeds.

Never use an already finalized/unpacked environment as the source of a new
runtime archive. Rebuild it from the maintained Conda environment and its
package cache.

## Unpack a runtime for source-tree use

```powershell
.\tools\unpack-windows-runtime.ps1 -Force
```

This validates and extracts into `runtime/windows/python/` through a candidate
directory, so extraction failure does not destroy the previous installation.
It deliberately leaves relocation pending. The next `zima-cad.bat` start runs
`conda-unpack` once at this final path and writes the local marker
`.zima-conda-unpacked`.

## Create a complete portable Windows build

Commit the intended source first. The build script intentionally rejects
tracked uncommitted changes and packages the selected Git object rather than a
copy of the working directory:

```powershell
.\tools\pack-windows-build.ps1 -BuildDate 2026.08.17
```

Useful explicit form:

```powershell
.\tools\pack-windows-build.ps1 `
    -BuildDate 2026.08.17 `
    -SourceRef HEAD `
    -RuntimeArchive .\runtime\windows\zima-cad-runtime-windows.zip `
    -StagingRoot C:\zb `
    -Force
```

The output is `builds/ZIMA-CAD-WINDOWS-YYYY.MM.DD.zip`. The script:

- validates the standalone runtime before using it;
- exports only the committed source through `git archive`;
- extracts the still-relocatable runtime under the build root;
- records source commit and runtime SHA-256 in `WINDOWS-BUILD-INFO.txt`;
- rejects archive paths above 180 characters;
- creates the ZIP using Windows `tar.exe`;
- validates the complete ZIP and performs the staged runtime smoke test;
- replaces an existing same-name build only after success and only with
  `-Force`.

The script defaults to the short staging root at `C:\zb` on the repository
drive. Pass another short, dedicated directory if that location is not
writable.

## Independent verification

The validator uses only the Python standard library and can be run separately:

```powershell
python .\tools\windows_package.py `
    .\runtime\windows\zima-cad-runtime-windows.zip `
    --kind runtime

python .\tools\windows_package.py `
    .\builds\ZIMA-CAD-WINDOWS-2026.08.17.zip `
    --kind build
```

Do not use `--skip-crc` for a published asset. That option exists only for a
fast local metadata check.

Before upload, also extract the final build ZIP into a normal user-owned
Windows directory and launch `zima-cad.bat`. The automated smoke test proves
imports and native libraries; the final manual launch proves the actual GUI,
graphics driver and first-run relocation path.

Record the build SHA-256 and use release asset labels that identify the runtime
contract, for example `Windows portable runtime (OpenBLAS + short-path fix)` and
`Windows build (OpenBLAS + short-path fix)`.

## Known-good v0.1.9-alpha assets

These values are an incident baseline, not permanent filenames for future
releases:

| Asset | Size | SHA-256 |
| --- | ---: | --- |
| `zima-cad-runtime-windows.zip` | 874,901,521 bytes | `9a88feb742894cc1174a555c982a469cb51de363a5086247a3aff491ca208f48` |
| `ZIMA-CAD-WINDOWS-2026.08.16.zip` | 836,752,135 bytes | `6c10394b807bb3c26353af22b3e0839f070f9945212c88625414b6701be79c29` |
