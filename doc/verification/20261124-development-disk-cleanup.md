# Development disk cleanup, 2026-11-24

At the user's request, obsolete Windows packaging copies were removed after
checking their resolved paths, process use and recoverability. Active SDKs,
the reusable release build stage, user configuration, native project documents
and project recovery backups were preserved.

Removed extracted staging copies:

- `C:/zcb/sign2303-input`: approximately 3.04 GiB.
- `C:/zcb/pub2303`: approximately 3.04 GiB.
- `C:/zcb/zc2103-release-tree`: approximately 1.34 GiB, removed as a clean
  Git worktree through `git worktree remove` without forcing removal.

The following local ZIPs, totalling 3,969,715,988 bytes, were also removed:

- `.dist-output/2026112301-signed/ZIMA-CAD-2026112301.zip`
- `.dist-output/2026112302-public/ZIMA-CAD-2026112302.zip`
- `.dist-output/2026112206-signed/ZIMA-CAD-2026112206.zip`
- `.dist-output/2026112303-public/ZIMA-CAD-2026112303.zip`

The first three matched the SHA-256 values and successful public-download
checks in their committed publication records. The last was byte-identical
to the retained latest signed 2303 archive. Validation reports, manifests and
signatures remain available. Cleanup did not change or remove GitHub releases.

Total reclaimed space is approximately 11.1 GiB. After concurrent development
builds, the observed free space was 14.54 GiB. This is a point-in-time measurement,
not a guarantee about later disk use. Local inventories and digest evidence are
under `build/fem-20261124/cleanup-inventory.json` and `cleanup-archives.json`.

## Superseded 2401 candidate staging

Before preparing 2402, the verified disposable assembly/extraction directory
`C:/zcb/r2002/p-4nlgjo2l` was removed, reclaiming 7,546,362,909 bytes. Its candidate
ZIP and validation evidence were retained and its archive SHA-256 checked before
deletion. The clean `C:/zcb/zc2401-release-tree` worktree was also removed through
Git after checking its status; the source commit and local tag remain available.
These were temporary release inputs, not user projects or SDKs. The reusable
release build stage and protected Toyota backup remain intact.

## 2402 packaging preparation

Another 1,983,468,189 bytes were reclaimed from the three local 2103 publication
assets and the unsigned 2303 candidate ZIP. Each 2103 asset matched the size and
SHA-256 reported by its published GitHub release. The 2303 candidate matched its
validation record, and the newer signed 2303 ZIP was retained after checking its
SHA-256. Local evidence is in
`build/release-2026112402/cleanup-old-archives.json`.

Before reusing the compilation stage, all 10,167 exported source files matched
the recorded 2401 Git archive, including Windows line endings. The 146 additional
files were generated FEM/sheet test outputs; they were preserved outside the
source export in `C:/zcb/r2002/previous-test-artifacts-2401`. The repository's
packager then performed its own source verification before refreshing to 2402.

After all twelve additional 2402 packaged GUI cases passed, the disposable smoke
extraction, duplicate staging ZIP and superseded unsigned 2401 ZIP were removed,
reclaiming 5,273,490,365 bytes. The accepted 2402 candidate ZIP and previous signed
2303 release remained available. The paths, identities and retained archive
digest were checked before removal; the evidence is in
`build/release-2026112402/cleanup-before-sign.json`.

## Final cleanup after publication

After public download, signature, updater and local installation acceptance,
the disposable 2402 assembly, public extraction and two redundant ZIPs were
removed, reclaiming 8,520,244,266 bytes. The clean release worktree was removed
through Git, reclaiming another 1,411,136,938 bytes. All recursive targets were
resolved inside their explicit staging roots and checked for unexpected links
and running processes before removal.

The signed 2402 ZIP remains in `.dist-output/2026112402-signed`, with SHA-256
`48a2e3733d95d66bbfae9d77a0b146e29e0d103dc83cb7bb58632beb691e3e8e`.
The preceding signed 2303 release, protected Toyota backup, independent verified
Family copy, SDKs and reusable compilation stage are also retained. Validation
records and signatures remain available for the removed archive copies.

Observed free space after this cleanup was 13,477,089,280 bytes (13.48 GB,
12.55 GiB). This is a point-in-time measurement. Evidence is in
`build/release-2026112402/cleanup-after-publication.json` and
`build/release-2026112402/cleanup-release-worktree.json`.
