# Exact derived Sketch references, independent instances and transition selection

## Scope

This correction covers two related user reproductions:

- the last 2D Sweep path endpoint in `06.prtz` could not retain two coincident
  constraints at the intersection of external references;
- opening another native document from Windows Explorer was rejected when a
  different ZIMA-CAD process already used the same project directory.

It also verifies exact reference identity with multiple parents, multiple copied
children, chained Mirror/Pattern-style descendants, later Boolean fragments and
changes to both source geometry and the dependent feature Origin.

The same release candidate repairs ordinary material-face selection for
compound sheet transitions and restores their shared double-click parameter
inspection.

## Reference identity and reprojection

The selection path previously validated a copied result face through its oldest
available analytical source and then stored that older identity. In a mixed copy
and parent reference this could combine geometry from different branches. A
subsequent refresh correctly detected the inconsistent equations and rejected the
second coincident constraint.

The clicked derived face is now the persistent Sketch reference. Current display
triangles supplement original-reference geometry only for that requested child.
A later Boolean fragment may follow its encoded Boolean parent chain back to the
stored child, but Mirror/Pattern ancestry never substitutes an original or a
sibling copy. Analytical plane/cylinder support can be inherited without changing
the stored identity.

All copied geometry passes through the current source, occurrence, dependent-Part
and Sketch frames. Repeated parent edits and feature-Origin translation/rotation
therefore reproject the same child rather than retaining a static point set.
Broken sources retain their last valid reference geometry under the existing
repair contract.

The authored `06.prtz` contained an older inconsistent cached face identity. It
was rebound to the source face matching its persisted geometry, regenerated and
saved through the native C++ path. The original is retained separately as
`06.before-reference-fix.prtz`; neither user file is part of the repository or
release archive.

## Process isolation

Instance reservations now protect open native files, not project directories.
Separate processes may work in the same directory and may open different files
there. A second process still cannot edit the same native file. Explorer/document
association launches remain separate processes, while File -> Open and the
integrated File Manager continue to add documents to the current process.

## Sheet-transition selection

Each transition retains exact panel and bend region identities for sheet
references, Unbend/Bend Back and manufacturing operations. Those compound
regions now carry the parent transition only as read-only display ownership.
Ordinary View selection therefore maps a visible material face to the authored
`SheetTransition` Container, draws its complete command preview wire in azure
without filling faces, synchronizes the Tree and exposes dimensions on View or
Tree double-click. Existing cached native documents receive the association at
scene publication; no regeneration, schema change or OCCT work is required.

## Verification

- `zima_cpp_sketch_reference_command_tests` passed exact copied/Boolean child
  identity, two-parent branch stability, both selection orders, two coincident
  constraints, repeated parent and frame changes, allowed and rejected dragging,
  native reopen and Undo/Redo. Its existing matrices also passed 384 frame/
  dimension/lock/mobility cases and 80 finite-outline dimension/edit cases.
- `zima_cpp_derived_copy_contract_tests` and
  `zima_cpp_derived_copy_command_tests` passed nested axes, multi-source
  Mirror/Pattern, Add/Subtract, native persistence and Undo/Redo.
- `zima_cpp_context_reference_refresh_tests`,
  `zima_cpp_viewer_contract_tests` and `zima_cpp_sweep2d_contract_tests` passed.
- `zima_cpp_sheet_transition_command_tests` and
  `zima_cpp_sheet_transition_ui_contract` passed exact panel/bend material
  identity, material-face picking, azure wire-only selection, View/Tree
  synchronization, View and Tree double-click dimensions, no calculation,
  unchanged document state, save/reopen and Undo/Redo.
- The external-face GUI verifier passed against the final Windows executable,
  exercising actual View selection and confirmation.
- The real-process instance contract passed Part, Assembly and Drawing launches,
  a second `.prtz` from the same directory, exact same-file exclusion, directory
  changes, stable numbering, crash-lock recovery and independent shutdown.
- The repaired `06.prtz` passed open, Regenerate, Save, close, reopen and another
  Regenerate with the rebuilt CLI.
- The Czech, English, German, French and Russian translation contract passed.
  No user-visible text or catalog key changed.
- The final serial Windows gate passed all 15 selected contracts in 182.01 s;
  dependent component source/activation/command and placement UI checks passed
  separately 4/4 with their test TEMP inside the repository.

The shared C++ implementation and OCCT 8.0.1 pin are unchanged. There is no native
schema, template, factory-library or protected container-placement change. Windows
was verified; native Linux compilation and execution remain a separate host gate.
