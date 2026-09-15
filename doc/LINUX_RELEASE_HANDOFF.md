# Linux build and distribution handoff — 2026-09-15

## Read first

- [Binding distribution requirements](DISTRIBUTION_CLEANUP_PLAN.md)
- [Portable release entry point](PORTABLE_RELEASE.md)
- [Windows dependency and acceptance rules](WINDOWS_RUNTIME_AND_BUILD.md)
- Repository `AGENTS.md`, including the mandatory English documentation rule.

The user explicitly chose to finish Linux from Linux. Do not infer that Windows
verification establishes Linux support. No Linux package or updater was built
in this session, and no release was published.

## Starting state

C++ is the only application. The obsolete Python application, Python-only tests,
launchers, Conda packaging scripts and benchmark were removed at the user's
request. Git history retains their original source. Keep active C++ native
fixtures, tracked `config/` templates/resources and user Projects.

Windows uses the pinned vcpkg manifest in `cpp/vcpkg.json`. GUI and CLI rebuilt;
console UI verification passed 1/1 after the terminal icon change. Regenerate is first in the
View toolbar for Part, Assembly and Drawing. The next button reuses `ZIMA-CAD-Parts/gfx/navigation/terminal.svg` and the
existing console toggle with Ctrl+Shift+C.

Linux presets in `cpp/CMakePresets.json` still point at
`runtime/linux/python` for native Qt/OCCT libraries. The user subsequently authorized removing the whole old runtime after
Windows checks passed with it detached. The directory and archives are now
deleted. These presets therefore require replacement before a fresh Linux
build; do not restore Conda from Git or package it.

## Next work on Linux

1. Inspect installed distro/toolchain and existing C++ build cache. Select and
   record the exact supported Debian x86_64 baseline and KDE/Wayland environment
   from the shared policy. Check actual versions; do not lower CMake's Qt/OCCT
   requirements to fit older system packages.
2. Establish versioned native dependencies using an explicit SDK or compatible
   system development packages. Remove the Conda path from Linux CMake presets
   and runtime search paths. Retain normal developer override mechanisms.
3. Configure and build in a fresh directory, run relevant native CTest contracts,
   then launch GUI and CLI with no Conda directory on PATH or library paths.
   Inspect linked libraries and plugin resolution; verify image/PDF exports,
   a deterministic Part operation and native save/reopen.
4. The obsolete runtime has already been deleted on this checkout. If another
   Linux checkout retains a local ignored copy, remove it after the new native
   checks pass. Check resolved paths and preserve user data. Record proof of
   dependency independence.
5. Implement packaging in the order specified by the binding requirements:
   one build ID, exact source export, native dependencies/licenses, portable
   settings separation, launchers, archive validation, then signed update logic.
   Adapt the sibling Parts design to CMake; do not claim its unfinished features
   already exist and do not import its unrelated runtime dependencies.

## Release rules to preserve

`YYYYMMDDNN` is the build identity; tag `ZIMA-CAD-<id>` and archive
`ZIMA-CAD-<id>.zip` must match embedded GUI/CLI metadata. A common root contains
`windows/<id>`, `linux/<id>`, `source/<id>` and separate `custom` directories.
Each version carries its own native libraries and immutable factory resources.
Portable user `config/`, Projects and recovery live outside those directories.
`config/config.ini` holds common overrides; `config/linux/config.ini` holds Linux
path overrides. Preserve the root `config/` instead of introducing `profile/`.

Build official artifacts only from clean committed/tagged source, including
initialized submodules. Never package working artifacts, old Conda runtimes,
private data or required external geometry sidecars. Validate paths/collisions,
member lengths, CRC, SHA-256, clean extraction, dependencies and actual execution.
Hashes prove integrity, not official origin; signatures/key management remain
required before authenticated updates. Keep the current and previous verified
official versions per OS, preserve custom builds and all user data, and retain
sources for every retained platform build.

Stage both platforms before publication. Do not overwrite a published archive
when adding the other platform later. Document unsupported native document
versions explicitly; binary rollback does not imply backward file compatibility.

Update English documentation with actual results, remaining limitations and
exact artifact identities. Commit and push the completed changes as requested;
do not publish release assets without completing release validation.

Final Windows run with the entire old runtime absent: GUI/CLI build passed;
console GUI contract passed 1/1 in 139.26 s. Logs:
`build/native-only-toolbar-build.log`, `build/native-only-toolbar-tests.log`.
The user explicitly accepted doing the remaining Linux work from Linux.

## Subsequent Windows implementation

Read [NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md) before duplicating work.
`VERSION`, GUI/CLI `--build-info`, shared portable installation detection and
configuration layers now exist. `tools/distribution/package.py` exports exact
Git blobs (including recorded submodule commits) and provides archive validation.
The native Windows launcher and Windows build wrapper are implemented.

`tools/distribution/launcher-linux.sh` is the Linux selection contract to verify
on Linux. Supply `build.ini` (`product=ZIMA-CAD`, matching version/platform),
`version.json`, version-local `config/`, executable `bin/` and dependencies.
Preserve ELF and script executable modes when assembling a Linux archive.
The current ZIP extractor is deliberately a Windows verifier and does not apply
Unix executable modes; extend and verify the Linux extraction path on Linux.
Do not claim KDE/Wayland execution, Debian dependency closure or the signed updater
based on these Windows changes. Keep configuration snapshots before future schema
conversion, separately from ordinary numbered INI backups.

Windows candidate `2026091503` is now validated (commit, hash and exact check scope
are recorded in [NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md#windows-verification-on-2026-09-15)).
Use this as evidence for Windows only. Changes needed for Linux will require a
new committed candidate identity and matching Windows build before combining
platforms; do not label a different Linux commit as the existing Windows build.
