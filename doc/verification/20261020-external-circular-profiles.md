# Analytic external profiles and retained unresolved references

## Requested behavior and persisted scope

The user approved the external-profile persistence extension on 2026-10-20.
A projected edge that is actually circular in the receiving Sketch becomes an
ordinary native Circle or Arc. A projected ellipse or other noncircular exact
curve retains its exact rational B-spline representation. Display tessellation
does not determine the curve class.

The change applies to external references and Reference → contour. An unavailable
source retains its identity, last usable projected support and owned geometry;
it is marked broken. Returning the same source can repair the same link. Explicit
detachment preserves the owned geometry. Changing the Sketch frame no longer
silently removes an unavailable reference and its authored constraints.

External-profile import blocks now require a nested `external_profile` contract:
version `1`, kind `source_curve`, `circle` or `arc`, source direction and a circle
parameter origin. Other import blocks do not carry this object. The native
document extensions and outer Part version are unchanged. This role-specific
contract is validated strictly; there is no runtime legacy conversion path.
Custom libraries and projects are not rewritten in bulk.

## Implementation contracts

Circular rational quadratic spans are certified using the full homogeneous
circle polynomial. Other exact curves keep the existing NURBS path. Native
analytic profiles own real native points; their source radius driver and import
ownership prevent independent edits from changing the read-only source.

Trim retains the original declared projection type and canonical supporting
parameterization. The circle seam and arc direction remain meaningful after
rigid corrections, source changes and repeated trimming. Canonical supporting
curves are reconstructed without OCCT. General spline supports also receive the
block's rigid correction. The generated source-radius driver is replaced by the
retained exact support when Trim replaces an analytic curve with a NURBS piece;
unrelated authored constraints retain the existing remapping rules.

Broken references remain broken in public queries and persistence. Structural
validation and solving can use a private snapshot of their last valid support,
so an unavailable source does not prevent saving the remaining owned geometry.
Axis-point failure handling retains its existing separate policy. Conflicting
frame projections retain their last valid link instead of auto-detaching it.

The shared container-placement implementation was not changed. The same C++
modeling implementation and OCCT 8.0.0 pin serve Windows and Linux.

## Factory and start-template verification

The tracked native-file inventory identified two live external-profile blocks
in `config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz`, serialized both in
the feature definition and its entity data. Three Sweep test inputs embed the
same contract. Two other historical test inputs contain an absolute import
filename with the text `external-reference:` inside it, rather than a live
virtual profile link; they do not require the new role contract.

An explicit offline preparation added the declared `source_curve` kind to the
affected authored spline blocks. Old derived caches were discarded because
their input fingerprints no longer matched. The changed native CLI then opened,
regenerated, saved and reopened all four affected files. This did not convert
the authored factory splines into a different curve class.

The factory model's authored data, excluding the new metadata, compare exactly.
Its final serialized BRep, original references at every history boundary and
kernel topology bindings compare exactly. Derived property evaluation and one
intermediate BRep serialization differ after recalculation. The three test
inputs additionally refresh their reference display caches, current serializer
defaults and calculated mass; their authored native curves and dimensions are
preserved. Evidence is under `build/external-profile-assets/`.

The native refresh tool saved and reopened all six mm/in Part, Skeleton and
Assembly start templates. Empty start documents need no role metadata, so their
committed bytes remain unchanged. The real GUI New Document tests verified the
factory templates, active first editable contexts and normal commands. Native
FORM definition tests passed all 24 Body role orders, source ancestry, cold
regeneration and all three thickness sides. The FORM command tests passed
repeated thickness edits, Undo/Redo, rotation, suppression, repeated occurrences
and native persistence.

## Regression evidence and remaining verification

The initial native matrix passed 192 geometry/state combinations: three planes,
Circle/Arc, original/body edge, both sides, minor/major arc spans, rigid
corrections and locked/unlocked driving-dimension attempts. Each combination
checks both R and D, successive reference values, atomic rejected driving edits,
three attempted drags of every owned point, successive source radius/center
changes, independent circle equations, stable identities, read-only sources,
loss, repair and explicit detachment. This is 384 R/D combinations, not 384
different geometry fixtures. Sixteen additional retained-interval cases check
multiple trim pieces, corrections, source changes, loss and serialized reopen.
Four native Part cases check unresolved geometry through session Undo/Redo and
save/reopen. Incompatible source kinds and unsupported metadata are rejected
without losing the owned curve.

The core document/OCCT suite, complete Sketcher suite and reference command suite
passed. Existing Sketcher coverage includes transformed native/external R/D and
angle drivers, locked/reference dimensions, tangent contact sides, selection
orders, repeated edits/drags, independent equations and serialized reopen.

Real GUI contour tests passed mouse insertion of a circular Body edge and an
arc Body edge, native analytic type, one-step Undo/Redo and native save/reopen.
Both screenshots were visually inspected. Existing owned-profile reference
tests passed Part/Assembly context, picking, projection, parent OK/Cancel and
dependency persistence. Template editing and New Document GUI suites passed.

The expanded matrix also passes point Distance/X/Y and three-point angular
dimensions in both selection orders, supported reference values, rejected
unsupported locked reference states and rejected conflicting driving edits.
Twenty-four neighbour configurations preserve ordinary editable Circle radius
drivers and actual free-centre movement. Fully dimensioned unlocked coordinate
drivers follow intentional drags and their new values satisfy the independent
coordinate equations; locking them rejects repeated drags atomically.

The updated external-constraint GUI suite passes all 25 scenarios, including
actual tangent-profile trimming with native Arc input. Its former B-spline-only
test filter was replaced with projection ownership; it still requires the real
two trim pieces and confirms mouse interaction. A general spline whose source
control-point count becomes incompatible also keeps its last valid reference
packet, not just its visible owned points. The regression failed before that
packet correction and passes after it; repeating the same incompatible refresh
is a no-op and the original compatible source repairs the retained link.

The existing ellipse-offset extrusion test exposed an independent earlier
validation defect, reproduced with the preceding CLI. A closed spline's two
ends differed by only `4.71002e-16`, but single-curve closure required bitwise
coordinate equality. The fix uses `1e-12`, matching the existing native distinct
endpoint threshold, without modifying curve control data, evaluation accuracy
or the separate elliptical-arc policy. Eight ellipse axis/orientation/side cases,
two narrow circular rings and invalid/open/nonfinite single-spline cases pass.

The diagnostic is translated in cs/en/de/fr/ru. Production source coverage,
catalog key equality, placeholders, nonempty translations and real UI language
switching passed in `zima_cpp_translations_contract` (20.08 s).

Final evidence:

- `build/external-projection-followup-tests.log`: tangent GUI, all-language
  coverage/catalog/UI checks, expanded analytic matrix and exact spline
  extrusion, all passing (87.55 s).
- `build/external-projection-final-kernel-regressions.log`: document/OCCT,
  complete Sketcher and cold factory FORM contracts, all passing (77.44 s).
- `build/external-frozen-packet-final-tests.log`: final packet correction,
  Sketcher/reference suites, real contour GUI and exact spline cases, all five
  passing (34.97 s).
- `build/external-profile-assets-accepted-calculation.log`: all four converted
  native assets regenerated, saved and reopened with the accepted runtime;
  output SHA-256 values remained identical to the first successful conversion.
- `build/external-profile-start-template-refresh.log` and
  `build/external-projection-final-tests.log`: six refreshed/reopened start
  templates, real New Document and template GUI checks, owned-profile GUI,
  factory command and independent spatial/flat FORM geometry checks. The two
  failures in that earlier combined log were corrected and reverified above.

All executed evidence is Windows-native. Linux build/GUI execution and a new
packaged-runtime release have not been verified. The dedicated new contour GUI
test uses the current UI language and theme; it does not independently exercise
every dimension kind by mouse in every language or theme.
