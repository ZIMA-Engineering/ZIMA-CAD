# Support-driven Fillet and Chamfer transitions

Build `2026111701`, shared C++ implementation, OCCT 8.0.1, Windows verification.
The preceding end-mode corrections are recorded in
[Material terminals](20261117-treatment-material-terminals.md).

## Cause and construction

Transporting one unchanged normal section is incorrect when the angle between
adjacent supports changes along the selected route. The reported examples
include plane/cylinder and cylinder/cylinder intersections, an earlier radius
consumed by a later treatment, simultaneous changes of both supports, and an
oblique junction with additive and subtractive branches.

ZIMA Fillet now intersects oriented offset supports to obtain a rolling center
curve and constructs its radius envelope with contacts on the actual supports.
The affected oblique three-branch node uses its actual angle. Constant-section
routes retain their established construction. This uses low-level OCCT geometry,
splitting, sewing and Boolean operations, not the complete OCCT Fillet builder.

ZIMA Chamfer develops tangent plane/cylinder support strips into metric charts.
It offsets the common boundary by A and B, maps the contacts back to the actual
surfaces, and joins them with ruled patches. A plus angle intersects a ray at
the requested angle with the opposite trimmed support. Contacts crossing face
seams are split at the actual shared boundary. Closed strips retain adjacent
turns so a contact can cross the authored route start without jumping layers.
The A side follows the selected seed and Flip; persisted side identities remain
separate. The new construction applies to closed routes. Existing open
line/arc/line routes and their independent end choices use the established
terminal implementation.

A calculation-local cache reuses identical contact samples only within one
unchanged route calculation. Native UV coordinates still come from the actual
support surface: recognizing an analytic cylinder does not establish that its
native parameterization matches a standalone cylinder.

Current analytic descriptors are derived from the actual oriented faces and
attached to edge-side references. Compact packets recover these descriptors
from the existing native face bindings without a BRep calculation. No native
format fields, placement rules, dependency versions or side identities changed.

## Saved examples

Only disposable copies under `build/test-models/rolling-junction-20261117` are
modified. Source snapshots:

| Document | SHA-256 |
| --- | --- |
| `04.prtz` | `7D7410E90640488265A4E0E5F2233DCCA97785BA3F5F56805CD09B7E401E7BDC` |
| `05.prtz` | `F7971979921F75F9CCE936DE98CF3D835ECBC69562CB9173D4C7D4CCA0F0F786` |

The final native checks rebuild history, require valid topology, preserve the
complete authored feature and volume on native save/reopen, and compare both
fresh and reopened previews against the calculated boundary. Preview generation
must perform zero Body calculations.

| Case | Authored radius | Preview/boundary maximum residual |
| --- | --- | --- |
| 04 / Fillet 09 | 5 mm | about `1.4e-14 mm` |
| 04 / Fillet 13 | 2 mm | about `2.4e-13 mm` |
| 04 / Fillet 14 | 1 mm | about `7.8e-5 mm` |
| 04 / Fillet 15 | 1 mm | about `5.4e-14 mm` |
| 05 / Fillet 04 | 2 mm | about `1.1e-7 mm` |

The nearly tangent 04/14 Boolean seam has an explicit `1e-4 mm` preview-test
bound; construction precision is unchanged. Sampled interior contact-normal
errors are at most approximately 0.00452 degrees for 04/14 and 0.01264 degrees
for 05/04. Terminal cuts are excluded from tangent-contact measurements. These
are sampled results, not a claim of exact G1 continuity everywhere.

The same nine-member closed 05 route passes Chamfer A=2, A=2/B=3 and A=2/30
degrees, including both Flip states for asymmetric modes. Each case checks valid
native topology, owned result faces, persisted definition/Flip/volume, and fresh
and reopened preview agreement. A/B preview error is about `3.3e-8 mm`.
The five final saved-case runs took approximately 12.2, 13.8, 12.6, 31.2 and
16.6 seconds respectively on the development host; these include regeneration
and verification work and are not general interactive latency guarantees.

## Independent regressions

- An obliquely trimmed rounded box exercises Fillet radii 1, 2 and 5 in both
  selection orders. Independent checks cover principal curvature, contact
  normals, face ancestry, topology and matching area/volume after reversal.
- A separately authored stepped prism trimmed by a cylinder has a 17-member
  nonplanar closed lip. All twelve combinations of Chamfer mode, Flip and
  reversed/reordered selection pass. Checks measure contact distance to actual
  result edges, angle on curved supports, one valid solid, owned faces and
  volume invariance. The angle bound is `2e-4 radians` at sampled contacts.
- A rotated and translated rounded box verifies current plane/cylinder
  descriptors, outward orientation and side identity through packet save/load.
- Existing terminal fixtures cover additive and subtractive line/arc/line
  routes, A/B ratios on both sides of one, 30/60 degree angles, Flip, oblique
  terminals, independent NoCut choices and reversed topological orientation.
- Command contracts cover rejected edits, locks, dependent references,
  unchanged OK, Undo/Redo and native persistence.

The final native Fillet, Chamfer, direct/batch, support and command suites pass.
The expanded Chamfer suite takes about 236.5 seconds. The method UI contract,
ordinary selection, creation and full treatment editing contracts pass. The
new translation test initially used an invalid empty factory constructor;
correcting its fixture and rerunning the five-language contract passes.
Logs: `regressions-final.log`, `translations-final.log`, `ui-final.log`,
`contracts-final-*.log` in the disposable verification directory.

## Interaction and localization

The localized selection prompt in Fillet and Chamfer Properties uses green
`#4DD811`; OK remains disabled until input is supplied. Actual errors retain
`#c64b4b`. Both operations are checked in Czech, English, German, French and
Russian under light and dark themes. Two new construction-error keys and the
closed-route validation wording are translated in all five catalogs.

## Scope and limitations

The implementation preserves tangency where the support-driven Fillet exists;
local node/terminal transitions need not all be tangent. Chamfer preserves its
authored distances or angle and is not itself a tangent fillet. The new closed
Chamfer construction requires tangent plane/cylinder support strips and a
consistent material sense. It is not an implementation of arbitrary freeform
or branching closed blends, nor of all varying-angle open Chamfer routes.

Some exploratory R3 edits of 04/13 and 05/04 fail the closed-shell check and are
rejected; their authored radii above pass. These unsupported candidates are not
accepted as approximate solids. No claim is made for every radius or topology.

Windows and Linux use the same sources and pinned dependencies. Linux compilation
and runtime verification were not performed on this Windows host. Portable
publication requires the separate committed-source packaging and signed-release
gates; local geometry verification does not replace them.

## GUI acceptance

The actual Windows application regenerated both saved models successfully.
Part 04's final treatment and Part 05's Fillet, A x B with Flip, and A plus
angle cases passed cold Properties opening, shared preview geometry and node
labels, zero additional draft calculations, and Cancel with unchanged revision.
Final and preview captures were inspected. The A/B comparison between the
Part-local packet and scene-normalized packet differs by only `1.94e-13 mm`;
the GUI check now uses a `1e-9 mm` geometric bound with identical sample counts
instead of requiring bit-identical floating-point coordinates. Native
preview/boundary checks retain their independent bounds.
