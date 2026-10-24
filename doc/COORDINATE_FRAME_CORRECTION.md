# Feature-Origin coordinate quantities

The user approved this correction and its native/CLI/Relations/Family and
factory-library scope on 2026-10-24. Existing geometry and persisted side
choices must be preserved. Windows publication follows complete verification.

## Cause and quantity contract

The XZ work plane deliberately uses native 2D X = Origin X, native 2D Y =
minus Origin Z and normal = Origin Y. Its right-handed solver basis is correct.
The previous common numeric conversion accepted a dimension without its work
plane, so it could expose native Y while the View displayed the Feature Origin Z.

Coordinate conversions now require an explicit work plane at every call site.
XY exposes X/Y, XZ exposes X/Z and YZ exposes Y/Z of the editing Feature Origin.
Rigid placement, Body placement, occurrence placement and camera navigation do
not change these local named quantities. Solver values, point coordinates,
constraint identity and the independent `solution_side` remain canonical.
Generic distances between arbitrary supports retain their existing magnitude
and side-entry semantics; the correction is limited to origin/axis coordinates.
Signed zero retains its explicit side through input, save/reopen and next edits.

View values, parameter Properties, Tree labels, CLI, Relations and Family Table
consume the same conversion. The CLI also reports the read-only `coordinate_axis`
identifier. Internal C++ callers must supply their work plane; no context-free
fallback remains. Existing localization keys and authored annotation formatting
are preserved.

## Native scope and conversion gates

Sketch numeric-contract version 36 replaces 35. The work plane and owner already
live in the native Sketch; no required sidecar or duplicated frame is added.
The product does not add an older-format reader or a compatibility branch.

A separate authoring/verification workflow stages committed factory inputs,
updates embedded Sketch contracts, rebuilds independently embedded FORM source
Parts before their receivers, re-expresses affected Relations and Family
values by stable dimension identifier, and then uses the changed native runtime
to calculate, save and reopen each affected asset. AST rewriting preserves
conditions, comments, strings and unrelated identifiers. Canonical geometry,
original/side references, authored history and quantity identifiers are compared.
Start Part/Assembly templates are regenerated and exercised through real GUI
new-document creation. The release must contain the exact verified committed data.

Private library edits are preserved separately from the committed factory
baseline. The active construction model is verified on a snapshot; source hashes
are checked before any approved installation of converted data. Backup originals
remain available. Validation does not publish private model data into Git.

## Verification to record

- All work planes, coordinate construction methods and selection orders;
  positive/negative values and both zero sides; driving/reference and
  locked/unlocked dimensions, successive value edits and actual dragging.
- Rigidly placed origins and applicable Part/Assembly/profile contexts; generic
  distance regressions, constraints, native save/reopen and Undo/Redo.
- GUI, CLI, Relations and Family Table agree on values and next-edit direction.
  Re-expression preserves existing evaluation and physical geometry.
- Factory documents, authored FORM roles, symbols/frames/title blocks, variants,
  insertion/dependent references and start-template GUI context.
- Packaged Windows source/resource identity, dependencies, signatures, archive
  safety/integrity and real portable GUI/CLI checks before publication.

Native Linux execution must be reported separately on the Linux host. The same
C++ implementation and committed factory data are shared by both platforms.

Passing finite coverage and explicit remaining gaps are recorded in
[the 2026-10-25 acceptance matrix](verification/20261025-coordinate-sketch-selection.md).
