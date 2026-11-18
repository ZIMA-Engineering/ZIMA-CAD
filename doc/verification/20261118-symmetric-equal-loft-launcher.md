# Symmetric Equal Length, Loft planes and configured project directory

Windows verification on 2026-11-18 uses the shared C++ implementation, Qt
6.11.0 and OCCT 8.0.1. Native formats and the shared container placement solver
and reference-entry contract are unchanged. Linux execution remains unverified
on this Windows host.

## Reproduction and changes

The preserved copy of the user's `15.prtz` is 25,775 bytes, SHA-256
`6f97faf037dc2a4773ef6d19a018f2d6e3c4826ea2bf14b2bd98e44ccf840afa`.
Adding Equal Length to its adjacent rectangle sides originally failed with
`Segment-pair constraint conflicts with existing geometry`. The rectangle
already has horizontal/vertical relations and symmetry about both Sketch axes.
Its connected component was excluded from the existing simultaneous equation
seed because that gate required PointOnLine. Symmetric components now reuse
the same seed; the existing residual validation and atomic rejection remain.
The exact copied document accepts equality after the fix with zero residual
and one remaining size degree of freedom. The user file was not rewritten.

Loft Properties now starts the existing automatic placement-entry sequence at
the first empty field. Its owned Sketch editor uses the ordinary Plane display
carrier with the already resolved Sketch frame. This makes the chosen work
plane visible without introducing another placement solver, OCCT calculation,
persistent construction or coordinate convention. The existing manual plane
selection and native Loft calculation needed no change.

The development BAT previously forced the repository through the application's
`--working-directory` option, overriding the saved project directory. It now
sets only the process directory using `start /D`; explicit caller arguments are
preserved. Personal settings and project files are not changed.

## Checked matrix

| Area | Cases and independent checks | Result |
| --- | --- | --- |
| Symmetric Equal Length, native | 16 combinations: four sign orientations, both Equal selection orders, default axes or native construction axes anchored to immutable external points | Pass |
| Dimensions | Segment length, projected X/Y point distance, diagonal point distance; reference, driving unlocked and driving locked; three successive value changes, 576 edits | Pass |
| Manipulation | All four free vertices and repeated dimension-state drags, 768 actions; movement when permitted, independent equality/H/V/reflection/dimension equations, external source immutability and serialization | Pass |
| Locked/redundant input | Locked fully constrained drag, redundant opposite-side equality and impossible unequal locked sizes rejected without document mutation | Pass |
| Dependents | Corner radius retained through three drags; opposite-side trim followed by three drags and reopen; shared point and remaining equality retained | Pass |
| Existing contact-based Equal Length | Existing native/external support matrix: 576 dimension edits and 2,016 drags, plus fully constrained three-rectangle cases | Pass |
| Equal Length GUI | Eight adjacent-side/order combinations; actual mouse picking, equality marker, three point drags, dimension creation and three double-click value edits; independent displayed equations, native save/reopen and Undo/Redo | Pass |
| Manual Loft planes, native | 12 actual calculations: XY/XZ/YZ, positive/negative offsets, translated parent and 0/90-degree parent rotation; resolved origins/normals, attachment identities, cylinder volume/centroid, native reopen and Undo/Redo | Pass |
| Loft GUI | Automatic progression through three references without clicking fields; default XY; XY/XZ/YZ/XY button sequence, matching preview border and Sketcher camera normal, parent attachment and return from Sketcher | Pass |
| Existing Loft GUI | Five languages, owned Sketch add/edit/remove, independent eye/order, whole-Origin entry, numeric corrections, resize, rollback/Cancel, unchanged OK, middle confirmation, ordinary View/Tree selection, solid/Thin calculation and persistence | Pass |
| Development launcher | Actual BAT startup reports configured ZE-CAD project directory; explicit override with spaces reports the supplied directory | Pass |
| Localization | No new/changed production UI text; five-language Loft GUI and translation coverage/catalog contract | Pass |

The selected six native/component CTest targets passed in 20.71 seconds:
`zima_cpp_equal_length_component_tests`, `zima_cpp_sketcher_contract_tests`,
`zima_cpp_sketch_relation_command_tests`, `zima_cpp_loft_tests`,
`zima_cpp_loft_document_tests`, and `zima_cpp_translations_contract`.
The Equal Length and Loft GUI contracts also passed in isolated project
directories with the Windows Qt platform.

Local evidence is retained under
`build/test-models/equal-segments-20261118/`: `native.log`, `native.xml`,
`exact-cli.log`, `gui-equal.log`, `gui-loft.log`, and
`launcher-probe/results.json`. GUI source tests are committed; temporary
models, personal configuration and screenshots are not release inputs.

## Limits

This is a finite regression matrix. Direct Symmetric-to-external-edge input is
not a supported existing contract; the external-source variant instead anchors
native symmetry supports to immutable external points. Oblique symmetry axes,
every possible angle/coordinate dimension combination, every trim variant and
all model topologies were not exhausted. Native equation tests cover more
dimension/lock variants than the mouse-interaction suite. The older broad
axis-endpoint and console linked-segment failures recorded for Windows 1811
are outside these fixes; this record does not claim they are repaired.

Public release acceptance and packaged-runtime checks must be recorded
separately after packaging. No Linux execution or live updater activation is
claimed by these local results.
