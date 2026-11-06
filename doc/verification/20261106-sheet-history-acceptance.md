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
orientation. Junction setback is calculated from thickness and the angle between
the meeting branches. The right-angle fixture verifies the requested 11 mm minus
1 mm result, while oblique and six-sided coverage exercises the angular path.
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

## Regression scope

The following Windows native suites pass:

- sheet exchange, Profile and sheet-state contracts;
- Flat, Bend and sheet-state command contracts;
- history deletion, dependency-layer and retained-history recovery contracts.

Seven affected GUI contracts pass: Bend attachment, localization catalogs,
retained features, history deletion, Profile, Sheet from Body and same-Body sheet
conversion. Application and test targets build successfully. Tests that require a
temporary directory were run with a repository-owned disposable directory because
the sandboxed account cannot write to the default Windows temporary directory.

No new user-visible string was added by the final corrections. Existing catalog
changes were validated for Czech, English, German, French and Russian. Native
Linux execution and the complete repository-wide test matrix are not claimed by
this Windows-focused verification.
