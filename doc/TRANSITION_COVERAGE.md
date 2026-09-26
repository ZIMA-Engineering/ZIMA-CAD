# Sheet transition coverage

Repository capability review, 2026-09-26. This is an inventory and proposed
follow-up order, not a claim that the missing commands are implemented.

## Verified current scope

| Shape or operation | Current capability | Boundary |
| --- | --- | --- |
| Semicircle to rounded half-rectangle | Sheet Transition, including two-axis tilt | One open semicircle and a three-side half-rectangle with two equal tangent corner radii |
| Rectangle to rectangle | Rectangular Sheet Transition, L or U, XYZ displacement and rotation | Two or three adjacent sides; intersecting panels/developments are rejected |
| Coaxial round reduction | Conical Revolved Sheet and its development | An open angular sector; a closed 360-degree region lacks a development seam |
| Cylindrical sector | Revolved Sheet and Unbend | The same full-circle seam limitation |
| Twisted strip | Twisted Sheet with manufacturing flat-length correction | Constant-width strip; this is not a general transition between two arbitrary profiles |
| Composite panels and bends | Flat, Sheet Profile and attached sheet features | Can construct some parts manually; not a dedicated transition generator |

Sources: the profile validators in
`cpp/research/transition_sketches.cpp`, the models in
`cpp/research/transition_half.hpp`, native transition command/state regressions,
[Sheet Metal](SHEET_METAL.md), and
[Sheet state development](SHEET_STATE_DEVELOPMENT.md#development-maps).
The present release tests representative L/U and half-profile cases, not every
possible spatial configuration or manufacturing process.

## Missing dedicated capabilities

1. **Closed four-wall rectangular transition with an explicit seam.** The
   rectangular validator currently accepts only two or three adjacent walls.
   A manufacturing seam and its flat boundary need explicit native ownership.
2. **Complete round-to-rectangle transition.** The existing command makes an
   open half. Building multiple halves is a possible modeling route, not an
   existing one-command complete transition with managed seams and pieces.
3. **Eccentric or oblique round-to-round transition.** Revolved Sheet covers
   rotationally symmetric material about one axis. There is no two-circle
   transition command with independently positioned endpoint frames.
4. **General polygon/profile-to-profile transition.** Triangles, trapezoids,
   unequal polygon vertex counts, ellipses, oval profiles and arbitrary mixed
   Sketch curves do not pass the current specialized transition validators.
   The kernel's general solid loft/sweep capability alone does not establish
   sheet material ownership or a usable flat pattern for these cases.
5. **Segmented elbows and Y/T branches.** There is no dedicated sheet
   transition command defining their segments, intersections, seams and separate
   blanks. Manual composition is distinct from a tested manufacturing command.

## Proposed next scope

First address explicit seams and complete rectangular/round transitions; these
extend the existing profile families. Then consider eccentric round-to-round
transitions, followed by a general open polygon strip with authored profile
correspondence. Branches and segmented elbows should be separate features.

For each candidate, specify the two profiles, frames, material, bend radii and
seams as inputs; use the existing native history and sheet-material maps as
means; require valid formed pieces and identified manufacturing blanks as
outputs. Independently verify connection, thickness, edge lengths, material
mapping, native save/reopen and Unbend/Bend Back. The user's acceptance of
weldable apex relief does not by itself define an approximation policy for
arbitrary doubly curved profiles.
