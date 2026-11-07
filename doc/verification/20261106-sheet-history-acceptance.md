# Sheet history and Sheet from Body verification (2026-11-06)

## Accepted behavior

Generated Flats and Bends are authored history features, not disposable output
owned by the Sheet from Body divider. A deletion removes only the selected
definition. A feature with a direct missing reference remains in the Tree with
its persisted parameters, owned Sketches and last numeric frame. Dependency-loss
propagation stops at a self-contained retained feature; Fillet, Chamfer and Shell
remain geometry-dependent exceptions.

The combined folded body is a result of those definitions. It does not replace
their individual identities. Removing the Sheet from Body divider therefore
restores the preceding solid without deleting or implicitly invalidating the
later sheet definitions.

## Geometry checks

Sheet from Body selects one actual outline segment by its endpoint pair and binds
both segment endpoints to the matching Bend endpoints, including reversed segment
orientation. Every branch is traversed and solved independently from the selected
starting Flat to its own terminal Flat. A length or sign correction from one
branch is never reused by another branch. Junction setback is calculated from
thickness and the angle between the meeting branches. The actual inner Bend edges
meet; an outer-skin intersection is not accepted as corner closure. The
right-angle fixture verifies the requested 11 mm minus 1 mm result, while oblique
and six-sided coverage exercises the angular path.
Imported STEP sheet geometry bypasses the additional setback because its authored
clearance is already part of the source model.

The sheet exchange suite verifies folded calculation, real Unbend and Bend Back,
editing a generated Bend, marker and middle-feature deletion, native save/reopen,
Undo/Redo, reference coverage and distinct final Flat ownership. The complete
suite passes on the Windows OCCT 8.0.1 development runtime.

## Representative deletion measurement

A disposable copy of the user's current six-sided document was opened and warmed
before deleting `Ohyb 12`. The operation completed in 874.304 ms. Exactly one
new feature, its directly dependent `Tabule 13`, was affected. The document also
contained the already invalid `Tabule 10`, so the post-delete unresolved count was
two. The calculation reported zero errors. This is a representative measurement,
not a universal latency guarantee.

## Follow-up endpoint and Unbend verification

The final native-solid conversion applies the calculated meeting setback to
both ends of every generated Bend. The endpoint arithmetic remains algebraic:
the established 11 mm extension minus 1 mm sheet thickness is 10 mm, while a
zero extension at an upper meeting end becomes a real inward setback rather
than remaining on the source edge. A fresh six-sided 3 mm fixture produced the
same `-3.4641 mm` endpoint correction at both ends of all six upper Bends; its
lower Bends retained equal corrections at both ends as well. Imported STEP
sheets still bypass this additional source-solid correction.

Warm Unbend creation measured 108.722 ms on the simple divider fixture and
538.003 ms on the fresh 29-feature six-sided fixture. Both results contained
zero calculation errors. The optimized path retains immutable authored sheet
operands, transforms independent additive regions in parallel, performs one
parallel union, and reuses reference/projection state only after every existing
prefix boundary has the same geometry fingerprint. An inserted or otherwise
nonterminal sheet-state operation remains on the full reference-resolution
path.

Deleting the same-Body divider and then every generated Flat/Bend now restores
current face ownership on the original solid. A regression selects the restored
Side A face and successfully runs Sheet from Body again without an intervening
Extrusion. The full Bend matrix also verifies that Insert Here before Unbend
materializes the actual pickable folded boundary wire, then preserves the
inserted Bend through Undo/Redo and native reopen.

The final warm `02.prtz` round-trip measurements were 678.751--697.925 ms for
Unbend and 306.337--327.674 ms for Bend Back over three clean runs. The generated
Flats no longer contain persisted helper guide segments. The five-Bend/six-Flat
branched `04.prtz` conversion follows each branch away from the selected face;
the user visually accepted that result. The final adjusted `02.prtz` corner still
requires the user's inspection in the development build.

## Regression scope

The following Windows native suites pass:

- sheet exchange, Profile, sheet-state, cone-state and twisted-sheet contracts;
- Flat, Bend, sheet-state and FORM command/geometry contracts;
- the sixteen-case Bend attachment matrix, cuts between states, Undo/Redo and
  cold native reopen;
- history deletion, dependency-layer, retained-history recovery, Body scale,
  placement references, Shaft Thread and multi-source Mirror/Pattern contracts.

Thirteen focused GUI contracts pass: measurement inspection, independent adjacent
constraint hover, localization catalogs, combined endpoint inference,
multi-source Mirror/Pattern creation/save/reopen/dimensions, surface and Origin
placement, FORM, Profile, Sheet from Body and same-Body sheet conversion. The
focused Shaft Thread Properties verification also exits successfully. Application
and test targets build successfully. Tests that require a temporary directory were
run with a repository-owned disposable directory because the sandboxed account
cannot write to the default Windows temporary directory.

No new user-visible string was added by the final corrections. Existing catalog
changes were validated for Czech, English, German, French and Russian. Native
Linux execution and the complete repository-wide test matrix are not claimed by
this Windows-focused verification.
