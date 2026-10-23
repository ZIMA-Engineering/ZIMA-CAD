# Native Sketch selection wire

Status: actual-model native and two-theme GUI checks pass; focused shared-viewer
regressions and portable acceptance are recorded separately below. Shared C++,
Qt 6.11.0 and OCCT 8.0.0. Native Linux execution is not established here.

## Reproduced cause

The protected `2000-kontrola-20261023.prtz` copy has SHA-256
`48fc65570314bb7e7dc84cc706ba5ff774ec67108256335f5980697d4500768f`.
The user's 17:35 screenshot showed the native interpolating curve and a distinct
thin selected curve. The native regression reproduced 129 native display points
against 27 points on the first selected cached edge, with additional selected
curves. The pure Sketch had entered the calculated-material contribution path.
That datum/profile packet is not its authored visible Sketch definition.

A second path selected the explicit Sketch child using the old whole-Sketch
painter. Its `sketch` candidate bypassed the cached authored-wire renderer and
could draw a thick curve. Ordinary View confirmation also mapped Modeling
Sketch sources to their parent while omitting the Sketch Feature role.

## Correction

Pure Sketch containers use their native `Sketch::viewer_mesh()` profile and the
same clean-profile predicate as ordinary inactive Sketch display. They do not
read calculated operation input to reconstruct a selection curve. The existing
`shows_sketch()` capability includes standalone Sketches, Sketch Features and
Modeling Features with inactive sides. Explicit Sketch child selection resolves
the same native profile and Body/occurrence frame.

The viewer treats that native source as authoritative, including an unavailable
or empty source. Ordinary parent and child selection share the cached GPU wire.
The base Sketch painter and older whole-Sketch painter do not draw another
stroke over it. Active Sketcher entity interaction retains its existing path.
View confirmation maps a pure Sketch source to its owning feature; direct
Tree-child selection retains the explicitly selected child.

Native interpolation, exact curve-plane intersection equations, body calculation,
stored reference supports, placement and document formats are unchanged. The
actual exact intersection-source spline agrees with all native display samples
within 1e-7; the reported divergent highlight was not produced by a new
intersection formula. No new user-visible text is introduced; localization is
reviewed through the existing five-language contract.

## Finite Windows evidence

- The actual-model comparison now returns the same single 129-point curve for
  both the owning feature and explicit Sketch child, without document changes.
- Forty-eight native cases cover standalone/current Sketch roles, Modeling with
  inactive sides, open/closed control and interpolating splines, XY/XZ and placed
  Body frames. Three successive free-point changes, native Sketch readback and
  Undo/Redo preserve the exact displayed/selected point arrays. Deliberately stale
  calculated support geometry is never substituted for the native source.
- Full-window mouse hover, LMB, Tree parent/child selection and double-click
  inspection pass on the actual protected copy in both themes (3.347 s light,
  3.315 s dark). View/Tree retain the owning feature. The selected GPU wire has
  256 stroke vertices, exactly 128 native segments; repeated repaint and
  inspection do not upload it again. Selection changes neither document state
  nor the base-scene revision and fills no faces. Native points are compared
  independently with the displayed profile; the final framebuffer was reviewed.
- User model and private settings hashes remain unchanged. Evidence is under
  `build/feature-five-drag-review/sketch-wire-*` and
  `build/curve-plane-review/gui/sketch-wire-*`.

The existing 19-suite development checkpoint predates this narrow selection
follow-up and is not relabelled as a later rerun. Focused GPU/selection, Thin-wire,
curve-reference and translation regressions must pass after the changed viewer
is linked. The portable package must include and run the self-contained native
spline selection scenario. Broader nested/repeated Assembly Sketch interaction
and every possible curve/dimension combination are not established by these
finite cases.

## Visible azure follow-up

The user subsequently reproduced an invisible confirmed selection after opening
the document. The source GPU batch was correct, but the later native Sketch
painter repainted the original foreground stroke over it. Its skip predicate
used `highlighted` after that candidate had intentionally been cleared for the
authored-wire path. It now consumes the retained ordinary preview candidate.
Both hover and confirmation retain the existing shared azure `#00D1FF` and
ordinary one-pixel wire; the shared palette is unchanged.

The GUI regression now examines actual framebuffer pixels beside the offered
curve, including beyond its endpoint marker, and checks azure both on hover and
after confirmation followed by moving the pointer away. A synthetic spline and
the protected actual document pass (3.367 s synthetic, 3.551 s light, 4.775 s
dark). The same tests retain exact native point-array, Tree ownership,
double-click, document-revision and unchanged-GPU-upload checks. Evidence:
`build/sketch-selection-color-gui.log` and the `sketch-wire-*` GUI folders above.
No UI text, native geometry, solver equations or persisted fields change.

The focused viewer regression also updates an obsolete test expectation:
ordinary Reference offers certified planar face supports under its existing
stable-geometry predicate. Uncertified faces remain rejected. This changes the
test to match the already implemented contract, not the product selection rules.

All four focused suites pass after linking: shared viewer, Thin Feature wire,
native Sketch profile wire and five-language translations (24.71 s total).
Distribution source/archive tests pass 13 applicable cases with two non-Windows
cases skipped; all 16 signing-input/publisher tests pass. Logs are
`build/sketch-selection-color-regression.log`,
`build/release-2301-package-tests.log` and
`build/release-2301-publisher-tests.log`. Portable acceptance remains a separate
gate on the exact committed candidate and signed archive.
