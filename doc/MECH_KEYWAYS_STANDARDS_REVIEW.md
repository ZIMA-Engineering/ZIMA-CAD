# Keyways and retaining screws: source review

Reviewed 2026-11-27 for the requested MECH implementation. This is a scoped
evidence record, not certification of all numerical engineering tables.

## Primary-source findings

- At the user's request, ISO alternatives were checked directly in the ISO
  catalogue. [ISO/R 773:1969](https://www.iso.org/standard/5056.html), covering
  rectangular/square parallel keys and keyways, is withdrawn. Its lifecycle
  records withdrawal on 1998-11-03.
- [ISO 3912:1977](https://www.iso.org/standard/9534.html), covering Woodruff keys
  and keyways, is also withdrawn. Its scope concerns cylindrical shafts and
  shaft ends; it cannot alone establish a tapered-shaft depth convention.
- The [ISO/TC 14 catalogue](https://committee.iso.org/cms/live/live/en/sites/isoorg/contents/data/committee/04/62/46214/x/catalogue/)
  lists both as withdrawn and shows no current successor for these general
  parallel/Woodruff-key dimensional specifications. Do not label new library
  parts as conforming to a current ISO key standard on this evidence. Related
  current spline standards have a different scope.
- The user requested a library not restricted to CSN. Provide separately
  identified DIN choices using reviewed data for DIN 6885-1 and DIN 6888;
  retain historical CSN rows with their own identity and review status.
  Withdrawn ISO references may be documented historically, but must not be
  aliases that silently relabel CSN or DIN dimensions.
- The user approved DIN 6885-1 form A and DIN 6888 as the respective default
  series, with CSN selectable separately and the same identity in model/BOM
  metadata. A specific reason for withdrawal of the two ISO standards was not
  found. ISO's [general review guidance](https://www.iso.org/files/live/sites/isoorg/files/store/en/PUB100413.pdf)
  describes possible withdrawal criteria and continued use without ISO
  maintenance; those general criteria are not evidence of the cause of these
  particular withdrawals or of defective key geometry.
- The [Czech standards catalogue, CSN 02 2570](https://seznamcsn.agentura-cas.cz/Detailnormy.aspx?k=68467)
  lists the October 2003 edition for interchangeable keys with two retaining
  screws, with no withdrawal date or replacement shown. It lists Corrigendum 1.
- The [official February 2020 corrigendum](https://seznamcsn.agentura-cas.cz/soubory/opravy/02/68467/Zmeny/509613.pdf)
  adds the equality b = b1 to figure 1. The user's existing diagram already
  includes this equality. This supports the two-hole end-offset interpretation;
  it does not independently verify every diameter, length and tolerance row.
- [Official October 2003 bulletin](https://www.unmz.gov.cz/wp-content/uploads/Vestnik_2003_10.pdf)
  records the 2003 editions of CSN 02 2562, 02 2570 and 02 2575, replacing their
  1953 predecessors. The [one-screw catalogue entry 68468](https://seznamcsn.agentura-cas.cz/Detailnormy.aspx?k=68468)
  was retrieved and checked separately: October 2003 edition, effective November
  2003, no withdrawal date or replacement shown; it replaces the 1953 edition.
- [DIN Media](https://www.dinmedia.de/en/standard/din-6885-1/346315269)
  marks DIN 6885-1:2021-11 current. This is the deep-pattern parallel-key/keyway
  standard. The listing alone does not establish numerical interchangeability
  with the supplied CSN tables.
- Manufacturer [Nosta](https://www.nosta.com/en/standard-products/din-products/parallel-keys/)
  offers round-ended A, straight-ended B and mixed-ended AB keys. Its product
  description also distinguishes drilled C/D/E/F versions from extraction-thread
  AS keys. Key shape and a keyway's actual opening through the shaft end remain
  separate modeling decisions.
- [Maedler's DIN 6885 table](https://smarthost.maedler.de/datenblaetter/K42_861.pdf)
  gives 5.0 mm shaft depth and 3.3 mm hub depth for 10 x 8, whereas the supplied
  historical table gives 4.7 mm and 3.3 mm, with different deviations. These
  are distinct catalogues; do not overwrite or relabel one as the other.
- [DIN Media, DIN 6888:2022-03](https://www.dinmedia.de/de/norm/din-6888/349192507)
  identifies the current Woodruff-key edition. Its listed revisions concern
  referenced standards/materials, ordering, surface finish and editorial changes;
  the catalogue summary is not the complete dimensional specification.
- [Norelem 03289](https://www.norelem.de/en/Product-overview/Flexible-standard-component-system/03000/Dowels-parallel-keys-extractors/Woodruff-keys-DIN-6888/p/agid.2779)
  lists 4 x 7.5 with disc diameter 19 mm and chord approximately 18.6 mm.
  The independent circle-segment calculation is 2 sqrt(7.5 (19 - 7.5)) = 18.574 mm.
  Verify the historical row before editing it; do not substitute diameter 18.
- [Nosta's Woodruff description](https://www.nosta.com/en/standard-products/din-products/woodruff-keys/)
  and [Maedler's Woodruff catalogue](https://smarthost.maedler.de/datenblaetter/K43_734.pdf)
  document tapered-shaft applications. The seat is produced by a circular cutter
  in the shaft's axial plane, with cutter thickness equal to key width. A
  longitudinal flat-bottom capsule is not the Woodruff shaft seat.

## Supplied local evidence

### Additional DIN 6888 numerical review

The [Wernecke manufacturer sheet](https://wilh-wernecke.de/docs/Datenblatt_Scheibenfedern-DIN-6888.pdf)
was rendered and visually checked, including its merged tolerance cells.
The computational catalogue separates series A/B and diameter assignments I/II.
Assignment I is for torque transmission by the key; II is for location when
another element transmits torque. A cone alone does not select assignment II.
Series A is the preferred deep hub groove. The generic catalogue excludes the
sheet's automotive-only 2.5 x 3.7 size. Open upper diameter intervals in II remain
explicitly unbounded, not truncated to an invented maximum.

The sheet misprints the 2 x 3.7 row as 2 x 2.7. The
[MBO 2 x 3.7 product specification](https://www.mbo-osswald.de/shop/scheibenfeder-din-6888-2x3-7-stahl-blank)
independently confirms height 3.7, disc diameter 10, shaft depths 2.9 for A/B,
hub depth 1.0, and depth allowance +0.1. Its drawing guidance permits t1 for the
shaft and D+t2 for the hub. The agreed Woodruff shaft annotation therefore uses
direct t1 at the cutter-centre station. P9 is the common reviewed fixed-width
fit; no assertion is made that historical N9 and the current hub JS9 are aliases.

Historical CSN Woodruff h11 applies to D-t, not direct t1. The corresponding
direct-depth deviations are currently unset pending a verified conversion;
the old fit must not simply be appended to the new depth value. The historical
hub B11 remains attached to its original D+t measurement.

These manufacturer data are dimensional evidence, not a substitute for reviewing
the full current standard. Tapered shaft/key/hub orientation still requires an
independently checked paired construction.

The user supplied the original HTML chapter and both diagrams at:

`C:/Users/vladi/Prace/ZIMA-Engineering/04-MECHANICAL_TABLES/08-PERA A DRAZKY/02-Pera tesna a pera s pridrznymi srouby/0000-index/`

The illustrations were visually inspected. One retaining screw is centered at
l/2. Two have end offset b1 = b and center distance f = l - 2b. The cross-section
separates the key's counterbore/clearance hole from the shaft's threaded hole.
The original printed note identifies ISO 1207 screws; do not substitute a
different screw head without checking its clearance and seat geometry.

The user's original diagrams have been copied unchanged into the native
reference-library chapters for key sizing, parallel keys and Woodruff seats.
The source and destination SHA-256 values were checked equal. Captions in all
five languages identify their historical source; original source files remain
unchanged. Figure ownership is recorded in tools/data/remaining-tables.json,
so regeneration retains these diagrams. HTML layout and preview verification
are separate from the numerical standards review.

The existing table is not yet a reviewed computational catalogue. Known issues:

- The plain-key heading says CSN 02 2565, while the designation and table say
  02 2562.
- A two-screw example states length 56 in prose but 63 in its designation.
- The 2 x 2 mass-per-length entry 0.314 is inconsistent with a basic steel-area
  sanity check; no corrected value is asserted here.
- A Woodruff size/disc-diameter discrepancy was noted in the earlier source
  review and still requires primary-source verification.
- Old key roughness examples are not automatically a complete ISO 21920
  specification, nor proof of universal shaft/hub keyway roughness defaults.

## Implementation consequences

### Explicit CSN selection and reference-table identity (2026-11-27)

The MECH catalogue selector identifies the retained parallel-key data as
**CSN 02 2507 / 02 2562 (historical table)**. The first standard defines shaft
assignment; the second identifies plain parallel keys. The reference chapters
carry the same identifiers and separately identify one-screw CSN 02 2575 and
two-screw CSN 02 2570. The 2003 official bulletin linked above confirms those
standard titles and their replacement of the 1953 editions. The
[official CSN 02 2507 catalogue](https://csnonline.agentura-cas.cz/Detailnormy.aspx?k=68475)
also confirms its shaft-assignment scope.

The 26 existing historical dimensional rows remain unchanged, including
28 x 16 for a 100 mm shaft and 100 x 50 for diameters over 440 through 500 mm.
This change identifies their provenance; it does not certify that every row
matches the full 2003 editions. DIN and CSN remain separate explicit choices,
and an unavailable DIN diameter does not silently select CSN.

### Factory key models and reviewed edge data

The initial native library contains eleven DIN 6885-1 form-A parallel-key
sections, with one editable preferred length per section, and 23 distinct
DIN 6888 form-A Woodruff sizes from assignments I/II. These are nominal models,
not a claim to provide every length, tolerance realization or drilled form.
Both parallel-key ends remain rounded, independently of an open-ended shaft cut.
The Woodruff models retain a nominal sharp outline.

Parallel-key length intervals follow the
[Wernecke DIN 6885 sheet](https://www.wilh-wernecke.de/docs/Datenblatt_Passfedern-DIN-6885.pdf);
the selected stocked lengths follow the
[Ganter form-A catalogue](https://live-katalog.ganternorm.com/pdf/ganter/de/6885.pdf?dispositiontype=attachment).
The computational catalogue records each row's MBO product URL under
`radius_source`, for example the
[10 x 8 x 45 specification](https://www.mbo-osswald.de/en/shop/parallel-key-din-6885-form-a-round-ended-high-sized-version-a-10x8x45-steel-bright).
The selected groove-bottom radii are the reviewed minima: 0.08 mm for widths
2–4, 0.16 mm for widths 5–8 and 0.25 mm for widths 10–18. Parallel-key edge
relief uses the corresponding nominal minimum 0.16/0.25/0.4 mm. These additional
sources do not change the separate Mädler shaft/hub depth provenance.

Models use C45 steel with a typical density of 7,800 kg/m³, as published by
[Ovako](https://steelnavigator.ovako.com/steel-grades/c45/). This is a model
material assumption, not a material certificate. Native Regenerate publishes
mass and dimension-dependent stock Relations. Independent volume checks use
the exact capsule/edge-relief and circular-segment equations; all 34 models
were saved, reopened and edited four successive times without changing sources.
The command receipts and source hashes are recorded in the verification report.

The reference chapters retain the user's unchanged historical diagrams and add
native previews of the rounded parallel key and Woodruff segment. Five-language
captions distinguish the source diagrams and nominal models. Existing historical
numerical discrepancies listed above remain explicitly unverified.

Keep no/one/two retaining-screw variants explicit. Use independently reviewed
size and length availability, hole spacing and thread-depth data; do not infer
thread engagement from key height or copy the key counterbore into the shaft.
Keep retaining and extraction holes distinct. Preserve table provenance and
user overrides. Show ordinary native Hole/Thread operations after decomposition.
An open-ended keyway may have an overrun cutter, but hole positions follow the
intended key length and datum. All missing/unverified choices remain visible
engineering work, rather than silently fabricated standard defaults.
