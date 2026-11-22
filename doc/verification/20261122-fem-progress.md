# FEM calculation progress

This development change follows the immutable Windows 2026112202 release. It
does not change the native document format, the meshing algorithm, integration
rules, solver stopping criteria or result presentation values.

Mesh generation reports geometry preparation, volume meshing, quality checks,
conditional quadratic-mesh optimization and result extraction. Linear statics
reports system preparation, conditions, each actual solver iteration and result
recovery. Element-based phases report their completed and total element counts.
Meshing and iterative solution have no predicted completion percentage.

The calculation thread publishes plain progress snapshots. An 80 ms UI timer
consumes the latest snapshot through the existing status-operation presentation;
there is no per-element event queue or widget access from the worker. The
existing background-task input and result-validity contracts remain intact.
Success and failure both end the activity. Reusing a current mesh or solution
does not start another progress operation.

The shared status progress widget now retains Qt's expanded percentage text
for bounded work. Its explicit phase-label override remains limited to the
indeterminate range, where native styles can otherwise omit that label.

## Windows verification

- The pressure/stress matrix passed for both element orders, repeated load
  magnitudes, zero load and reversed pressure. Solving the same mesh with and
  without a progress observer produced exactly equal displacement, reaction
  and stress arrays and maxima. Reported element totals and iteration order
  were checked against actual work.
- The full FEM GUI suite passed, including cached mesh/solution reuse, a worker
  failure caused by missing elastic material data, diagnostic retention,
  activity cleanup, unchanged document revision and recovery after Undo.
- A paint-style observer verified the actual progress control receives an
  expanded `25%` label and retains iteration text in indeterminate mode.
  Labels were verified in all five languages and both application themes.
  Catalog/UI translation validation also passed.
- A read-only copy of the saved twisted-profile case meshed and solved with
  live UI updates during both operations. A real screenshot captured iteration
  10. The final maximum displacement was 0.167992 mm and maximum element-averaged
  von Mises stress was approximately 10.0818 MPa. Document revision and CAD
  calculation count stayed unchanged.

The instrumented real-document GUI run took 95.0 s. The prior packaged run took
51.3 s, but these are not a controlled timing comparison: the new run captures
progress during calculation and host activity was not isolated. No performance
improvement or unchanged wall-clock cost is claimed here. Separate phase and
solver profiling is the next verification step.

No factory assets or templates require conversion. Linux execution remains
unverified on this Windows host.
