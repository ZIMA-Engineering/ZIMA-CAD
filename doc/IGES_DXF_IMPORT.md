# Import IGES a DXF

Implementováno 2026-09-09. Příkaz **Soubor → Importovat** pracuje s právě
editovaným dokumentem, také při aktivaci Partu nebo podsestavy uvnitř sestavy.
BREP import není součástí této změny. STEP si zachovává vlastní import
produktové hierarchie, popsaný v [STEP import/export](STEP_IMPORT_EXPORT.md).

## DXF: Part → těleso → Sketch → importní blok

- Bez aktivního tělesa se vytvoří nové těleso a v něm kontejner Sketch.
- S aktivním tělesem se nový Sketch vloží přímo do tohoto tělesa, na jeho
  aktuální pozici v historii.
- Při práci přímo v aktivní skice se importní blok přidá do této skici.
- V sestavě vznikne nový Part se strukturou těleso → Sketch → DXF blok.
  Je vložen jako komponenta právě editované sestavy. Aktivovaný Part uvnitř
  sestavy používá pravidla Partu; další Part se v takovém případě nezakládá.

Nový Sketch používá lokální rovinu XY tělesa. Umístění lze později upravit
běžnými Properties skici/tělesa. Umístění komponenty vlastní pouze její
bezprostřední sestava. Sdílený placement kontrakt se nemění.

„Mrtvola“ je jeden importní blok s geometrií ZIMA Sketcheru, bez původní CAD
historie a bez živého odkazu na DXF. Blok lze následně transformovat stávajícími
nástroji Sketcheru. Původní soubor není potřebný pro opětovné otevření Partu.
DXF nevytváří objemové těleso: vzniká geometrie skici použitelná pro modelování.

### Vlastní čtečka a její rozsah

Čtečka je implementována v C++, bez placeného převodníku a bez knihovny pro
parsování DXF. Čte textové DXF z modelového prostoru:

- `LINE`, `CIRCLE`, `ARC`;
- `LWPOLYLINE` a 2D `POLYLINE` / `VERTEX` / `SEQEND`;
- otevřené i uzavřené polylines, včetně kladných a záporných oblouků `bulge`;
- jednotky `$INSUNITS`, převedené do interních milimetrů. Bez jednotek nebo
  při hodnotě 0 používá UI předpoklad 1 jednotka = 1 mm.

Podporována je rovinná geometrie XY s nulovou elevací a tloušťkou a normálou
+Z. Jinak import odmítne geometricky nejednoznačná data, místo tichého
zploštění. Neplatná čísla, neúplné páry, neplatné polylines a překročení
limitu 100 000 zdrojových záznamů odmítne bez změny cílové skici.

`INSERT`/blokové reference, `ELLIPSE`, `SPLINE`, texty, kóty, šrafy, 3D sítě
ani binární DXF zatím nejsou podporovány. Nepodporované entity se vynechají
s upozorněním; pokud nezbude podporovaná geometrie, nový Part/Sketch nevznikne.
Paper space se neimportuje. Čáry na vrstvě `CONSTRUCTION` jsou konstrukční.

## IGES

Přípony `.igs` a `.iges` používají čtečku OCCT. V Partu vznikne nové těleso
s kontejnerem importované geometrie. Jeden soubor je jeden importní kontejner;
plošná/drátová geometrie zůstává plošná/drátová, nevyrábí se z ní náhradní objem.
IGES se v této verzi nerozkládá na produktovou hierarchii jako STEP.

V sestavě vznikne jeden zdrojový Part s tímto tělesem a vloží se jako její
komponenta. Existující obsah cílového Partu nebo sestavy zůstává zachován.
IGES nelze importovat dovnitř aktivní skici. Export IGES není implementován.

Import explicitně vypočte zmrazený B-Rep, zobrazovací síť a mapu referencí.
Identita je odvozena od directory entry původního IGES a sémantické role;
u rozděleného zdrojového objektu se připojí rozlišující geometrický locator.
Sdílené hrany/vrcholy používají nejkonkrétnější dostupnou zdrojovou entitu,
při shodě nejnižší directory pointer. OCCT pořadí průchodu není identitou.
Nejednoznačné shody locatorů nejsou nabízeny jako trvalé reference.

Používá se existující mechanismus zmrazeného importu STEP (`ImportedStep`,
`imported_step`, `StepRequest` jsou dosavadní interní názvy společného úložiště).
Zdrojový formát rozlišuje cesta a prefix identity `iges:`. Nevzniká druhá
implementace editace, placementu ani persistence. Otevření a reference čerpají
z uložených ZIMA dat; Regenerate z uloženého B-Rep, bez původního IGES.

## Zdrojové Party v sestavě

Nový `.prtz` vznikne vedle cílové sestavy, u neuložené sestavy v pracovním
adresáři. Při kolizi názvu dostane číselnou příponu. Import nezmění zobrazenou
vrcholovou sestavu. Skicové Party se zobrazují i ve vnořených sestavách.
Změna jejich zdroje se do rodiče převezme až explicitním **Regenerate**.

## Ověření

`zima_cpp_import_model_contract_tests` kontroluje:

- nové/aktivní těleso, aktivní Sketch, nezávislé bloky a jejich uložení;
- skutečné vytažení DXF obdélníku 20 × 10 mm o 5 mm (objem 1 000 mm³);
- polylines, záporný bulge, palce → mm a atomické odmítnutí neplatných dat;
- vložení skicového Partu do sestavy, vnořené vlastnictví a explicitní regeneraci;
- skutečný externí DXF (22 entit) a IGES krychli 10 mm (objem 1 000 mm³);
- IGES kvádr, 26 topologických identit, uložení a regeneraci bez zdrojového souboru;
- IGES drát a odmítnutí poškozeného souboru.

Malé externí vzorky, přesné zdrojové revize, kontrolní součty a licence jsou
uloženy v [cpp/tests/fixtures/import](../cpp/tests/fixtures/import/README.md).

Ověření Windows Release: celá sada 27/27 testů; po závěrečné úpravě
nezávislosti bloků a IGES zvýraznění znovu prošly všechny tři dotčené sady
(import model, interchange, viewer). Aplikace byla znovu sestavena.
