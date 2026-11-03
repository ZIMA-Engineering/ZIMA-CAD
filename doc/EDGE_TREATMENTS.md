# Edge treatments: Fillet and Chamfer

Fillet and Chamfer are two modes of one edge-treatment workflow. They share
`EdgeTreatmentPropertiesDialog`, stable edge selection, history rollback,
preview semantics, view highlighting, and dimension editing.

## Separate user-facing properties

Fillet opens Fillet Properties with a common radius. Chamfer opens Chamfer
Properties with a common symmetric distance. Neither window contains an
operation selector and an existing feature cannot be converted to the other
type. Both operations accept one or more edges, selected with Ctrl+click or
removed from the list.

Both windows expose three mutually exclusive calculation choices: Automatic,
ZIMA and OCCT. Every open route also has independent first and last end-face
fields. A valid face clips the generated treatment at that route endpoint;
clearing the field restores the perpendicular end. The shared reference-cell
controls provide replacement, inspection and Delete without changing route
ownership.

A visually continuous route may contain several OCCT edges after Boolean
splits. The properties tree stores both the route and its individual members.
Removing a child removes only that edge and preserves the remaining route;
removing the parent removes the complete route. Restore Route recomputes the
continuous route from its surviving seed, or from the first remaining member
when the old seed was removed.

Creation and editing of each operation use the same shared dialog
implementation and selection workflow, configured with a fixed operation type.

## Calculation contract

- Explicit OCCT Fillet uses `BRepFilletAPI_MakeFillet`; explicit OCCT Chamfer
  uses `BRepFilletAPI_MakeChamfer`.
- Explicit ZIMA constructs supported planar Fillets and Chamfers from their
  exact section profiles, route sweeps and Boolean operations. Automatic selects
  that path only when its geometry contract is satisfied and otherwise retains
  OCCT.
- The current ZIMA Fillet contract accepts an ordered open route of straight or
  circular segments with one unique forward-tangent continuation. Chamfer keeps
  its straight-segment contract. Both require supported planar adjacent faces
  and, when present, planar end faces containing the exact route endpoints. A
  selected end face may be angled. Explicit ZIMA rejects an unsupported case
  instead of silently changing method.
- End-face fields are invalid in explicit OCCT mode. Automatic first uses a
  perpendicular terminal face or one uniquely inferred angled planar face. A
  subtractive Fillet falls back to the actual Body envelope only for a complex
  unresolved terminal; it never extends through a known perpendicular cap.
- Both store the full `edge_refs` list and mirror its first entry in legacy
  `edge_ref`.
- The effective selected intervals are first submitted in one OCCT builder
  operation. A bounded recovery is used only when that combined operation
  fails, as described below.
- Creation and editing publish only a transient preview while Properties is
  open. OK validates, calculates, commits and closes; Cancel discards the
  pending values and restores the unchanged history boundary.
- A failed build keeps the last valid body and leaves Properties open.

The general tree and view rollback rules are defined in
[`HISTORY_EDITING.md`](HISTORY_EDITING.md).

## View interaction

The generated treatment face is never filled as a selection substitute. Hover
and selection highlight only its boundary edges. Selection survives camera
rotation and clears through the common view-selection rules. A left double
click exposes the editable radius or distance dimension. Context-menu
Properties opens the shared Edge Properties window.

Hover, left-click and RMB cycling consume the same ordered viewer candidate
list. A treatment contributes only the persisted boundary edges of its own
generated face. Generated-edge ancestry inherited by later Boolean operations
must not make the rest of the result body selectable as that treatment.

## Inspection dimensions

Treatment dimensions use the persisted input-edge samples and adjacent-face
side directions at the feature's real history boundary. Properties and ordinary
double-click inspection use the same preview helper; neither needs an OCCT
calculation merely to display the dimensions.

- Fillet radius dimensions lie in the normal section of the route. Linear-radius
  Fillets expose their start and end radius separately.
- Chamfer distances retain the route endpoint and its corresponding tangent
  point as their measured witnesses. Each distance lies in its adjacent-face
  plane. Its outward direction follows the route tangent away from the selected
  start, so the shared dimension envelope offsets the dimension line beyond that
  end instead of across the solid in a common transverse section.
- Both distances in A × B mode follow that same envelope rule, including Flip.
  Distance-plus-angle retains its angular dimension in the normal section
  defined by the measured rays.

The standard envelope offset is 8 mm. The ordinary purple grips and dimension
layout controls can change it. This is a three-dimensional annotation rule;
rotating the View can still project an external dimension over the displayed
solid. The placement remains stable rather than jumping with camera rotation.
Stored layout offsets are interpreted in the corrected distance plane; existing
manually positioned Chamfer labels can therefore move when this version opens
or inspects them. Dimension identities, measured values, route side selection
and body geometry are unchanged.

## Validation of Chamfer distance planes (2026-10-01)

The Windows GUI regression covers vertical and horizontal edges and checks that
the outward direction follows the edge and that both dimension-line endpoints
clear every corner of the dimension envelope by the default 8 mm. The A × B
case also toggles Flip on and off. Existing tests exercise all five
Fillet/Chamfer modes, every applicable grip, Cancel/OK, Escape, direct value
editing, native save/reopen and Undo/Redo. The default horizontal-edge view is
captured for visual review. Log: `build/chamfer-envelope-ui.log`.

Localization review: no new or changed user-visible strings. The existing
five-language catalogs are reused.

The complete Windows GUI run passed in 60.39 seconds. The shared dimension-layout
and five-language translation contracts passed 2/2 in 11.71 seconds
(`build/chamfer-envelope-contracts.log`). No new geometry calculation or format
change was introduced. Linux validation remains deferred to the Linux host.

## Bounded edge-treatment recovery (2026-10-28)

Three user-provided native models exposed two distinct OCCT failure modes. The
models remain outside the repository and were read only; no project geometry was
copied into the source tree.

- A single source edge was valid on its standalone additive feature, but the
  same Fillet failed after the feature had been fused into the preceding body.
  The failure was at the later intersection endpoint, not an excessive radius.
- Several individually valid bounded edges failed when submitted to one OCCT
  Fillet or Chamfer builder because their endpoint patches interacted.

A Boolean split interval is a real joint only when its persisted adjacent-face
ancestry contains support owners from different source features. A surviving
fragment bounded only by faces from one source feature remains an original edge,
even when it is collinear with the joint. Text contained in a length-delimited
parent reference is never mistaken for the current interval's role.

At an overlapping pick the current calculated edge is offered before its explicit
original-reference alternative. RMB cycling can still select that original edge.
A current edge seed grows into one ordered route only through a unique exact
forward-tangent continuation; an explicit original-reference seed remains exactly
that one authored edge. Hover shows only the candidate under the pointer, while
confirmation, Properties and the cyan preview share the complete selected route.
A collinear 180-degree return terminates the route instead of being appended as a
final segment, and a branch or ambiguous continuation also stops safely.

The normal combined OCCT calculation remains the primary path. On failure, a
constant single-edge Fillet immediately following an additive Feature can retain
that standalone operand, fillet the exact authored edge there, and then fuse the
treated operand with the real preceding body. A cold or reused native prefix
reconstructs only the required source branch before applying the same recovery.
For a failed constant multi-edge Fillet or multi-edge Chamfer, the kernel applies
the persisted resolved edges as deterministic independent patches and publishes
one final history boundary. If an earlier patch completely consumes a later
fragment, that fragment is skipped; the kernel never selects a nearby replacement
by coordinates.

Edge resolution now recognizes recursively persisted Boolean split ancestry.
Actual split descendants take precedence over a transitional parent alias. A
partially surviving edge is therefore treated only on its real current intervals.
If every persisted interval has disappeared, Fillet and Chamfer publish an
unchanged no-op boundary. Explicit history deletion remains a different workflow:
it removes the source reference and retains the existing repair state.

The available private cases passed read-only recalculation on the development
Windows machine. The synthetic regression covers fresh and reused-prefix
single-edge Fillet recovery, exact joint-versus-original priority for both
Fillet and Chamfer, recursive split-parent resolution, stable public feature
ownership, and ordinary fragment
save/reopen, regeneration and Undo/Redo contracts. That 2026-10-28 release still
used OCCT treatment surfaces. The later ZIMA planar builder consumes the same
explicit route, support faces and endpoint ancestry under the bounded contract
below.

There is no native file-format or precision change. Localization review found no
new or changed user-visible text, so the existing `cs`, `en`, `de`, `fr` and `ru`
catalogs are unchanged. Linux execution remains deferred to the Linux host; the
same shared C++ path is used on both platforms.

The route/branch display-order observation from the original investigation is
not changed by this geometry correction. It requires a separate UI-only audit of
both traversal directions before any ordering change.

## ZIMA planar construction and route survival (2026-11-03)

The ZIMA Fillet builder creates the supported circular section along the exact
straight route and fuses or subtracts the resulting treatment as required by the
two adjacent planar faces. Its closed section is exactly one circular arc and
the two straight support-face segments meeting at the source edge. It has no
auxiliary outside extension. Chamfer has a separate planar builder using its
distance/angle definition; its closed section is exactly the treatment line and
the two support-face segments, a three-line triangle. Both builders use the
document modeling tolerance, share stable-reference resolution and end clipping,
and do not duplicate platform-specific geometry.

The exact support segments may leave redundant coplanar Boolean seams. A local
same-domain cleanup removes only those new seams while protecting surviving
authored input boundaries and their non-coplanar descendants. Cleanup history is
merged into the treatment history so stable references continue through later
operations. The calculation does not replace the configured tolerance with a
larger fixed modeling value.

Each route persists two end-face slots even when both are empty. The selected
face must contain the corresponding open-route endpoint. A perpendicular empty
end and a valid angled planar end are both finite supported cases. If a later edit
makes the face invalid, calculation reports the reference error without reaching
an unsafe Boolean call. OCCT mode cannot accept end faces.

A placed history container resolves each body-topology reference from the exact
calculated output immediately before that container, never from a later final
Body. Ordinary regeneration retains only the requested face, edge or point in a
disposable reference packet; it does not retain an intermediate display mesh,
B-Rep archive or aggregate-property calculation. The packet is rebuilt after a
native reopen and is not a persisted format field. Repeated regeneration keeps
the resolved frame stable, while an actual change to the preceding source updates
the placement through the ordinary Undo/Redo transaction contract.

Boolean edits may split or consume selected source edges. Calculation, View and
Properties recursively resolve every surviving current fragment through the
same exact length-delimited ancestry. The stable authored route remains one
group. Its authored segments and any read-only numbered current fragments are
shown directly; this table has no expand/collapse interaction. The cyan preview
publishes every matching fragment with both adjacent-face guides. The treatment
stays resolved while at least one fragment remains and becomes unresolved only
after all fragments disappear; no coordinate-nearest replacement is invented.

Spline routes, arbitrary blend transitions, unrelated holes crossing the
treatment and advanced cap shapes remain outside this first ZIMA contract.
Automatic provides the existing OCCT fallback for unsupported geometry. The
format-48 migration makes calculation method and end-face slots mandatory and
rewrites repository-owned assets only after calculated-contract and native
readback verification.

### Windows verification

The user project `POKUS.prtz` was copied to an isolated build directory and left
unchanged. Fillet06 was changed from an OCCT radius of 12 mm to a ZIMA radius of
17 mm, exceeding the preceding Fillet05 radius of 15 mm. The command calculated,
saved and reopened without an error; the following Hole and Pattern also remained
calculated. This verifies that the preceding Fillet radius is not an artificial
upper bound for a later independent treatment.

The protected user project `TOP.prtz` was opened read-only. The Fillet07 seed
resolves to the two exact forward-tangent segments that meet at its internal
four-edge junction. On a disposable copy, committing those two segments with
the ZIMA method at the authored 6 mm radius calculates, saves and reopens without
a calculation error. The source file SHA-256 remained
`272E199D667DF9BD715AAFB8DDBE6B06007CB401FC717EE77617FC0C813663F3`
throughout this verification.

The synthetic Boolean regression starts with one persisted vertical edge. A
subtractive feature first splits it into two surviving fragments, then moves so
only one original fragment survives, and finally consumes it completely. The
ZIMA Fillet resolves and treats both fragments in the first state, remains
resolved on the single surviving fragment in the second, and becomes unresolved
only in the zero-fragment state. This regression runs both independently through
`zima_cpp_boolean_split_edge_tests --zima-fragments-only` and as part of the full
Boolean suite.

The format-47-to-48 offline migration regenerated and reopened all twelve
remaining repository test fixtures from their authored history. Every fixture
passed exact native readback, unchanged authored-history and preserved calculated
contract checks. Existing Fillets and Chamfers are explicitly migrated to OCCT
so a format update cannot silently select a different geometry implementation.

Localization review covered Czech, English, German, French and Russian. The
verification diagnostics and this documentation add no user-visible application
text. Linux geometry execution remains deferred to the Linux host; Windows and
Linux compile the same shared implementation.
