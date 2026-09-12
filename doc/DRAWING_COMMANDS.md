# Výkresy přes společné příkazy

První výkresová etapa zpřístupňuje listy, jejich parametry, vložené formáty a
razítka. Katalog má 132 příkazů. Tvorba a editace pohledů, měřené kóty,
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
