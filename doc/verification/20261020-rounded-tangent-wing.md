# Rounded tangent outlines and dependent Relations

## Defects and bounded corrections

The casting study combines a crown circle, two native tangent arms and a
scalar corner radius on their underlying polygon. The circular seeding path
rejected every Sketch containing a corner radius. It now includes editable
corner-radius variables and their Radius/Diameter drivers, while preserving
the existing visible-radius ownership and EqualRadius support whitelist.
Every converged candidate must also materialize a valid evaluated profile.
The ordinary equations still decide whether an edit can be accepted.

A separate M24 regeneration failure depended on target order. A contact moved
beyond the old finite support before Relations could extend that support.
Relations now defers a transactionally rejected Sketch dimension, applies
other targets and retries only after progress. No-progress failure rethrows
the original error. Finite PointOnLine domains, native branch/side policies,
immutable external sources and ordinary constraint rejection are preserved.
Part and Assembly use the same implementation and unpublished calculation drafts.

There is no native-format, placement-contract or dependency change. No product
UI text was added or changed; all five localization catalogs remain applicable.

The subsequent primary-CSN closed-nut profile exposed the same sequential
failure in a line-only angular graph: editing its end-flat ordinate did not
retain the diagonal's angle. Value edits now reuse the existing angular seed
whenever the graph contains a driving AngleBetween equation, matching explicit
support regeneration. Failed point drags use the same bounded fit with the
dragged point pinned. This also handles reversed angular participant order.
The common solver remains the final authority; no tolerance or finite contact
domain was relaxed.

## Finite Windows verification

| Gate | Result |
| --- | --- |
| Rounded outline native matrix | 1,792 cases: eight axis/sign orientations, both tangent participant orders, native/external seating supports, driving/reference and locked/unlocked dimensions, fully/underconstrained profiles, seven drivers. Passed. |
| Successive rounded actions | Three 102/104/100% edits, repeated landing drags, independently checked actual circle/line/tangency/HV/dimension equations, DOF, stable identity, external immutability and serialized reopen. Free landing points move; fully locked geometry rejects movement. Impossible corner radii reject without mutation. |
| Sloping finite-line controls | 512 cases, repeated edits/drags/reopen. Height uses 101/99/100% inside the existing finite support; a 104% out-of-domain contact rejects without mutation. Other drivers use 102/104/100%. Passed. |
| Angular triangles | 512 axis/sign, participant-order, native/external anchor, lock/reference and full/underconstrained cases. Three coordinate/angle edits, actual equations, repeated permitted/rejected drags and serialized reopen pass. Locked driving controls also exercise finite native contacts and reject points beyond the segment without mutation. |
| Relations order | 16 Part/Assembly cases, reversed identifier allocation order, X/Y and signs, growing/shrinking contact/support pairs, unchanged re-evaluation, serialized reopen and impossible final contact rejection. Passed. |
| GUI | Three lock/DOF configurations, actual ordered picking and double-click editors, five drivers with three edits each: 45 edits with Undo/Redo. Nine drag gestures: six permitted movements and three fully locked rejections. Native save/reopen passed. |
| Authored casting studies | All 40 driver cases over M12/M16/M20/M24 pass: 120 successive edits with Undo/Redo, explicit regeneration, original source-face identities, independent BRep/bounds/volume/current mass and native save/reopen. |
| Authored CSN closed nuts | Eleven models, 165 successive edits of height/flats/end-flat/bore/thread, current mass and original face identities pass. Actual M8/M30 GUI tests perform 12 coordinate/angle edits and six real point drags with Undo/Redo. Owned-profile drafts are inspected from their actual viewer packets and published only by Finish/OK. Final native saved solids pass an independent checker. |

The reference target is unlocked because a reference dimension cannot own a
lock. The other dimensions follow each case's lock state. The finite matrices
are not a claim that every Sketch geometry or mouse permutation is covered.
The existing Sketcher contract supplies dependent trimming controls; this
change does not add direct trimming of a corner-modified tangent chain.

Receipts: `build/rounded-relations-native-final.log`,
`build/rounded-wing-gui-test-v1.log`,
`build/cpp-windows-release/rounded-tangent-wing-ui/` and
`build/wing-nut-cast-edit-matrix-v7/results.json`. The GUI run took 298.34 s;
the native rounded/control matrix took 34.93 s on this Windows host. These are
observations, not speedup claims. The GUI timeout is 600 s.

The initial relation-order regression failed before the retry correction.
The casting matrix progressed from 20/40 passing cases to 39/40 with the
corner correction, then 40/40 with the Relations correction. Original study
hashes remain unchanged. Their unresolved contour/source and R2 gates remain
documented in [Wing-nut studies](../WING_NUT_LIBRARY.md); editability alone
does not qualify them for installation.

Linux has not been built or exercised for these shared C++ corrections.
The final expanded native matrix passes in `build/angled-graph-native-v6.log`;
the existing Sketcher suite passes in `build/angled-graph-sketcher-final.log`.
An earlier sandbox run stopped at Windows atomic file replacement, before the
same suite passed outside that restriction. Closed-nut GUI receipts are in
`build/csn-closed-primary-gui-v3/` and `build/csn-closed-gui-independent-v1/`.

## Reproduction

```text
ctest --test-dir build/cpp-windows-release -R "^zima_cpp_(rounded_tangent_wing_tests|relation_dimension_order_tests|sketcher_contract_tests)$" --output-on-failure
ctest --test-dir build/cpp-windows-release -R "^zima_cpp_rounded_tangent_wing_ui_contract$" --output-on-failure
python tools/verify-wing-nut-studies.py --models build/wing-nut-cast-studies-v1 --output build/wing-edit-check-new
```

Build the current native targets first and use fresh command-matrix directories.
