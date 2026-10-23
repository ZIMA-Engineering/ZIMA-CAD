# Assembly cutter presentation wire

Status: the finite Windows native, GPU, main-window and start-template gates
below passed. Broader occurrence/View-hover variants and release gates remain
explicitly outstanding.
The user approved the native persistence scope on 2026-10-23 in
`../ASSEMBLY_FEATURE_PREVIEW_STORAGE_PLAN.md`.

## Cause and implementation

Component subtraction intentionally retains component-owned reference geometry.
The cutter's original wire therefore cannot be recovered from the component's
original-reference packet. The previous Properties lookup returned no wire.

Each Assembly cut now owns `preview_wire`, populated from the actual calculated
cutter before it is discarded. It stores only that owner's non-seam edges in the
Assembly frame. It does not duplicate a native BRep or inject cutter edges into
component reference topology. The existing compact viewer-reference serializer
stores exact curve data and validates finite coordinates. The Assembly reader
additionally rejects foreign owners and non-wire payloads. The current Assembly
versions are outer INI 37 and inner native JSON 49; no legacy reader is added.

The ordinary selection provider and unchanged exact Properties preview consume
the same stored wire. A provider explicitly owning the presentation may return
an empty wire without falling back to unrelated component result edges. GPU
source caching remains responsible for uploads and camera/repaint reuse.

Explicit cut regeneration replaces the wire; suppression and non-solid features
clear it. Semantic equality continues to compare authored definitions and targets,
not derived calculation packets. Opening, selection and unchanged OK do not
calculate a new cutter or create a transaction.

## Factory inventory

`build/assembly-preview-storage/factory-impact-audit.json` records all tracked
Assembly files and a scan of tracked Drawing paths. There are two factory
Assembly start templates, both without components or cuts. There are no authored
factory Assembly cutters and no tracked Drawing contains an Assembly source path.
The historical `tests/fixtures/cross_language/nested.asmz` has outer version 22
and was already unsupported before this change; it is not a current factory
model or evidence of current reader compatibility.

Both factory Assembly templates must be recreated by the changed native code,
cold-reopened and checked through actual new-document GUI creation in millimetres
and inches. Preserve user projects, settings and custom libraries.

## Recorded Windows acceptance

- Native workspace suite passed 89 checks, including eight selected Through-all
  input/tool checks. The previously failing accepted-wire minimum now agrees
  with the exact private draft. Cold reopen, required-packet/foreign-owner
  rejection and Undo/Redo passed. The mixed three-target tool adds 124,738 bytes
  for 30 edges, without an additional BRep.
- Assembly contract, cut history and profile command suites passed in 0.89 s,
  0.68 s and 2.77 s. Revolution checks include independent profile-envelope
  equations, full/half-turn volume, repeated targets, angle edits, wire changes,
  native reopen and Undo/Redo.
- Main-window Assembly selection, inspection and Properties passed in 4.185 s.
  A real Tree click uses the cut's own wire. Opening/unchanged OK calculated no
  preview; opening published one rollback scene. Three rapid correction edits
  calculated one draft and published no additional base scene. Cancel retained
  the revision. User files and settings hashes remained unchanged.
- GPU regression passed in 5.55 s, including the existing occluded selection,
  camera/repaint, curved hover and 29,000-edge import cases. The new dedicated
  Assembly provider is read once across repaints. An empty inspection clears
  the previous GPU batch and does not re-read the provider or expose foreign
  component edges. The initial failing empty-inspection case is retained in
  `build/assembly-preview-storage/regression.log`; the corrected result is in
  `gpu-final.log`.
- Five-language catalog/source validation passed in 15.18 s; Feature controls
  passed all five languages and both themes in 17.98 s. Relinked Sketch reference,
  Sketch intersection, face/plane and ordered-limit suites passed in 0.83 s,
  16.76 s, 1.41 s and 14.58 s.
- Both factory Assembly templates were authored, saved and cold-reopened by the
  changed native implementation. All authored fields, units, Relations and
  labels were compared with the prior templates. Sizes remain 4,194 bytes (mm)
  and 4,286 bytes (in). Actual new-document GUI creation, editable contexts,
  normal commands and insertion passed in all five languages in 44.723 s (mm)
  and 46.831 s (in), using isolated settings and library copies.

## Scope of the recorded matrix

- Native cut geometry, separate Through-all input bounds, multi-target correction,
  accepted/draft bounds, source placement, save/cold reopen and Undo/Redo.
- Missing required packet and foreign-owner rejection without an adapter.
- Main-window ordinary selection and inspection, unchanged Properties/OK with
  zero preview calculations, one rollback scene, three rapid correction edits
  coalescing into one calculation, no base-scene publication on correction,
  and Cancel preserving the revision.
- GPU-only source wire without component original edges, one source read over
  repeated repaints, and no foreign-edge fallback for unavailable wire.
- Five-language validation, template GUI creation, file-size overhead and
  relevant native reference suites after relinking the shared kernel.

The recorded GUI case covers the Tree's ordinary cut selection and explicit
inspection, not a change to ordinary Assembly component picking in the View.
Nested/repeated Assembly cut View hover, changed GUI OK and the wider occurrence
matrix still need separate evidence. The approved curve/plane associative point
work has its own record in
[curve-plane points and rounded mirror](20261023-curve-plane-points-and-rounded-mirror.md).
Native Linux execution and release-package acceptance are separate gates.
These finite passes do not establish those broader variants.
