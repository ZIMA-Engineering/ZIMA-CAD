# Fillet/Chamfer node choices, terminal trims and inspection

## Scope

This change follows `6afa11635`. It preserves the shared lightweight authored
wire generator: radius edits, node alternatives and ordinary inspection never
construct a treatment body. Changed OK uses the normal native calculation and
transaction; Cancel and unchanged OK preserve the document.

Two-branch corners now offer spike and rounded solutions for Fillet and
equal-distance Chamfer, for both additive and subtractive material. The rounded
connector revolves the existing end profile. Native result-face ancestry keeps
the connector owned by its treatment. The preview uses the same section and
rotation analytically. Intersecting branches without a distinct rounded solution
remain passive/azure. Compact saved input packets can prove a rounded connection
from their exact section directions even when oriented surface descriptors are
absent; known opposing material senses are still rejected.

Automatic end cuts on a multi-edge Fillet now follow the persisted curve-end
identity even if an arc is reversed during spine traversal. Circular preview
rails meet a tangent terminal plane through analytic circle/plane intersections.
Terminal planes are resolved from existing descriptors or verified planar
boundaries, without reconstructing a native body. Menu extensions must be
adjacent through the local curved terminal; an unrelated distant ray hit no
longer creates an additional Cut choice. An explicitly authored face remains
honored.

Growing a Fillet previously let its section centroid cross an earlier cylindrical
terminal and reverse the retained side. The route interior now chooses that side
independently of the new radius. Cylindrical trims with an unambiguous retained
side use a finite analytic cylinder covering the entire tool, with Common/Cut
selected by that side. This avoids the infinite half-space failure seen when
R8 crossed the previous R7 terminal and its tangent cap. A preview outside the
finite cylindrical arc closes at its adjacent cap instead of continuing onto
an unrelated cylinder extension.

## Ordinary inspection and editing

The existing feature double-click inspection publishes the dimension and
numbered editable treatment nodes. The follow-up described in
[Direct node editing and periodic terminals](20261116-treatment-direct-nodes-and-periodic-ends.md)
replaces the initial Properties shortcut with a direct solution popup. A single
selection click still follows the ordinary selection contract.

Labels are placed through the existing Body and occurrence transforms. The
viewer retains a copy of the active label callback while opening Properties,
because opening the editor replaces the callback and its labels.
Node probes map displayed ends to persisted vertex identities, including
reversed traversal and definitions without optional end overrides. Closing
creation Properties also retires the draft inspection activated by dragging
its dimension; nodes reappear only on subsequent explicit inspection.

A compact saved history may need the existing one-time preparation of its
shaded rollback input when Properties is entered. This is separate from the
treatment preview. The initial node interaction checks prepared that input
explicitly before asserting zero body calculations. The follow-up checks
exercise direct node editing from a cold saved document without opening
Properties; neither check claims that every cold Properties opening avoids
input materialization.

## Verification evidence

Windows x64 Release, Qt 6.11.0, OCCT 8.0.1. The user's original `03.prtz` was
read through an isolated snapshot under
`build/test-models/node-alternatives-20261116/input.prtz`; tests never save over
the original. The snapshot contains history through Zaoblení 06.

- Exact native curve comparisons cover spike/rounded nodes, both material
  senses and both route orders. The synthetic cases verify distinct volumes,
  node-face ownership and equal sampled wires after native save/reopen.
- The user's five-route pocket tests all four connected corners, including
  `1.1-2.1` and `2.2-3.1`, with both commands and both solutions. The largest
  measured deviation from the native result boundaries was about 1.3e-13 mm.
  Observed preview time was 0.10–0.17 ms, with zero body calculations.
- Zaoblení 01 exercises R1, R3, R4, R5, R6 and R8, including its curved route
  ending on a tangent plane. The largest observed native boundary deviation
  was about 5.2e-8 mm, below the 1e-5 mm verification gate.
- Zaoblení 04 exercises changed R1, R4, R5, R6 and R8 against its existing R3.
  The R5/R6 side reversal and R8 loss of the main component are covered. The
  largest observed deviation was about 6.4e-8 mm. At R8, authored wire segments
  merged into a support face by the final Boolean must independently classify
  as ON that face; points inside or outside the result are rejected.
- A separate native short-edge fixture increases the radius through and beyond
  its preceding radius. It checks validity, monotonically increasing material
  change and zero changed volume on the forbidden side of the old cylinder.
- Focused wire checks cover circular ends at R1/R5/R8, reversed endpoints,
  oblique planes, local/remote candidates and repeated Cut/No-cut changes.
- GUI checks exercise actual Tree and node double-click events, the dimension,
  shared node menu, Cancel, changed OK, unchanged OK, Undo/Redo and native
  reopening on additive/subtractive Fillet and Chamfer fixtures. The inspector
  also passes on both five-route pocket variants derived from the user's model.
- The actual three-edge pocket recovery matrix passes for both commands:
  rejected oversized OK, correction, Cancel, subsequent Fillet and Chamfer,
  native reopening and Undo/Redo. Four successive wire edits including GUI
  event processing took 17.53 ms for Fillet and 15.97 ms for Chamfer in these
  runs. Timings are observations on this host, not universal guarantees.
- All 15 selected CTest contracts passed across the final native and UI runs:
  planar Fillet/Chamfer, batch operations, treatment supports, history recovery,
  fragment/method dialogs, translations, full/focused wires, commands, treatment
  editing/creation, symmetric editing and ordinary selection. The first creation
  run exposed stale inspection after a draft dimension drag; the corrected
  lifecycle passed that test and the other three affected UI contracts.
- The final command matrix also verifies node choices with absent optional end
  overrides and with stored endpoint order opposite to displayed traversal.
  No new user-visible translation keys were introduced; catalog parsing and
  localization coverage passed for cs/en/de/fr/ru.

Detailed logs and disposable fixtures remain under
`build/test-models/node-alternatives-20261116`; recovery logs are the
`node-delivery-*` files under `build/test-models/pocket-diagnosis-20261116`.

## Delivery and limits

The implementation is shared C++. Native file formats, dependency versions,
side identities and the protected container-placement solver are unchanged.
UI labels and errors reuse existing cs/en/de/fr/ru translation keys. Linux
execution was not verified on this Windows host. The new inspection shortcut
was directly exercised in Parts; nested Assembly occurrence interaction was
not independently exercised in this change.

The local Windows binary is `build/cpp-windows-release/zima-cad-cpp.exe`, launched
by the unchanged root `zima-cad.bat`. This delivery is a local development build,
not a newly packaged portable release.
