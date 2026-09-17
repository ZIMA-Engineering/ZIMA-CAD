# Linux build and distribution handoff — 2026-09-15

## Native Linux follow-up (2026-09-17)

The repository now has a pinned OCCT SDK builder and a committed-source Linux
candidate builder. Follow [Native Debian build and release](LINUX_BUILD.md).
The historical handoff below describes the starting state; actual acceptance
results are recorded in [Linux candidate 2026091701](releases/2026091701.md).
The unsigned package passed native smoke on KDE/Wayland; the publisher key is
still needed before signed finalization and public release.

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

The updater uses Qt Network, OpenSSL 3 Ed25519 and the **matching Qt Core private
headers** for QZipReader. Build `zima-cad-update` with GUI/CLI and deploy it under
`linux/<build>/bin/`, with its native dependencies and TLS plugin. Current manifest
platform identity is `linux-x86_64`; establish and enforce the supported distro/
libc baseline before publishing Linux assets. Add Linux runtime smoke/report
generation to the candidate builder and finalized-archive acceptance; Windows
tests of synthetic Linux metadata do not replace that work. Run the isolated
updater lifecycle tests and exercise KDE/Wayland startup/acknowledgement, normal
shutdown, instance blocking, launcher recovery, permissions and rollback.

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
5. Extend the existing native candidate builder with Linux dependency deployment
   and smoke checks. The CMake updater, signed metadata, GUI Settings and retention
   are now implemented; read [Application updates](UPDATES.md) and verify their
   actual Linux execution. Do not import unrelated ZCP runtime dependencies.

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
Hashes prove integrity, not official origin; use the implemented signed manifest
and publisher workflow for authenticated updates. Keep current and previous verified
official versions per OS, preserve custom builds and all user data, and retain
sources for every retained platform build.

Platforms may publish independently. Use a new build ID when adding another
platform; never replace published bytes. Document unsupported native document
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

## Published Windows release and AI follow-up

The first signed Windows release,
[2026091504](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091504),
is now public. Its accepted archive and signed manifest are immutable. Linux can
publish independently under a new build ID after Linux runtime validation.

The signed Windows release
[2026091505](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091505)
adds the [AI console](AI_CONSOLE.md). Its native smoke, signed archive and packaged
Settings checks passed; this does not establish Linux support. The CMake
`zima_ai` target uses existing Qt Core/Gui dependencies, AUTOMOC and a bundled
engineering-reasoning resource. Codex remains a separately installed optional
native executable; do not bundle accounts or a developer's Codex profile.
Settings stores `AI/Model` and `AI/CodexExecutableLinux` in the shared installation
configuration. Verify native executable discovery, the desktop sign-in browser,
OS keyring, the private application-local profile and cancellation on Linux.
Run `zima_ai_contract` without an account first; authenticated AI acceptance is a
separate step. Windows results do not establish Linux authentication/runtime support.
