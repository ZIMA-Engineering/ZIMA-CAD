# Fillet and Chamfer interaction without body construction

## Scope and lifecycle

This follows `dc7605674` and the input-topology recovery described in
[the preceding report](20261116-edge-treatment-preview-recovery.md).
The earlier change still calculated disposable bodies to draw previews and
probe node choices. That production path, its cache, the workspace preview API
and the native endpoint-inspection API have now been removed.

Creation, Properties, radius edits and endpoint menus consume the calculated
input viewer packet. They share the authored wire generator with ordinary
feature selection. Final body construction and validation happen on changed
OK. Unchanged OK and Cancel perform no body calculation or Undo transaction.
A rejected OK retains the dialog and unchanged input; correcting the draft or
cancelling and starting another treatment remains possible.

The shared generator now covers planar terminal envelopes, explicit extension
to a remote face, spike intersections, rounded two-branch elbows and supported
three-branch corners. Existing rolling-support and curved-terminal code is
reused. Terminal modes follow persisted vertex identities rather than display
traversal order. New junction defaults are assigned once; refresh does not
overwrite an authored mode.

Remote candidates come from published face adjacency and outward ray hits.
Local faces form one automatic trimming envelope. Explicit remote planes can
extend farther than the radius. No-cut retains the original perpendicular
section. Actual solid validity is checked on OK, not by building every menu
candidate in advance.

Only two-branch nodes with a distinct supported rounded alternative expose a
purple solution label. Continuous/intersecting nodes without that alternative
and automatic three-branch corners remain azure. Free-end labels retain the
existing No-cut/cut interaction.

Saved compact rollback packets retain analytic support descriptors in their
original-reference packet. Preview lookup now resolves the same face identity
there when the displayed face reference omits the descriptor. Current material
triangle winding determines orientation. This fixes the rounded-node preview
after save/reopen without changing document serialization or side identities.

## Verification

Windows x64 Release, Qt 6.11.0, OCCT 8.0.1. The user source was inspected through
an isolated saved snapshot, never modified. The pocket input used by the GUI
fixture is `build/test-models/pocket-diagnosis-20261116/input.prtz`, SHA-256
`ff32500b42f6b5b23dafb05476d34c32cefc236ee34dccd2a0e282faba63ce65`.

Fourteen relevant CTests passed:

- Native planar Fillet, planar Chamfer, direct batch treatment and history
  recovery contracts.
- Full Thin feature wire matrix, focused edge-treatment wire tests and
  edge-treatment command tests.
- GUI treatment editing, creation, symmetric endpoints and ordinary selection.
- GUI fragmented references, calculation-method controls and the five-language
  translation/catalog contract.

The creation test now compares the authored preview with its saved selection
wire. It no longer incorrectly requires independently sampled preview arcs to
have exactly the same polyline vertices as the tessellated final body.
Geometric equivalence is checked independently: ten node cases compare every
preview sample with the trimmed native result curves, using a 0.00001 mm gate.
The largest measured distance was approximately 2.9e-14 mm. These cases cover
both route orders, two/three-branch exterior corners, shoulder spike/rounding
and the mixed three-branch shoulder. Rounded-node native save/reopen also
retains the same sampled wire.

Focused wire checks cover open/closed/reordered source labels, plane/circle
rolling contacts, a smaller Fillet ending on a larger cylinder, Boolean-split
curved terminals, oblique local caps, remote planar extension, repeated
Cut/No-cut changes and reversed endpoint ownership. The tests use the existing
viewer precision for sampled curved supports; they do not establish exhaustive
coverage of arbitrary spline surfaces or every possible terminal arrangement.

Additional GUI runs on the actual three-edge pocket passed for both Fillet
and Chamfer. They exercise oversized values rejected on OK, correction,
failure/Cancel, subsequent new commands of both kinds, save/reopen, Undo/Redo
and unchanged OK. Joined pocket labels were verified passive/azure.
A separate reopened shoulder fixture exercised the real node menu repeatedly:
rounding, spike, rounding, then Cancel.

An in-process counter at the body-history evaluation boundary verifies zero
body calculations during opening, picking, radius edits and node-menu changes,
including free-end menus. Changed OK increments the counter; unchanged OK does
not. Native result construction in test oracles is explicit and isolated from
the production interaction path.

On the pocket fixture, four successive radius/size edits including the final
GUI event flush took 15.46 ms for Fillet and 8.00 ms for Chamfer in the final
runs. These are observations on this machine, not universal latency promises.
The deterministic improvement is zero pre-OK body calculations; the preceding
report's three-edge interaction still required 16 primary Fillet Boolean
batches. There is no precision reduction in final body construction.

Logs and disposable native fixtures are under
`build/test-models/wire-only-20261116` and
`build/test-models/pocket-diagnosis-20261116/wire-delivery-*`.

## Delivery and limits

The local development executable is
`build/cpp-windows-release/zima-cad-cpp.exe`; the root `zima-cad.bat` continues
to launch it. This is a local Windows binary, not a newly packaged portable
release. The verified executable SHA-256 is
`228cd7117146575d9ead47573e6ee5292e11f47269ad804900f9e404adbbc0ad`.

The implementation remains shared C++. No native format, shared placement
contract or dependency version changed. No user-visible strings were added;
existing labels were reviewed and the cs/en/de/fr/ru contract passed.
Linux execution and performance were not verified on this Windows host.
