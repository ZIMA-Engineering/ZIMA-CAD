# Výkresy přes společné příkazy

První výkresová etapa zpřístupňuje listy, jejich parametry, vložené formáty a
razítka. Katalog má 136 příkazů. Tvorba a parametrická editace pohledů, měřené kóty,
anotace, BOM, Show/Erase a výkresové exporty zatím nejsou kompletně pokryté.

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
