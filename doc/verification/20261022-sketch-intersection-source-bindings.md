# Native source-boundary bindings for Sketch intersections

Status: Windows implementation, native/GUI checks, factory calculation and
readback, Drawing comparisons and complete factory load gates pass. The user explicitly
approved this persisted-contract scope on 2026-10-22. Sketch editor commands and
selected-branch persistence remain separate unfinished integration.

## Cause and shared implementation

A normal calculated solid previously stored face bindings while omitting its
owned edges and vertices. A closed circle could survive cold reopening, but a
bounded open section could not recover endpoint ancestry. `make_result` now
captures all existing owned topology through the existing single ShapeSet archive
and binding arrays. It adds no field, sidecar, kernel upgrade, modeling tolerance
change or hidden source calculation.

Rigid Body publication preserves all three local binding arrays while appending
its existing rigid placement. Scale, Mirror and linear/circular Pattern map the
same exact native subshapes through their existing transforms and reference
remappers. A metadata-only packet avoids copying tessellation for this work.
Placement solving, original faces, authored side choices and offset math are
unchanged. Deferred scalar face-measurement snapshots retain their narrower role;
they are not complete intersection source boundaries.

The factory check caught an additional issue in the preceding symmetric-endpoint
fix: replacing propagated vertex aliases with operational junction names changed
parents consumed by a later Surface Shell. The shared endpoint helper now derives
one operational inspection name without replacing native propagated aliases.
Packet endpoints and variable Fillet consume that helper; downstream source
operations and persisted bindings retain their original parents. The symmetric
matrix explicitly checks 16 original vertex bindings alongside 12 operational
endpoints and four shared junctions. No nearest-parent or enumeration identity is
introduced.

## Finite verification

- The face/Sketch-plane matrix passes 108 exact cases: the earlier 94-case matrix
  now accepts cold open-solid sections, plus cold rigid placement and Scale,
  Mirror, linear Pattern and circular Pattern source checks, both plane normals.
  Independent sampled equations, exact curves, endpoint identities, native
  round trips and unchanged source packets are checked.
- Eight native suites pass: face measurement demand, derived copies, nested copy
  references, Body Scale, edge-treatment commands, symmetric endpoints, native
  boundary surfaces and face/plane intersections (44.36 s while factory checks
  were running). Ordinary profile commands also pass, including their 18 point
  end-target combinations. Assembly profile and Boolean Part fragment suites
  pass separately in 8.32 s.
- Five GUI/localization suites pass in 191.33 s: symmetric Fillet/Chamfer mouse
  creation/editing, FORM topology, actual new-document creation from mm and inch
  Part/Skeleton/Assembly templates, and all five translation catalogs. Template
  tests explicitly select the regenerated template directory and verify active
  editable contexts and enabled normal commands in cs/en/de/fr/ru.
- All 1,048 committed Part-based/Assembly factory inputs pass their applicable
  native calculation/save/reopen gates. Authored history, sides, Body hierarchy,
  Sketch definitions and dimension identifiers are preserved. Every previous
  reference identity and binding is retained; original source geometry packets
  compare exactly. Volume and area checks retain the established tolerances.
  Two empty Assembly templates use their dedicated serializer/load gate rather
  than a Part calculation. All six templates are regenerated and reopened.
- After installing verified factory files, six more GUI suites pass in 102.84 s:
  original previews for BridgeLance, CornerGusset90, DimpledHole,
  VentilationWindow and the formed-nose washer, plus actual Sheet FORM creation,
  placement, editing and dependent behavior. Private working native definitions
  (`PWN-CSN021665-M10.0.prtz` and `ZE-TEXT.symz`) remain byte-unchanged; their
  separately verified committed factory counterparts are staged from the
  isolated conversion output.

The first factory batch hit six Windows report-path length failures after native
readback had succeeded. Retrying those cases with short hashed report paths passes
without changing assets, precision or verification criteria. Private working files
are excluded from conversion inputs; inputs come from Git commit
`35f93be7bd674b4ece40e062c1a82624480a813b`.

Drawing inventory is now classified by content rather than presence of a JSON
key: 137 Drawings contain 204 display packets, all with empty native archives and
empty face/edge/vertex bindings. They are not affected native geometry assets.
Their committed bytes are preserved. All 137 separate checks pass, covering
783 resolved Drawing dimensions and 30 model annotations. All 1,185 native assets
then load read-only in 103.65 s with zero source body calculations. Separate
current and released-runtime copies
check regeneration, dimensions, native reopening and complete refreshed sheets;
only the known test-directory root in derived BOM paths is normalized for the
comparison. Existing refreshed annotation geometry and display-pool sharing are
not changes caused by this source contract.

## Measured cost

Three independent cold CLI processes per runtime/control perform Open, explicit
Regenerate, Save, Close and native Reopen/Close. Startup is included. The baseline
is the accepted immutable Windows 2026102201 installation. Windows native peak
counters are sampled every 20 ms. The table gives medians, not a general speed
promise. Source controls are identical committed data; cached volume, area,
centroid and integral records compare exactly across all 18 runs.

| Control | Baseline / current time (s) | Peak working set (MiB) | Native size (bytes) |
| --- | --- | --- | --- |
| HHB ISO4014 M8 x 40 | 0.725 / 0.750 | 108.63 / 111.83 | 1,202,547 / 1,239,758 |
| CornerGusset90 FORM | 0.280 / 0.280 | 44.94 / 45.30 | 322,045 / 332,903 |
| Boolean FORM regression model | 0.871 / 0.894 | 89.00 / 90.26 | 1,092,332 / 1,122,544 |

The 1,046 converted Part-based assets total 1,532,728,070 bytes before and
1,571,361,641 after: +38,633,571 bytes (+2.52%). The complete source archive is
reused; there is no second serialized BRep. Hover, selection, unchanged OK, source
Open and tab switching do not acquire a new body-calculation path.

## Evidence and remaining boundaries

Local evidence: `build/sketch-source-bindings-native-final.log`,
`build/sketch-source-bindings-dependent.log`,
`build/sketch-source-bindings-gui.log`,
`build/sketch-source-bindings-installed-factory-gui.log`,
`build/sketch-source-bindings-factory/verification.json`,
`build/sketch-source-bindings-factory/templates-verification.json` and
`build/sketch-source-bindings-performance/report.json`, plus
`build/sketch-source-bindings-factory/drawing-verification.json` and
`build/sketch-source-bindings-factory/load-verification.json`.

No new product UI strings were added; existing localized diagnostics are reused.
The full Sketch dimension/drag/constraint/Trim/Undo matrix belongs to the pending
editor integration and is not claimed by these kernel tests. Arbitrary ambiguous
closed-loop domains, every nested Assembly occurrence and Linux execution remain
unverified or explicitly rejected as recorded in the foundation matrix. The older
standalone FORM harness issue remains separately documented; its tolerances and
expectations were not weakened here. Published Windows 2026102201 is unchanged.
