# Agreed Skeleton workflow

Status: implemented and verified locally on 2026-09-21. Published
Windows build 2026092101 includes only the appearance preset, not this workflow.

## Inputs, means and outputs

The input is an ordinary native Part whose filename ends in `_skeleton.prtz`
(case-insensitive), for example `ZE026-0100-0000_skeleton.prtz`. Existing Part
geometry and the ordinary component placement contract provide the means. The
output is a single reference-only Skeleton occurrence directly owned by an
Assembly, displayed without entering its body-operation history or mass totals.

## Agreed behavior

- Each `.asmz` may directly own at most one Skeleton. A nested Assembly may own
  its own Skeleton independently. A missing source still occupies this slot.
- The Assembly context menu offers **Insert Skeleton**. New Part includes a
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
- Skeleton uses the former wire-cube Drawing icon. Drawing uses a distinct
  sheet/frame/title-block icon.
- The basic Skeleton appearance is muted purple, initially 70% transparent.
  It remains editable through the common appearance controls.

## Verification required before implementation is complete

Verify single-Skeleton enforcement, nested ownership, source rename/missing-file
recovery, placement creation/edit/Cancel, stable references and cycle rejection,
visibility, mass/BOM exclusion, save/reopen and Undo/Redo. Verify that opening
Skeleton Properties neither regenerates bodies nor hides unrelated components.

## File identity and properties

Ordinary Part and subassembly Tree rows show the actual source filename,
including `.prtz` or `.asmz`. Native occurrence snapshots carry that filename as
their display name; Assembly-owned Mirror and Pattern entries retain their own
labels. Stable document and occurrence IDs continue to identify references.

Component Properties show an icon and a selectable, read-only source filename.
Position and Rotation are adjacent columns. Rename remains a separate native
file transaction: it relocates the source and its owned Drawing companion,
updates open and saved dependencies and nested display names, and preserves
open document history. Renaming a Skeleton keeps `_skeleton.prtz`. A rename
that would create two direct Skeletons in an Assembly is rejected.

Missing sources stay red, including hidden occurrences. Properties offer the
existing explicit source-file recovery flow; externally renaming a file does
not automatically repair references. Recovery requires the original document
identity. The file format is unchanged; no sidecar metadata is introduced.

Skeletons use the ordinary component placement and editing workflow. Their
geometry remains available as reference geometry in the Assembly scene, while
Assembly mass, volume, area, BOM and physical compound calculations omit it.
Assembly cuts do not offer Skeleton targets, and explicit body operations reject
them. Consumer Parts may reference Skeleton geometry; Skeleton geometry cannot
introduce a dependency on another document. Ordinary document-cycle checks also
remain in force.

## Local verification (2026-09-21)

The Windows application and affected test executables build successfully. Ten
focused contracts pass: Assembly, Workspace, component commands, native file
rename, drawing balloons/BOM, shared UI, dialog layout, translations, component
Properties GUI, and New Document GUI.

New Document verification creates and saves Skeleton Parts, inserts them using
Insert Skeleton, checks the read-only filename header and Tree ordering, rejects
a second insertion in the menu, and reopens the saved Assembly in Czech, English,
German, French and Russian. The initial GUI test exposed the Sections folder
being inserted ahead of the Skeleton; its insertion position now preserves the
Origin/Skeleton ordering and the five-language test passes.

Core checks cover case-insensitive filename recognition, full snapshot filenames,
physical totals and nested compound exclusion, missing-source slot reservation,
serialization, visibility Undo/Redo, empty reference Part insertion, nested
Assembly ownership and reverse-dependency rejection. Rename verification also
checks Skeleton suffix preservation and relocation of its Drawing companion and
open Assembly/Drawing references. Existing component placement GUI checks pass
with the revised source header and adjacent coordinate columns.

This local change has not replaced the immutable 2026092101 release archive.
