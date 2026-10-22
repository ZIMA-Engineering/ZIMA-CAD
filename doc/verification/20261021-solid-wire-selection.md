# Solid selection: wire ownership and occlusion

This result-edge experiment is historical. The user reported remaining unrelated
edges and requested the simpler authored-preview policy on 2026-10-22. The
split-edge ownership decoder described below has been removed; current behavior
is recorded in [Authored preview wire selection](20261022-authored-preview-selection.md).

## User contract

The user clarified on 2026-10-21 that ordinary solid/feature hover and selection
must never fill or recolor solid faces. Hover offers the actual result edges of
the feature; clicking confirms that same wire, including occluded edges.
Double-click inspection shows the complete authored feature preview. All these
wires keep ordinary width. The user subsequently extended the same permanent
wire-only rule to individual faces selected for placement/reference entry.
Show their exact current boundary, holes and view silhouettes through occlusion;
never fill them or switch the offer to an untrimmed original face.

The local XY prototype is paused for user acceptance and is not changed by this
viewer work. No kernel version, model calculation, persistence or placement
contract changes are required here.

## Completed first stage: no whole-solid face fill

Removed the whole-container triangle fill pass and its range cache. Ordinary
selection again consumes the existing result-wire path. The exact Face fill
pass was initially retained separately, then removed after the user confirmed
wire-only feedback for individual faces as well. Their existing cached boundary
path is reused at ordinary width; no second boundary implementation is added.

Windows checks pass on the isolated `C:/zcb/reference-next` build: synthetic GPU
selection/cache checks, the current private L30X30X3/Vytažení 006 packet, the
interaction-color GUI suite, and all five language catalogs. Source preview
upload remains zero for ordinary selection. Forty inspection frames add no
source/base uploads or ownership builds. Evidence:
`build/solid-no-fill-gpu.log`, `build/solid-no-fill-L006.log`,
`build/solid-no-fill-colors.log`, `build/solid-no-fill-localization.log`.

## Ownership and hidden-wire investigation

The current L30X30X3 has six Extrusions. Its final packet assigns 19 operational
edges to 006, including four explicit Boolean split-edge descendants whose
persisted parent belongs to 004. Operational fragment identity remains valid
for exact topology references. Ordinary wire presentation must retain the
parent's authored owner for those inherited fragments, while a newly created
intersection remains owned by its creating operation. Explicit derived display
owners (sheet states and patterns) and occurrence paths remain authoritative.

Read the already persisted length-delimited edge ancestry; do not guess from
coordinates, adjacency, UUID order or a runtime OCCT enumeration. Face and edge
parent encodings differ: face parent sets wrap each reference in a length field;
an edge's single parent stores the three reference fields directly. Neither
encoding is changed by this work.

The occluded-wire pass reuses cached result-edge ranges with depth
testing disabled at ordinary line width. The complete source preview remains
exclusive to explicit inspection. Individual Face selection reuses the existing
cached boundary/silhouette path, at width 1; its fill buffer and triangle pass
have been removed entirely.

## Final Windows verification

- Native viewer contracts pass, including 144 complete picking-list comparisons,
  nested split-edge ancestry, new intersections, explicit derived display owners,
  occurrence isolation and existing exact face ancestry.
- GPU checks pass for ordinary Container and individual Face mouse hover,
  right-click candidate cycling and left-click confirmation behind an occluding
  solid in all five display modes. Reference inspection retains the hidden
  face boundary. Interior framebuffer pixels remain unchanged.
- The current L30X30X3/006 fixture passes actual mouse hover/cycling/click, with
  the same displayed candidate identity on confirmation. Exactly 15 result edges
  belong to 006; the four inherited 004 fragments are excluded. The complete
  40-vertex preview appears only during explicit inspection. Forty subsequent
  frames add no source/base uploads or ownership builds. The input packet and
  user document remain unchanged. Fixture SHA-256:
  `82B9548E55EEF1B256D5B8AEE0E583918A6CC62E60D4D736E62B46AD07CD2988`.
- Exact face/reference-eye wire GUI checks, ordinary View/Tree selection and
  double-click inspection, Fillet/Chamfer selection and Solid/Thin start/end cap
  mouse reference insertion pass. The cap checks retain identity through
  Undo/Redo and native reopen.
- Interaction colors and all five language catalog checks pass. No product
  strings were added or changed.

Final idle-host observations for the real 006 packet: idle median 16.63 ms,
inspection median 16.40 ms, inspection p90 17.78 ms. These observations are not
universal performance guarantees. Face boundary camera projection uses the
existing path; no separate timing claim is made for it.

Evidence: `build/strict-wire-native.log`, `build/strict-wire-ui-contracts.log`,
`build/strict-wire-treatment-contract.log`, `build/solid-no-fill-gpu.log`,
`build/solid-no-fill-L006.log`, `build/solid-no-fill-colors.log`,
`build/solid-no-fill-localization.log`. The L fixture's actual hover screenshot is
`build/L30X30X3-006-wire.png.hover.png`.

Linux execution, every modeling operation's ancestry combination and a separate
hole-face framebuffer matrix remain unverified. Manual user acceptance remains
the next step; this is an isolated development build, not a portable release.

The source file is used read-only; the GPU test loads
`build/L30X30X3-selection-fixture.prtz`. No factory/user documents are converted.
No product-facing strings change; localization coverage is still required.
