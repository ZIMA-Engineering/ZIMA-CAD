# Development follow-up, 2026-11-23

This list records the user's follow-up scope. Entries are pending unless a
verification report explicitly records completion; the list is not release
evidence.

1. Finish noncircular Offset tangency before starting CAM. Verify native and
   externally sourced supports, circles and arcs, repeated dimension edits and
   drags, contact identity, side choice, trimming, Undo/Redo and native reopening.
2. Complete the Render interaction follow-up in `RENDER_BODY_DESIGN.md`: marker
   visibility and ordinary selection, view through a camera from Properties and
   both context menus, image-tab command column and Drawing-style navigation,
   explicit image retention, and saved-image context actions.
   FEM and Render marks, including their adjacent text labels, use dimension
   yellow in ordinary display and the shared hover/selection colors. Owned
   planar and projected FEM Sketch region boundaries follow the same colors
   in ordinary display and pending Properties; Sketch editing keeps its colors.
3. Straighten: preserve dependent operations by transforming their position and
   orientation with the straightened substrate. Preserve feature/reference
   identities and authored dependencies, including subsequent additive and
   subtractive extrusions. Investigate the reported unsupported replay on an
   isolated copy of the supplied Part; do not infer support from a successful
   straightening of its source alone. Also use the existing failed-calculation
   confirmation contract: unchanged second OK retains an unresolved definition
   without recalculation, while Cancel preserves the original history.
4. Scaled, mirrored and patterned Bodies: axis endpoints must remain pickable
   and usable as references without permanent point dots. Trace transformed
   presentation and picking metadata and verify identical interaction across
   all three operations, including dependent references and native reopening.
5. Implement the approved initial CAM scope in `CAM_BODY_DESIGN.md`. General NC
   motions and operations remain independent of a machine-specific postprocessor.
6. Rectangular image cropping is a planned gallery extension. Preserve the
   original result and create a new retained image after confirmation. Additional
   image tools have not yet been specified.

Update documentation, commit and push completed coherent changes, then publish a
verified Windows build using the repository release gates. Report native Linux
verification separately. Keep user configuration, private models and temporary
diagnostics out of Git and release archives.
