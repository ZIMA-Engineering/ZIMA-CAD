# Exact profile limits and solid-skin extraction

## Inputs, means and outputs

Inputs are native Sketch profiles, the authored rotation side/draft angle,
stable original target references and the calculated input solid. Outputs are
exact limited geometry with Sketch topology parents, or zero-volume surface
pieces with explicit input-topology parents. Means are the existing profile
transaction, Shell rollback/selection, persisted viewer packets and operation-local
OCCT calculations. Independent checks project calculated end geometry onto the
exact target support, validate BRep shapes and compare volume/area with analytic
box/sphere results.

## Revolution Up To

Planes containing the rotation axis retain the established oriented uniform-angle
path. Other planes and general surfaces use exact circular-trajectory contacts
with the original bounded native face. Sampling establishes a temporary overrun,
not the calculated cap. Exact half-space Common removes that overrun; validation
requires the complete start to survive and the complete overrun end to disappear.
Ambiguous, tangent, unreachable and overlapping limits are errors. Two-sided
rotation counts the actual contact angle, excluding the discarded overrun margin.
Symmetric ends reflect the target across the original profile plane.

Exact limited rotations retain the valid Boolean trims instead of applying the
usual same-domain unifier afterwards. On two sectors joined across a cylinder
seam, OCCT unification changed a valid 629.732505 mm³ result into its complement
with -2511.860149 mm³ volume while still passing the BRep check. The regression
therefore checks both exact validity and twice the independently measured
single-side volume. Ordinary uniform rotations retain their established path.

Generated side faces remain children of Sketch curves, longitudinal edges remain
children of Sketch points, and replacement rims/endpoints retain those same native
parents. The cap remains a child of the profile region. The kernel request is
transient; the native Feature already owns the target reference and side choice.
Properties, picking and wire previews consume persisted data only. Preview angles
from viewer triangles never define the exact calculated end.

## Drafted Extrusion Up To

Solid results retain clip-then-draft calculation. OCCT cannot reliably draft a
curved free rim on an uncapped surface shell, so that case drafts the overrun
prism before exact clipping. Both paths retain source-curve/point end identities.
Only entities actually present after Common are passed to Draft. A calculation
fingerprint version invalidates reuse of older limited-Extrusion ancestry when
calculation is explicitly requested; opening a saved document does not regenerate.

## Surfaces from solid

The separate command consumes the existing Shell lifecycle. Native Shell thickness
zero identifies the surface mode; ordinary Shell UI/CLI still requires positive
thickness, and editing cannot switch between modes. There is no added persistent
field, extension, sidecar or factory-template structure. Face selection uses the
real calculated insertion/rollback boundary. The action availability uses that
same boundary rather than the final Body volume.
Only material-solid faces are offered for removal; independent surfaces remain
passive context and keep their original ownership.

The exact solid faces and shared edges are retained. Removing faces partitions
the remainder into connected shells without sewing, fitting or offsets. Each new
face/edge/point identity contains its original owner and parent key. Unrelated
surface geometry retains its ownership. No material solid remains, even when the
skin is closed. Surface rendering and downstream surface Fillet use the existing
surface classification. Family and View expose no thickness dimension in this mode.

## Interaction and verification scope

Creation/editing share one internal Properties window, OK/Cancel and middle-button
confirmation. Cancel restores input/history; unchanged confirmation performs no
calculation or Undo transaction. Fill/Trim allocate extra height to their lower
tables; Sewing/Intersection retain compact two-row viewports. Upper fields and
row heights remain fixed. All new text is localized in cs/en/de/fr/ru.

Focused tests cover forward/reverse and solid/surface angular ends, exact native
general-surface caps and centroid endpoints, source edits, symmetric ends,
fingerprint invalidation, unchanged OK, Undo/Redo, save/reopen and cold regeneration.
Draft cases include positive/negative angles on general surfaces for solid and
surface outputs. Solid-skin checks include closed/open boxes, disconnected
remaining faces, spheres, mixed Bodies, source edits and downstream yellow Fillet.
Actual GUI checks cover reference picking, cancellation, persistence and reopening.

Synthetic native curved-rotation calculation is approximately 100 ms on the local
Windows Release build; it is not a timing promise for arbitrary surfaces. Existing
Fill Properties measurements still show one base-scene publication per opening
and unchanged mesh hashes after confirmation. Release evidence and inherited
regression limitations are recorded separately.
