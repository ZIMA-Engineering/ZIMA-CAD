# Cap-nut dimensional review

The archived CSN 02 1431 illustration and table are not a sufficient basis for
approving the current candidate crown and blind-end execution. They must not
be silently replaced by a differently dimensioned DIN family.

For archived M8, the table gives overall m5=16.5 mm, minimum threaded b=11 mm
and p=16 mm. The candidate interpreted p as a flat-ended cylindrical bore depth
and selected an otherwise unspecified spherical crown R=2d. This leaves only
0.5 mm at the pole and about 0.15 mm axially at the bore rim. A valid, editable
solid does not establish a reasonable manufactured closed end.

The candidate's exact axial clearance at bore radius rb is independently
calculated as `h - R + sqrt(R*R - rb*rb) - p`. The current 21-size review is in
`build/cap-nut-native-candidates-v2/end-wall-review.json`. These models and their
illustrations remain uninstalled review candidates. Preserve source values;
do not arbitrarily reduce p to make a thicker wall.

The older 1963 extract in the historical engineering-table scan uses different
sizes and symbols again. It is useful evidence of a construction, not permission
to mix its dimensions with the later archived table.

## Separately specified DIN candidate

The primary [Böllhoff DIN 1587 brass product sheet](https://eshop.boellhoff.de/out/media/pdf/DIN_1587_Messing___en.pdf)
provides another clearly identified execution. Its drawing includes a domed
upper portion, bounded hexagon height m, crown radius r and separate thread-end
detail. That detail is not sufficient evidence for a conical drill bottom. The
original DIN drawing distinguishes the closed bore from the thread runout or
undercut. Its axial t dimension and the archived p must not be assumed identical.

For M8, this sheet gives overall h=15 mm, s=13 mm, m=6.14–6.5 mm,
crown r=6.25 mm and t=10.65–11.35 mm. These differ substantially from the
archived CSN candidate. The sheet covers a brass product; it does not establish
steel material properties, every available thread variant, or universal
interchangeability between CSN 02 1431 and DIN 1587. The user approved a separate
DIN family on 2026-10-19 while retaining CSN 02 1431 for further review.

The PDF page was rendered and visually inspected in
`build/cap-extension-sources/DIN1587-Boellhoff-brass.png`; the original downloaded
PDF remains beside it as read-only research evidence. Factory assets must use
native authored models and drawings, not copied manufacturer illustrations.

## Reviewed DIN CAD execution

The separate family contains 12 coarse and 14 fine-pitch sizes from M4 to M24.
`tools/data/din-cap-nut-reviewed-dimensions.json` records the manufacturer and
original-standard sources, the M10 hexagon-height typo correction, and edition
limits. Nominal ISO wrench widths are retained instead of legacy stock widths.
The material is generic steel at 7850 kg/m³, without a strength-class claim.

The selected usable thread depth t is the midpoint of the published interval.
The model's flat-bottom bore is `p = min(h - w_min, t + pitch)` and its actual
axial pole wall is `w = h - p`. The tabulated minimum wall is a limit, not a
target drilling depth. The nominal thread and selected 0.5-pitch runout are
schematic; no detailed DIN 76 tooling or complete 2021 manufacturing compliance
is claimed. The 2021 preview omits the detailed dimension pages.

The 26 native candidates are under `build/din-cap-nut-closed-wall-candidates`.
The independent volume oracle integrates the actual hexagon/disk union even
when a test radius grows beyond the inscribed circle. Its overlap integral is
split at the exact geometric transition to avoid treating a derivative kink as
a smooth integration interval. Neither this correction nor its regression
changes the model geometry or omits enlarged-radius edits.

See [derived opening dimensions](OPENING_DERIVED_DIMENSIONS.md) and
[section hatching](SECTION_THREAD_RUNOUT_HATCHING.md) for defects discovered and
corrected during this verification. The historical CSN assets remain uninstalled.
