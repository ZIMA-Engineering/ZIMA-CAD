# Drawing balloon curve contacts

Balloon creation, reference replacement and leader-end dragging accept arbitrary
locations on the selected view's curves. They use the common measurement picker
with continuous contacts enabled. Existing characteristic points retain their
selection priority. BOM filtering still resolves the exact source owner and full
occurrence path, including a child of a listed subassembly.

The existing CurvePoint attachment stores a normalized curve parameter in the
native Drawing. No document fields, Part/Assembly contracts or factory assets
change. There is no face attachment in this extension.

For a true projected circle, continuous picking computes the radial parameter
analytically. Interpolation along displayed chords previously introduced a small
angular discrepancy between the cursor and the resolved circular contact.
Noncircular and foreshortened supports retain their established parameter path.
This calculation uses prepared projected geometry and does not invoke OCCT.

## Verification

The native measurement test covers clockwise and counterclockwise circular arcs,
major arcs and full circles at three interior parameters. It independently checks
the resolved location and radius, and compares direct and prepared candidates.

The mouse-driven balloon GUI test covers a line, a circular arc and a noncircular
polyline away from characteristic points. It checks exact attachment locations,
the correct BOM item, preview without a transaction, Cancel, one-item commit,
Undo/Redo, endpoint dragging, refreshed source geometry and native save/reopen.
The complete pre-existing balloon GUI and native contracts also pass. Evidence
is in `build/balloon-exact-contact-tests.log`; the complete measurement GUI,
prepared-picker and translation checks also pass in
`build/drawing-contact-final-regressions.log`. Generated Drawing and screenshot
fixtures are under `build/balloon-continuous-curve`.

This is a finite Windows matrix. It does not establish native Linux or packaged
release acceptance, nor exhaustive coverage of all section/detail/crop/break
combinations. No user-visible strings were added; existing localized command and
reference controls are reused.

The ordinary `zima-cad-cpp` development target was rebuilt with these changes,
and its existing compact-window startup GUI contract passed. The root
`zima-cad.bat` continues to select this executable.
