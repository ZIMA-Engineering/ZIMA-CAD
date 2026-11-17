# OCCT 8.0.1 current collection and scalar API

Status: verification passed; signed Windows 2026111703 published and verified.

## Scope and cause

The complete committed-source build of Windows 2026111702 emitted deprecation
messages for 14 obsolete OCCT collection headers, including headers included
indirectly by other obsolete aliases. The installed, pinned OCCT 8.0.1 headers
identify their direct `NCollection` replacements. `Standard_TypeDef.hxx` also
marks the old scalar aliases used by this project as deprecated.

This change replaces those aliases and their includes in the shared kernel
adapter and its existing tests/benchmarks. The 49 affected source files include
geometry implementation fragments that inherit the main adapter's includes.
The breadth reflects common types used by many operations, not new algorithms.
No warning suppression, dependency upgrade, compatibility wrapper or separate
Windows/Linux modeling path is introduced.

| Previous name | Current type |
| --- | --- |
| `TColgp_Array1OfPnt` | `NCollection_Array1<gp_Pnt>` |
| `TColgp_Array2OfPnt` | `NCollection_Array2<gp_Pnt>` |
| `TColgp_HArray1OfPnt` | `NCollection_HArray1<gp_Pnt>` |
| `TColgp_HArray1OfPnt2d` | `NCollection_HArray1<gp_Pnt2d>` |
| `TColStd_Array1OfInteger` | `NCollection_Array1<int>` |
| `TColStd_Array1OfReal` | `NCollection_Array1<double>` |
| `TColStd_HArray1OfReal` | `NCollection_HArray1<double>` |
| `TDF_LabelSequence` | `NCollection_Sequence<TDF_Label>` |
| `TopTools_HSequenceOfShape` | `NCollection_HSequence<TopoDS_Shape>` |
| `TopTools_IndexedDataMapOfShapeListOfShape` | `NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>` |
| `TopTools_IndexedMapOfShape` | `NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>` |
| `TopTools_ListOfShape` | `NCollection_List<TopoDS_Shape>` |
| `TopTools_ListIteratorOfListOfShape` | `NCollection_List<TopoDS_Shape>::Iterator` |
| `Standard_Real` | `double` |
| `Standard_Integer` | `int` |
| `Standard_Boolean` | `bool` |

## Preservation checks

A standalone MSVC compilation against the installed OCCT 8.0.1 SDK passed all
16 `std::is_same_v` assertions comparing the previous types with these exact
replacements. Consequently the array bounds, scalar precision/signedness,
reference-counted collection types, topology hash policy and iterator types
remain identical. In particular, shape maps retain `TopTools_ShapeMapHasher`;
they do not change to a different default hash policy.

The change retains all modeling arguments, tolerances, traversal order,
reference ancestry, side choices, signed zero, serialization and transaction
logic. Native document formats, factory native files and protected shared
placement code are unchanged. Supported `Handle(...)` usage remains in place;
the task does not require an unrelated handle-style rewrite.

## Verification

The regression set contains 39 existing contracts covering support-driven
Fillet/Chamfer, Boolean topology, exact splines, boundary and transition surfaces,
ordered Up To, side reversal, STEP, sheet geometry/exchange, solid state,
native persistence, Assembly, Mirror, Sketcher editing and localization.
The initial run passed 37 contracts in 463.36 seconds and exposed two fixture
setup defects. After the fixture-only corrections below, both remaining
contracts passed in 23.45 seconds. No production geometry code was changed to
make those tests pass, and their geometric/identity assertions were retained.

- The split-edge fixture supplied a default Cut row without endpoint references.
  It now explicitly requests perpendicular NoCut terminals while retaining the
  authored grouping of route members. Two/one/zero surviving fragments, missing
  members, cold/incremental equivalence and topology identities still pass.
- The FORM Body-order test compared the archived fixture's tessellation against
  a newly calculated source. Diagnostic sample sets had 583 and 581 points;
  nearest-sample comparison is not a surface-equivalence test. Both sides now
  start from the same fresh source calculation. The original exact sample and
  serialized-reference assertions then pass without relaxing any tolerance.
  The fixture also creates its diagnostic output directory before native Save.
  The archived native fixture and factory assets are not rewritten.

The completed local build and the fresh committed-source Windows 2026111703
candidate emit zero OCCT deprecation diagnostics. The candidate also passes
native smoke, all 1,185 factory asset loads and matching GUI/CLI/updater build
identities. Existing unrelated `[[nodiscard]]` diagnostics are outside this
change. The first local link attempt was interrupted by a user-opened development
application; the complete retry passed after the user saved and closed it.

Independent substitution checking verifies that all 31 changed production
files and 16 other changed test/benchmark files are identical to the baseline
after replacing only the documented types and include directives. The two
fixture corrections above are reviewed separately.

Seven saved Fillets and five saved Chamfer variants were recalculated. Every
recorded input/result volume, preview/reopened-preview deviation, owned-face
count and sampled-seam summary matches the 2026111702 baseline exactly. The
06 six-feature history, including its downstream Fillet, also passes. Notable
results remain `1028534.41136416 mm3` for recreated 04/13 and
`700730.017438195 mm3` for 06/02; the latter's 44 sampled seams retain a maximum
normal angle of `1.95810338065116e-5 degrees`. The 04 terminal cuts are not all
tangent, as documented for the previous release.

The actual GUI circle-inference matrix passes all 16 cases and 576 dimension
variants, including repeated edits/drags, native/external circles, H/V and
rectangle symmetry, native reopen and Undo/Redo. Mirror save/Undo/camera/default
plane checks and the five-language catalog contract also pass. The public
Windows package passes the same circular-inference and Mirror GUI contracts,
plus regeneration/preview/Cancel for both saved 04 and 06 Fillets. Their
published preview wires match the expected geometry exactly.

See [machine-readable API/regression evidence](20261117-occt-api-evidence.json)
and the preceding [detailed repair matrix](20261117-blend-circle-mirror-followup.md).

Localization review: the implementation changes C++ type names and includes
only. It introduces no user-visible strings or translation keys. The five
supported catalogs remain subject to the normal localization contract test.

Windows and Linux keep the same C++ implementation and pinned OCCT 8.0.1 / Qt
6.11.0 dependencies. Native Linux compilation and runtime verification require
the Linux host; a Windows result does not establish Linux acceptance.

Windows publication: [release acceptance](../releases/2026111703.md#windows-publication-acceptance) and [machine-readable evidence](20261117-windows-1703-publication.json).
