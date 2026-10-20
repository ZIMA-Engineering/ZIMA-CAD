# External conic center references

## Approved behavior and persistence

The user approved persisted external center references on 2026-10-20, then
clarified that **C selects center + center** and **= separately selects circular
curve + curve**. C between circumferences must not imply concentricity. Ellipse
and elliptical-arc centers were explicitly added to the scope. Existing explicit
Concentric is retained.

A center is a read-only derived point of the existing exact external support.
Its identity is `sketch_external_center:<reference-id>` in existing native
constraint/dimension fields. Viewer semantics add `external_point:`. No duplicate
point coordinates, new sidecar, outer document version or extension is introduced.
The cached exact curve and reference identity remain in the native document.

Circular centers reuse the exact rational-quadratic circle certificate. Elliptical
centers are calculated from the implicit quadratic of the first rational Bezier
span, then certified against every span's homogeneous degree-four polynomial.
No display tessellation fit is used to invent an ellipse center. The helper is
shared C++ and does not invoke OCCT. Nonconics do not expose a false center.

## Interaction and dependency contracts

- C binds a native point to the derived external center in either selection order.
  A native Circle, Arc, Ellipse or Elliptical Arc retains its own real center point.
- Circular equality accepts the external curve in either selection order; the
  external radius is always read-only. Native/native direction remains unchanged.
- Drawing a circle with center and rim snapped to the same external circle/arc
  creates the point binding and equal radius, without a redundant rim point.
  Existing automatic constraint preferences continue to apply.
- Ordinary center selection and box selection resolve to the owning external
  reference in the Tree. Delete removes that reference and its dependent center
  constraints/dimensions; owned native geometry remains. Undo restores it.
- A missing or incompatible source freezes its last valid exact support and owned
  geometry, marks the link broken and can repair the same identity on return.
  Repeated incompatible refresh is a no-op. A source changing from Circle to
  Ellipse cannot silently satisfy a required circle-radius equality.
- Generic native point insertion does not automatically attach to every derived
  center. The GUI owns the explicit snap. This avoids a redundant attachment
  during Reference Profile creation and preserves existing profile editability.

## Finite Windows regression matrix

The dedicated native target is `zima_cpp_external_curve_centers_tests`.

| Matrix | Enumerated coverage |
| --- | --- |
| Center binding and equality | 96 cases: four source conics, XY/YZ/ZX frames, both projected sides, original/body-edge packets and both radius-equality orders; three source movements/scales, repeated rejected center drags, stable identities, read-only source, broken/repair, detach and serialized reopen |
| Direct center dimensions | 576 Distance/X/Y cases: four conics, three frames, two sides, both selection orders, driving/reference and locked/unlocked; three edits and three drags, independent equations and reopen |
| Owned curves | 32 native Circle/Arc/Ellipse/EllipticalArc cases: four source conics and both sides; repeated source following, rejected center drags and reopen |
| Circular dimensions | 96 Circle/Arc R/D cases: four center-source kinds, driving/reference, locked/unlocked, free radius or circular equality; three edits, independent radius and center checks |
| Angular dimensions | 32 three-point angle cases with a native vertex bound to the external center; both ray orders, driving/reference, locked/unlocked, repeated edits and drags, independent dot-product angle |
| Ellipse dimensions | 48 major/minor/rotation cases: four source conics, driving/reference, locked/unlocked, repeated edits/drags and independent defining-point measurements |
| Invalid source classes | Circle/Ellipse to line; Circle to Ellipse with required radius; frozen packet/shape, repeated-refresh no-op, reopen and repair; a deformed spline is rejected by the conic certificate |
| Native Part persistence | Four real Part documents with owned source/target Sketches; `.prtz` save/reopen, center cross, exact point/constraint identities and DocumentSession Undo/Redo |

Unsupported locked reference dimensions and redundant driving R/D on a radius
already owned by external equality are checked for atomic rejection. Reference
dimension input updates its measurement without driving geometry. A rejected
drag must leave the serialized document unchanged; an accepted free-point drag
must actually move geometry. These counts include explicitly rejected variants.

`zima_cpp_external_curve_centers_ui_contract` passed all twelve scenarios in
39.90 s: four conics, manual C in both selection orders and automatic Circle
creation. Circular = is tested in both orders. Save/reopen, Undo/Redo, derived
center selection/Tree synchronization/Delete/Undo and real mouse confirmation
are checked. The ellipse-source scenario also changes a native Circle radius
three times through the real inline editor, attempts three mouse drags of the
bound center, and checks edit Undo/Redo. Two screenshots were visually inspected:
`Projects/test/external-center-0-2.png` and `external-center-2-0.png`.

Existing full Sketcher and analytic-profile native regressions passed (9.33 s
and 0.83 s respectively). The final seven-test run also passed the 25-scenario
external-constraint GUI suite (51.11 s), actual body-edge circular/arc contour
GUI (6.36 s), five-language source coverage/catalog/translator checks (15.11 s),
template GUI (12.64 s), new-document options GUI (40.16 s) and dedicated centers.
The added selection prompt is translated in cs/en/de/fr/ru and explicitly checked
after changing the application translator. Logs are
`build/external-centers-final-tests.log` and
`build/external-centers-native-regression-v2.log`. The final expanded native
matrix, including ellipse semiaxes and rotation, passed in 0.98 s
(`build/external-centers-final-native-matrix.log`).

An independent known-center numerical oracle exercised 10,000 affine elliptical
arcs and projectively reparameterized quadratic weights, seed 20261020. Its
maximum center error was 2.7107228301795386e-9 mm. This is a finite numerical
sample, not a universal conditioning bound (`build/external-center-oracle.json`).

## Factory assets and remaining boundaries

Read-only inventory found no new center token in 1,210 tracked native assets.
Thus existing factory geometry needs no center-specific conversion. All six
Part/Skeleton/Assembly start templates were saved/reopened with the current
native implementation and remained byte-identical; the actual GUI checks above
confirmed new documents, active first editing contexts and normal commands.
The separately updated factory VentilationWindow from the analytic-profile
change remains part of the next committed package's acceptance. Custom libraries
and the user's edited pressed M10 nut were not overwritten.

Direct virtual external points retain the existing native-only three-point
angular creation limitation: use a real native vertex bound by C. Degree-elevated
or general NURBS conics, extremely short/ill-conditioned spans, every Assembly
occurrence transform and every mouse gesture/language/theme combination have not
been exhaustively verified. Elliptical arcs retain their existing available
dimension commands; no new semiaxis command was added. Native Linux execution
and new Windows package acceptance remain separate gates.
