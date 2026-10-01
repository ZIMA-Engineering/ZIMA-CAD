# Extrusion and Revolution Properties opening — 2026-10-01

## Scope and implementation

Inputs are the calculated Part, its persisted history boundary and the pending
profile parameters. The required output is the existing editable profile wire,
dimensions, reference-entry state and rollback scene, with unchanged modeling
precision, persistence and confirmation behavior.

Previously, opening an existing Part Extrusion or Revolution from the Tree
published the final scene, published an initial profile context, then installed
rollback and published the input scene. The initial callback ran before rollback
was available. For Through-all, a downstream body's extent could therefore drive
the initial wire even though the final displayed solid was the earlier input.

The Tree now omits its redundant preliminary publication for these two command
kinds. For ordinary edited Part profiles, callback registration is deferred until
the existing rollback and dialog context are installed. The callback publishes
the input scene once. The original opening fit is retained; later parameter edits
preserve the camera. No new cache is introduced.

Sheet-cut previews retain their special input preparation and callback order;
only their Tree pre-refresh is skipped. Assembly-owned cuts, creation, general
Feature, shared placement solving, commit/Undo and dialog destruction are not
changed. Closing performance is outside this change.

## Measurements and equivalence

Serial native Windows Release probe, baseline `dd5459ac`, with identical probe
instrumentation before and after. Two small circular-profile fixtures are used:
ordinary Extrusion and two-sided Revolution (120/20 degrees). Each is recreated
three times by the existing profile-frame harness. Its `plane` log label comes
from that harness; the opening-only probe runs before changing placement planes,
so those three runs are independent instances of the same starting geometry.

Each instance first opens and cancels to establish the normal post-close
annotation state, then performs three unchanged OK and three Cancel operations.
Means below use trials 1–5 in each instance (15 samples per command).

| Command | Before | After | Reduction | Base-scene publications |
| --- | ---: | ---: | ---: | --- |
| Extrusion | 57.747 ms | 50.240 ms | 13.0% | 3 → 1 |
| Revolution | 66.082 ms | 54.306 ms | 17.8% | 3 → 1 |

All 36 corresponding preview RGBA SHA-256 hashes match. Each probe also checks
repeatable preview images, exact restored viewer-packet serialization, camera,
document state and Undo/Redo availability. The first-ever post-load close clears
normal inspection annotations, so restoration is compared from the established
post-close state rather than incorrectly treating that initial cleanup as a
regression. Timings are small-fixture GUI measurements, not a kernel speedup or
a prediction for large user models.

Raw evidence: [opening probe logs](20261001-profile-properties-opening.txt).
Run the probe using `ZIMA_VERIFY_PROFILE_OPEN_ONLY=1` and `--verify-startup` with
Windows/Fusion. No product timing threshold is imposed.

## Regression coverage

The new `zima_cpp_profile_opening_limits_ui_contract` creates a stock body,
Extrusion cut and much larger downstream body. It checks every initial transient
wire point against preview geometry calculated from the stock input for
one-sided Through-all, two-sided Through-all and two-sided Up-to datum planes.
This verifies the actual rollback input rather than only counting refreshes.

The existing owned-profile external-reference GUI fixture initially failed
before opening Properties: it replaced the Sketch list and discarded the source
solid's authored profile. Its fixture now retains both Sketches, locates the
edited Sketch by ID and retains source profile/Body ownership when separating
Assembly components. These are verification-only repairs; native validation and
product reference semantics are unchanged.

Localization review: no user-visible product text is added or changed; fixture
names and diagnostic messages are test-only. English documentation, unchanged
five-language catalogs, native file format and root `zima-cad.bat` launcher.
Linux verification and portable packaging are not part of this change.

Verification: the initial selected Windows run passed nine of ten contracts
(156.73 s); the remaining external-reference fixture failed during preparation
as described above. After its repair, both the updated limit fixture and the
complete external-reference GUI contract passed (24.70 s). Thus all ten selected
contracts have passed: profile frames, opening limits, profile-on-sheet,
translations, extrusion limits, Assembly profile commands, Feature prototype,
selection filters, owned-profile external references and surface profiles.
The native GUI was rebuilt successfully. The external-reference test includes
Part and activated Assembly context, import, projection, Cancel/OK, persistence
and Assembly dependency Undo/Redo.
