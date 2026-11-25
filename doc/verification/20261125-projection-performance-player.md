# Projection confirmation, relief presentation and FEM player

Date: 2026-11-25. Baseline: Windows release 2026112405, source
`934979cb4f471f052494ddb01d8e811291b1cb80`. Shared C++ implementation,
Windows x64 Release, MSVC, Qt 6.11.0 and OCCT 8.0.1. User documents were read
and tested on isolated copies; originals and personal settings were preserved.

## Causes and corrections

The reported saved model has two Projection features, earlier Fillets, a Mirror
Body and an evaluated Family member. Editing a Projection unnecessarily replayed
earlier geometry to reconstruct original operands and disposable reference
coverage. The Family member then repeated the same unchanged local Body work.

Projection now consumes the current bounded topology without requesting unused
original edge operands. Original face demand across Bodies remains explicit.
When a cold saved boundary lacks an absence proof for a requested fragment, the
evaluator restores that boundary's reference packet independently, validates its
fingerprint and coverage, and retains unchanged later native results. Failed
recovery still takes the existing recalculation path. A completely matching
history does not rebuild unused live edge ancestry.

Family transactions can reuse a longer exact local Body prefix from the newly
calculated base. The kernel still checks its normal operation and dependency
keys. An unchanged Mirror output is reused only for the same source fingerprint,
normalized plane, owner, physical-analysis state and error-free result. Changed
inputs invalidate reuse. Family publication, native storage and Undo remain part
of the same established transaction.

The failed relief had no retained region. GUI, workspace and kernel validation
now identify that missing input before material calculation. GUI confirmation
arms region entry, retains the draft and removes the prompt after valid input.
It does not create an unresolved history row or an Undo transaction. The source
Sketch remains white; projected and wrapped relief tool wires are azure.

The Feature type list pairs Project sketch/relief, then Wrap sketch/relief.
Explicit item data separates type identity from menu order. Its neutral icons
retain the theme foreground even on the selected row. The FEM sidebar exposes
one Deformation player action with the play icon. Its compact window contains
separate Start, Pause and Stop controls; Pause retains time and Stop returns to
zero. Settings and video-export progress do not resize it.

## Measured user-model confirmation

The saved `50.prtz` reproduction was copied before testing. Measurements exclude
initial document loading unless stated. These are observations on this workstation,
not universal timing limits or a promise that changed solid calculations are free.

| Operation | Released baseline | Corrected build |
| --- | ---: | ---: |
| CLI changed second Projection, first edit after open | 29.690 s | 3.262 s |
| CLI second direction edit | 20.066 s | 2.965 s |
| CLI unchanged Projection confirmation | 0.003 s | 0.003 s |
| GUI open Projection Properties | — | 0.408 s |
| GUI unchanged Projection OK, including display restoration | — | 0.470 s |
| GUI relief OK without a retained region | late generic failure | 0.014 s |
| GUI new negative relief with both retained regions | — | 5.229 s |
| GUI unchanged relief OK, including display restoration | — | 0.215 s |

The GUI unchanged confirmations and missing-region rejection recorded zero CAD
body calculations and unchanged document state. The CLI changed-edit trace no
longer executes the preceding Fillets; the Family member reuses its matching
local history. The changed relief still performs the required surface partition,
material operation, dependent Family work and scene restoration.

Independent BRep analysis on the same copied model verified valid results and
the cylindrical-layer volume for both -3 and +3 mm corrections: approximately
-1666.83 and +1842.29 mm3. Both adjacent target regions, Undo/Redo and native
serialization/reopening retained the definition and calculated shape.

## Finite regression coverage

- Projection document/history and directional-projection tests pass, including
  curved relief, missing-region rejection, preview colors and reference identity.
  A cold missing-fragment test compares fresh output and confirms the unchanged
  intermediate native buffer was retained.
- Wrap document/history tests and the four-output GUI matrix pass, covering
  reference entry, inspection, preview without OCCT, embedded artwork, Cancel,
  unchanged OK, persistence and Undo/Redo.
- The five-output Projection GUI matrix passes: picking, replacement, independent
  inspection, preview colors, type order/icons, OK/Cancel, whole-Origin placement,
  direction, save/reopen, Tree selection/double-click and successive source-radius
  edits. This is not a new exhaustive Sketcher constraint matrix.
- Family tests pass, including mirrored members, changed sources, cold regeneration,
  native reopen, Assembly insertion and Undo/Redo. Model-calculation tests pass
  reference convergence, last-frame retention, dependent Cuts, deferred rollback,
  source identity and explicit Assembly refresh.
- The model-calculation suite initially failed an obsolete assertion that the
  input before a Fillet had no triangles. The exact released baseline failed
  identically. The established policy intentionally retains that full input for
  Properties; the assertion now requires it, while retaining output identity and
  independent volume comparisons. No Fillet algorithm was changed for this test.
- The FEM GUI matrix passes compact/stable player layout, separate transport
  actions, five languages and both themes, mechanical/thermal/expansion controls,
  MP4 export/decoding and restoration of the original result presentation.

Four new text keys are translated in cs/en/de/fr/ru. Local source scanning also
finds an unfinished, untracked CAM editor; it is excluded from this CAD release.
The committed-source catalog/coverage gate passed in all five languages.

Visual acceptance rejected the unpublished 2501 candidate because the Stop SVG
was missing from the Qt resource collection. Replacement 2502 includes that
resource and verifies opaque pixels in all three transport icons in both themes
and all five languages. The export status reserves one fixed-height line instead
of three; the complete final message remains available in its tooltip. This
keeps controls stable without retaining a large blank message area.
No native format, template contract or protected placement solver changes in
this follow-up. Native Linux execution and the previously documented dedicated
external/nested Wrap GUI matrix remain unverified. Package acceptance and public
publication are recorded separately in the release record.

Disposable measurement logs and isolated model copies are under
`build/projection-perf-20261125/`; GUI fixtures also use
`build/wrap-20261124-fixes/`. Private user model content is not committed.

Signed Windows 2026112502 passed its independent package and public-download
gates; see [publication evidence](20261125-windows-2502-publication.json).
