# Feature side reversal and release verification

Status: persisted extension and factory verification scope explicitly approved on 2026-10-22; implementation in progress.

The independent [factory origin/preview audit](verification/20261022-factory-origin-preview-audit.md)
passes the accepted native reader, existing-frame/Sketch-data and finite GUI
prerequisites. It fixes rollback Sweep-axis presentation and cross-Body native
end-reference lookup in Properties. These results do not replace the conversion
and direction/side-identity matrix required after the proposed persisted change.

## Requested behavior

Swap sides reverses physical directions without exchanging the parameter panels.
Side 1 keeps its operation, length/angle, draft, end references and value locks;
Side 2 keeps its own definition. The selected panel and numeric focus remain
attached to that same logical side. A second swap restores the original result.
The definition, dimensions and Start/End references must agree after calculation,
save/reopen, regeneration and Undo/Redo.

The former implementation exchanged both side definitions and forced the user
to edit the other panel. It had no independent persisted direction state in
FeatureParameters. Rotating the shared placement or changing the Sketch
frame would alter protected placement, references and downstream geometry; this
is not an appropriate implementation of the requested panel behavior.

## Proposed persisted scope

Add one Feature-owned reversed-directions parameter, neutral by default. Logical
side indices remain stable. A shared direction mapping is consumed by analytical
preview, native requests, dimension/arrow presentation and reference entry.
Do not encode direction in UI position, user names, value-lock keys or global
settings. Do not change shared placement solving or sketch/drawing coordinates.

Keep logical Start/End ancestry separate from physical direction. Trace both cap
roles and the Revolution adapter's current side-derived orientation before
editing; a correct body volume alone is insufficient evidence of cap identity.
Preserve signed zero, oriented normals and exact persisted reference identity.
No OCCT calculation is permitted during hover, selection or preview preparation.

The additional Feature parameter affects native Part and Assembly definitions,
including embedded Feature records in factory native assets. Existing neutral
definitions retain their authored geometry. Use one current implementation,
without legacy geometry algorithms or alternate product execution paths.

Regenerate the corresponding tracked start templates with the changed native
implementation. Verify new-document GUI creation, active editable Body/component
context and normal command availability. Inventory and regenerate/verify every
affected repository-owned FORM and other native library definition, preserving
authored history, reference/side identities, properties and insertion behavior.
Never bulk-overwrite user projects or custom libraries. Use private copies for
user examples. The packaged factory files must equal their committed versions.

## Verification matrix

- Side 1 only, Side 2 only and independent unequal sides; symmetric to independent
  and back; repeated swaps, successive edits in each original panel, numeric
  precision, value locks, end-reference entry/inspection and unchanged Cancel/OK.
- Extrusion Length/Through All/Up To, two distinct lengths and draft angles;
  Solid/Thin/Surface, Add/Subtract, supported native/external targets and both
  physical directions. Check independent extents, volume/area and cap ancestry.
- Revolution Angle/Full/Up To, both axis orientations, independent/mixed side
  operation modes and applicable Twist behavior. Verify the calculated direction
  against the actual command-preview strokes and Start/End reference equations.
- Save/reopen, explicit regeneration, Undo/Redo, Part/Assembly context and active
  occurrences. Ordinary selection/inspection must preserve revision and avoid
  kernel work. Record unsupported and unverified combinations explicitly.
- Origin/local-XY and command-preview changes on FORM and ordinary library Parts:
  creation/placement, Properties, reference inspection, Sketch frame, insertion,
  editable dimensions, dependent operation, Cancel, save/reopen and Undo/Redo.
  Preserve Drawing sheet anchoring, text orientation and existing native source
  identities. Localize any changed UI text in cs/en/de/fr/ru.

## Authorized release sequence

After the behavior and factory gates pass, integrate the accepted changes into
main, update English documentation, commit and push. Build the Windows release
from that immutable committed source using the repository-owned distribution
pipeline. Validate dependency closure, build identities, factory/resource equality,
archive paths/CRC/SHA-256, packaged GUI/CLI behavior and publication downloads.
Preserve the stable development BAT and user data. Publish the Windows version
on GitHub as requested. Report Linux verification separately; do not claim a
Linux release without Linux-host completion.
