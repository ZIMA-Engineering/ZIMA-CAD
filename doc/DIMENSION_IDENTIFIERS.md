# Document-wide dimension identifiers

Every dimension receives an immutable `d1`, `d2`, ... designation alongside its
existing internal ID. Each Part, Assembly, or Drawing owns one sequence. Drawings
share it across all sheets. Inserted Parts retain their own sequences; placement
and mate dimensions belong to the immediate owning Assembly's sequence.

Numbers are allocated when a document change is committed. Zero values,
suppression, invisibility, and inactive optional dimensions do not prevent
allocation. The catalog includes Sketch dimensions, corner radii, feature size
parameters, placement, mate offsets and specified limits, 3D-curve points/radii,
embedded Sweep Sketches, and Drawing dimensions. Recognition is independent of
numeric value and View rendering.

Changing an object's value, name, or order does not change its identifiers.
Allocated numbers remain reserved after deletion and through Undo/Redo, including
new history branches. Cancel does not register pending dimensions.

## Data contract

`DimensionIdentifiers` is a separate document metadata registry keyed by the
existing dimension owner/semantic-key pair. It stores allocated numbers and the
next free number within the document. Loading rejects duplicate numbers, empty
identities, and invalid sequences.

Placement size parameters retain their existing parameter slots. An Assembly
mate uses `(assembly_id, placement-reference:occurrence_id:row_index)`; replacing
the reference or value in that slot retains its designation. The registry creates
no second mate object, solves no placement, and changes neither mate ownership
nor calculation.

Numbering creates no geometry and calls no OCCT. The catalog contains identities
only; its numbers are not topology identities or inputs to geometric calculation.

## Interface

- Sketch dimension Properties has a read-only **Dimension identifier** field.
- **Relations** lists supported designations, objects, parameters and their
  current values, including zero dimensions. Double-click inserts an identifier
  into the text editor; the selection arrow can insert it from the View.
- The inline View value editor includes the designation in its tooltip.
- Selecting a Drawing dimension shows its designation in the status bar.
- [Relation expressions](RELATIONS.md) read and drive supported `dN` values on
  explicit Regenerate. The working View marks assigned dimensions with `fx`.
- Pattern spacing, angle and count use the Pattern's own identity, including
  derived Bodies and Assembly groups. Selecting a copy does not redirect its
  dimension display to the source Part.

INI versions at this stage: Part 14, Assembly 12, Drawing 12. Older formats have
no compatibility loading branch. Supplied templates and test documents use this schema.
