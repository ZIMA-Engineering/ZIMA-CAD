# Circumferential grooves in Parts

## Definition and lifecycle

The Groove command belongs to Mechanics in an active editable Part Body. It
creates one subtractive history feature. Its cylinder reference determines the
axis, support diameter and internal/external material side from native analytic
surface geometry. Ordinary placement references determine its axial position;
cylinder/plane selection order does not change the definition. Circumferential
geometry has no angular placement parameter. The command consumes the shared
placement section and solver without changing that solver.

The section is an exact rectangle or a clipped circular segment. The ordinary
360-degree revolution and subtraction paths produce the material. For circular
sections, opening width is `2 sqrt(h (2 r - h))`; it is derived, not an independent
dimension. This preserves the selected depth even when the arc exceeds a
semicircle. Display polylines are used only for preview, never as cutting curves.

Creation and editing use `PrimitivePropertiesDialog` and the same commit
operation. The existing rollback boundary supplies source geometry. Preview and
dimension inspection read the definition and native reference packet without
calculating material. An unchanged commit returns before calculation or an Undo
transaction. Regeneration refreshes the section only from current source inputs.
Reference loss retains the last valid section/frame and reports an unresolved
feature. The shared modeling-confirmation mechanism owns failed OK handling.

## Catalogue scope

`resources/data/mech/grooves.json` contains supplier dimensional facts, not ring
models or a claim of certification to the latest standard editions. The current
selection includes rectangular grooves from SEEGER DIN 471/472 data and circular
wire-ring grooves from SEEGER DIN 9925/9926 data. No unverified ISO equivalence is
assigned. Historical CSN tables remain unchanged.

The input is nominal shaft/bore diameter. The groove diameter is distinct from
the free ring diameter. Catalogue sizes match the native support diameter within
`1e-7 mm`; unsupported diameters remain unresolved rather than silently rounding
to another size. Custom sections remain available independently of a catalogue.

Rectangular width uses the supplier minimum and a selectable H13 interval. This
is a dimensional selection, not a load-capacity assessment. SEEGER's design notes
allow widening towards an unloaded side for some applications and require closer
matching to ring thickness for alternating loads; there is no universal safe
maximum independent of the application. A custom definition expresses dimensions
outside the offered tolerance interval explicitly.

Sources reviewed on 2026-11-28:

- [SEEGER DIN 471 dimensional data](https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/datenblatt_seeger-ringe_471.pdf), first four paired tables, 200 sizes from 3 to 460 mm.
- [SEEGER DIN 472 dimensional data](https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/datenblatt_seeger-ringe_472.pdf), first four paired tables, 200 sizes from 8 to 450 mm.
- [SEEGER DIN 9925 shaft wire rings](https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/datenblatt_seeger-ringe_Sprengringe_9925_9926_Wellen.pdf), 40 sizes from 4 to 125 mm.
- [SEEGER DIN 9926 bore wire rings](https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/datenblatt_seeger-ringe_Sprengringe_9925_9926_Bohrungen.pdf), 37 sizes from 7 to 125 mm.
- [SEEGER design notes, section 8.1](https://www.seeger-orbis.com/fileadmin/assets/zertifikate/SEEGER-KATALOGUE-light-2022_11.pdf).
- [Rollco DIN 471 groove width H13 designation](https://www.rollco.no/storage/481CCA4337A9AF14595A0E6BD7E6EF92C45EE683200F507CAB07D2A58B7B8044/d02afffd99b34ac1aecf1a0b206d2231/pdf/media/54a437ce9e61417bae17eb38979e51a5/Standard_tolerances_of_groves_for_circlips_P0000-4ENTI.pdf).
- [Schaeffler technical handbook, ISO 286 tolerance grades](https://www.schaeffler.com/remotemedien/media/_shared_media/08_media_library/01_publications/schaeffler_2/catalogue_1/downloads_6/stt_de_de.pdf).

The unreviewed final large-diameter supplier tables are deliberately absent;
their text extraction contains ambiguous values. Third-party PDFs and artwork
are not bundled.

`tools/prepare-groove-catalog.py --source-dir <directory> --check` reproduces
the complete numerical catalogue from the four reviewed local PDFs with
`pypdf`. It checks their SHA-256 hashes before extraction and independently
checks the supplier's depth against the nominal and groove diameters. A changed
supplier PDF needs a fresh review; the importer does not silently accept it.

## Persistence and verification status

The approved extension adds the `mech_groove` feature tag and a strict version-1
typed definition. The existing Part envelope and unrelated feature contracts are
unchanged. Parameters, support identity, catalogue selection and the exact
section including its last valid frame live in the native Part. The catalogue is
bundled into the executable; the saved geometry does not require an external
cache. Old readers reject the unknown feature tag.

Native Windows tests currently pass 20 combinations of material side, profile,
source orientation, reference-entry order and Body placement. They independently check analytic
removed volume, BRep validity and material probes at three azimuths; repeated
depth edits; source diameter/translation edits; reference identity; loss and
repair after unresolved serialization; Undo/Redo; cached save/reopen and cold
regeneration. Preview and unchanged commit assert zero material calculations.
Catalogue width endpoints and midpoint are exercised for all four families.
Whole-Origin placement and two successive independent cuts pass in all combinations. Failed creation and
editing verify the first-failure acknowledgement, invalidation after parameter
and source changes, cancellation, and the unchanged second OK committing an
unresolved definition without another material calculation. Repair and cold
regeneration of those definitions also pass. The complete native matrix took
33.03 seconds in the local Windows Release build; this is a test duration, not
an interactive performance benchmark.

The Windows GUI gate passed actual mouse cylinder/plane entry in both orders
for both sides and profiles, whole-Origin replacement, independent inspection,
resize, Cancel, unchanged OK, edited depth, middle-button confirmation,
Undo/Redo, native save/reopen, ordinary single-click selection and double-click
dimensions. Its language/theme matrix checks all five translations and
Light/Dark/Light without changing the definition or calculating material.
Rendered evidence exposed duplicate automatic numbering, subsequently removed
by reusing the ordinary naming path.

The final GUI rerun passed in 30.52 seconds, including failed creation and editing:
the first failed OK retains the private draft, the unchanged second OK saves an
unresolved definition without another calculation, and one Undo restores the
previous history. The catalogue width endpoints and midpoint update the actual
draft without calculation; Cancel retains the original definition. Ten native
button/dialog captures document the cylinder-plus-plane workflow in all five
languages. Website validation checks their unchanged bytes and language routes.

All six Part, Skeleton and Assembly start templates were regenerated and reopened
with the current native writer; their bytes are unchanged because the extension
adds a new feature kind without changing existing definitions. Actual New Document
GUI tests passed for inch and millimetre templates, including the first active
editable Body, normal commands, Assembly insertion and native reopen. Existing factory
models do not contain the new kind. Exact committed factory loading and release
packaging remain release gates. Linux runtime verification requires a Linux host;
the implementation is shared C++.
