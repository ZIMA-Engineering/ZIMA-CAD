# FEM player, video export and activation visibility

Status: verified in the private Windows development build; not published.
Baseline: `ab2ed1633` with the pending changes described below.

## Behavior and ownership

The FEM sidebar exposes the existing result choices as individual actions.
Mechanical and thermal availability rules are preserved. Play switches to Pause
while running. The shared internal properties window hosts a bottom-right player
with a timeline, expandable cycle-duration and magnification controls, and Save
video. Cancel restores the initial presentation without publishing an unnecessary
replacement scene. Playback changes only result presentation, not CAD geometry,
solver data or document history.

MP4 export uses Qt Multimedia's FFmpeg backend and H.264 at 30 frames per second.
It suggests the native source file's basename and lets the user choose a separate
destination. The native document never stores the video. Frames use the existing
deformation presentation and restore the initial frame after capture. The encoder
finishes a temporary file in the destination directory before QSaveFile atomically
publishes it; failure leaves an existing destination untouched.

Generated mesh edges are shown independently of the ordinary shaded CAD display
mode, exclusively while their FEM Body is active and mesh display is selected.
Activating a material Body removes this analysis-only presentation. Reactivating
the FEM Body restores the same calculated mesh and toggle state.

## Windows verification

`ZIMA_VERIFY_FEM_BODY_ONLY=1`, `--verify-startup`, the Windows Qt platform plugin
and the FFmpeg backend passed with exit code zero. The final isolated run was
`build/fem-20261124/fem-run-46b0cf4c27b8440fadb7dc4301571fa6`.

- Repeated material/FEM activation, actual shaded framebuffer mesh edges,
  unchanged ordinary display mode, mesh packet and CAD calculation count.
- Direct result-button ordering, availability, exclusive quantity choices,
  checked states and result colors; five languages and both themes.
- Player placement, timeline endpoints and midpoint, actual Play/Pause icons,
  held paused frames, magnification, duration, Cancel and shared middle-button
  confirmation. No CAD calculation or native document revision change.
- Actual Save video button and file chooser, source-name suggestion, a Unicode
  destination (`deformation-žluťoučký.mp4`), preserved camera and restored frame.
- Native playback through QMediaPlayer/QVideoSink: three-second H.264 video,
  30 fps, at least 85 decoded frames, and a changed peak-deformation image.
- Deliberate capture failure preserved an existing destination byte for byte.
- The existing complete FEM GUI matrix also passed: placement, second Origin
  references, conditions, projected/planar mesh regions, linear/quadratic mesh,
  static solve, pressure, thermal and combined analyses, measurement, cache,
  Undo/Redo and native save/reopen.

Distribution unit tests passed: 32 tests, two pre-existing skipped tests. The SDK
closure test now follows the dynamically loaded FFmpeg plugin through Qt
Multimedia and transitive codec libraries, using the selected SDK exclusively.

The complete translation coverage contract and FEM GUI language/theme matrix
passed after the final changes. Player time values use the UI locale's numeric
formatting.

## Remaining gates

The private run used the matching Qt SDK plugin directory. Deployment and MP4
export/playback from the final portable runtime still require verification.
Linux build, multimedia dependency closure and GUI execution remain unverified
and are recorded in `doc/LINUX_RELEASE_HANDOFF.md`.
Wrap is separate ongoing work and is not claimed complete here. The user removed
Unwrap from the agreed scope; the original planar Sketch remains editable.
