# Odstranění komponenty v CLI a GUI

`component.remove` odstraňuje konkrétní bezprostřední výskyt v aktivní
Assembly. Používá stejnou funkci `workspace::remove_component` jako akce
Odstranit v kontextovém menu. Katalog obsahuje **207 příkazů**.

```json
{"command":"component.dependencies","arguments":{"instance_path":"PRESNA-CESTA"}}
{"command":"component.remove","arguments":{"instance_path":"PRESNA-CESTA"}}
```

Cesta pochází z `component.list/get`. Rodič tímto příkazem nemaže vnitřní
díl vložené podsestavy. Volitelné `document` musí odpovídat aktivní sestavě.
Během dialogu/skicáře se příkaz odmítne. Výsledek obsahuje `occurrence`,
`instance_path`, `document`, `revision`, `removed:true`, `changed:true`.
Neexistující výskyt je chyba, nikoli úspěšné odstranění.

## Zachovaná pravidla

Kontrola závislostí je stejná jako dosavadní GUI a `component.dependencies`.
Mazání blokuje použití výskytu v řádku vazby komponenty, závislost jiného
výskytu a externí reference sestavové skici. To zahrnuje i vlastní řádky
umístění mazaného dílu: nejdříve se odstraní přes Vlastnosti nebo
`component.set` s prázdným `placement_references`. Odvozená kopie blokuje
svůj zdroj; kopii lze odstranit samostatně. Není zaveden přepínač obcházející
závislosti ani kaskádové smazání navázaných objektů.

Zdrojové dokumenty, jejich soubory a další výskyty téhož Partu zůstávají.
Odstranění má jediný krok Undo/Redo s původní stabilní identitou výskytu.
GUI ponechává stávající potvrzovací otázku; explicitní CLI příkaz se
provede přímo jako ostatní příkazové modelové operace.

## Atomická příprava sestavy

Původní GUI před mazáním běžného výskytu zveřejnilo regenerované zdroje
v živé sestavě a teprve potom upravovalo a počítalo řezy. Nová společná
transakce používá tutéž existující přípravu závislostí, ale její výsledek
zůstává soukromým kandidátem až do úspěšného potvrzení celého odstranění.
`Workspace::prepare_assembly_calculation` pouze zpřístupňuje dosavadní
výpočet kandidáta; samotný solver, jeho pořadí ani reference se nemění.
Stejnou přípravu nadále používá běžná regenerace.

Běžný výskyt se odstraňuje z aktuálních vstupních zdrojových dat, včetně
neuloženého vypočítaného Partu. Zdrojový Part se neukládá ani nepřepočítává.
Odstranění odvozené kopie zachovává dosavadní větev nad uloženou sestavou.
Po odstranění výskytu se podle stávající politiky upraví cíle řezů,
odeberou jím vlastněné závislosti, vyřeší umístění a spočtou řezy. Až poté
vznikne jediný commit sestavy. Selhání přípravy, řezu nebo fyzikální relace
nezanechá částečné polohy, zdrojové pakety, historii ani změněnou generaci.

Řezy převezmou čerstvé vstupy před řezáním. Jejich rollback data obsahují
jen zbývající výskyty. Po odstranění posledního cíle zůstává definice řezu
s prázdným seznamem cílů. Dosavadní politika ztráty cíle v poli
`extrusion.target_face` pro `UpToPlane/UpToSurface` zůstává převodem na
slepé zakončení a vyčištěním tohoto cíle; touto etapou se kontrakt
zakončení otvorů ani obecné umístění kontejnerů nepředělávají.

Veškerá data nadále používají existující `.asmz`, `.prtz` a jejich uložené
reference. Formát a start šablony se nemění.

Známá chyba pořadí řetězců sestavových vazeb a samostatně čekající souhlas
s její opravou jsou popsány v [ASSEMBLY_MATE_ORDER_REVIEW.md](ASSEMBLY_MATE_ORDER_REVIEW.md).
Odstranění tuto zamítnutou změnu solveru neprovádí ani neobchází.

## Ověření

První sestavení a oba cílené modelové testy prošly **2/2** (0,83 s),
`build/component-removal-model-build.log` a
`build/component-removal-model-tests.log`. Test vlastností byl přestavěn
z commitované verze bez odložené reprodukce řetězce vazeb.

Test odstranění kontroluje přesné výskyty, všechny skupiny blokujících
závislostí, aktivní editaci a vnořenou cestu, neuložený zdroj, zachovaný
zdrojový soubor, Undo/Redo, odvozenou kopii a poslední cíl řezu. Objem
3000 mm³ s odečtením obdélníkového průřezu 200 mm³ zůstává po odstranění
jiného cíle správně 2800 mm³; rollback vstup je 3000 mm³ a je ověřen i
nativní výsledek. Výslovně testuje selhání fyzikální relace po úspěšné
přípravě nových zdrojů, které dříve mohlo částečně změnit živou sestavu.


Oba programy a testovací programy jsou sestavené. Dotčená sada prošla
**20/20** (192,48 s), `build/component-removal-all-build.log` a
`build/component-removal-related-tests.log`. Zahrnuje skutečný CLI proces,
konzoli, kontextové menu s potvrzením a Undo, celý startovací GUI kontrakt,
zdrojové dokumenty, sestavy, importy, řezy, odvozené kopie a překlady.
Sada nyní obsahuje 108 registrovaných testů; tato etapa cíleně spustila
20 souvisejících, nikoli nový úplný běh všech 108.
