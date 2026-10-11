# Large-import placement validation and neutral 3D Curve presentation

## Scope and cause

The actual G92H1-T native fixture is 618,644,457 bytes and contains the complete
import used for the preceding Windows 2026101101 acceptance. The follow-up
profiles the shared viewer candidate pipeline while entering a Body placement
reference. The common picker takes approximately 9–12 ms. The command filter
was taking approximately 1.4 seconds, repeatedly resolving many imported edges.

The reference-entry validation cache discarded all entries when it reached 64
identities. The measured pick position `(352,176)` offers 2,272 candidates,
including `step:face:#1496349`. Every unchanged warm pass consequently repeated
more than 500 placement resolutions taking over 0.5 ms each. This limit defeated
reuse precisely on the dense imports for which it was intended.

Retain entries for the reference-entry session and its scene revision. The cache
contains only stable identities from that finite scene; it is not a global cache
and does not retain geometry packets. Starting another reference entry creates
a new cache. A scene revision clears existing entries. The existing checks of
prepared row values, current side/locks, measurement inputs and signed zero
remain in place. Equations, tolerances, candidate ordering, supported references,
visible fragment identities, persistence and command ownership are unchanged.

## Diagnostic measurements

Serial diagnostic runs use the same fixture, viewport, configuration copy and
native Windows executable. Before the change, the mean of twenty warm picks is
1,398.8 ms; after it, the mean is 39.7972 ms. The candidate run compares the
entire ordered candidate list on every warm pass and passes exact face hover,
RMB cycling and LMB reference confirmation. Temporary per-phase instrumentation
is removed from the product after tracing the cause.

These are picker-phase measurements, not complete UI response promises. The
diagnostic confirmation samples remain 4,738.53 and 4,642.72 ms, respectively.
The complete tests take 282.234 and 286.953 seconds and include file handling,
scene painting and extensive candidate cycling. Their elapsed times do not
demonstrate a total workflow speedup. The 0.5-second Body movement/confirmation
goal remains unmet; the previously recorded scene-publication costs still apply.

An additional uninstrumented-picker run measures 31.1979 ms and confirms exact
agreement after a new scene revision forces fresh validation. Its click remains
4,608.28 ms. Existing scene tracing identifies two publications of the same
Body preview, each requiring approximately 1.7–1.8 seconds. The user explicitly
approved removing the second Body publication on 2026-10-11. Other feature
dialogs retain their existing post-pick initialization. A permanent GUI assertion
counts publications during actual reference confirmation.

Body confirmation also reuses its existing, fixed reference geometry index for
baseline and proposed references. All existing seed, measurement, solving and
DOF operations remain; their packets retain every matching fragment in source
order. Commands without that Body-owned index retain their complete source.
The explicitly confirmed visible fragment still provides the clicked point and
inspection identity. The final serial run measures 34.4553 ms for the twenty
warm picks and 2,250.38 ms for reference confirmation, with exactly one scene
publication. Fresh and cached lists still match all 2,272 candidates. Display
preparation takes 1,055.359 ms and viewer publication 619.291 ms; these remaining
costs explain why the 0.5-second complete confirmation target is still unmet.
The complete test passes in 282.125 seconds, including extensive RMB cycling.

## Presentation and verification

Confirmed 3D Curve strokes and owned point markers use the Sketch foreground.
Point-entry frames retain their brown datum colour. The shared 3D Curve and
Surface Intersection SVG uses the existing palette-aware icon engine. The
colour ownership lookup is rebuilt with the scene and includes the exact
occurrence path; painting does not scan all edges for each point.

The focused GUI contract passes Wire, Shaded with Edges and Shaded presentation
and live Light → Dark → Light changes, including confirmed points, brown edited
points, curve selection and brown axes. The shared icon test also checks existing
icons through live theme changes. No product UI strings are added or changed;
localization coverage remains required in all five supported languages.

The general GUI test previously constructed a Hole by changing a Sketch's enum,
leaving its required serialized Sketch empty. Specialized feature fixtures now
use their ordinary factories. Its end-plane picking fixture also attempted a
hover/click assertion while animating the camera; it now uses the same intended
front orientation directly. These are test-input corrections, not changes to
Hole calculation or camera navigation.
The existing mixed-reference frame test retains its XZ/FRONT assertions and
expects XY for its complete-Origin case, matching the already established
whole-Origin contract. Its equation and reopened-reference assertions remain.

The corrected complete `zima_cpp_ui_contract_tests` passes in 16.27 seconds.
The eight affected GUI contracts all pass: small import interaction (including
the exactly-one-publication assertion), curve presentation, both curved
placement branches and repeated reference edits, placed Sketch references,
Boundary Surface, General Surface and Surface Intersection. The separate
localization, point-marker, axis, mesh/curve placement, Body reference-command,
reference assignment/removal and placement-command contracts also pass.

Evidence is retained in `build/import-followup/large-picker-filter.log`,
`large-picker-session-cache.log`, `large-picker-final.log`,
`large-picker-single-scene.log` and `body-reference-single-scene-contracts.log`.
Five generated fixture copies were removed after completed verification,
recovering 3,093,222,285 bytes; the original fixture and all evidence logs remain.
Signed-package verification is recorded in the corresponding release record.
Native Linux
verification belongs to the user's later Linux session; shared source does not
constitute a Linux acceptance result.

## Signed Windows follow-up

Signed Windows 2026101102 repeats the full large-import GUI checks: warm picks
average 26.0481 ms and reference confirmation takes 1859.27 ms, publishing one
scene. Both fresh and cached ordered lists match exactly. Complete offset
previews remain approximately 2.8–4.0 seconds; the 0.5-second full movement target
is still unmet. See the [release acceptance](../releases/2026101102.md) for exact
samples and public asset identities.

The ordinary cylindrical placement lifecycle passes two serial trials per
version, using identical copied settings and clean runtime environments.
Complete-process means are 18.391 s for 1101 and 18.586 s for 1102, about 1.1%
apart; this finite comparison shows no substantial regression for that workflow.
It does not establish timing parity for every operation.
