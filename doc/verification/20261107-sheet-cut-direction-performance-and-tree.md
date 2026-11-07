# Sheet Cut direction, terminal-history performance and Tree scrollbar

## Scope

This correction keeps Sheet Cut on its existing legacy Extrusion storage while
bringing its Up-to interaction into the same direction contract as the current
Modeling Feature. The forward branch owns the shared direction and turns toward
an unambiguous target on the opposite side of the resolved pending Sketch plane.
The reverse branch never changes that shared direction. Two Up-to branches in
the same unambiguous half-space are rejected before a transient invalid preview
can be calculated.

The target classification uses the pending container, placement references,
owned Sketch and profile-plane offset. It does not classify against a stale
committed frame. Symmetric extrusion remains outside the two-branch rule. No
protected container-placement solver or reference-entry contract is changed.

The reported long Sheet Cut calculation in `02.prtz` was not spent in the final
cross-bend cut. The final operation itself took about 0.1 seconds; the delay came
from replaying the unchanged preceding history after reopening the document.
The native evaluator now restores a verified persisted B-Rep checkpoint for an
unchanged retained prefix before calculating a changed terminal suffix. Reuse is
accepted only when the complete persisted face, edge and vertex bindings are
available and the suffix does not require historical original-edge ancestry, a
copy source, sheet-state reconstruction or an external dependency. A later
operation that needs unavailable historical ancestry forces conservative replay.

The exact already persisted top-level prefix packets are retained byte for byte
when their local source fingerprint and error state are unchanged. Live reference
coverage required by the changed suffix remains in the final calculated result.
Profile-frame and resolved-reference reuse is shared by Modeling Feature, legacy
Extrusion and Revolution, but only for an unchanged frame/projection definition
with no downstream reference consumer.

The main document Tree now lays out its vertical scrollbar against the actual
Tree viewport. Its track begins below the document header and ends with the
viewport instead of occupying the complete widget height. The shared Part,
Assembly and Drawing Tree receives the same behavior; unrelated list and File
Manager widgets are unchanged.

## Verification completed

- `zima_cpp_feature_type_ui_tests` passed on native Windows. It covers Sheet Cut
  forward direction reversal and restoration, same-side rejection, opposite-side
  acceptance and the rule that the reverse branch does not turn the shared
  direction.
- `zima_cpp_feature_ordered_up_to_ui_contract`,
  `zima_cpp_feature_ordered_up_to_native_tests`,
  `zima_cpp_feature_up_to_parameters_tests` and
  `zima_cpp_ordered_up_to_kernel_tests` passed.
- The exact loaded-document terminal-edit probe passed five cold runs of the
  reported `02.prtz`: 218.571, 180.817, 174.576, 190.873 and 194.052 ms. Every
  run retained all 31 history boundaries, kept the preceding 30 serialized
  boundaries byte-identical and produced output SHA-256
  `c0666f9b379a008a1d713315543ef65997ad715ee5c86df2073c73d4c7099845`.
- `zima_cpp_history_fingerprint_tests`,
  `zima_cpp_history_recovery_tests`,
  `zima_cpp_history_deletion_tests`,
  `zima_cpp_history_dependency_layer_tests`,
  `zima_cpp_direct_batch_edge_treatment_tests`,
  `zima_cpp_occt_edge_treatment_support_tests`,
  `zima_cpp_metadata_command_tests`,
  `zima_cpp_edge_treatment_command_tests`,
  `zima_cpp_profile_command_tests` and
  `zima_cpp_profile_frame_ui_contract` passed.
- Cross-bend Sheet Cut behavior remained covered by
  `zima_cpp_sheet_cut_wire_preview_tests`,
  `zima_cpp_sheet_cut_skin_to_skin_contract` and
  `zima_cpp_sheet_cut_skin_ui_contract`.
- The dedicated Windows Tree geometry gate forced a vertical scrollbar and
  verified that its top and bottom exactly match the nonzero viewport bounds.
- The five-language localization contract passed. This correction adds or
  changes no user-visible text, so the Czech, English, German, French and Russian
  catalogs require no new entries.

## Boundaries

The user-facing Sheet Cut name is retained because the command owns sheet-aware
cross-bend and unfold behavior; only its shared profile-operation interaction
and calculation paths are aligned. Deleting a solid that supplies an Up-to
target remains an explicit manual repair case for this release.

There is no native document-format, start-template, factory-library, protected
placement, geometry-side, Qt, OCCT or dependency-version change. The same C++
implementation is used by Windows and Linux. Native Linux compilation and
execution remain a separate host acceptance gate.
