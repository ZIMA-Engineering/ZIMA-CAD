# Native Unicode and temporary-path I/O - 2026-10-20

## Reproduced defects and bounded correction

The independent factory checker accepted UTF-8 path strings but received ANSI
`main()` arguments on Windows. Its first Unicode-directory audit failed before
loading any model. Its Windows entry now converts the OS UTF-16 arguments to
UTF-8, as the existing native CLI does; Linux retains the same shared verifier
body. Report paths and authored Drawing names use `document::path_to_utf8`.
The checker keeps its existing native dependencies; Qt is not added.

After correcting the checker, regeneration of a CJK-named copy exposed a real
product defect: Part and Assembly writers constructed `.tmp` through
`path.string()`. That conversion can fail for names outside the Windows ANSI
code page. They now append the same suffix to the native filesystem path and
open the native path directly. Existing read/write error prefixes are unchanged;
the authored path in the error is UTF-8.

A separate real Save As GUI reproduction reaches another boundary: its valid
256-character staged target becomes 260 characters after `.tmp`, before the
terminating null. The original path is within the classic Windows limit, but
its private temporary write is not. `native_file_io_path` adds the Windows
extended-length spelling only at I/O boundaries for paths requiring it. Short
paths retain their original spelling; the boundary is 248 native code units so
directory creation also has the legacy child-name allowance. Existing extended/device spellings pass
through. UNC uses its corresponding extended spelling. Linux returns the
original path. Neither the added namespace prefix nor an I/O-normalized name
is persisted or used as a document/reference identity.

Part/Assembly streams and temporary renames, Drawing streams and archive
filesystem operations consume that internal I/O spelling. Archive queries still
return the caller's logical directory/name. Existing backup numbering, output
bytes, document ownership and file extensions remain unchanged. No native schema,
factory geometry or start-template conversion is required. This is filesystem
integration, not a separate modeling implementation.

The next deep GUI run also exposed the Save As transaction's private
`.zima-copy-<id>` directory exceeding the directory limit, before any document
write. Copy preflight, staging, atomic hard-link publication, rollback, cleanup
and companion-Drawing discovery now consume the same I/O spelling. Published
paths and discovered companions retain their logical directory/name, so an
internal namespace prefix cannot leak into persisted dependencies.

## Evidence and finite checks

- `build/checker-unicode-baseline.log`: the original native checker rejects a
  Unicode working directory.
- `build/native-unicode-write-baseline-ucn.log`: the original Part writer rejects
  an actual CJK native path. Test paths use universal-character escapes and
  metadata uses explicit UTF-8 bytes: the non-Qt MSVC target must not silently
  reinterpret UTF-8 source text through its ANSI source code page.
- `build/native-unicode-io-fixed-contracts.log`: Native Documents, Archives and
  Save/physical-analysis contracts pass (three suites, 1.85 seconds). Part,
  Assembly and Drawing byte comparisons, Unicode names/identity, overwrite
  archives and reopening pass. A pre-existing unavailable temporary directory
  leaves the original bytes, archive count and that directory unchanged.
- `build/native-unicode-io-dependency.log`: calculated Part data and reference
  caches remain byte-identical across paths; Assembly occurrence/source identity
  and Drawing source identity survive actual CJK native dependencies.
- `build/native-checker-unicode-final.json`: the independent checker regenerates,
  saves and reopens a CJK-named native copy and emits its exact UTF-8 name in JSON.
- `build/unicode-save-copy-deep.log`: the first native-path-only repair still
  rejects the deep Save As case. Extended temporary/archive path verification
  is in progress; this failed attempt is retained rather than counted as a pass.
- `build/unicode-save-copy-deep-fixed.log`: the next attempt reaches the private
  copy-staging directory limit. It is retained as a failed intermediate attempt;
  the final native and GUI copy checks follow the complete I/O correction.

`build/long-copy-native-tests.log` passes all four focused suites in 3.54 seconds:
Workspace, Native Documents, Archives and Save/physical analysis. The final native
matrix covers 259-code-unit targets for all three document types, longer temporary
and backup names, backup reopening and Part/Assembly failed-temporary-write
preservation. Pure Windows spelling checks include the 248-unit directory boundary,
relative paths, UNC and explicit device namespaces. The Workspace matrix creates
a 249-unit parent directory, publishes a 259-unit Unicode Part/Drawing pair, rejects
its existing-name collision, creates a Drawing-originated pair, and verifies logical
source identity, unchanged originals and complete private-staging cleanup.

`build/unicode-save-copy-deep-final.log` passes the actual deep GUI reproduction
and all eight paired naming policies after the complete correction. It exercises
Part/Assembly/Drawing Save Copy and Unicode GUI/native-command file workflows.
Both failed intermediate attempts above remain failures in their original logs.
Fresh committed-package acceptance is still pending. No new
product UI text or translations are introduced by the I/O correction; paths and
user-authored names are preserved. Native Linux execution and real network UNC
share I/O remain unverified on this Windows host.
