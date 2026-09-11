# Pokrytí CAD operací příkazovou vrstvou

Uživatel 2026-09-11 rozšířil zadání na úplnou příkazovku pro všechny podporované
CAD operace a povolil pokračovat po samostatně testovaných a commitovaných krocích.
Tento seznam zachycuje skutečný stav; přítomnost CLI procesu sama neznamená
úplné pokrytí modelování. Napojení AI následuje až po společných příkazech.

## Kritéria dokončení

- Operace nad dokumentem používá stejnou datovou transakci z GUI i konzole/CLI.
- Objekty a geometrické reference se zadávají stabilními ZIMA ID a přesnou cestou
  výskytu, nikoli pořadovým číslem plochy OCCT nebo textem položky stromu.
- Čtení dat nepočítá tělesa. Výpočet je výslovný, se společným řešením referencí,
  vlastnictvím těles a sestav a odpovídající historií Undo/Redo.
- Příkazy mají ověřitelné argumenty, chyby, výsledek a dokumentované jednotky.
- Existují testy skutečných modelových výsledků a vstupu přes CLI; převáděná
  GUI cesta se ověřuje rovněž. Zamítnutý požadavek nesmí zanechat částečnou změnu.
- Exporty a ukládání ověřují skutečné soubory. Nezavádějí se povinné vedlejší
  soubory mimo nativní `.prtz`, `.asmz` a `.drwz`.

Čistě myší interakce (hover, tažení kamery, dialogový náhled) zůstávají adaptérem
GUI. Jejich modelový výsledek musí jít zadat příkazem. CLI bez View nevymýšlí
výběr ani kameru; k takové interakci používá explicitní reference a parametry.

## Postup a stav

| Oblast | Stav | Zbývající rozsah |
| --- | --- | --- |
| Proces CLI, UTF-8, skripty, stdin, config | Hotovo | Rozšiřovat testy nových příkazů |
| Katalog, kontext, datový strom | Hotovo pro současné příkazy | Doplňovat popisy a dotazy podle domén |
| Typované argumenty | Hotovo | Řetězce, čísla, celá čísla, boolean, objekty a pole; validace před mutací |
| New/Open/Save, Save As, aktivace/zavření, pracovní adresář | Hotovo | Přejmenování souborů a správa archivů jsou další samostatné operace |
| Regenerate, Undo/Redo | Základ Part/Assembly hotov | Výkresové operace a jejich historie |
| Kvádr | Hotovo | Společná tvorba, čtení a rozměrový patch; včetně zámků a přesnosti |
| Válec, koule, kužel, jehlan, klín | Hotovo | Společné create/get/set, zámky, přesnost, GUI/CLI a 59/59 regresí |
| Historie Partu | Hotovo | Společný přesun, ověření závislostí, potlačení, odstranění a kurzor; včetně historie těles a Booleanů |
| Tělesa a Boolean | Základ hotov | Tvorba, čtení, aktivace, název/viditelnost, kurzory a Boolean create/get/set; pořadí/mazání řeší historie; zbývá příkazové umístění a odvozené kopie |
| Umístění a původní reference | Zbývá | Dotazy a zadání do existujícího společného řešení umístění |
| Konstrukční geometrie | Zbývá | Body, osy, roviny, 3D křivky |
| Skicář: geometrie | Zbývá | Tvorba a úprava všech podporovaných křivek a textu |
| Skicář: vazby a operace | Zbývá | Kóty, vazby, trim, extend, offset, mirror, uvolnění referencí |
| Externí reference skici | Zbývá | Původní geometrie, projekce, aktualizace a zachování trimu |
| Vytažení a rotace | Zbývá | Vlastněný profil, thin, zakončení, více směrů |
| Tažení | Zbývá | Sweep 2D/3D, loft a helical včetně profilů a drah |
| Otvory a závity | Zbývá | Hole, Thread, ShaftThread, DrillPoint a reference |
| Zaoblení, zkosení, skořepina | Zbývá | Výběr skutečného vstupního tělesa a sdílené transakce |
| Zrcadlo a pole | Zbývá | Odvozená tělesa a komponenty |
| Sestavy | Zbývá | Komponenty, přesné výskyty, vazby, aktivace a řezy |
| Výkresy | Správa neuložených změn hotova | Příkazy pro listy, pohledy, kóty, anotace, šablony, BOM, Show/Erase |
| Řezy, měření a vzhled | Zbývá | Datové operace a uložené výsledky |
| Parametry, relace a materiál | Zbývá | Jednotky, fyzikální údaje a rodinné tabulky |
| Import a export | Zbývá | STEP/IGES/DXF, přesnost, cílový Part/Assembly a podporované exporty |

Každá další etapa aktualizuje tabulku a uvádí ověřené testy. Neobcházíme
chybějící operaci nevalidovanou změnou serializovaného dokumentu ani voláním
widgetů ze samostatného CLI. Plošný audit Undo/Redo zůstává samostatným úkolem;
regrese historie potřebné pro právě převáděnou operaci jsou součástí etapy.

Etapa správy dokumentů: katalog měl **34 příkazů**. Kompletní Windows Release
sada ověřila 58/59 testů; nová regrese odhalila chybějící přesměrování vlastního
řádku kusovníku v kopii výkresu. Po opravě prošlo všech **9/9 dotčených testů**
(16,71 s), včetně původně selhávající regrese. Podrobnosti a logy jsou v
[DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md).

Etapa těles přidala devět příkazů; katalog měl **43 příkazů**. Sdílené
vlastnosti těles a Booleanů, aktivace a kurzory prošly kompletní sadou **60/60**
(383,35 s) a závěrečnou sadou **6/6** po sjednocení výběru (12,28 s).
Podrobnosti jsou v [BODY_COMMANDS.md](BODY_COMMANDS.md).

Etapa historie přidala šest příkazů; katalog má nyní **49 příkazů**. Celá
Windows Release sada prošla **61/61** (386,26 s), včetně nových modelových,
procesových i GUI scénářů. Podrobnosti jsou v [HISTORY_COMMANDS.md](HISTORY_COMMANDS.md).
Další krok: dotazy na původní reference a příkazové zadání stávajícího umístění.
