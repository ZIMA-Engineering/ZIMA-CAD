# Atomické potvrzení a historie Partu

Společná relace Partu nyní dokončí validaci fyzikálních relací, identifikátorů
rozměrů a přípravu paměti před publikací změny dokumentu. Odmítnuté `commit`,
`replace` ani `update_calculated_boundaries` nesmějí měnit revizi, generaci,
uložený stav, dostupné Undo/Redo nebo vypočtenou geometrii.

## Uložení stavů

Aktuální stav a jednotlivé položky historie mají jednoznačné vlastnictví pomocí
`std::unique_ptr`. Potvrzení přesune připravený stav; růst vektoru historie
přesouvá jen ukazatele. To je důležité také ve Windows: přesun celého
`PartDocument` nemusí být bezvýjimečný a růst vektoru hodnot mohl kopírovat
staré vypočtené hranice.

Výslovná kopie celé `DocumentSession` nadále vytváří nezávislý dokument a historii.
Přesun relace je bezvýjimečný. Nativní formát ani význam historie se nemění.
Importované B-Rep zůstává sdílené přes dosavadní `shared_ptr`; ověřování nových
fyzikálních hodnot nekopíruje dosavadní vypočtené hranice.

Undo/Redo si nejprve připraví zachování přidělených identifikátorů rozměrů.
Teprve potom přesune vlastnictví stavů. Zamítnutá editace nespotřebuje nové
číslo revize a nezruší dosud dostupné Redo. Přepočet mění generaci a příznak
neuloženého výpočtu; nevytváří samostatný editační krok.

## Ověření

Nový `zima_cpp_document_session_transaction_tests` používá kvádr o objemu
1000 mm³ a změnu na 2000 mm³ s relací, která druhý stav odmítne dělením nulou.
Kontroluje chyby fyzikální relace, jednotek a identifikátorů, původní geometrii,
stav uložení, čítače i pokračování Undo/Redo. Při 24 dalších krocích porovnává
skutečné adresy vypočtených hranic při průchodu historií a ověřuje nezávislost
výslovné kopie relace.

Původní implementace test odmítla za 0,13 s: odmítnutá transakce změnila generaci.
Po opravě prošly 3/3 cílené testy za 0,48 s včetně identifikátorů rozměrů a
ukládání dokumentů. Po rozšíření testu prošlo celé sestavení obou aplikací a
**115/115 regresí za 502,04 s**, včetně skutečného procesu CLI, konzole,
startu GUI, vnitřních profilů, přesných spline, offsetů a nativních dokumentů.
Výsledky jsou v pracovních protokolech `build/part-transaction-all-build.log`
a `build/part-transaction-full-tests.log`.
