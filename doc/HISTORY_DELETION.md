# Deleting a Part history source

Deleting a Part history object opens the shared internal OK/Cancel dialog.
Dependent history objects are marked red before confirmation. Cancel restores
the original tree and does not modify the document. OK removes the selected
source and its owned objects, retaining downstream history containers, their
identities, names and numeric parameters.

References to the removed source and the broken dependent chain are detached.
Unrelated references remain. Retained objects require replacement references
in Properties; Fillet and Chamfer may consequently have no selected edge routes.
They remain red and do not generate substitute geometry. The existing history
recovery calculation preserves valid input geometry and independent Bodies.
Deletion is committed as one document transaction, including detached references
and calculation diagnostics, and can be undone or redone.

The native Part document stores `removed_reference_states`, keyed by the
retained history owner. Each value records the surviving reference definition.
Regeneration, a rename or a numeric parameter edit does not acknowledge repair.
Changing the reference definition retires this state; ordinary calculation and
reference validation then decide whether the edited feature is valid. There
are no required sidecar files.

This change concerns Part history source deletion. Assembly occurrence deletion
and multiple-selection confirmation retain their existing workflows. Body
Boolean dependency restrictions are not replaced by this feature.

## Verification

Focused core tests pass for a calculated Extrusion–Fillet–Chamfer chain,
retained edge treatments, native persistence, regeneration, repair using another
input edge, an independent Body and a Pattern whose source has been deleted.
The GUI test passes confirmation preview, Cancel, OK, save, Undo/Redo and middle
button confirmation over the View. The confirmation screenshot was inspected.
Five-language catalogue and translation coverage checks pass. These focused
results do not establish that the full application regression suite passes;
the broader limitations recorded in [Relations](RELATIONS.md) remain relevant.
