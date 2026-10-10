# Native Linux calculation fixture

`ventilation-linux.prtz` preserves the seven calculated boundaries from the
factory Ventilation Window produced by native Linux build 2026100908 with
OCCT 8.0.0. The original captured file SHA-256 is
`28dfe5f4981a4a8f0886d845ac1d0dd2fc779b6203b6db9ac9226b52cd37dcc2`.

The current native writer reserialized the exact definition and added its
definition proof on Windows. The original calculated packets, B-Rep, physical
properties and topology were retained without OCCT recalculation. Runtime reuse
keys still originate from Linux in the file; the loader rebinds them only after
validating the persisted definition. This explicit fixture preparation is not
a legacy reader or migration path in the product.

The last stored boundary has area 2242.5998362849596 mm² and zero volume. Native
document tests verify unmodified file bytes on open, seven boundaries, retained
properties and exact save/reopen output on Windows. Native Linux execution of
the current code and reverse-direction acceptance remain pending.
