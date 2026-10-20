# Horizontal text landings for 3D angular dimensions - 2026-10-20

## Requested presentation and shared implementation

The user requested the same termination used by spatial linear dimensions:
continue the angular dimension behind its arrow, bend into a horizontal
landing, and place the value above it. The arrow tip retains the measured arc
endpoint. The end tangent, not a free diagonal toward the text, owns the first
leg. No native document or parameter-dialog contract changes.

`dimension_presentation.hpp` already owns the common geometry used by painting,
label picking and layout grips. The bounded change routes model angular text
through its existing outside horizontal-landing branch. When an arc end tangent
points across the requested screen side, the landing extends outward from its
elbow; selecting the opposite shelf end would detach the leg from that tangent.
Linear, radial and diametral presentation retain their existing branches.

The existing `angular_text_shelf` argument distinguishes model presentation from
planar Sketch/Drawing circular text. Sketch and Drawing callers already pass
false. Their curved text, sectors, extensions, printing and vector-export path
remain under their existing contract and regression suite. Existing explicit
angular leader annotations retain their separate early branch.

All work is projected annotation arithmetic. It neither calculates solid geometry
nor changes a measured angle, its semantic reference or persisted layout format.
Ordinary selection, text picking and drag grips consume the same result. Existing
text dragging can move the annotation arc radius as well as its text offset;
that is presentation geometry, distinct from changing either measured ray or the
angle value. This change does not redefine that established drag contract.

## Finite evidence

- `build/angular-landing-baseline.log`: the previous implementation fails the
  requested horizontal outside landing assertion in a normal 3D view.
- `build/angular-landing-fixed-native.log`: the first implementation exposes an
  arc-tangent/shelf-end mismatch in the broader sector matrix. The final path
  chooses the outward shelf side rather than folding the shelf across its tail.
- `build/angular-landing-fixed-native-v2.log`: the complete dimension layout
  suite passes in 8.59 seconds, including 1,344 projected angular variants:
  front/isometric/reflected views, four starting bearings, seven signed sweeps
  from 15 to 270 degrees, both text sides, arrow reversal, normal/basic framing
  and 12/24-pixel fonts. Independent equations check the actual endpoint, tail
  tangency and outward direction, minimum tail length, shared elbow, horizontal
  shelf, opaque text clearance and unchanged arrow tips.
- The existing suite also covers planar angular clearance, linear/radial
  presentation, picking, layout persistence, per-view Drawing overrides and grip
  transactions. `build/cpp-windows-release/dimension-presentation-proof.png`
  provides a rendered side-by-side linear/radial/angular proof.

`build/angular-landing-degree-proof.log` and its detailed log pass the complete
suite in 8.04 seconds, including actual View Light/Dark/Light, label picking and
three mouse text drags. Degree-labelled render proofs are
`build/angular-landing-theme-{0,1,2}.png`. Measured rays, value and reference
identity remain unchanged; the established presentation drag can adjust the
annotation radius. The initial harness was corrected to use ordinary confirmed
selection and to check measured rays rather than treating annotation endpoints
as immutable source geometry.

The five-language localization contract passes; this rendering change adds no
user-visible text. Fresh packaged Windows acceptance remains separate. Native
Linux build/GUI verification remains outstanding on the Linux host.

## Packaged Windows acceptance

Signed [Windows 2026102004](../releases/2026102004.md) passed fresh committed
build, candidate/signed native smoke, 42 finite packaged scenarios, immutable
source/resource comparisons, production trust and public asset verification.
The dedicated 1,344-case angular matrix and live View checks above were
run in the native Windows source build; no separate packaged 1,344-case run is claimed.
Native Linux execution remains unverified on this Windows host.
