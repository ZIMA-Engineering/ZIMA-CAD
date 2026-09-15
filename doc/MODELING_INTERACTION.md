# Highlighting and editing in Modeling

Confirmed View/Tree selection uses cyan wire, visible through material in all
five display modes so an entire hole and its internal subfeatures can be inspected.
Only the selected container, subfeature, or exact component occurrence is highlighted;
other instances of the same source Part are unaffected. Hover uses orange wire.

Double-clicking a container keeps its cyan wire visible with its dimensions.
This inspection state is separate from input selection, so dimensions remain
selectable and repeatedly editable. Clicking empty View space clears confirmed
selection, inspection wire, and associated Tree selection. Hole and Thread
parameter changes preserve the camera.

Wire comes from persisted container geometry. Chamfer and Fillet use real input
edges before the operation; their selection is not a general placement reference.
Display, hover, and selection do not invoke OCCT calculation.

Tree rows for broken references have a red background in Part and Assembly,
including sections. The error disappears only after actual reference repair;
OK alone does not suppress it. Properties shows a missing reference as an empty
red field that can be replaced. The original identity remains until replacement
or removal. Valid references use readable names such as **Face 6**, not internal IDs.
Standalone Thread also retains the last calculated geometry of missing references
for ordinary regeneration; opening its Properties requires replacing missing
mandatory references. Persistence and Cancel details are in [Shaft Thread](SHAFT_THREAD.md).

In the right-hand Part command panel, a green separator divides Shell from the
group starting with Hole. The Hole command and its primary Tree subfeature share
a name and icon. Shaft Thread has a separate icon.

`zima_cpp_ui_contract_tests` verifies shared rendering, including occluded edges,
all display modes, and exact occurrence paths. Thread integration also checks
camera and wire retention after dimension editing.

## Initial View and first-Sketch scale

An empty document uses an approximate 1:1 scale based on View height and monitor
DPI. Accuracy depends on monitor information supplied by the operating system.
Origins and axes are auxiliary references: they do not zoom an empty Sketch down
to a fraction of a millimeter, even when the same Origin occurs repeatedly.
Fit View still fits existing geometry to the viewport. Origin symbols have a
screen size independent of this scale.
