# Drawing improvements and precision benchmark

User-authorized implementation scope (2026-09-21), including commit and push.
Existing uncommitted drawing interaction improvements remain part of the work.
Do not restore title blocks removed by the user. Shared container placement is
outside this scope. Documentation is English; UI localization covers cs/en/de/fr/ru.

## Work and verification checklist

- [x] 1. Diagnose and remove the short stray line at the thread centre in the
  axial drawing view (reference screenshot: 2026-09-21 225535).
- [x] 2. Suppress thread lead-in chamfer circles in axial drawing views by
  default, with an explicit visibility option. Preserve model chamfers and
  unrelated geometry.
- [x] 3. Restore Show/Erase dimension projection into the drawing plane,
  including rotated views; preserve original model dimensions and true values.
- [x] 4. Global equal-font-size upper/lower deviation layout: inline or stacked,
  shared by Part, Assembly and Drawing. Symmetric tolerances remain inline.
  Verify painting, picking, bounds, persistence and live settings changes.
- [x] 5. Support manual associative dimensioning of threads in hidden-line and
  section views, including thread designation rather than only circle diameter.
- [x] 6. Rename remaining localized title blocks to ZE-TITLE-BLOCK-CS/EN/DE/FR/RU,
  update defaults and consumers, and use kontakt@zima-engineering.cz.
- [x] 7. Show the complete crop spline during editing, but draw only the thin
  boundary segments crossing the projected body in the finished view.
- [x] 8. Add per-section hatch-region editing to the view properties section
  table. Limit hatching only; persist exclusively in the drawing document.
- [x] 9. Add Insert Detail below Insert View: select source point, define circle,
  ellipse or spline, place preview, set name/caption/scale. Associative content,
  independent source-boundary/label visibility, X/Y/Z naming, drawing persistence,
  shared creation/edit dialog and undo/redo. Use Pro/E/Creo as interaction reference.
- [x] 10. Benchmark calculation tolerance changes from 0.001 to 0.01 mm across
  Part, Assembly and Drawing, small and larger fixtures. The user explicitly excludes decimal formatting. Distinguish calculation,
  viewer refresh and any geometry work; record parameter invariance and actual geometry differences.

## Completion gates

Targeted model and GUI regression tests, localization coverage/catalog checks,
native save/reopen, undo/redo for document mutations, final local C++ build and
measured benchmark report. Preserve the root zima-cad.bat development entry point.
No portable release is requested. Record actual results, not assumed compliance.

## Verification results

- Seven CTest model/rendering/localization contracts passed: native documents,
  Drawing documents, thread drawing, Show/Erase, dimension layout, PDF command,
  and translation coverage/catalog validation in all five languages.
- Eight Drawing GUI contracts passed: Show/Erase, manual dimensions, balloons,
  general Drawing interactions, view controls, breaks, details and source picker.
- Main-application Drawing startup verification passed, including Tree/View
  synchronization, exact annotation properties, Ctrl selection and deletion
  protection for views.
- The modeling dimension matrix passed for primitive View/Properties edits,
  Cancel/OK and persistence, including thread, chamfer and drill-point values.
- New detail coverage includes all three boundaries, transient previews,
  shared create/edit properties, OK/Cancel, persistence and Undo/Redo. Hatch
  property removal verifies OK/Cancel and Drawing-only ownership. A pixel test
  verifies clipped hatch strokes with unchanged ordinary contours.
- Thread tests cover axial and hidden projections, internal/external threads,
  sections, designation persistence, repeated geometry, lead-in toggle, removal
  of the central stray line and PDF line weights.
- Local C++ application, CLI and harness builds succeeded. The development
  launcher still selects the current local build.
- Calculation results and reproducible samples are recorded in
  [the benchmark report](benchmarks/CALCULATION_PRECISION_20260922.md).
  The requested follow-up comparison at 0.0001 mm is complete in the same report.
