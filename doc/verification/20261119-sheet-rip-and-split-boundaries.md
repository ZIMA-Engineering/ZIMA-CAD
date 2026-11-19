# Sheet RIP selection and split source boundaries

## Report and root causes

The saved private `25.prtz` has two successive Fillets on a symmetric Extrusion.
Sheet from Body with no RIP selected produced a vertical Bend over only half
the height, leaving a long opening between its adjoining Flats. The join on
the source cylinder consists of two contiguous straight edges, split at the
Extrusion Sketch plane. Surface traversal visits each cylinder once and
previously used only the first encountered edge as its attachment extent.

Conversion now derives the complete connected, collinear boundary shared by
the same two actual surface fragments. It uses that transient span for the
generated attachment and Flat outline. Disconnected edges, different supports
and parameter seams are excluded. Source topology is unchanged: association
bindings still persist all original edge identities and individual endpoints.
This is a shared C++ change without a native-format or placement-solver change.

RIP selection had no Sheet-from-Body Tree dispatch. The View could select an
ordinary standalone Sketch, but clicking its Tree row did nothing. The command
now accepts Tree containers and exact Sketch rows through its existing reference
callback. An owned Sketch retains its own ID; its parent feature is not used
as an ambiguous substitute. Eligibility still requires visible, unsuppressed
ordinary geometry in the selected source Body before the conversion boundary.
Native RIP calculation already supports these owned Sketches.

## Finite verification

- Native sheet-exchange regression covers one-sided, symmetric and unequal
  two-sided Extrusions with a vertical Fillet crossed by two cap Fillets.
  Each creates exactly six Flats and five Bends. Independent endpoint equations
  require the single vertical Bend to cover the complete interval between cap
  tangencies; the assertion is repeated after native serialization, cold
  regeneration and successive +7/-7 mm source length edits with stable child IDs.
- All twelve mandatory `sheet-branch` gates passed locally on Windows in
  188.45 seconds, including actual parallel-branch/sequential equivalence,
  contact, overlap, containment, dependent attachment, state and GUI checks.
- Actual GUI mouse-event tests cover View and Tree RIP entry, removal, Cancel,
  confirmation, saved Sketch identity, and Undo/Redo in both same-Body and
  separate-target-Body conversion. Both final suites pass in 16.72 seconds,
  including exact owned-Sketch selection, rejection of its ambiguous parent
  feature and unchanged document/scene revisions during selection.
- The translation/catalog/UI contract passes in 62.41 seconds. Packaging and
  publisher unit tests pass: 32 executed, with two Linux-only skips on Windows.
- On the isolated `25.prtz` copy, conversion with no RIP still creates eleven
  children with zero skipped regions and zero calculation errors. The vertical
  attachment now runs from approximately z = -68.4545 to +68.4545 mm instead
  of stopping at z = 0. Saved native reopening and cold regeneration pass;
  a native viewer capture confirms removal of the long half-height opening.
  The ordinary corner relief at the crossing of three Bends remains visible.

The user's original file is unchanged. The corrected diagnostic copy was
created by repeating conversion from its original solid prefix; this does not
claim that an unchanged Regenerate rewrites an already-authored short Bend.

Localization review: no new product labels, messages or errors were added;
the existing five-language reference controls are reused. Test diagnostics and
this documentation are English. The native Windows tests use Qt 6.11.0 and
OCCT 8.0.1. Linux execution remains unverified on its separate host. The finite
matrix does not assert every arbitrary split topology or every RIP curve shape.
