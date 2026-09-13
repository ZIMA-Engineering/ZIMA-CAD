# Otevírání dokumentů bez kopírování vypočtené geometrie

Ve Windows růst seznamu otevřených dokumentů kopíroval vypočtené hranice
již otevřeného Partu i jeho historii. Samotná oprava historie Partu nestačila:
společný `DocumentState` obsahuje také Assembly a Drawing a jejich přesun
mohl vyvolat výjimku, takže vektor volil kopii všech dosavadních dokumentů.

AssemblySession a DrawingState nyní používají jednoznačně vlastněné aktuální
a historické stavy stejně jako DocumentSession. Přesun všech tří typů je
bezvýjimečný; výslovná kopie relace nebo Workspace nadále vytváří nezávislá
editovatelná data a historii. Stávající sdílení neměnných B-Rep/BodySnapshot
zůstává zachované. Nativní struktura dokumentů se nemění.

Potvrzení, výměna Assembly a aktualizace jejích vypočtených závislostí dokončí
validaci a přípravu dat před změnou živého stavu. Odmítnutí nezmění revizi,
generaci, Undo/Redo, uložený stav ani geometrii. Čistá obnova zobrazení zdrojů
zachovává současný dokument, historii a stav uložení a nespouští fyzikální
relace, řešení vazeb ani výpočet těles.

## Ověření

`zima_cpp_workspace_publication_tests` otevírá 48 smíšených dokumentů a
porovnává adresy vypočtených hranic Partu i jeho historie. Původní implementace
selhala (0/1 za 0,11 s), protože při růstu seznamu geometrii překopírovala.
Opravené chování se ověřuje bez časového benchmarku; překladač navíc kontroluje
bezvýjimečný přesun celého `DocumentState`.

Test zahrnuje 24 kroků historie Assembly a Drawing, kopii a přiřazení relací,
nezávislost výslovné kopie Workspace, odmítnuté fyzikální relace a jednotky
Assembly, neplatnou identitu Drawing a konflikt čísel rozměrů. Objem zdroje
1000 mm³ se změní na 2000 mm³: zobrazovací aktualizace respektuje svůj dosavadní
kontrakt, zatímco explicitní fyzikální aktualizace odmítne dělení nulou.

Cílená sada prošla **5/5 za 0,84 s**, včetně nativního uložení, identifikátorů
rozměrů, vlastností komponent a příkazů výkresů. Po závěrečné kontrole zachování objektu aktuální Assembly při obnově jejího
zobrazení prošlo celé sestavení obou aplikací a **117/117 testů za 501,22 s**.
Protokoly: `build/workspace-publication-all-build.log` a
`build/workspace-publication-full-tests.log`.
