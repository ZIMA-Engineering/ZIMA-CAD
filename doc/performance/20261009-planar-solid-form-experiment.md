# Planar solid FORM feasibility experiment

Date: 2026-10-09. Baseline commit: `43d0b1b1ff325a72cea3475e6a8102ad23d3f9c9`.
Platform: Windows x64, local Release, pinned OCCT 8.0.0.

## Inputs, means and outputs

The user requests one solid-tool authoring style for planar and corner FORM,
with outer-side attachment. A standalone Sketch line in `FORM_CUT` describes
actual piercing; an empty role describes a closed pocket. The experiment uses
`Projects/20-FORM.prtz`, retaining the original file. Its SHA-256 is
`4B7EB0179CF1D14E9C0C6E8832C99A1E6A2D8F117A16AAD03197879ACA96E4D4`.
The source contains two additive extrusions, one subtractive extrusion and two
R5 Fillets in one `FORM` Body.

The test-only probe is `cpp/tests/planar_solid_form_experiment.inc`, invoked with:

```powershell
build/cpp-windows-release/zima_cpp_sheet_form_geometry_tests.exe --probe-planar-solid-form Projects/20-FORM.prtz
```

It creates `Projects/test/20-FORM-solid-planar-experiment.prtz` with the original
solid and ordinary independent `FORM_CUT`, `FORM_FLAT`, and `FORM_SYMBOL` Bodies.
The Cut Body owns an XY Sketch with a line from (-50, 0) to (50, 0); the independent
symbol repeats that piercing line. This is an experimental authoring asset, not
a supported production library definition. The existing reader still rejects a
solid definition with a Cut Sketch. Native reopening checks the source histories,
Sketch count, Cut ownership and exact line endpoints.

The probe derives the base closing face from persisted calculated analytic data.
For the piercing case it resolves the line to exactly one additional tool wall
whose plane contains both endpoints. This restricted prototype does not claim
support for arbitrary lines, partial-face cuts, curved cuts or multiple openings.
The line is a semantic wall-selection input, never a zero-width Boolean cutter.

Outer attachment maps the tool base outward normal (-Z) to the sheet outer
normal (+Z). Stock occupies -t..0. A rigid half-turn around X places the tool
inside the stock side, following the corner tool's direction convention.
The completed solid removes the footprint, and the prepared wall is fused into
the remaining stock. These explicit, test-only Booleans use the existing 0.001 mm
tolerance, non-destructive inputs and exact native surfaces.

## Discovery matrix

For each direction the probe tests pockets and pierced windows at t = 0.5, 1 and
2 mm:

| Mode | Preparation direction | Equivalence limitation |
| --- | --- | --- |
| 0 | Native Shell on the complete authored rounded solid | Intended solid-tool direction |
| 1 | Zero-thickness Shell conversion, then native Surface Thicken | Same authored outer surfaces |
| 2 | Native Shell after omitting the two source Fillets | Deliberately different sharp source; not an accepted replacement |
| 3 | Hollow the sharp source, then execute the original R5 Fillet requests | Different operation order; thickness and inner transitions must not be assumed equivalent |

The probe checks strict BRep validity, one solid, volume and centroid/inertia
against the existing independent native test integration. Completed insertion
additionally checks strict BRep validity, connectivity and independent GK volume.
Failures remain recorded rather than being counted as accepted results.
Logs and incremental JSON are under `build/form-diagnostic/planar-solid-*`;
successful exact insertion shapes are disposable `.brep` diagnostics.

Initial investigation found the fully rounded solid unreliable: the 0.5 mm
Shell throws `Courbes non jointives`; the pierced 1 and 2 mm cases reject the
inner offset. Pocket walls passed isolated Shell checks at 1 and 2 mm, but
subsequent sheet insertion failed exact validity or Fuse completion. A surface
conversion also rejected duplicate stable face identity. The sharp source
completed connected insertions at 1 and 2 mm, taking approximately 0.03 seconds
for wall preparation and 0.03 seconds for the cut/fuse pair. At 0.5 mm the
existing independent inertia check rejected a numerical disagreement. No
precision, identity checks or acceptance thresholds were weakened.

These preliminary times are observations, not a speedup ratio: the successful
sharp source is geometrically different. They exclude source authoring/loading,
independent verification, native saving, UI commit and scene publication.
The final matrix and the subsequent corner investigation are recorded below.

## Completed planar matrix

Each cell covers both an unpierced pocket and a pierced window. The original
rounded `20-FORM.prtz` remains unchanged (SHA-256 verified again).

| Route | 0.5 mm | 1 mm | 2 mm |
| --- | --- | --- | --- |
| 0: original rounded solid | Shell exception | Pocket insertion invalid; window offset fails | Pocket Fuse fails; window offset fails |
| 1: original solid converted to surface | Duplicate native face identity | Duplicate native face identity | Duplicate native face identity |
| 2: sharp source, Shell | Independent inertia disagreement | Connected valid insertion | Connected valid insertion |
| 3: sharp Shell, then original R5 Fillets | Fillet fails | Final insertion invalid | Connected valid insertion |

Route 3 wall preparation takes about 0.26–0.28 s; the cut/fuse pair about
0.31 s at 2 mm. Routes 2 and 3 change the author's geometry/order and cannot
justify an equivalent-result speedup. Route 0's accepted isolated 1 mm pocket
wall takes 12.34 s but fails final insertion. A closed `FORM_CUT` or an open
slit does not repair these offset failures by itself.

## Corner surface investigation

The alternative requested by the user is to author both styles as surfaces.
Reproduction:

```text
build/cpp-windows-release/zima_cpp_sheet_form_geometry_tests.exe --probe-corner-surface-form config/lib/01-SHEETMETAL/01-FORM/CornerGusset90.prtz
```

This creates `Projects/test/CornerGusset90-surface-experiment.prtz` by adding
an ordinary zero-thickness Shell inside the existing FORM Body. Source entries
and their IDs are checked individually after reopening, because serialization
orders the feature vector by Body hierarchy rather than insertion into the
global vector. Cached surface area is also checked. The source library is not
modified. The inspection copy retains empty FORM_CUT and is **not** a completed
production surface FORM definition.

Native Surface Thicken on either side produces a valid wall, but neither side
matches direct solid Shell. With the inward side:

| Stock thickness (mm) | Solid Shell volume (mm³) | Surface wall volume (mm³) |
| --- | --- | --- |
| 0.5 | 105.747749 | 106.725267 |
| 1 | 206.308948 | 210.140604 |
| 2 | 392.372549 | 407.035946 |
| 3 | 559.192506 | 590.684175 |
| 4 | 707.770524 | 761.085293 |

Changing only the raw offset join from intersection to arc gives the same
volume disagreement. A further test intersects the raw inward surface wall
with the **original closed source volume**. All five thicknesses then pass
exact BRep validity, one-solid count, independent GK volume, surface area and
both directional Boolean difference-volume checks (existing 1e-6 tolerance).
This establishes a geometric route to equivalent corner walls with controlled
boundary trimming. It does not establish a surface-only implementation: this
probe still needs the original volume as a trimming tool.

The serial observation is approximately 0.02–0.03 s for native solid Shell,
0.13–0.18 s for native inward Surface Thicken, and 0.001 s raw offset plus
0.04–0.05 s raw trimming. These scopes differ: the raw route lacks native
ancestry, packets, persistence and insertion. None shows a verified product
speedup. No shared Surface Thicken or FORM implementation was changed.

## Decision and remaining work

Surface authoring can unify the visible structure, provided the attachment and
trim boundaries are explicit and preserved. The outer-side attachment requested
by the user remains the target. Existing closed FORM_CUT authoring remains the
safe baseline; an open slit and a no-cut pocket need distinct validated semantics.
Neither curve closure nor empty cut alone determines planar versus corner style.

Before promoting the surface route, replace the original-volume experimental
trim with a native persisted boundary definition, prove equivalent complete
sheet insertion and preserve source/fragment/side ancestry. Then verify
placement, Pattern/Mirror, Unbend/Bend Back, native reopening, Undo/Redo and GUI
editing. This investigation does not alter the protected common placement
contract or establish these dependent behaviors.

## Classification and product lifecycle

The earlier proposal to identify styles solely by counting origin-plane closing
faces is insufficient. This planar window also has a front wall on XZ through
the Origin. A solid can therefore provide two perpendicular candidates without
being a corner tool. Production integration needs explicit attachment intent or
another unambiguous authored attachment definition; cut presence alone cannot
classify an unpierced pocket. Recognition must consume persisted ZIMA data.

This experiment changes no production FORM operation, shared placement code,
native schema, templates, OCCT pin or calculation precision. The intended
production lifecycle remains the existing command lifecycle: open/preview and
reference inspection consume saved data; only changed OK or explicit Regenerate
calculates; unchanged OK performs no calculation or Undo; Cancel restores input.
No experimental route has been promoted to that lifecycle.

The new native authoring asset is round-tripped. Persisted insertion ancestry,
Pattern/Mirror, Unbend/Bend Back, Undo/Redo, GUI creation/editing/picking, general
style classification and Linux execution are not accepted by this kernel probe.
No Sketcher solving, dimensions, dragging or mouse behavior was changed. The
probe only authors and checks an ordinary fixed-coordinate line. Production
localization review finds no new FORM UI text.

## Final Windows verification

The serial focused CTest run passes 6/6 in 106.14 s: import contract,
localization/catalog coverage, standalone IGES GUI export, export commands,
existing planar FORM geometry and existing corner native roundtrip/copy.
Log: `build/form-diagnostic/form-iges-final-tests.log`. This checks the retained
production paths; it does not qualify the experimental routes for insertion.

The existing planar sample reports cold insertion 1.289 s, Unbend/flat 0.892 s,
shape restoration 1.538 s and native reopening calculation 1.393 s in this
serial run. These include production work missing from the raw wall probes,
so no cross-route speedup ratio is reported. Main-thread command lifecycle and
scene/Undo operation counts have not been measured for an experimental command
because no such product command has been implemented.

The development GUI and CLI build successfully at their existing launcher paths.
User configuration and the pre-existing unsaved-document screenshot are restored
from pre-verification backups and checked by SHA-256. Source `20-FORM.prtz` and
the production corner library are not edited. Linux remains a verification gap.
