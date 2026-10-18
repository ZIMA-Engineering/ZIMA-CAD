# Fitted bolt library

The factory Engineering Tables contain 24 independently editable Parts: twelve
sizes each for ČSN 02 1111 (long thread) and ČSN 02 1112 (short thread). They
derive from the user's approved `HFB-CSN021111-M10x050.prtz` history. The original
Project is never overwritten by generation, illustration or verification.

## Geometry and provenance

Dimensions come from the existing historical fitted-bolt table in
`tools/data/fastener-source-tables.json`, including its nonpreferred sizes and
M36×3 fine thread. The page's historical-source warning remains. Public previews
of [ČSN 02 1111](https://nahledy.normy.biz/n.php?i=68481) and
[ČSN 02 1112](https://nahledy.normy.biz/n.php?i=68482) identify the 2003 editions
as replacements for the 1967 editions; their previews do not expose all numeric
requirements. These factory models are therefore not a claim of complete
verification against the current editions. DIN 609 dimensions are not substituted.

Each family has one practical default length per size, chosen from the table's
available mass rows at or above 50 mm. Thread length uses the corresponding
length band. The runout is 2P and ends at the fitted shoulder; it cannot extend
through the head. The thread blank retains its real nominal diameter, while the
fitted cylinder uses the table's separate diameter (M10 uses Ø11). Tolerance n6
is authored metadata and a diameter annotation, not an arbitrary geometric
epsilon. Filenames use padded numeric fields, for example
`HFB-CSN021111-M10x050.prtz`; names and native parameters remain readable.

The native seven-operation pattern retains its authored semicircular underhead
relief, 30° shoulder and end chamfer. The relief is not described as a quarter
circle fillet. End-chamfer properties use 60° relative to the end face and a
radial setback of 0.4P/1.5; the resulting cone is 30° to the shaft axis and its
axial depth is the radial setback times √3. The fitted diameter is enlarged
before resizing its source shaft, so intermediate authoring transactions retain
a positive shoulder width and do not reverse the angular branch.

## Data and artwork

Production Parts live directly in `02-FITTED`. The `0000-index` directory owns
metadata, horizontal 320×240 thumbnails on white, two independent representative
Part/Drawing pairs and localized main images for cs/en/de/fr/ru. The fallback PNG
uses English. Artwork derives from the approved native Drawing pattern, with
real geometry references and independently checked dimension values. The R
leader stays above neighboring extension lines. Native names, parameter labels
and the underhead-relief legend are translated in all five languages.

`tools/generate-fitted-bolt-library.py` authors candidates using native commands;
`tools/render-fastener-library.py` and
`tools/render-fastener-reference-drawings.py` render them without modifying the
production sources. `tools/install-fitted-bolt-library.py` checks every candidate,
verification receipt, identity, parameter and required image before installation.

## Verification

All 24 candidates passed independent OCCT BRep checks for one valid solid, head
dimensions, nominal and fitted cylinders, 30° shoulder position, relief radius,
end chamfer, calculated mass, fully constrained Sketches and thread limits.
All 24 also passed three successive length edits, Undo/Redo and save/reopen:
72 edited bodies were independently checked. Their source hashes stayed intact.
Both representative Drawings have resolved, measured dimensions and five
localized rendered images. Evidence is under `build/fitted-bolt-library-ordered/`,
`build/fitted-bolt-library-edit-verification/` and
`build/fitted-artwork-ordered-02/`.

The regression matrix additionally exercises 96 orientation, endpoint-order,
native/default-axis, source-contact and dimension-lock combinations of coupled
diameter/angle edits. Enlarging the wall precedes enlarging its contact radius;
shrinking the contact precedes shrinking the wall. This avoids requesting an
intermediate chamfer with reversed physical direction. Linux runtime execution
and complete numeric verification against the current standard remain outside
this Windows acceptance.
