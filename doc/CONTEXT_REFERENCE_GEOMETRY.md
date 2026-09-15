# Original geometry in Assembly context

Reference reads for an activated Part use persisted source-Part reference packets
and exact occurrence paths in the displayed Assembly. They do not solve mates,
constructions, derived copies, or Assembly cuts. The former path through
`Workspace::refreshed_assembly` performed these calculations while preparing
references; it remains available only to calculation callers. This stage does
not change the shared placement solver.

## Data contract

- An open Part supplies current calculated geometry, including unsaved changes.
  Closed sources are read from native `.prtz` files and their IDs checked against
  the occurrence. Reading does not add live tabs.
- Repeated occurrences have separate full paths. Translation/rotation comes from
  the currently displayed persisted hierarchy. Queries do not change parent
  Assembly history, generation, or shared geometry.
- An already calculated derived copy supplies its persisted geometry, never an
  unmirrored source replacement. A direct derived Part also exposes its Origin
  with the same identity as the common viewer.
- Shared conversion transfers edge samples, exact spline poles, points, axes,
  directions, and analytical surfaces. Spline degree, knots, and weights remain
  unchanged. Samples and analytical surfaces may use different persisted frames.
- Filtering copies only requested references and used triangle vertices. Triangles
  belonging to one face share one transformed analytical surface. Invalid indices
  and incomplete reference triangles are rejected.

The catalog remains at **209 commands**. Command-driven creation, detachment, and
refresh of external references in activated Parts are a later stage, not completed
by this entry. Format and start templates are unchanged.

## Verification

The model suite passed **4/4** (1.02 s), `build/context-reference-geometry-tests.log`.
Independent mathematical checks cover 257 rational-quarter-circle points after
translation/rotation, an analytical plane, axes, points, surface sharing, and sparse
index conversion. Cases include an unsaved source, a closed native source, incorrect
identity, an exact nested occurrence, and a mirrored Part with its Origin.

The regression Assembly contains an empty-profile cut: explicit calculation
provably fails, while reference reads succeed without state changes. The first
fixture version lacked a valid cut definition; it was corrected before assessing
production behavior.

After building all programs (`build/context-reference-all-build.log`), the wider
suite passed **17/17** (142.93 s), `build/context-reference-integration-tests.log`.
It covers full GUI startup, owned-profile projection, GUI offsets and Assembly
refresh, an actual CLI process, component activation, reference queries, derived
copies, explicit model calculation, exact splines, and offset geometry.
