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
| New/Open/Save, Regenerate, Undo/Redo | Základ hotov | Save As, zavírání/aktivace dokumentu a další dokumentové operace |
| Kvádr | Hotovo | Společná tvorba, čtení a rozměrový patch; včetně zámků a přesnosti |
| Válec, koule, kužel, jehlan, klín | Hotovo | Společné create/get/set, zámky, přesnost, GUI/CLI a 59/59 regresí |
| Historie Partu | Zbývá | Přesun, potlačení, odstranění, kurzor |
| Tělesa a Boolean | Zbývá | Tvorba, aktivace, vlastnosti a operace mezi tělesy |
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
| Výkresy | Zbývá | Listy, pohledy, kóty, anotace, šablony, BOM, Show/Erase |
| Řezy, měření a vzhled | Zbývá | Datové operace a uložené výsledky |
| Parametry, relace a materiál | Zbývá | Jednotky, fyzikální údaje a rodinné tabulky |
| Import a export | Zbývá | STEP/IGES/DXF, přesnost, cílový Part/Assembly a podporované exporty |

Každá další etapa aktualizuje tabulku a uvádí ověřené testy. Neobcházíme
chybějící operaci nevalidovanou změnou serializovaného dokumentu ani voláním
widgetů ze samostatného CLI. Plošný audit Undo/Redo zůstává samostatným úkolem;
regrese historie potřebné pro právě převáděnou operaci jsou součástí etapy.
