# Spotlight, marker colors and live camera

Status: verified in the signed Windows 2026112303 release.
The user approved the spotlight's native storage scope on 2026-11-23.

## Changed behavior

The shared Render object editor now creates spotlights with luminous power,
source radius, full cone angle and edge softness. The renderer prepares the
directed cone and integrated flux normalization once per immutable scene.
Existing Sun and omnidirectional-light calculations keep their respective
direction-only and position-only behavior. The latter command is labelled Light;
existing object names and the `point-light` native tag are unchanged.

Camera Properties uses a live View from camera checkbox. Its transient output
frame preserves the authored vertical field of view or orthographic height and
the Render output aspect ratio. Position and parameter edits update the camera
without committing. Leaving this mode restores the previous modeling view.
The context-menu command retains its one-time view change.

New scene objects immediately enter the existing first-reference placement flow.
All ordinary Render marker strokes use their declared dimension-yellow color,
with the existing hover/selection and broken-reference colors. Properties wire
and label previews use cyan through model occlusion. No placement solver or
native placement contract was changed.

## Windows evidence

- Native Render document tests: all four object kinds, exact native round-trip,
  spot-only required fields, invalid angle/softness rejection, metadata Undo/Redo,
  Part save/reopen and unchanged calculated material snapshots.
- Native image tests: 15 cone/softness combinations with independently integrated
  flux; inside/outside/backward emission; sharp and soft images; enabled state,
  color and deterministic worker counts. Independent image checks verify the
  inverse-square distance law, solid-angle normalization and shadow occlusion.
  Existing multi-Body, cancellation and immutable-snapshot checks also pass.
- Full Render GUI at ordinary and compact sizes: all four object types use
  whole-Origin placement, reference replacement and inspection, Cancel, unchanged
  OK, deletion and Undo/Redo. Initial reference entry is asserted explicitly.
- Live camera: repeated actual position edits; perspective/orthographic modes;
  320:200, 200:320 and 2048:256 aspect ratios; window resizing; and restoration
  when unchecked or cancelled. Three independent render-ray comparisons per
  state verify direction and eye/framing. Ordinary camera tab switching and
  the context-menu action retain their behavior without CAD calculation.
- Actual framebuffer checks cover normal yellow strokes, hover, selection,
  broken references and occluded cyan drafts in five display modes and both
  themes. Existing FEM/Render label and selectable hidden-endpoint checks pass.
- Gallery export, navigation, background modeling, immutable output comparison,
  retained-image Undo/Redo and native save/close/reopen pass. Concurrent explicit
  extrusion Properties/OK took 121 and 142 ms in these two synthetic runs; these
  observations are not a general performance guarantee.
- The five-language translation and 660-case shared dialog layout matrices pass.
  Camera checkbox and Light/Spotlight property labels are checked in all five
  languages. No new placement text
  was introduced.
- A follow-up full GUI run passed after inspecting the camera composition:
  its own frustum and editing Origin are hidden while looking through that
  camera and restored when the checkbox is cleared. The output frame remains
  visible. The native placement definition is unchanged.

Local evidence is under `build/render-followup-20261123` (native logs, GUI logs
and screenshots) and `build/trim-contact-20261123` (layout, translation and
marker framebuffer logs). These tests used the native Qt/OCCT Windows build.

## Native assets and limits

The inventory contains 1,210 tracked native assets, with no factory Render Body
definitions. The current native serializer regenerated and reopened all six
Part/Skeleton/Assembly start templates; their bytes were unchanged. GUI new
document tests verified template creation, the active editable Part Body and
normal Part/Assembly commands, including localized document options.

All 172 protected project files retained their recorded hashes. Ten native
backup copies were checked: two Drawings round-tripped, while seven Parts and
one Assembly were already unsupported by published Windows 2026112302. The new
runtime reports the same unsupported-format condition. Their modeling behavior
is therefore unverified; no original file was converted or overwritten.

The shared C++ implementation has not been executed on Linux. Packaged-runtime,
committed-asset and public-release checks passed for Windows 2026112303. Assembly Render scenes,
image cropping and cylindrical profile wrapping are outside this change.

## Windows package acceptance

The same focused GUI cases passed in the committed Windows 2026112303 package.
See the [publication evidence](20261123-windows-2303-publication.json).
