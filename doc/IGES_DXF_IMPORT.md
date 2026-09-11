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
Aktuální vypočtená geometrie zdrojového Partu se při obnovení zobrazení převezme
bez OCCT. Vazby a vlastní operace sestavy se počítají až explicitním
**Regenerate**; přepnutí záložky je nespouští.

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


## Porovnání dodaného STEP a IGES (2026-09-11)

Zkoumané soubory v `Projects/import`: `ze0026-0000-0000.stp`
(9 092 072 B) a `ze0026-0000-0000.igs` (37 994 290 B).
Jde o rozbor dodaného exportu, nikoli obecnou vlastnost všech IGES souborů.

Čtení zdrojových IGES entit a nezávislý rozbor directory záznamů potvrdily:

- 501 solidů (typ 186), 13 506 zdrojových ploch (510) a 738 shellů (514).
- 511 pojmenovaných skupin (402): 10 obsahuje další skupiny, 501 je koncových.
- Soubor neobsahuje subfigure definition/instance entity 308/408.
- Čtyři skupiny šroubu `ZE0026-0101-9001` (včetně variant `_1`, `_2`, `_3`)
  mají každá jeden solid, 28 zdrojových ploch a dva shelly. Obsahují také
  pomocné křivky a seznamy hran/vrcholů. Počet shellů sám o sobě neurčuje
  počet samostatných plošných těles: shell je také součástí solidu.

Současný `import_iges_part` vždy vytvoří jeden Part s jedním importním
kontejnerem za celý soubor. Pojmenované skupiny nepřevádí na hierarchii
sestavy. STEP oproti tomu v tomto modelu rozpoznává 85 unikátních Partů
s opakovanými výskyty. Shodný model tedy není v obou výměnných formátech
organizován stejně; shodný název či odstranění číselné přípony nestačí
jako důkaz totožnosti zdrojového dílu.

Doporučený další krok pro IGES:

1. Převzít významové pravidlo STEP: jeden zdrojový díl/skupina obsahuje
   solid i jeho pomocné plochy, například závit šroubu.
2. Skutečné vztahy nadřazených skupin převést na sestavy. Nevyrábět
   podsestavu pouze proto, že díl obsahuje více geometrických objektů.
3. Ve skupině vybrat vlastněné solidy a zbylé samostatné plochy/dráty.
   Nevkládat znovu jednotlivé plochy a pomocné entity již obsažené v solidu.
4. Použít společnou nativní persistenci, sdílení geometrie a viewer reference.
   Zachovat zdrojové IGES identity; opakování sdílet jen při prokázaném
   společném zdroji, nikoli odhadem podle názvu.

Toto rozdělování IGES zatím není implementováno. Již společná cesta zmrazeného
importu používá opravené archivní vazby topologie popsané v
[sdílení geometrie](ASSEMBLY_GEOMETRY_SHARING.md).


### Skutečný převod přes současný importér

Lokální diagnostika volala produkční `OcctKernel::import_iges` s odchylkou
sítě 0,1 mm; zdrojový soubor ani uživatelské dokumenty neměnila.

- Import před opravou vyhledávání při archivaci referencí: 887,58 s
  (přibližně 14,8 min).
- Výsledek: 501 solidů, 738 shellů, 27 012 ploch, 132 690 hran a
  168 358 vrcholů (unikátní OCCT objekty podle druhu).
- Zobrazovací síť: 2 271 821 trojúhelníků; B-Rep: 73 347 359 B.
- Nabízené původní reference: 209 767 identit ploch, hran a vrcholů dohromady.
- Po importu proces vykázal 1 416,36 MiB working set a 1 521,44 MiB private;
  dosavadní maximum working set bylo 1 825,23 MiB. Nejde o paměť celé GUI sestavy.

Oddělená diagnostika samotného OCCT přečetla soubor za 0,81 s,
`TransferRoots` skončil v čase 37,35 s a ověření platnosti v čase 46,21 s.
Převod vrátil šest kořenů a platnou geometrii. Dlouhá celková doba tedy
není vysvětlena pouhým čtením a převodem IGES. Naše následné zachycení
referencí, síťování, měření a příprava viewer dat potřebují samostatné
profilování. V kódu se geometrický locator opakovaně počítá i pro děti
nadřazených zdrojových skupin; jeho přesný časový podíl zatím není změřen.
Během plného importu běžely krátké samostatné diagnostiky na stejném počítači,
takže tyto časy nejsou izolovaným rychlostním benchmarkem STEP proti IGES.

U stejného šroubu se po převodu přímo ověřilo: solid obsahuje 26 ploch,
shelly dohromady 28, z toho právě dvě nejsou součástí solidu. Všech 28
samostatných zdrojových face entit už patří do těchto shellů. Celá převedená
skupina přesto obsahuje 56 ploch. Nelze proto prostě zobrazit všechny
geometrické členy skupiny jako obsah dílu.


Navazující audit původních entit typu 120/122/128 potvrdil zdroj těchto
28 přebytečných ploch: jsou to podpůrné povrchy. Všech 28 je součástí
převedené skupiny, ale žádný není totožný s plochou výsledných shellů.
Správný import šroubu má ponechat solid s 26 plochami a dvě samostatné
pomocné plochy; podpůrné povrchy nemají vytvořit dalších 28 zobrazovaných
ploch. Oprava vyžaduje respektovat vlastnictví a závislosti IGES entit,
ne obecné mazání podobných nebo souhlasných ploch podle geometrického hashe.


Při rozboru bylo v novém `persist_imported_topology` nalezeno kvadratické
prohledávání: pro každou vlastněnou plochu/hranu/vrchol se celá tabulka
identit znovu procházela pomocí `find_if`. Nahrazuje je jednorázový index
sémantických klíčů pro každý druh topologie. Nemění zdrojové identity,
archivní formát ani pravidla IGES seskupování. Výše uvedených 887,58 s
pochází z běhu před touto opravou.


Kontrola zmrazeného B-Rep ve stejném běhu obnovila všech 209 767 nabízených
sémantických identit beze změny (`references_equal=1`). Celý diagnostický běh
včetně opětovného výpočtu viewer dat trval 2 106,35 s; maximum working set
bylo 3 769,64 MiB. To dokládá zachování již zachycených referencí, nikoli
správnost seskupování IGES ani úplnost referencí ke každé přebytečné ploše.


Opakovaný import po zavedení indexu archivních identit trval 1 061,30 s a
vrátil stejný počet solidů, ploch, hran, vrcholů, trojúhelníků i nabízených
referencí; B-Rep měl opět 73 347 359 B. Working set po importu byl
1 423,56 MiB, private 1 541,23 MiB. Opakovaná následná obnova byla diagnosticky
ukončena; úplný roundtrip je doložen prvním během výše a malými regresními testy.
Vzhledem k souběžným testům a diagnostikám nejde o izolovaný rychlostní
benchmark; toto opakování nepotvrzuje zrychlení celého importu. Index odstranil
konkrétní kvadratický postup, dominantní náklady celého převodu je však stále
potřeba změřit po jednotlivých fázích.
