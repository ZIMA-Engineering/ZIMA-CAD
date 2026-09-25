# Unified Feature: GUI prototype

Scope: an interactive layout proposal for review before implementing modeling.
The layout has been approved and connected to native modeling; see
[the implementation checkpoint](UNIFIED_FEATURE_IMPLEMENTATION.md). The approved layout is now hosted by the real Feature Properties dialog.
The historical prototype limitations below describe the original review stage,
not the current implementation; see the implementation checkpoint for status.
The Part toolbar exposes Feature above Extrusion. The shared PropertiesSubWindow
and ContainerPlacementSection provide the existing internal-window style and
placement layout. Shared placement solving and picking are not modified.
The middle controls are grouped into Sketch (base plane and plane offset) and
Result (result type, shared Add/Subtract, thickness and thickness direction),
using the same group-box presentation as Side 1 and Side 2.
Add carries a green plus (black on hover or while selected); Subtract carries a red minus.

Inputs are one Sketch, container placement and independent side settings. Each
side offers no operation, Extrusion or Revolution. Symmetry disables independent
editing of side 2 and hides its controls while retaining its independent values. End-condition choices and length/angle fields follow the
selected operation. A common result type selects solid, surface or thin feature;
one shared addition/subtraction choice for the entire Feature, thickness direction and
origin/centroid-axis options complete the
proposal. The blue Sketch button sits inside the Sketch group to the right of its two plane-setting rows. It spans their combined height,
uses a 220-pixel logical width and carries the shared Sketch icon. The window
opens at the minimum width computed by the shared properties-window layout.

The original prototype stage did not create geometry, modify history or serialize a new feature.
OK and Cancel close the prototype. Reference picking and the Sketch editor are
explicitly disabled with explanatory tooltips. Numeric placement fields are
editable for layout review only. The existing modeling commands remain available.
Mixed Extrusion/Revolution sides require later geometry and topology validation;
the prototype does not establish that every such combination is manufacturable
or produces a valid connected solid.

After GUI approval, connect the existing placement, Sketch and modeling contracts
incrementally, preserving identity, side choices, undo and rollback semantics.
Do not remove old commands before the unified implementation is verified.

Operation matrix: one common Add/Subtract choice applies to the complete Feature,
including mixed Extrusion/Revolution sides. Solid and thin results expose both
buttons. Surface mode disables Boolean operations. Both sides off represent the
Sketch-only proposal and disable Boolean operations. Each side independently
chooses its type and end condition. Symmetry uses side 1 settings on both sides;
leaving symmetry restores the independent side 2 controls. The GUI test covers
all 27 side/result combinations and exclusivity of the common operation buttons.

Symmetry and the two axis options share one row above the side panels, avoiding
a separate row below them. Window height follows the compact layout.

The dialog now initializes and returns a typed `FeatureParameters` editing
definition. Each side remembers its own length, angle and limit choice when its
operation is switched or disabled. Symmetry derives the effective second side
without overwriting its independent settings. The GUI regression checks these
transitions and restoring a populated editing definition. The original prototype
used pending dialog state only. Native creation, persistence and calculation are
now connected through the shared profile transaction; the implementation document
describes the current verification and limitations.
