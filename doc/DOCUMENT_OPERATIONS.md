# Dokumentové operace bez GUI

## První oddělená etapa (2026-09-11)

`cpp/modules/workspace/document_operations` obsahuje společné ukládání Partu,
Assembly a Drawing a dokumentové Undo/Redo. Veřejné rozhraní je v
`include/zima/workspace/document_operations.hpp`, implementace v
`src/document_operations.cpp`. Nepoužívá Qt, okna, picker ani aplikační smyčku.
GUI i konzole používají tutéž implementaci, původní kopie logiky byly odstraněny.

Vstupem operace je Workspace, přesné ID dokumentu a cílová cesta, případně
směr historie. Výstupem je uložený nativní dokument nebo změněný stav historie.
Zobrazení, dialogy, lokalizované zprávy a stavový panel zůstávají v aplikaci.

## Ukládání

1. `prepare_document_save` na vlákně vlastnícím Workspace pořídí snímek
   dokumentu a již vypočtených dat. Nic nezapisuje a nemění dirty stav.
2. `DocumentSave::write` pracuje jen se snímkem. Může běžet na pracovním vlákně
   nebo synchronně v programu bez GUI. Používá stávající nativní serializaci.
   Až po úspěšném zápisu vrací `SavedDocument`.
3. `complete_document_save` na vlákně Workspace dohledá dokument znovu podle
   identity. Neuchovává ukazatel do vektoru otevřených dokumentů přes pracovní
   úlohu. Teprve zde aktualizuje cestu a případně označí dokument za uložený.

Revize parametrů sama nestačí: explicitní přepočet může změnit vypočtená data
bez nové položky historie. Dokončení kontroluje revizi, generaci dat a počet
přidělených rozměrových identifikátorů. Novější změny zůstávají dirty.
AssemblySession proto stejně jako DocumentSession vystavuje runtime generaci
svých dat; tato hodnota není součástí souborového formátu.

Každý otevřený stav má runtime identitu odlišnou od trvalého ID dokumentu.
Dokončení staré úlohy nesmí změnit dokument, který byl mezitím zavřen a znovu
otevřen ze stejného souboru. Odmítne také mezitím změněnou cílovou cestu.
Pracovní snímek není revize připnutá k sestavě a nevytváří žádný sidecar.

Výkres zatím nemá dokumentovou session s dirty revizemi. Uložení zachovává
stávající chování: zapíše snímek a aktualizuje cestu, novější obsah v paměti
nepřepisuje. Rozšíření správy výkresové historie není součástí této etapy.

## Historie

`can_step_document_history` a `step_document_history` používají existující
Part/Assembly session. Part po úspěšném kroku synchronizuje závislosti externích
skic stejně jako dříve. Operace nemění aktivní ani zobrazený dokument.

V GUI zůstávají zákazy během editace, lokální historie řezu/skici, zrušení
rozpracovaného segmentu a obnova pohledu. Tyto interakční kroky nemají patřit
do dokumentového jádra. Není to plošný audit všech transakcí Undo/Redo.

## Ověření a další hranice

`zima_cpp_document_operations_tests` běží bez QApplication. Ověřuje:

- skutečný zápis a opětovné načtení všech tří nativních typů;
- identitu dokumentu, geometrii a reference Partu a komponent sestavy;
- neměnný snímek zapisovaný na pracovním vlákně;
- novější editace a vypočtená data během ukládání;
- selhání zápisu bez změny cesty, revize a dirty stavu;
- odmítnutí chybějícího dokumentu a prázdné cesty;
- Undo/Redo Partu a Assembly, prázdnou historii a nepodporovaný typ;
- změnu cesty, zavření a opětovné otevření během ukládání;
- zachování aktivního a zobrazeného dokumentu.

Integrační test konzole nadále ověřuje kombinaci GUI editace, konzolového
uložení a Undo/Redo proti skutečnému souboru. Celá regresní sada se spouští
přes `tools/build-windows.ps1 -Configuration Release -RunTests`.

Otevírání, vytváření podle config šablon a regenerace zatím mají aplikační
orchestraci v hlavním okně. Další etapou je přesun těchto operací a sestavení
hostitele příkazů bez GUI; samotný přenos textu/JSON již GUI nevyžaduje.
Formát souborů se nemění; stávající přípony a config šablony zůstávají platné.

## Výsledek ověření

Windows Release: kompletní sada **52/52 prošla**, 374,17 s. Testovací program
nových operací byl navíc spuštěn samostatně a jeho závislosti byly ověřeny přes
`dumpbin /dependents`: neobsahuje Qt. Úplný protokol je v lokálním
`build/document-operations-full-tests.log`.

Po doplnění lokalizace chybové zprávy a přesného porovnání ID původních ploch
byly znovu přeloženy dotčené cíle. Následně prošly `zima_cpp_ui_contract_tests`,
`zima_cpp_document_operations_tests` a `zima_cpp_console_ui_contract` (3/3,
12,27 s). Geometrický test porovnává vlastníka, sémantický klíč a cestu výskytu
každé uložené původní plochy, ne pouze počet referencí.
