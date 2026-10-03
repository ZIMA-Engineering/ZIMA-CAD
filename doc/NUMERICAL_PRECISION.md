# Numerical precision and model tolerance

ZIMA-CAD treats calculation precision, model tolerance and display rounding as
three independent concepts.

## Contract

**Inputs**

- Geometry and parameters are ordinary IEEE-754 binary64 (`double`) values.
- `DocumentPrecision.linear_tolerance` is the accepted model resolution in
  canonical millimetres, independently of document input/display units; its current default is
  `0.001 mm`. OCCT's effective floor remains approximately `0.0000001 mm`.
- `DocumentPrecision.mesh_deflection` is the positive absolute display
  deviation in model millimetres (default `0.1 mm`), shared by surface
  triangulation and edge wires.
- `DocumentPrecision.decimal_places` controls presentation only.

These values already belong to File Settings and are saved with the document.
Sweep properties also provide their existing optional approximation override;
see [the sweep precision verification](benchmarks/SWEEP_PRECISION_20260926.md).
This override and document geometric precision retain physical millimetre units.
Persisted precision values always
use a decimal point and are parsed independently of the system locale. The
whole token must be valid: a Czech decimal-comma locale must not turn `0.1`
into zero or silently clamp `0.001` to the kernel floor. Template creation,
Part calculations and Assembly STEP import use the same parser.

**Means**

- Numeric editors retain their full value when they display fewer decimals.
- Model values use the shortest lossless decimal representation (up to 17
  significant digits) for a binary64 round trip.
- Every OCCT Cut, Fuse and Common operation receives the same document linear
  tolerance through `SetFuzzyValue`; input coordinates are never rounded.
- Regeneration resolves dependencies from their original persisted references
  in history order. It recalculates derived placement instead of repeatedly
  adding rounded deltas.
- Linear constraint residuals are accepted only inside the model tolerance.
  Residuals are normalized by their equation normals, so algebraic scaling
  cannot change a physical millimetre result. Matrix rank still uses a
  separate, tight dimensionless numerical threshold.

**Outputs**

- Opening and confirming an unchanged properties dialog cannot alter geometry.
- A gap smaller than the model tolerance is coincident for a Boolean and cannot
  create a false microscopic skin.
- A residual larger than the model tolerance is a conflicting definition, not
  a value to hide by rounding.

## Numeric property fields

Part container properties, Sweep properties and Assembly component properties
inherit `decimal_places` from their owning document before initializing their
numeric controls. This includes placement values, offsets, angles and feature
dimensions; disabled values use the same presentation.

The common properties UI measures the complete formatted value with the actual
editor font, including trailing zeros, sign, unit suffix and spin buttons. It
updates the minimum field width after showing the editor and after value,
precision, font, locale or style changes. Numeric table columns reserve this
width; reference columns use the remaining space. Formatting must not hide
`0.0000` behind a field that only has room for `0.00`.

The GUI regression `zima_cpp_numeric_fields_contract` (also available through
`ZIMA_VERIFY_NUMERIC_ONLY=1`) exercises container, Point, 2D Sweep, Helical Sweep
and Assembly dialogs at 3, 4, 6, 9 and 12 decimal places, Czech decimal
commas, larger signed values and enlarged fonts. It checks both text width and
cell/dialog bounds and writes inspection images to
`Projects/test/numeric-fields`.

### Document units in property input (incremental integration)

Feature lengths, profile offset, wall thickness, draft and rotation angles use
the writable source document's units. 2D/3D Sweep wall thickness and Helical
Sweep wall thickness, pitch and base-Sketch offset use the same input control.
Length choices remain mm, cm, m and in; angular choices remain deg and rad.
The display converts canonical mm/degrees on entry and presentation only.
Numeric signals, ranges and model values retain their native units. Step buttons
use the displayed unit. An unchanged rounded field preserves the complete stored
value and must not create a Feature calculation/transaction.

The native Qt storage precision is independent of the requested displayed
decimals. Call `UnitDoubleSpinBox::set_display_decimals`, not the base Qt
`setDecimals`, when configuring these controls. The properties-window width
calculation uses their actual displayed precision. No new units, translated
messages or drafting-standard promises are introduced by this integration.

This is not yet application-wide conversion support. Shared placement, other
commands, relations/Family, View dimensions and manufacturing tolerance handling
are tracked in [the unit audit](DOCUMENT_UNITS_AUDIT.md). Unit changes must not
silently reinterpret an existing relation or manufacturing specification.

## Accumulated error

Tolerance is not applied by rounding every intermediate coordinate. Such
quantization would accumulate with each operation. ZIMA-CAD keeps the original
full-precision definitions and recomputes dependent values from them during an
explicit calculation. The tolerance is used only for geometric equivalence and
residual validation at operation boundaries.

Features whose intended size is at or below the selected linear tolerance are
not reliably distinguishable model geometry. Reduce the document tolerance if
such a feature is intentional.

Using binary64 does not add a practical performance penalty: it is the native
numeric representation used by the C++ model and OCCT. Boolean complexity and
topology size dominate calculation time. Each tolerance-aware Boolean is built
once; setting its fuzzy tolerance does not require a preliminary second build.

## Sweeps and display approximation

2D, 3D and Helical Sweep use their persisted `SweepPrecision::effective()`
approximation tolerance: the explicit custom value when enabled, otherwise the
default captured when the feature was created. This is distinct from the Part's
Boolean tolerance and mesh deviation. All remain physical millimetre values
when document input units change. Details and the limitations of sampled path
checks belong to the [sweep precision study](benchmarks/SWEEP_PRECISION_20260926.md)
and the operation-specific verification records.

During explicit Part body calculation, all history feature results and original
reference wires use the document mesh deviation. Curved edges are sampled by
chordal deviation instead of a fixed 33 points per edge, including long helical
seams. Direct STEP import into Part uses that Part's mesh deviation from its
first frozen preview. Import into Assembly uses the target Assembly setting;
newly extracted Parts and subassemblies persist the same precision settings.
The standalone kernel import API defaults to `0.1 mm` when no value is supplied.
This controls display tessellation, not the accuracy of the source STEP
surfaces or the STEP translator's repair tolerances.

Both tolerances participate in calculation fingerprints, so explicit Regenerate
cannot reuse a result or reference mesh calculated with another precision.
Changing only units or displayed decimals in File Settings does not launch a
body calculation. Confirming changed geometric precision can calculate affected
Part operations and Assembly-owned cuts; source Parts own the precision of
their geometry. The owning Assembly uses its own precision for Assembly cuts.

## Solid operation audit

| Operation | Document precision |
| --- | --- |
| Box, cylinder, cone, sphere, prism, extrusion, revolution | Analytic source geometry; document tolerance for Boolean combination and Up To clipping, mesh deviation for display. |
| Feature groups | Document tolerance for child fusion and final combination. |
| Opening and shaft thread | Document tolerance for bore/cut/trim operations; mesh deviation also for technological thread surfaces and their persisted reference wires. |
| 2D, helical and 3D sweep | Persisted feature tolerance for sweep approximation; document tolerance for final body combination. |
| Fillet | Document tolerance for spatial solving and 3D surface approximation; OCCT angular, UV and marching parameters retain their independent meanings. |
| Chamfer | Exact size/angle construction; shared document tolerance for subsequent trim/unification, shared mesh deviation for display. OCCT's chamfer API does not expose the fillet approximation settings. |
| Shell | Document tolerance for offsets, wall/opening cuts and joining tools. |
| Assembly cuts | The immediate owning Assembly passes its document precision to the cutter and subtraction; original component reference ownership is preserved. |

Cut/Fuse/Common builders receive their arguments, tolerance and history options
before a single explicit Build. The eager two-shape constructors must not be
used and then followed by another Build: that calculates once with defaults
and again with the intended settings.

Internal degeneracy tests, topology identification thresholds and dimensionless
solver thresholds are not substitutes for the document's approximation
accuracy and must not be replaced indiscriminately. STEP/STL export settings
are a separate export concern; they do not define model calculation accuracy.
