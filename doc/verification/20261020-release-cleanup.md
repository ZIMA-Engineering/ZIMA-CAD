# Windows release workspace cleanup — 2026-10-20

The user authorized cleanup of generated working directories and specifically
`C:/zcb`. Cleanup was performed during Windows 2026102003 preparation after disk
exhaustion interrupted disposable source/export and archive-extraction attempts.
Failed attempts remain failures in their original logs; cleanup does not replace
the normal complete archive, native smoke or signed-release acceptance gates.

## Preserved inputs

- The main working repository, user configuration and metadata, `Projects`, custom
  libraries, unfinished cast-wing/toothed-washer studies and reference artwork.
- Current development executables, linked libraries, native test executables,
  vcpkg dependencies and the Qt 6.11.0 SDK at `C:/zcb/qt611`.
- The complete latest released 2002 verification installation and signed ZIP.
  Automatic approval review had rejected deleting that installation's source
  copy; this cleanup did not retry or bypass that exclusion.
- Rejected 2001 diagnostics, candidate/signed validation receipts and the active
  2003 source, candidate and signing inputs until their verification is complete.
- The user's pressed M10 Part remains at SHA-256
  `5aeb36fda9f7af13af74dff9cfca9a680b9f2cc43830130494d484904eaaed73`.
  The current `Projects/31.prtz` remains at
  `a3590ea7a19ebfadb3b25b442b5463e59c15b8475236ac0c7fb9ac2ada4dd2b8`.

## Completed cleanup

Every recursive operation checked its resolved absolute boundary and rejected
reparse points. Native PowerShell file operations stayed in one shell. Historical
compiler/checker copies were confirmed unused before removal. JSON receipts in
`build` record the exact paths and sizes.

| Generated data | Logical bytes removed | Independent scope check |
| --- | ---: | --- |
| Remaining local compiler intermediates | 1,950,443,292 | Only `.obj`, `.pdb`, `.idb`, `.ilk` under the existing `CMakeFiles`; binaries/libraries retained |
| Historical knurled-nut reopen copies | 1,130,289,604 | Exactly 48 `COPY-N.prtz`/`.drwz` files in two completed test folders; original models, successful results and logs retained |
| Repeated checker executables | 424,436,736 | 59 byte-identical generated duplicates; one of every 24 historical binary hashes and the canonical checker retained |
| Superseded local 1906 release ZIP | 915,813,591 | Exact published manifest/public-download SHA-256 and size; newer 2002 rollback package retained |
| Clean obsolete `C:/zcb/src-1703` clone | 1,290,532,131 | Clean Git status; commit `f179b5939bf2eaeb8e48a848fbcbd674972231a4` retained in main Git; all ignored native test evidence moved intact to `build/cleanup-20261020-src1703-test-evidence` |

These rows total 5,711,515,354 logical bytes. This is not an observed free-space
delta: NTFS compression, allocation and concurrent user activity affect it.
The large old import diagnostics were also losslessly NTFS-compressed, preserving
2,525,357,569 logical bytes in 1,262,839,390 reported stored bytes.

The completed 2003 build staging tree was removed only after dependency closure,
clean build identity and all three staged EXE hashes matched the retained runtime.
Incomplete extraction attempts were removed only after every partial file was
identified as a member of the retained valid archive. A later full extraction and
all standard smoke checks passed, including 1,185 factory document loads. That
completed disposable extraction was then removed, retaining the validated ZIP,
report, sample native documents, rendered screenshot and exports. These temporary
release copies are accounted for separately in `release-2003-*-cleanup.json`.

Finally, 86 old distribution receipt folders containing only JSON/SIG files were
moved into `.dist-output/_history`. No release asset was rewritten. The active
2003, rollback 2002 and rejected 2001 folders remain separate. Moving these small
receipts improves directory organization; it is not a material disk-space saving.

No product UI or localization text changed in this cleanup. The normal repository
`zima-cad.bat` remains unchanged. Future incremental builds may recompile the
removed object files. Native Linux verification is outside this Windows cleanup.
