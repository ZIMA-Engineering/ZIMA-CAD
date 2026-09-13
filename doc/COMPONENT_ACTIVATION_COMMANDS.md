# Aktivace komponenty v sestavovém kontextu

`component.activate` a `component.deactivate` sdílejí aktivaci s GUI.
Katalog obsahuje 209 příkazů. Aktivace je dočasný stav Workspace; nemění
formát dokumentů ani šablony.

## Příkazy a adresování

```json
{"command":"component.activate","arguments":{"document":"TOP_ASSEMBLY_ID","instance_path":"EXACT_PATH_FROM_COMPONENT_LIST"}}
{"command":"context"}
{"command":"box.set","arguments":{"container":"SOURCE_BOX_ID","height_mm":"40"}}
{"command":"save"}
{"command":"component.deactivate"}
```

`document` je u aktivace nepovinné ID **zobrazené hlavní sestavy**. Výchozí
hodnota je aktuální zobrazený dokument. `instance_path` je celá přesná
cesta od této sestavy; převezměte ji z `component.list recursive=true`
s explicitním ID hlavní sestavy. Cestu nesestavujte z názvů komponent.
Aktivace odvozené kopie přechází na původní editovatelný výskyt.

Výsledek obsahuje `document` (ID editovaného zdroje), `displayed_document`,
kanonický `instance_path`, `opened` a `changed`. Opakovaná aktivace stejného
výskytu vrací `changed=false`. Dva výskyty stejného Partu mají stejné ID
zdroje, ale rozdílnou cestu aktivace. `context.active_occurrence` tuto cestu
vrací ve skutečném CLI i v GUI konzoli.

Aktivovaný Part přijímá příkazy modelování nad svými vlastními objekty.
Aktivovaná podsestava přijímá sestavové příkazy nad svými bezprostředními
komponentami. Zde `component.list/get/set/remove/dependencies` používají
lokální cesty od aktivního zdrojového Assembly a jejich nepovinné
`document` označuje tento zdroj. Například po aktivaci podsestavy nejprve
zavolejte `component.list` a její vrácené cesty předejte `component.set`.
Celá cesta z hlavní sestavy není lokální adresou příkazu `component.set`.

`component.insert` a vložení z GUI vloží komponentu právě do aktivní
vlastnící podsestavy. GUI poté otevře stejné Vlastnosti nového výskytu
v úplném sestavovém kontextu. Závislostní cyklus zůstává zakázaný.

`component.deactivate` vrátí editaci do zobrazené hlavní sestavy. Pokud
žádná komponenta není aktivovaná, vrací `changed=false`. Naproti tomu
`component.open` a `activate DOCUMENT_ID` otevřou zdroj na jeho vlastní
kartě a ukončí sestavovou aktivaci.

## Vlastnictví, historie a zobrazení

- Aktivace otevře chybějící nativní zdroj bez výpočtu tělesa a bez řešení
  vazeb. U hluboké cesty nemusí mezilehlé sestavy otevírat jako karty.
- Již otevřený zdroj je autoritativní včetně neuložených změn; aktivace
  jej nepřepíše daty ze souboru.
- Modelování, Undo/Redo, uložení a výslovný Regenerate pracují s aktivním
  zdrojem. Hlavní sestava zůstává viditelná jako kontext. Aktualizace
  zobrazení současných zdrojů sama nepřepočítává její vazby ani operace.
- Zavření aktivního zdroje vrátí editaci do hlavní sestavy. Zavření hlavní
  sestavy ponechá otevřený aktivní zdroj na jeho vlastní kartě. Zavření
  nesouvisejícího dokumentu přes CLI uchová přesnou aktivaci.
- Otevřená editace blokuje aktivační příkazy stejně jako ostatní mutace.
  Nesoulad aktivního zdroje a uložené cesty blokuje modelovou mutaci.
- Čtení zdroje přes GUI event loop sleduje také přesnou cestu aktivace.
  Novější přepnutí mezi výskyty stejného zdroje nesmí čekající čtení
  přepsat. Chybné čtení nebo identita zdroje se odmítnou před vložením.

Aktivace zpřístupňuje již implementované příkazy podle vlastnictví.
Příkazová tvorba externí reference do jiného Partu v sestavovém kontextu
je následující samostatná etapa; tato změna ji neoznačuje za dokončenou.
Známá oprava pořadí řetězce sestavových vazeb zůstává samostatně popsaná
v [ASSEMBLY_MATE_ORDER_REVIEW.md](ASSEMBLY_MATE_ORDER_REVIEW.md).

## Ověření

Modelové testy aktivace, otevírání zdrojů a příkazového hostitele prošly
**3/3** (0,64 s), `build/component-activation-model-tests.log`.

Regrese porovnává objem 6000 → 8000 mm³ po změně výšky 30 → 40 mm
s půdorysem 10 × 20 mm, Undo/Redo a uložený nativní zdroj. Ověřuje
opakované hluboké výskyty při zavřených mezilehlých kartách, nezměněnou
revizi/umístění hlavní sestavy, lokální vložení/změnu/odstranění,
závislostní cyklus, chybné kontexty, idempotenci a životní cyklus dokumentů.
Samostatný test kontroluje odvozený zdroj a změnu výskytu během čtení.

Úplné sestavení GUI, CLI a všech testů je v
`build/component-activation-all-build.log`. Samostatné CLI a běžná konzole
prošly integračním během. Nový GUI scénář po opravě testovací kontroly kořene
stromu prošel **1/1** (9,57 s), `build/component-activation-gui-tests.log`.
Ověřuje také vložení přes skutečné menu a otevření Vlastností v podsestavě.

První úplná sada prošla **108/109** (490,27 s),
`build/component-activation-full-tests.log`. Starý startovací scénář se
pokoušel aktivovat jinou komponentu při dosud otevřených Vlastnostech
vložení. Test nyní ověřuje odmítnutí takové aktivace, zavře dialog přes
Cancel a teprve potom pokračuje. Také tlačítko návratu do hlavní sestavy
používá společnou aktivaci a neopustí otevřené Vlastnosti nebo skicu.

Po této úpravě prošla závěrečná související sada **9/9** (174,88 s),
`build/component-activation-final-tests.log`, včetně dříve selhávajícího
celého startu GUI (93,91 s), vlastností komponent, konzole, skutečného CLI,
překladů a modelových kontraktů. Odpovídající GUI je sestavené podle
`build/component-activation-final-build.log`; ostatní programy pocházejí
z úplného sestavení této etapy.
