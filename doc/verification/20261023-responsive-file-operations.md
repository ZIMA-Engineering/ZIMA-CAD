# Responsive File Manager operations and inline dimensions

Status: focused Windows file-operation, shared activity and main-application
dimension GUI checks passed. Linux execution has not been performed.

## Cause and implementation

Clean recursively discovered candidates and hashed complete files on the UI
thread before showing confirmation. Its subsequent recycle loop, Copy, Move and
Delete also performed filesystem work on that thread. A large working directory
could therefore appear unresponsive in both cleanup phases.

The existing background wait and input gate now live in the shared
`zima/ui/background_task.hpp`. Workspace callers keep the same implementation.
File operations capture plain paths/source policies and use this wait, retaining
UI-thread destination selection, confirmation, errors and final table refresh.
The existing delayed cyan indicator is anchored to the visible File Manager.
Fast operations retain its ordinary 250 ms delay.

Clean retains numeric archive suffixes, canonical containment, link exclusion,
read-only source policy, inherited protection and SHA-256 revalidation. Recycling
retains its OS recycle-bin path and has no permanent-delete fallback. Move
rechecks protection before removing a successfully copied original.

Inline dimension activity begins after the unchanged-value fast path. Model
calculations reuse the established private-candidate runner. Sketch dimension
and corner-radius solving use private copies; live mutation remains on the
existing owner-thread path. No modeling equation, native format or user setting
changes are introduced by this activity integration.

## Recorded Windows checks

- `zima_file_manager_background_operations_ui`: passed in 16.16 seconds using
  native Windows filesystem permissions. Its isolated 6,500-archive scan took
  11,567 ms with 1,153 UI timer callbacks and a changing animation frame. Cancel
  retained every archive and the unnumbered document. Earlier contended scans
  took 18,897–30,437 ms and also kept processing UI events; these observations
  are not hardware-independent timing guarantees.
- Copy retained the source and produced the destination. Move retained the
  destination and removed the copied original. Delete Cancel preserved its
  selection; confirmation recycled the disposable test file.
- Clean Cancel preserved the archive. Modifying the archive while confirmation
  was open caused the expected validation error and retained that changed file.
  A subsequent unchanged confirmation recycled it while retaining the current
  unnumbered document. The diagnostic in the acceptance log is this deliberate
  changed-file rejection, not an unhandled failure.
- `zima_cpp_operation_activity_tests`: passed, including real private kernel
  calculation after a controlled 700 ms delay, successive animation frames,
  input/close blocking, source/result equivalence, failure cleanup and nested
  phase retirement. The regression now exercises the extracted common wait.
- All 55 new activity/exact-preview catalog entries across `cs`, `en`, `de`,
  `fr` and `ru` passed presence and formatting-placeholder checks. Full catalog
  and live application checks remain separate gates.
- Main-application dimension editing passed in 22.46 seconds; inline unit entry
  passed in 5.307 seconds. These runs use the current integrated native runtime
  and isolated fixtures. Log: `build/exact-preview-and-dimension-gui-review.log`.

Only generated temporary fixtures were recycled. No user working directory or
project was cleaned by these checks. Sandbox recycling cannot write the Windows
Recycle Bin; native-permission acceptance was therefore run separately. The
broad File Manager test also exercises many unrelated WebEngine catalog pages;
its headless run exceeded a 180-second diagnostic timeout and is not reported
as passed here. The focused operation selector avoids claiming that coverage.

Logs: `build/file-manager-background-acceptance-native.log`,
`build/file-manager-background-small-native.log`, and
`build/background-and-exact-preview-tests.log` (shared activity result only;
its earlier file/preview failures were investigated separately).
