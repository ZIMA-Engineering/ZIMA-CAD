# Výkresy přes společné příkazy

Výkresové příkazy zpřístupňují listy, šablony, tvorbu a vlastnosti pohledů,
uložené reference a výslovnou regeneraci. Katalog má 138 příkazů. Měřené kóty,
anotace, editace BOM, Show/Erase, zdrojové styly šraf a výkresové exporty
zatím nejsou kompletně pokryté.

| Příkaz | Argumenty | Výsledek |
| --- | --- | --- |
| `drawing.sheet.list` | `[document]` | Uložené listy v pořadí dokumentu |
| `drawing.sheet.get` | `sheet`, `[document]` | Parametry a počty vložených objektů |
| `drawing.sheet.create` | `[name]`, parametry níže, `[document]` | Nový list s vlastním stabilním ID |
| `drawing.sheet.set` | `sheet`, parametry níže, `[document]` | Částečná změna parametrů |
| `drawing.sheet.delete` | `sheet`, `[document]` | Odstranění listu s jeho pohledy a kótami |
| `drawing.frame.load` | `sheet`, `path`, `[document]` | Vložená geometrie souboru `.frmz` |
| `drawing.frame.clear` | `sheet`, `[document]` | Odstranění vloženého formátu |
| `drawing.title_block.load` | `sheet`, `path`, `[document]` | Vložená geometrie souboru `.tblz` |
| `drawing.title_block.clear` | `sheet`, `[document]` | Odstranění vloženého razítka |

`document` označuje otevřený výkres, výchozí je aktivní dokument. Změny vyžadují
aktivní výkres a uzavřenou předchozí editaci. `sheet` je ID z výsledku vytvoření
nebo seznamu listů, nikoli jméno či pořadové číslo. Dotazy vracejí uložená data,
nekopírují projekční geometrii a nepotřebují zdrojové soubory.

Parametry vytvoření a úpravy listu:

- `name`: neprázdný jednořádkový název, nejvýše 256 bajtů UTF-8.
- `format`: `A4`, `A3`, `A2`, `A1`, `A0`; rozměry v mm vrací dotaz.
- `projection`: `first_angle` nebo `third_angle`.
- `scale`: desetinné měřítko 0,001 až 1000; `0.5` znamená 1:2 a `2` znamená 2:1.
- `thick_line_mm`, `thin_line_mm`, `red_line_mm`: tloušťky 0,05 až 2 mm.
- `locale`: neprázdný jednořádkový kód jazyka, nejvýše 32 bajtů.

Vytvoření používá A4, první kvadrant, měřítko 1, tloušťky 0,5/0,25/0,7 mm
a jazyk `cs`. Neuvedené položky při editaci zůstávají beze změny. Identická
editace a odstranění již prázdné šablony nevytvoří krok historie.

```json
{"command":"drawing.sheet.create","arguments":{"name":"Detail","format":"A3","scale":2}}
{"command":"drawing.sheet.set","arguments":{"sheet":"ID-LISTU","scale":0.5,"locale":"cs"}}
{"command":"drawing.frame.load","arguments":{"sheet":"ID-LISTU","path":"config/formats/ZE-A3.frmz"}}
```

Měřítko listu se přenese pouze do pohledů se zapnutým `use_sheet_scale`.
U řezů se při této výslovné editaci obnoví projekce a šrafování z dostupné
vypočtené sítě; rozteč šraf zůstává v milimetrech papíru. Otevřený zdroj má
přednost před souborem a kontroluje se jeho identita. Není-li nutný zdroj
dostupný, neprovede se žádná část změny. Samotný zdroj ani jeho sestavy se
neregenerují, OCCT se pro změnu listu nepoužívá. Změna metody promítání mění
nastavení listu; existující projekční kamery aktualizuje výslovná regenerace
pohledů, stejně jako dosavadní GUI.

Změna formátu odstraní všechny části dosavadního formátu a razítka včetně
kružnic, obrázků a opakovaných oblastí. Pohledy a jejich kóty zachová.
Odstranění razítka zachová lokální parametry a uložené řádky BOM, které
nejsou jeho kresbou. Formát `.frmz` musí odpovídat formátu listu. Šablony se
vloží do `.drwz`; po uložení není původní soubor šablony nutný. Chybný import
ponechá původní list beze změny. Poslední list nelze odstranit; odmítne se také
smazání listu, na jehož pohled závisí pohled na jiném listu.

## Historie a GUI

Výkresová `DrawingState` má společné Undo/Redo dostupné v CLI i hlavním GUI.
Každý potvrzený krok má vlastní revizi; nová větev zruší Redo. Historie
uchovává nativní výkresové snímky bez opětovné projekce, načítání souborů nebo
OCCT. Přidělená čísla kót se neztrácejí při Undo a později se nepoužijí znovu.
Dokončení staršího asynchronního uložení nesmí označit mezitím změněný výkres
za uložený. Formát `.drwz` se nemění a historie není povinný vedlejší soubor.

GUI vytvoření a odstranění listu, dialog vlastností, spodní lišta, import i
odstranění formátu/razítka používají stejné modelové operace. Dialog při
neplatných hodnotách zůstane otevřený; Cancel nezapisuje změny. Otevřená
editace blokuje souběžné příkazové změny a hlavní GUI Undo/Redo. Zvětšovací
měřítko se ve spodní liště správně zobrazí například jako 2:1.

## Ověření

Cílená Windows Release sada prošla **7/7** (21,42 s),
`build/drawing-sheet-integration-tests.log`. Pokrývá skutečný CLI proces,
GUI dialog a historii, nativní uložení/znovunačtení, chyby importu a změny
formátu. Nezávislá geometrická kontrola ověřila rozteč šrafování 1,2 mm na
papíře při měřítku 2, zachování vlastního měřítka druhého pohledu a nedotčenou
revizi zdrojového dílu. Další testy ověřují zmizelý zdroj, nesprávnou identitu
souboru, přidělování čísel kót a uložení během změn historie.

Celá Windows Release sada této etapy prošla **79/79** (407,77 s),
`build/drawing-sheet-full-tests.log`, včetně obou výsledných programů GUI a CLI.

## Pohledy, reference a regenerace

| Příkaz | Argumenty | Význam |
| --- | --- | --- |
| `drawing.view.list` | `[sheet]`, `[limit]`, `[document]` | Uložené pohledy a jejich dostupná metadata |
| `drawing.view.get` | `view`, `[document]` | Jeden uložený pohled včetně skutečné kamery |
| `drawing.view.references` | `view`, `[kind]`, `[limit]`, `[document]` | Původní měřicí křivky a body uložené s projekcí |
| `drawing.view.delete` | `view`, `[document]` | Odstranění pohledu a jeho projekčních potomků |
| `regenerate` | `[document]` | Výslovná regenerace všech pohledů aktivního výkresu |

`view` a `sheet` jsou stabilní ID z dotazů. `limit` je 1 až 10000, výchozí
2000; `total` zahrnuje i položky nad limitem. Dotazy neotvírají soubory,
neprojektují geometrii a nemění potvrzený výběr ani historii. Vrací vlastnosti
pohledu, zdrojové ID/cestu, vazbu na rodičovský pohled, skutečnou kameru,
orientaci, polohu v mm papíru, měřítko, styly čar, řez, nastavení popisků a
vodítek, zámky a počty projekčních a měřicích prvků.

`drawing.view.references` přijímá `kind` s hodnotou `all`, `curve` nebo `point`.
Každý řádek obsahuje původní `owner`, sémantický `key` a přesnou `instance_path`.
Výsledek uvádí také kořenový `source_document` pohledu. Nejde o pořadová čísla
OCCT ani o novou identitu z projekčních čar. Křivka poskytuje příznak přímky,
počet uložených vzorků, první/poslední bod a případná kruhová data. Bod poskytuje
souřadnici. Souřadnice jsou v mm souřadného systému zdrojového modelu
(`source_model_mm`); vektory směru jsou bezrozměrné. Celá vzorkovaná křivka se
neposílá při běžném seznamovém dotazu.

Regenerate načte dostupné vypočtené zdroje, anotace, aktuální definice vybraných
řezů a jejich tras a obnoví projekce, měřené kóty a uložený kusovník. Projekční
potomci se počítají po rodiči bez ohledu na pořadí v souboru. Chybějící rodič,
nesouhlas zdrojové identity, cyklus, chybějící vybraný řez nebo nedostupný zdroj
zamítne celou změnu. Hloubka projekčního řetězce je omezena na 256. Prázdný
výkres bez pohledů nevytvoří krok historie.

Otevřený zdrojový dokument má přednost. Uzavřený Part používá stejný nativní
snímek jako otevřený Part, včetně samostatných skic, konstrukční geometrie a
původních datumových referencí. Pomocné načtení zůstává mimo uživatelské taby.
Regenerace výkresu nepočítá tělesa a neprovádí regeneraci zdrojové sestavy ani
Partu; pracuje s jejich posledním vypočteným stavem. Relativní cesty se vyhodnotí
od souboru výkresu. Cesty uložené v kusovníku používají UTF-8 i na Windows.

Mazání odstraní celý řetězec pohledů navázaných přes `parent_view` i jejich
měřené kóty. Nezávislému řezovému pohledu pouze zruší odkaz na smazaný rodičovský
pohled trasy; jeho řez a zdroj zůstanou zachovány. Jeden krok Undo obnoví
pohledy, kóty a jejich původní ID. Původní akce GUI Regenerovat a Odstranit
používají stejné operace jako konzole.

Regrese této etapy prošly **12/12** (36,66 s),
`build/drawing-view-final-tests.log`. Obsahují změnu délky kvádru z 20 na 40 mm
a odpovídající měřenou kótu, projekční strom uložený v opačném pořadí,
Undo/Redo, zmizelé a zaměněné zdroje, anotace, řezy, kusovník, skutečné CLI,
GUI mazání i uložení do cesty s českými znaky. GUI bylo také vizuálně ověřeno
na `Projects/test/command-drawing-views.png`.

Závěrečná kontrola zdrojů a vstupů prošla **3/3** (17,21 s),
`build/drawing-view-source-tests.log`: navíc pouze skica bez tělesa, přípona
`.PRTZ`, český název a čitelně rozmístěné pohledy v GUI. Finální sestavení
odpovídá `build/drawing-view-source-build.log`.

## Vytvoření a vlastnosti pohledu

`drawing.view.create` vyžaduje `sheet` a buď `source` (ID otevřeného Partu
nebo Assembly), nebo `parent_view`. `drawing.view.set` vyžaduje `view` a mění
jen zadané vlastnosti. Oba příkazy přijímají volitelný `document` a používají
stejnou modelovou operaci jako OK v dosavadním dialogu vlastností pohledu.

- `name`: neprázdný jednořádkový název do 256 bajtů UTF-8.
- `orientation`: `front`, `back`, `left`, `right`, `top`, `bottom`, `isometric`.
  Alternativně `camera` s vektory `horizontal`, `vertical`, `depth`, každý
  jako pole tří čísel. Vektory musí tvořit jednotkovou kolmou bázi ve stejné
  konvenci jako GUI (`horizontal × vertical = -depth`). Obě možnosti současně
  se odmítnou. Nový základní pohled má přední orientaci.
- `x_mm`, `y_mm`: -10000 až 10000 v souřadnicích papíru: počátek vpravo dole,
  X roste doleva, Y nahoru. Nový základní pohled začíná na 100, 100 mm.
- `scale`: 0,001 až 1000, automaticky zapne vlastní měřítko.
  `use_sheet_scale:true` převezme aktuální měřítko listu. Současné zadání
  konkrétního `scale` a `use_sheet_scale:true` je rozporné a odmítne se.
- `display_style`: `visible_edges`, `hidden_edges`, `shaded_with_edges`, `shaded`.
  `hidden_edge_style`: `dashed`, `gray`. `tangent_edge_style`: `visible`, `thin`, `hidden`.
- `show_caption`, `show_section_label`, `show_dimension_guides`: booleany.
  `guide_offset_mm`: 0 až 1000; `guide_spacing_mm`: 0,1 až 1000.
- `value_locks`: celé pole zámků `x`, `y`, `scale`; prázdné pole je odstraní.
  Stejně jako v dialogu zámek nebrání výslovné ruční změně dané hodnoty.
- `section`: stabilní ID existujícího řezu zdroje z `model.tree`, prázdný řetězec řez zruší.
  `section_markers`: celé pole ID zobrazovaných řezových tras; prázdné pole je skryje.
  `hidden_hatch_components`: celé pole přesných klíčů komponent, které se v tomto
  pohledu nešrafují. Viditelnost patří pohledu, styl šrafování patří zdrojovému
  řezu. Příkazy zatím nemění zdrojové parametry šraf; GUI zachovává jejich
  dosavadní samostatnou zdrojovou transakci.

Pro nový projekční pohled se místo `source` zadá `parent_view`,
`projection_direction` a `distance_mm` (0,001 až 10000 mm podél jednotkového
paprsku). Směry jsou `right`, `top_right`, `top`, `top_left`, `left`,
`bottom_left`, `bottom`, `bottom_right`. Zdroj a kamera se odvodí od rodiče a
metody promítání listu; `source`, `orientation`, `camera`, `x_mm`, `y_mm` se
u projekčního pohledu odmítají. Pozdější `distance_mm` mění polohu na jeho
existujícím paprsku. Změna rodiče nebo směru již existujícího pohledu není
součástí této etapy.

```json
{"command":"drawing.view.create","arguments":{"sheet":"LIST","source":"PART","orientation":"front","x_mm":120,"y_mm":80}}
{"command":"drawing.view.create","arguments":{"sheet":"LIST","parent_view":"POHLED","projection_direction":"right","distance_mm":40}}
{"command":"drawing.view.set","arguments":{"view":"POHLED","scale":2,"show_caption":true}}
```

Při posunu rodiče se o stejný rozdíl posunou všichni projekční potomci.
Změna kamery nebo zdroje znovu promítne jejich řetězec; vlastní měřítka
potomků zůstávají zachována. Kóty se obnoví z původních měřicích referencí.
Neplatný parametr, nedostupný zdroj nebo chyba pozdějšího potomka zamítne celý
návrh před změnou historie. Jeden Undo obnoví celou výkresovou operaci.

Změna vlastností je výslovná projekce, stejně jako potvrzení dialogu: používá
poslední vypočtená data zdroje, nepočítá zdrojová tělesa a nespouští OCCT.
Krátkodobá `DrawingProjection` sdílí načtení zdrojů a projekci shodných kamer
v rámci operace/dialogu; odděluje různé zdroje. Stejnou projekci používá také
Regenerate. Uložené relativní cesty se zachovávají. Samotné otevření uloženého
výkresu a dotazy zůstávají bez projekce.

Etapa vytvoření a vlastností pohledů přidává `drawing.view.create/set`, celkem
**138 příkazů**. GUI a CLI sdílejí výpočet projekce, atomický návrh změny,
aktualizaci potomků a měřených kót. Pokryté jsou orientace, vlastní kamera,
měřítko, papírová poloha, styly, řezy a trasy. Opraveno je načtení kusovníku
neuloženého otevřeného dílu na Windows. Celá Windows Release sada prošla
**80/80** (410,05 s), `build/drawing-edit-full-tests.log`; oba výsledné programy
jsou sestavené. Testy zahrnují skutečné GUI i CLI, Undo, zachování přesných
referencí, chybu pozdějšího potomka bez částečného zápisu, zámky, měřítka,
neuložené zdroje, řez i nativní uložení. GUI je vizuálně ověřeno na
`Projects/test/command-drawing-views.png`.
