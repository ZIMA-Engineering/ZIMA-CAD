# Shaft thread entrance and factory audit, 2026-10-18

## Defect and correction

Without an explicitly selected entrance chamfer, the cyan shaft-thread preview
starts at the selected start plane. The OCCT calculation instead replaced the
original cylindrical support with its currently visible, chamfer-shortened face.
Consequently the calculated root sheet started later than the preview.

Keep the current calculated original cylinder as the axial support and retain the
visible face's current material-side classification. An explicitly selected
entrance chamfer still intersects and clips the root sheet. The current original
support, rather than an old parameter snapshot, preserves regeneration after
shaft diameter, length and placement changes. Shared placement and Sketcher
solver rules are unchanged. The native file schema is unchanged; calculated
thread rim identities are corrected as described below.

The user's `HFB-CSN021111-M10x050.prtz` was tested through an independent copy.
Its saved root sheet ended at Z = 49.30717967697245 mm at the entrance; explicit
regeneration now reaches the selected plane at Z = 50 mm. The other full-thread
end remains at Z = 35 mm. The user-authored Project was not saved or overwritten.

## Boundary selection and downstream references

Previously all rims and the parameter seam of a thread sheet shared one edge
key, for example `thread:boundary:root`. An edge selection could therefore
resolve to another rim. Publish stable start/end cap identities instead:
`thread:boundary:root:start`, `thread:boundary:root:end` and, for a shaft with an
exit runout, `thread:boundary:runout:end:end`. A complete rim shared by the root
and runout uses one identity and is published once. Keep genuinely separate open
fragments after trimming. Parameter seams and other unclassified trimming wires
remain cosmetic; they are not published as original reference edges.

Identity derives from the authored cap roles and calculated boundary planes,
including explicit cap trimming, rather than OCCT enumeration or the camera.
The operation-local rim registry is rebuilt for each publication and contains
only the current thread rims; it does not scan all existing body wires for every
rim or cache data across source changes. Existing Face references are unchanged.

Shaft thread commits now consume `calculate_part_with_resolved_references`, the
existing common calculation entry point already used by other features. This
refreshes dependent Sketch references in the same transaction after a length
change. No shared placement implementation or rules are edited. Unchanged OK
still returns before calculation and creates no transaction.

## Verification

- The command regression reproduced the defect before the fix: clearing the
  selected chamfer incorrectly retained a 1 mm entrance offset. It now reaches
  zero, while the explicit entrance and terminal chamfer case retains its exact
  0.08 mm and 29.92 mm root-cylinder intersections.
- Native shaft-thread contracts cover numeric edits, independent root diameter,
  runout, Through All, Up To plane/cylinder/cone, both start directions, source
  changes, material-side rejection, retained references and native round trips.
- The new rim-reference command matrix covers both shaft start directions and
  both coordinate sides. It independently checks source rim radii/stations and
  contact coordinates after successive X dimension edits, locked Properties
  edits, locked dragging rejection, 12 successive shaft-length changes,
  Undo/Redo, explicit regeneration, underconstrained dragging and save/reopen.
  Driving X and reference Y dimensions are covered. This is not an exhaustive
  matrix of every Sketch dimension kind or both GUI contact selection orders;
  the solver and projected-edge geometry implementations are unchanged.
- The GUI command regression passes actual root-face selection, RMB candidate
  cycling and external-reference creation for both the root face and its exact
  end rim in a longitudinal Sketch, together
  with Properties, OK/Cancel and Undo/Redo. The same actual mouse selection also
  passes in the owned Sketch of the user's final Revolution, on a separate copy
  with its owning Body activated; Cancel restores the editing definition.
  Face/curve selection filters narrow the common candidate list and each end rim
  occurs once. The latest user-saved fixture also passes; the test verifies that
  its active document path is the isolated copy before editing.
  This is a finite selection test;
  it does not claim a complete Sketcher dimension/drag matrix.
- A separate in-memory check of the user's final Sketch attaches a construction
  point to both the root sheet and the exit runout sheet. Changing thread length
  15 -> 16 -> 17 -> 15 mm moves the contact through world Z = 35 -> 34 -> 33 ->
  35 mm while X remains 4.08 mm. This demonstrates the existing two-support
  contact at the full-thread end, rather than an arbitrary point on its cylinder.
  Dimensions, dragging, reversed contact sides and both selection orders were not
  verified for this additional user-fixture contact check; no solver behavior was
  changed on its evidence.
- `--audit-shaft-threads` in the native fastener contract tool checked all 212
  `.prtz` files currently below the screws table directory, including reference
  models. All 212 explicitly select their entrance chamfer. Saved and freshly
  calculated root radii, start stations, full-thread ends and runout ends agree
  with the authored definitions. Every model was calculated, saved to a separate
  audit directory and reopened without changing its authored history.
- The independent current fastener manifest check passes all 200 production
  bolts: valid single solids, dimensions, mass, bearing choice, fillets,
  constrained profiles and thread limits. None requires an entrance-definition
  correction. The subsequent boundary-identity change requires refreshing the
  calculated caches of the 211 tracked factory Parts; the additional untracked
  user reference model is checked read only and excluded from installation.
- All 11 tracked reference Drawings regenerate and save/reopen with their 101
  measured dimensions resolved and their numerical measurements unchanged within
  0.000001 mm/degrees. Five authored `b` attachments formerly used the ambiguous
  root-sheet key. They are explicitly reattached to `thread:boundary:root:end`,
  retaining their dimension/segment IDs, layout, style and other attachment.
  This is factory authorship, not a runtime legacy-reference adapter.
- Native thread Drawing tests cover axial, oblique, occluded and repeated views,
  entrance chamfers, dimensioning, persistence, PDF and thin/thick line weights.
  The common picker contract checks 144 accelerated candidate lists against the
  scan implementation and source-change invalidation, including cosmetic wires
  carrying native viewer labels. Cosmetic wires select their Container rather
  than leaking as reference Edges.
- Localization review found no new or changed product text. The five-language
  translation/catalog GUI contract passes for cs, en, de, fr and ru.
- Factory installation replaced exactly 211 tracked Parts and 11 tracked
  Drawings after checking their unchanged source hashes against the preflight
  inventory. All 222 installed hashes match the calculated candidates. The
  current native reader loads all 244 tracked native assets; library extensions
  use byte-identical disposable `.prtz` reader copies. This read-only gate also
  verifies that shared settings and the latest user Project remain unchanged.

Windows verification uses the pinned shared OCCT 8.0.0 implementation. Linux was
not run on this Windows host. No release was built or published for this fix.

Local evidence is under `build/shaft-thread-library-audit-20261018/report.json`,
`build/shaft-thread-fastener-check-20261018.log`,
`build/shaft-thread-ui-20261018.out` and
`build/fitted-thread-ui-20261018.out`,
`build/fitted-thread-end-contact-20261018.json` and
`build/shaft-thread-translations-20261018.out`.

The boundary follow-up uses `build/thread-rim-factory-final-20261018/`,
`build/thread-rim-ui-20261018.out` and the factory-before checksum inventory
`build/thread-rim-factory-before-20261018.json`. Factory installation is limited
to tracked, previously unchanged assets after the candidate verification gates.
