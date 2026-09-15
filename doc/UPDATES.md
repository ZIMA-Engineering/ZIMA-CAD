# Application updates

## User workflow

Settings contains **General**, **Updates** and **AI** sections in the existing internal
properties window. Updates shows the running build, retained previous build,
available build, plain-text release notes, download size, progress and errors.

- **Check** requests the public ZIMA-Engineering/ZIMA-CAD GitHub Releases feed.
- **Install and restart** authorizes the offered build, downloads and verifies it,
  confirms Settings through its normal OK action, then starts the installer and
  closes the application. One click completes this sequence; no second download
  confirmation is needed. Save or close unsaved Part, Assembly and Drawing
  documents and finish active model edits first. The guard is checked both before
  downloading and immediately before restarting. A document edited during the
  download blocks restart and leaves the verified build ready for an explicit retry.
- **Return to previous version** verifies the retained runtime and restarts it.
- **Cancel download** stops only the owned download/preparation helper. Cancel
  in Settings discards pending preferences and cancels an active download.
  Closing Settings, including its ordinary OK action, revokes a pending automatic
  restart before deferred widget deletion. Cancel also revokes a restart already
  queued by successful verification. Cached prepared files never authorize a later
  activation; reopening Settings requires another explicit install action.

The startup check is asynchronous, delayed by 1.5 seconds and enabled by default.
Only a verified newer version produces a small link in the status bar. Clicking
it opens Settings directly at Updates. Offline failures and rate limits are
reported inside Settings, without startup dialogs. Checking never installs.
`Updates/CheckAtStartup` belongs to installation-root `config/config.ini`; project
configuration cannot redirect updater preferences, installation ownership,
the repository endpoint or trusted keys. Development uses the repository config.

The daily development entry point remains repository-root `zima-cad.bat`.
Development and unsigned candidate copies can check releases, but cannot install
updates over their working tree. Installation requires a signed portable bundle.
The first signed bundle must be extracted into a new installation directory;
existing unsigned candidates are not silently promoted to official builds.

## Runtime and installation contract

`zima-cad-update` is a native Qt Core/Network executable. It uses OpenSSL 3
Ed25519 verification and the matching Qt Core private ZIP reader. Windows ships
the Schannel TLS backend alongside its existing Qt/OCCT dependency closure.
Python is used only by developer packaging/tests, never by the running updater.

Additional distribution files:

```text
installation.json                         product, protocol 1, installation UUID
release-info/<platform>-<build>.json       signed bootstrap inventories
windows/<build>/zima-cad-update.exe
linux/<build>/bin/zima-cad-update
.updates/instances/<build>-<uuid>.lock     GUI/CLI process lifetime registrations
.updates/install.lock                     launch/activation gate
.updates/downloads/                       bounded temporary downloads
windows/.u<8hex>/ (or linux/.u<8hex>/)     isolated prepared runtime
source/.u<8hex>/                          isolated prepared sources
.updates/ready/<platform>-<build>.json      prepared receipt
.updates/installed/<platform>-<build>.json  installed receipt
.updates/<platform>-journal.json           previous/candidate/committed selection
.updates/<platform>-result.json            last installer outcome
.updates/engine.ini                        last working recovery helper
```

Installation ownership comes from the executable's version directory and the
product marker. Git checkouts, unsafe paths and linked managed directories are
rejected. The helper verifies the full archive but imports only this platform's
new immutable runtime and its matching source directory. It never extracts a
whole update over a live installation or shared user configuration.

Activation waits up to 30 seconds for GUI/CLI registrations to close normally.
It never terminates another CAD instance. A one-use local socket/token and PID
identify the candidate's startup acknowledgement after its main window opens.
The previous helper remains selected for recovery until acknowledgement succeeds.
Failure restores the previous selection; an exited candidate permits restarting
the previous GUI. A slow still-running candidate is preserved and reported.
The root launcher recovers an interrupted selection before ordinary GUI/CLI
launch. Explicit version/custom selection and `-Check` retain their contracts.

After successful startup, keep **current plus previous**, independently per OS.
Delete only older signed runtimes whose complete inventories still match and
whose process registrations are idle. Preserve modified, unknown, custom and
busy versions and report deferred cleanup. Sources survive while either platform
retains that build. Rollback never restores or overwrites user settings/projects;
native-document backward compatibility is not promised. No config migration is
introduced here; future migrations must follow the existing backup contract.

Cancelled/interrupted preparation may leave disposable download/staging files;
they never become an activation request on the next launch. Downloads restart
from the beginning; byte-range resume is not implemented.

## Discovery and authentication

Stable tags are `ZIMA-CAD-YYYYMMDDNN`. Releases are paginated and sorted by build
identity, ignoring drafts/prereleases and foreign tags. The newest compatible
platform release is offered even when another platform has a newer release.
Windows and Linux may publish independently. Once published, assets are immutable;
adding another platform later requires a new build ID.

Each release has three public assets:

- `ZIMA-CAD-<build>.zip`
- `update-manifest.json`
- `update-manifest.sig`

The signed canonical UTF-8 JSON binds product, channel, build/tag, full Git commit,
platform runtime/entry paths, ZIP SHA-256/size/count/unpacked size, source inventory,
file-inventory hash and minimum updater/root-launcher protocol. JSON keys use Qt's
UTF-16 ordering, compact encoding and one final newline. Signature verification
precedes interpretation; duplicate keys/noncanonical encodings are rejected.
Per-file hashes, safe paths, collisions, ZIP sizes and the existing **180 UTF-16
unit archive member budget** are enforced. Limits: fewer than 2 GiB compressed,
50,000 files, 512 MiB per file and 8 GiB unpacked. Windows staging paths must fit
259 characters; use a short installation directory.
Temporary version-directory names have the same length as a build ID, so
preparation preserves the archive's path budget rather than adding a deep prefix.

Requests use HTTPS, a restricted GitHub host allowlist, bounded redirects/timeouts,
ETag caching and rate-limit backoff. A highest-verified-version record detects a
missing or older catalog. Discovery/cache state is disposable application cache
data, not model data or a registry-based preference store. A new check retires the
previous prepared offer; failed rechecks clear the live offer. Downloading rechecks
the release before fetching its archive.

`cpp/update/trusted-keys.json` embeds the same **public publisher key** currently
used by ZCP (`zcp-8fa1f825cbde5d81`). CAD manifests are product-bound and ZCP
manifests are rejected. No private key is copied into CAD, its source or packages.
The publisher can use its existing protected key by explicitly supplying its
local path. New public keys must ship in a version signed by an already trusted
key before rotation. A manifest cannot add trust. The native root launcher is
protocol 1 and is not replaced during ordinary updates; a higher required protocol
requires a new full bundle.

## Publisher procedure

1. Commit the intended source, update `VERSION`, and tag that exact clean commit.
2. Run the repository platform candidate builder in a new short staging directory.
   Keep its archive and `.validation.json` report with passing native smoke checks.
3. Extract a fresh candidate for finalization. Do not use a working installation
   containing projects, shared overrides, caches or other build directories.
4. Run the signing finalizer with the protected publisher key and candidate report:

```text
python tools/distribution/update-release.py finalize --version <build> --package <fresh-ZIMA-CAD-directory> --output <new-assets-directory> --private <protected-publisher-key> --validation <candidate.validation.json>
```

The finalizer requires a clean repository, exact tag/VERSION/commit, byte-identical
Git-exported sources and runtimes/root launcher matching the smoke-tested candidate.
Its key must already be trusted by the packaged source. It creates bootstrap
attestations and signed assets, revalidates archive paths/CRC/SHA-256 and, on Windows,
smokes the finalized signed archive from a fresh extraction. Existing output is
never replaced. `--development` creates isolated test assets that production
discovery rejects; it is not a release shortcut.

**The finalizer never uploads.** Publish the three validated immutable assets only
after reviewing the final validation result. On Linux, complete the native smoke
pipeline and supported-baseline checks described in [the Linux handoff](LINUX_RELEASE_HANDOFF.md).

Optional key maintenance is available as `keygen` and `export-key`. Windows keys
use user-bound DPAPI; encrypted portable PEM backups require an interactive
password. Private material must remain outside tracked/distributable files.

## Verification

Build `zima_update_test_helper` and `zima_update_fixture` with CMake. The test helper
alone accepts localhost HTTP, test keys/state and fixture preparation. These
overrides are absent from production; tests generate disposable Ed25519 keys.

```powershell
$env:ZIMA_UPDATER_TEST_EXE = (Resolve-Path build/cpp-windows-release/zima_update_test_helper.exe).Path
$env:ZIMA_UPDATE_FIXTURE_EXE = (Resolve-Path build/cpp-windows-release/zima_update_fixture.exe).Path
$env:ZIMA_UPDATE_TEST_RUNTIME = (Resolve-Path build/cpp-windows-release).Path
python cpp/tests/test_updates.py
python tools/distribution/test_package.py
python tools/distribution/test_update_release.py
ctest --test-dir build/cpp-windows-release -R zima_cpp_updates_ui_contract --output-on-failure
```

Windows fixtures exercise signed discovery, download, install, rollback, startup
failure/recovery, live-instance blocking, two-version retention, source sharing,
tampering, path rejection, interrupted imports and independent platform publishing.
GUI verification checks the internal Settings page, silent initial state,
Cancel/OK preference semantics, one-action installation, release notes/progress,
exact-build approval, cancellation before/after preparation, closing Settings,
failure/retry, explicit rollback and unsaved-document protection at both ends
of the download. Presentation-only test callbacks drive the actual Qt controls;
production still uses the authenticated helper without test trust overrides. Linux
fixture manifests test protocol handling only; they do not establish Linux runtime
or desktop compatibility. No official update was published during implementation.

Windows verification on 2026-09-15 passed: 22 lifecycle/signature cases plus the
maximum permitted source-path case; seven archive/source tests; seven publisher
input-gate tests; and the native portable-settings, CLI-process and Updates GUI
contracts. A live production-helper HTTPS check against GitHub returned `current`.
Logs: `build/updates-accepted-engine.log`, `build/updates-accepted-longpath.log`,
`build/updates-accepted-package.log`, `build/updates-accepted-publisher.log`,
`build/updates-accepted-native.log` and `build/updates-live-check.log`.
The GUI capture `Projects/test/updates-settings.png` was visually inspected.

The one-action UI follow-up passed all 23 lifecycle/signature cases, seven
archive/source tests, seven publisher input-gate tests and five native contracts
(Updates GUI, shared dialogs, translations, portable settings and CLI processes).
The final Updates GUI rerun also covers retry after Settings validation and
immediate cancellation of an already queued restart. Evidence:
`build/updates-release-engine.log`, `build/updates-release-package.log`,
`build/updates-release-publisher.log`, `build/updates-one-action-native.log` and
`build/updates-one-action-ui.log`.

The first signed Windows build, `2026091504`, passed the committed-source builder,
signing finalizer and finalized-archive Windows smoke. In a fresh extraction the
production helper reported `trusted: true`; live GitHub discovery returned
`current`, and the packaged Updates GUI contract passed. The three immutable
assets were published with user approval on 2026-09-15 at 14:11:02 UTC:
[Windows release 2026091504](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091504).
GitHub asset names, sizes and digests matched the accepted local files. The
production helper subsequently verified public discovery from a disposable older
version selection and returned `available` for `2026091504`
(`build/updates-public-release-04.log`). See
[signed build acceptance](NATIVE_DISTRIBUTION.md#signed-windows-build-2026091504)
for hashes, locations and the distinction from an upgrade between two installed
production releases.

The following Windows release,
[2026091505](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091505),
was published with user approval on 2026-09-15 at 15:00:15 UTC. Its candidate and
final signed archive passed the same native smoke and archive gates; a fresh
extraction also passed the packaged AI/Updates Settings contract and production
bootstrap verification. Remote asset hashes matched the accepted local files.
The production updater from signed version `2026091504` verified the public
manifest, offered `2026091505` as installable and downloaded/prepared the public
archive in a disposable installation. Shared settings, the test project and the
selected previous version were preserved. This public test did not activate or
roll back the prepared update. Exact archive identity and logs:
[signed build acceptance](NATIVE_DISTRIBUTION.md#signed-windows-build-2026091505).

Windows release
[2026091508](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091508)
was published on 2026-09-15 at 18:00:05 UTC after both candidate and signed-archive
checks passed. A fresh extraction passed production bootstrap trust. The updater
in a disposable signed `2026091505` installation verified its public manifest and
offered `2026091508` as installable; the new version reported `current`. This was
public discovery verification, without activating or rolling back an update.
The three published asset hashes matched local acceptance. See
[release acceptance](releases/2026091508.md#signed-windows-acceptance).
