# Document unit audit

Status: in progress, requested on 2026-10-03. The preceding chained Sketch
fillet and mixed Feature direction fixes are committed and their native/GUI
regressions pass. This audit must not be reported as complete yet.

## Required behavior

Changing document units changes numerical presentation and interpretation of
new input, not the physical size of existing geometry. Native lengths remain
millimetres and native angles remain degrees. A 25.4 mm Part and a one-inch
Part occupy the same physical length in any Assembly. Counts, ratios, and
explicitly specified paper-space or command-API units retain their semantics.
Preserve signed zero, geometric side choice, reference identity, calculation
accuracy, no-op editing, Undo/Redo and native persistence.

## Engineering assessment: units, precision and manufacturing intent

The user confirmed canonical millimetres and approved proceeding with the
design on 2026-10-03. The scope below is not a claim of fully implemented
support. Preserve cm as an existing supported length unit; mm/in can remain
the primary first-run presets without deleting other unit choices.

Inputs are existing canonical geometry, document units, expressions, Family
values, explicit tolerances and drawing requirements. Available means are the
existing canonical model, quantity-aware input/output boundaries and expression
parser. Required outputs are unchanged physical geometry and manufacturing
requirements, predictable editing, and reliable exchange with other documents.

### Independent concepts

| Concept | Proposed responsibility |
| --- | --- |
| Canonical geometry | Retain native mm/degrees. Do not scale stored bodies, placements or references when changing presentation units. |
| Document authoring units | Interpret new length/angle input and numerical relation quantities consistently. A Part keeps its own units inside any Assembly. |
| Numerical display | Control decimal places, trailing zeros, decimal separator and optional fractional notation. Never feed rounded display strings back into unchanged geometry. |
| Manufacturing tolerance | Retain the physical permissible limits or geometric tolerance specification. Formatting and unit conversion must not change these requirements. |
| Kernel/solver tolerance | Retain the existing physical numerical thresholds. Neither decimal count nor a change from mm to inches changes them. |
| Display approximation | Mesh deflection controls visual approximation separately from kernel accuracy and manufacturing tolerance. Preserve its physical value. |
| Drawing conventions | Treat drafting standard, general tolerance policy, sheet format and projection convention independently of document units and UI language. |

An inch is exactly 25.4 mm according to
[NIST](https://www.nist.gov/pml/owm/si-units-length). The conversion factor does
not introduce a manufacturing allowance. Floating-point representation still
has finite precision; preserve existing canonical values to avoid cumulative
conversion drift instead of repeatedly converting stored geometry back and forth.

Independent scale checks:

- `25.4 +/- 0.0254 mm` and `1.000 +/- 0.001 in` describe the same limits.
- A display increment of `0.001 in` represents `0.0254 mm`, whereas `0.001 mm`
  is 25.4 times finer. Equal decimal counts are not equal physical resolution.
- `10 +/- 0.01 mm` has limits `[9.99, 10.01] mm`. The superficially similar
  rounded `0.394 +/- 0.001 in` has limits `[9.9822, 10.033] mm`: both its centre
  and its tolerance interval have changed. This is not an acceptable conversion.

Therefore no single global decimal count can express geometry accuracy and all
manufacturing tolerances. Initial presets may offer convenient display defaults,
but the tolerance itself must not be inferred from those defaults. Where a
drawing explicitly defines tolerances by decimal count, trailing-zero policy is
part of that drawing's specification and cannot be altered as mere formatting.

### Tolerances and drawing output

Convert nominal values and both deviations/limits together. Support symmetric,
asymmetric and one-sided limits individually; angular tolerances require their
own conversion. Preserve basic/reference status, geometric tolerance modifiers,
datum references and fit/thread designations. Converting units must not change
an M thread into a UNC thread, select another stock thickness, or replace an ISO
fit with an assumed imperial equivalent.

Dimension tolerance strings retain their authored decimal meaning. The current
formatter supports an explicit annotation unit, precision and trailing-zero
policy; the exact decimal converter handles numerical strings without binary
rounding. Arbitrary authored text must remain text. Do not guess the meaning of
digits in notes, identifiers, thread names or material specifications. Further
data-model changes need a traced implementation, template updates where required,
and persistence tests. See incremental evidence below for completed gates.

Some exact converted limits have no finite decimal representation. Merely storing
the precise model value is insufficient if the printed drawing communicates
different acceptance limits. For such cases, preserve the original authoritative
specification and offer a clearly identified secondary converted indication, or
reject a requested authoritative conversion and explain the affected item.
Never silently round outward (looser) or inward (tighter). Choosing new practical
manufacturing limits is a separate intentional design edit.

Drawing documents own their annotation policy independently of source Parts.
Changing a source Part's authoring units must not silently rewrite an existing
drawing's manufacturing specification. PDF, printing and DXF must agree with the
selected drawing policy. Paper-space text height, line width, sheet size and
scale ratios must retain their existing meanings. A fraction display, if later
supported, is formatting rather than automatic snapping of the model to stock
fractions.

British and American practice must not be selected merely from the UI language
or the choice of inches. BSI describes
[BS 8888:2025](https://pages.bsigroup.com/BS8888-whats-new-webinar) as the UK
technical specification framework integrating ISO GPS standards. ASME describes
[Y14.5-2018 (R2024)](https://www.asme.org/codes-standards/find-codes-standards/dimensioning-and-tolerancing)
as establishing GD&T rules and interpretation for drawings and digital models.
These official overviews establish that a standard is more than a unit label;
they do not supply every detailed formatting rule. Full standards compliance
must not be claimed from these overviews or from adding an "ISO/ASME" selector.

### Relations and Family conversion

Retain the approved single authoring unit system per document. Do not add a
hidden, independently selected unit system for relations. Plain `d6 = 10`
continues to use the document's unit for the target quantity.

An explicit conversion must preserve the physical result on later regeneration
and for every Family variant, not just the currently evaluated values. In
`d3 = d1 * 2 + 10`, the multiplier and length literal have different meanings.
Counts, ratios and text must not be scaled. Area, volume and density require
their respective powers of length; temperature values and temperature differences
cannot be treated as the same multiplicative conversion.

The parser must account for dimensional comparisons, all conditional branches,
trigonometric angle conventions, inverse functions, and rounding functions.
For example, rounding to whole millimetres is not equivalent to rounding to whole
inches. Formatting a dimension into a text parameter also needs an explicit
policy; arbitrary strings cannot be safely rewritten numerically. Expressions
whose meaning cannot be established must block the conversion with a precise
line/field diagnostic, before any document modification.

Family currently applies native numeric dimension values. Do not scale those
native values simply because displayed cells change units. Inspect storage,
presentation and evaluation separately to prevent double conversion.

### Alternatives and implementation sequence

| Candidate | Assessment |
| --- | --- |
| Rescale the complete internal model | Reject: unnecessary geometry/reference/cache risk and repeated-conversion drift. |
| Permanently lock units after creation | Simpler restriction, but does not fix mixed-unit input/output defects and removes the requested conversion capability. |
| Canonical storage with consistent quantity boundaries and explicit conversion | Recommended: preserves the established model while allowing controlled conversion where semantics are known. |

1. Finish the boundary audit and fix quantity-aware input/output without changing
   geometric identity, placement solving or calculation accuracy. Treat mm as
   the regression baseline; verify source-document context in nested Assemblies.
2. Establish numerical tolerance handling and drawing formatting policy. Keep
   existing annotations intact until their semantics are known.
3. Implement explicit document conversion as a preflighted, atomic transaction.
   List affected quantities and any unresolved expressions/annotations. Cancel
   or failure changes nothing. Successful conversion is one Undo step and must
   preserve physical results after regeneration, save/reopen and variant changes.
4. Add first-run unit presets through the already approved in-application wizard.
   Global defaults apply to future documents, not existing open documents. Resolve
   template/default precedence explicitly; do not create extra template copies
   solely to compensate for inconsistent precedence.

The initial supported scope should be explicit. Missing conventions or ambiguous
authored content must not be advertised as fully supported unit conversion.
Independent drafting-standard redesign is not a prerequisite for correcting
ordinary mm/inch modeling input, and must not turn this work into a global rewrite.

## Audit map

| Area | Current evidence | Remaining work |
| --- | --- | --- |
| Document settings | Repeated mm/cm/m/in switches with rad, geometry reuse, volume, native signatures, save/reopen, Undo/Redo and explicit relation conversion pass. | Manufacturing annotations now retain their authoritative basis and derive exact or explicitly approximate View presentation; quantity validation now rejects mismatches before committing; finish remaining settings consumers. |
| Mixed-unit Assemblies | Explicit mixed-unit tests cover repeated rotated subassemblies, source-unit switches, native reopening and current unsaved geometry/material data. | Retain the separate Drawing/exchange verification gates. |
| Modeling input | Feature parameters and selected Sweep fields now use the shared unit control; conversion/no-op/Cancel tests pass. Other dialogs still have fixed-unit fields. | Continue command-by-command integration, retaining exact canonical values and quantity distinctions. |
| Container placement | Numeric coordinates, angles and reference offsets now use document-unit controls; canonical values, solving and reference semantics remain unchanged. | Continue dependent GUI verification; no reference or solver redesign is part of this work. |
| Sketch input | Dimension expressions, plane coordinates, spline points, offsets and model text sizes use document units with canonical storage; paper text sizes remain mm. | Finish annotation authoring checks and remaining context boundaries; retain counts and curve parameters. |
| View dimensions | Ordinary nominal labels share one document-unit formatter across painting, picking, label position and grips; native values/references remain unchanged. | Exact complete specifications convert for display; otherwise show the original specification with an explicitly approximate secondary nominal. New annotation authoring and layout fields now follow document units; quantity validation passes; finish drawing/output gates. |
| Measurements | Native and GUI checks cover reopened records, nested occurrences, length/area/volume/mass and all supported unit labels. | No angular-distance result exists in this inspector; orientation input is covered separately. |
| Physical properties | Mixed-unit checks cover area/volume powers, kg/m³ and lb/in³ density, repeated occurrences, and current nested mass after source changes. | Finish the broader settings-consumer audit; orientation input already has unit-control coverage. |
| Relations | Explicit source conversion preserves quantity-dependent results, inactive branches, fixed-unit sheet thickness and atomic rejection; no hidden second unit basis. | Complete the remaining dimension-binding audit; typed trigonometry is verified below. |
| Family | Native values remain mm/degrees; editor and unit labels use document units. | Fractional editing, unit changes, exact no-op, native reopen, shared Undo/Redo and geometry reuse verified. |
| Drawings | Sheet geometry, pens and annotation sizing explicitly use paper mm. | Separate paper units from model dimension values; audit source units, text/tolerances, PDF/print/DXF output. |
| Exchange | STEP/IGES/DXF need file-unit interpretation at their existing import/export boundaries. | Verify actual unit metadata and physical extents; no double scaling from document settings. |
| Native templates | New-document creation overrides template unit metadata with configured unit defaults. | Make precedence explicit before adding mm/inch choices to first-run setup. |
| Other settings | Supported choices also include time, temperature and stress. | Identify actual consumers; do not merely relabel stored user-authored parameter strings or invent numerical consumers. |

## Verification gates

- Compare actual extents, volume and saved canonical dimensions before/after
  changing units and after regeneration. Display-string checks alone are not
  sufficient.
- Exercise numerical entry, arithmetic expressions, stepping, Cancel and
  unchanged OK. An unchanged rounded display must not round the stored value.
- Verify degrees/radians separately from dimensionless counts and quantities.
- Verify manufacturing limit intervals before/after conversion, including cases
  that cannot be printed exactly at the requested decimal precision. Check
  trailing zeros, explicit/general tolerances and all output paths.
- Check repeated mm/inch switching for geometry drift, source changes in nested
  Assemblies, all Family rows and inactive relation branches. Changing only
  presentation must not invoke OCCT or modify reference identities.
- Keep tests for the user's constrained L profile and mixed-side Feature active.
- Review localization in all five languages, document the resulting contracts,
  commit and publish only verified changes. Linux GUI acceptance remains a
  Linux-host task.

The approved built-in first-run wizard follows this work; its scope and the
remaining modeling requests are recorded in [the roadmap](../ROADMAP.md).

## Incremental implementation evidence

On 2026-10-03, `zima_cpp_unit_input_tests` passed for mm/cm/m/in, deg/rad,
stepping, unchanged values down to `1e-20`, parent-context selection and numeric
entry in all five supported locales. The control is now used by the Feature
parameter panel, 2D/3D Sweep thickness and Helical Sweep thickness/pitch/base
offset. The active writable document supplies the unit context; metadata edits
to other open documents must not replace that context.

`zima_cpp_feature_unit_input_contract` verifies converted parameter input,
length/angle mode switching, exact unchanged Feature OK without a callback,
one changed Feature commit, unchanged values in Sweep fields, Cancel and
retained mm kernel-approximation controls. The numeric field layout regression
also passes after inspecting actual displayed decimal places rather than Qt's
internal storage precision. The five-language catalog test and numeric lock
test passed for the initial Feature integration. Unit identifiers are unchanged;
there are no new translated user-visible messages.

This does not complete document conversion, manufacturing tolerance conversion,
expression conversion, signed-zero input integration or all command paths.
The user explicitly approved the numeric input/display integration in shared
placement. Its solver, reference identities, side flags and persistence format
are not changed. Display quantum comparisons use the displayed increment
converted to canonical units, rather than Qt's internal storage precision.

The next integration covers Hole, Thread, Drill Point, Twisted Sheet, Shell,
Fillet/Chamfer, standalone Extrusion/Revolution and construction geometry.
Length, angular and dimensionless fields are classified explicitly. In
particular, a thread runout pitch factor remains dimensionless. Thread standards,
designations, catalogs and physical thread geometry are unchanged, as reaffirmed
by the user on 2026-10-03; changing document units never substitutes a thread.

Placement unit tests cover all eight length/angle combinations, exact unchanged
coordinates, rotations, reference offsets, signed zero, side flags, numeric
locks, JSON round-trip and reference solving. A pre-existing Point inline test
now checks exact canonical input and independently rounded visible text: display
precision no longer quantizes the stored coordinate or live preview.

The placement/primitive stage passed `zima_cpp_unit_input_tests`, the expanded
`zima_cpp_feature_unit_input_contract`, full UI contracts, numeric field layout,
numeric locks and five-language catalog checks. Application GUI regressions for
surface placement, Body scale and both straightening/restore fixtures also
passed. This is incremental evidence, not completion of the remaining audit.

The extended `zima_cpp_metadata_command_tests` passed repeated length-unit
switching with radian settings, physical volume conversion, unchanged cached
shape, unchanged calculation precision, native geometry fingerprints after
save/reopen, and Undo/Redo. Existing Feature GUI, chained Sketch fillet, Feature
parameter and profile-centreline regressions also passed. These fixtures do not
yet prove all mixed-unit/nested Assembly or relation-regeneration cases.


The Sketch input stage uses canonical unit fields for plane offsets, flat/Bend
parameters, spline control points, offsets and modeling text. Drawing text height
remains a paper-millimetre value. K factors remain dimensionless. View dimension
entry and Sketch Dimension Properties accept arithmetic with one optional
trailing unit, including decimal commas and points. An explicit suffix converts
only the submitted value; it does not change document metadata. Invalid units
are rejected, and the error messages are translated in all five languages.

The expanded Feature unit contract passed all eight length/angle combinations,
including precise unchanged values, signed zero, invalid-expression retention,
explicit suffixes, Sketch Properties, spline points and modeling/paper text.
The inline View unit contract passed mm-to-inch and inch-to-mm entry, unchanged
confirmation, native geometry fingerprints, Undo/Redo and pending Properties
Cancel. Full UI contracts, ordinary dimension edits, numeric locks/layout and
five-language catalogs passed. These checks do not yet establish converted
nominal View labels, Family/Relations conversion or manufacturing tolerances.

The current DXF point/spline tests also pass after rebuilding their executables.
The point export assertion now follows the established manufacturing export
policy: construction points remain in native persistence but are omitted from
manufacturing DXF. Ordinary coincident points retain separate exported entities.
No DXF production behavior was changed in this stage.


The subsequent parameter stage covers component placement and mate offsets/
limits, Pattern spacing/angles, Body scale centers, sheet transition parameters,
Sheet from Body thickness, Shaft Thread length and measurement-axis rotation.
Counts, K factors, scale factors and thread runout factors remain dimensionless.
Thread catalog selection, pitch and unchanged root diameter remain identical.
Body measurement uses its explicitly supplied unit map for both the physical
results and its angular input fields.

The eight-combination GUI input matrix passed for these controls, including
precise unchanged values, placement-reference identities and flip flags, mate
limits, Pattern counts and unchanged sheet Sketch definitions. Full UI contracts,
five-language source/catalog validation, numeric layout and lock tests passed.
Application checks for ordinary/explicit-unit inline dimension edits, Pattern
View dimensions and Body scale passed. This stage introduces no new translatable
text and does not change placement solving or the native format.

Independent inch STEP and IGES fixtures retain their physical dimensions when
imported into mm/cm/m/in documents. The IGES fixture explicitly declares the
inch unit flag and is checked for 25.4 x 50.8 x 76.2 mm extents and their physical
volume, including native save/reopen and regeneration. The STEP test also checks
the kernel component-import path. Both model-import contract suites passed;
this evidence does not yet cover every exchange export or nested Assembly path.


Relation conversion now has an AST-based quantity preflight which traverses
all branches without evaluating their numeric domains. It preserves authored
source lines/comments/text and inserts explicit factors around quantity reads
and results. This keeps literal ratios, comparison thresholds, rounding,
trigonometric conventions and text output physically/semantically consistent.
There is no separate stored relation unit basis. Ambiguous quantity branches or
variable quantity exponents reject conversion before any live mutation.

File Settings applies this conversion to its private Part/Assembly draft,
including cached numeric output parameters. The reserved millimetre
`SHEETMETAL_THICKNESS` parameter retains its existing fixed-unit contract.
Canonical geometry and Family's current native numeric storage are not scaled.
The Part/Assembly settings transaction remains the publication boundary.

The core relation tests passed conditional branches, quantities including
area/volume/density, rounding, direct/inverse trigonometry, signed zero, unchanged
text/comments, inactive domain failures, unsafe-expression rejection and six
round-trip unit switches. Metadata integration tests passed cached geometry
reuse, physical volume after Regenerate and native save/reopen, full-document
Undo/Redo, atomic rejection of an incompatible inactive branch, Assembly source
sharing and the fixed-unit sheet-thickness parameter. Existing Family and
engineering metadata command tests passed against the rebuilt libraries.

The Family audit reproduced a separate decimal bug: renaming a member restored
its generic `10.5` dimension as `10`, because display-comma text was passed to
`std::stod`. The catalog now formats lossless, locale-independent native numbers
and restores them with strict full-string parsing. Dimension unit metadata comes
from the existing relation binding catalog; persisted Family values and reference
identities keep their existing canonical contract.


The rebuilt application and UI contracts passed. The five-language GUI check
verifies that File Settings remains open and shows the localized conversion
message with its original line and target; source/catalog coverage passes too.
The ordinary Relations editor and the expanded metadata/core suites pass after
the final localization correction. Typed inverse-function results were still
pending at this stage; the later typed-angle verification below closes that
specific gap.


### Family numeric presentation (2026-10-03)

The Part/Assembly Family editor labels dimension columns with their units and
converts base/variant values at the UI boundary. Explicit trailing units and
arithmetic reuse the existing dimension parser. Untouched cells preserve their
exact original native strings, including absent inherited values. Reference
inspection does not replace pending input. Invalid values identify their row
and column through a message localized into all five supported languages.

Added regression checks cover fractional member edits and generic restoration,
unit changes through mm/cm/m/in, member unit propagation, cached geometry reuse,
shared Undo/Redo and native save/reopen. GUI checks cover all length/angle unit
combinations, explicit-unit arithmetic, inheritance, signed zero, exact no-op,
Cancel and localized invalid-input handling. These rebuilt checks passed:

- `zima_cpp_family_table_tests`: fractional native values, shared edits, unit
  changes, Undo/Redo and native reopen; the generic and member retain their
  calculated kernel shape objects on a unit-only change.
- `zima_cpp_feature_unit_input_contract`: all eight length/angle unit pairs,
  exact unchanged Family rows, explicit suffixes, arithmetic and Cancel.
- `zima_cpp_translations_contract`: catalog coverage and invalid Family input
  in Czech, English, German, French and Russian, without committing or closing.
- `zima_cpp_family_table_ui_contract`: real View/Tree selection, stable column
  identity with its unit, presence controls and linked variant workflow.
- `zima_cpp_inline_units_ui_contract` and `zima_cpp_ui_contract_tests`: View
  entry and existing shared dialog behavior remain covered.

The fractional regression was also exercised against the original number
formatter/restoration path, where it failed. Stored dimensions are checked
exactly; independently calculated Sketch extents use a geometric tolerance
because solver coordinates can differ by approximately 1e-15 mm.

Nominal View labels, tolerance conversion and the remaining whole-application
unit audit remain pending. This is not the final units release. The rebuilt metadata, engineering metadata
and relation-program suites also passed after this change.


### Approved follow-up: optional Family CSV exchange

The user approved CSV import/export on 2026-10-03 after confirming that native
Family data is already structured text inside the document. Keep the table as
the primary editor. CSV must remain optional; all required variant definitions,
reference bindings and calculated state stay exclusively in `.prtz`/`.asmz`.
CSV exchange is not implemented in the numeric-presentation stage above.
Preserve units, stable row/reference identity, authored text and localization,
validate the complete import before replacing pending editor data, and commit
only through the dialog's normal OK transaction. Do not infer model references
from coincident names or silently import one document's values into another
binding. Cover quoted separators/newlines, Unicode, invalid data and Cancel.


### Ordinary model View dimension labels (2026-10-03)

The active Part/Assembly's numeric context now supplies ordinary model View
labels with millimetres per displayed length unit and degrees per angular unit.
One formatter serves painting, the common candidate picker, label position,
dimension grips and grip dragging. It does not mutate the stored viewer mesh,
source dimensions, sweep geometry, references, or calculated bodies. Counts
remain scalar and literal catalog/thread labels remain literal. Native drawing
renderers keep their own annotation policy and are not wired to this View state.

Automatic labels and numeric-only/basically exact styles convert to mm/cm/m/in
or deg/rad. Explicit per-dimension decimal formatting is preserved. This stage
deliberately does not reinterpret text-based manufacturing tolerances or custom
suffixes: their complete original native-unit annotation remains unchanged.
Those styles still require the explicit tolerance/specification conversion work
above; this is a remaining requirement, not a claim that all styled labels now
follow document units. Repainting must never change an acceptance limit.

Added checks cover ordinary labels, radial/diameter/angle labels, common picking,
unchanged native values/reference/geometry, scalar counts, literal thread names,
custom text, explicit style precision and preservation of toleranced labels.
The inline-edit integration also checks that units from a loaded native Part
reach the same View label used to start editing. All rebuilt checks passed:
`zima_cpp_dimension_layout_contract_tests`, `zima_cpp_inline_units_ui_contract`,
`zima_cpp_ui_contract_tests` and `zima_cpp_translations_contract`. The isolated
picker fixture explicitly enables the same Dimension selection contract used
by the application. It checks all eight length/angle unit combinations, label
hits and all three presentation grips. Existing layout/drag/annotation tests
continue to pass. The internal invalid-unit diagnostic is translated in all five
language catalogs; unit symbols themselves are language-independent.


### Exact decimal conversion and Family presentation (2026-10-03)

A shared decimal-string converter now distinguishes an exact finite result from
one that would require rounding. It uses the exact 25.4 mm inch definition and
integer decimal arithmetic, preserving explicit signs and signed zero. Invalid
numbers, mismatched quantities, nonterminating ratios and oversized annotation
text are rejected. Nonzero degree/radian conversions cannot have a finite exact
decimal result and are rejected by this exact-only helper. Ordinary approximate
numeric entry continues to use its existing parser.

Family Table uses the helper for finite exact display conversions: for example,
2.54 mm now displays as 0.1 in without a floating-point decimal tail. Nonterminating
ordinary Family values retain their existing numeric presentation. Unchanged
cells still preserve the original native string independently of display.
There is no file-format change and no new user-visible text; unit symbols are
language-independent.

Rebuilt checks passed: `zima_cpp_exact_unit_conversion_tests` (including 1,000
inch values checked against independent integer arithmetic, exact round trips,
scientific notation, signs, invalid and repeating values),
`zima_cpp_feature_unit_input_contract` (all document unit pairs, exact displayed
Family values and unchanged confirmation), `zima_cpp_family_table_tests` and
`zima_cpp_translations_contract` (all five catalogs).

This is the arithmetic foundation for manufacturing tolerance conversion, not
its completed integration. Authoritative tolerance storage, nominal values,
property editing and drawing/export annotation behavior still require coherent
end-to-end implementation and validation before the final units release.


### Explicit annotation units and exact specification conversion (2026-10-03)

Manufacturing annotations now have an explicit `value_unit` independent of
canonical geometry and authored suffix text, plus a `keep_trailing_zeros` policy.
An empty annotation unit denotes canonical mm/degrees. The same style is carried
by model layout, persisted Viewer dimensions and Drawing dimensions. Sketch
annotations also persist their numerical annotation precision. No geometry,
reference identity, side or solver tolerance is rescaled.

The shared formatter uses that unit for the nominal value and leaves its authored
deviations in the same declared unit. An explicitly based annotation is not
reinterpreted by a View repaint in another document's units. Drawing formatting
may omit implicit mm only for a canonical/mm annotation. Property fields show the
unit used by nominal and tolerance values, and expose trailing-zero formatting;
Sketch properties also retain their per-annotation decimal setting. Unit symbols
are language-independent and the new labels/errors are translated into all five
supported languages.

`convert_dimension_annotation_units` prepares a new complete style without
modifying its source. It converts the printed nominal and all four deviation
fields, including currently inactive fields, using exact decimal arithmetic.
It checks that the resulting formatter actually prints the same converted
nominal. Nonterminating conversions, incompatible units, precision above the
supported annotation range and ambiguous authored text return a stable field
identifier instead of silently rounding. Fit/thread designations and arbitrary
text are not interpreted as numbers. Choosing different practical manufacturing
limits remains an intentional design edit, not unit conversion.

This stage establishes persisted annotation meaning and the shared converter.
The subsequent presentation integration below supersedes the proposed destructive
style conversion during a document-unit transaction. Drawing annotation policy
remains independent of source Part authoring units. End-to-end PDF/DXF/printing
checks remain part of the full audit; formatter and serialization checks alone
do not establish those output gates.


Verification for this stage passed after rebuilding the affected native and GUI
binaries:

- `zima_cpp_exact_unit_conversion_tests`: exact conversion of nominal plus
  symmetric, one-sided and asymmetric deviations; actual converted limits
  checked against the original physical interval; inactive deviations,
  repeating/rounded nominals and arbitrary text rejected without source mutation.
- `zima_cpp_dimension_layout_contract_tests`: explicit inch specification across
  View unit changes, a real Sketch radius, shared properties, drawing text,
  Viewer serialization and actual `.prtz`, `.asmz` and `.drwz` save/reopen.
- `zima_cpp_model_dimension_layout_command_tests`: explicit inch styles through
  CLI, one Undo/Redo transaction, repeated no-op, invalid-input atomicity, native
  save/reopen, nested occurrence ownership and identical calculated geometry.
  CLI validation accepts the new typed fields and still rejects unknown fields.
- `zima_cpp_sketch_serialization_tests`, `zima_cpp_feature_unit_input_contract`,
  `zima_cpp_ui_contract_tests`, `zima_cpp_inline_units_ui_contract` and
  `zima_cpp_translations_contract`: native Sketch data, existing input behavior,
  unchanged inherited annotation formatting and all five language catalogs.

`zima_refresh_start_templates` regenerated and reopened `START_PART.prtz`,
`START_SKELETON.prtz` and `START_ASSEMBLY.asmz`. Their SHA-256 values were unchanged:
these empty templates contain no custom annotation styles. The rebuilt
`zima_cpp_new_document_options_ui_contract` and
`zima_cpp_relation_templates_ui_contract` passed new-document creation from the
native templates, normal commands and active first editable context. The final
property/CLI-only changes do not alter those templates or creation code.

### Specification-preserving View unit conversion (2026-10-03)

A conversion that preserves today's printed nominal can still change a future
Family variant or relation result when the converted decimal precision is reused.
Document-unit changes therefore leave authoritative annotation units, precision
and deviation strings unchanged. The View derives a complete exact equivalent
from the current value and original specification whenever its formatter can
represent that equivalent. This is presentation only and does not invoke OCCT,
change references, create an undo transaction or rewrite native annotation data.

If exact presentation is unavailable, the original specification remains visible
with its source unit and an explicitly approximate secondary nominal, for example
`10mm ±0.01 (≈0.394in)`. The secondary value is derived from the authoritative
printed nominal, not an unrounded geometry value that might contradict it.
Only this secondary indication uses the global display decimal count. The
original manufacturing nominal retains its persisted annotation precision.
Arbitrary text overrides stay literal; custom suffixes are not interpreted as
unit tokens. Thread and count labels retain their previous behavior.

The shared text layout uses the effective converted deviations for stacked
presentation. Approximate text is a separate display run beyond the complete
primary specification. Basic-dimension frames enclose only the authoritative
nominal; masking, bounds, picking and grips include the complete visible label.
The transient text separators are never persisted in documents. File Settings
explains the approximate marker in all five languages.

Numeric input and new annotation authoring still need their document-unit
context; those controls, drawing/export policy, mixed Assemblies and remaining
audit gates must be completed before the final units release. Existing Drawing
annotations remain independent of source Part authoring units.


Verification for specification-preserving presentation passed after rebuilding:
`zima_cpp_exact_unit_conversion_tests`, `zima_cpp_family_table_tests`,
`zima_cpp_metadata_command_tests`, `zima_cpp_dimension_layout_contract_tests`,
`zima_cpp_ui_contract_tests`, `zima_cpp_feature_unit_input_contract` and
`zima_cpp_translations_contract`. The new layout proof was visually inspected:
stacked deviations remain readable and the secondary indication is outside the
basic frame. The common picker and all dimension grips are also exercised.
The regression suite caught an unwanted addition to a custom suffix; that path
now preserves the authored annotation, and the affected suite passed again.
Family checks cover generic/member unit switches with unchanged manufacturing
strings, shared geometry, Undo/Redo and native reopen. Localization checks cover
source keys, complete catalogs and actual controls in all five languages.


### Annotation authoring context and layout input (2026-10-03)

An unbound model or Sketch annotation begins editing in its document's length
or angle unit. Confirming unchanged fields returns the original annotation
exactly; editing only the geometric Sketch value does not bind a new annotation
basis. Adding a tolerance, a basic frame or other annotation formatting stores
the explicitly shown authoring basis. Reopening a manufacturing annotation
retains its existing basis, deviation strings and precision even when document
units differ. Custom literal text/suffixes retain their original handling.
Drawing fields do not opt into the model's authoring-unit context.

Model annotation offsets now use the shared canonical-value unit control and
radial label rotation uses the angular quantity. Merely opening these fields
does not quantize their stored values. Read-only measured-value labels follow
document units while scalar counts remain scalar. Drawing annotation placement
retains its existing paper-mm and degree controls. This changes annotation UI
boundaries only, not container placement, references, geometry or side choices.

After rebuilding, the authoring matrix passed all mm/cm/m/in × deg/rad pairs:
new annotation basis, unchanged properties, tolerance commit/reopen/Cancel,
unchanged canonical Sketch values, exact no-op layout, offset/rotation entry,
read-only measured labels and independent Drawing fields. The new numeric-input
fixture explicitly selects the C locale for its dot-decimal test strings.

Passed gates: `zima_cpp_feature_unit_input_contract`,
`zima_cpp_dimension_layout_contract_tests`, `zima_cpp_inline_units_ui_contract`,
`zima_cpp_translations_contract` and `zima_cpp_measurement_dimension_ui_contract`.
The general `zima_cpp_ui_contract_tests` initially failed its existing plane
click/hover check during a combined run; an isolated repeat passed without any
production change to that path. This transient result is recorded rather than
claimed as a fixed plane-selection defect. No new UI strings or native fields
were introduced in this authoring stage. All existing labels remain covered by
the five-language source/catalog and actual-control verification.

The following stage closes quantity validation at model-layout command
boundaries. The complete drawing/export unit policy and remaining typed
relation bindings still require verification. Properties must not invoke OCCT
or change reference identities merely to establish quantity metadata.


### Model annotation quantity validation (2026-10-03)

The existing native dimension catalog now carries derived quantity metadata.
It describes lengths, angles and scalar counts/ratios without changing any owner,
semantic key, allocated dimension number or serialized identifier. Model-layout
commands check an explicit annotation unit against that quantity before creating
an undo transaction. This closes the path that accepted `rad` for a length and
could subsequently fail during View formatting. Empty annotation units retain
the established canonical interpretation.

The catalog distinguishes Sketch angular kinds, placement rotations, reverse
Revolution angle, Feature draft/rotation angles, Transition rotations, thread
angles, Pattern counts and Assembly angular-reference limits. Changing metadata
alone must not alter stored identity. Count-label property edits do not attach
an implicit physical unit. Catalog thread designations remain literal and their
underlying diameter remains a length; this audit does not change thread systems.
No native file-format or template change is required by derived metadata.

Rebuilt validation passed: `zima_cpp_dimension_identifiers_contract_tests`,
`zima_cpp_model_dimension_layout_command_tests`,
`zima_cpp_dimension_layout_contract_tests`, `zima_cpp_feature_unit_input_contract`,
`zima_cpp_inline_units_ui_contract` and `zima_cpp_translations_contract`.
The command test rejects deg/rad on a length, a length unit on rotation, and
mm/rad on a real Pattern count without changing the revision, cached geometry or
published workspace state. Both deg/rad annotations on rotation and unitless
count appearance succeed. Existing Undo/Redo, native reopen, nested occurrence
and common picker checks pass. Derived quantity changes leave the serialized
identifier map unchanged. Existing diagnostics are reused and verified in all
five languages; no new user-visible messages were added.

### Typed trigonometric relations (2026-10-03)

The audit reproduced inconsistent angular bases: values were tagged as angles
in document units, but trig functions read their numbers in a fixed named basis,
and inverse functions returned the same type with different numeric bases.
The evaluator now receives the document's angular scale. Typed angle inputs
respect that scale, while scalar sin/cos/tan and sind/cosd/tand retain their
radian/degree conventions. Every inverse function returns a document-unit angle.
Canonical geometry remains in degrees; there is no native format change.

Explicit source conversion adjusts the typed argument/result boundaries too.
It preserves enclosing arithmetic, rounding, conditions, text and comments;
conversion does not execute inactive domain failures. Repeated degree/radian
conversion is checked across all outputs, including nested inverse functions
and trig guards. Constant quantity exponents use the source angular scale,
and converted inverse results retain grouping when used as divisors. Signed
zero and tangent/domain rejection remain covered.

Core checks cover both angular units and all four length units. Native command
checks create a spherical sector with a relation-driven 60-degree Revolution,
and subtract that sector from a real Assembly occurrence. Their independently
expected volumes are 6*pi and 8000-6*pi cubic millimetres. Fresh radian formulas,
repeated unit conversion, explicit Regenerate, complete Undo/Redo and native
save/reopen all retain those values. A nested inverse/sine expression also
drives an Extrusion length through unit conversion and native reopening.
Both relation evaluation phases and source-save validation use the same scale.

No new user-visible text or translation keys are introduced. Existing quantity
and domain errors retain their localized diagnostics. The remaining relation
binding catalog work is separate from this evaluator correction.

Final rebuilt verification passed all six targeted suites (10.36 seconds):
`zima_cpp_relation_program_tests`, `zima_cpp_metadata_command_tests`,
`zima_cpp_engineering_metadata_command_tests`, `zima_cpp_family_table_tests`,
`zima_cpp_relations_editor_contract` and `zima_cpp_translations_contract`.
The last suite checks production source coverage, catalogs and affected UI in
Czech, English, German, French and Russian. The Windows application was rebuilt;
this stage is not the final units release.

### Mixed-unit Assemblies and saved measurements (2026-10-03)

A native command fixture now creates a 25.4 x 50.8 x 76.2 mm Part and an
inch-configured 25.4 mm cube with different materials. They enter a centimetre
subassembly, which is inserted twice into an inch parent, including a translated
90-degree copy. The test independently checks scene extents, exact areas/volumes,
kg-based physical masses and distances of the two distinct occurrence paths.
Unit switches preserve scene vertices/triangles, source calculated geometry,
parent history, native identities and saved measurement values. Unsaved source
height and material edits, their Undo, explicit Regenerate and fresh opening of
only the top Assembly are covered.

The fresh-open test reproduced missing nested mass and approximate volume when
the immediate owning subassembly had no open tab. Measurement now uses its saved
native calculation through the existing Family resolver. It does not open source
tabs or run OCCT; a missing dependency uses the existing missing-reference state.
This targeted query can read a closed owner's native sources on demand; it adds
no persistent cache or sidecar and does not change native formats.

A second regression reproduced stale nested mass after current source geometry
changed. Source refresh now publishes the corresponding kg mass and volume basis
with the reused/replaced geometry, including reuse for a second occurrence.
This is physical input refresh only: existing user Parameters remain unchanged,
and Assembly-owned cuts/mates continue to require explicit regeneration.

The GUI test reopens a saved measurement in all four length and mass units,
checks area/volume powers and approximate-distance markers, and verifies unchanged
Save creates no history and preserves canonical values. This stage adds no UI
messages; existing missing-reference localization is reused. Physical-properties
documentation was corrected where it still described automatic relation updates
and frozen ordinary occurrence geometry.

Final rebuilt validation passed seven suites (19.04 seconds):
`zima_cpp_measurement_inspector_ui_contract`, `zima_cpp_translations_contract`,
`zima_cpp_workspace_contract_tests`, `zima_cpp_workspace_publication_tests`,
`zima_cpp_engineering_metadata_command_tests`, `zima_cpp_measurement_edit_command_tests`
and `zima_cpp_measurement_command_tests`. Catalog/source coverage and language
switching pass for cs/en/de/fr/ru. The source-refresh benchmark also passed
shared-snapshot, placement, history, source edit and Undo checks at 256 and 1024
leaves with open/closed sources and different tab orders. This run establishes
correct reuse after the mass fix, not a before/after speed improvement claim.
No native file-format/template update is needed for these changes.
