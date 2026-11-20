# Owned RIP, Sketch Fillet and placement repairs

Windows verification of the follow-up agreed on 2026-11-20. Publication is a
separate gate; this record does not itself claim that a release is available.
Private reproduction files and raw logs remain under `build/rip25-20261120/`.
They are not document dependencies or distributable user data.

## Resulting behavior

- Sheet from Body creation and Properties share one dialog. An empty selection
  creates the non-geometric history boundary. RIP entry creates or edits ordinary
  Sketches owned by the conversion. The Tree nests those inputs under the
  conversion; complete release leaves them as ordinary standalone Sketches.
- RIP curves imprint the actual source skin locally. They do not sweep a cutter
  through the opposite wall. All ordinary exact Sketch curve types are passed
  to the kernel; construction geometry is excluded. Curves must overlap a source
  face, and distinct split regions retain independent sheet branches.
- Failed creation retains the input solid and an editable unresolved definition
  after the explicit second confirmation. Failed editing retains the preceding
  valid sheet and diagnoses its parent. It must not add both the original solid
  and retained sheet. Repair clears that diagnostic on successful calculation.
- Entering an owned or standalone Sketch retains the preceding model as context,
  including after conversion deletion. Cancel restores the pending command's
  original state. Unchanged Properties confirmation avoids a calculation/Undo.
- Sketch Fillet uses an internal Properties window with radius and two shared
  reference fields. It accepts preselection or subsequent picks, presents an
  exact transient preview, and commits once on OK. No automatic radius dimension
  is inserted. The purple quarter-arc icon has white endpoint dots.
- Deleting native or virtual rounded supports counts directly selected geometry
  separately from dependent corner rounds. Selecting a complete chain no longer
  reports that an already removed dependent geometry object does not exist.
- Ordinary Extrusion Up To uses the selected face's exact underlying curved
  surface. Explicit ordered targets retain their bounded-face behavior. Current
  reference identity survives source edits and native persistence.
- At an orientation pole, negligible components of a plane normal no longer
  determine an arbitrary in-plane azimuth. The explicitly approved shared change
  affects only that degenerate azimuth; free angles and corrections remain
  ordinary placement inputs. Signed-zero side semantics are unchanged.

## Finite verification matrix

| Area | Cases and independent checks | Evidence / result |
| --- | --- | --- |
| Native Sketch Fillet | 400 combinations of five support types on each side, four orientations, both selection orders and mirror states; repeated preview radii; real and virtual support/chain deletion; retained points/contacts | `curve-delete-native.log`, pass |
| Fillet editability | Radius and diameter, locked/unlocked, driving/reference; successive 1.2/0.8/1.1 radius edits and permitted drags; fixed-point rejection; center X/Y coordinates, Equal Radius, impossible edits, native serialization | Same 400-case native suite; geometric radius and tangent equations checked after actions |
| Fillet GUI | 20 Part cases (native/virtual) and 10 Assembly native cases; five support types and both selection orders; five languages; empty/preselected entry, replacement, eye, resizing, Cancel, manual R/diameter, successive edits/drags, support/chain Delete, Undo/Redo, native save/reopen | `final-ui4.log`, Fillet test passes all 30 cases in 116.90 s, including Light/Dark/Light changes without modifying the document; both captured themes visually reviewed |
| Assembly dimension interaction | Retain universal-dimension selection filter after scene refresh so a second arc pick selects diameter; private previews retain passive Part context | Included in the 10 Assembly GUI cases |
| Assembly Sketch properties | Edit/delete/Undo without changing occurrence geometry; explicit Save may republish equivalent analysis snapshots and is checked by exact serialized geometry and placement | `placement-sheet6.log`, pass |
| Owned RIP and failed confirmation | Same-Body and cross-Body ownership; source-face edit, empty boundary, retained identities, failed creation/edit, repair, release, source deletion, cold calculation, persistence, Undo/Redo | `placement-sheet6.log`, Sheet exchange suite passes in 62.33 s |
| RIP curve types | Segment, circle, circular arc, ellipse, elliptical arc, cubic B-spline, shared endpoints and inert construction geometry; local skin imprint, material preservation, rejected off-skin input | Sheet exchange native suite |
| Sheet GUI | Five languages, native picker, owned Sketch authoring/context, empty creation, Properties/Cancel, first/second failed OK, source visibility, Tree ownership, Undo/Redo and DXF | Cross-Body passes in `final-ui4.log` (16.38 s); same-Body passes in `final-sheet5.log` (18.56 s), including two successive Sketcher entries after conversion deletion and independent material-volume comparison across complete release |
| Private 25.prtz | Valid short RIP on its source skin; exact local partition; all eight generated children retained, zero skipped or failed children; save/reopen and three independent cold calculations | `private25-cold.log`, pass; each cold result has volume 42791.551582856 mm3 and area 86918.5433428637 mm2 |
| Cylinder Up To | One-sided, symmetric and unequal two-sided source; both contact sides, radius edits, analytic integral/volume and stable references | `cylinder-matrix4.log`, pass |
| External Up To | 48 signed/rotated plane cases; Body frames, authoritative unsaved source; exact cylinder integral before/after radius change; current Boolean fragment ancestry; read-only source, persistence and Undo/Redo | `external-targets-bounded.log`, pass |
| Private 26.prtz | Reported cylindrical target, calculated final Thin Extrusion, unchanged authored source, save/reopen and cold volume/reference equality | `26-cylinder-exact.log`, pass; this does not claim every later unfinished feature in the subsequently saved file calculates |
| Placement pole | 96 combinations of FRONT/TOP, both poles, roundoff signs, free angles 0/17/-32 and corrections 0/11; independent rotation matrices, remaining DOF, serialization and repeated resolution | `placement-regressions1.log`, five placement suites pass |
| Placement GUI | Three successive plane picks at a pole, free angle and numeric correction, OK, Undo/Redo, unchanged edit and Cancel, replacement by whole Origin, native save/reopen and preserved reference identities | `final-ui3.log`, pass in 8.24 s |
| Localization | All five catalogs and existing UI translation contract; new Fillet/Sheet text in cs/en/de/fr/ru | `final-ui1.log`, translation test passes |

The GUI matrix uses actual mouse/dimension-editor paths for the stated cases;
it does not claim every native dimension/lock combination was also repeated by
mouse. External read-only Fillet supports are rejected rather than modified.
Arbitrary off-skin curves and through-solid projected RIP are not supported by
the agreed local-edge operation. No universal validity claim is made for every
possible self-intersecting curve or sheet branching configuration.

## Factory data and protected user work

The extension reuses persisted Sketch/history/reference data; it introduces no
required external sidecars and no legacy-format fallback. Factory verification
inventoried 1,185 native assets and explicitly regenerated the 1,016 affected
Feature Parts, including all four FORM definitions. For 1,014 assets, current
native regenerate/save/reopen preserved authored history and calculated
contracts. Two wheel-nut assets required re-expressing the existing free angle
under the corrected pole convention. Their physical frame, six history
boundaries, source/side identity, volume, area and mass were verified; a second
ordinary regeneration preserved the adjusted definition exactly. Only those
two verified assets replace committed factory data.

Evidence: `factory-verification/native-current-verification.json`,
`pole-0-preserved.json`, `pole-1-preserved.json`, their repeated checks, and
`installed-frame-reexpressions.json`. The older Python cache-v1 verifier is not
the current native cache oracle and is not used as evidence of geometry failure.

Eleven native Toyota files have verified recoverable backup copies. SHA-256
comparison confirms both originals and backups remain unchanged. Both copied
Drawings pass current native save/reopen equivalence. Seven Parts already fail
the preceding baseline reader with unsupported native formats; the copied
Assembly also fails while loading an embedded Part. Consequently complete
Toyota regeneration/reference verification is **not established**. No originals
were converted, overwritten or replaced, and no compatibility path was added.
The legacy `.prt` import is preserved in the verified backup and is outside the
supported current native format. Evidence: `protected-originals-unchanged.json`,
`protected-readback/*.report.json` and the preceding baseline inventory under
`build/sheet-conversion-20261119/correction-asset-verification/`.

## Remaining acceptance

All twelve mandatory sheet-branch gates pass in `mandatory-final1.log`
(220.44 seconds). The six native start templates were rewritten and reopened
with the changed implementation, with byte-identical resulting tracked files.
GUI creation from the refreshed mm and inch templates passes in
`final-ui2.log`, including active editing context and normal command availability.
Final placement and both Sheet GUI contracts pass. The last same-Body harness
correction selects the actual Sketch Tree row: its preceding plane row carries
the same Sketch ID but is not the Sketch Properties command. Earlier failures
remain in `final-ui3.log` and `final-ui4.log`; no product behavior was changed
to bypass those assertions. Packaged checks are recorded when complete.
Linux execution is a separate host gate; Windows results do not establish Linux
acceptance.

Projection and FEM/frame-node behavior in the follow-up document are agreed
future design, not implemented features of this repair batch.
