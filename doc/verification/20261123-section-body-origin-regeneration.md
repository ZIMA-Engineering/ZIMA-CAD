# Section Body Origin references during Part regeneration

## Defect and correction

At baseline `ef0991c95`, a Section placed on a material Body's Origin planes
was valid after creation or Properties confirmation but became invalid after
Part Regenerate. Drawing Regenerate then rejected its unavailable placement.
Recreating the Section did not avoid the defect.

Section creation/editing already included `body_origin_reference_geometry()`
in its reference source. The Part regeneration lifecycle assembled the document
Origin, construction geometry and history-container Origins, but omitted Body
Origins. The correction adds the same existing Body Origin packet at that
lifecycle boundary. Shared placement solving, reference entry, side identity,
native formats and geometry accuracy are unchanged.

An existing Section incorrectly persisted with `reference_valid=false` recovers
on explicit Part Regenerate when its actual references still exist. A genuinely
missing Body Origin remains invalid and retains the last valid frame; it cannot
be used for a new Section calculation.

Deleting and recreating A-A is a separate identity change. The current source
Part then owns a new Section ID while an existing Drawing can still refer to
the deleted ID. This correction does not silently match Sections by name.

## Windows verification

The new command regression failed before the correction with
`Part regeneration lost existing Body Origin Section references`. The real
50 Part fixture also failed with `Real Part regeneration left its Section invalid`.

After the correction, the command regression covers three Body Origin planes,
separate FRONT/TOP references, nonzero placement offsets, repeated regeneration,
unchanged identities and cut area, solid volume, reference editing Undo/Redo,
native save/reopen, recovery of a stale invalid flag, and genuine reference loss
with retained frame and rejected calculation.

The real-file Qt harness loads `50.drwz` together with the earlier read-only
`build/fillet-perf-followup-20261123/50-saved.prtz` fixture that retains its original
Section ID. It explicitly regenerates the Part, saves/reopens a temporary native
Part, then invokes the real Drawing Regenerate action. Mouse picking creates
the 20 mm web dimension in both selection orders, with Cancel, repeat entry,
Undo/Redo and native Drawing save/reopen. The original fixture failed before
the fix and passed afterward. Drawing regeneration and dimension interaction
perform no Body calculations.

The current user Part has a newly created A-A with a different identity. A
second Qt run loads that exact Part and the existing Drawing, regenerates the
Part, opens the Section view's ordinary Properties, selects the new A-A, then
updates the parent view's Section trace in Properties. Repairing the child
first is necessary because parent edits refresh their dependent views. Drawing
Regenerate and the same 20 mm mouse/Undo/native-reopen checks then pass. Existing
view IDs, scale and dimensions are retained. Position checks allow the existing
Properties fields' 0.001 mm resolution; exact unrounded Drawing position
preservation through that dialog is not claimed by this test.

Ten CTest targets passed: `section_reference_command_tests`,
`section_property_command_tests`, `section_contract_tests`,
`section_measurement_tests`, `measurement_picker_tests`,
`measurement_dimension_contract_tests`, `drawing_dimension_command_tests`,
`drawing_view_command_tests`, `measurement_dimension_ui_contract` and
`drawing_parallel_ui_contract` (all with the `zima_cpp_` prefix).

Localization review found no new or changed UI text. The separate
`zima_cpp_translations_contract` passed for the five supported catalogs.
The correction uses shared C++; Linux execution remains unverified on this
Windows host. No Sketcher equations or native serialization contracts changed.

Disposable before/after logs are under `build/drawing-section-20261123/`.
User project documents are read only; test saves use temporary files.

The candidate desktop executable passed factory-template creation, solid
calculation, native save and rendered-View startup checks. The existing Fillet
preview test also passed: 16/16 strokes, 10/10 labels, zero coordinate difference
and Cancel restoration. Candidate SHA-256:
`6B76A2AC1B8DCC1B764F07E84D9968DDB4EAC440D4D92742E7B0C4ADE7D1B1E6`.

## Local deployment

After the user closed the running application, the verified executable from
deployment commit `03d20c0ff` replaced `build/cpp-windows-release/zima-cad-cpp.exe`.
The previous executable was backed up and its hash verified first. The installed
hash matches the candidate above, and its factory-template/solid/native-save/View
startup check passed. The root `zima-cad.bat` entry point remains unchanged.

The fix was also cherry-picked as `2f65c87c8` onto `codex/sketcher-mechanisms`,
preserving the separate unfinished CAM work. This local executable contains
the isolated Drawing/Section fixes, not that unfinished CAM work. This is a
local development deployment, not a new published portable release.
