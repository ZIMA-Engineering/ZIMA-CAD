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
