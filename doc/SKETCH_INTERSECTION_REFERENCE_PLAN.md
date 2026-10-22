# Sketch intersection references: proposed persisted scope

Status: persisted scope approved by the user on 2026-10-21; native calculation
foundation implemented, Sketch editor and persisted definition integration pending.
This follows the 2026-10-21 discussion of `Projects/1000.prtz`.

## Existing behavior and the immediate fixes

Reference to contour preserves a certified circle or arc as native circular
geometry. Rational quadratic reference storage does not turn that geometry into
an approximate spline. The final Sketch in `1000.prtz` contains an R200 native
arc, no native B-splines, a linked centre and an equal-radius source constraint.

Previously stored face references project the source face's outline. An edge-on planar
face supplies an infinite straight support. This is distinct from intersecting
the face with the Sketch plane. The 2026-10-21 fix restores direct face candidates
using the existing face-reference format. The separate trim
fix consumes existing line/circle references as read-only cutters and retains
endpoint incidence; it does not require a new document field.
The subsequent ordinary Reference-entry correction restricts new entry to
individual geometry. Preserve stored Face references; new whole-face selection
belongs only to the separate intersection command.

## Requested new operation

The user accepted the Czech command names **Referenční průsečnice** and
**Průsečnice → obrys** on 2026-10-21. The second command must create associated
native Sketch geometry through the existing Reference-to-contour behavior,
preserving certified circles/arcs. Both commands require complete cs/en/de/fr/ru
labels, tooltips and validation messages in the same implementation change.
These names describe the pending UI; they are not evidence of completed commands.

Add an explicit Face x Sketch-plane intersection mode to external references.
Preserve stored projected face references and Reference to contour behavior.
The source remains read-only, and the owning Sketch's actual resolved plane is
the second support. Consume the current calculated source at its allowed history
boundary and exact Assembly occurrence; preserve one-way dependencies.

Reuse the existing native surface-intersection calculation, curve capture,
seam joining and ancestry code. Refactor only the necessary common calculation
after tracing callers; do not duplicate a second modeling algorithm. Calculate
the requested intersection explicitly when a source is chosen. Ordinary hover,
selection, tab switching and numeric preview must not invoke OCCT.

The result can be empty, isolated contact points, one open/closed curve or several
disconnected branches. Offer exact individual branches and let the user select
one or more. Preserve lines, circles/arcs and other conics when certified; use
exact rational spline geometry for genuinely nonconic curves. A rectangular
cut is its real closed contour, including holes, rather than a bounding frame.

## Native data and branch ownership requiring agreement

The persisted external definition needs an explicit intersection mode, exact
source face/occurrence identity and a selected branch identity. Reuse the
existing native exact-curve cache where its contract is sufficient. Store all
required information inside `.prtz`/`.asmz`; add no required sidecar. A branch
is not an OCCT edge index, sampled point, UUID ordering or nearest screen curve.

The current surface-intersection implementation rejects ambiguous native
ancestry. The subsequent [radial-domain extension](verification/20261022-sketch-intersection-closed-domains.md)
distinguishes two coaxial toroidal circles by their predefined inner/outer domains.
Other closed branches with identical parent faces still share the `:closed` role.
This cannot simply be connected to a new
Sketch button and claimed to support several branches. Define branch ownership
from the source's explicit topology and oriented support domain during explicit
calculation. Where that cannot identify a branch unambiguously, report unresolved
selection rather than silently rebinding. Preserve direction, material/contact
side and branch identity through parameter changes, recalculation and reopen.
Splitting, merging or disappearing branches must not select another branch.

Curve x Sketch-plane intersection is a related follow-on returning actual
contact points (or an explicit coincident-curve case). It uses the same ownership
rules, but is not to be approximated by ordinary projection.

## Assets and verification

The native foundation is `OcctKernel::intersect_face_plane`. It consumes the
current calculated topology or exact native archive bindings, adds the resolved
Sketch plane as a transient support, and calls the existing surface-intersection
implementation. It preserves bounded domains and returns exact curve/point
packets. Compound Body publication retains a separate local calculation key and
placement; the query resolves those existing records rather than guessing an
identity from a mesh or replaying history. This foundation initially had no
production UI consumer or serialization change. The editor integration below
now consumes it and persists explicit selected-branch bindings.

The first native matrix covers single line/circle branches, two disconnected
open branches with distinct native boundary ancestry, empty results, coincident
area rejection and branch merging. Shared closed-branch ancestry remains
ambiguous outside the newly certified radial domains and is still rejected. This is not yet
acceptance of arbitrary multiple-branch reference selection or persistence.
See the [51-case native foundation matrix](verification/20261021-sketch-face-plane-foundation.md)
and its [94-case extension](verification/20261022-sketch-intersection-closed-domains.md)
for their historical cases and gaps. The separately approved
[source-boundary extension](verification/20261022-sketch-intersection-source-bindings.md)
now passes 108 cases, including cold open-solid sources and transformed copies.
It strengthens existing native binding completeness. The subsequent editor
integration and its finite manipulation matrix are recorded below.

Before changing serialization, inventory committed native documents for the
affected persisted definition. Existing authored references must retain their
projection meaning. Regenerate the corresponding start templates with changed
native code and verify real new-document GUI activation and command availability.
If the representation changes existing required data, calculate/save/reopen every
affected factory asset, including FORM and embedded library data, and commit the
verified assets with the code. Do not bulk rewrite personal libraries, Projects,
or independently embedded user definitions. Report inventory and conversion
counts before performing those writes.

Verify no intersection, tangency, coplanar overlap, one/two/multiple open and
closed branches, trimmed faces, holes, seams, opposite orientations, local and
external Parts, repeated/nested occurrences, source dimensional edits and lost
supports. Check exact equations, stable identities, read-only sources, multiple
successive dimension edits and drags, rejected redundant/impossible edits,
save/reopen, Undo/Redo, independent inspection, Cancel and actual GUI selection.
Run the applicable Sketcher dimension/manipulation matrix; explicitly record
unverified variants. Localize controls/errors in cs/en/de/fr/ru and verify both
themes and live language changes. Measure explicit calculation and scene counts.
Linux completion requires its native host. Do not claim package acceptance before
the repository's committed-source runtime gates pass.

## Editor integration (2026-10-22)

The current implementation uses an explicit `intersection_` discriminator on
persisted external-reference kinds, with a required `intersection` object carrying
the exact offered face owner/key and selected native branch key. The existing
source owner/key retain original face ancestry; the existing occurrence/context
fields retain their exact meaning. Ordinary projected references keep their
current discriminator and data. An older reader rejects the new kind instead of
silently interpreting a selected section as a projected outline. This is an
additive current variant, not a legacy migration path.

The read-only committed-source inventory at `6ea08e046` contains 1,210 native
assets and no authored intersection bindings. Existing factory FORM/library
geometry therefore requires no conversion for this additive variant. All six
start templates pass changed-code save/reopen and remain byte-identical; actual
mm/inch new-document GUI activation and command checks pass. User assets are not
rewritten.

Explicit Part calculation refreshes cached selected branches only after source
geometry and placement frames settle. Endpoints and multiple branches of one face
share an operation-local face query; missing/ambiguous branches keep their own
last valid caches and become broken. Ordinary projection snapshots skip these
queries. Cache-only contextual updates must not feed empty projection packets to
ordinary references from the same source document.

Assembly queries consume native input snapshots before the owning cutter and
follow exact nested occurrence paths. Already transformed copy geometry is read
in its actual copy frame. The kernel localizes only a private query packet's
selected occurrence metadata before reusing the shared surface-intersection
algorithm; it does not modify stored bindings or source shapes. A contextual Part
reference must not silently acquire Assembly-owned modified geometry.

The two editor actions share ordinary reference command ownership, draft commits,
Cancel and middle-button behavior. A face choice calculates explicitly. Several
branches are transient native-reference wires; selecting a prepared branch runs
no kernel operation. Source/session identities, calculated generations and the
actual Sketch definition invalidate prepared choices. Ordinary Reference still
admits individual original geometry, without enabling whole-face projection.

The [editor verification record](verification/20261022-sketch-intersection-editor.md)
documents the native/GUI matrices, localization/themes, explicit work counts,
template gates and remaining limits. It includes certified full-domain spline
extrusion slices and signed cylinder/plane endpoint domains, without changing
existing source bindings or ordinary Surface Intersection/Trim feature names.
Portable publication remains a separate committed-source gate; finite checks
are not a claim of universal Part/Assembly/editor coverage.
