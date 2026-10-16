# FORM instance dimension editing — 2026-10-16

Driving dimensions displayed by double-clicking a FORM can be edited through the ordinary inline value editor. Each edit calculates a private copy of the insertion's embedded native definition and its downstream operations before committing one transaction. The factory definition, placement, source identities and other insertions remain unchanged. Reference dimensions remain read-only. An unchanged value does not calculate or commit. Numeric editing follows ordinary Sketch locking semantics.

## Windows verification

The native finite matrix exercised Dimpled Hole (4 controls), Bridge Lance (3), Ventilation Window (7 accepted, 3 rejected) and corner FORM (3). Successive edits, independent geometric equations, unchanged values, synthetic reference-only rejection, a separately calculated peer insertion, Undo/Redo and save/reopen passed. Three Window edits invalidated existing downstream 2D Sweep references and were rejected atomically; this change does not repair those source-profile topology dependencies.

Both attachment sides and three thicknesses passed the Dimpled Hole and Bridge Lance matrices, including dimension editing after Unbend/Bend Back. Eight dependent native regression suites passed. Actual GUI value edits, Tree inspection and middle-button dimension dismissal passed for all four FORM examples; actual dimension-label double-clicks passed for the three planar definitions. Five localization catalogs contain matching 2491 nonempty keys and passed validation.

This is finite coverage of the authored factory controls, not exhaustive Sketcher coverage. Arbitrary Sketch topologies, every driving/reference and fully constrained manipulation variant, and Linux execution remain unverified in this change. The earlier Sketcher verification reports retain their separately recorded scope.
