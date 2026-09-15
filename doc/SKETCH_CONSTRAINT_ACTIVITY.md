# Sketcher constraint activity

The Constraints button opens a shared internal window. A constraint's name and
symbol invoke its existing manual command. **Automatic** controls which relations
may be inferred while creating geometry; all automatic constraints are enabled
by default. Settings belong to the current application window and do not modify
existing constraints. Fix Point remains a manual command.

OK accepts settings; Cancel discards them. Choosing a manual constraint leaves
the narrow window open and starts its repeated command. The active constraint
and Constraints button are green. Ending the command or switching tools clears
the row highlight. OK, including MMB double-click over View, ends input and closes
the window through shared PropertiesSubWindow confirmation. Already created
manual constraints are separate Sketch operations; Cancel discards only automatic-constraint settings.

New free points and geometry origins infer horizontal/vertical alignment from
all persisted Sketch points. Preview and confirmation use the same function with
pixel-based tolerance. A segment's second endpoint uses the existing inference
candidate list, now filtered by Automatic settings. Rectangle geometry definitions
and already constrained points on arcs are unchanged.

Constraint markers carry exact semantic participant identities. Tree/View selection
highlights those points, curves, or axes in cyan; automatic inference highlights
its supports in orange. Only Sketcher geometry and Viewer data are used, without
OCCT calculation or whole-object recoloring.

Horizontal/Vertical first accepts either a complete segment (marker at its midpoint)
or the first of two points. Equal also accepts virtual corner radii. The solver
links their persisted values, respects the driving dimension, and stores the
constraint with the Sketch. Preview uses the same corner identity.
