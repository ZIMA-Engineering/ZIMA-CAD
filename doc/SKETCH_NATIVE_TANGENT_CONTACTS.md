# Native circular tangent contacts

Additional centre/end-to-axis and Origin editing cases are documented in
[Native arc centre and endpoint contacts](SKETCH_ARC_AXIS_CONTACTS.md).

Separate native circular curves use an actual persisted contact point. Each
curve that does not own that point receives an independent C relation; T
constrains their tangent direction and retains the internal/external contact
side. Removing T keeps the point and both independent incidence relations.
Manual circle-pair T and circle creation against a circle or finite arc use
the same native insertion path. The creation preview displays C+T.

Two native arcs sharing the same endpoint identity already own its incidence.
They receive only T at that point: no duplicate point and no redundant C.
The two arcs in `FORM_FLAT` from the user's `FORM-KOULE.prtz` are retained as a
read-only regression fixture. Their original radii are preserved when T is
inserted and a solution at those radii exists.

The bounded simultaneous circular equation seed fits contact, centers, radii
and dimension equations together. The ordinary solver verifies the complete
result before committing. Temporary creation anchors preserve the selected
reference position and never enter the persisted rank cache. A blocked driven
curve remains an atomic creation rejection under the established reference /
driven contract. Subsequent contact dragging respects immutable native supports
independently of the original input order.

Native arc endpoint incidence participates in the numerical DOF calculation as
an intrinsic equation of the arc definition. It does not create user-visible C
constraints. This removes four spurious degrees of freedom in the completely
dimensioned two-arc fixture. Fully fixed and fully dimensioned graphs cannot
escape their constraints during dragging.

## Finite regression matrix

`zima_cpp_native_tangent_contact_tests` covers 108 cases. Equations are checked
independently before another solve can normalize the geometry.

| Area | Variants and actions |
| --- | --- |
| Separate native circles | Internal/external contact; both input orders; orientations 0, 0.7, 1.8 and 3.5 radians; real point and two independent C relations; three radius changes; contact dragging; persisted side; removing T retains C |
| Immutable native support | Both sides and orders; fixed support center and locked radius after contact creation; three successive contact slides; source center/radius unchanged |
| Shared native arcs | Exact FORM-KOULE point/constraint fixture, both orders; T-only insertion; unchanged entered radii; stable existing point/constraint identities |
| Arc size dimensions | Radius and diameter on either arc; driving/reference; locked/unlocked; three successive value edits and center drags; locked reference insertion rejected atomically |
| Contact position | X, Y and distance to Origin; driving/reference; locked/unlocked; three value edits and independent measurement; invalid locked reference rejected |
| Arc angle | Three-point angle on either arc; driving/reference; locked/unlocked; three successive value edits and independent angle measurement; negative and wrapped-angle drivers retain their geometric magnitude |
| Point manipulation | Shared endpoint and every arc center/start/end; three successive permitted drags; native endpoint radius, T and existing axis C/T equations checked |
| Fully constrained | All points fixed, and a separate fixture fixed by center plus locked radii/angle; zero DOF; every attempted point drag leaves exact serialization unchanged |
| Persistence | Native Part save/reopen, Sketch round-trip, T removal Undo/Redo and exact reference identity |

The existing general Sketcher suite additionally covers line/circle/arc,
ellipse/elliptical-arc and external tangent cases, trim, serialization and
common-tangent dimensions. Its FORM Sweep matrix includes 128 transformed
native/external R/D and angle variants and 1920 successive edits/drags. The
external point/native-circle study covers another 64 R/D, side, order,
orientation and lock combinations. The C+K, command-level relation, dimension,
curve, reference, serialization and Mirror/tangent suites are dependent checks.

The focused GUI contract covers six fixtures: manual circle T in both orders,
the actual shared arcs in both orders with R/D dimensions, and creating a new
circle against a circle or finite arc. It uses the common hover/cycling/click
list, persisted point/C/T checks, real dimension double-click and three value
edits with Undo/Redo, three mouse drags per relevant point, native save and
Sketch reopen. Finite-arc end drags stay within the valid contact domain;
moving an end past contact is not a valid editable tangent configuration.

These finite matrices do not establish exhaustive GUI coverage of every
dimension sector, symmetric/line-distance driver, imported curve packet,
ellipse-pair contact, trim fragment or downstream modeling operation. This
change does not introduce new analytic ellipse-pair contact creation. Native
Linux execution remains pending in [the handoff](LINUX_RELEASE_HANDOFF.md).
No localized UI text is added: C+T uses the existing universal constraint
symbols and shared palette-aware overlay renderer. Preview screenshots were
inspected in both themes after live Light/Dark/Light switching. The Czech,
English, German, French and Russian catalogs pass the final validation batch.
