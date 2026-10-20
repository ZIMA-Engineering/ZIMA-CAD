# Sheet Transition thickness inheritance

## Scope and ownership

The user approved a persisted **Custom thickness** switch on 2026-10-20.
Both the round-to-rectangular transition and the rectangular L/U transition
inherit the document sheet thickness by default. An enabled switch uses the
feature's own thickness. An unset document thickness uses the established
1 mm default, as Flat and Bend do.

Previously the creation dialog copied the document thickness once, and every
later calculation used that independent value. A document setting change could
therefore leave a transition at its original thickness.

`resolved_sheet_transition_parameters` now resolves thickness on a private
parameter copy. The calculation request, material regions, manufacturing bend
keys and disposable placement wire use the same effective thickness. The
authored feature and its disabled stored override are not rewritten by display
or regeneration. Existing inner radius, K factor, profile geometry, side choices
and shared container placement retain their independent definitions.

Creation and editing use the existing internal `SheetTransitionDialog`. The
unchecked thickness field displays the inherited value and is disabled. Enabling
the switch starts from the displayed effective thickness, matching Flat's
interaction. Disabling it immediately displays the document value. Only explicit
custom thickness is offered as an editable feature dimension to Family/Relations
and inline editing. The shared translation key is present in cs/en/de/fr/ru.

The existing command lifecycle is retained: opening rolls back to input history,
parameter edits publish a disposable wire without OCCT, OK calculates and commits,
and Cancel restores the model. The established origin-display-only path also
handles unchanged OK without calculation or an Undo transaction. No shared
placement implementation, kernel dependency or algorithm precision was changed.

The GUI review exposed an existing palette-polish issue: an open Properties
stylesheet retained the preceding theme while the application changed themes.
`apply_application_appearance` now reapplies existing Properties styles along
with toolbars. This does not change placement, layout or confirmation behavior.

## Persistence and factory inventory

The native `sheet_transition` object requires a boolean `thickness_override`.
There is no legacy fallback or required sidecar. Extensions and other feature
contracts are unchanged. The approved scope does not convert custom projects.

`build/transition-thickness-inventory.json` records a scan of all 1,210 tracked
native assets, including factory libraries and test fixtures: none contains a
Sheet Transition. No factory model requires conversion. The existing Part,
Skeleton and Assembly start templates must still be regenerated and reopened
with the changed implementation, followed by actual GUI new-document checks.

## Verification record

Windows Release with Qt 6.11.0 and OCCT 8.0.0 passed:

- The focused three-mode thickness matrix: round, rectangular L and rectangular
  U. It checks inherited and unset defaults, successive document changes,
  independent overrides, returning to inheritance, actual valid solids and
  volume changes, original face identities, exact preview coordinates and
  material thickness definitions. Family and Relations drive only an enabled
  override; a disabled driver cannot change the Part. No-op OK dispatches zero
  calculations and leaves the session generation unchanged. Undo/Redo,
  Unbend/BendBack, save/reopen and cold regeneration pass in every mode
  (`transition-thickness-analysis-tests.log`, 26.31 s for the focused matrix).
- The existing complete transition command regression, including profile
  editing, rotated L/U transitions, original references, manufacturing reliefs,
  DXF, sheet states, attached Bend and native persistence
  (`transition-thickness-ui-tests.log`, 150.87 s).
- The separate transition attachment regression
  (`transition-thickness-final-tests.log`, 223.97 s).
- Actual GUI creation, whole-Origin entry, inline placement, editing/reopening,
  Cancel, repeated thickness switches, Undo/Redo, OK and middle-button
  confirmation, Save and sheet states. Unchanged OK dispatches zero
  calculations. A live Light -> Dark -> Light sequence retains normal XYZ
  field heights, matches the actual application palette and performs zero
  geometry calculations (final GUI run 20.90 s). The light/dark screenshots
  under `Projects/test/transition-thickness-*.png` were visually inspected.
- Source/catalog coverage and all five actual GUI languages (12.87 s), plus
  the existing mm/cm/m/in and deg/rad field contract (16.92 s).
- All six native Part/Skeleton/Assembly templates regenerated and reopened
  unchanged. Actual template GUI (12.09 s) and new-document options GUI passed
  with their normal active editable context and commands. Template bytes did
  not change. The core native geometry/document contract also passed (5.97 s).

The old geometry test diagnostics assumed a raw single BRep at Part level and
read volume without explicitly requesting current analysis. They now validate
the actual immutable Body outputs and request the existing eager-analysis mode
only in these volume acceptance tests. A pending analysis is rejected instead
of being interpreted as measured zero. Undo comparison retains the established
monotonic dN allocation contract, while still comparing the complete authored
document; disabling an override must not recycle its dimension identity.

Logs named above are under `build/`. Native command test setup can take about
150 s with explicit analysis on this host; this is a verification observation,
not a command-interaction performance promise.

Native Linux execution and acceptance in a newly packaged Windows release
remain separate gates. This change does not claim verification of every
arbitrary profile, possible combination of manufacturing reliefs, material,
language and theme.
