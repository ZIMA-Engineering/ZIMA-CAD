# Projection presentation and first FEM GUI milestone

Baseline: `e16628390`. Local verification passes on Windows with MSVC 19.51,
Qt 6.11.0 and the existing patched OCCT 8.0.1 SDK. This is a finite delivery of
Projection presentation and FEM Body/source authoring, not a mesher or solver.

## Changed behavior

Projection source curves, projected preview curves and point markers use the
ordinary foreground color in both themes. Native projected endpoint identities
and picking are retained. No connecting dashed azure guides are added. The
existing viewer-triangle ray preview remains disposable and does not supply
persistent geometry. The two editable reference tables start at compact heights;
both target and retained-region entry remain visible, while surplus window
height expands the table viewports. Direction/reference and first-hit semantics
are unchanged.

Create FEM is available at the Part Body-list level. One shared internal
Properties window creates and edits the analysis name and source reference.
The required source is replaceable, with shared reference entry and an
independent inspection eye. Sources include a preceding calculated material
Body, a consumed Body, or a Boolean result. No second material Body is inserted.
The FEM mode displays the selected calculated source and disables ordinary CAD
modeling commands. Return to Part restores Body-list editing; activating a
material Body restores Modeling. Conditions, meshing, solving and results are
not exposed as implemented commands.

## Command lifecycle and cost

FEM opening prepares its candidates from immutable calculated `body_outputs`
and publishes its input scene once. Reference entry reuses a prepared pick
packet. Inspection updates only a transient wire. Source selection, name editing,
unchanged OK and Cancel do not invoke the CAD kernel. Changed OK uses the existing
metadata-only FEM transaction; suppression of an analysis is metadata-only too.
Source geometry remains owned by the material Body and shared through
`BodySnapshot`. No container-placement solver changes were made.

The Projection preview reuses the established ray-to-viewer-triangle algorithm.
It adds only visual point markers to the existing source/result wire calculation.
The ordinary feature selection provider consumes this same preview generator.

## Verification record

- Native Projection document tests pass for Curves, Split and Surface, including
  read-only preview, unchanged OK, repeated edits, source dimension/drag matrix,
  Undo/Redo, native reopening and external/nested source contracts.
- Projection history tests pass, including downstream Surface Trim, Thicken,
  Shell and native direction references.
- All 26 directional projection kernel cases pass.
- Component GUI checks pass for reference table visibility at parent heights
  900, 1000 and 1200, fixed row heights and five languages in both themes.
- Foreground marker/preview, both selected source/result wires and native endpoint
  picking pass in both themes. The one-pixel selected GPU wire is verified against
  its local background to account for fractional coverage at 150% display scaling.
  The original opaque-pixel assertion rejected a correctly rendered antialiased
  line; product stroke width and selection colors were not changed.
- The actual Projection GUI passes all three outputs, View target/region entry,
  replacement/removal, independent inspection, whole-Origin/native-axis direction,
  OK/no-op/Cancel, save, Undo/Redo, Tree/View single/double click and three successive
  source radius edits. Preview and inspection calculation counters remain zero.
- FEM GUI passes consumed Body and Boolean selection in Tree and View, source
  replacement, independent eye, fixed reference-row resizing, short-middle entry
  termination, middle-double confirmation over View, Cancel, unchanged OK, metadata
  Undo/Redo, shared source packet, exact active source display, CAD command exclusion
  and restoration, suppression without calculation, and native save/reopen.
  Properties do not add a modeling Origin. Five languages and both themes pass.
- Native FEM definition and workspace transaction tests pass, as does the existing
  native Body Scale suite. The actual Body Scale GUI also passes.
- New Part and Assembly creation through the regenerated start templates passes
  in the actual GUI, including active editable context and enabled normal commands.
- Localization key/placeholder and production-call coverage passes for 4,664 Qt
  keys, 938 named keys and 4,301 calls across all five languages.
- Committed-source packaging, signed runtime and public release checks are separate
  delivery gates; their publication record will identify the accepted commit.

## Persistence, assets and limits

The GUI consumes the already approved native FEM definition from `e16628390`.
This presentation change adds no further native schema or required sidecar.
All six Part/Skeleton/Assembly mm/in templates were recalculated or rewritten,
saved and reopened on isolated copies with the current native implementation;
all are byte-identical. Hashes confirm 1,185 factory assets and eleven Toyota
originals remain unchanged, and all eleven protected backups match. Previously
unsupported Toyota formats remain a reopening gap; hash equality does not certify
their native loading. The packaged factory acceptance remains a release gate.

Source loss/repair is covered by native FEM tests; its interactive GUI workflow
and FEM source selection inside an activated nested Assembly occurrence are not
claimed as verified by this finite GUI matrix. No condition Sketch editing or
shared placement solver change is included. Linux execution is not verified on
this Windows host. No mesh quality or numerical solution accuracy is claimed.

## Published Windows delivery

Windows 2026112101 passed committed-source packaging, twelve sheet-branch gates,
all 1,185 factory native loads and the four targeted packaged GUI checks. Signed
archive validation, anonymous public download, safe extraction, production updater
trust and local binary equality also pass. Exact identities, hashes and timestamps
are in the [publication record](20261121-windows-2101-publication.json).
