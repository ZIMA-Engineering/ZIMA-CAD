# Render group and Section command, 2026-11-24

The Part Tree contains one Renders group directly below Sections. Each Render
keeps its existing native Body identity, source selection, cameras, lights and
saved gallery. Group context creation uses the existing Render Properties dialog
and the ordinary Part-root activation path. No document schema or shared
placement change is involved. Assembly Render remains future work.

Section creation has moved from the View toolbar to the right command panel at
Part Body-list level and in the Assembly workspace. It uses the existing Sections
Tree icon. The Render group, definitions and creation action use a camera icon.

The follow-up fixes the missing Render button at Part Body-list level: the
existing action is now actually added to the command toolbar. Section creation
is labelled `Create section` (`Vytvořit řez` in Czech), and the adjacent Render
action is `Vytvořit render` in Czech. The plural Tree groups remain unchanged.

## Windows verification

- Full Render GUI regression passed: source selection, object placement, unchanged
  OK/Cancel, camera Tree context action, image computation/export, background
  snapshots, gallery transactions, save/reopen and all five languages.
- Two independent Render definitions were created in the same group; creating
  the second preserved the first. Undo/Redo and GUI save/close/reopen retained
  their distinct identities and grouped presentation.
- Group collapse survives scene refresh with unchanged revision and zero CAD
  calculations. Moving rows preserves expansion, selection and current-item
  state; the original camera context test caught the first draft's lost expansion.
- Full Section GUI regression passed, including Part-root and Assembly command
  location, placement and selected work plane, Sketch editing, Cancel, middle
  button confirmation, Drawing hatches/PDF, Undo/Redo and repeated components.
- The CAM GUI regression passed 204 checks again after grouping Render definitions.
- The new group label is translated in cs/en/de/fr/ru and checked after rebuilding
  the Tree under each language. The Section command label is translated in all
  five catalogs; shared Render creation keys are reused.
- The follow-up GUI test clicks the actual visible Render toolbar button,
  verifies both command buttons and their labels under all five languages, and
  records `render-section-commands.png`. Full Render and Section GUI suites pass.

Evidence is in `build/cam-20261124/`, including the Render group screenshot,
section fixtures and per-suite logs. Tests use isolated documents and do not
modify user Parts or Drawings. Linux GUI execution is unverified.
