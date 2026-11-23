# CAM machine editor and clamping capability boundary

Date: 2026-11-23. Windows native development checkpoint after `25d0425a4`.
This does not complete or deploy the CAM command UI.

## Machine properties

The new `CamMachineDialog` consumes the existing native Machine definition and
shared internal Properties window. It edits machine name/type, axis count,
XYZ minimum/maximum travel, maximum cutting feed, rapid feed and maximum spindle
speed. Travel describes the tool mounting point in machine coordinates. The
dialog identifies the currently supported calculation subset, 3-axis milling;
authoring another machine capability does not enable its unsupported generator.

Opening and unchanged confirmation preserve exact authored values, including
precision beyond the displayed decimal places. Changed fields preserve machine
identity, unedited parameters and the separate postprocessor identifier. Empty
names and reversed/zero travel ranges remain in the editor without committing.
Cancel discards changes. Shared middle-button double-click confirms from the
owning View; a short middle click does not. Compact numeric controls and travel
rows retain their dimensions when the window grows.

All new labels and diagnostics have cs/en/de/fr/ru catalog entries. Live language
changes retranslate an already open dialog, including an existing validation
message, while retaining user-authored names. Light -> Dark -> Light is covered
in each language. Offscreen Qt captures were inspected for Czech Light and
Russian Dark after correcting the redundant empty numeric-table viewport.

## Clamping behavior and current limit

The user clarified outward exclusion from a clamping Sketch, the upper Sketch
as the jaw-top boundary, and a warning when cutting stock below that boundary.
`CAM_BODY_DESIGN.md` now separates physical/exclusion collisions, below-jaw
material removal and grip-region removal. Holding-force certification is not
inferred from tool-tip height or geometric contact alone. The cited Autodesk
documentation supports the complete-tool fixture check; the Sketch/grip scheme
is the ZIMA-CAD design requirement.

The native implementation already tracked clamping Sketch references but did
not calculate their exclusion volumes. Calculation now explicitly rejects that
unsupported input before material work. Publication independently rejects an
unchecked result, even if its fingerprints match; retained unchecked results
cannot enter the next setup as accepted stock. Metadata authoring and dependency
tracking remain available. Explicit fixture-Body collision calculation continues
to work. This temporary gate must be replaced by verified oriented Sketch-region
checks when they are implemented, not removed to make the UI appear complete.

## Verification

Built with the existing MSVC/Qt 6.11/OCCT 8.0.1 Windows development configuration.
The following eight CTest targets passed together in 17.60 seconds with four
workers (an execution observation, not a performance comparison):

- `zima_cpp_cam_program_tests`
- `zima_cpp_cam_document_tests`
- `zima_cpp_cam_sweep_tests`
- `zima_cpp_cam_simulation_tests`
- `zima_cpp_cam_operations_tests` (47 assertions)
- `zima_cpp_cam_calculation_tests` (48 assertions)
- `zima_cpp_cam_machine_dialog_tests` (485 checks, Qt offscreen)
- `zima_cpp_translations_contract` (source coverage and five-language catalogs)

The calculation regressions retain the independently checked 1200 -> 1000 ->
800 mm3 two-setup example, different work frames, exact save/reopen packets,
cancellation, fixture collisions, source immutability and unchanged design-Body
calculation count. New cases reject unchecked Sketch clamping at calculation and
publication without a document revision, and prevent downstream stock reuse.
The metadata regression retains suppression/Undo, in-flight result rejection
and actual source-geometry invalidation.

Local logs/captures: `build/cam-20261123/machine-clamping-tests.log` and
`build/cam-20261123/machine-<language>-<theme>.png` (disposable test artifacts).

## Remaining gates

The machine editor is a tested component; main-window CAM setup integration,
tool/operation editing, NC pane/stepping and oriented clamping-region simulation
are unfinished. The offscreen widget tests do not replace the required complete
GUI workflow, template/factory-library verification or Linux verification.
No native format or protected placement-solver change was made in this step.

The normal `zima-cad.bat` executable was not replaced. Its SHA-256 remains
`FD192739063610FE0CF7406DA63FF349B79829E6A26BEF68221B5F86CC1BF3B9`, the previously
verified Fillet build. No portable release was created or published.

Follow-up: [2026-11-24 setup UI verification](20261124-cam-setup-ui.md) records
main-window integration of the machine editor, source/frame selection and
translucent stock. The remaining tool/operation, NC and clamping gates still apply.
