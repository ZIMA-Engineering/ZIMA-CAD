# Keyway placement and Part mode presentation

Baseline: `3c490dd652a2c639878082acf4f3d58ab18ac9ae`.

The user approved removing the independent keyway angle, including the native
definition change, on 2026-11-28. Circumferential position now comes exclusively
from the existing Container placement: the contact ray for surface placement,
or local X for an Origin on the support axis. The keyway no longer applies a
second rotation, exposes a lower Angle field, or publishes an independent
angular inspection dimension. Axial extent, catalogue sizing, manufacturing
children and roughness remain on their established paths. The shared placement
solver and its reference, side, angle-display and orientation contracts are
unchanged.

The same request subsequently specified the Part mode order Modeling, Surfaces,
Sheet Metal, Mechanics, and renamed the former visible MECH label to Mechanics
(Czech: Mechanika). The button strip and Applications menu use that order.
Existing enum values, action IDs and document mode identities are unchanged.
The existing translation key supplies the new name in all five languages.

## Native data scope

The keyway parameter object now requires `version: 2` and omits `angular_offset`.
Unversioned keyway definitions are rejected with a localized error rather than
silently ignoring their extra rotation or accepting a stale calculated result.
There is no legacy loader or automatic conversion. Document extensions and the
unrelated Part/Assembly payloads remain unchanged. User projects, including the
protected Toyota project, were not opened or modified for conversion.

The read-only inventory in `build/keyway-placement/factory-inventory.json`
examined 1,259 tracked native assets under `config` and `cpp/tests/fixtures`.
None contains a keyway definition, so no authored factory model needs conversion.

## Woodruff floor regression

The expanded placement matrix reproduced a missing-floor error at a 90-degree
stored reference angle. The Boolean had split the cutter's End-side cylindrical
floor at its seam; the old literal face-key comparison missed its descendant.
The command now uses the existing persisted ancestry matcher to retain the
authored End-side cylinder, for both its measurement and floor surface texture.
Its axial measurement plane uses the existing certified analytic cylinder
generators. A unique incidence check selects the floor branch. The implementation
does not match by proximity, create topology identities from triangles, switch
cutter sides, or modify the shared placement/intersection implementations.

The native matrix checks the direct Woodruff depth equation to 1e-8, stable
authored generator identity, and both resolved texture attachments after commit,
cold readback, and two successive source-radius edits. The original seam and
subsequent missing-original-intersection failures are retained in ignored local
logs; the final native run passes with neither test disabled.

The additional actual-dialog test then exposed an independent creation/editing
ordering bug: switching from a parallel key to Woodruff validated the previous
zero disc diameter before selecting the Woodruff catalogue row. A Woodruff
shaft's measurement station is its authored cutter centre, independent of disc
size. The draft now obtains the catalogue dimensions at that exact station and
then performs the unchanged full extent/geometry validation. Other keyway types
keep their existing station path. The GUI case confirms this type change with
both textures enabled, checks the persisted depth equation, and restores the
parallel key with Undo.

## Verification scope

- Native keyway definitions and persistence; missing/old payload version rejection.
- Placement-owned radial direction, absence of the old dimension, and zero solid
  calculations while inspecting or changing numeric Origin orientation.
- Numeric axis-Origin angles 0, ±0.01, ±90, 180 and 360 degrees.
- Cylindrical shafts, Woodruff shafts, conical shafts, cylindrical hubs and
  generator-parallel conical hubs: four reference angles, both reference sides
  or axial directions as applicable, Undo/Redo, cold readback and successive
  source-diameter edits.
- Existing native keyway variants, generated Sketch editing, decomposition,
  dependent features, catalogue selection, textures and failed confirmation.
- Actual workspace creation, reference picking, repeated shared-angle edits,
  Cancel, changed/unchanged OK, angular Undo/Redo, persistence, whole-Origin
  placement and the existing mouse selection/inspection checks.
- All five localization catalogs, fresh localized workspaces and shared guide text.

## Windows results

- Native keyway suite after the catalogue-order correction: passed, 43.45 s.
- Keyway dialog contract: passed, 1.60 s. The roughness expansion fixture now
  reserves enough initial width for its optional controls before testing height
  growth; its original unchanged-width/no-clipping assertions remain intact.
- Localization catalog contract in the combined run: passed, 21.50 s.
- The first requested-language workspace runs all loaded the console fixture's
  forced Czech locale. Visual inspection rejected those guide captures. The
  replacement runner omits the console-only flag and uses each isolated locale
  configuration directly.
- Actual workspace verification, including the Woodruff type switch and depth
  equation, passed in Czech, English, German, French and Russian. Visual review
  then found missing existing translations for Depth and None. Both keys were
  supplied in all five catalogs and the synthetic source feature uses the active
  localized name before capture; persisted user-authored names remain untouched.

- Final fresh workspace runs passed: cs 15.270 s, en 15.230 s, de 15.419 s,
  fr 15.406 s, ru 15.320 s. The captures show the placement angle without a
  second keyway angle. The Czech Woodruff view shows the curved floor and the
  requested mode order. The corresponding native synthetic model is retained
  as `woodruff-gui.prtz` beside each run's local screenshots.
- Final catalog verification after adding the two missing keys: passed, 23.84 s.
- The native template tool rewrote and reopened all six start templates. Their
  committed bytes remain unchanged. All 1,259 inventoried factory assets retain
  their original SHA-256 hashes.
- Actual New Document creation from the Part and Assembly start templates,
  active editable context and enabled modeling/insertion commands: passed in
  the Family-label GUI gate, 3.784 s.
- Shared guide regeneration and website checks passed: 806 HTML pages, 17,096
  local references, all five languages and the exact runtime/public inventory.

Workspace captures remain local verification artifacts. New cropped dialog
captures were not published because visual review found clipped labels in
several languages; the existing localized command images remain in the guide.
Linux execution requires the user's Linux boot and is not established by this run.
