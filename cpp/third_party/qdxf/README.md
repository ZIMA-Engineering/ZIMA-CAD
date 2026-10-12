# DXF preview dependency

Source-only snapshot of ZIMA-libqdxf and its bundled libdxfrw 0.5.9 from
ZIMA-Parts. The original GPL notices remain in the files and LICENSE.
Only the C++ DXF reader, scene primitives and navigation are included.
CAD preview adaptations add layer association, closed/bulged polyline rendering,
block instances, native rational-spline evaluation, load failure reporting and
explicit unsupported-entity notices. The separate legacy spline implementation
is not included; pure curve evaluation is shared with the native CAD library.
The viewer is read-only; this library does not implement native CAD persistence.
