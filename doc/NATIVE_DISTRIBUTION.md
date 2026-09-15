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
and verified on Linux. Signed updates, restart coordination, startup
acknowledgement, recovery and two-version retention are implemented; see
[Application updates](UPDATES.md) for the UI, signing and acceptance procedure.
Neither the scripts nor a successful local build publish a release.

## Installed layout

```text
ZIMA-CAD/
  ZIMA-CAD.exe
  ZIMA-CAD.sh
  launcher.ini
  installation.json
  release-info/
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
    zima-cad-update.exe
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
To retry a package check, pass `-ReuseBuild` with the original `-Stage` and the
desired `-Commit`. The builder first compares every staged source file against
its previously recorded Git commit and rejects modified or unknown files. It
then exports the selected commit, preserves unchanged source timestamps and
lets CMake rebuild changed inputs. Assembly/extraction use new directories.
It never overwrites an old archive.

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

The builder deploys GUI, CLI and updater executables, transitive DLLs, Schannel TLS, Windows/offscreen and
image/style plugins, OCCT resources, licenses and exact committed source.
Validation enforces a 180 UTF-16-code-unit archive member budget (including the
top-level folder), safe paths, case collisions, file/size limits, CRC and SHA-256.
Extraction uses a directory with spaces and non-ASCII characters and removes
developer SDK locations from PATH. Smoke checks cover build identity, a
10 x 20 x 30 mm Part (6000 mm3), save/reopen, PDF/JPEG and a GUI startup smoke
that creates a Part from the packaged template, calculates/saves a Box and
exports the rendered View. This bounded package check does not require the
repository-wide interactive console test's fixture paths. Only then is the
candidate copied to the requested output directory.
The SHA-256 and validation report are stored beside the ZIP.

```powershell
python tools/distribution/test_package.py
python tools/distribution/package.py validate path/to/ZIMA-CAD-2026091501.zip
```

Hashes establish integrity, not publisher authentication. The signing finalizer
and authenticated updater are described in [Application updates](UPDATES.md).
Never merge by extracting a new whole archive
over an existing installation. Import only a validated new version/source pair;
switch selection after application exit and preserve all shared user data.

## Remaining update and Linux acceptance

- Build/verify the native Linux dependencies and launcher on the Linux host.
- Platforms may publish independently. If combined, their commits must match;
  published archives are immutable and adding a platform later requires a new ID.
- The first signed Windows distribution is prepared and verified (see below).
  Publishing the three immutable assets remains a separate release action.
- Run the updater's Linux execution and desktop acceptance on Linux.
- Binary rollback does not promise backward compatibility of native documents.

See [binding requirements](DISTRIBUTION_CLEANUP_PLAN.md) and
[Linux handoff](LINUX_RELEASE_HANDOFF.md).

## Windows verification on 2026-09-15

### Signed Windows build 2026091504

Tag `ZIMA-CAD-2026091504` identifies commit
`5ef3ea0fc8ada37954b1dca47e40b2acf5d6f9d9`. Its clean, detached checkout supplied
the release builder; unrelated working files did not enter the source snapshot.
The candidate and finalized signed archive both passed the native Windows smoke,
archive path, CRC and SHA-256 gates. The publisher used the existing protected
local key; no private key was exported or included in the package.

Prepared assets are under `.dist-output/release-2026091504/`:

- `ZIMA-CAD-2026091504.zip` (70,087,247 bytes).
- `update-manifest.json` and `update-manifest.sig`.
- `ZIMA-CAD-2026091504.validation.json` records acceptance and is not a required
  public update asset.

Signed ZIP SHA-256:

```text
65827c71e1df697d06b0dea0e67f68666b1713873789624d7224d0d5415bddba
```

A fresh extraction passed the production helper's bootstrap verification
(`trusted: true`, installed version `2026091504`), a live HTTPS discovery check
(`current`), and the full Updates GUI interaction contract. No public date-based
release was available, so this does not claim a live public upgrade between two
production releases; signed lifecycle fixtures cover installation and rollback.

Logs: `build/updates-package-04.log`, `build/updates-signed-04.log`,
`build/updates-signed-04-acceptance.log`. The candidate View capture was visually
inspected and copied to `Projects/test/release-2026091504-view.png`. Release notes:
[2026091504](releases/2026091504.md). With the user's approval, all three assets
were published on 2026-09-15 at 14:11:02 UTC as the latest stable
[GitHub release](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091504).
Their remote names, sizes and SHA-256 digests matched the accepted local assets.
The production updater then verified the public manifest and reported `available`
from a disposable older-version selection; see `build/updates-public-release-04.log`.
This was discovery only, not replacement of a user's running installation.
Development still starts through repository-root `zima-cad.bat`;
the first signed portable bundle must be extracted into a new installation root.

### Earlier unsigned candidate

Validated candidate: `2026091503`, built from commit
`3d7eaaf89bdd6a8a7f47081ae587c2a8c449d6cc`.
The local ZIP is `.dist-output/candidate-2026091503/ZIMA-CAD-2026091503.zip`
(66,880,593 bytes). SHA-256:

```text
b6a8c804a457ddf2dd36e2f89d4ca1afd0da1775c0bd4ca88e8d082b39a0ad8e
```

Passed checks:

- Seven Python source/archive/environment validation tests.
- Native portable settings test: GUI/CLI layering, user overrides, numbered
  backups, version switching and rollback after a failed common-config save.
- Existing standalone CLI process suite, including native documents, Unicode,
  settings, streaming and explicit calculation.
- Committed-source Windows build, dependency deployment, archive paths, CRC and
  SHA-256, extraction with spaces/diacritics, and runtime/source immutability.
- Packaged GUI/CLI identities, launcher stdout/stdin pipes, 6000 mm3 Part
  save/reopen, PDF/JPEG exports, and actual GUI View rendering. The rendered Box
  was visually inspected. Shared user configuration remained unchanged.
- A local unpacked installation at `.dist-output/portable/ZIMA-CAD` contains
  current `2026091503` and previous `2026091502`. Both started through the same
  native launcher with correct JSON output and shared configuration preserved.

Evidence logs: `build/version-package-final-03.log`,
`build/portable-settings-tests.log`, `build/version-cli-tests.log`,
`build/version-layout-final-build.log`. The per-archive `.validation.json`
records its commit, hash and Windows smoke result. Artifacts/logs remain local
and ignored by Git. No release/tag was published, no signing key was created,
and no Linux runtime or automatic updater was verified by that earlier candidate
run. Subsequent updater implementation and verification are in [UPDATES.md](UPDATES.md).

Earlier local candidates remain available as ZIPs. The prepared runnable
installation keeps the current and previous versions; custom/user directories
were not pruned. Package test staging is disposable and is not the user's data.
