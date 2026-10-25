# Sketch face-reference confirmation

Status: development candidate, not yet installed as the normal user runtime.

## Measured cause

The production Skeleton copy used for measurement is 137,707,218 bytes. The
reference-entry test opens actual Sketch Properties, arms its first placement
row, sends real mouse events in the View, cycles the common candidate list to
an offered face, confirms it and cancels the pending command. It checks unchanged
document state and stable base-scene revision during hover. Cold mouse movement
is measured before warming the candidate filter with direct queries.

The observed confirmation took 2,615 ms. Placement equations took approximately
3.5 ms; most time was spent rebuilding the same scene three times. The shared
placement row first notified Sketch Properties before its automatic work-plane
adjustment was complete. The work-plane update published another preview, and
the reference confirmation path published an additional final scene.

## Change

Sketch Properties still performs the same synchronous reference assignment,
work-plane choice and resolved-placement updates. Its own preview notifications
are coalesced until that reference update is complete. The View confirmation
path consumes the completed Sketch preview when it actually advanced the base
scene revision, rather than publishing it again. Deferred whole-Origin entry
and dialogs without a published Sketch preview retain their existing refresh.

The shared placement solver, assignment rules, offsets, sides and persistence
contracts were not changed. Other dialogs retain their established lifecycle.
This is command-local scene ownership, not global callback suppression.

## Result and verification

On the same Windows Release build configuration and model copy, confirmation
took 971 ms, approximately 63% less than the observed 2,615 ms baseline. The GUI
test asserts exactly one base-scene publication for that confirmation. Hover
and Cancel leave the document unchanged. All 16 sampled positions retain
deterministic candidate order across repeated queries. The observed slowest
first hover remains approximately 343 ms in a dense region with 433 candidates;
ordinary sampled hover was mostly 17–37 ms. This change does not eliminate every
remaining selection or scene-construction cost.

The Loft GUI regression passes five languages, owned 2D Sketch placement and
editing, whole-Origin entry, inspection, ordering, rollback, unchanged OK,
Cancel, native reopen and Undo/Redo. Additional ordinary/owned Sketch and Bend
reference regression results remain to be consolidated. The earlier recorded
placed-Sketch baseline failure must be reported separately if it persists.
The change introduces no user-visible text. Linux execution is not verified on
this Windows host.

Private evidence is in `build/loft-audit/reference-confirm-stage-profile-candidate.log`
and `reference-single-publication-verified-candidate.log`. The deliberately
discarded run started before the new executable had finished linking and is
not performance evidence.
