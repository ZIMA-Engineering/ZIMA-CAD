# Windows 2026111811 validation

Source: `55ae5be9a426e1772d9d77978e7980b9a81ddfd5`, tagged
`ZIMA-CAD-2026111811`, incorporating upstream `292536c1f`.
Windows uses MSVC 19.51.36248, Qt 6.11.0 and OCCT 8.0.1. This release changes
build identity and documentation; no product implementation was modified while
preparing the Windows release.

## Local verification

The application, CLI, updater, drawing harness and selected regression programs
built successfully. Compiler warnings about ignored `nodiscard` results remain.
GUI/CLI/updater report build 2026111811. The development build identifies its
uncommitted personal configuration honestly as `source_modified`; the portable
candidate is built separately from exact committed Git data.

All 24 selected native/component tests passed in 60.05 seconds. An additional
shared placement-row test passed, including replacing a populated reference in
fully constrained placement, removing a redundant reference, releasing a degree
of freedom and rearming the corresponding row. The selected tests cover:

- 3D Curve, 3D and helical Sweep, trajectory conversion and releasing points;
- exact curve placement, orientation/reference assignment, reference removal
  and placement transactions;
- model calculation, multibody history, Assembly history, construction,
  Fillet/Chamfer, Bend and derived-copy operations;
- native Sketch equations, spline commands and their persistence contracts;
- numeric controls in Fusion, Windows and Windows 11 styles, axis highlighting,
  curve presentation and Czech, English, German, French and Russian catalogs.

The initial 17-scenario GUI run passed 14 scenarios and failed three. Repeating
the spline scenario in a new isolated working directory passed in both the
local executable and the exact portable runtime. The shared `Projects/test`
run still reproduces its point-picking failure; its precise interaction with
retained test state has not been diagnosed. It is not reported as a uniformly
passing original CTest run.

## Finite interaction matrix

| Area | Actions exercised | Result |
| --- | --- | --- |
| Curve/Sweep | Native creation, mode conversion, references, point release, edits, invalid-input rejection, persistence and Undo/Redo | Selected native contracts passed |
| Helical Sweep | Actual creation/Properties interaction | Local and portable GUI passed |
| Point references | Pending curve point, whole-Origin placement and Origin preview | Local and portable GUI passed |
| Editing context | Real input Body preview during editing | Local and portable GUI passed |
| Templates | Template chooser and creation from millimetre start templates in five languages | Local and portable GUI passed |
| Settings | Forms library path and System Setup | Local and portable GUI passed |
| Drawing | Drawing UI and reopened view controls | Local GUI passed |
| Sketch corners | Four equal corners, dimension edits and repeated point dragging | Local and portable GUI passed |
| Spline | Both spline types, shared closure point, self tangency, preservation of existing constraints and save/reopen | Isolated local and portable GUI passed |
| Interpolating spline | Three successive mouse drags; actual movement and independently checked seam derivative equations after each drag, Undo and Redo | Isolated local and portable GUI passed |
| Copies | Save Copy workflow | Local and portable GUI passed |

The spline checks retain native point identity and compare seam derivatives;
they do not infer editability from the initial solve alone. These existing
finite suites do not cover every dimension type, lock/reference combination,
fully constrained state, selection order, orientation, external support or
dependent trimming variation. Those additional combinations remain unverified
by this release task. The complete repository test suite was not run.

## Outstanding GUI checks

- `zima_cpp_feature_axes_ui_contract` fails at
  `Explicit command entry did not reveal transient axis endpoints`.
  It also fails at the same assertion in the previous signed Windows 2026111703
  executable, and fails with both ordinary and hidden test windows in 1811.
- `zima_cpp_console_ui_contract` fails at
  `Console did not persist the linked native segment and its endpoint references`.
  The identical failure was already recorded in the
  [20261107 verification](20261107-edge-treatment-nodes.md).
  Later portions of that broad console
  scenario are not covered by this run.
- The initial shared-workspace spline scenario fails to pick its drag point.
  Its isolated local and portable reruns pass the complete scenario. The earlier
  signed 1703 spline test instead fails earlier at
  `Drawn spline tangent seam is not smooth`; it does not establish equivalent
  dragging coverage.

No failing check was removed from the source or weakened for publication.

## Packaging and isolation

The repository packaging/publisher suites passed 29 tests; two Linux-only tests
were skipped on Windows. The clean Windows candidate passed dependency closure,
archive path/collision/CRC/SHA-256 checks, extraction with spaces and Unicode,
GUI/CLI startup, deterministic Part calculation/save/reopen and export smoke.
All 1,185 committed native factory assets loaded successfully.

Thirteen of fourteen selected portable GUI scenarios passed after providing
their required test inputs; the axis-endpoint scenario retains the failure
above. The first portable harness omitted repository-relative localization and
drawing fixtures and the `build/` parent required by existing tests. Origin and
both template scenarios were rerun successfully using isolated copies of the
exact committed inputs and the packaged start-template directory. These were
harness setup failures, not changes to product code or to the packaged assets.

Personal `config/config.ini` retained its SHA-256 through the local tests.
Untracked work, browser data and production project documents were not packaged
or modified. Only obsolete generated test copies were removed for disk space;
the previous signed archives remain available.

Signed and public artifact acceptance is recorded separately in the
[release record](../releases/2026111811.md). Native Linux execution and live
update installation/activation are outside this Windows verification scope.
