# Properties content in limited window height

The shared `PropertiesSubWindow` keeps its title and confirmation controls
outside a frameless, resizable content scroll area. The existing content-layout
API is retained. Content keeps its layout minimum height rather than compressing
numeric editors below their text/style requirements. A tall form scrolls when
the owning application window cannot accommodate it.

Ordinary fields remain anchored at the top. Lower editable tables still receive
additional height where the command declares that behavior. In 3D Sweep, the
upper point table retains its existing minimum height while surplus space goes
to the lower profile table. The source-inspection column in Surfaces from Solid
allows the actual shared inspection button width, including its cell boundary.
These are presentation changes; shared container-placement solving, offsets,
reference selection and persistence are unchanged.

The native layout matrix checks numeric minimum heights, field anchoring,
meaningful lower-table growth, containment, confirmation-control access and
scrolling in tall Revolution properties. It covers 380 combinations of Part
dialogs, five languages and 1366x768 / 1920x1080 parent sizes. Use
`ZIMA_VERIFY_PLACEMENT_FIELD_HEIGHTS_ONLY=1` with
`zima_cpp_ui_contract_tests` to run it independently of unrelated rendering
checks. The complete UI suite retains those rendering checks. A focused layout
pass does not establish that the entire UI suite passes.

The initial reproduction compressed some numeric editors to 4–13 logical pixels
instead of their 21–22 pixel style minimum. The updated matrix reports zero
layout failures, including the explicit scroll-control assertion, in
`build/placement-field-height-scroll-final-check.log`. A further 380 combinations
pass with `QT_SCALE_FACTOR=1.5` in
`build/placement-field-height-scale-1_5-check.log`. Re-run the matrix after
subsequent layout changes. Windows verification does not establish Linux display
behavior or exhaustive coverage of all display scales.

## Default Feature initial height (2026-10-19)

The Default Feature's queued fitting callback formerly subtracted its parameter
viewport from the shared window's minimum hint. That hint intentionally excludes
most scrollable content, so the callback shrank an initially fitted window. The
reproduction opened a 477-pixel window with 533 pixels of hidden outer content
while the parent still had available height.

Feature fitting now reserves the natural parameter viewport, invalidates and
activates the content layout, and measures the complete window hint before
bounding it to the parent. Limited-height windows retain scrolling. The change
is local to Feature presentation; placement, geometry, dialog confirmation and
document persistence are unchanged. There is no new user-visible text.

The Feature type UI regression fails before the fix and passes after it,
including repeated type changes. The layout matrix now includes current Default
extrusion and revolution, for 400 combinations across all five languages and
both parent sizes, with zero failures at the normal scale. Evidence:
`build/feature-initial-height-before.log`,
`build/feature-initial-height-after2.log` and
`build/feature-initial-height-matrix-root.log`.
The same 400 combinations also pass at scale factor 1.5, recorded in
`build/feature-initial-height-matrix-scale1_5.log`.
The combined UI test's working directory is explicitly the repository root so
relative source paths load the real localization catalogues under CTest too.
