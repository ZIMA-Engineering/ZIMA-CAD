# Pressed-wing symmetric-root verification

The six CSN 02 1665 pressed-wing nuts, M3, M4, M5, M6, M8 and M10, were
rebuilt from the user's accepted M10 correction. The wing extrusion is
symmetric, the root closes through the sketch origin, and the inward root arc
is contained by the hub. A construction-only tangent retains the authored
lower transition while keeping each wing profile fully constrained. Only the
valid mass and stock Relations remain.

Windows verification used the native C++ build and OCCT 8.0.1. Every size
passed 35 actions: eleven driving dimensions changed twice and were restored,
followed by a fine-thread selection and restoration of the original thread.
Each action checked Undo/Redo, zero remaining sketch degrees of freedom,
quadrant preservation, reference identity, explicit regeneration, native
save/reopen, connected valid BRep geometry, derived mass and stock, and
repeated locked and unlocked point dragging. This is 210 successful actions.

The final nominal files passed the independent section-volume calculation at
the existing one-ppm limit. During edited intermediate states the analytical
and Gauss-Kronrod BRep volumes were required to agree within two ppm, accounting
for the product's normal 0.001 mm modeling tolerance. The persisted physical
cache was compared directly with the Gauss-Kronrod integral at 1e-10 relative;
the independent adaptive BRep integral agreed with Gauss-Kronrod at 2e-8.

Receipts are in `build/pressed-wing-user-pattern-all-v1`,
`build/pressed-wing-user-pattern-all-verify-v4`,
`build/pressed-wing-user-pattern-gui-v3`,
`build/pressed-wing-user-pattern-drawing-verify-v1` and
`build/reference-table-verification/fastener-tables-pressed-wing-symmetric-v1`.
The GUI receipt was repeated with the normally launched
`build/cpp-windows-release/zima-cad-cpp.exe` after its final link. Linux
execution remains an explicit platform-specific gap and must be completed on
the Linux host.
