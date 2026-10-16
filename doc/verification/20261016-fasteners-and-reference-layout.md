# Fastener, external-support and reference-layout verification

## Scope and causes

The original user bolt and FORM documents are preserved. Work uses independent
copies and repository-owned factory definitions. Native formats, templates,
OCCT 8.0.0, geometry-side identities and shared container placement are unchanged.

The bolt's supported angular profile was geometrically feasible. Sequential
line projections could rotate it toward the angle and then back toward its
supports. Angular value edits and refreshed supports now reuse the existing
bounded coupled equation seed, followed by the ordinary constraint validator.
This is a fallback after a conflicting ordinary solve, not a new parallel solver
or a weaker acceptance tolerance. Invalid edits remain atomic. Driving values
request geometry; reference values derive from geometry. External supports remain
read-only.

The three previously rejected Ventilation Window dimensions had a separate cause:
native document copying rebased `external-reference:<id>` as if it were a file
path. Linked spline poles then stopped following their refreshed source. Copying
now preserves this internal marker while still rebasing actual file paths.
The factory's two affected authored spline links were rebuilt with native code,
calculated, saved and reopened. Body roles and surface area were retained.
Existing inserted user definitions are not silently rewritten or migrated.

Native sketch strokes render after coincident read-only reference wires. Origin
axes retain their earlier pass. Picking and reference identity are unchanged.

## Finite Sketcher matrix

`sketch_supported_angle_matrix.inc` uses the real bolt angular profile and covers
64 combinations: four quarter-turn orientations, two mirrors/contact sides,
both arm-selection orders, native fixed line versus external face supports, and
both numeric lock states. Each combination exercises 30°, 35°, 45° and 25°
successive edits and independently measures the angle from its actual endpoints.
It checks actual motion, unchanged support data and native Sketch serialization.

The matrix also exercises reference/driving changes, reference-value non-motion,
fully constrained locked repeated drags, three permitted underconstrained drags,
atomic impossible edits with fixed points, and movement after shifting a support
while retaining its angular equation. The 64 combinations pass.

The actual Windows GUI regression opens the owned Sketch through its ordinary
Tree properties path. It picks and drags the angular endpoint three times along
its external face support, checks movement and support incidence, and exercises
Undo/Redo. Actual dimension-label double-clicks change -35°, -45° and -30°;
native saving and Sketch-definition Undo/Redo pass. A framebuffer capture verifies
the native/reference stroke order. Undo comparisons concern the authored Sketch;
they do not claim byte identity of other features' recalculated derived caches.

Fourteen related native suites pass, including Sketcher, keypoint contacts,
dimensions, reference commands/refresh, native documents, localization and factory
Ventilation Window insertion. The repaired inserted FORM accepts all ten tested
internal dimensions, with successive edits, save/reopen and Undo/Redo.
All four factory FORM GUI scenarios pass: Dimpled Hole, Bridge Lance,
Ventilation Window and Corner Gusset, including their established placement,
inspection, Cancel, Undo/Redo and native reopening checks.

This is finite coverage. It does not establish every dimension-kind combination
or trimming sequence on every angular profile. The related existing trim/contact
tests remain relevant; a dedicated post-trim matrix for this particular bolt
profile was not performed. Linux GUI/native execution requires the Linux host
and remains unverified.

## Library and tables

The four ISO 4017 models pass twelve successive shaft-length edits, dimension-
derived stock values, Undo/Redo, native save/reopen, exact thread-reference
retention and unchanged external head supports. Read-only factory SHA-256 values
remain unchanged during the test. Material, names in all five languages and the
ordinary native Up to thread contract are checked by
`tools/verify-fastener-library.py`. Geometry verification additionally recalculates
the saved models with OCCT and checks valid single-solid native roundtrips.

Independent table tests retain source cells through joining/transposition, check
the explicit M4 head-height correction and verify generated five-language output.
The shared branding test covers linked, unlinked and omitted logos. Native HTML
uses the existing Company website setting; changing or clearing it is checked
alongside language/theme changes. The original
counterbore image and actual native screw Drawing/illustration are local assets.

All 1,405 localized reference pages pass browser checks at both 1,100 and
1,920 CSS pixels: full available width, left alignment, logo on the right, loaded
local images, expected rows, sticky headers/identifying columns and dark contrast.
Remote assets are blocked. A4 print verification remains independent from screen
layout. Native File Manager checks exercise five languages and Light/Dark/Light
without a reload for the affected reference families.

Logs and captures are disposable evidence under `build/`; the native documents
and committed generators are the product inputs. Release acceptance is recorded
separately after packaging, signed-package checks and public download verification.
