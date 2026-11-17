# Blend, circular inference and Mirror follow-up

Status: native and interaction verification passed; Windows packaging pending.
Shared C++, Windows, Qt 6.11.0 and OCCT 8.0.1. Baseline: `d180eb718`.

## Scope and causes

The follow-up addresses the recreated five-member 04/Fillet 13 and the rib
between cylindrical bosses in 06/Fillet 02. It preserves the earlier open
terminal decisions and the collapsed-radius construction.

Regular rolling sections now use their actual contact boundaries, support
patches and circular section caps. Closed intersection splines keep the short
periodic branch. The angular fan is unwrapped consistently, including reversed
route orientation. Periodic support patches determine their inside after their
pcurves are aligned. A source intersection is reconstructed from its analytic
supports only when its curve misses the required section corners.

The swept envelope uses a tighter construction tolerance than the final Boolean
merge allowance. The independent cylindrical-rib test exposed open edges and
internal support remnants with the earlier approximation, even when BRepCheck
accepted the result. General-route acceptance now also checks manifold edge
incidence. This does not reduce document precision or enlarge the global
tolerance. Section-vertex tolerances record bounded, measured projection error.

A closed route can start on an intersection spline. Its local section seed is
rotated to an analytic member while retaining all source-job identities. The
enumeration seam is not a terminal: automatic end-face inference there could
otherwise clip away the returning half of the route.

Fillet and Chamfer distinguish failed ZIMA construction from invalid command
input. OK and middle-button double-click close a rejected construction without
inserting a feature or replacing an existing definition. Invalid input still
requires correction. Successful and unchanged confirmation use the established
transaction path. OCCT command behavior is unchanged.

Sketcher intersects the actual circular support with the proposed H/V or
symmetry coordinate, selecting the nearby intersection. Preview and confirmation
use the same helper. Native and external circular contacts retain C; external
circle display chords no longer define a newly entered contact. The rank test
uses a smooth signed circle equation instead of an absolute projection-distance
cusp, which incorrectly classified an outer-side H/V length as redundant.
Finite arc-domain handling and the ordinary solving equations are preserved.
Tangency-constrained circles retain their established rank treatment: isolated
contacts can have zero first-order radial motion. The full older Sketcher suite
exposed that distinction and passes with the ordinary-incidence change scoped
accordingly.

New Mirror definitions select their own XY plane. Editing retains the stored
plane. Source-entry mesh publication preserves the camera rather than fitting
the model again. Shared placement and reference-entry contracts are unchanged.

## Saved user data

Verification modifies disposable copies only, under
`build/test-models/fillet13-followup-20261117`.

| Snapshot | SHA-256 | Target |
| --- | --- | --- |
| `user04.prtz` | `77cf8cf6f81bd1b27e866bc235940b93bb902210fd7980bad46c18051871f658` | `6d7c3ee1-b1b2-4449-a292-da70fe626fd0`, R2 |
| `user06.prtz` | `1da36cf37a9aafcec6403dfeaf3af16de1225252922a9c3d937fa9bf251df9d9` | `4670b641-f315-4bc9-b517-4797b4272892`, R3 |

Final saved-case checks require calculation success, native validity, authored
definition and side preservation, save/reopen, owned faces and agreement between
the lightweight preview and calculated boundary without preview calculations.
06 additionally retains and calculates its downstream Fillet 03.

The five earlier Fillet snapshots and the five closed Chamfer variants are
retained as regression inputs. Their scope and the explicit 04/14 preview bound
are documented in [the preceding verification](20261117-support-driven-edge-blends.md).

## Regression matrix

The new independent rib fixture joins a rounded rectangular bar to two cylinders.
Its two closed routes have different topology counts. Both edge orders and two
Boolean tolerances check one valid solid, manifold boundary, owned treatment
faces, tangent support contacts, volume invariance and unchanged remote material.
The existing oblique-route matrix independently checks radius and contact normals.

The new circular-inference GUI matrix covers segment/rectangle, native/external
circles, horizontal/vertical orientation and inner/outer contacts. It creates
geometry through mouse input, checks H/V or S preview labels and persisted C,
then performs three actual mouse drags. Its dimension matrix covers segment and
point-pair lengths, projected lengths, axis coordinates and both rectangle side
lengths, both point orders, driving/reference and locked/unlocked states.
It independently checks contact, H/V, symmetry and dimension equations after
successive numeric edits and drags. Impossible values and locked reference
dimensions must be rejected atomically. Native R/D and redundant/reference
angle variants are included. Save/reopen and GUI Undo/Redo retain source identity;
external source Sketches and cached reference definitions remain unchanged.

Related existing suites cover native tangent junctions, circle R/D, trimmed
circular fragments, external circle trimming, angle sectors, ordinary dimension
editing, rectangle external contact, symmetry, parallel inference and endpoint
priority. This is a finite matrix; arbitrary freeform blends and every radius,
view orientation or external-reference type are not claimed.

Mirror GUI verification clicks the actual source field after changing the camera,
checks unchanged camera/document state, the new XY default, YZ preservation on
reopening, independent source inspection, Cancel and Undo/save branching.
Recovery tests cover failed and successful Fillet/Chamfer creation and editing,
OK/middle-button confirmation and unchanged OK.

## Executed results

- The final 19-test CTest selection passed in 87.62 seconds: the complete
  Sketcher contract, native tangent contacts, mechanisms, engineering profiles,
  trimming, angle sectors, symmetric junctions, rounded wings, equal circles,
  coincident reference circles, dimension/curve commands, rounded mirrors,
  pattern dimensions, edge-treatment commands, derived copies, the two changed
  GUI contracts and five-language translation validation.
- The circular-inference GUI contract passed all 16 creation/orientation/support
  cases and 576 dimension variants, with the repeated edits, native and mouse
  drags, independent equations, reference protection, save/reopen and Undo/Redo
  checks described above.
- Nine related GUI contracts also passed: dimension edits, external-circle trim,
  rectangle external contact, circular alignment, symmetric rectangle, common
  tangent, parallel inference, endpoint priority and equal-endpoint inference.
- The full independent native Fillet suite passed in 63.38 seconds, including
  the four new two-boss rib order/tolerance variants and prior route cases.
- Both new saved Fillets and all five preceding saved Fillets passed calculation,
  preview/boundary comparison and native reopening. The recreated 04/13 volume
  is `1028534.41136416 mm3`; 06/02 is `700730.017438195 mm3`. Their maximum preview
  residuals are approximately `2.20e-13 mm` and `2.54e-8 mm`, respectively.
  The 06 transition's 44 sampled seams have a maximum normal angle of
  approximately `1.96e-5 degrees`. The 04 terminal cuts are not all tangent.
- The preceding 04/14 preview residual improved from about `7.8e-5 mm` to
  `7.46e-10 mm`; its existing non-tangent terminal cuts remain intentional.
- The complete six-feature 06 history, including downstream Fillet 03, calculates
  and reopens with no calculation errors and valid native topology.
- All five saved closed-Chamfer variants pass again: A x A, A x B and A plus
  angle, with both Flip states for asymmetric modes.
- Separate actual GUI recovery runs pass for Fillet (21.67 seconds) and Chamfer
  (21.27 seconds): failed OK closes without insertion/replacement, successful
  creation/editing, native reopening, Undo/Redo and unchanged OK. No invalid
  Boolean input was reported in their nine and seven measured batches.
- The actual GUI regenerated both disposable user models, captured all seven
  standard views, and passed cold Properties preview/Cancel checks. Published
  wires exactly match their expected four and eight edge groups; all four and
  22 source-node labels respectively remain present. Overview and enlarged
  captures were visually inspected; they supplement the native checks.

These are host observations, not general performance promises. Successful
construction is still conditional on the authored geometry and radius.

## Compatibility and remaining gates

There is no native format change, factory-asset conversion, dependency upgrade or
shared placement change. The local `zima-cad.bat` entry point is unchanged.
No new application text or translation key is introduced. The earlier green
empty-selection prompt and red actual errors retain their five-language catalogs.

Packaged checks and publication evidence will be recorded after execution.
Linux compilation and native execution require the Linux host.
Deprecated OCCT API modernization is explicitly scheduled after this release,
followed by its own verification and Windows release.
