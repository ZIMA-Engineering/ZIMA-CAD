# Sketch tangency verification, 2026-10-06

## Engineering scope

Inputs are native Sketch circles/arcs, immutable external line/circular-arc
references, native or external points, and dimension/drag requests. Outputs are
valid editable geometry, actual persisted contact points, unchanged source
identity, and transactional rejection when no permitted solution exists.
Means are the existing Sketch model, common viewer picker, exact rational arc
geometry, a bounded one-parameter geometric seed, and ordinary equation/rank
verification. No OCCT calculation is introduced into Sketcher interaction.

For the FORM start-profile geometry, an independent sanity check is the signed
line distance equal to the native radius and the centre separation equal to
native plus external radius. Every accepted manipulation retains these equations
and the external arc's finite contact domain. A seeded candidate is never accepted
solely because its requested dimension value was stored.

## Executed matrix

| Case | Variants | Evidence |
| --- | --- | --- |
| Circle tangent to axis and external arc | R13.63 and R20.425365935154556; both curved selection orders | Two distinct persisted contacts; independent incidence and tangency checks; native save/reopen |
| Position dimensions | X, Y, centre-to-origin distance | Actual coordinates measured against requested values |
| Size dimensions | Radius, diameter; successive R15, R17, R14 | Actual radius; both tangent equations; contact movement |
| Dimension state | Driving, reference, unlocked and locked | Reference value follows drag; unlocked driver follows drag; locked driver rejects movement |
| Trimmed native arc | Circle trimmed between contacts | Both endpoint tangencies survive; native save/reopen |
| Trimmed-arc dimensions | Radius R16 and three-point angle | Radius changes; angular driver changes actual geometry |
| Native dragging | Centre, line contact, circular contact of both circle and trimmed arc | Actual displacement; both tangent equations; unchanged source geometry |
| Orientation and side | 0, 90 and 37 degrees; reflected and ordinary sides | Signed line side, centre separation, unlocked driver dragging and native reopen |
| Rejection | Duplicate radius driver; R1 outside the retained external arc domain | Exception and byte-identical original Sketch serialization |
| External-point H/V | Both constraint kinds and both selection orders | Only native point moves; source-coordinate refresh; save/reopen |
| Exact external contour | Imported circular arc, local Trim, native reopen | 65 independent radial samples with 1e-10 tolerance; source unchanged |
| FORM-like native arc endpoint | Original saved start-profile coordinates and axis incidence | Endpoint tangent fit retains endpoint and axis geometry |
| GUI | 19 generated scenarios | Common picker, axis/external T, H/V, local arc Trim, six actual centre/contact drag gestures, commit, save/reopen, Undo/Redo |
| Localization | cs, en, de, fr, ru | Coverage, catalog and affected localized UI checks; no new user-visible strings |

## Scope limits

The new one-parameter seed applies to a native circle or an arc whose two ends
are the two tangent contacts, with one immutable straight support and one
external circular support, using external tangency. It does not replace the
ordinary solver for general multi-curve networks, ellipse/spline tangencies or
internal circular tangency. Existing paths remain in use for those cases.

Generated GUI fixtures isolate the saved FORM start-profile coordinates and
reference geometry. They do not claim to cover every downstream operation of
that private document. Existing FORM modeling and packaged regeneration checks
remain separate release gates. Actual dimension editing has native equation
coverage; the six new drag variants additionally use real GUI mouse events.
Linux execution is not inferred from Windows verification.
