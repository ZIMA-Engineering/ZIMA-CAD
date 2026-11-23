# Holes Plane and Sketch layout

Holes now consumes the same `ProfilePlaneControls` presentation as Feature and
FEM: the Plane group contains the selector and offset in two rows, and the
adjacent Sketch group has the shared icon button spanning both rows. Existing
fields, the offset lock and their connections are reparented without changing
placement or Sketch ownership.

Windows verification on 2026-11-23:

- The native Qt layout matrix passed 660 dialog/language/size combinations,
  including explicit Holes group alignment and two-row button-height checks.
- The Holes GUI contract passed creation, standalone Sketch conversion, draft
  changes, Cancel, OK, middle-button confirmation, native persistence and Undo.
- The five-language translation UI contract passed. This change reuses existing
  Plane, Sketch, selector and offset translations; it adds no user-visible text.

Local logs: `build/trim-contact-20261123/layout.out`,
`build/trim-contact-20261123/translations.out` and
`build/render-followup-20261123/holes.out`. Tests used the current native Windows
build. Linux execution and packaged-release verification remain pending.
