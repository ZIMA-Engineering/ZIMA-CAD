# Curve / Sketch-plane associative point references

Status: the user explicitly approved this scope on 2026-10-23 under the native
format rule in AGENTS.md. Exact Feature previews and restoration of
ordinary face/edge picking do not depend on this persistence change.

## Requested behavior

The intersection commands accept a preceding native Sketch curve or authored
3D Curve against the active Sketch plane. A transverse isolated contact creates
a native associative point, rather than a projected curve or an unrelated Origin.
Multiple contacts are separate selectable candidates. Tangent contacts and
endpoint contacts must be distinguished from a coincident curve interval.

The actual user case is the last modeling Feature and the preceding spline
Sketch in `Projects/2000-kontrola-20261023.prtz`. Investigation uses an immutable
copy; the original and user-edited review document are not overwritten.

## Stored contract

Add a dedicated curve/plane intersection binding alongside the existing
face/plane binding. Retain the existing face variant and its meaning. A curve
binding records the source document/occurrence, exact source curve identity,
selected native parameter domain and branch identity. Do not place edge identities
into fields defined as face identities or add a hidden geometry sidecar.

The current implementation uses the distinct `curve_intersection_point` source
variant. Its required version-1 payload records the source curve owner/key,
native degree and knot basis, ordered authored-point parameterization identity,
an isolating native parameter interval, contact kind/orientation, isolated-root
count and the deterministic branch key. The binding is stored in the native
Sketch inside its Part or Assembly. Existing face bindings retain their variant;
there is no old/new migration branch or required external cache.

Use the source's exact native curve/basis and real bounded domain. Parameters
describe association to that authored curve, not sampled display-polyline indices,
OCCT enumeration or screen position. Root association must certify one compatible
root in the retained branch domain. A lost or ambiguous branch becomes broken
and keeps its last useful point; it must never silently select a neighbouring root.
Reject a coincident interval as non-unique rather than inventing one point.

Refresh is deliberately conservative. A changed degree, knot basis, authored
point order, contact orientation or isolated-root pattern breaks the association.
The cached point remains available for repair/deletion. In particular, rotating
an ellipse must not silently exchange two roots merely because another root
now occupies the old parameter interval. A changed root count can also break
otherwise surviving contacts; explicit reselection is preferable to an uncertified
association. Closed-curve seam intervals use the same native domain unwrapped
across the seam, not a display-segment number.

Explicit Assembly calculation receives authoritative open Part sources from its
own Workspace while retaining the candidate Assembly's current occurrence frames
and calculated copy geometry. An unsaved source Sketch must not be replaced by
the last saved native file. Cold calculation reads the selected native source;
neither path saves that source, solves mates during picking, or recalculates on
ordinary hover/tab changes. Several bindings to one exact source curve share one
intersection query within that Sketch refresh.

The existing ordinary projected reference and existing face/plane intersection
paths retain their current distinct capabilities. No shared placement equations,
geometry-side identities or Drawing coordinates change.

## Assets and conversion

Audit all repository-owned native assets for affected bindings. The preceding
1,185-file audit found no factory intersection-bound Sketches; repeat that audit
against the final committed assets. Calculate/save/reopen every affected native
factory asset with the changed C++ implementation if any are found, including
FORM definitions, library Parts and dependent Drawing data. Regenerate all six
Part/Assembly start templates and verify actual GUI new-document creation and
the initial editable context. Publish the exact committed assets with the runtime.

Do not bulk rewrite user projects, private configuration or custom libraries.
Existing documents can contain unchanged face bindings and newly authored curve
bindings in the same native file. This is a current geometry-source variant,
not a legacy migration path.

## Acceptance

Verify exact line, circle/ellipse/arc, Sketch spline and general 3D Curve sources;
one/multiple/no contacts; endpoints, tangency and coplanar ambiguity; opposite
orientations; placed Bodies and Assembly occurrences; immutable source geometry;
stable identities through successive dimension edits and drags; locked/reference
dimensions; atomic rejection of impossible edits; Undo/Redo; and cold reopening.
Check the actual mouse selection and point/contour command behavior, including
the saved user case. Record unverified variants explicitly.

Measure explicit intersection queries and native file sizes. Picking/hover and
ordinary tab switching consume existing packets without calculating intersections.
Complete all five UI languages in the same change. Native Linux execution and
portable Windows acceptance remain separate verification gates.
