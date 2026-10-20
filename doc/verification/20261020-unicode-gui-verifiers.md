# Unicode paths in GUI verification helpers - 2026-10-20

The final real-user-directory run of signed candidate 2026102003 failed in the
external-conic-center verifier at its first fixture open. The native fixture had
been written successfully under `ZIMA ověření 2003`, but the verifier passed a
different path to Qt and displayed an error dialog. The user dismissed it; the
process exited with failure. That candidate was rejected before publication.

## Cause and bounded correction

The affected test helpers converted native Windows paths with `path.string()`
and passed those bytes to `QString::fromStdString`, which expects UTF-8. The
machine's native narrow encoding did not preserve the Unicode path through that
conversion. Earlier ASCII staging-directory runs had not exposed the mistake.

Eight helpers now use the existing shared `document::path_to_utf8` for Qt paths:
external conic centers, external circular profiles, command guidance, midpoint
profiles, equal circles, sheet transitions, the sheet FORM verifier and the
expanded Slot verifier. Their
native source-path inputs use `std::filesystem::u8path` for UTF-8 values obtained
from Qt. The equal-circle helper also reads its optional source through Qt's
Unicode environment API instead of the narrow CRT environment API.

The patch changes only diagnostic path handling and capture paths. It does not
change modeling algorithms, numerical tolerances, native document contracts,
container placement, ordinary file-opening behavior or translated application
text. No factory model conversion or start-template regeneration is required by
this diagnostic correction. The previously approved format/library checks remain
part of the fresh candidate 2004 acceptance.

## Evidence and required acceptance

- `build/release-2003-functional-acceptance.log` and
  `build/release-2003-acceptance/external-conic-centers.*.log` retain the failed
  real packaged test. Its earlier candidate/signed archive smoke passes do not
  turn this failure into a passing result.
- `build/release-2004-diagnostic-object-build.log`: the main-workspace object
  compiled. The GUI EXE remained unchanged. A dynamic build dependency also
  rebuilt the updater; its post-build deployment attempted to copy Qt Core and
  Network DLLs, which Windows correctly refused while they were in use. This
  is not accepted as a complete runtime deployment. Subsequent GUI checks use
  the separately named local `zima-cad-slot-check.exe`, linked without runtime
  deployment; the normal development EXE and launcher are preserved.
- `build/release-2004-localization-native.log`: the existing five-language
  translation contract passes. A prior restricted attempt could not create its
  temporary fixture and is not accepted as a test result.
- All affected main-workspace and console-verifier code compiles. The conic
  centre matrix passes 12 cases in both the ASCII test directory and
  `build/diagnostic-path-checks/Unicode ověření`.
- `build/real-unicode-verifiers.log` and `build/ověření GUI 検査/results.json`
  pass Slot, circular profiles, guidance, midpoint profiles, equal circles,
  transition thickness and factory FORM checks with actual Czech/CJK working
  directory names. Original fixture bytes remain unchanged. They retain the
  existing dimension/edit/drag equations, reference identity, Undo/Redo and
  native reopening assertions. Slot also checks its five live prompt languages.
- An earlier combined run used an ASCII working directory with Unicode source
  paths. Its six successful focused cases were retained, but it did not alone
  establish Unicode working-directory behavior. Its FORM-only flag also needed
  the outer `ZIMA_VERIFY_CONSOLE_ONLY` entry; otherwise the full unrelated startup
  suite ran. The corrected focused FORM run passes. That accidental run exposed
  a real deep Save As temporary-path boundary, recorded separately in
  [native Unicode I/O](20261020-native-unicode-io.md).
- The independent native library checker required a separate Windows wide
  command-line entry and UTF-8 report paths. A real CJK-named regeneration then
  exposed ANSI temporary-path conversion in the product writer; those bounded
  native I/O corrections and their finite evidence are in the same record.

The corrected build uses the new identity
[2026102004](../releases/2026102004.md). Native Linux compilation and GUI execution
remain separate verification work on the Linux host.
