# Mirror presentation and handed Family variants

## Scope

The user wants one authored model with original and mirrored Family Table rows
for insertion into an Assembly. The existing Body-presence model supports this:
the original row includes the source Body and suppresses Mirror; the mirrored row
does the reverse. A suppressed source remains available to its derived copy.
No independent mirror-file command or new Family format is introduced.

Read-only inspection of the saved `50.prtz` found columns bound to the source
Body and FEM Body. Its `50-V01` row suppresses the source and includes FEM; Mirror
has no table column. This identifies the saved state, not which mouse action
produced it. The original file remains unchanged. All investigations use copies.

## Presentation changes

- Mirror Properties always highlights the selected plane through the existing
  reference-wire overlay, including an empty source list and external Body Origin
  planes. Temporary Origin visibility uses the command's existing cleanup policy.
  The shared placement solver and persisted plane identities are unchanged.
- Mirror/Pattern Tree rows use the same theme-aware resource icon as the command.
  Expanding a Tree row no longer selects the checked-command glyph.
- Body copy selection uses the shared authored preview provider, transforming
  cached source Body wires or the existing feature preview. It excludes seams,
  smooth transition junctions and topology-point markers. Selection does not
  calculate a solid or recolor its faces.
- Copy face ancestry resolves inherited source face and Body styles in View,
  Render and exchange output. An explicit copy face or Body style takes precedence.
  Nested Mirror/Pattern ancestry uses the same resolver; no style is baked into
  a new persisted metadata field.

## Finite Windows checks

- Native Family tests cover opposite Body presence, independent reflected bounds,
  one-Body volume, cold regeneration, native reopening, both Assembly insertions
  and Undo/Redo, using a small box and a private copy of the user's model.
- The GUI Mirror/Pattern suite passes actual Tree-click Family bindings, opposite
  Yes/No values, stored owner identities, both variant tabs and Undo. It also
  covers plane highlighting before source selection, XY/YZ/XZ, own-Origin View
  picking, an external Body plane, reopening, nested Part/Assembly contexts,
  reference entry, placement, Cancel, copy operations and double-click inspection.
  Highlight changes retain the camera/document state and perform zero Body
  calculations. Icon comparisons cover both palettes and collapsed/expanded,
  normal/selected states.
- The existing Family GUI suite passes component variants, Cancel, middle-button
  confirmation, Undo and Part/Assembly Drawing sources. Copy Save after Undo and
  history branching also passes.
- Appearance tests cover inherited face/Body finishes, nested copies, source
  changes, explicit copy overrides, persistence and framebuffer equivalence.
  Copy selection with and without helper points produces the same framebuffer.
  Existing appearance-command tests also pass.
- Localization coverage, catalogs and UI contracts pass for `cs`, `en`, `de`,
  `fr`, `ru`. This change introduces no application-visible text.

Evidence is retained locally under `build/mirror-20261124`. Native Linux execution
is unverified. These are finite tests, not proof of every imported topology,
appearance combination or arbitrary Family dependency graph.

## Save-after-Regenerate repair

Extended testing reproduced `Calculated reference geometry is not append-only`
when saving a regenerated variant. Scoped history boundaries retain sparse exact
reference packets. Native load correctly discards their ephemeral calculation
coverage proof, but the next serializer incorrectly inferred a cumulative
reference prefix from the absent proof.

The writer now uses the existing full-packet representation after a nonempty
scoped boundary. It retains the current geometry without persisting kernel proof,
changing the native format or recalculating a solid. Both variants of the private
user-model copy pass regeneration, Save through the owning generic, native reopen
and Assembly insertion. Exact reference packets are compared across the roundtrip.
The native document, shared-cache and history-reference-view suites also pass.
A small synthetic scoped-history fixture verifies three successive native
roundtrips with a retained exact point packet followed by an empty packet. The
complete native Family suite passes with this regression included.

An independent `50-family-overeno.prtz` copy was created with native Save As and
a fresh document identity. Its `50-original` and `50-mirror` rows both pass
regeneration and Save through the generic. The source original's SHA-256 remains
`4d454a28e8072c4afc1c4d847e56ab81c60863d161ccb056d5e7a06bf4cb7a07`.

Windows package/publication acceptance remains pending.
