# External shaft threads

**Thread** is a standalone cosmetic Part history operation. **Opening** still creates
holes and optional internal threads. Thread changes neither shaft volume nor material
diameter; it creates a thread-root surface and optional runout.

Inputs are the original external cylindrical shaft face, start plane face, optional
conical entry chamfer, and, for Up To, an end plane, coaxial cylinder, or conical end
chamfer. Internal cylinder walls are not offered. References belong to the exact
active Part occurrence and cannot target later history operations.

Internal/external classification accounts for analytic-frame orientation and material
side after Booleans. Reversing extrusion direction alone does not reverse cylinder
meaning. These data are calculated and merely read during picking.

The catalog shares Opening data. Selection transfers external root diameter, nominal
size, designation, and pitch once. Manually edited root diameter survives opening
Properties and editing other parameters. View always shows a numerical diameter;
double-click opens its numerical editor. With Properties open, only working values change.

Length is measured from the axis/start-face intersection to the thread cylinder end;
chamfer does not shift the measurement origin. Runout continues beyond this length,
limited by available cylindrical length. Up To and Through All create no runout.
Through All applies to the selected cylindrical portion. Up To Cylinder uses its
far axial end; Up To Chamfer uses intersection of the root cylinder with the finite
conical face. If a shallow chamfer lies wholly outside the root cylinder, thread
reaches the shaft end face. End chamfers must adjoin the shaft and narrow along the
thread direction. Unsupported/nonintersecting targets are rejected. Switching to
Up To/Through All also unchecks runout in the dialog.

The third reference row is always available for entry chamfer; clicking enables its
use and selection. Preview contains root-surface circles, a runout-end circle, and
one shared connector.

Source plane/cylinder/cone descriptions are persisted in reference packets during
explicit body calculation. Dialog, picking, and wire preview read them without OCCT.
If descriptions are missing, explicitly regenerate the Part first. OK resolves
references against operation input again. Root diameter is independent of shaft
diameter: shrinking the shaft retains the thread, visible outside material. Surfaces
are therefore not clipped by shaft volume; selected references define start/end.
The entire thread is one history/Undo operation. Properties uses persisted input
before the edited thread; Cancel restores original history. Shared container placement
is unchanged.

Regression checks: `zima_cpp_shaft_thread_contract_tests`, `zima_cpp_ui_contract_tests`,
and `ZIMA_VERIFY_SHAFT_THREAD_ONLY=1` with application `--verify-startup`.

If a reference is lost, ordinary recalculation/display uses its last successfully
resolved persisted geometric description, refreshed on successful calculation and
saved with thread parameters. This does not replace source-face identity or become
an offered reference; missing sources still mark the tree red. Even complete removal
of the source shaft preserves its thread.

Opening Properties clears only missing references in the working copy and uses the
same dialog as creation. OK requires valid mandatory references; Cancel preserves
original references and retained descriptions.

Picking/editing highlights follow shared [View and Tree](MODELING_INTERACTION.md)
rules. Thread has its own external-thread icon; Opening uses the same cylindrical-hole
icon in its menu and Tree child.
