# First FEM conditions and Projection corrections

Windows acceptance, 2026-11-21. Implementation checkpoint for build 2026112102.
The shared implementation uses C++20, Qt 6.11.0 and OCCT 8.0.1. Linux execution
has not been verified on a Linux host.

## Delivered behavior

The active FEM Body offers Constraint, Force and Pressure shortcuts opening one
internal Properties class. Its type selector preserves independent force and
pressure values. The region selector has Point, Curve and Surface; a bounded
Projection surface is a Surface, not a fourth region kind. An empty Point uses
the placed container origin. Pressure requires a Surface. Constraints expose
independent local UX/UY/UZ restraints, force follows the container normal and
pressure symbols use the oriented selected face normal. Native exact references
and parameters are stored in the FEM definition inside the Part.

The existing shared placement section/picker supplies references, whole Origin,
numeric corrections, orientation, degrees of freedom, inspection and middle
double-click confirmation. Its solver and persisted placement contract were not
changed. A Boolean source reuses its material target Body's frame; an analysis
does not invent a second material Origin. Condition edits use metadata commits
and share the already calculated source snapshot. Preview, selection and
unchanged OK do not calculate CAD or create an Undo transaction.

Force arrows, pressure arrows and restraint hatches are prepared overlay wires
with labels and distinct native SVG icons. They are pickable as their condition,
not fake material edges. Single click synchronizes the Tree; double click uses
the shared parameter-inspection lifecycle. Missing placement references preserve
the last valid frame and mark the condition for repair.

Projection Properties no longer exposes direction/reverse controls. Sketch-normal
projection searches both sides and retains the nearest selected bounded face;
opposite equidistant hits are rejected as ambiguous. Explicit reference-direction
requests remain supported by the native geometry API. Source/result wires and
points use the theme foreground, including an incomplete first preview. Hover
shows both source and result. There are no dashed connecting helpers. Sketches,
3D curves and Projection retain a two-logical-pixel stroke after OK and on hover.
The two lower reference tables have compact initial heights and expand with the
window. Projection has its own native SVG icon.

## Executed checks

- `zima_cpp_fem_condition_tests`: seven applicable kind/region serialization
  combinations, exact numeric values, signed zero and side flags, invalid input
  rejection, rotated/reversed force symbols, full/partial restraints, visibility,
  and pressure arrows restricted to a real circular Projection patch. Real Body
  transactions cover Point/Curve/Surface references, unchanged OK, rejection,
  deletion, Undo/Redo, native save/reopen and immutable source sharing. Repeated
  reference-loss regeneration retains the frame; replacing the reference with
  the source Body origin restores resolution. CAD calculation count is unchanged
  throughout condition transactions.
- `zima_cpp_fem_document_tests` and `zima_cpp_fem_operations_tests`: existing
  ownership, Body/Boolean source, dependency, stale-draft and persistence contracts.
  Existing Body Scale native regression also passed.
- `ZIMA_VERIFY_FEM_BODY_ONLY`: actual View hover/click/double-click on a force
  symbol, Tree synchronization, shared inspection dismissal, Point and Curve
  picking, Surface picking/replacement/removal, independent inspection eye,
  no-op OK, Cancel, whole-Origin creation, MMB double-click, numeric placement,
  type changes, partial constraint, deletion Undo/Redo and native reopen. The
  existing Body/source checks remain included. All five languages and both
  themes passed; live retranslation preserves the draft and localized Cancel.
  Inspection found and fixed a missing symbol-to-container picking route and
  standard Qt button labels being reset after a language change.
- `zima_cpp_directional_projection_tests`: all 31 cases passed, including a truly
  inclined Sketch normal onto a B-spline surface, two trimmed patches and an
  additional plane. Analytic ray/surface samples, complete source coverage,
  pcurves, reversed normals and target row order are checked. Opposite sides,
  ties, occluded ties and isolated direction switches are covered.
- `zima_cpp_projection_document_tests` and `zima_cpp_projection_history_tests`:
  all three outputs, repeated radius/diameter edits and point drags, driving and
  reference dimensions, locked/free states, external and nested read-only
  sources, no-op, Undo/Redo, native reopen, unsaved source regeneration,
  lost references, Surface Trim, Thicken and Shell passed. This is the finite
  existing projection matrix, not exhaustive coverage of every Sketcher tool.
- `ZIMA_VERIFY_PROJECTION_ONLY`: creation/editing, real View references, whole
  Origin, replacement/inspection, preview, Cancel, no-op, save/reopen, Undo/Redo
  and successive dimension edits passed for all outputs.
- `zima_cpp_feature_type_ui_tests`: panel behavior, five catalogs and two themes,
  complete source/result highlight, endpoint picking and framebuffer stroke
  measurement passed at device pixel ratios 1, 1.5 and 2.25. Integrated stroke
  coverage is approximately 2.00–2.03 logical pixels for all three geometry types
  in idle, preview, confirmed selection and actual mouse hover. Thin-line raster
  assertions account for antialias coverage instead of requiring opaque pixels.
- Localization review: 4,685 Qt keys and 938 named keys in each of cs/en/de/fr/ru;
  production string coverage and placeholder validation passed. There are no
  untranslated new condition errors or controls.
- All six native start templates were regenerated into isolated outputs, reopened
  and compared byte-for-byte: identical. Automated template GUI creation and
  editable context checks passed. Hashes also match all 1,185 factory assets and
  all eleven protected Toyota originals against their backups. This hash check
  is not a claim of opening legacy Toyota documents with the current format.

Local reproducible logs and screenshots are under `build/fem-20261120/` and
`build/fem-20261121/gui-final/`; these disposable directories are not release
inputs. Signed distribution, factory runtime loading and publication checks are
separate release gates and must be recorded after the committed package exists.

## Remaining boundaries

This checkpoint authors conditions; it does not mesh, solve or claim numerical
load integration. Mesh quality, thin-solid convergence, force/moment balance and
solver acceptance remain explicit future gates. Thermal and fluid commands are
not exposed as working choices. Conditions currently reference existing exact
geometry, including bounded Projection regions; creating an owned Sketch inside
the condition editor remains a separate integration step. Nested Assembly FEM
condition UI and material-source geometry edits/remeshing are not covered by
this finite GUI matrix. There is no Linux runtime acceptance in this report.
