# Drawing dimensions and shared Properties (2026-09-10)

## Inputs → means → outputs

Inputs are a specific Drawing view's saved geometry, camera, dimension type and
endpoint references. Means are projection of reference curves/points, shared
dimension presentation and the internal Properties window. Output is an associative
measured dimension in the sheet plane, without changing model parameters.
Selection, preview, dragging and opening Properties do not invoke OCCT calculation.

## One Dimension Properties dialog

The separate Dimension Display dialog has been merged into Properties. Prefix/
suffix text, replacement text, tolerances and placement use shared controls across
Sketcher, Part, Assembly and Drawing.

Sketch dimensions retain nominal value, driving/reference state and value lock.
Value and placement commit in one transaction. Part/Assembly model-dimension text
and tolerances belong to presentation settings; the value editor still changes
the model parameter. Drawing presentation of a model dimension has its own override
without modifying the source model.

Manual Drawing dimensions share text/presentation controls and add a Type and
References tab. Measured values are read-only. The mm unit directly follows the
number. All forms use internal `PropertiesSubWindow`, OK/Cancel and MMB double-
click confirmation over the View. Short MMB ends reference entry only; MMB dragging
does not confirm.

## Dimension command

Dimension replaces the experimental two-parallel-edge workflow. Creation and
editing use the same window.

1. Choose linear, radius, diameter, chain or angular dimension.
2. Click a reference field to arm its endpoint.
3. Select view geometry. The first reference determines the view; others belong to it.
4. After supplying references, place with LMB and confirm with OK or MMB double-click.

Everything remains preview until OK. Cancel discards new dimensions and pending
edits. A confirmed dimension's context menu offers Properties, removal and, for
linear dimensions, chain continuation from either endpoint. The ordinary context
menu does not open over purple handles.

**Delete** removes a selected manual dimension. For a model-derived dimension, it
hides it only in that view (Erase), preserving the source parameter. Clicking a
dimension also gives keyboard focus to the Drawing workspace.

Nominal values in Sketcher, Part, Assembly and Drawing use a decimal comma.
Precision sets the maximum decimal places; trailing zeroes are omitted after
rounding. At precision 3: `10mm`, `10,5mm`, and `10,526mm` for original 10.52584.
Small negative values rounded to zero display `0`. Numeric tolerances also use a
comma but retain entered zeroes. Full manual text overrides are unchanged.

## Attachment and selection

Each endpoint independently selects an attachment mode:

- Automatic: nearby curve endpoints/centres first, then geometry.
- Point: saved point or endpoint/centre of an attached curve.
- Point on curve: saved curve and parameter in source geometry.
- Segment: with automatic direction, the first segment defines the dimension-line
  perpendicular.
- Centre (C): circle/arc centre or displayed-axis centre.
- Tangent (T): contact in the measurement direction; RMB selects the other side.
- Intersection (I): two saved references and the chosen intersection branch.

Linear dimension lines can follow references, horizontal/vertical direction or a
further saved segment. After a first Segment input, attach a point or parallel
segment; a nonparallel second segment is invalid. Line intersections may lie
beyond the selected segments' endpoints.

Hover and confirmation consume one ordered candidate list. Before confirmation,
RMB only changes its active candidate. A green outline marks the sole armed field;
eyes independently inspect saved references and highlight their exact geometry in
azure. Camera changes do not move a saved point parameter along its source curve.

Displayed axes belong to the same reference list. Viewed end-on, any arm of the
cross can select its true centre; a side view also offers the axis line. Rendering,
offering and highlighting use the same four branches or segment, including 2 mm
paper overhang. Repeated Part occurrences keep separate references. Hidden axes
are not newly offered, but existing attachments remain valid. Missing axes require
reference repair. Point fields preserve the actual attachment type: vertex,
curve point or centre.

## Chains and reference repair

A normal linear dimension can extend into a chain from either endpoint. The
original segment retains identity, references, direction, baseline and placement.
Each new segment measures adjacent references. Placement lets the user choose
which segment's presentation to edit.

Properties mark lost references as missing. Last-valid presentation keeps the
dimension selectable with its last value. As agreed 2026-09-12, there is no question
mark: invalid dimensions are red and turn yellow after repair. Invalidity remains
red in Drawing exports; valid exported dimensions use the normal drawing colour.
Click a specific reference to replace it, retaining other attachments and style.
Restoring the same geometry resolves the original attachment again.

## Projection and measurement

Linear dimensions measure projection into the current view plane. Sanity check:
a 40 mm segment inclined 60° from the plane measures 20 mm along its projection.
R/⌀ display only for circular projections. Tilting a circle into an ellipse hides
them without deleting references or placement; returning to a normal view restores
them. In-sheet rotation does not hide them.

Measurement geometry is captured with view projection from saved source references.
Circularity is checked against all saved curve samples; merely looking circular
on screen is insufficient for R/⌀. Geometry, references including occurrence paths,
style and segment placements are saved in the current Drawing format. Old
experimental manual dimensions are not migrated.

## Radius based on the koty.bmp sketch

Shared Sketcher/Part/Assembly/Drawing rendering has three modes. While holding a
purple point with LMB, RMB cycles through:

1. Centre-to-arc line with an outside arrow.
2. Retained centre-to-arc line with reversed arrow.
3. Shortened dimension without a mandatory centre line. Arrow orientation follows
   the side on which the auxiliary line continues from its tip.

The next RMB returns to mode 1. The first two allow text beyond the arc or centre;
the third also allows it between centre and arc. The auxiliary line directly
continues the arrow leader, with text above it. In oblique spatial projection the
text shelf remains horizontal and the arrow leader retains projected radius direction.

Existing purple handles are reused. The point beneath text moves its shelf along
the radius without moving the arrow. The arrow point moves the dimension around
the circle; its saved angle rotates presentation in the radius plane. Neither
handle changes the measured value or model constraints. The plane is retained in
oblique and nearly edge-on views. Text masking still covers geometry under the
entire label with a 0.5 mm Drawing margin.

## Verification

During reference entry, eligible endpoints, midpoints and other explicit point
targets precede curve/line targets inside the existing hit tolerance. The core
picker and the canvas use the same ordering, including candidates gathered from
multiple views. RMB cycles the remaining choices. Explicit line/circle-only
requests keep their own eligibility restrictions.

Calculation contracts cover C/T, both intersection branches, tangent contact,
projected measurement, both chain ends, missing references, save/reopen and
invariant radius while dragging. UI contracts use real mouse events for references,
preview, Cancel, MMB confirmation, text tolerances and all three radius modes.
The shared presentation contract renders seven sketch states to
`build/radius-seven-states-proof.png`.

## Angular dimension between two straight edges (2026-09-12)

**Angular** belongs to the same Dimension command and Properties. It accepts two
different original straight references from one view, including exact occurrence
paths. Hover, LMB and RMB share the candidate list; the second edge is not filtered
for parallelism. Other attachment modes, linear-line direction and chain extension
are disabled for this type.

It measures the angle of projections in the view plane, not the hidden spatial
angle. Value and tolerances use degrees. LMB placement selects the sector (smaller
or supplementary angle) and arc distance from the intersection. Arm choices are
saved; geometry changes do not spontaneously choose another sector. Arrow handles
change arc radius; the text handle changes text placement, without changing the
measured value. Normal OK/Cancel, MMB double-click, Properties editing, tolerances
and arrow reversal remain available.

After geometry changes, explicit view regeneration uses original references.
Trimming/changing a straight edge's length without changing identity does not
disconnect direction measurement. Lost references, replacement of a line with a
general curve or zero projection cause invalid state: the last drawing/value
remain red and selectable for repair. Another coincident/nearby edge cannot replace
the reference without explicit user input. Repair recalculates and restores yellow.

Parallel references retain angular type and value 0° or 180°; degrees are never
automatically converted to millimetres. Local leaders to actual reference points
replace an undefined or excessively distant vertex. The same applies to nearly
parallel lines with an impractically remote intersection, keeping the dimension
near the model. The last drawing mode is saved with last presentation in `.drwz`,
so this limiting case remains selectable after subsequent reference loss.
Opening Properties and selection do not use OCCT.

Verified by GUI/CLI builds, five targeted tests and full regression 80/80
(2026-09-12). UI tests use real mouse events for creation, Cancel, MMB double-click,
reference loss and repair. They check yellow/red pixels, last value without a
question mark, invalid-dimension selection and `.drwz` save/reopen. Independent
checks cover 60°/120°, trimming, 0°/180° and nearly parallel lines. Screenshots of
valid, invalid and repaired states passed visual inspection.


## Original-edge preference (2026-09-25)

Drawing dimensions and direct sheet symbols share the measurement candidate
pipeline. A selected result edge prefers its uniquely recoverable original edge
when the persisted identity proves unchanged geometric ancestry. Boolean
`split-edge:from` identities encode that parent with length-prefixed owner, key
and occurrence fields; nested splits are followed without OCCT or proximity
searches. The contact parameter is recalculated on the parent, and the resolved
contact must remain unchanged before accepting the replacement. Occurrences
must match exactly. Picking continues to use the displayed fragment, not removed
portions of its original curve. Reference highlighting can follow displayed
split descendants of the stored original edge.

If the parent is absent or the contact cannot be preserved, keep the selected
body-edge reference. Intersection and edge-treatment boundaries retain their
own identities: face ancestry or a fillet's generating edge is not proof that
the resulting edge is the same curve. Mirror/copy ancestry must not move a
contact back onto its source object. Persisted point references remain points.

The policy applies at selection time. It does not reconstruct a previously lost
reference whose source data is no longer available, nor silently repair old
annotations by geometric proximity. No document schema or UI text changed.

Regression coverage in `measurement_dimension_contract_tests.cpp` and
`symbol_drawing_tests.cpp` covers reversed split direction, preserved contact,
nested ancestry, missing parents, distinct occurrences, intersections, dimension
serialization and restoring the unsplit source geometry.


The same preference applies to persisted point references. Boolean
`vertex:from` ancestry may resolve to the original point only in the same
occurrence and at the same 3D location. Matching projected coordinates alone is
insufficient. Original point records take priority over duplicate result records.
New intersection vertices without a unique inherited point retain body identity.
Direct Drawing symbols accept these Point contacts as well as curve contacts,
persist their kind, and retain their last position if that point disappears.
