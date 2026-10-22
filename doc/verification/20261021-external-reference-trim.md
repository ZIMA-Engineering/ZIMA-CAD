# External cap references and trim boundaries

The later [individual Reference entry correction](20261022-individual-sketch-reference-entry.md)
supersedes the whole-face mouse entry described here. New ordinary Reference
entry selects individual geometry; existing stored Face supports and the native
trim equations remain intact. The results below record the earlier contract.

Platform: Windows, current native main checkout, pinned OCCT 8.0.0 and Qt 6.11.0.
Scope: the 2026-10-21 `Projects/1000.prtz` report and its immediate dependencies.

## Authored user fixture

The last Feature, `Vytažení 004`, is Solid, while `Vytažení 001` is Thin with
mixed Extrusion/Revolution sides. The last Sketch contains one native circular
arc of radius 200 mm, zero native B-splines, a PointReference on its centre and
EqualRadius to an external source. The source's rational quadratic cache is an
exact circular representation, not evidence of approximate reconstruction.

The read-only native source check accepts all 38 preceding face definitions.
The seven rejected face definitions belong to the edited last Feature itself;
they remain invalid self/forward dependencies. A mouse test on a private native
copy selects a preceding cap face, inserts an infinite external face support,
commits through the real Feature editor, and checks Undo/Redo and native reopen.
SHA-256 before/after confirms that the original `1000.prtz` is unchanged.
Evidence: `build/1000-external-face-diagnostic.json` and
`build/1000-external-face-gui-tests.log`.

The final, more specific user-file test appends a private standalone Sketch
after `Vytažení 004`, in its existing Body, and selects each of that final
Feature's two caps in separate runs. Both actual mouse selections preserve
the exact offered source identity, create an infinite Face support, retain all
authored Feature parameters, and pass Undo/Redo and native reopen. This covers
the final Feature as a source, rather than only earlier caps while editing its
own Sketch. Evidence: `build/1000-final-feature-cap-gui-tests.log` (15.74 seconds).
Original SHA-256 remains
`351C173F9B5B552BFBDB3CB72D912E681F2299929F36E597C5BDF0FB34E9F685`.

## Finite regression matrix

| Area | Variants and independent checks | Result |
| --- | --- | --- |
| Straight external cutters | 10 cases: forward/reversed direction; finite/infinite Edge and Face, infinite Axis; exact endpoint on the line, unselectable read-only cutter | Passed |
| Persistence and transactions | Those 10 trims through DocumentSession Undo/Redo and actual Part save/reopen; original native geometry/contact IDs restored | Passed |
| Exact circular cutters | 4 cases: both parameter directions and both source insertion orders; two line intersections, three successive actual endpoint drags, exact radius equation, unchanged source cache | Passed |
| Finite arc cutters | 2 parameter directions; only the real arc branch cuts; three successive drags stay on the finite arc; coincident native circle creates no false cuts | Passed |
| Trimmed native circle | 8 R/D x driving/reference x locked/unlocked cases; 2 unsupported locked reference combinations rejected atomically; remaining cases have three successive value edits | Passed |
| Arc manipulation | Locked centre X/Y; driving R/D keeps fully constrained endpoints fixed and rejects escape; reference R/D leaves permitted dragging, checks actual motion, plane incidence and native radius equation | Passed |
| Existing shared behavior | Full native Sketcher, reference-command, dimension-command and external circular-profile suites, freshly linked | Passed |
| Mouse cap selection | Both start/end caps of Solid/Thin fixtures; candidate cycling behind thin rim edges, exact offered source identity, infinite support, Undo/Redo and reopen | Passed |
| Localization and appearance | Updated guidance in cs/en/de/fr/ru, both themes, live language/theme application | Passed |
| Mouse trim | External straight cutter, two successive gestures, immediate preview, Cancel restoration, finish/save, read-only source identity | Passed |

The combined seven-suite run passes in 28.62 seconds on this host. Evidence:
`build/external-reference-verification-build.log` and
`build/external-reference-verification-tests.log`. Timings are observations,
not performance guarantees. No new solid calculation is added to trim topology
or ordinary face-reference projection.

The native matrix consumes `cpp/tests/external_trim_boundary_tests.cpp`. The GUI
cap matrix is in `cpp/app/external_face_reference_ui_verification.inc`; the
external trim variant reuses the existing standalone trim GUI harness.

## Remaining limits and follow-on work

The new read-only cutters cover straight supports and certified circular curves
with exact caches. General external ellipse/NURBS/mesh-outline cutters, tangent
intersection discovery without an existing persisted contact, all trimming
combinations for elliptical/spline survivors, and every point/distance/angle
dimension arrangement are not certified by this finite new matrix. Existing
native suites cover their separately documented cases; this report does not
promote those into exhaustive acceptance of new combinations.

The actual mouse circle-to-external-line variant also passes (3.92 seconds),
including repeated gestures, Cancel restoration, finish/save and unchanged source
identity. Evidence: `build/external-circle-trim-gui-tests.log`.
After expanding the cap matrix to both sides, the freshly built application
passes all three affected GUI suites in 28.37 seconds:
`build/external-reference-final-gui-tests.log`. Production-source catalog
coverage and the full five-language UI translation suite also pass:
`build/external-reference-localization-tests.log` (24.12 seconds).
Linux execution is unverified. The approved Face x Sketch-plane intersection
mode and multiple-branch persistence remain separate implementation work; no
new format or branch-rebinding heuristic is introduced by these fixes.
The separate native [intersection foundation matrix](20261021-sketch-face-plane-foundation.md)
records its implemented calculation cases and remaining integration gaps.

Localization review: one product guidance string is updated in all five Qt
catalogs. Test diagnostics and project documentation remain English. No shared
placement contract, factory native model or personal library is rewritten.

## Final visible-face and naming update

The user renamed the Sketch command to **Reference** on 2026-10-21. The shared
label and selection/completion/error messages are localized in cs/en/de/fr/ru.
The completion prompt no longer exposes kernel implementation details. Catalog
coverage/validation passes in 19.19 seconds; actual command labels in windows
created for all five languages and both themes pass with all four Solid/Thin
start/end cap fixtures in 22.46 seconds. Evidence:
`build/sketch-reference-name-localization-tests.log` and
`build/sketch-reference-name-gui-tests.log`.

The final actual-file check differs from the earlier original-face picking
check: the visible start cap of feature 004 passes, but its end cap has no
matching offered current Face candidate, even after normalizing persisted
Boolean ancestry. This remains unresolved; removal/merging of that cap has not
been established as the cause. Do not report both actual-file visible caps as
accepted. Evidence: `build/1000-visible-final-cap-gui-tests.log`.
The original `Projects/1000.prtz` still has SHA-256
`351c173f9b5b552bfbdb3cb72d912e681f2299929f36e597c5bdf0fb34e9f685`.
The successful fixture checks and original-file checks use private copies.
