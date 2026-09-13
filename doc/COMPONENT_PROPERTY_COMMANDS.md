# Vlastnosti komponent v GUI a CLI

`component.set` upravuje bezprostředně vlastněnou komponentu aktivní Assembly.
Společná transakce `workspace/component_properties` obsluhuje také OK v GUI
Vlastnostech a kontextové akce viditelnosti, potlačení a uzemnění. Katalog
obsahuje **206 příkazů**. Sestavový solver ani obecné umístění kontejnerů
se touto etapou nemění.

```json
{"command":"component.set","arguments":{"instance_path":"PRESNA-CESTA","name":"Šroub M10","visible":true}}
{"command":"component.set","arguments":{"instance_path":"PRESNA-CESTA","grounded":false,"placement":{"x_mm":20,"rotation_z_deg":30}}}
```

Cestu vrací `component.list/get`. Musí obsahovat jediný bezprostřední výskyt
v aktivní sestavě; rodič nesmí tímto příkazem upravovat vnitřní komponentu
podsestavy. Volitelné `document` pouze ověřuje aktivní dokument. Během
otevřené editace se mutace odmítá. Vnořená aktivace zůstává samostatnou etapou.

Volitelné vlastnosti jsou `name`, `visible`, `suppressed`, `grounded`,
`placement` a `placement_references`; alespoň jedna musí být zadána.
Příznaky jsou skutečné JSON booleany. Název používá validaci nativních textů.
Výsledek odpovídá `component.get` a přidává `document`, `revision`, `changed`.

`placement` přijímá JSON čísla `x_mm`, `y_mm`, `z_mm` (±1 000 000 mm)
a `rotation_x_deg`, `rotation_y_deg`, `rotation_z_deg` (±180°).
Ruční změnu povoluje stejná maska volných souřadnic jako dialog a stejné
uložené číselné zámky. Uzemněný díl se nejdříve uvolní; lze to zadat ve
stejném příkazu. Zámky lze měnit již existujícím `value_lock.set`.

## Vložené vazby

`placement_references` je úplný seznam nejvýše tří řádků; prázdné pole
odstraní vložené vazby. Stejný seznam vrací `component.get`.

```json
{"command":"component.set","arguments":{"instance_path":"CESTA-POHYBLIVEHO-DILU","placement_references":[{
  "kind":"plane_coincident",
  "component":{"owner":"ID-PUVODNIHO-OBJEKTU","key":"KLIC-PLOCHY","instance_path":"CESTA-POHYBLIVEHO-DILU"},
  "target":{"owner":"ID-JINEHO-OBJEKTU","key":"KLIC-PLOCHY","instance_path":"CESTA-CILOVEHO-DILU"},
  "offset":7,"flip":false,"locked":true,"lower_limit":2,"upper_limit":9
}]}}
```

Povinné `kind`, `component`, `target` určují typ a přesné původní reference.
Druhy jsou `plane_coincident`, `plane_angle`, `axis_coincident`,
`point_coincident`. Reference obsahuje `owner`, `key` a výslovně uvedené
`instance_path`. Prázdná cesta označuje vlastní datum Assembly.
Volitelné referenční `kind` musí odpovídat vazbě (`face`, `axis`, `point`).

Pohyblivá strana patří umísťovanému výskytu, cílová je na něm nezávislá.
Reference může ukazovat dovnitř podsestavy, ale vždy umísťuje její celý
bezprostřední výskyt. Nepřenáší vlastnictví vnitřního dílu do rodiče.
Změněné reference se ověřují proti existujícím původním datům; nepoužívají
pořadí OCCT ploch ani výsledné těleso jako vlastníka. Nová cyklická vazba
se odmítne společně s celým příkazem.

`offset` je vzdálenost v mm u plošné vazby (±1 000 000 000), úhel ve stupních
u `plane_angle` (±180). Osová a bodová shodnost přijímají nulový offset. Uložené meze
u nich musí zahrnovat nulu; stejně jako v GUI samy neomezují zbývající
volný pohyb podél osy nebo volné rotace. `flip` a `locked` jsou booleany. `lower_limit`/`upper_limit` mohou být
čísla nebo `null`, které mez zruší; hodnota musí ležet uvnitř mezí.
Při zachování stejného typu a dvojice referencí se nezadané hodnoty převezmou
z existujícího řádku, včetně jeho zámku. U nové dvojice jsou výchozí hodnoty
nula, bez Flip a bez mezí. Plošné/úhlové vazby začínají bez zámku,
osová/bodová shodnost má stejně jako po výběru v GUI nulovou hodnotu zamčenou.
Přesun stejného řádku v seznamu nesmí
obejít zamčený offset. Výslovné `locked:false` dovolí odemčení a změnu
hodnoty společně, jako OK ve Vlastnostech.

## Transakce a výpočty

Společná příprava zachytí revizi a pouze vlastnosti vlastněné výskytem.
Potvrzení nekopíruje zpět zastaralý balík zdrojové geometrie, cestu, identity
ani zdrojový vzhled z otevřeného dialogu. Změna názvu nebo viditelnosti
neřeší vazby a zachová vypočítaná tělesa. Změna polohy, referencí, uzemnění
nebo potlačení převezme současné zdrojové pakety stejným sdílením jako GUI
a použije dosavadní `calculate_placement_references` na kandidátovi.
Nepočítá OCCT, odvozené kopie ani sestavové řezy; jejich aktualizace patří
výslovné regeneraci. Neuložený zdroj zůstává autoritativní.

OK/CLI uloží jediný krok Undo, shodné hodnoty nevytvoří změnu. Cancel
ponechá stav beze změny. Stará příprava po změně revize se odmítne.
Zrcadlo/Pole dovoluje vlastní název, viditelnost a potlačení, jeho umístění
se mění příkazy `mirror.set`/`pattern.set`, nikoli běžnou polohou komponenty.

Používají se současná pole `.asmz`; formát ani start šablony se nemění.

## Ověření etapy

Dosavadní modelový a skutečný GUI test prošly po sdílení potvrzení **2/2**
(8,29 s), `build/component-properties-shared-build.log` a
`build/component-properties-shared-tests.log`. GUI scénář je nyní
samostatně registrovaný v CTest jako `zima_cpp_component_properties_ui_contract`.

Nový modelový test kontroluje nezávisle očekávané souřadnice, všechny čtyři
druhy vazeb, úhel/Flip, původní plochy, volné stupně pohybu, číselné zámky,
meze, přesnou identitu opakovaných výskytů, cykly, neplatné vstupy, jediný
Undo/Redo, no-op, nativní data a nezměněný zdrojový Part. Odhalil zvýšení
`AssemblySession::data_generation` před úspěšným potvrzením při selhání
fyzikální relace; revize i obsah přitom zůstaly stejné.
Reprodukce: `build/component-properties-rejection-tests.log`, **0/1**.

Čítač se nyní mění až po ověření kandidáta. Po opravě prošel modelový test
**1/1** (0,18 s), `build/component-properties-atomic-build.log` a
`build/component-properties-atomic-tests.log`. GUI navíc ověřuje, že se
příkazové jméno a plošná vazba zobrazí ve Vlastnostech, následné OK je čitelné
z konzole a obě změny lze samostatně vrátit. Procesový test načítá skutečné
`.asmz`, mění komponentu, provádí Undo/Redo a ověřuje uložený posun 7 mm,
meze, zámek a zachovaný objem 6000 mm³.


Úplné sestavení obou programů a sada **107/107** testů prošly (496,95 s),
`build/component-properties-all-build.log` a
`build/component-properties-full-tests.log`. Závěrečná revize porovnala
osové/bodové meze s GUI: jsou uložitelná metadata zahrnující nulu, nikoli
omezení dalších volných stupňů pohybu. Model i skutečný GUI scénář je nyní
výslovně používají. Nové shodnosti také přebírají stejný výchozí nulový
zámek jako GUI. Další modelový scénář mění neuložený zdroj po přípravě
Vlastností a ověřuje, že pozdější potvrzení názvu zachová novou sdílenou
geometrii 12000 mm³ a neregeneruje starou odvozenou kopii.


Po posledním doplnění jsou oba programy znovu sestavené a všech **16/16**
dotčených modelových, procesových, GUI a překladových testů prošlo (93,45 s),
`build/component-properties-final-build.log` a
`build/component-properties-final-tests.log`. Úplný běh 107 testů výše
předcházel úpravě výchozího zámku/mezi a testu zdroje změněného během editace.
