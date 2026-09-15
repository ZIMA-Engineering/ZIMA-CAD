# Native versioned distribution

## Scope and status

The approved layout uses a shared root `config/`, replacing the earlier
`profile/` proposal. The root `VERSION` file is the single build ID authority
(`YYYYMMDDNN`, daily sequence 01-99). CMake embeds it in GUI and CLI;
`--build-info` prints product, version, commit, modified-source flag, platform,
Qt/OCCT versions and compiler without creating a window or reading settings.
The CLI also supports `--version`. The About window displays the same ID.

Windows has a native launcher and a committed-source candidate builder. Linux
has the equivalent launcher source; its runtime and execution must be completed
and verified on Linux. Authenticated network updates, restart coordination,
startup acknowledgement, automatic rollback and retention are subsequent work.
Neither the scripts nor a successful local build publish a release.

## Installed layout

```text
ZIMA-CAD/
  ZIMA-CAD.exe
  ZIMA-CAD.sh
  launcher.ini
  LICENSE
  checksums.json
  config/
    config.ini
    windows/config.ini
    linux/config.ini
    templates/ materials/ formats/ localization/
  windows/<build-id>/
    zima-cad-cpp.exe
    zima-cad-cli.exe
    *.dll
    plugins/
    qt.conf
    resources/
    config/
    licenses/
    build.ini
    version.json
  linux/<build-id>/
    bin/ lib/ plugins/ resources/ config/ licenses/
    build.ini
    version.json
  source/<build-id>/
  custom/windows/<name>/
  custom/linux/<name>/
  Projects/
  autosave/
  recovery/
  cache/
  .updates/
```

Launchers create missing user directories on first use; Windows-only candidates
have no Linux runtime. Empty directories need not appear as ZIP entries. The
package contains no pre-existing user settings, projects or update transactions.
Runtime/config resources come from the selected commit. Every version owns its
dependencies. Root user data is independent of the selected executable.

## Configuration ownership and versioning

Repository `config/` is already tracked in Git, including native start templates.
The package builder copies that committed configuration into each version's
factory `config/`. Do not relocate or delete the source repository's configuration.

Installed GUI and CLI read these layers, in increasing priority:

1. Executable version's factory `config/config.ini`.
2. Installation root `config/config.ini` (common user overrides).
3. Installation root `config/windows/config.ini` or `config/linux/config.ini`.
4. Existing project `config.ini`, preserving the project override contract.

The executable location establishes the portable installation; the project or
current working directory cannot impersonate its root. An explicit CLI
`--config` retains its documented base-file override behavior.

GUI global preferences save to the common file. Changed directory paths save
to the OS-specific file; relative paths belong to the file supplying them.
Unchanged inherited paths are not saved as overrides: switching versions must
use the new version's factory templates/resources. Project-specific settings
continue to save to the project file. WorkingDirectory defaults to installation
`Projects/`; development defaults to the repository's `Projects/`.

User resource directories start empty. A custom resource library is selected
through the corresponding directory setting. Until then, the program reads the
selected version's factory library. This is directory selection, not an implicit
per-file merge of template/material libraries. Put editable copies in the shared
user directories; do not edit the factory library in a version directory.

Existing numbered INI backups (`config.ini.1`, `.2`, ...) are preserved. Before
any future configuration format conversion, the updater must create a complete
snapshot under `.updates/config-backups/<old-build>/<transaction>/`, with a file
inventory and hashes. Restore must be explicit and preserve edits made since
the snapshot. Binary rollback must not silently replace current user settings.
No configuration schema conversion is currently required or implemented.

## Windows launcher

`launcher.ini` selects Windows/Linux independently:

```ini
[launcher]
windows=2026091501
windows_custom=false
linux=
linux_custom=false
```

Examples (build IDs are illustrative):

```powershell
./ZIMA-CAD.exe
./ZIMA-CAD.exe -Version 2026091501
./ZIMA-CAD.exe -Custom -Version experiment
./ZIMA-CAD.exe -Check
./ZIMA-CAD.exe -CLI -- --build-info
```

Additional application arguments are forwarded. `-Check` validates the selected
build manifest and executable and prints the path without launching or creating
user directories. The launcher uses the static MSVC runtime and its child's
private DLL/plugin paths; it does not change system PATH. Explicit version
selection does not edit `launcher.ini` and does not delete any other version.

## Build a committed Windows candidate

First build dependencies with `tools/build-windows.ps1`. Commit the intended
source and updated `VERSION`. Use a new short staging directory; neither an
existing staging tree nor an existing output ZIP is overwritten.

```powershell
./tools/distribution/build-windows.ps1 `
  -Python C:/Tools/Python/python.exe `
  -Stage C:/zcb/candidate-01 `
  -Output ./.dist-output/candidate-01
```

The script exports Git blobs, recursively exports recorded submodule commits,
builds with CMake in the staging tree and uses the pinned vcpkg dependencies.
The existing native SDK is consumed with `VCPKG_MANIFEST_INSTALL=OFF`; packaging
must not reinstall or mutate the developer SDK. Provision/verify dependencies
separately before invoking the candidate builder.
Python is only a developer tool; no interpreter is included in the product.
Uncommitted changes and untracked files never enter the candidate. A regular
candidate may be built from a selected commit while unrelated work is present.
`-Release` additionally requires a clean checkout and a matching
`ZIMA-CAD-<build-id>` tag; it still produces an unsigned, unpublished candidate.

The builder deploys both executables, transitive DLLs, Windows/offscreen and
image/style plugins, OCCT resources, licenses and exact committed source.
Validation enforces a 180 UTF-16-code-unit archive member budget (including the
top-level folder), safe paths, case collisions, file/size limits, CRC and SHA-256.
Extraction uses a directory with spaces and non-ASCII characters and removes
developer SDK locations from PATH. Smoke checks cover build identity, a
10 x 20 x 30 mm Part (6000 mm3), save/reopen, PDF/JPEG and the existing GUI console
contract. Only then is the candidate copied to the requested output directory.
The SHA-256 and validation report are stored beside the ZIP.

```powershell
python tools/distribution/test_package.py
python tools/distribution/package.py validate path/to/ZIMA-CAD-2026091501.zip
```

Hashes establish integrity, not publisher authentication. Signing keys, signed
metadata verification and the updater must be implemented before advertising
automatic authenticated updates. Never merge by extracting a new whole archive
over an existing installation. Import only a validated new version/source pair;
switch selection after application exit and preserve all shared user data.

## Remaining update and Linux acceptance

- Build/verify the native Linux dependencies and launcher on the Linux host.
- Assemble matching platform commits before publication; published archives
  are immutable and adding a platform later requires a new release identity.
- Define publisher key ownership, trusted keys and signed release metadata.
- Implement staging, instance coordination, restart/startup acknowledgement,
  recovery after interruption and explicit rollback.
- Retain current/previous verified official builds per OS and required source;
  preserve modified/custom builds and user data.
- Binary rollback does not promise backward compatibility of native documents.

See [binding requirements](DISTRIBUTION_CLEANUP_PLAN.md) and
[Linux handoff](LINUX_RELEASE_HANDOFF.md).
