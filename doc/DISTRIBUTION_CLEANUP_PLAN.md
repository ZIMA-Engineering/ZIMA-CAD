# Native runtime and versioned distribution

## Decision and implementation status (2026-09-15)

The user approved retiring the Python application and using the same principle
as ZIMA-CAD-Parts: each version owns its executables, native dependencies and
resources; previous versions remain launchable; user data lives outside version
directories. C++ is the only product implementation. The detailed layout and
implementation stages below are the agreed packaging requirements. The Windows
candidate builder and native signed updater are now implemented; actual release
acceptance is recorded separately in [Application updates](UPDATES.md).

Reviewed sibling documents: `ZIMA-CAD-Parts/doc/distribution-policy.md` and
`ZIMA-CAD-Parts/doc/windows-distribution.md`, as present on 2026-09-15.
The Parts policy is applicable, but its scripts cannot be copied unchanged:
Parts uses qmake and different executables/resources, while CAD uses CMake,
GUI/CLI executables and different runtime plugins. Its updater/signing sources
have since been adapted and tested in CAD; this is not a claim that either
product has published a signed official release.
CAD's implementation and acceptance status are recorded in
[NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md).

## Inputs, means, outputs and independent check

- Inputs: committed C++ source, a build ID, pinned toolchain/dependencies,
  tracked factory configuration and native templates, licenses and test inputs.
- Means: CMake, the existing Windows vcpkg manifest/build script, a supported
  Linux native toolchain, dependency inspection and archive validation.
- Output: a self-contained versioned package with GUI and CLI, reproducible
  source, version metadata and a safe switch back to an earlier version.
- Independent check: the actual `cpp/vcpkg.json` requires Qt, OCCT, FreeType,
  HarfBuzz and nlohmann_json. Python is not a linked application requirement.
  Qt graphics/export plugins remain necessary for the CLI. Linux presets still
  point to `runtime/linux/python`, so removing it also
  retires that old Linux development SDK. The user accepted that Linux will
  establish fresh native dependencies; current Windows functionality was tested
  with the whole runtime detached before deletion.

## Cleanup performed

- Removed `archive/python/`: the old application, launchers, Python-only tests
  and historical packaging implementation. All 81 tracked archive/tool files
  were checked against HEAD before removal; none had local changes. Historical
  source remains recoverable from Git.
- Removed root Python bytecode caches and the orphaned `zima_cad/` bytecode
  directory, plus Python benchmark and Conda Linux pack/unpack scripts.
- Retained current C++ regression fixtures under `tests/fixtures/cross_language`:
  current native tests consume them. Their historical name does not make them
  a Python runtime requirement.
- Replaced the mandatory Conda/OpenBLAS packaging rule with native dependency
  verification. Python may be a developer scripting tool, never an implied
  application runtime dependency.
- Retained `config/`, user `Projects/`, current C++ build outputs and their
  deployed native libraries. Removed the obsolete root `runtime/` tree after
  Windows verification without it (approximately 8.6 GiB). This does not
  publish or delete existing release archives under `builds/`.

- Retired obsolete Python migration/cutover/viewer-removal checklists and the
  old session handoff. Current architecture and enduring behavioral decisions
  are in [CXX_ARCHITECTURE.md](CXX_ARCHITECTURE.md) and
  [NATIVE_BEHAVIOR_CONTRACT.md](NATIVE_BEHAVIOR_CONTRACT.md). Command coverage was
  consolidated and the manual updated to current native behavior. Documentation,
  including retained SVG proposal captions, is English; exact localized UI/test
  literals remain unchanged where their spelling matters.

## Required target package

Use the Parts build ID convention `YYYYMMDDNN` (two-digit daily sequence), with
product-specific names: tag `ZIMA-CAD-2026091501` and
`ZIMA-CAD-2026091501.zip`. These are examples, not an assigned release.
The embedded GUI/CLI build identity, manifest and tag must agree. The root
`VERSION` file replaces `RELEASE_DATE`; GUI, CLI and the package builder consume
that one authority. Do not introduce competing authoritative version values.

```text
ZIMA-CAD/
  ZIMA-CAD.exe                 # small Windows launcher
  ZIMA-CAD.sh                  # Linux launcher
  launcher.ini
  windows/<build-id>/
    zima-cad-cpp.exe
    zima-cad-cli.exe
    <native DLLs and Qt plugins>
    config/                   # immutable factory defaults/templates
    resources/
    licenses/
    version.json
  linux/<build-id>/
    bin/
    lib/
    plugins/
    config/
    resources/
    licenses/
    version.json
  source/<build-id>/           # exact source and initialized submodules
  custom/windows/<build-id>/
  custom/linux/<build-id>/
  config/config.ini           # shared user overrides
  config/windows/config.ini   # Windows directory overrides
  config/linux/config.ini     # Linux directory overrides
  config/templates/
  config/materials/
  config/formats/
  config/localization/
  Projects/
  cache/
  autosave/
  recovery/
  .updates/
```

Each version is complete. Never share mutable Qt/OCCT directories or factory
resources across installed versions. The root launcher selects a validated
version without changing system PATH. A previous version must still start
using its own dependencies after an update. Do not bundle WebEngine or
Ghostscript merely because Parts uses them; CAD's dependency graph decides.

The shared root config, Projects and recovery data are user-owned data.
Updates never overwrite them. Version-specific defaults must be separated from
user changes in the settings/path implementation. Opening an older application
is not a guarantee it can read documents saved in a newer native format:
backward format compatibility remains outside the product contract. Keep
original documents/recovery data and report an unsupported format clearly.

Use the Windows/Linux ZIP and source layout from Parts. Following the user's
2026-09-15 updater request and current ZCP policy, platforms may publish
independently. Combined platforms must share the same source commit. Published
archives are immutable; adding a platform later requires a new build ID.

## Native runtime replacement

### Windows

Continue building against the pinned vcpkg manifest. Package the transitive
native DLL dependencies of both GUI and CLI, Qt platform plugins (including
Windows and offscreen), SVG/image plugins actually used, codecs, fonts and
redistributable compiler runtime as permitted by their licenses. Header-only
nlohmann_json adds no runtime DLL. A developer machine's installed MSVC runtime
must not hide a missing redistributable in the portable acceptance test.

### Linux

Replace the Conda-based development presets with a dedicated native SDK or
version-checked system packages on the policy's supported Debian x86_64 target.
Keep Qt/OCCT versions at or above the requirements in `cpp/CMakeLists.txt`;
do not lower requirements to fit an older system OCCT. Pin exact distro,
compiler and library revisions in build metadata. Verify KDE/Wayland and the
selected Qt platform dependencies. Bundle the required application libraries;
use the supported host's libc and graphics drivers.

The user subsequently authorized removal of the complete obsolete runtime
after verification of the current Windows application. That removal is complete.
Linux must now establish a fresh SDK and prove a new configure/build and GUI/CLI
smoke test without Conda paths; old cached Linux builds are not validated.
No Python interpreter, pythonocc,
Conda package cache or `conda-unpack` belongs in the finished product archive.

## Implementation order

1. Complete the native Linux dependency replacement and prove both platform
   builds are independent of the removed runtime. Windows local checks passed;
   Linux remains assigned to the Linux host.
2. Implement one build identity and a read-only GUI/CLI `--build-info` contract.
3. Add a repository-owned `tools/distribution/` CMake package builder with an
   explicit file allowlist, licenses, source export and dependency closure.
   Keep staging/build outputs outside source, under ignored `.dist-output/` or
   a short external staging path. Never export arbitrary working-directory data.
4. Separate factory defaults from portable user settings; implement the small
   launchers and version selection. Preserve current development launchers
   until their replacement is verified.
5. Validate the complete extracted package and publish only a committed/tagged
   candidate. Development exports must be clearly marked and cannot be
   represented as signed official releases.
6. Add signed update metadata and staged switching after application exit.
   Keep the current and previous verified official versions per OS. Never
   clean custom versions or user data automatically. Retain sources needed by
   every retained official platform version. Define launcher update and signing
   key management before claiming authenticated updates.

## Acceptance gates

- Exact committed source/tag and build identity; clean release input and
  initialized submodules; source archive builds with the documented toolchain.
- Complete native dependencies, plugins and license notices for both programs.
- Safe relative archive paths, no traversal/case collisions, member-length
  budget suitable for normal user extraction, CRC and SHA-256 verification.
- Extract and launch from a normal user directory (including spaces/Unicode),
  without developer Qt, OCCT, Conda, vcpkg or compiler paths on PATH.
- GUI startup, CLI operation, deterministic Part calculation, native
  save/reopen, image/PDF export and configuration persistence.
- Version switching and return to the previous version, plus preservation of
  Projects, user settings, autosaves/recovery and custom builds.
- Authentication verified separately from integrity: hashes alone do not
  establish official provenance. Apply the signature and key policy in
  [Application updates](UPDATES.md).

The earlier cleanup itself produced no distributable or updater. The subsequent
Windows launcher, committed-source builder and settings implementation are
documented in [NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md). Authenticated
updates have since been implemented in [UPDATES.md](UPDATES.md); native Linux
acceptance remains assigned to Linux. See [Windows runtime requirements](WINDOWS_RUNTIME_AND_BUILD.md) and
the [earlier portable layout](PORTABLE_RELEASE.md) for the superseded design.

## Windows verification and Linux handoff

On 2026-09-15, the native Windows GUI and CLI rebuilt successfully after Python
removal and the terminal icon change (`build/terminal-icon-build.log`). The
existing console GUI contract passed 1/1 in 147.28 s
(`build/terminal-icon-tests.log`). These are local development checks, not
portable-package acceptance. The Windows CMake cache and launchers contain no
reference to the old Python runtime.

The user will complete the Linux build from Linux. This Windows host has no
installed WSL environment. Do not install WSL or attempt a Linux release here
for this task. The receiving agent should follow
[LINUX_RELEASE_HANDOFF.md](LINUX_RELEASE_HANDOFF.md).

Final cleanup verification: Windows GUI/CLI rebuild passed with `runtime/`
absent, followed by console UI 1/1 in 139.26 s
(`build/native-only-toolbar-build.log`, `build/native-only-toolbar-tests.log`).
The detached old runtime was then deleted. The screenshot was inspected:
Regenerate is first and the console toggle second above the View.
