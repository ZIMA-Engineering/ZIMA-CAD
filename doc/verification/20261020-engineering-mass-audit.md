# Engineering Tables mass audit — 2026-10-20

All 1,026 native documents under the clean Windows 2026102003 candidate's
`config/lib/03-ENGINEERING-TABLES` passed the read-only mass audit. Every document
has a positive calculated physical mass, a saved `mass` parameter matching it at
the document's decimal precision, and a mass expression referencing `model.mass`.
No model required the audit's diagnostic in-memory regeneration path. No factory
document was saved or rewritten during this check.

## Inputs and method

The candidate was built from immutable Git commit
`5b095f39b33249ab0e380d3b9c165c919386adbc`, with Qt 6.11.0 and OCCT 8.0.0.
The actual clean candidate CLI opened each `.prtz`/`.asmz` in an isolated working
directory and queried `document.relations.get`. Checks compared the persisted
mass with `model.mass` using a rounding tolerance of `0.501 * 10^-decimal_places`,
and checked positive physical mass and the authored mass relation separately.
All 1,026 results are `current_mass`; none are missing, zero, stale or unreadable.

An additional native checker independently inspected the cached BRep solids in
all 1,030 factory library Parts, including the four FORM definitions. It found
146 bodies with trimmed cylindrical faces and reported zero stale volumes and
zero read/calculation errors. Open FORM role surfaces were correctly excluded
from solid-volume integration. This separate volume check guards against merely
comparing two values derived from the same stale property cache.

The unchanged candidate subsequently passed the repository's full standard
archive extraction and native smoke checks, including 1,185 factory document
loads. Final signed-package resource equality, Part/Assembly Save analysis,
GUI acceptance and public publication are recorded separately in the
[release acceptance](../releases/2026102003.md); this mass audit alone does not
establish those gates.

## Evidence and limits

- `build/release-2003-library-mass/results.jsonl` and `summary.json` retain each
  native query, stored/calculated value, density, volume and status.
- `build/release-2003-library-mass.log` records all 1,026 successful checks.
- `build/release-2003-independent-factory-properties.json` and `.log` retain the
  independent 1,030-Part BRep volume audit.
- `build/release-2003-functional-input-preflight.json` checks exact factory hashes
  for the 18 representative library GUI inputs used in subsequent acceptance.

## Original presentation models and title-block mass

The signed 2003 runtime also opened the original demonstration models read-only.
`build/release-2003-demo-mass/results.json` records their unchanged SHA-256 values,
physical values, stored rounded parameters and actual `mass = model.mass` source.
The plate is 0.6532332366366904 kg, screw 0.05299400693885462 kg and nut
0.016651225350188575 kg. Two occurrences of each sum to the Assembly's
1.445756937851467 kg; its stored three-decimal value is 1.446 kg.

The exported original Assembly and screw Drawing PDFs were rendered with Poppler
and visually inspected at 2,200/1,600 pixels. The Assembly BOM shows 0.653,
0.053 and 0.016651 kg with quantity two each, and the title block shows 1.446 kg.
The screw BOM and title block both show 0.053 kg. Render evidence is under
`build/demo-pdf-mass-review`; the unchanged PDFs are in the rejected 2003
verification installation's `Projects/acceptance-2003`. These checks establish
the displayed mass fields, not overall Drawing layout acceptance or publication.
Text extraction alone omits the curve-based title-block lettering and is not
used as evidence for those displayed values. Fresh 2004 packaged acceptance must
repeat the actual export with its own immutable runtime.

## Remaining limits

This verifies calculated and published properties, not the measured mass of a
manufactured item. In particular, the user-approved symbolic insert lock nut is
one steel Part, including its colored insert. Existing source-dimension and
manufacturing approximations remain documented by family. Custom libraries and
the user's private remodeled pressed M10 nut were not bulk-converted or audited
as factory files. Native Linux execution remains unverified on this Windows host.
No user-visible application text changed in this audit; localization was reviewed
and requires no new translation keys.

## Signed Windows 2026102004 confirmation

The fresh signed Unicode installation repeats the actual native CLI audit over
all 1,026 Engineering Tables documents: all are `current_mass`, positive, bound
to mass Relations and within their saved decimal precision. None used diagnostic
in-memory regeneration. Evidence: `build/release-2004-library-mass/results.jsonl`
and `summary.json`. The independent reader loads 1,030 Parts; its exact adaptive
volume integration covers the 146 trimmed-cylinder bodies, with zero stale
values/errors (`build/release-2004-independent-factory-properties.json`).
This subset must not be presented as independent integration of every body.

The new Assembly/screw PDF exports render pixel-identically to the previously
visually inspected proofs, retaining 1.446/0.053 kg title-block masses. Four
original model hashes remain unchanged. Pixel/hash evidence is under
`build/demo-pdf-mass-review-2004`. Published asset identity and platform limits
are in [Windows 2026102004](../releases/2026102004.md).
