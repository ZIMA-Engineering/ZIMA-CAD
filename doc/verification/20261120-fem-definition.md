# FEM native definition and transaction checkpoint

Status: initial native foundation, not a completed FEM command or release.
The user approved the persistence scope in [FEM_DESIGN.md](../FEM_DESIGN.md) on
2026-11-20. Baseline: `66e57cc6f`. Windows verification uses MSVC 19.51.36248,
Qt 6.11.0 and the existing patched OCCT 8.0.1 SDK in a separate
`build/cpp-windows-fem` build. The published/local 2026112004 application is
unchanged. No solver or mesher dependency was added to that release.

## Implemented scope

A typed FEM Body stores its preceding material Body/Boolean source identity,
analysis kind and volume mesh controls. It shares the existing calculated source
through `BodySnapshot`, contributes no material operation and is excluded from
Boolean, Scale and Mirror operands. The source can be a consumed Body or the
Boolean result. Activation presents that source once without altering its saved
visibility; suppressed and missing sources remain unavailable.

Native material Body rows are unchanged. Only the new FEM row has `kind: "fem"`
and a required `definition`. Source loss keeps its identity for explicit repair.
The Part format discriminators and extensions are unchanged; no legacy reader,
required sidecar or duplicate persisted CAD source was introduced. Conditions,
mesh/result buffers and their final persistence contracts remain pending.

The workspace uses metadata transactions. Preparing or cancelling a draft,
reading source candidates and inspecting the source do not calculate CAD.
Unchanged OK creates no revision or data generation. Changed controls use one
Undo transaction sharing the calculated packet. Stale drafts and invalid source
replacement fail without publishing changes. No shared placement solver or
Sketcher algorithm was changed.

## Native verification

All three executables pass against the final checkpoint build:

| Executable | Finite coverage |
| --- | --- |
| `zima_cpp_fem_document_tests` | Type/identity, Body and Boolean sources, unchanged material operations/schema, activation and visibility, source suppression/loss/repair, missing-source native reopen, forward/cyclic source rejection, invalid reorder/operand/history/placement/type edits, tetrahedral order and size validation, malformed numeric JSON, `.prtz` save/reopen, exact source sharing, Undo/Redo and 1,000 mm³ source volume after regeneration |
| `zima_cpp_fem_operations_tests` | Real nested-solid union and consumed source selection, empty/missing/suppressed calculated source rejection, draft/Cancel/no-op behavior, stale draft rejection, changed controls, immutable packet sharing, Undo/Redo and zero CAD calculation-count increments during FEM interaction/commit |
| `zima_cpp_body_scale_tests` | Existing Scale/Mirror ancestry, geometry and properties, suppression, family presence, unchanged confirmation, Undo/Redo and native Part/Assembly persistence |

These tests exercise source ownership and metadata, not FEM solution accuracy.
They do not certify an interactive FEM GUI, remeshing, force integration or
condition Sketch manipulation.

## Templates and protected files

The changed native implementation recalculated, saved and reopened copies of
the four Part/Skeleton start templates (mm/in), and saved/reopened copies of
both Assembly start templates. All six outputs are byte-identical to the
committed originals; no factory asset requires a data conversion for this
definition-only alternative. Later FEM persistence additions must reassess the
affected asset set rather than assuming this result covers them.

Hash verification confirms all 1,185 inventoried factory native assets and all
eleven protected Toyota originals remain unchanged. All eleven Toyota backup
copies match their originals. Previously unsupported Toyota formats remain a
native reopening gap; hash equality is not evidence of successful loading.
No original project file was overwritten.

## Localization and outstanding gates

The ten new validation/error strings have Czech, English, German, French and
Russian translations. Catalog key/placeholder checks pass for 4,658 Qt keys,
938 named keys and 4,284 existing production translation calls. No FEM UI is
exposed yet; GUI language/theme switching is not claimed by this checkpoint.

Remaining work includes the shared FEM Properties UI and command filtering,
condition authoring/regions and their placement/Sketcher matrices, actual ZIMA
topology bindings in Gmsh, bounded native mesh/result storage, invalidation and
job publication, numerical solver validation, results/legend/animation, template
creation through the changed GUI, full affected factory acceptance and Linux
verification. These gates remain required before publishing FEM as usable.
