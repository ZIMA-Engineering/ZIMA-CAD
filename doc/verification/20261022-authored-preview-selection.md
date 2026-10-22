# Authored preview wire selection, 2026-10-22

The user rejected this first experiment: the calculated original contribution
is not the analytical command-preview wire they requested. Its passing checks
below did not establish that distinction. The correction and direct GUI oracle
are recorded in [Command preview wire selection](20261022-command-preview-selection.md).

## Scope and implementation

The user requested a further isolated experiment after L30X30X3/Extrusion 006
still offered unrelated result edges. Ordinary feature hover and selection now
reuse the complete authored preview wire used by explicit inspection. Wires
remain ordinary width and visible through occlusion; solid faces are never
filled. Double-click retains its established additional parameter inspection.

Extracted the existing preview builder into one shared viewer implementation.
The application supplies the already calculated contribution at the feature's
creation boundary through the existing authored-wire helper. This is necessary
for sheet/solid state histories, whose final original-reference packet can
contain changed material geometry. Occurrence and Body transforms are retained;
the provider affects only presentation and never changes pick/reference data.
The existing source-wire GPU buffer serves hover, confirmation and inspection.
It rebuilds for a different source and invalidates on changed scene data;
unchanged hover-to-click-to-inspection transitions reuse its uploaded segments.
No kernel operation, persistence change or Undo transaction is introduced.

Removed the experimental Boolean split-edge ownership decoder, the duplicate
original-wire lookup and the separate ordinary Fillet/Chamfer result-edge
expansion in View and Tree callbacks. The explicit command/reference highlight
API remains available. Source-less imported geometry still uses its existing
display-wire batching instead of duplicating large imported buffers.

Individual Face entry and inspection retain the exact current fragment boundary,
holes and silhouettes, including occluded portions. Original ancestry is used
for reference identity without replacing the highlighted fragment. Shared
placement solving, side identity and the paused local XY experiment are unchanged.

## Verification

Windows verification passes on the isolated `C:/zcb/reference-next` build:

- Eight CTest contracts pass: native viewer/picking, exact Face wire inspection,
  ordinary View/Tree selection and double-click, imported drawn features,
  Fillet/Chamfer/Shell selection, Solid/Thin start/end cap reference entry,
  Sheet states and Solid states. The state tests include Part/Assembly context
  and authored inspection through state changes. Reference-entry checks retain
  identity through Undo/Redo and native reopen.
- GPU mouse hover, RMB cycling and LMB confirmation pass behind an occluding
  solid in all five display modes. Container feedback uses the authored preview
  and excludes both current result edges and later-state reference edges.
  Face feedback retains its current boundary. Interior pixels are unchanged.
  The authored provider is not reread on hover-to-click confirmation.
- Source-less curved silhouettes pass four quarter turns from both viewing
  sides. A separate 29,000-edge STEP fixture uses existing display batches,
  without a source-buffer upload or repeated ownership builds on navigation.
- Both the reported six-feature L30X30X3/006 snapshot and the user's current
  five-feature snapshot pass actual View/Tree clicking and double-clicking.
  Ordinary selection already shows the authored wire; double-click retains the
  same vertex count without another source upload. Document state and ordinary
  selection's base scene revision remain unchanged.
- The 006 and current 003 GPU fixtures each show the expected 40 preview
  vertices. Forty navigation frames add no source/base uploads or ownership
  builds. Clear/reselect, source changes and unchanged serialized packets pass.
- Interaction colors, ordinary wire width, independent reference highlights
  and all five language catalogs pass. No product-facing text changed.

Observed frame times are approximately 16–17 ms median for the two L fixtures
and the synthetic 29,000-edge scene. These are local observations, not universal
performance guarantees. The cache assertions establish avoided repeated work.

Evidence: `build/original-preview-contracts.log`,
`build/original-preview-gpu.log`, `build/original-preview-L006.log`,
`build/original-preview-L-current.log`, `build/original-preview-L006-ui.log`,
`build/original-preview-L-current-ui.log`, `build/original-preview-colors.log`
and `build/original-preview-localization.log`.

The normal development launcher remains unchanged. The experimental launcher is
`tmp/zima-cad-wire-test.bat`. Tests load private copies; the current source file
retains SHA-256
`E6BB3EDDEDC46D4F0F6128805D57D0DECC4851C5C5679EC3CEAE4E8E09BBDEBE`.
The older 006 fixture retains SHA-256
`82B9548E55EEF1B256D5B8AEE0E583918A6CC62E60D4D736E62B46AD07CD2988`.
User and factory models are not converted or overwritten.

Linux execution and exhaustive coverage of every feature type remain unverified.
This is a local experiment awaiting user acceptance, not a portable release.
