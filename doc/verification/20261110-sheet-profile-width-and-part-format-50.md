# Sheet Profile width, copy Origins, Fillet rails and native Part format 50 verification

Date: 2026-11-10
Platform: Windows, MSVC 18.8, repository OCCT 8.0.1

## Scope

This change replaces the Sheet Profile's special single-width fields with the
shared Feature side contract. A free Sheet Profile supports Forward, Reverse
and Symmetric direction, independent Side 1 and Side 2 lengths, and one Up To
point, plane or planar-face target per side. An edge-attached Sheet Profile
derives its complete width interval from the selected sheet edge and keeps the
attachment controls read-only.

The same correction batch completes three dependent reference contracts:

- an owned Sketch automatically displays the active Body Origin, while manually
  shown history/copy Origins remain selectable and persist as external Sketch
  references;
- Pattern and Mirror retain independent copied topology and copied-Origin
  identities through nesting, regeneration and native persistence;
- Fillet/Chamfer resolve Feature Start/End as one logical rail only when their
  exact persisted endpoint graph is a connected open chain, including an exact
  coplanar 180-degree return.

The authored Sheet Profile contract changes the native Part transport from
format 49 to format 50 and the internal serialized packet from version 73 to
74. No compatibility branch was added. The repository migration tool rejects
format-49 documents that contain a Sheet Profile because those documents need
an explicit authored conversion rather than a guessed width direction.

## Repository asset regeneration

The format migration staged every source separately before replacing any
tracked asset. It found and converted 1,058 Part-based native assets:

- 1,030 factory library `.prtz` files;
- four Part/Skeleton start templates;
- one drawing frame and one title block;
- ten native symbol definitions;
- twelve Part test fixtures.

No repository asset contained an authored Sheet Profile, so their authored
definitions required no width conversion. The migration removed stale derived
caches. The current Windows implementation then calculated, saved and reopened
all 1,030 library models, the frame, title block and ten symbols. The four Part
templates were saved and reopened with the current serializer. The twelve Part
fixtures were recalculated, saved and reopened separately. The Linux parity
fixture retained its final surface area `2242.5998362861433` and volume `0`.

After regeneration, the repository contains 1,058 format-50 Part-based assets
and no format-49 Part-based asset under `config` or `cpp/tests/fixtures`.

## Build and passing verification

A clean rebuild compiled 455 targets/objects and linked the application and
affected tests successfully. The normal `zima-cad.bat` development launcher
still resolves to the new `build/cpp-windows-release/zima-cad-cpp.exe`.

The final focused CTest run passed 9/9 tests:

- `zima_cpp_contract_tests`;
- `zima_cpp_derived_copy_contract_tests`;
- `zima_cpp_origin_display_ui_contract`;
- `zima_cpp_sheet_attachment_dialog_contract`;
- `zima_cpp_translations_contract`;
- `zima_cpp_native_documents_tests`;
- `zima_cpp_sheet_profile_tests`;
- `zima_cpp_sheet_profile_ui_contract`;
- `zima_cpp_flat_command_tests`.

After the Origin, logical-rail and metadata-reader corrections, the final
combined Release run passed 22/22 contracts. It includes the 18-test concurrent
matrix for Profile, derived copies, Origins, Sketch references, work planes,
Fillet calculation/query, native documents, the shared core contract and all
five languages; the complete monolithic `zima_cpp_ui_contract_tests`; the
format-50 `zima_cpp_metadata_command_tests`; and the direct batch plus command
edge-treatment tests. The previously observed monolithic UI crash no longer
reproduces. The metadata failure was traced to and fixed as a stale format-49
constant in the bounded parameter reader; the full loader and writer were
already format 50.

The Sheet Profile regression matrix covers Forward and Reverse width, two
independent sides, symmetric length per side, Up To point and plane, rejection
of a nonparallel plane, attached Bend/Flat route seeding, tangent incidence,
automatic thickness, removal of redundant endpoint/manual-placement buttons,
save/reopen and derived-copy behavior. The native-document tests create Parts
from the regenerated start template, verify a fresh active Body and preserve
template identity and parameters. The start-template multi-process GUI contract
also passed independently.

All five localization catalogs (`cs`, `en`, `de`, `fr`, `ru`) parse as JSON and
the translation coverage contract passed. No Rip/Nastřižení UI was introduced;
that feature remains only a separate design proposal.

## Known unrelated verification failure

`zima_cpp_workspace_startup_contract` completed its component-origin, import,
Save Copy, Unicode native-file and Body-placement checks, then failed the
pre-existing assertion that Tree/View feature menus follow the second active
Body. That menu activation issue is outside the Profile, Origin-reference and
Fillet route paths and was not obscured or converted into release evidence.

This failure is recorded rather than treated as a regression in the changed
paths. An entirely green repository-wide Windows suite is not claimed.

## Remaining platform and user-file gaps

Linux was not available in this Windows session. The shared source and OCCT pin
remain unchanged, but the regenerated assets and format-50 persistence must be
rebuilt and verified on the Linux host before claiming cross-platform release
completion.

External user documents were not modified. A format-49 user document containing
the previous Sheet Profile definition needs a deliberate one-file conversion;
the repository migration tool rejects it by design rather than silently changing
its geometry.

The user's `15.prtz` was opened and explicitly regenerated read-only with the
current CLI without a calculation error. Native Linux compilation, GUI behavior
and regenerated-asset acceptance remain for the Linux host; Windows results do
not establish them.
