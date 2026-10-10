# STEP loading and Extrusion command lifecycle

## Scope

Follow-up to [import interaction](20261010-import-interaction-investigation.md).
The user requested investigation of STEP loading, then opening Extrusion and
cancelling its Properties window over an imported model. Input geometry and
files must remain exact, including stored properties, source identities, sides,
native persistence and Undo/Redo. The implementation uses shared C++ and OCCT
8.0.0; it does not change the document format or development launcher.

## Redundant import preparation

Both `import_step_part` and `import_step_assembly` previously called
`import_step_components` to create complete result packets. They retained only
the BRep and source topology bindings from these packets. Ordinary history
evaluation then restored that BRep and prepared geometry, measured properties
and references again. The first full packet was discarded.

`freeze_step_components` now validates the imported shape and captures its
native BRep and source bindings through the existing topology archive path.
Ordinary history evaluation still performs the complete final calculation.
No mesh detail, integration precision, property or supported reference is
removed. The immutable archive remains in the native document, independently
of the original STEP file.

Product structure inspection and component capture still transfer STEP through
separate OCCT readers. This remaining repetition is distinct from the discarded
packet and is not removed by the freeze-only change.

## Measurement method

`zima_cpp_step_model_contract_tests --import-preparation-file <source> <leaf>
<baseline|frozen> <packet.json>` reconstructs the former preparation and the
freeze-only preparation, each followed by the same ordinary history evaluation.
The timer includes hierarchy inspection, source capture and final calculation,
but excludes JSON diagnostics, native saving, Body placement and GUI scene
publication. The selected leaf is explicit; this must not be described as a
complete GUI import timing unless that additional path is measured separately.

The final complete packets are compared, including geometry, properties,
references, side data, topology bindings and the persisted BRep. Synthetic
hierarchy and cylinder checks compare every serialized field directly.

Extrusion diagnostics independently time action opening and Cancel through
processed GUI events. They count scene publications and verify exact restored
viewer packet, camera, document state and Undo/Redo availability. Optional
`ZIMA_CPP_PROFILE_COMMAND` output separates dialog, reference, preview and
selection setup, then closing cleanup and scene restoration.

## Windows observations

Release build, baseline source `1919228b` plus the preceding interaction fixes,
OCCT 8.0.0, `Projects/import/G92H1-T.step` (220,957,785 bytes), leaf 0,
definition `0:1:1:2`. Runs were serial on the same Windows machine.

| Preparation | Structure inspection | Total preparation |
| --- | ---: | ---: |
| Former complete capture followed by history | 14,368.5 ms | 133,288 ms |
| Freeze-only capture followed by history | 14,782.2 ms | 96,414.4 ms |

This sample eliminates about 36.9 seconds (27.7%). It is an observation of the
defined preparation path, not a universal speedup or full GUI import latency.
Both complete final packet serializations have SHA-256
`7ea67287da61be2342dda6168698dc806a167242816fbea6b454b0dd573ab9e3`.
The final mesh/properties/reference/BRep preparation runs twice in the former
path and once in the new path. Source transfer and exact property integration
remain substantial costs; no approximate property path was introduced.

## Extrusion work removed

- Move the locally prepared common reference packet into the command. Extrusion
  has no sheet-attachment consumer, so it no longer makes that unused copy.
- In the Part Extrusion preview, register the owned Sketch and its work-plane
  context before their common document resolution. The existing resolver runs
  once rather than twice; other feature and Assembly paths retain their ordering.
- Keep previously uploaded shaded geometry, ordinary wire and silhouette
  adjacency only when their actual coordinates, triangle indices, face identities
  and non-overlay edge geometry/identities match. Rebuild scene picking, reference
  data, styles and edge-slot associations normally. An altered source invalidates
  GPU reuse. Transient overlays and analytical wire remain current.
  Coordinate comparisons are bit-exact, including signed zero; no coordinate
  normalization is used to obtain reuse. The small GUI diagnostic verifies
  invalidation after both a signed-zero input change and a moved vertex.
- Saved-measurement reference availability was indexed on every scene refresh,
  even when no measurement row existed. Skip only this unused index; command
  availability and Body-properties rows still update, and saved measurements
  retain their original validation path.
- Empty section lists also constructed a complete reference index and copied
  the model for a display update that never happened. Keep the normal Tree row
  and a new section dialog's preview; skip only this unused preparation.

The representative native G92H1 fixture retains the complete imported packet.
Before these lifecycle changes, three warm trials opened Extrusion in
4,888.23 / 5,216.22 / 4,885.98 ms and cancelled it in
3,676.81 / 3,501.29 / 3,629.29 ms. Each transition published one base scene.
Opening and cancellation invoke no body calculation. Exact restored viewer
packets, camera and document/Undo/Redo state passed in every trial.

After these changes, the three warm opening trials were
2,254.38 / 2,222.36 / 2,269.22 ms; Cancel took
1,415.66 / 1,434.75 / 1,556.68 ms. Median observations improve from 4,888.23
to 2,254.38 ms (53.9%) and from 3,629.29 to 1,434.75 ms (60.5%). Each
transition still publishes one base scene, but performs zero geometry uploads.
Remaining work includes copying the Body display context, rebuilding picking
and reference presentation, and one required preview resolution. This is not
an instantaneous command or a measured rotation-FPS result.

The shaded vertex array, ordinary line array and silhouette candidate data
match a forced fresh preparation byte-for-byte. Their combined SHA-256 is
`4001f7fb5a632c8997884e1635515be409e8e3534f94249022fe924c860fe41d`.
The complete restored viewer packet and camera also remain exact. A direct
desktop framebuffer comparison differs at seven pixels by at most one 8-bit
channel level; the diagnostic records this explicitly and permits only this
one-level raster rounding, alongside the mandatory exact upload-data check.
No normal, silhouette, mesh or modeling precision is reduced.

The final acceptance rerun opened in 2,284.53 / 2,489.64 / 2,711.44 ms and
cancelled in 1,448.89 / 1,488.98 / 1,647.48 ms. The exact same upload digest
passed, and this rerun's framebuffer comparison differed at zero pixels.
The timing variation is recorded rather than treated as a fixed latency limit.

An earlier forced-render experiment replaced the viewer packet twice; that
also resets picking and annotation layout, so it was not an isolated upload
comparison. The accepted diagnostic invalidates only GPU preparation, without
changing scene state.

## Acceptance scope

The focused Windows matrix passes STEP/interchange, native section geometry,
import surface interaction, Extrusion lifecycle, complete Body preview,
profile frames, profile limits, profile-on-sheet, saved measurement inspection,
face inspection, viewer candidate equivalence, import commands and primitive
reference commands (13 checks). Profile-frame coverage includes 12 creation/edit
Extrusion/Revolution cases over three planes and eight frame orientations.
The unchanged Sketch solver is not covered by an exhaustive dimension/edit/drag
matrix in this investigation.

The full section GUI diagnostic passes its preceding 3D section operations but
fails at Drawing label/caption default positions. The signed Windows 2026100906
application reproduces the same failure with isolated factory settings; this
is not accepted as a full section GUI pass. Broader existing UI failures and
the G92K1 OCCT crash remain outside this acceptance.

Windows could not load the factory VentilationWindow cache refreshed by Linux
preview 2026100908. Explicit Windows regeneration preserves all persisted
reference identity sets at every history boundary; independent two-way OCCT
cuts against both the previous Windows and Linux final shapes produce zero area
and volume. The definition matches the previous Windows definition exactly.
Cross-platform cached external coordinates differ at floating-point roundoff
scale and strict fingerprints are not bypassed. Linux execution and native
cache portability require verification on the Linux host.

Repository archive validation tests pass (10 checks, two platform-specific
skips); publisher tests pass (16 checks). No product-visible text was changed.
