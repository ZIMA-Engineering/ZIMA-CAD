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

## Shared reference heading row (2026-10-17)

All seven reviewed datasets now put the chapter title, language links and brand
inside one shared header. The title starts at the content's left margin, the
brand remains right-aligned, and all three cells share their vertical center.
Long titles may wrap within their cell. The print stylesheet retains the title
and hides the language links and brand.

All 1,405 localized pages pass the browser layout checks at 1,100 CSS pixels.
The five-language capacity chapter additionally passes at 1,920, 700 and 320
pixels, including print-media title visibility and hidden navigation. Czech
captures at 700 and 320 pixels were inspected visually. These are browser checks;
this update does not claim a new native File Manager GUI run. All 35 table
unit tests pass. No localized wording changed; all five generated languages
use the same updated structure.

## Adaptive branding and Sketch points (2026-10-17)

The native File Manager regression now passes with the adaptive SVG helper.
It checks transparent pixels, unchanged corporate green and white lettering,
dark ink in the light theme and off-white ink in the dark theme. Previously
imported copies of the original artwork are included. Light/Dark/Light caching
and five-language HTML/automatic-overview theme changes pass without reloading.
The browser capacity-page fixtures also pass in both themes at 1,100 CSS pixels;
the Czech dark capture was inspected visually. The shared About-dialog call site
uses the same helper, but its separate GUI was not exercised in this run.

The focused native Sketch framebuffer regression passes in both themes:
construction and auxiliary strokes remain green, while their native points use
the ordinary foreground. The symmetric point fixture keeps automatic fitting
centered, so its pixel probes follow the actual displayed geometry. This is a
presentation-only change; it does not extend the solver coverage described above.
No new translated UI strings were introduced by either presentation change.

## Expanded fastener library (2026-10-17)

The factory manifest contains 174 ISO variants and 48 historical CSN variants.
All 222 installed Parts pass the native geometry checker: valid single solids,
nominal envelope, shaft cylinder, underhead fillet, selected bearing-face presence,
thread designation/pitch and partial-thread limits. Actual tip-cone angles/depths
and locking-hole radii, transverse axes and head-center positions are checked.
Product grade C and historical small-head variants have no raised bearing face.
ISO chamfered ends use 0.7P at 45 degrees; the independent checker verifies the
end diameter below the basic external-thread root and the ISO 4753 end limits.
Historical tips retain their separately reviewed 30-degree geometry.

All 912 authored Sketches solve fully constrained with residual below 1e-8 and
unbroken external references. A separate persisted-data audit verifies 666 circle/
polygon Origin centers, their actual coordinates and native C contacts. The
checker locks driving dimensions in independent Sketch copies and repeatedly
attempts three displacements of their first/last native points. Coordinates,
point identities and equations remain constrained. An unlocked driving value may
deliberately change during dragging; DOF=0 alone is not an immobility assertion.
This check concerns the stated points, not every point or every lock combination.

The final edit matrix covers all 222 models and 666 successive shaft-length
changes, each with Undo/Redo, native save/reopen and independent exact BRep/
Sketch/thread checks. Stock and positive mass update correctly. Cylinder, start,
end and chamfer reference identities remain stable. Candidate source SHA-256
values remain unchanged during verification. The initial end-form matrix was
superseded by this final ISO-tip matrix; unchanged historical results retain
their original evidence and matching source hashes.

Actual Windows GUI scenarios pass for a full-thread A model, a long partial-thread
B, a full-thread C and a historical small head with locking hole. Each exercises
three angle values, repeated endpoint dragging, Undo/Redo, native persistence and
reference-stroke capture. Their source files remain unchanged. This is four
representatives, not GUI execution of every model or every thread-property mode.

All 222 native thumbnails are 320 by 240 pixels; the minimum artwork margin is
19 pixels, with no clipped model. Ten independent native reference Parts and
Drawings provide three actual views and four resolved dimensions per standard.
Their attachments use current fillet/chamfer/flat geometry, and source paths are
sibling-relative native Part filenames. Dimensions remain resolved after native
reopening and source-path rebasing. Original reference artwork remains separate.
All five names and metadata labels are supplied; six-place kilogram masses are
positive, including the smallest models. Installation requires matching complete
verification records and all native documents/images before copying candidates.

Evidence includes `build/final-fastener-audit.json`,
`build/fastener-edit-matrix-final/results.json`, native command logs,
`build/final-fastener-acceptance.log` and `build/fastener-expanded-gui.log`.
Packaged acceptance remains a separate release gate. Linux execution remains
unverified.

After installation, all 365 localized fastener pages pass clean-browser layout,
local-image and dark-contrast checks at 1,100 CSS pixels, with no failed requests
or page errors. All 35 independent table tests and 14 whole-library layout/thread/
tolerance tests pass. The whole-library table-count assertion now respects the
established single dimensional matrix per size-range group while retaining
separate length tables. The native File Manager language/theme regression passes
against the installed figures; package-specific
execution is recorded separately. The five-language catalog/UI coverage check
also passes. No private browser data or personal settings are release inputs.

The installed native documents also pass an independent mass check against
exact BRep volume at steel density 7.85e-6 kg/mm3, within the six-place kilogram
rounding interval. Five representative installed models additionally pass 15
successive length edits with that exact mass assertion after every save/reopen,
using the final local 2026101701 runtime. Evidence:
`build/fastener-installed-native-check.log` and `build/fastener-mass-edits.log`.
