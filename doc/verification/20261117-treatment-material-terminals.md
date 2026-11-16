# Fillet and Chamfer material terminals

Build: `2026111701`. Windows, shared C++ implementation, OCCT 8.0.1.

## Defects and changes

- Chamfer endpoint flags were mapped through oriented topological vertices,
  while the tool consumed underlying curve-parameter endpoints. Reversed
  edges could therefore apply NoCut or an explicit end face to the wrong
  physical end. Both mapping boundaries now use curve-parameter order;
  wire traversal retains its own orientation.
- An additive Fillet route clipped its optional terminal extension using
  a point inside the extended tool. That point could already lie beyond
  the terminal plane, retaining the outward wing. The preceding route point
  now selects the retained side. An entirely clipped optional tail is valid.
- A continuous Fillet route mistook crossing a cylindrical Body envelope
  for continuation through an earlier radius. It now uses the same local
  surface differential decision as a single edge.
- When an additive treatment reaches a tangent curved terminal and its
  continuation leaves the material, automatic termination stays perpendicular
  at the real edge endpoint. It does not grow a tail into empty space.
  Crossing a curved envelope and meeting material in an earlier inner radius
  retain their exact intersection behavior. An explicitly selected end face
  remains authoritative. The rule is shared by Fillet and Chamfer.
- Continuous Chamfer routes now transport the actual A×B or A+angle profile,
  including the selected A side and FLIP. Their straight terminal members
  reuse the established single-edge end construction. Merely clipping the
  unextended sweep could not reach an oblique terminal on the additive side.

No native format, placement solver, dependency version or user-visible text
changed. English/Czech/German/French/Russian localization remains unchanged.
The development launcher remains the repository-root `zima-cad.bat`.

## Regression matrix

`cpp/tests/edge_treatment_terminal_fixture.hpp` is exercised by both native
treatment test executables. It covers:

- additive and subtractive tangent line/arc/line routes;
- two independently oblique terminal planes;
- normal and reversed topological edge orientations;
- Fillet radii 1 and 3;
- equal Chamfer, A×B with B=A/2 and B=2A, and A+angle at 30° and 60°;
- both A-side choices (FLIP), with A=1 and A=3;
- automatic versus explicitly selected terminal faces;
- each physical endpoint independently set to NoCut;
- exact Boolean changed-volume checks beyond both terminal planes;
- independent section-point checks for A, B, angle and FLIP;
- a curved cylindrical envelope crossed by a continuous Fillet route;
- explicit NoCut on an additive vertical pocket corner reaching a rounded lip,
  including asymmetric Chamfers and reversed input orientation.

The existing suites also exercise sequential radii, split routes, larger
profiles, circular/rolling supports, mixed addition/removal, route nodes,
reference ownership, rejected edits, parameter locks, Undo/Redo and native
persistence. These checks do not constitute coverage of every possible CAD
topology or profile size.

## User-model verification

Only isolated copies of `04.prtz` and `05.prtz` are used. Original user files
are not overwritten. The opt-in command fixture uses
`ZIMA_VERIFY_SAVED_TREATMENT`, `ZIMA_VERIFY_SAVED_TREATMENT_OWNER`, and
`ZIMA_VERIFY_SAVED_END_MODES=1`. `ZIMA_VERIFY_SAVED_ASYMMETRIC=1` additionally
replaces the final treatment in a private fixture with A×B and A+angle,
both FLIP states.

The fixture rebuilds the complete input history and candidate output. This
is essential: an unchanged edit intentionally reuses its saved result, which
may have been calculated by the defective version. It verifies native
save/reopen, persisted feature definitions, volume, lightweight preview
agreement with the calculated boundary, and zero Body calculations during
preview generation. Offered endpoint choices are exercised independently.
For a removal profile, authored wire portions already outside the input Body
are reported separately: they cannot create a new result boundary. The
material-domain comparison still rejects points inside the input that do not
reach the resulting boundary. Additive wires receive no such exemption.

The reported `Zaoblení 09` tangent exit in model 04 was checked with its
automatic cut and NoCut: both produce `959138.510692052 mm³`. Its explicit
alternative remains distinct at `959139.048554608 mm³`. The additive
`Zaoblení 11` route produces `1029549.77305706 mm³`, with different volumes
when either oblique end is independently left perpendicular. A×B and
A+angle, both FLIP states, also pass on that isolated route.
The same asymmetric matrix passes on model 05's crossed cylindrical boundary
and model 04's tangent material exit. All reported 04 treatments (Chamfer 02,
Fillets 07, 09, 11 and 12) and 05 Fillet 03 pass their offered endpoint choices.

The user-model GUI preview fixture allows the required cold rollback-input
materialization when Properties opens, then checks that draft generation and
node choices perform no Body calculation. It compares the published shared
wire and labels, cancels, and verifies the unchanged document revision.

Local logs and isolated native fixtures are under
`build/test-models/chamfer-no-cut-20261117/`; GUI captures are under
`Projects/test/`. These disposable outputs are not required by native files.

## Completed Windows checks

- Seven CTest contracts passed: native Fillet, native Chamfer, treatment
  commands, direct/batch treatments, support identity, history recovery and
  treatment wires (`final-native.log`).
- Five GUI contracts passed: ordinary feature selection, treatment editing,
  creation, symmetric/asymmetric treatment controls and five-language
  translation/catalog validation (`final-ui.log`).
- Four additional user-model GUI runs passed: regenerated 04 and 05, plus
  isolated A×B and A+angle versions of 04's oblique additive route. Shared
  preview geometry, labels, no draft calculation and Cancel were checked.
- Native copies were saved and reopened with unchanged treatment definitions
  and volumes. Existing command/GUI contracts cover Undo/Redo and unchanged OK.
- `git diff --check` passed. Localization review found no new or modified
  user-visible strings; reused validation messages retain their translations.

## Platform scope

Windows native and GUI verification is performed locally. Linux uses the
same changed C++ sources and OCCT version, but Linux compilation and runtime
verification require the Linux host and are not claimed here. This change
delivers the local development build, not a portable release archive.
