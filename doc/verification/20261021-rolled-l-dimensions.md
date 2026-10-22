# Rolled L profile dimension investigation

Status: the reported Windows GUI failure is reproduced and repaired. The native
and GUI matrices below pass; unverified variants remain explicitly listed.

The supplied `Projects/L30X30X3.prtz` initially contained six design segments,
three non-destructive corner radii, equal outer legs and equal thicknesses,
with no dimensions. The filename does not describe its current measured
dimensions: the outer legs are approximately 44.9142 mm and the thickness is
8.40907 mm. The source SHA-256 was
`7E71D932B7D25796A63B18B7F32A258A2EEE3FC5B0004057206B4115402FEB9D`.
The user subsequently saved a newer document; the original is preserved in
their `.prtz.1` save and copied to `build/rolled-l-source-original.prtz` for
testing. No test writes either user-owned source.

## Reported gesture and demonstrated defect

The user first dimensions the long left segment, then attempts to dimension
from its opposite upper-right design point to the lower tangent endpoint of
the upper-right corner radius. Their screenshot confirms the intended points.
The tangent handle is not an independent native dimension endpoint. This work
does not introduce that capability or change the persistent Sketch schema.

The universal picker can instead offer the adjacent native segment. A valid
point-to-line dimension against that segment failed while publishing its
preview: `materialize_corner_fillet` treated every dimension whose `geometry_id`
named a trimmed segment as an owned segment-length dimension. It overwrote
the dimension's independent point with segment endpoints and filled its
required-empty second-point field. Subsequent validation threw
`Sketch dimension is invalid`. The preview path did not catch this exception.
The native reproduction fails before the fix. A baseline GUI executable linked
against the preserved original Sketcher implementation also fails on the actual
mouse sequence. The exception escapes mouse-move preview publication. No WER
access-violation dump is expected from that exception path.

Endpoint remapping now applies only to owned Distance/DistanceX/DistanceY forms.
Evaluation restores authored dimensions after all corner trims, rather than
solving them against an intermediate, partially trimmed profile. Both universal
and ordinary dimension preview publication are contained by the established
non-modal rejection path, including viewer-mesh construction.

The initial native sequence covers the original leg length and 30, 40 and
50 mm, each with a native point-pair and point-to-line thickness dimension.
All eight preview/placement cases pass. An impossible thickness whose existing
corners cannot fit is rejected without changing the Sketch.

## Additional matrix findings

The first expanded radius matrix covers 96 R/D, driving/reference,
locked/unlocked, equality-before/after and equality-selection-order cases.
It completes with 24 atomic locked-reference rejections and eight atomic
rejections where the selected read-only radius would have to change to match
an existing driver. Supported cases include successive radius values, real
leg movement, independent circle/tangency equations, fixed-point rejection
and exact Sketch serialization/reopen.

The point matrix covers 576 cases: three thickness point pairs,
Distance/DistanceX/DistanceY, both selection orders, four sign orientations,
driving/reference, locked/unlocked and equality-before/after. The initial run
found 192 valid-value edit failures. The simultaneous rectilinear point-equation
seed had excluded all corner records, although those parameters do not change
the authored point graph. Its eligibility now accepts consistent non-destructive
corner parameters and explicit Origin-axis contacts. It continues to decline
visible trimmed-segment length drivers and unsupported external sources.

The final matrix has zero failures, 144 atomic locked-reference rejections and
64 atomic redundant-zero-projection rejections. Supported cases undergo three
successive dimension-value edits and real thickness drags. Independent equations
check design-line H/V, equal outer legs, uniform thickness, dimensions, corner
circle radii and tangent contacts after each action. Dimension application and
point movement reject an impossible corner fit before committing.

Native logs: `build/rolled-l-reported-sequence-native.log`,
`build/rolled-l-reported-sequence-fixed.log`,
`build/rolled-l-expanded-matrix.log`,
`build/rolled-l-coupled-origin-matrix.log` and
`build/sketch-camera-rolled-l-regression.log` (the native Sketcher and rolled-L
tests pass; that earlier run separately exposed a camera-test oracle issue).

## Actual Windows mouse interaction

The original file is opened through private Part copies. Four cases cover the
ordinary and universal dimension tools and both endpoint selection orders.
Each dimensions the left leg, offers the right native endpoint and lower toe
tangent area through the common picker, and places or rejects the supported
native dimension without escaping the event loop. The derived tangent handle
does not become a new dimensionable native point.

Each case then selects the two toe radii for equality, edits the radius through
4.1, 3.8 and 4.2 mm, and drags the outer leg through 47, 49 and 46 mm. All twelve
radius edits and twelve leg drags pass. Finishing the Sketch, confirming the
owning feature, native save/reopen and document Undo/Redo preserve point and
corner identities and equal radii. Logs:
`build/rolled-l-gesture-baseline-universal-ui.log`,
`build/rolled-l-gesture-baseline-ui.log` and
`build/rolled-l-gesture-fixed-ui.log`.

External-support coupled-L variants, fully constrained combinations beyond the
fixed-point rejection checks, and Linux execution are not certified by this
matrix. The tested profile uses native points and native Origin-axis supports.

The Windows investigation uses the independent `C:/zcb/reference-next`
build with the existing pinned OCCT 8.0.0 and Qt SDK. It does not replace the
user's development executable or change `zima-cad.bat`. There are no new
user-visible strings; existing UI localization is reviewed in cs/en/de/fr/ru.
