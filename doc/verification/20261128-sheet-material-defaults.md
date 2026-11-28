# Sheet from Body material defaults and source Fillet edits

Status: local native and actual Properties verification passed on an isolated
copy of the reported model. All thirteen mandatory sheet gates pass locally,
including the expanded six-box matrix (1130.93 s). Final committed Windows
package acceptance must finish before release publication. Native Linux
execution remains unverified.

## Cause and repair

Changing Part sheet thickness from 3 mm to 2 mm updated the root Flat but left
attached Flats and Bends at 3 mm. Their thickness comes from the calculated
attachment boundary. The source-association refresh noticed a different input
key, but skipped reconstruction when the source solid faces and edges were
unchanged, then recorded the new key against the old boundary.

The refresh now checks the last calculated conversion thickness before accepting
that key. Thickness and K-factor changes rebuild the boundary through the
existing conversion path, which retains generated identities and authored
corrections. Successful reconstruction publishes its actual thickness. Explicit
conversion thickness overrides retain their existing meaning. The original
solid Fillet definition is unchanged by a thickness edit; changing that Fillet
separately still drives the converted geometry.

A file saved with the earlier stale boundary is repaired by explicit Regenerate
or the next changed modeling calculation. An unchanged Properties confirmation
remains a no-op; it is not repurposed as implicit regeneration.

There is no document-format, shared placement-solver, geometric-tolerance or
user-interface text change. Localization review found no new displayed strings;
the existing Sheet Properties dialog and translations are reused.

## Verified local behavior

The reported model has 25 generated Flat/Bend features in five independent native
material branches. The original user file was inspected read-only; all edits
were made on a separately copied file.

- Repeated Part thickness changes 3 -> 2 -> 3 mm update every bound feature.
  Independent analytic plane separation and coaxial cylinder-radius differences
  verify the native wall thickness within 1e-7 mm. The five branch solids remain
  separate and valid.
- Authored correction dimensions retain their identities, values, signed zero,
  solution side and driving/locked state. Thickness edits retain complete source
  Fillet definitions.
- The original solid Fillet radius changes 6 -> 7 -> 6 mm, changes actual
  converted material, and preserves corrections and source references.
- K-factor 0.37 and the original factor both retain corrections and thickness.
- Unchanged Sheet Properties performs no kernel calculation and creates no Undo
  revision. Changed thickness and source Fillet edits survive Undo/Redo.
  Undo deliberately retains dimension-number reservations; the derived
  definition fingerprint includes these reservations and is not an authored
  geometry-equality oracle.
- Native save/reopen and cold regeneration retain exact branch geometry.
  Unbend has zero measured branch overlap. Bend Back and Explode retain the
  folded native branches, checked by independent two-way Boolean differences,
  exact curve/surface probes and integral properties.
- The actual Sheet Properties GUI passes Cancel, changed/unchanged OK, view
  refresh for all 25 feature owners, Undo/Redo, source Tree reference checks and
  native saving. This isolated GUI run took 48.09 seconds overall, including
  fixture preparation and multiple operations; it is not an interaction-speed
  benchmark.
- Existing native Sheet Profile tests pass, including slanted Up-to targets,
  both material sides, corrections, linked next Bends, cold calculation and
  reopening. Eight independent branch contact tests and the Transition command
  CTest pass.

The aggregate metadata executable stops in its older Assembly angular-relation
fixture, which attempts to create a new material Revolution in an Assembly.
That action is forbidden by the existing Part-only material-authoring contract.
The sheet-default transaction is exercised directly by the new regression and
through the GUI; the aggregate metadata run is not reported as passing.

The passing mandatory boundary-skeleton gate additionally exercises inherited thickness
on corrected rectangular and asymmetrically drafted boxes, and fixed conversion
thickness on the rectangular box. These cases must also pass in the final committed
candidate. Local diagnostic logs and copies are under
`build/release-2026112803/user-thickness/`; user models are not committed.
