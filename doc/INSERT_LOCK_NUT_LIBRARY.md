# Symbolic insert lock nuts

The factory `05-NUTS/07-LOCKING` folder contains thirteen native Parts from M3
through M36, using the explicitly reviewed **ISO 10511:2012** Table 1 dimensions.
This is not a claim of conformity to the revised 2025 edition. The separate
archived CSN 02 1492 dimensions remain visible in their own table.

## Representation and material

The user selected one steel Part on 2026-10-20, replacing the proposed steel/PA6
Assembly. Each `.prtz` contains an ordinary main metal Body and an ordinary blue
symbolic insert Body. Both use the document's steel density, 7,850 kg/m³. No
external component documents, material sidecars or native format changes are
required.

**The BOM mass is a homogeneous-steel approximation**, including the insert
volume. It is not the physical mass of a real steel/PA6 product. This distinction
appears in the library notes and localized reference drawings in all five
languages. Color changes appearance only, never material density or mass.

The hexagon is seated at Z=0. Its two outer bevels are 30 degrees and its bottom
thread mouth is 120 degrees. The retained native thread is schematic. The
manufacturer-defined collar and insert are selected CAD details: collar outside
radius 0.46s, wall 0.3P, lip 0.2P, outside corner radius 0.125P, lip bore radius
D/2+0.15P and insert bore radius D/2−0.1P. These choices do not simulate crimping,
deformation, interference forces or prevailing torque.

## Editing

The original constrained hexagon size, metal extrusion height, collar top
coordinate and native thread-catalog selection drive the useful edits.
Relations connect both bevel widths, collar cavity, lip, corner radius, insert
diameters, insert axial position/height, thread length and stock designation.
The mouth chamfer remains independently editable. Its initial depth places the
mouth at the nominal thread diameter; changing the thread catalog does not
silently overwrite that independent depth.

Regenerate recalculates native geometry and `mass = model.mass`. Ordinary
save/reopen retains both Bodies and the blue appearance. The main metal Body is
active in the installed model, preserving the normal Part editing context.

## Verification record

Windows native verification used Qt 6.11.0 and OCCT 8.0.0:

| Check | Measured scope and result |
| --- | --- |
| Baseline physical geometry | All 13 Parts: two valid, individually connected Body solids; independent analytical volumes and bounds; zero volumetric overlap; positive steel-derived BOM mass; native reopen and persisted color. |
| Repeated drivers | All 13 Parts: metal height, hexagon width and overall height each changed twice and restored; a finer catalog pitch selected and restored. 143 successive edits passed. |
| Dependent geometry | Collar, cavity, lip, insert diameters/position/height, thread lengths, stock and mass checked after Regenerate, native save and reopen. Original face references retained. |
| Undo/Redo | Executed before every one of the 143 regeneration checks; restored driver values checked independently. |
| Profile dragging | The existing native checker exercises every authored profile point repeatedly, with unlocked and locked dimensions. Solver equations and native point identity remain valid; locked dimensions do not move. |
| Actual GUI | Existing native mouse-edit scenario for M3 and M36: three successive width edits each, Undo/Redo, Regenerate and native save/reopen. Both saved GUI outputs independently passed the physical checker. Ordinary manual M8 opening also displayed the blue insert. |
| Native drawings | One representative M8 source with base and five localized Drawings. Both views retain real axes; six associative dimensions resolve after regeneration and native Save As/reopen, retaining source/annotation identities. The first-angle projected section is aligned with its cutting plane. |
| Reference pages | Five languages, 13 ISO rows and the preserved separate 10-row CSN display. Native drawing graphics, screen layout and selectable A4 output are checked by the established reference tools. |

This is a finite library acceptance matrix, not exhaustive Sketcher coverage.
Alternate dimension construction/selection orders, reference-only replacements,
every possible invalid user combination and every direct mouse drag are not
retested for this asset-only feature. Shared Sketcher behavior and native
serialization are unchanged. Native Linux build and GUI execution remain
unverified here and require the Linux host.

Authoring, verification and installation tools are
`generate-insert-lock-nut-library.py`, `verify-insert-lock-nut-library.py`,
`verify-fastener-library-gui.py`, `render-insert-lock-nut-library.py`,
`prepare-insert-lock-nut-page.py` and the existing verified-candidate installer.
Private execution receipts are under `build/insert-lock-nut-*`; installed file
hashes are recorded in the model stage's `installed-insert-lock-nuts.json`.
Factory installation rejects different pre-existing files and preserves custom
libraries. Release packaging still requires committed-source and packaged native
runtime acceptance; library installation alone does not complete a new release.
