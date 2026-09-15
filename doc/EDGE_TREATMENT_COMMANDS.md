# Fillet and Chamfer commands

The first stage exposed an operation's actual calculated input and tangent routes
through two read-only queries. Creation/editing followed in the later sections.

```json
{"command":"edge_treatment.edges","arguments":{}}
{"command":"edge_treatment.edges","arguments":{"container":"FILLET-ID","owner":"SOURCE-ID"}}
{"command":"edge_treatment.route","arguments":{"seed":{"owner":"SOURCE-ID","key":"EDGE-KEY"},"container":"FILLET-ID"}}
```

Without `container`, input is the active Body at its history cursor. With it, input
is the body before that Fillet/Chamfer, including edges removed by the operation.
Optional `document` identifies an open Part without requiring activation. Coordinates
are mm in `coordinate_system` (`body` or `document`); `body` identifies the exact
Body frame. This is not automatically the displayed top-level Assembly frame.

## Edges

`edge_treatment.edges` supports `owner`, `offset` (0–100000000) and `limit`
(1–5000, default 500). It returns `items`, `total`, `offset`, `limit`, `has_more`,
document/container/Body context and revision. Items contain original `owner`, `key`,
empty `instance_path`, `ambiguous`, and `segments`. Each segment reports display-point
count, saved `length_mm` (or `null`), parametric-seam flag and endpoints with their
own identities and `position_mm`. This reads saved data without geometry calculation.

Multiple geometric segments sharing one identity are marked ambiguous, never named
by order. Listing describes input geometry; explicit operation calculation checks
whether a particular treatment is geometrically feasible.

## Tangent routes

`edge_treatment.route` requires `seed` with string `owner`, `key`, optional empty
`instance_path`. It rejects OCCT edge numbers and `geometry_index`. Optional numeric
`tolerance_degrees` ranges 0–90, default 35. Result includes `edges`, free `endpoints`,
`endpoints_complete`, `closed`, tolerance and the same context as listing. Free ends
have stable identities/positions. When saved endpoint references are incomplete
(for example, standalone circles), the route is a valid edge but
`endpoints_complete:false`, empty `endpoints`, `closed:null`. The query never guesses
closure from point proximity. Otherwise `closed` is a boolean from saved connectivity.

GUI/query share `kernel::tangent_edge_route` over saved viewer data. Only identical
original points connect edges; spatial proximity is insufficient. Routes may cross
source containers within one occurrence. Two possible continuations stop traversal.
A direction returning to the same side of a point is not tangent continuation.

GUI supplies image scale to skip near-zero polyline samples; camera-free CLI uses
0.000001 mm. This tolerance never joins distinct point IDs. Missing/ambiguous sources
are rejected; incomplete endpoint references are reported. GUI route restoration
explicitly passes no valid selected-edge index, so even the first array edge cannot
bypass ambiguity checks.

## Query verification

Data-traversal tests cover branching, reversal, circular routes, different owners,
repeated occurrences, ambiguous/empty sources. Model queries check actual box edges/
ends, cylinder circles, rollback before Fillet, multiple Bodies, history cursors,
pagination and unchanged revision/cache. Real CLI/GUI console scenarios are included.
Native format/templates are unchanged; reads use no OCCT, regeneration or sidecars.

Initial targeted run passed **1/2**: traversal passed, while the model test incorrectly
expected two saved endpoints for a standalone circle. After verifying the data contract,
queries return that case without invented endpoints, with `endpoints_complete:false`
and `closed:null`. Targeted suites then passed **2/2** (0.16 s),
`build/edge-query-circular-build.log`, `build/edge-query-circular-tests.log`.
An actual-viewer regression distinguishes explicit selected edges from ambiguous
programmatic restoration. Geometry/native storage are unchanged.

Both programs/test targets built. Related suites passed **10/11** (74.43 s); the new
GUI test sent `null` instead of an empty object. Correcting the request made the
complete GUI scenario pass **1/1** (45.78 s). Production queries, CLI process, viewer,
model, history and translations passed initially. Logs:
`build/edge-query-related-tests.log`, `build/edge-query-final-build.log`,
`build/edge-query-gui-tests.log`.

## Fillet/Chamfer creation and editing

`fillet.create/get/set` and `chamfer.create/get/set` use
`workspace::commit_edge_treatment`, shared with GUI creation/Properties. Catalog
at this milestone: 195 commands. Earlier results above concern queries only.

```json
{"command":"fillet.create","arguments":{"radius_mm":2,"routes":[{"edges":[{"owner":"SOURCE-ID","key":"EDGE-KEY"}]}]}}
{"command":"fillet.set","arguments":{"container":"FILLET-ID","mode":"linear","radius_mm":2,"radius_end_mm":5,"routes":[{"edges":[{"owner":"SOURCE-ID","key":"EDGE-KEY"}],"start":{"owner":"POINT-OWNER","key":"R1-POINT-KEY"}}]}}
{"command":"chamfer.create","arguments":{"mode":"two_distances","distance_a_mm":2,"distance_b_mm":5,"flip":false,"routes":[{"edges":[{"owner":"SOURCE-ID","key":"EDGE-KEY"}]}]}}
```

- Fillet `mode`: `constant` or `linear`. `radius_mm` is R/R1, `radius_end_mm` R2;
  `reverse` swaps R1/R2 assignment between endpoints.
- Chamfer `mode`: `equal_distance`, `two_distances`, `distance_angle`.
  `distance_a_mm`/`distance_b_mm` are A/B, `angle_degrees` the angle. `flip` selects
  the other support face using existing stable original-face identity ordering.
- Dimensions match Properties ranges: 0.001–1000000 mm, angle 0.1–89.9 degrees.
  Arguments are JSON numbers in mm regardless of display units; defaults 1 mm/45°.
- `routes` completely replaces 1–10000 nonempty routes, at most 10000 edges total.
  Each has `edges` and optional `start` (or `null`). References contain string
  `owner`, `key`, optional empty `instance_path`. Each edge may be selected once.
- Variable Fillet requires an explicit original R1 endpoint for every connected
  open route. CLI never guesses direction; retrieve ends through queries. Closed
  circles without two endpoints permit constant Fillet.
- Optional `name` and active-document `document`. `.get` can read another open Part.
  Mutations require the active owning Body and cannot edit derived copies.

`.get` returns container/feature/Body IDs, parameters, original routes, saved starts,
`value_locks` and revision. `.create/.set` add `changed`. Identical patches do not
recalculate. GUI lock keys are shared: `primary`, `secondary`, `treatment_angle`.
Missing/ambiguous edges, wrong R1, foreign-Body edges and invalid dimensions leave
no partial mutation. Kernel checks feasibility at explicit commit.

GUI preview preserves saved R1 despite edge reordering. Splitting routes after member
removal uses the original shared rule, moved into
`document/edge_treatment_selection.hpp`: original points define ends/direction.
General container placement is unaffected.

The first model test assumed an incorrect variable-Fillet volume formula: integrating
planar quarter-circles along Z is not generally valid. It now checks actual endpoint
radii, volume bounded by analytical constant-R1/R2 cases, and reversal volume symmetry
on a symmetric box. A later run found missing dimensions in a helper-box test request;
that request was completed. Production geometry was not changed for these assumptions.

Targeted model tests then passed **1/1** (0.66 s), including actual circular cylinder-
Fillet volume. Both programs built; related suites passed **12/12** (82.26 s),
`build/edge-treatment-related-tests.log`. GUI covers both feature types, OK/Cancel,
native results and saved R1 opposite the viewer's default ordering.

Final review retained dependency refresh for tree edits: shared commit calls existing
`calculate_part_with_resolved_references`, preserving geometry and dependent sketches/
references. The general placement solver is unchanged. Extra GUI coverage removes
the first of two routes, checks the remaining start and rejects empty Fillet. A full
101-test run followed.

That run passed **100/101** (471.33 s). The new two-route GUI case exposed an R1 bug:
expanding one original edge into multiple runtime uses shifted an index into the
original endpoint list and could read out of bounds. An independent four-route model
test reproduced rejection.

The fix carries saved R1 with each runtime occurrence of its original edge. Original
ZIMA references still define identity. A Fillet calculation-fingerprint version
invalidates old derived results without changing document structure or empty templates.
Regression measures all eight endpoint radii, direction reversal, route reordering
and native-file recalculation. The fix passed **1/1** (1.66 s). Both programs/all tests
rebuilt; full regression passed **101/101** (470.10 s), including real CLI, GUI
OK/Cancel, multiple opposite-R1 routes, tree edge/route removal and translations.
Logs: `build/fillet-multiple-routes-fixed-tests.log`,
`build/fillet-multiple-routes-all-build.log`,
`build/fillet-multiple-routes-full-tests.log`.

## Removing members or routes

`edge_treatment.remove` is shared by tree/CLI for Fillet and Chamfer. It requires
`container` and nonnegative integer `route`: a zero-based position in saved user
`routes` returned by `.get`, not an OCCT edge index. Optional `edge` is the exact
original member reference; omission removes the whole route. Optional `document`
checks active Part identity.

```json
{"command":"edge_treatment.remove","arguments":{"container":"FEATURE-ID","route":0,"edge":{"owner":"SOURCE-ID","key":"EDGE-KEY"}}}
{"command":"edge_treatment.remove","arguments":{"container":"FEATURE-ID","route":1}}
```

Remaining groups use the same original-endpoint splitting as Properties. Variable-R1
starts are preserved or selected on split routes by original topology direction.
Calculation rechecks feasibility; impossible partial edits do not commit. OCCT's
automatic continuation along tangent edges is unchanged.

If no route remains, the entire container is removed with the same history command
as the tree. Result contains `changed`, `removed`, `revision`, `calculation_errors`;
surviving features also return `.get` details. Every operation, including removing
the last route, has one Undo step. No invalid empty container is created.

As with `history.delete`, deleting the feature may leave downstream missing references.
Then result reports `ok:false`, code `calculation_errors`, and simultaneously
**`data.changed:true`, `data.removed:true`**: deletion occurred and is undoable.
Clients must not treat this as an unchanged document. Invalid arguments, foreign
references and inactive Bodies instead leave the document unchanged.

Model tests passed **1/1** (2.16 s), `build/edge-remove-model-tests.log`: exact volumes
for both types, member/route removal, R1, argument errors, ownership, native save,
last route, actual downstream Fillet and Undo/Redo. Both programs/tests built; related
suites passed **38/38** (250.10 s), including original tree member/route/last-route
cases for both types and real CLI/GUI console scenarios. Translations passed separately
**1/1** (1.43 s). Logs: `build/edge-remove-all-build.log`,
`build/edge-remove-related-tests.log`, `build/edge-remove-translations-tests.log`.
Catalog: **196 commands**; formats/templates unchanged.
