# Planar FORM stock sides, complete skin and circular measurement

The seating convention described below records the initial investigation.
The user's subsequent direction and starting-skin clarification supersedes it;
see [the current verification](20261016-form-starting-skin-and-continuous-metric-table.md).

## Changes

Bridge Lance's symmetric surface extrusion contains two native shells sharing
their junction edges. Ordinary Thicken correctly consumes its selected shell;
FORM incorrectly used that same selection without first collecting its complete
authored skin. One half could therefore disappear while the result still passed
solid validity and connectivity checks.

FORM now joins the already connected native patches before thickening. Shared
topology retains its identities, and the chosen anchor retains its orientation.
The existing ancestry-preserving sewing operation handles separate patch
boundaries when necessary. Disconnected/nonmanifold input and unconsumed faces
are rejected. Ordinary Thicken and the protected shared placement implementation
are unchanged.

The Dimpled Hole and Bridge Lance factory definitions now set `form_outward`,
matching Ventilation Window's stock-side and thickness-seating convention.
Their authored skins were already on the positive side of their cutting datum:
no Sketch, profile, dimension, Body placement or original working document was
mirrored or edited. The flag controls thickness side and seating, not reflection
of the authored skin. Replacement cuts remain two-sided. Select the stock face
on the desired raised side. Existing inserted definitions remain independent;
replace their definition explicitly to adopt the revised factory intent.

The existing planar FORM reference axis is preserved: owner is the inserted
feature, key is `axis`, origin is the insertion point, direction is normal to
the selected support and display length is 10 mm, without endpoint handles.
No additional source-extrusion or centroid axis was introduced.

Circular measurement now derives diameter from the persisted exact rational
curve. Every quadratic span must satisfy the circle and plane equations;
display tessellation and arc length do not determine diameter. The derived value
belongs to transient inspection geometry and is resolved again when reopening
a saved measurement. No native format, start template or saved scalar schema
changes. New text is translated in all five supported languages.

Two transparent PNG thumbnails were captured from the complete native factory
FORM skins through the existing Windows viewer. They are bounded to 320 by
240 pixels and were visually inspected. User-edited directory metadata was
preserved; the exact-extension thumbnail filenames use existing lookup rules.

## Verification

| Area | Matrix and result |
| --- | --- |
| Dimpled Hole and Bridge Lance | Thickness 0.8, 1.0 and 1.2 mm, top and bottom stock faces: 12 successful native insertions |
| Complete skin | Every authored skin mesh vertex is within one stock thickness of the exact inserted B-Rep; this check rejected the previous half-bridge result |
| Bridge symmetry | Isolate raised material from the original stock; its native common volume with its reflection equals its complete raised volume |
| Cut and side | Exact solid classification rejects original stock below the opening and retains a point outside the replacement footprint; signed bounds keep material on the chosen raised side |
| Solid and analysis | One connected valid solid, independently integrated native volume and available inertia checks |
| Dependencies | Existing Bridge point-stop matrix: source width 24, 18, 22 and 20 mm, with exact symmetric extrusion bounds |
| Persistence and history | Insertion Undo/Redo, Unbend/Bend Back, flat precut volume, save/reopen, cold regeneration, embedded role identities and manufacturing symbols |
| Planar FORM axis | Native owner, insertion point, normal, 10 mm length and absence of endpoint handles before and after persistence/regeneration; GUI checks cover Dimpled Hole, Bridge Lance and Ventilation Window |
| FORM GUI | Actual support-face picking, rotation and positioning, rollback, unchanged OK, replacement, Cancel, Undo/Redo and save/reopen passed for all three planar definitions |
| Measurement native | Exact circular length and diameter after packet serialization; sparse quarter-circle arc; translated/scaled exact curve; rejection of a noncircular rational curve; saved measurement native reopen |
| Measurement GUI | Actual circular-edge mouse selection, length plus diameter, localized labels and unchanged document revision: cs, en, de, fr and ru |
| Localization | Five catalogs contain the same 2490 unique nonempty keys; placeholders match. Existing translation/source coverage and catalog UI contract passed in all five languages |
| Adjacent native suites | Eight suites passed: measurement geometry, measurement commands, measurement edit commands, FORM definition, FORM commands, factory corner, factory window and ordinary surface thickening |

Windows uses the existing shared C++ implementation with Qt 6.11.0 and pinned
OCCT 8.0.0. Linux execution was not verified. This matrix does not claim coverage
of every custom disconnected skin, arbitrary tiny arc or nonuniformly scaled
Assembly measurement. Original `Projects/FORM-KOULE.prtz`, `Projects/FORM-UCHO.prtz`,
personal settings and protected pre-existing library edits retain their recorded
hashes. The normal `zima-cad.bat` continues to launch the current local build.

## Evidence

Local diagnostic logs are disposable build outputs:

- `build/bridge-final-axis.log`, `build/dimple-final-axis.log`
- `build/form-side-native-regression.log`
- `build/measurement-diameter-reopen.log`
- `build/form-side-translations.log`, `build/form-side-localization.json`
- `build/form-side-*/stdout.log` and `stderr.log` for the GUI cases

This change updates the local development build and factory data; the published
2026101601 archive remains immutable.
