# Numerical precision and model tolerance

ZIMA-CAD treats calculation precision, model tolerance and display rounding as
three independent concepts.

## Contract

**Inputs**

- Geometry and parameters are ordinary IEEE-754 binary64 (`double`) values.
- `DocumentPrecision.linear_tolerance` is the accepted model resolution in the
  document length unit (normally millimetres); its current default is
  `0.001 mm`. OCCT's effective floor remains approximately `0.0000001 mm`.
- `DocumentPrecision.mesh_deflection` is the positive absolute display
  deviation in model millimetres (default `0.1 mm`), shared by surface
  triangulation and edge wires.
- `DocumentPrecision.decimal_places` controls presentation only.

These values already belong to File Settings and are saved with the document.
New documents inherit them from their template; there is no additional sweep
precision control or new `config.ini` key. Persisted precision values always
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
numeric representation used by Python, NumPy and OCCT. Boolean complexity and
topology size dominate calculation time. Each tolerance-aware Boolean is built
once; setting its fuzzy tolerance does not require a preliminary second build.

## Sweeps and display approximation

2D Sweep and Helical Sweep take their path approximation tolerance from the
Part's linear tolerance. Half is allocated to the sampled cubic-path deviation
checks and half to OCCT's sweep surface approximation. The path check is sampled,
not a formal global error bound. Experimental 3D Sweep passes the document
linear tolerance to its OCCT sweep builder. Validity and self-intersection
checks remain enabled; coarser tolerance does not bypass them.

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
Changing File Settings alone does not launch a body calculation. Assemblies
continue to display their calculated component meshes until explicitly
regenerated; the source Part owns the precision of its geometry and the
owning Assembly uses its own precision for Assembly cuts.

## Solid operation audit

| Operation | Document precision |
| --- | --- |
| Box, cylinder, cone, sphere, prism, extrusion, revolution | Analytic source geometry; document tolerance for Boolean combination and Up To clipping, mesh deviation for display. |
| Feature groups | Document tolerance for child fusion and final combination. |
| Opening and shaft thread | Document tolerance for bore/cut/trim operations; mesh deviation also for technological thread surfaces and their persisted reference wires. |
| 2D, helical and experimental 3D sweep | Document tolerance for sweep approximation, hollow/Thin cuts and final combination. |
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
