# Properties layout and confirmation verification — 2026-11-11

## Scope

This correction addresses two reported GUI failures and one lifetime defect
found while verifying them:

- Sheet Profile Properties could paint unused Feature controls over the
  direction row, and expanding **Up To** could disorder or clip the lower form.
- New Document initially omitted the hidden error label from its height, so a
  duplicate-file message could appear below the visible viewport.
- A shared middle-button OK callback could synchronously rebuild and delete the
  View currently dispatching that mouse event.

## Implementation boundary

Sheet Profile continues to consume the shared Feature extent controls. Its
profile-owned Sketch button now sits beside the work-plane selector. Thickness
override and thickness remain anchored above a bounded scroll area containing
both sides. Controls that have no row in an extent-only host are hidden rather
than left at a default widget position. Dynamic target-table changes refit only
that variable extent area.

New Document reserves three font lines for validation before the first error and
clears an old error when the name is edited. Shared Properties confirmation queues
the enabled OK click for the next event-loop turn. The specialized Drawing
Dimension creation path retains its finish-command flag until that queued click
has committed or validation has kept the window open.

No modeling algorithm, native document format, calculation tolerance, geometry
side identity, protected container-placement behavior, Qt 6.11.0 dependency or
OCCT 8.0.1 dependency changed. No user-visible string was added or changed; the
existing Czech, English, German, French and Russian catalogs remain authoritative.

## Windows verification

The Release targets `zima-cad-cpp`, `zima_cpp_ui_contract_tests` and
`zima-cad-drawing-harness` build successfully with the pinned Windows toolchain.
Nineteen distinct focused suites pass:

- shared UI and middle-button confirmation;
- the Part-dialog layout matrix;
- translation catalogs;
- component Properties, Feature modeling wire, ordered Up To and compact-window
  contracts;
- New Document, application tools, rectangle external contact, Sketch dimension
  entry and Sheet Profile contracts;
- Measurement/Dimension, Show/Erase, Drawing Balloon, Drawing UI, Drawing view
  controls, Drawing breaks and Drawing details.

The Part-dialog matrix executes 630 combinations across all five languages,
three window sizes and the applicable command variants. It checks containment,
row ordering and overlap, including Sheet Profile Length and Up To states. The
Sheet Profile contract repeatedly expands and collapses Up To. New Document is
also exercised with an actual duplicate Part name. A live Czech Windows run was
visually inspected: the complete duplicate-file message remains inside the
content viewport and above OK/Cancel.

The complete repository-wide regression suite and native Linux execution are
not claimed by this finite Windows verification.
