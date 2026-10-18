# Stud and cylindrical-head screw factory library

## Scope and native history

Engineering Tables contain 74 independent stud Parts from the tabulated sizes
of ČSN 02 1174, 02 1176 and 02 1178, and 40 cylindrical-head Parts: 20 ISO 4762,
10 ISO 1207 and 10 ISO 1580. Seven A2 button-head Parts cover the existing
ISO 7380-1 topic sizes M3–M12. Each diameter/pitch/type has one available default
length. The full ISO 7380-1 scope also includes M16; the current A2 supplier
length source does not cover it, so no M16 catalogue combinations are invented.

Studs start with an origin-coincident, diameter-dimensioned circle at the metal-end
seating plane. The Default extrusion has two independent sides: first `e` in
positive Z, then nominal length `l` in negative Z. Overall length remains `e + l`;
the origin is no longer the midpoint of the complete blank. A single Chamfer
treats both outer ends, followed by separate metal-end and nut-end ShaftThread
operations. Fine pitch applies only to the nut end; the metal end retains coarse
pitch. Existing representative lengths remain unchanged. The stock relation reads
the nominal extrusion side directly. Metal-end thread runout continues beyond
the seating plane into the shank, rather than adding length to the blank.
`update-fastener-library-geometry.py` applies this correction and socket-mouth
updates to copies of the original factory documents through native commands,
preserving document, Body, Sketch and existing feature IDs. Original input hashes
and retained-ID receipts accompany the independently checked candidates.

Cylindrical heads and shafts use separate origin-coincident circular profiles.
The head extends towards negative Z; the shaft towards positive Z. Real Fillets
round the outer head rim and underhead junction, and a Chamfer treats the shaft
tip. A separate ShaftThread keeps its original cylinder, start plane and entry
cone references. ISO 4762 uses the table's full-thread region where applicable:
the root band and 2P runout leave the specified 3P maximum smooth approach to
the head. Other selected lengths retain their table thread-length condition.

The socket profile is an origin-anchored regular hexagon with a driving size and
orientation constraint. It lies on the offset plane at the socket floor and is
subtracted Through All only towards the head face. The opposite side is disabled;
the floor therefore remains real material. Slotted variants use an equivalently
dimensioned rectangular cut on their floor plane. Slot/socket widths use the
tabulated minimum where supplied, so the nominal tool fits. The large-head
ISO 1580 variants use the table's reference head-edge radius.

ISO 4762 has a separate current subtractive Default revolving a small 45-degree
ring at the socket mouth. Its radial lead is an authored `0.1P`, not a prescribed
standard size; it eases the flats and need not replace the six corner tips with
a complete circle. The floor is deeper by that amount so the original minimum
effective key depth remains. Each size also retains the tabulated minimum head
floor thickness. No shared placement or modeling algorithm changes are required.

Button heads use an exact circular-arc profile revolved around a finite native
construction axis. The crown radius is the midpoint of the ISO 7380-1:2022
minimum/maximum range, with its center on the screw axis. The head diameter,
height and reference top diameter are independent driving dimensions; the crown
center is derived, allowing subsequent radius edits. The head, shaft and socket
are anchored to the origin. Existing native 1.4301/AISI 304 supplies a
representative A2 density of 7.90 kg/dm³; A2 is not claimed as a strength class.

There are no obsolete primitive/extrusion commands, artificial fitted-diameter
offsets, external geometry sidecars or dependencies on a user Project. Ordinary
Body ownership and the existing placement implementation are retained.

## Sources and corrections

The preserved historical input is `tools/data/fastener-source-tables.json`.
Availability cells select default lengths; missing combinations are not invented.
Historical warnings remain in the published tables. Public ISO scope confirms
[ISO 4762 product grade A](https://www.iso.org/standard/34460.html),
[ISO 1207 product grade A](https://www.iso.org/fr/standard/54039.html) and
[ISO 1580 product grade A](https://www.iso.org/standard/57369.html).

The original ISO 1207 head-height maximum row was inconsistent with its own
minimum row. `tools/data/cap-screw-reviewed-dimensions.json` records the corrected
nominal/maximum heights and the source, ISO 1207:2011 Table 1. The curated table
records each old/new cell in `reviewed_numeric_corrections`; raw historical input
is unchanged. These checks do not establish exhaustive manufacturing conformity
for every tolerance, finish, strength class or historical ČSN edition. In
particular, property classes below M3 in ISO 4762 require agreement.

## Data, artwork and reproduction

Parts live directly in their topic directories. `0000-index` holds six ordinary
metadata columns, horizontal white 320×240 thumbnails, three independent native
Part/Drawing pairs for each new group, and cs/en/de/fr/ru images with an English
fallback. Main illustrations are exported from the native Drawing renderer;
dimensions attach to real reference circles, thread boundaries and drive-floor
edges. The source Parts are read-only during rendering. Native stock and mass
relations follow subsequent length edits. Default steel records are 5.8 for
studs, 8.8 for socket screws and 4.8 for slotted screws, at 7.85 kg/dm³. Finite filename padding controls sort
order independently of readable designations and model parameters.

Reproduction tools:

- `generate-stud-library.py`, `verify-stud-library.py`, `render-stud-library.py`;
- `generate-cap-screw-library.py`, `verify-cap-screw-library.py`,
  `render-cap-screw-library.py`, and shared `render-fastener-library.py`;
- `install-reviewed-screw-library.py` preflights source hashes, edit receipts,
  unique identities, all five metadata languages and required images before
  installation. Different existing files are never silently overwritten.

## Verification boundaries

Native BRep checks independently verify one valid connected solid, dimensions,
origin anchoring, independent `e`/`l` stud extents, actual chamfers/rounds, thread limits,
mass and fully constrained profiles. Stud volume is additionally compared with
the analytical volume of a cylinder with two chamfered ends. Cap-screw checks
verify the actual six socket walls or two slot walls, width, floor depth, and
the subtractive Default's negative-only Through All definition.

Each of the 74 studs passes three successive length changes: 222 checked bodies.
Each of the 40 cylindrical-head screws likewise passes three changes: 120
checked bodies. Every step covers Undo/Redo, stable source-reference identities,
native save/reopen and independent body checks; original source hashes remain
unchanged by verification. Updated seating-origin evidence is in
`build/stud-seating-stock-final-edits/` (222 nominal-length edits with unchanged
metal-end length, independently checked BReps after every edit). Updated socket
evidence is in `build/cap-entry-directed-final-results.json` (120 length edits,
combining the corrected socket cases with byte-identical slotted source cases). Original
document identity and committed-source receipts are in
`build/factory-identity-source-proof.json`. Earlier cap evidence is in
`build/cap-edit-verification/` and
`build/cap-edit-verification-final-sockets/`; `build/cap-final-edit-results.json`
collects the earlier 2026101802 receipts. These are library authoring checks, not a replacement for the general
Sketcher dimension/drag matrix. No Sketcher algorithm changes are made here.

The actual in-application browser checks the six affected topic pages in all
five languages and Light/Dark/Light themes: images load, paired figures remain
contiguous in one row, headers stay fixed, and clicking a cell marks the complete
row azure without reloading on theme changes. Screenshots and logs are under
`build/factory-fastener-ui/` and `build/final-fastener-browser.*.log`.
Linux runtime execution remains unverified; both platforms consume the same
native files and shared C++ implementation.

The updated A2 button source is
[`button-screw-reviewed-dimensions.json`](../tools/data/button-screw-reviewed-dimensions.json):
dimensions come from ISO 7380-1:2022 Table 1, while listed lengths come separately
from Bossard BN 1593 (2016). The HTML explicitly distinguishes catalogue entries
from current stock and the complete normative length series. Old supplier-image
cells are retained for provenance but no longer displayed; in particular their
socket-depth values are not used in new Parts. The seven button Parts pass 21
length edits and 21 crown-radius edits with Undo/Redo, stable references,
save/reopen and independent BRep checks. Evidence is in
`build/button-final-edits/`. Their 2P thread runout ends at the underhead fillet;
an extra arbitrary smooth cylindrical band is not added. Generation reuses the shared cylindrical-head
authoring and native socket paths through `generate-button-screw-library.py`.
Native profile checks also drag every point repeatedly with unlocked and locked
driving dimensions. Unlocked dimensions retain their existing editable-handle
contract; locked profiles keep their positions and origin contacts. The entrance
profile uses directed coordinates so its locked triangle cannot mirror above
the head. These checks operate on copies and do not alter factory sources.

## HTML presentation and reference dimensions

Approximate mass sections and mass rows are omitted from published fastener HTML.
The archived source cells remain intact for provenance. Native calculated mass
and directory metadata are retained. The stud mass grid supplies a separate
thread/nominal-length matrix: `✓` means listed in the historical source and `—`
means not listed; it makes no claim about current stock or manufacturing.
Headers explicitly show both coarse and fine threads where the source groups
them. M3 has only 12, 14, 16, 18, 20, 22, 25 and 28 mm listed.

Socket-head and slotted cylindrical-head illustrations put `b` above the shaft
at the same height as `t`. The socket end view includes a real attached `e`
across opposite hexagon corners, alongside `s` across flats. ISO 4762:2004
Figure 1/Table 1 specify `e`/`e min.`; parentheses are not added. The illustration
measures the actual regular-hexagon model rather than substituting the tabulated
minimum. Its dimensional source is the
[ISO preview](https://cdn.standards.iteh.ai/samples/34460/06335046afaf46fb8e84d91a3eda001d/ISO-4762-2004.pdf).
