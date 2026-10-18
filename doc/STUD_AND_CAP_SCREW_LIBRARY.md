# Stud and cylindrical-head screw factory library

## Scope and native history

Engineering Tables contain 74 independent stud Parts from the tabulated sizes
of ČSN 02 1174, 02 1176 and 02 1178, and 40 cylindrical-head Parts: 20 ISO 4762,
10 ISO 1207 and 10 ISO 1580. Each diameter/pitch/type has one available default
length. The separate, image-only ISO 7380/A2 button-head topic is outside this
cylindrical-head generation; no dimensions are guessed from its preview.

Studs start with an origin-coincident, diameter-dimensioned circle and a symmetric
Default extrusion. A single Chamfer treats both outer ends, followed by separate
metal-end and nut-end ShaftThread operations. The symmetric half length is half
of `l + e`: nominal length excludes the metal end. Fine pitch applies only to the
nut end. The metal end retains coarse pitch. Both complete thread bands and their
2P runouts stay on their respective original extrusion halves. Default lengths
avoid the historical short-stud exceptions.

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
origin centering, symmetric stud extents, actual chamfers/rounds, thread limits,
mass and fully constrained profiles. Stud volume is additionally compared with
the analytical volume of a cylinder with two chamfered ends. Cap-screw checks
verify the actual six socket walls or two slot walls, width, floor depth, and
the subtractive Default's negative-only Through All definition.

Each of the 74 studs passes three successive length changes: 222 checked bodies.
Each of the 40 cylindrical-head screws likewise passes three changes: 120
checked bodies. Every step covers Undo/Redo, stable source-reference identities,
native save/reopen and independent body checks; original source hashes remain
unchanged by verification. Evidence is in `build/stud-edit-verification/` and
`build/cap-edit-verification/` and
`build/cap-edit-verification-final-sockets/`; `build/cap-final-edit-results.json`
collects the receipts for the exact final native source hashes. These are library authoring checks, not a replacement for the general
Sketcher dimension/drag matrix. No Sketcher algorithm changes are made here.

The actual in-application browser checks the six affected topic pages in all
five languages and Light/Dark/Light themes: images load, paired figures remain
contiguous in one row, headers stay fixed, and clicking a cell marks the complete
row azure without reloading on theme changes. Screenshots and logs are under
`build/factory-fastener-ui/` and `build/factory-fastener-ui-1802.*.log`.
Linux runtime execution remains unverified; both platforms consume the same
native files and shared C++ implementation.
