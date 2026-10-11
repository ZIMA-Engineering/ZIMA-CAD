# Linked surfaces from a preceding Body

The zero-thickness **Surfaces from Body** command can extract an exact skin
into a different active, editable Body. Select a face of the preceding ordinary
source Body through the Source Body field, then optionally select faces to omit.
The source solid remains unchanged. Leaving the field at Active Body retains
the established same-Body conversion. A thick Shell continues to operate on its
own history input.

The source must precede the target and remain available at that boundary.
Self, forward, consumed, missing and derived/link/scale sources are rejected.
The native Shell definition stores the source Body ID; ordinary dependency
resolution prevents reversing their order. Extraction is in place: calculation
converts the source's world geometry into the target's local coordinates. Changes
to the source geometry or either Body frame invalidate the dependent result.
A suppressed/failed source retains the dependent feature with a calculation
error. No reference geometry is reconstructed during selection or inspection.

The exact source topology becomes the parent of distinct Shell-owned surface,
edge and vertex identities. Calculated native bindings of the retained solid
and extracted surface are persisted inside the Part; embedding the definition
into FORM therefore needs no live source document or external shape file.
Part envelope/schema versions remain 46/70; the current Shell entity gains the
optional `source_body` field, and the source Part carries the optional
`form_outward` authoring intent. Their empty/false defaults preserve ordinary
current definitions without an alternate version execution path. Start Part
templates were rewritten with the current serializer and reopened.

Creation and Properties use one shared dialog. Source entry, replacement, clear
and independent inspection use the common reference-cell controls. Exactly one
field owns input. Clear restores Active Body input and re-arms source selection;
it also removes pending omitted-face references and inspection. Selecting a new
source discards omitted faces from the previous source. Short MMB finishes entry
and inspection; ordinary shared double-MMB confirmation remains applicable.
Cancel restores the complete scene and unchanged history. Unchanged OK neither
calculates nor adds an Undo transaction.

The workspace publishes its ordinary input/rollback scene. Source/face entry
and inspection read cached viewer/native data and update the common candidate
filter and overlays. Changed OK owns the explicit calculation and commit;
dialog destruction restores the normal full scene. No general container
placement contract is changed.

Windows native checks cover retained source volume, omitted-face area, unique
native ancestry, source dimension changes, both translated/rotated Body frames,
self/missing references, dependent Body reordering, suppressed source recovery,
unchanged OK, Undo/Redo and save/reopen/regeneration. The GUI contract covers
an empty later Body, real hover/click source and face selection, inspection,
replacement/removal, resizing, Cancel, unchanged OK and Undo/Redo. All five
language catalogs and the affected rendered controls are validated. The
existing Shell, planar/curved/spline/connected skin thickness, native FORM
definition and new-document template checks are also exercised.

The surface-thickness regression reader now uses the existing calculated-body
reader for compound snapshots; its previous direct BRep read rejected the
document aggregate rather than checking its contained solid. Strict geometric
validity and solid-count checks remain unchanged. Native Linux execution and
manual review of automatically rounded new corner variants are separate gaps.

See [the FORM investigation](performance/20261011-corner-outward-surface-experiment.md)
and [Linux handoff](LINUX_RELEASE_HANDOFF.md).
