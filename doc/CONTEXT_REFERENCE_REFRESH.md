# Obnovení reference Partu v kontextu sestavy

`sketch.reference.refresh` nyní podporuje existující externí reference
Partu aktivovaného v sestavě. Příkaz vyžaduje přesnou uloženou hlavní
sestavu a cestu závislého výskytu; jiný výskyt stejného Partu se odmítne.

Příkaz čte aktuální vypočítané původní hrany, body, osy a plochy zdrojů.
Používá společný převod do souřadnic závislého Partu a následně rámec
skici. Nevypočítává těleso, sestavové řezy ani vazby; výsledek uvádí
`body_calculated: false`. Obnovená skica včetně navázané nativní křivky,
trimu a offsetu se potvrzuje jako jeden existující krok Undo/Redo.
Sestavové závislosti ani jejich vlastnictví se tímto příkazem nemění.

Otevřený zdroj je autoritativní i před uložením. Zavřený Part se načte
z nativního souboru soukromě. Chybějící hrana, nedostupný soubor nebo
soubor s jiným dokumentovým ID označí příslušné reference za neplatné,
ale zachovají poslední geometrii. Při návratu téhož původního zdroje se
reference opraví. Chybná struktura geometrie se odmítne, nepřevádí se
na novou odhadnutou referenci.

Běžné reference Partu a kořenové Assembly používají dosavadní cestu.
Příkaz nemění formát ani šablony; katalog má stále **209 příkazů**.
Tvorba a odpojení kontextové reference se společným potvrzením změny
skici a sestavových závislostí zůstávají následující etapou.

## Ověření

První testy odhalily dvě vlastnosti testovacích dat: nezávisle zadané
analytické souřadnice se po složené rotaci liší v posledních bitech a
samotná příprava sestavy nepočítá její kořenový řez. Fixture nyní jednou
normalizuje reprezentaci souřadnic a ověřuje opakovaný refresh beze změny;
prázdný řez je v podsestavě, kde příprava jeho výpočet skutečně vyvolá.

Modelové testy a skutečný CLI proces poté prošly **4/4** (18,20 s),
`build/context-refresh-tests.log`. Kontrolují:

- 257 bodů přesné racionální čtvrtkružnice po posunu zdroje o 0,01 mm,
  navázaný offset a nezměněné intervaly oříznutí;
- všechny čtyři druhy referencí, zdroj před uložením, zavřený zdroj,
  zachování poslední křivky při ztrátě hrany a opravu při jejím návratu;
- chybnou identitu souboru, nepovolený opakovaný výskyt, žádnou změnu
  historie/generace/geometrie hlavní sestavy, Undo/Redo a nativní zápis;
- samostatný CLI proces, který otevře sestavu, aktivuje Part, obnoví
  referenci a uloží jeho `.prtz`, i odmítnutí jiné cesty výskytu.

Nové zprávy kontextu a kontroly cyklů jsou přeložené do cs/en/de/fr/ru.

Po doplnění případu fyzicky chybějícího nativního souboru a novém sestavení
všech programů prošla **celá sada 113/113** (511,11 s).
Logy: `build/context-refresh-all-build.log` a
`build/context-refresh-full-tests.log`. Zahrnuje modelové příkazy,
regeneraci, nativní soubory, import/export, reference, skicář, výkresy,
společné GUI dialogy, celý start aplikace, konzoli a všechny překlady.
