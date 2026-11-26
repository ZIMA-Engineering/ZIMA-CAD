# CAM profile milling: implementation and verification

On 2026-11-26 the user approved this CAM stage, its stored definitions and
affected factory/template verification. Windows 2026112504 remains the accepted
release; this work is not in that ZIP. The local development entry remains
`zima-cad.bat` and the implementation is shared Windows/Linux C++.

Status: the scoped Windows development stage passed all ten native CAM suites,
313 full-application GUI assertions, both start-template unit modes and the
five-language localization gate. This is not a new packaged-release claim.

## Approved scope and invariants

Add pockets and inside/outside contours to three-axis milling, using preceding
native Sketch regions, including islands and separate closed loops. Preserve
the Sketch identity and include its exact definition and Body/work frames in
calculation invalidation. Extend nested CAM storage to schema 3; retain the
ordinary native document extensions and envelope. Controller postprocessors,
turning and rotary axes are subsequent stages.

Inputs are native profiles, explicit machining side/direction, tool geometry,
depths, allowances, step sizes and current stock/target/fixture geometry. Outputs
are generic NC blocks, independently checked swept material, collisions and
remaining stock. Means are the existing private CAM calculation/publication,
document session, native curve mathematics and exact solid simulation.

Generate the tool-centre boundary from the signed material region and cutter
radius. Preserve lines and circular arcs as analytic motions. Trim offset pieces
at their analytic intersections and classify them against the original region;
holes must remain obstacles. Other curves require an explicit bounded path
tolerance before approximation. Viewer tessellation is never a path input.

Opening/editing/picking uses existing native data and shared reference cells.
Only explicit Calculate generates paths and simulates solids. Cancellation and
stale calculations publish nothing; unchanged OK creates no Undo transaction.
Existing Facing/manual operations, clamping sides, fixture checks, target-gouge
reports and chained stock remain supported.

## Required finite verification matrix

- Rectangles, circles, mixed arcs/lines, concave regions, islands, separate loops,
  both contour sides/directions, multiple depth levels and positive allowance.
- Independently check offsets, complete arc extrema, swept/material volumes,
  machine limits, insufficient cutter access, invalid/nonplanar profiles and
  translated/rotated/reversed Sketch and setup frames.
- Source changes/loss, stale publication, repeated calculation, chained stock,
  native save/reopen, Undo/Redo, unchanged OK and Cancel.
- Actual GUI profile entry/replacement/inspection, NC stepping, all five
  languages, affected factory data and mm/in template creation.
- Record timings and operation counts; keep Windows and Linux results separate.

## Implemented behavior

Pocket and contour editors reuse the existing private setup draft, operation
ordering, required replaceable reference field and independent inspection eye.
Both Tree and View accept preceding Sketches. Child OK changes only the setup
draft; parent OK commits once. Closing a child restores the parent scene and
reference-entry policy. Profile source identity, complete native definition and
Body/work frames participate in dependency freshness.
The first profile editor expands to the shared natural size hint instead of
retaining the empty list's short window. Subsequent manual resizing is retained;
the shared SubWindow owns bounds, scrolling and control spacing.

Offsets use exact line/circle intersections, exact arc-extrema broad-phase
bounds and even-odd classification against the authored material region.
Analytic lines/arcs remain linear/XY circular generic NC motions. Ellipses and
native rational splines use the shared homogeneous curve subdivision routines.
A positive-weight control-polygon chord bound allocates half the requested
path tolerance to subdivision; compensation includes that bound to avoid
cutting through the true curve. This is independent of viewer tessellation.

Narrow necks may split a pocket into separate closed tool paths. A tool that
exactly fits a slot retains an open centre path; an exact-fit circular pocket
retains a centre plunge. Two-edge closed profiles sharing both endpoints remain
valid, while coincident retraced edges are rejected. All transfers retract to
clearance above the stock, target and fixture bounds. Entry is a cutting plunge,
not rapid motion through stock.

## Verified finite matrix

The native test targets are registered with CTest. The current Windows run uses
the repository's native Qt/OCCT build, with logs and binary SHA-256 values under
`build/cam-profile-20261126/`. Machine-readable retained evidence is in
`20261126-cam-profile-milling.json` beside this document.

| Contract | Evidence |
| --- | --- |
| Analytic compensation | Independent distances for rectangles, circles, semicircles, mixed arcs/lines and both contour sides; CW/CCW, positive allowance and successive depth levels. |
| Region ownership | Concave L, circular island, disconnected loops and a neck narrower than the cutter; paths remain on the permitted side. |
| Degenerate offset limits | Exact cutter-width slot and exact cutter-radius circular pocket; coincident retraced boundary rejected. |
| Noncircular native curves | Ellipse closest-point search bounds compensation independently; elevated ellipse, rational quadratic quarter circle, both normal signs and elliptical-arc orientations. |
| Validation and cancellation | Invalid/nonplanar/crossing profiles, insufficient access, invalid depths/step sizes and deterministic checkpoint cancellation. |
| Exact material | Radius-3, depth-1 pocket removes `9*pi` mm3; edited radius 2.5 removes `6.25*pi`; exact-fit radius 1 removes `pi`. A deeper cut removes `18*pi` and reports target gouging. Fixture intersections are reported. |
| Lifecycle and references | Native save/reopen, repeat calculation, Undo/Redo, no-op OK, cancellation, suppression/deletion/replacement, moved source frame and stale-result publication rejection. |
| Chained stock | Subsequent Facing consumes the calculated pocket stock; profile changes invalidate dependent setups and Undo restores their current state. |
| Existing CAM behavior | Manual programs, Facing, machine limits, exact linear/circular swept volumes, simulation, clamping/contact sides and existing setup lifecycle tests remain passing. |
| Actual GUI | Mouse selection in Tree and View, independent wire inspection without base-scene publication, private child edit, single parent commit, background calculation and NC stepping without CAD source regeneration. |
| Localization and presentation | New controls translated into cs/en/de/fr/ru; catalog/source coverage and live translated controls checked. Profile editor Light -> Dark -> Light captures retain the pending definition and document revision. |
| Start documents | Changed native writer refreshes/reopens six tracked Part/Skeleton/Assembly templates; actual New Document checks exercise active Body/component context and commands for mm and inch templates. |

The assertions describe a finite matrix, not exhaustive support for every
possible spline or arrangement. Checks after an initial solve alone are not
used as proof of editability. No Sketcher solver, dimension or dragging behavior
was modified by this CAM stage.

Measured Windows examples: the finite profile suite completes 32,264 assertions
in 0.303 s; the exact calculation/lifecycle suite completes 109 assertions in
10.978 s; the complete CAM GUI gate takes 10.793 s. The operation editor has
73 assertions, including first-profile growth and preservation of manual size.
GUI inspection verifies unchanged document revision and zero authored CAD
calculation increments. Explicit CAM simulation still performs its required
exact solid calculations.

Reproduction uses the ten `zima_cpp_cam_*_tests` targets named in the JSON
record. Run the native application with `--verify-startup --working-directory`
pointing at a **fresh** disposable directory and `ZIMA_VERIFY_CAM_BODY_ONLY=1`.
For start documents use `ZIMA_VERIFY_CONSOLE_ONLY=1`,
`ZIMA_VERIFY_NEW_DOCUMENT_OPTIONS_ONLY=1`,
`ZIMA_VERIFY_START_TEMPLATE_DIRECTORY=<repository>/config/templates`, and set
`ZIMA_VERIFY_NEW_DOCUMENT_LENGTH` explicitly to `mm` and then `in` in separate
runs. The UI contract executable with `ZIMA_VERIFY_TRANSLATIONS_ONLY=1` performs
source coverage/catalog checks and translated UI verification.

## Persistence and factory/protected data

The approved nested CAM schema is **3**, with intent keys `cam-intent-v3`.
Schema 2 CAM definitions are intentionally unsupported; there is no migration
branch. Required profile data resides in the native document. The ordinary Part
envelope remains 79 and all native extensions remain unchanged.

Structural inspection of all **1,190** tracked native factory assets finds
**zero CAM Bodies**, so this nested extension requires no factory model
conversion. The changed native writer saved/reopened the six ordinary start
templates; they remain byte-identical. Eleven direct native loads cover those
templates, both tool templates and the factory mill, end mill and vise. This
targeted check is distinct from a future release's full packaged-library gate.

A read-only structural inventory of the protected TOYOTA project's ten native
documents likewise finds no CAM definition affected by schema 3. No original
was converted or overwritten. This inventory does not claim a new regeneration
of those user models. Earlier exact-surface verification and recoverable-copy
evidence remain in the 2026-11-25 verification record.

## Corrections found during verification

The finite tests exposed and corrected circle endpoint ray-crossing roundoff,
double application of the elevation check to flattened curves, exact-fit pocket
collapse, and rejection of valid two-edge closed profiles. Rejection of an
actual retraced two-edge boundary remains covered.
Visual review also found the initially collapsed profile form; the GUI gate now
requires its final direction control to be visible in each tested theme.

One repeated GUI run reused an exported test NC filename and waited for the
ordinary overwrite prompt. That test process was stopped; subsequent runs use
new isolated directories. A concurrent link attempt failed on the running test
executable's Windows file lock and was repeated after it exited. Neither is
reported as a successful gate. The template harness now sets both `mm` and `in`
explicitly; an earlier case labelled mm had inherited the verifier's inch default
and is not accepted as mm evidence.

## Limits and subsequent work

This is three-axis, XY-parallel 2.5D milling with a cylindrical flat cutting
portion. Noncircular paths use the explicit conservative tolerance. Resource
limits bound subdivision and program size; unsupported or invalid inputs fail
without publishing partial material. Cancellation checkpoints are also present
during path generation, before native solid simulation.

Generic NC still needs a controller-specific postprocessor before executable
machine export. Adaptive/ramped entry, turning, rotary axes, workholding-library
placement and arbitrary-solid tool conversion are subsequent stages. Native
Linux execution and production machining are not verified here. Timings are
for the recorded synthetic Windows cases, not a general speed guarantee.
