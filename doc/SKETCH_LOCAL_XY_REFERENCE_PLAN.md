# First-face local XY mapping

## Agreed intent and prototype scope

On 2026-10-21 the user confirmed that the camera already faces the desired side.
The discrepancy is the actual local XZ frame of the face-attached feature, with
its Z axis pointing down. The user requires the Sketch and feature Origin to
show one genuine coordinate system; an independent cosmetic Sketch coordinate
overlay was explicitly rejected. Existing model geometry and Drawing behavior
must be protected. The user authorized an isolated first-face prototype before
the complete mapping is rolled out.

The first prototype covers automatic first-plane entry in the ordinary Sketch
and profile Properties dialogs. An unplaced profile consumes the existing TOP
orientation equation: the picked plane normal drives local Z, and the profile
lies in the feature's real local XY plane. Replacing an already authored plane
retains its explicit orientation role. No common FRONT/TOP axis definitions are
changed. Explicit manual work planes retain their meaning.

## One frame policy and real geometry

`sketch_placement.hpp` owns the automatic plane derived from the primary
reference's actual orientation role. TOP/BOTTOM selects XY, FRONT/BACK selects
XZ and LEFT/RIGHT selects YZ. A complete Origin retains its authored work plane.
The same policy is consumed by the document resolver, profile/Sketch commits,
Properties previews and Sketch entry. Later orientation references must not
replace the primary work plane merely because they include a FRONT equation.

The existing normalizer must retain authored orientation equations and their
mirrored table entries rather than rewrite TOP as FRONT. Reference identity,
flip, signed zero, offsets and orientation-only identity remain intact. The
prototype uses existing persisted fields/roles; no schema member or format
version is added. No model conversion or factory rewrite is performed.

## Lifecycle and verification

Only the normal reference-entry/Properties path constructs the pending pose.
Sketch entry, preview and final calculation consume that pose. No independent
viewer transform, duplicate modeling algorithm or relabeled Z axis is introduced.
Placement previews continue using the existing shared calculation and wire
paths; explicit geometry calculation remains separate from hover and selection.
The dialog's candidate-preparation policy runs before both hover validation and
confirmation. Other container dialogs retain their existing preparation through
the default implementation. A profile must not validate a FRONT candidate and
then confirm the same face as TOP.

The native first-face matrix passes 48 cases: XY/XZ/YZ supports, both source
sides, Back states and four quarter-turn states. Each verifies matching real
Sketch/feature axes, signed offsets, calculated Extrusion volume and native
save/reopen. It also verifies that normalization retains explicitly authored
FRONT and TOP roles. The existing complete-Origin/manual-plane/history native
matrix passes. Evidence: `build/profile-first-face-xy-native.log`.

The cap GUI matrix checks real Solid/Thin start/end faces through the ordinary
reference-entry handler, matching feature/Sketch XY axes, camera orientation,
return to Properties and unchanged source document on Cancel. These handler
checks do not claim a separate mouse-search regression for every source cap.
Native title-block, symbol and localization checks accompany this build.

The Windows isolated build passes the four CTest gates for Sketch entry, native
work planes, title blocks and symbols (71.93 seconds total). The existing rolled
L GUI sequence also passes both selection orders in ordinary and universal
dimension entry, successive radius changes, leg drags, save and Undo/Redo. That
sequence protects existing editing behavior; it is not the complete dimension
matrix for a newly TOP-attached profile. All five language coverage checks pass.
Evidence: `build/profile-first-face-xy-gui.log`,
`build/rolled-l-preview-guard-ui.log` and
`build/camera-localization-coverage.log`.

The initial broader return-frame run rejected its first three-plane fixture.
That fixture hard-coded FRONT/TOP/TOP before first-plane entry; converting the
first plane to TOP therefore supplied three TOP equations. The fixture now uses
the same automatic role assignment as the actual picker for each successive
plane. The owned-profile fixture uses that same assignment. The axis,
rotation-lock, persistence and return assertions remain intact. The corrected
Sketch/Holes/Flat/Bend matrix passes all 48 create/edit scenarios with two
successive Sketcher return cycles per scenario, native save and Properties
reopening (`build/profile-first-face-xy-return-frames.log`). The finalized native
matrix also passes (`build/profile-first-face-xy-native-final.log`).

After integrating candidate preparation, the owned Extrusion/Revolution frame
matrix passes again, including eight orientation states, offset dimensions,
OK/Cancel and the existing other Sketch hosts. The final camera/entry run passes
all six ordinary/rotated-Body cases and all four Solid/Thin start/end cap cases.
It additionally checks that candidate preparation and confirmation author the
same TOP role. Evidence: `build/profile-first-face-xy-owned-frames.log` and
`build/profile-first-face-xy-entry-ui-final.log`. The 1000 Part source SHA-256
remains `351C173F9B5B552BFBDB3CB72D912E681F2299929F36E597C5BDF0FB34E9F685`;
the rolled-L sequence uses its private baseline copy.

The read-only factory inventory examines 1,035 Part files and 5,436 authored
history definitions: 1,023 first planar references are explicitly FRONT, 28
explicitly TOP and 4,385 definitions have no positional plane. This is an authored
role inventory, not regeneration certification. The 28 TOP cases require
individual review before general acceptance. Inventory:
`build/profile-plane-reference-role-inventory.json`.

## Remaining rollout work

The command/CLI reference authoring defaults, second/third reference ordering,
replacement/removal with multiple references, curve and sheet attachments,
derived Sweep section frames, repeated/nested Assembly occurrences and the
complete dimension/drag/Undo/Redo matrix require verification before this is a
general production convention. The first-face handler prototype is not a claim
that these dependent cases have all been converted or certified.

Keep the Drawing right-to-left frame/title-block convention and existing world
geometry. Drawing rendering itself is not changed. No kernel version, precision
or persistent geometry-side contract changes. Sources remain shared by Windows
and Linux; Linux execution is pending. There are no new product-facing strings;
existing XY/FRONT/TOP identifiers and all five catalogs are reused.

The isolated build is `C:/zcb/reference-next`. The ordinary `zima-cad.bat` and its
development executable are not replaced. Manual acceptance starts with a new
test feature or private copy, not a factory conversion.
