# CAM setup UI and stock presentation

Date: 2026-11-24. Baseline: `62ef05cd0`, Windows x64 Release, MSVC,
Qt 6.11 and OCCT 8.0.1. This is a local candidate checkpoint, not a completed
CAM product or a deployed native format extension.

## Implemented workflow

Create CAM is available at the source Part's Body-list level. Creation and
Properties use one internal Properties window with shared reference cells,
independent inspection eyes, the existing complete container-placement section,
and OK/Cancel. Stock and target are required replaceable Body references;
fixtures are removable list entries. Duplicate fixtures are rejected.
The same Body can serve multiple roles without sharing the fields' inspection
state. Replacing a source clears its old inspection; removing a fixture deletes
its row and re-arms the empty fixture field. A short middle click ends source
entry and temporary inspection without deleting reference values.

The existing machine editor is integrated as a child draft. Its OK updates only
the pending setup; parent Cancel discards machine, source and frame edits together.
Machine work-origin coordinates are editable separately from the machining frame.
Initialization and unchanged OK preserve authored precision beyond the displayed
decimal places. Active CAM exposes setup controls and passive source geometry;
ordinary CAD modeling shortcuts are disabled in that context.

The editor consumes the shared placement implementation without changing its
solver, reference contract or persistence. Source Part editing is supported;
editing a CAM setup through a nested Assembly occurrence is not exposed.

## Scene lifecycle and appearance

Opening prepares one input scene from already calculated source packets. Visible
predecessors and selected sources remain displayed, including a selected fixture
that was hidden in Modeling. View picking offers only displayed eligible Bodies;
the Tree can explicitly add a hidden source. Arming source selection reuses the
prepared reference packet. Inspection changes overlays without republishing the
base mesh. Source-role edits rebuild geometry only when the set of displayed
sources changes. Frame previews use cached reference geometry and existing frame
placement helpers, not a solid calculation. Cancel/OK restore the normal scene.

The user confirmed translucent stock on 2026-11-24. During CAM display and setup
editing, stock uses alpha 64/255 (approximately 75% transparent); target and
fixtures use alpha 255. Their authored RGB, finish and native geometry remain
unchanged. Target/fixture roles win when a Body serves both roles. Returning to
Modeling restores the authored appearance without a document transaction.

Visual inspection exposed depth fighting where stock and target share a plane.
The existing polygon-offset rendering path now gives blended fill one additional
depth-bias step, preferring opaque material at a coincident surface. This applies
to transparent fill generally. It does not move mesh coordinates, alter picking,
change the separate edge-depth pass or override the intentional foreground bias
of technological sheets. A distinct foreground transparent surface still blends.

## Verification

The native GUI setup verification passed **153 checks**, including actual Tree
and View clicks, candidate cycling, hidden-source filtering/display, independent
inspection, replacement/removal, resizing, whole-Origin entry, individual frame
reference replacement, machine editing, Cancel, unchanged OK, middle-button
confirmation, Undo/Redo and native save/reopen. All setup interactions retained
the original stock packet and recorded **zero CAD body calculations**. Unchanged
OK left the document revision unchanged. The synthetic fixture has a 20 x 10 x 6
stock, 20 x 10 x 4 target, and a hidden 30 x 14 x 2 fixture below the stock.
Its isolated test process completed in about 4.9 seconds; this is not a machining
performance benchmark or a result measured on the user's `50.prtz`.

All five languages (cs/en/de/fr/ru) were checked in the live dialog in light and
dark themes, preserving authored names. Fourteen new catalog keys were reviewed;
the source-coverage/catalog validation target passed. The final Czech dialog and
clean stock Viewer captures were visually inspected.

Eight native/widget regression targets passed together, serially, in 29.31 seconds:

- `zima_cpp_cam_program_tests`
- `zima_cpp_cam_machine_dialog_tests`
- `zima_cpp_cam_document_tests`
- `zima_cpp_cam_sweep_tests`
- `zima_cpp_cam_simulation_tests`
- `zima_cpp_cam_operations_tests`
- `zima_cpp_cam_calculation_tests`
- `zima_cpp_translations_contract`

The new coincident-surface framebuffer test failed before the depth-bias fix and
passed afterward. It checks opaque target pixels with different stock
triangulation, both mesh orders, top/isometric views, and blending of distinct
foreground stock. The complete appearance contract also passed, including
transparent ordering, fully transparent fill, alpha-independent hidden edges,
palette transactions, persistence and existing material rendering.

The existing Render GUI regression passed on the final candidate: source/Origin
selection, unchanged OK/Cancel, Undo/Redo, private rendering, JPEG/PNG output,
background snapshots, gallery persistence and five languages.
The candidate startup smoke also passed factory-template opening, Box
calculation, native Save and View rendering. This is not the complete factory
asset audit required for deployment of the CAM document extension.

Disposable logs and captures are under `build/cam-20261124/`. The GUI verification
is registered as `zima_cpp_cam_body_ui_contract`; the isolated candidate executable
was used directly so that the normal development executable was not replaced.

## Remaining work and deployment boundary

Tool/operation editors, the generic NC pane and stepping UI, and oriented
clamping-Sketch exclusion/grip checks remain unfinished. The existing native guard
against unchecked Sketch clamping remains enabled. This setup window does not
claim that defining a fixture certifies workholding or generates a toolpath.

No native serialization or protected placement contract changed in this step.
Before deployment of the earlier CAM format extension, the agreed template GUI,
affected factory/native-library and protected-document-copy verification gates
still apply. No user model, factory asset or private profile was rewritten.
Linux and nested-occurrence GUI verification have not been performed.

The normal `zima-cad.bat` executable remains the verified Section-fix build,
SHA-256 `6B76A2AC1B8DCC1B764F07E84D9968DDB4EAC440D4D92742E7B0C4ADE7D1B1E6`.
No portable release was created or published.
