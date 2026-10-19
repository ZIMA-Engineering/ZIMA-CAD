# Coupling nuts, 20 October 2026

The factory `05-NUTS/11-COUPLING` family contains fourteen nominal coarse-thread
sizes M6 through M36. Height is 3d. Aspen's dimensional sheet supplies the
nominal flats and height; Fabory explicitly identifies DIN 6334 as a customary
trade designation for coupling nuts. The five localized pages retain this
qualification. They do not assert certification to a published coupling-nut
DIN standard. Outside 30-degree bevels and 90-degree mouth treatments are
identified CAD choices. Zinc coating and tolerances are not modeled.

The generator reuses the established native hex-nut implementation without
changing the modeling algorithms or file format. Each model retains one Body,
an Origin-constrained regular hexagon, prism, externally supported upper/lower
turned bevels, native metric opening and exit chamfer. Existing Relations
derive stock, thread/bore length, bevel width and mass. Nominal height 3d is
the initial catalog value; users can deliberately edit a custom height.

Windows Qt 6.11.0 / OCCT 8.0.0 verification:

- All fourteen baseline solids and 154 successive edits passed the independent
  native hex-nut checker: six height/flats edits and five thread-catalog edits
  per size. Every edit includes Undo/Redo, explicit regeneration, stable face
  references, derived stock/thread length, exact analytical volume, BOM mass
  and save/reopen. Sources retain their SHA-256 hashes.
- M8 and M36 passed three actual mouse dimension-editor changes each in the
  GUI. Both resulting saved Parts independently passed the native geometry,
  volume, mass and profile-drag checks. The initial GUI test exposed rounding
  in the test's default six-digit input formatting; the test now enters the
  intended value with fifteen significant digits. Product precision is unchanged.
- Four associative dimensions and axes in both projected views passed native
  regeneration/save/reopen in all six reference Drawings. The renderer uses a
  smaller projection distance for this long family; visual inspection caught
  and corrected a cropped side view before publication. Final illustrations
  use h for height and retain the complete model and its dimension.
- Fourteen source-preserving 320 by 240 thumbnails and all five translations
  passed the installer gates. Existing user assets were not overwritten.

Evidence: `build/coupling-nuts-edit-v1/results.json`,
`build/coupling-nut-gui-v3/`,
`build/coupling-nut-gui-independent-v2/results.json`,
`build/coupling-nuts-drawing-reopen-v2/results.json`,
`build/coupling-nuts-art-v2/` and
`build/coupling-nuts-v1/installed-coupling-nuts.json`.

The shared native implementation is unchanged. Linux execution and rendering
remain unverified on this Windows host.
