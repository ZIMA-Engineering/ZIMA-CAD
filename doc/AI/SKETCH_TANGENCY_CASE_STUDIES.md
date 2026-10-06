# Circular tangency and rectangle case studies

## Inputs, means and outputs

Inputs are persisted native circle/arc geometry, immutable external references,
dimension drivers and drag targets. The expected output is a geometrically valid
Sketch, retaining its chosen contact sides and source identity through editing,
local Trim and native save/reopen. Existing ZIMA constraints, exact viewer curve
packets and the ordinary solver are the means; interaction does not invoke OCCT.

Independent checks compare centre separation with the sum or difference of
radii, line distance with radius and retained contour endpoints with actual
contact points. A successful solver status alone is insufficient evidence.

## Reproduced defects and repairs

- A circle tangent to an external fillet had a persisted contact on the external
  reference, but its inserted Reference Profile used another native curve ID.
  Trim did not transfer the contact to that local curve and deleted the entire
  contour. Source ownership now transfers the existing contact to the exact
  local support. Contacts outside an already retained interval are ignored.
- Trim sampling used a principal-plane conversion instead of the Sketch's
  resolved frame. It now consumes the same world-to-local transform as the other
  Sketch geometry paths.
- A retained contour did not follow a moved tangent contact after radius edits.
  Tangent intersections have a singular intersection Jacobian. Existing native
  contacts now locate the exact endpoint on both current supports, within the
  existing branch bounds. Successful dimension and drag transactions refresh
  dependent curves before publishing their result.
- Shared arc endpoints could fail contact-domain checks because angle rounding
  placed the theoretical contact infinitesimally outside the domain. The existing
  physical endpoint check now also resolves shared endpoints; its 1e-8 incidence
  tolerance is unchanged. External reference domains remain checked.
- A native arc endpoint already incident on an external arc was not retained as
  the tangent contact. Creation now consumes the existing endpoint/C relation.
- Three-curve chains could accept dimensions but reject dragging. The existing
  bounded native circular seed now includes native circles and circular-pair
  tangencies, preserving internal/external contact choice. Shared endpoints fit
  together before consistency validation. A chain around a fixed neighbour can
  deform through free radii. Free arc endpoints retain their angles instead of
  unintentionally rotating outside valid tangency domains. Ordinary equation
  and domain verification still decides whether to commit a candidate.

No native format, topology identity or shared container-placement contract is
changed. Source external geometry remains immutable.

## Native matrix

The executable `zima_cpp_sketcher_contract_tests` includes
`cpp/tests/sketch_tangent_reference_studies.hpp`. By default it uses an exact
rational quarter-circle with R3, matching the saved FORM fillet. Setting
`ZIMA_TANGENT_TRIM_SOURCE` to a serialized Sketch containing the actual persisted
fillet reference runs the same matrix on that source. Private project data is
kept outside Git.

| Study | Variants and checks |
| --- | --- |
| Tangent Reference Profile Trim | Profile inserted before/after T; both curve-selection orders; ordinary/transformed frame; both retained sides; exact contact split; source unchanged |
| Trim dependencies | Successive R15, R16.5, R14 edits; exact retained endpoint incidence; both contact drags; nonbroken dependencies; save/reopen |
| Directly created native arc | Endpoint C on external fillet and axis; both curved selection orders and T creation orders; R16, R14, R15.5; centre and both endpoint drags; persisted contacts/source identity |
| Three mutually tangent curves | Three circles and three arcs; both selection orders; three T equations; successive radius edits; centre dragging; native reopen |
| Three-curve chains | Circles and arcs with shared endpoints; both orders; radius/diameter drivers; successive changes; centre drag; independently checked separation; locked-driver transactional rejection |
| Internal circular tangency | Both selection orders; fixed outer radius; successive inner-radius changes and centre drag; retained internal side |
| Rectangle axis symmetry | X/Y axes and both sides; point-pair S; successive width edits; corner dragging; reflected corner coordinates; native reopen |

This matrix supplements, rather than replaces,
[the preceding Sketch tangency matrix](SKETCH_TANGENCY_VERIFICATION.md), which
covers position and angle dimensions, reference dimensions, rotations/reflections,
duplicate/impossible rejection and existing external-point H/V workflows.

## Rectangle interaction

Automatic centring of an ordinary rectangle on a principal or parallel
construction axis uses S between opposite corner pairs. Its offer follows the
automatic Symmetry setting and displays S. Width/height and sliding along the
axis remain available through ordinary point equations. Existing corner C and
external-point C/M offers keep their priority and meaning. An explicit slanted
construction axis continues to use the existing oriented-rectangle command and
its point-pair symmetry.

## Verification boundaries

Seven related native suites passed on Windows, including all five translation
catalogs and localized UI checks. No new translated UI strings were introduced;
S is the existing constraint symbol. The GUI suite passed 23 generated scenarios
and two retained-side Trim scenarios on a copy of the actual saved FORM Part,
using its persisted fillet and transformed Sketch frame. Eight rectangle GUI
variants passed: X/Y, both symmetry sides and the existing external-point C/M
offers. Commit, native save/reopen and Undo/Redo were checked. Release packaging
acceptance is recorded in [Windows 2026100611](../releases/2026100611.md).

The bounded native seed rejects unsupported equation graphs without publishing
them. These studies do not establish universal convergence for arbitrary
multi-curve networks, ellipse/spline tangency, internal arc tangency, or every
possible dimension combination. More cases belong in this executable matrix
when a new geometry or constraint combination is reported.

GUI, release and Linux acceptance are separate gates. Native mathematical
coverage does not substitute for common-picker, mouse confirmation, Undo/Redo
or packaged-runtime verification. Windows checks do not imply Linux execution.
