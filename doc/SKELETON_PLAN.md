# Agreed Skeleton workflow

Status: design agreed on 2026-09-21. Only the Skeleton appearance preset is
implemented in Windows build 2026092101. The modeling workflow below is pending.

## Inputs, means and outputs

The input is an ordinary native Part whose filename ends in `_skeleton.prtz`
(case-insensitive), for example `ZE026-0100-0000_skeleton.prtz`. Existing Part
geometry and the ordinary component placement contract provide the means. The
output is a single reference-only Skeleton occurrence directly owned by an
Assembly, displayed without entering its body-operation history or mass totals.

## Agreed behavior

- Each `.asmz` may directly own at most one Skeleton. A nested Assembly may own
  its own Skeleton independently. A missing source still occupies this slot.
- The Assembly context menu offers **Insert Skeleton**. New Part will gain a
  Skeleton choice, using the standard Part editor and the filename suffix.
- The Tree displays the full filename immediately after the Assembly Origin.
  Its insertion uses the same Origin placement contract as ordinary components.
- Activate, edit, Properties, remove and show/hide follow ordinary occurrence
  ownership. Removing an occurrence does not delete its source file.
- Skeleton Properties retain other components in the Tree and View. There is
  no history rollback, Assembly solid operation, mass or bill-of-materials
  contribution from the Skeleton occurrence.
- Skeleton reference geometry may include solids. Dependencies run from the
  Skeleton to consuming Parts, and direct or indirect cycles are rejected.
- Externally renaming a source does not silently relink it. A missing source
  remains in the Tree with an error and offers explicit source-file recovery.
- The existing Drawing icon is intended for Skeleton; Drawing needs a distinct
  sheet/frame/title-block icon for review before this workflow is completed.
- The basic Skeleton appearance is muted purple, initially 70% transparent.
  It remains editable through the common appearance controls.

## Verification required before implementation is complete

Verify single-Skeleton enforcement, nested ownership, source rename/missing-file
recovery, placement creation/edit/Cancel, stable references and cycle rejection,
visibility, mass/BOM exclusion, save/reopen and Undo/Redo. Verify that opening
Skeleton Properties neither regenerates bodies nor hides unrelated components.
