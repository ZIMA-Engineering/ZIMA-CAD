# Numerical precision and model tolerance

ZIMA-CAD treats calculation precision, model tolerance and display rounding as
three independent concepts.

## Contract

**Inputs**

- Geometry and parameters are ordinary IEEE-754 binary64 (`double`) values.
- `DocumentPrecision.linear_tolerance` is the accepted model resolution in the
  document length unit (normally millimetres); its current default is
  `0.001 mm`. OCCT's effective floor remains approximately `0.0000001 mm`.
- `DocumentPrecision.decimal_places` controls presentation only.

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
