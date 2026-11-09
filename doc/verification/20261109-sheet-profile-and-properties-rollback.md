# Sheet Profile attachment and Properties rollback verification (2026-11-09)

## Scope

This finite Windows Release matrix verifies the correction batch after signed
Windows 2026110802. The baseline source before the final Profile work was
`604247d77`. The source candidate version is `2026110901`.

The batch contains five already separated changes:

- 3D Sweep Properties installs its rollback boundary before the first preview,
  so the real input scene remains visible and pickable. Circular Pattern also
  regenerates the copied Revolution axis.
- Trim preserves applicable Parallel and point/line Horizontal or Vertical
  relations only when the surviving endpoint geometry still satisfies them.
  Projected Drawing views can be placed directly from their prepared frame and
  the localized Insert View text follows the selected language.
- Pattern and Mirror expose each copied container Origin, including its point,
  axes and planes, to ordinary reference picking and 3D Sweep reference entry.
- Interactive Drawing uses its prepared depth geometry, removes the expensive
  line-weight preview control, and displays thin edges with the same continuous
  gray family as hidden edges while PDF/vector export retains output weights.
- Sheet Profile removes the material-side control. Selecting a valid sheet
  boundary derives edge, joining face, endpoint and thickness, creates an empty
  owned route as Bend followed by tangent Flat, and maps the route into positive
  local X/Y. Endpoint switching and manual detachment reuse the existing controls.

No native file-format field, factory asset, start template, protected shared
placement solver, geometry-side meaning, modeling precision or dependency pin
changed. Windows and Linux continue to share the same C++ implementation and
OCCT 8.0.1 pin. Linux build and execution were not run on this Windows host.

## Profile contracts

The native attached-Profile fixture creates a real thickness-2.5 Flat, selects
one of its actual boundary references and commits a Profile with one 90-degree
arc and one tangent segment. It independently verifies:

- one picked edge fills the three Bend-style references;
- attached thickness is inherited and its override is disabled;
- the persisted material side is the derived outer branch;
- all authored route points are non-negative in local X/Y;
- the first generated child is Bend, the second is Flat and the Bend has `R=T`;
- endpoint switching changes only the derived endpoint;
- manual placement restores the ordinary plane and thickness controls;
- Undo/Redo restores byte-equivalent definitions;
- native save/reopen and cold OCCT regeneration finish without errors.

The general free-Profile UI and native suites still cover cyan preview, commit,
Tree ownership, explode, existing material-side persistence, Undo/Redo and native
reopen. Existing authored route geometry is never replaced when attachment is
added; automatic seeding is limited to a new empty owned Sketch.

## Properties and rollback matrix

The Release build completed successfully for the desktop application, Profile
tests and the shared UI contract. Tests used an explicit writable `TEMP`/`TMP`
directory so command fixtures did not depend on the user's system temp ACLs.

The selected matrix passed 13/13 in 333.23 seconds:

| Contract | Result |
| --- | --- |
| Bend attachment UI | passed |
| 3D Curve / 3D Sweep native contracts | passed |
| Sheet attachment dialog, including Profile | passed |
| Five-language catalog coverage | passed |
| Fillet/Chamfer Properties rollback | passed |
| Sheet Form Properties rollback | passed |
| Sheet Profile native contracts | passed |
| Sheet Profile UI contract | passed |
| Solid-state Properties rollback | passed |
| 2D/3D/helical Sweep solid-state rollback | passed |
| Flat command contracts | passed |
| Arc-only Bend UI | passed |
| Bend command contracts | passed |

The Sweep rollback GUI contract was additionally repeated twice without source
instrumentation; both runs passed in 24.81 and 24.65 seconds. It covers 2D spline,
3D spline and helical inputs, reference picking, the real preceding scene,
Cancel, unchanged OK without Undo, native save and reopen. The Loft Properties
rollback and five-language dialog contract passed separately in 6.96 seconds.

This is a focused matrix, not a claim that every repository test or every
possible modeling feature was exhaustively exercised. The earlier first Sweep
GUI run terminated without a diagnostic; one traced run and the following three
uninstrumented runs passed. No product-source change was made to hide that
observation.

## User model check

The current `ZE-CUBE.prtz` opens and explicitly regenerates through the shared
CLI without a calculation error. Its Body history contains five containers:
two Extrusions, one Pattern, one 3D Sweep and one Fillet. The 3D Sweep path is a
closed Euler route with 22 route vertices, 21 segments, 21 unique undirected
segments and 10 unique nodes; no path segment is traversed twice. The user model
is outside the repository and is not included in the product commit or release.

## Localization review

The Profile UI removes one existing control and reuses the already translated
**Other edge endpoint** and **Manual placement references** strings. No new
control label was added. The three new validation messages are translated for
Czech, English, German, French and Russian and the dialog now resolves dynamic
operation failures through the active Qt catalogue. Catalog coverage also
includes the projected-view translation added earlier in this batch.
