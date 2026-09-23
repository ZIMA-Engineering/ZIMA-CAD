# Sheet Profile corner closure

Sheet Profile properties expose **Corner closure** independently for the first
and second profile endpoints. Enable the meeting endpoint on each neighbouring
profile. The existing end-profile extension dimensions still define the terminal
width; this option curves the transition to that width through the bend.

The default **Corner gap** is 0.05 mm (allowed range 0–1 mm). Each enabled
endpoint retracts by half this value along its own width direction. It is a
per-profile allowance, not a measurement of the diagonal separation between
neighbouring edges. A small opening is intentional for bending and welding.
Disabled ends retain their original linear transition.

## Geometry and limits

Inputs are the authored arc, thickness, radius, endpoint extensions and gap.
The means are rectangular sections of constant thickness and the existing
smooth loft/material mapping. The output is a curved corner boundary in the
folded part and its corresponding boundary in the flat pattern.

For arc fraction `f` and angle `a`, the enabled endpoint extension is
`(extension - gap/2) * sin(a*f) / sin(a)`. This closes the inner opening of the
matched perpendicular profiles in the PLECH example. It does not search for or
modify a neighbouring feature. Unequal radii, angles or unmatched endpoint
dimensions may require adjustment; it is not a general automatic mitre solver.
Angles must be strictly between 0 and 180 degrees and the inner radius must be
positive. Hem closure is unsupported.
The terminal section also controls a following straight segment.

Sampling uses a 0.01 mm circular chord criterion, at least eight and at most 128
intervals. Excessive sampling is rejected rather than allowing unbounded work.
The target accepted for this feature is 0.1 mm, with a preference for clearance
over overlap. No global modeling tolerances or geometry-side choices change.

Only an explicit calculation invokes OCCT. Property previews consume authored
geometry. The loft's faces and edges retain their original profile and arc
parents; temporary sampling stations never become persistent reference owners.
Shared arc/straight vertices use their canonical authored station identity.
Unbend bend-line extents interpolate the actual sampled sections. Sheet Cut
recognizes cylindrical spline skins at calculation time and projects its cut
onto an analytic cylinder within the existing kernel tolerance.

## Persistence and commands

The native Bend parameter block optionally stores `corner_closure` with `ends`
(two booleans) and `gap` in millimetres. An absent block represents the disabled
default. No document version change, sidecar or external geometry is required.
Factory start documents contain no Sheet Profile; their default behavior remains
unchanged.

`bend.create` and `bend.set` accept `corner_first`, `corner_last` and
`corner_gap_mm`; `bend.get` reports the same values. These use the normal atomic
history transaction, native persistence and Undo/Redo.

## Verification

`zima_cpp_bend_command_tests --verify-corner-prototype Projects/PLECH.prtz`
reads the original and writes only private copies under `build/sheet-corner`.
It checks the analytic boundary, radial thickness, pairwise overlap, native
round trip, Unbend/Bend Back, Sheet Cut and a cut-state round trip.

On 2026-09-23, the representative calculation measured 1.206 s without closure
and 1.366 s with closure. The maximum sampled boundary error was 1.75e-8 mm,
radial depth error 4.66e-8 mm, and overlap volume zero within numerical precision.
These are fixture measurements, not a performance or accuracy guarantee for all
possible dimensions.

`ZIMA_VERIFY_SHEET_CORNER_FILE` selects the private baseline fixture for the
GUI startup verifier. It checks all five languages, independent ends, disabled
defaults, OK, Cancel, reopening, persistence and Undo/Redo. General Bend,
sheet-state, sheet-cut preview, translation and new-document GUI contracts cover
the surrounding behavior.

The existing cross-branch fixture was refreshed with explicit `body_edge=false`
for its nine original-object external references; no legacy reader fallback was
introduced. All three factory start documents were opened and saved with the
current native writer without content changes, and new-document GUI verification
confirmed their normal initial editing contexts.

Closing Sheet Profile properties with OK or Cancel retires the editing
dimension owner and transient dimension previews before refreshing the normal
scene. Otherwise the saved trajectory and end-profile Sketches repopulate the
dimensions after the preview mesh is cleared. Reopening restores the editing
dimensions; no authored dimensions or saved layouts are deleted. The GUI
verifier checks both closing paths and reopening. This cleanup adds no visible
text and does not change placement solving or modeling calculations.
