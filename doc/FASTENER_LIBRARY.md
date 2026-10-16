# Native fastener library

The first ISO 4017 full-thread hexagon-head screws are M3x20, M4x20, M5x20
and M6x25. They are ordinary editable Parts under
`config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD/03-FULL-AB/01-MODELS`.
They retain the authored Body, sketches and feature history. Copy a model into
the user's project, edit its shaft length and rename the copy through the normal
workflow. The library does not enumerate every available length or rename a
document when a dimension changes.

## Names and parameters

Use `HHS-ISO4017-M3x20.prtz` for this full-thread family and
`HHB-ISO4014-M12x45.prtz` for the discussed partial-thread family. The lowercase
ASCII `x` belongs in filenames. A model filename must be unique, ignoring case,
throughout the engineering library. These conventions do not rename the original
user-authored `HHN-ISO4014-M3X10.prtz`.

The agreed simple BOM uses a localized descriptive `name`, `standard=ISO 4017`,
`material=8.8`, `quantity=-` and dimension-derived `stock`, for example `M3x20`.
The five names are provided in CS, EN, DE, FR and RU. Existing parameter labels
and the ordinary Parameters window remain authoritative. The Relations are:

```text
stock = "M" & d1 & "x" & d12
mass = model.mass
```

Here `d1` is the nominal shaft diameter and `d12` its extrusion length in this
authored template. Copies preserve those bindings. This is not filename parsing.
User copies retain the library's original filename until the user renames them.

The material library contains `Steel-8.8.matz`. Its representative steel density
is 7850 kg/m³; 8.8 identifies the agreed property class, not a unique alloy grade.
The record supplies no invented alloy composition or FEA strength properties.
Zinc is a coating and is not substituted for the steel's physical material.

## Geometry and sources

| Model | Pitch P (mm) | Across flats s (mm) | Head height k (mm) |
| --- | ---: | ---: | ---: |
| M3x20 | 0.5 | 5.5 | 2 |
| M4x20 | 0.7 | 7 | 2.8 |
| M5x20 | 0.8 | 8 | 3.5 |
| M6x25 | 1 | 10 | 4 |

The head-cut sketch uses a 30° angle. The cosmetic thread uses its metric catalog
designation and an **Up to** reference to the plane below the head, with runout
disabled. The inactive former numerical length field is not the effective thread
limit. Shaft-length edits retain the actual cylinder, start, end and chamfer
references. Threads are represented by the established native thread feature,
not by a newly modeled helical solid.

Catalog checks: [M3](https://www.fabory.com/en_NL/hexagon-head-screw-iso-4017-din-933-steel-zinc-plated-8-8-m3x20/p/01210030020),
[M4](https://www.fabory.com/nl_BE/zeskanttapbout-iso-4017-din-933-staal-elektrolytisch-verzinkt-8-8-m4x20/p/01210040020),
[M5](https://www.fabory.com/fr_BE/vis-%C3%A0-t%C3%AAte-hexagonale-filetage-total-iso-4017-din-933-acier-electro-zingu%C3%A9-8-8-m5x20/p/01210050020),
[M6](https://www.fabory.com/nl/zeskanttapbout-iso-4017-din-933-staal-elektrolytisch-verzinkt-8-8-m6x25/p/01210060025).
[ISO 4017](https://www.iso.org/standard/72585.html) describes this full-thread
family; product grades A/B differ from property class 8.8.

The technical illustration uses an actual native Drawing and its independent
native source Part under `0000-index/`. Its enlarged lower details explain the
table's remaining symbols; they do not claim additional manufactured geometry
in the simplified model. Native isometric thumbnails accompany all four models.
No external geometry/cache sidecar is required to reopen their native documents.

## Verification and limits

Run `tools/verify-fastener-library.py --cli <native-cli>` to edit independent
copies. It checks all four pitches/designations, localized BOM fields, twelve
successive shaft-length changes, stock Relations, Undo/Redo, save/reopen, exact
thread-reference retention and unchanged read-only head supports. Factory source
hashes must remain unchanged. The test retains its evidence under `build/`.

The angular-solver matrix and actual GUI checks are recorded in
[the finite verification report](verification/20261016-fasteners-and-reference-layout.md).
The library is a modeling starting point, not a certified manufacturing model or
a complete dimensional audit of every ISO size. An experimental M8 conversion
encountered a missing original Fillet/Chamfer edge and is not included in this
verified library. Linux execution remains unverified.
