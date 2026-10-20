# Dimensions attached to end-on axis crosses

The user approved independently selectable horizontal and vertical strokes of
an end-on axis cross on 2026-10-20. They are available to manual angular,
linear and chain dimensions. Automatic entry selects the stroke under the
pointer; the centre remains available near the intersection and through the
existing explicit Centre and Point modes.

## Geometry and reference ownership

The physical 3D axis still projects to a point when viewed end-on. Its existing
reference continues to identify that axis and its centre. The two additional
Drawing references identify the displayed horizontal and vertical strokes,
using the same `axis_annotation_geometry` result as the renderer. They are
not inferred model edges and do not change the source Part or Assembly.

The reserved semantic keys are
`drawing:axis-cross:h:<byte-count>:<original-axis-key>` and
`drawing:axis-cross:v:<byte-count>:<original-axis-key>`. The owner and complete
occurrence path are copied unchanged. The enclosing dimension retains its
view ID. The byte count validates the complete original key; it is not a
topology index or an identifier regenerated on projection.

This is an approved extension of persisted `.drwz` reference semantics. It
uses the existing generic reference fields, with no new document fields or
Part/Assembly format change. Existing centre and side-on-axis references
keep their interpretation. Existing factory drawings do not require asset
conversion: their original references are unchanged.

Each stroke is one support line across both halves of the cross. Hover and
inspection highlight only those two displayed halves, including the ordinary
view-break clipping. Explicit centre inspection retains the complete cross.
Hidden annotations cannot be picked, but hiding them does not invalidate an
already authored dimension. Removing the axis or changing to a camera where
the selected cross stroke does not exist makes the attachment unresolved;
it must not silently rebind to the physical side-on axis or the other stroke.

The common projected-geometry and prepared-picker paths own the new supports.
Their existing source, annotation, camera, scale and break invalidation checks
apply. Picking, hovering and inspection do not call OCCT or create transactions.
Only confirming the dimension commits its existing Drawing operation.

## Verification

The native measurement contract covers:

- Both strokes, their opposite halves, automatic entry and explicit line entry.
- The original Centre and Point modes and automatic centre entry.
- Exact stroke inspection, and independent repeated component occurrences.
- Axis/edge angular and linear dimensions in both selection orders.
- Axis/axis angles in both selection orders and all four ray-side combinations.
- Axis/axis linear dimensions in both orders and a three-axis chain.
- Three successive source-coordinate changes and scales 0.25, 1 and 4.
- Hidden and unresolved axes, an end-on/side-on/end-on camera transition,
  malformed stroke references, and native Drawing save/reopen.
- Equivalent candidate ordering from the direct and prepared pickers.

The native GUI contract exercises six mouse scenarios: both stroke directions
for angular, linear and chain commands. It checks the independent inspection
eye, unchanged revision during hover/inspection/preview, Cancel, atomic
Undo/Redo, measured values and native save/reopen. The existing complete
measurement GUI suite also checks the new reference labels after switching
among Czech, English, German, French and Russian.

A separate native source check authors a real Part with an ordinary plate and
two through openings, calculates and saves it, projects it interactively, then
recalculates three successive opening-position changes. The Drawing retains
the exact opening references and updates its linear value while retaining the
90-degree angular value. Its final Drawing is saved and reopened. Source
fixtures and screenshots remain under `build/drawing-axis-cross-gui`.

Windows native and GUI checks passed. This finite matrix does not certify all
possible projection cameras, every section/detail/crop/break combination or
native Linux execution. Existing broader measurement tests remain relevant.
The release package and native Linux runtime have not yet been verified for
this extension.

## Development test executable selection

The current CMake targets `zima_cpp_ui_contract_tests`,
`zima_cpp_fastener_library_contract_tests` and `zima_file_manager_ui_tests`
use the isolated `build/cpp-windows-release/fastener-runtime` output directory.
Older executables can remain at the build root; do not use those stale files
as evidence for the current implementation. Prefer CTest's resolved target
paths, or verify the actual CMake link output before invoking a test directly.

On 2026-10-20 the user observed short placement fields while an older build-root
UI test executable was running. Rebuilding and running the correct isolated
target passed the original 400 dialog/language/window-size combinations.
The expanded 600-case matrix also passed at 900 x 650, 1366 x 768 and
1920 x 1080, checking minimum numeric-control height, actual numeric/unit glyph
bounds, anchored rows, table growth, scrolling and confirmation accessibility.
The initial glyph check used the complete font line box and incorrectly flagged
90 cases where Qt reserved 15 pixels for 16-pixel line metrics including unused
leading; none involved the placement X/Y/Z fields. Replacing that proxy with
actual numeric/unit ink bounds removed those false positives without changing
the application controls. Screenshots of the current default Feature dialogs
are in `build/dialog-layout/cs-default-*-900.png`. No production placement or
numeric-field change was necessary for this observation.
The same expanded 600-case matrix was repeated successfully after the balloon
extension; see `build/xyz-repeat-current-600.log`.
