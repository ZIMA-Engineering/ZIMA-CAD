# Portable release distribution

ZIMA-CAD will be distributed primarily as one self-contained portable archive
for Linux and Windows. After extraction, the user starts `zima-cad.sh` on
Linux or `zima-cad.bat` on Windows. A normal installation, system-wide runtime
or manual directory preparation must not be required.

The first portable package is planned as the next near-term release target,
approximately for the beginning of September 2026. This date is a working
target; the package may be published only after both packaged-runtime smoke
tests and archive validation pass.

## Archive layout

The release archive owns the complete directory structure needed by the
application:

```text
ZIMA-CAD/
|-- zima-cad.sh
|-- zima-cad.bat
|-- linux-x86_64/
|   |-- bin/
|   `-- runtime/
|-- windows-x86_64/
|   |-- bin/
|   `-- runtime/
|-- shared/
|   |-- defaults/
|   |-- translations/
|   |-- templates/
|   |-- materials/
|   |-- icons/
|   `-- documentation/
|-- profile/
|   |-- common/
|   |-- linux/
|   `-- windows/
|-- Projects/
|-- README.md
|-- RELEASE_DATE
|-- MANIFEST.json
`-- SHA256SUMS
```

The exact internal runtime subdirectories may evolve, but these ownership
boundaries are part of the distribution contract.

## Settings and user data

Portable mode is the default for the distributed archive. All non-secret
ZIMA-CAD settings and all application-created directories live inside the
extracted package; the application must not depend on hidden Linux user
configuration or the Windows Registry for its primary configuration.

- `shared/defaults/` contains versioned factory defaults and is read-only at
  runtime.
- `profile/common/` contains portable user choices such as units, colours,
  dimension and Sketcher preferences, shortcuts and template selection.
- `profile/linux/` and `profile/windows/` contain platform-specific state such
  as window geometry, renderer choices, file-dialog locations and absolute
  recent-file paths.
- `Projects/` is the default portable project workspace.
- Cache, autosave and recovery directories are created predictably within the
  portable data layout when first needed.
- Credentials, private keys and other secrets are not release-archive content.

Application updates may replace launchers, runtimes, shared resources and
factory defaults. They must preserve `profile/`, `Projects/`, autosaves and
recovery data. Missing working directories are created by the launcher or the
application on first start.

## Release form

Prefer one archive containing both platform runtimes and the shared data. If a
hosting or practical download-size limit makes that unsuitable, publish two
complete platform archives with the same root layout and settings contract.
Neither form may require users to assemble runtime and data archives manually.

Public archives use the product name and ISO release date so they sort
chronologically and remain understandable without a separate version lookup:

```text
ZIMA-CAD-YYYY-MM-DD.zip
```

The canonical value is stored in the repository-root `RELEASE_DATE` file. A
release has one combined public identity, `ZIMA-CAD-YYYY-MM-DD`; it is not
presented as an unrelated semantic version beside a date. The ZIP filename,
the application's **O aplikaci ZIMA-CAD** window and the manifest must all use
that same combined identity. Release preparation changes `RELEASE_DATE` before
the clean build; packaging must reject a mismatched archive name, embedded
identity or manifest.

For example, the planned first archive would be named
`ZIMA-CAD-2026-09-01.zip` if it passes release validation on that date. A
replacement published on the same date uses an explicit revision suffix such
as `ZIMA-CAD-2026-09-01-r2.zip`; an existing published archive is never
silently overwritten. If separate platform archives are required, the suffix
precedes the date:

```text
ZIMA-CAD-Windows-x86_64-YYYY-MM-DD.zip
ZIMA-CAD-Linux-x86_64-YYYY-MM-DD.zip
```

Windows packaging and acceptance additionally follow
[`WINDOWS_RUNTIME_AND_BUILD.md`](WINDOWS_RUNTIME_AND_BUILD.md). Release archives
must be produced from committed Git data and pass dependency, path, collision,
CRC, SHA-256, extraction, startup and deterministic calculation checks before
publication.
