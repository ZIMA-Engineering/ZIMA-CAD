# Body derivation and result presence

## Model

Inputs are calculated source Body results, a positive uniform scale factor and
a center in Part coordinates. The means are the ordered Body dependency graph,
native geometry packets and explicit OCCT calculation. The output is a derived
Body with no editable modeling history. For factor k, independently verify
lengths by k, areas by k squared, volumes by k cubed and centroidal volume
inertia by k to the fifth power.

Body suppression is separate from feature suppression and visibility:

- A suppressed Body is absent from the final Part geometry and physical
  properties. Its current calculated geometry remains available to dependents.
- Suppressing a feature inside a Body continues to skip that feature.
- Visibility changes only display state and does not change physical properties.
- An active Body cannot be suppressed. A suppressed Body cannot be activated.
- A derived Body must follow its source; rejected reorder operations are atomic.

Scale takes the entire final result of its source Body, including Boolean results,
not a source history cursor or a copied list of features. It has only a source,
factor, center and result. It cannot be activated for modeling or own placement.
Changing its source geometry invalidates reuse of the derived result. Positive
uniform scaling preserves winding, normals and geometric side choices; new
reference identities retain recoverable source ancestry.

Whole-Body Mirror and Scale each retain their own transformed original-reference
database, with derived-owned face, edge and point identities. They do not copy
the source feature tree. Assembly placement resolves those persisted references,
including oriented planes, even when the source Body is suppressed. The native
Part stores this database; Assembly dependencies continue to reference native
Part files rather than introducing external reference caches.

Family Table uses its existing Body-presence yes/no cells for ordinary Bodies,
Mirror/Pattern, Scale, linked Bodies and Boolean results. It does not expose scale dimensions.
Suppressing a source Body in a variant does not suppress its features or remove
the geometry needed by a present derived Body.

## Commands

`body.suppress body=<id> suppressed=true` changes result presence.
`body.scale source=<id> factor=1.02` creates a derived Body at the Part-level
insertion cursor. Optional `center_x`, `center_y`, `center_z` coordinates are in
millimeters. Optional `body=<id>` edits an existing Scale with the same command.
Creation requires the Part-level editing context. Unchanged properties do not
calculate geometry or create an Undo entry.

The GUI uses the shared internal properties window, reference-entry cell and
independent inspection eye. Preview consumes existing viewer geometry; only OK
calculates and commits. Cancel discards pending parameters.

## Linked Bodies

The Part root context menu offers **Insert Body from Part** in the Part-level
editing context. Choose a native `.prtz` file, then a Body from its available
results. The same internal Body Properties window handles creation and editing.
The inserted Body has its own Origin and consumes the existing shared Body
placement controls. Its initial five references attach it to the target Part
Origin. It cannot be activated for feature modeling and has no copied history.
It can be selected as a Boolean tool, including subtraction for a mold cavity.

`body.link file=<path> source_body=<id>` exposes the same transaction to the CLI.
Optional `body=<id>` edits a link; `source_document=<id>` identifies a native
source or already evaluated family member. The GUI chooses Bodies from the
selected source document; it does not add a separate Family Table editor.

The link stores source document and Body identities, the source native path,
and an immutable calculated geometry packet inside the target `.prtz` file.
Its reference database is locally owned, with `link:from:` ancestry retaining
the original owner and semantic key. No required sidecar file is created.
Save/reopen uses the embedded result, including when the source is unavailable.
Explicit target Part regeneration refreshes the link from the source's current
calculated Body. Open source documents are authoritative, including unsaved
geometry edits. Switching tabs does not refresh links or invoke OCCT. Regenerate
the source Part first if its own upstream dependencies need refreshing.

Direct and transitive document cycles are rejected before insertion or refresh.
An unavailable or invalid source fails without committing partial changes.
Native source rename rebases link paths in current and Undo/Redo states.
Unchanged link properties produce no calculation or Undo transaction.

## Persistence and verification

Part INI format 45 / serialized packet format 69 stores Body suppression,
Scale parameters and native Body links. Older Part formats are intentionally unsupported. Start Part
and Skeleton templates use this format. `zima_refresh_start_templates` rewrites
and reopens the empty native start templates through the current serializer.

Regression targets are `zima_cpp_multibody_contract_tests`,
`zima_cpp_body_command_tests`, `zima_cpp_body_scale_tests`,
`zima_cpp_family_table_tests`, `zima_cpp_derived_copy_contract_tests` and
`zima_cpp_body_scale_ui_contract`. Test existence is not a verification claim;
report actual run results separately.

### Initial Scale verification, 2026-09-27

The local C++ GUI and CLI targets built successfully with one compiler job.
The empty native start templates were rewritten and reopened with the current
serializer. These tests passed:

- `zima_cpp_multibody_contract_tests`
- `zima_cpp_contract_tests`
- `zima_cpp_derived_copy_contract_tests`
- `zima_cpp_translations_contract` (catalog coverage and all five languages)
- `zima_cpp_body_scale_tests`
- `zima_cpp_family_table_tests`
- `zima_cpp_history_command_tests`
- `zima_cpp_body_command_tests`
- `zima_cpp_body_properties_tests`
- `zima_cpp_body_scale_ui_contract`
- `zima_cpp_family_rename_ui_contract`
- `zima_cpp_new_document_options_ui_contract`

The Scale tests include a source cursor before its features, exact scaled
measures, source edits, source suppression, Family Table variants, STEP output,
Undo/Redo, no-op properties, native persistence and Assembly insertion. A
dedicated case verifies both whole-Body Mirror and Scale original planes after
source suppression and native Part/Assembly save/reopen, including actual plane
mating with both Flip choices and both signed zero offsets. GUI checks cover
source replacement, independent inspection of suppressed source geometry,
Cancel, resizing, occurrence-specific picking and middle-button confirmation.
The GUI screenshot was inspected. Tab close-button placement and azure insertion
cursors were also checked; these visual changes introduce no localized text.

Additional regression runs are not clean. Their baseline has not been
established, so they must not be described as verified pre-existing failures:

- `zima_cpp_body_reference_command_tests`: Body placement reference unavailable.
- `zima_cpp_nested_body_sketch_ui_contract`: calculated boundary fingerprint mismatch.
- `zima_cpp_family_table_ui_contract`: Drawing source-list/order assertion.
- `zima_cpp_application_tools_ui_contract`: Modeling Insert commands assertion.
- `zima_cpp_derived_copy_ui_contract`: exceeded its 120-second limit after the
  nested Mirror/Pattern cases. Its earlier stale-source assertion was corrected
  to compare with the current open source rather than the initial fixture.

### Linked Body verification

The final Windows format-45 GUI, CLI, template-refresh utility and selected test
targets built successfully with one compiler job. The CLI help smoke check
passed. Start Part, Skeleton and Assembly templates were rewritten and reopened
with the current serializer; the new-document GUI check then passed.

All 17 selected tests passed after Body link integration: the 12 tests listed in
the initial Scale run above, plus:

- `zima_cpp_body_link_tests`
- `zima_cpp_body_link_ui_contract`
- `zima_cpp_document_dependency_tests`
- `zima_cpp_file_relocation_state_tests`
- `zima_cpp_document_operations_tests`

The link tests verify native source selection, locally owned original references,
five initial Origin references, actual translated mass properties, explicit
refresh from unsaved source geometry, no regeneration on tab changes, Boolean
subtraction, Save Copy, native persistence without the source file, atomic failure,
direct and transitive cycles, source rename through Undo/Redo, and Family Table
presence. The CLI test also verifies working-directory-relative source paths.
GUI tests cover selecting a scaled source, whole-Origin entry, replacement,
inspection, resizing, creation/editing, Cancel, middle-button confirmation over
the View and all five UI languages. The resulting dialog screenshot was inspected.

These results do not constitute a passing full regression suite. The broader
failures listed above remain separate findings; their baseline is not established.
All five were rerun against the final build and failed with the same reported
errors, including the 120-second derived-copy GUI timeout. No protected shared
placement implementation was changed to address these separate findings.
