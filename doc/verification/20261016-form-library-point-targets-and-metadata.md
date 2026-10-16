# Form libraries, point extrusion limits and metadata columns

## Scope and implementation

Dimpled Hole and Bridge Lance are independently namespaced native copies of
FORM-KOULE and FORM-UCHO. Source working documents remain unchanged. Each copy
uses ordinary Part Body ownership and the four FORM role names. The dimple
retains its authored flat precut and two construction centerline circles;
the bridge has an empty flat role and a construction rectangle symbol.

Real insertion exposed three native topology gaps: toroidal parameter seams
were counted as missing authored edges; closed circular surface rims lacked
native vertices; and coplanar closing walls of a thickened tangent shell could
have several exact parent rims. The fixes retain seams as parameterization
artifacts, derive missing rim vertices from persisted incident-edge ancestry,
and encode the complete exact parent set for merged closing walls. Existing
single-parent wall identity is retained. No side, signed-zero, oldest-face,
protected placement, dependency version or native format contract changes.

Extrusion Up to consumes a persisted point reference from preceding native
geometry. It resolves the current source position in the existing feature frame
and uses a perpendicular stop plane. Symmetric extrusion mirrors this limit.
The generalized Feature and ordinary Extrusion paths share this interpretation;
rotation does not gain point targets. Changing an ending to Up to arms the
existing reference-entry control. No new placement solver is introduced.

File Manager custom columns now come only from explicit Directory/Parameters
lists and metadata includes. Native data remains authoritative for current
values, labels and units. Browsing cannot discover or rewrite column lists.
Sixteen factory directories received explicit lists preserving their preceding
native columns, including all material properties. Existing configured lists
and independent working-document content are preserved.

## Verification record

Native point targets pass 18 result/direction/extent combinations (solid,
thin, surface; forward/reverse; one-sided, symmetric, two-sided), three successive
source moves per combination, independent bounds/volume/area equations,
Undo/Redo, invalid-source rejection and cold save/reopen. Two generalized Feature
cases additionally cover symmetric and one-sided repeated source moves.
Shell command and Surface Thicken regression suites pass, including real input
faces, curved offsets, ancestry, sides, persistence and unchanged transactions.

Final factory definitions each pass six insertions: 0.8, 1 and 1.2 mm stock,
both planar sheet sides. Checks include connected exact solids, flat precut
volume, Unbend/Bend Back, drawing construction strokes, immutable embedded
source, Undo/Redo and cold native reopen. The point-dependent bridge additionally passes successive 24, 18, 22 and 20 mm
cut-width dimension edits with independent exact extrusion bounds. The four
factory definitions and ordinary FORM commands pass their native regressions.

New runtime guidance and command validation are translated in cs/en/de/fr/ru.
Metadata identifiers and user-authored model names are not translated.

## Explicit limits

This is a finite Windows verification; Linux execution remains unverified.
The point target matrix uses native earlier Sketch points. Every possible
external support, orientation, deleted-source repair, point lock state and
arbitrary curved target combination is not claimed. The preceding Sketcher
verification documents retain their own complete matrices and explicit gaps.
No native format version or factory start-template contract changed.

The new definitions author multiple surface patches. FORM sews them during
insertion. The supplementary ordinary Thicken GUI check remains attached to its
original single-shell Window fixture: selecting a face in an unsewn authored
compound legitimately consumes that face while retaining its other patches.
The new-definition GUI matrix covers the actual FORM insertion/editing workflow,
not a claim that ordinary Thicken consumes every separate authoring patch.

## Final local Windows acceptance

All 15 focused native suites pass: 14 passed together in
`build/form-final-native-matrix.log`; the remaining centerline suite passes in
`build/profile-centerlines-final.log` after correcting its comparison of an
outdated derived calculation-cache stamp. Its authored history comparison is
unchanged in scope. Independent geometry and reference assertions remain enabled.

Both new FORM GUI workflows pass actual file selection, face hover/click,
in-plane rotation, independent inspection, rollback, unchanged OK, definition
replacement, Cancel, Undo/Redo and save/reopen. The point-limit GUI passes automatic
entry activation, actual preceding-Sketch point selection, Cancel/OK, native
save and Undo/Redo. The workspace File Manager GUI passes native values,
unsaved-source authority, editing, localized material labels, independent tabs
and File Open. Evidence: `build/form-new-{DimpledHole,BridgeLance,point,manager}`
stdout/stderr logs. The Bridge Lance insertion image is visually inspected.

Catalog validation reports 2,489 unique nonempty keys in each of five matching
catalogs. The native five-language translation/material-manager matrix passes
(`build/form-final-translations.log`). Packaging and publisher suites pass 31
cases, with two Unix-only skips on Windows. Native GUI checks retain their actual
Windows platform; Chromium emits graphics diagnostics while the asserted
File Manager workflows complete successfully.

The standalone File Manager suite also passes its new metadata-only column
checks, absent metadata, refresh, native hidden parameters, included lists,
editing/protection, native mirrors and independent tabs. Its existing three
engineering-reference groups pass five-language Light/Dark/Light HTML checks.
Evidence: `build/file-manager-columns-final.log` (native Windows Qt).
