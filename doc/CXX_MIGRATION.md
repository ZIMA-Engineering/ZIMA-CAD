# Budoucí migrace ZIMA-CAD do C++

## Stav rozhodnutí

Řízená migrace byla zahájena. Python verze je ve feature freeze: nepřidávají se
do ní další velké funkční oblasti, ale přijímá opravy blokujících chyb a změny
nutné k přesnému zachycení referenčního chování. Nedokončené oblasti jako nový
STEP/DXF framework, Drawing a povrchové modelování se dotáhnou v cílové C++
architektuře.

Python verze zůstane během migrace referenční implementací. C++ část nesmí být
považována za náhradu, dokud nad stejnými vstupy neposkytne stejné persistované
výstupy, geometrické výsledky, chybové stavy a uživatelské chování.

První vertikální řez je v `cpp/`: textový prototyp Part dokumentu, přímý OCCT
výpočet kvádru, převod na ZIMA `ViewerMesh`, Qt viewer a deterministický test
objemu, plochy, meshe a save/load. Prototyp používá vlastní příponu `.zcp.json`,
aby předčasně nepředstíral úplnou kompatibilitu se současným `.prtz`.

Společný C++ `PropertiesSubWindow` a jeden `BoxPropertiesDialog` pro tvorbu i
editaci zavádějí interní `SubWindow`, pouze OK/Cancel, transakční Cancel a
potvrzení dvojklikem prostředního tlačítka i nad hlavním oknem. GUI kontrakt je
automaticky testovaný; OCCT výpočet kvádru proběhne až po úspěšném OK.

Part prototyp nyní vlastní obecnou uspořádanou historii kontejnerů místo
jednoho speciálního kvádru. Kontejnery mají stabilní unikátní ID a explicitní
operaci Add/Subtract; první Subtract, duplicitní ID a neplatné rozměry jsou
deterministicky odmítnuty. Celá historie se vyhodnotí jediným explicitním
požadavkem adaptéru a UI obdrží pouze výsledný ZIMA mesh a metriky.

Každý kontejner má také persistované posunutí v milimetrech a natočení kolem
lokálních os X, Y a Z ve stupních. Transformace se aplikuje na operand před
jeho Boolean operací, takže nejde o zobrazovací zkratku. Test oddělených a
natočených kvádrů ověřuje, že výsledný objem odpovídá skutečné OCCT geometrii.

`DocumentSession` sjednocuje transakce dokumentu. Každé úspěšné OK vytváří
právě jednu revizi, Cancel žádnou, Undo/Redo obnovují celý persistovaný stav a
savepoint určuje indikaci neuložených změn. Nový commit po Undo ruší starou
Redo větev. Nahrazení nebo zavření změněného dokumentu vyžaduje explicitní
Save/Discard/Cancel; během otevřeného Properties okna nelze změnit revizi pod
rozpracovaným dialogem.

První GPU viewer používá OpenGL 3.3 core přes `QOpenGLWidget`. Pozice, normály,
trojúhelníky a hrany se nahrávají do GPU bufferů; orbit, pan a zoom mění pouze
kamerové matice. Jeden ray test vytváří společný ordered candidate list pro
oranžový hover, LMB potvrzení přesné geometrie azurovou barvou a RMB cycling
před potvrzením. Trojúhelníky stejné persistované plochy se seskupí do jednoho
kandidáta a zvýrazní se celá přesná plocha. Deterministický test ověřuje pořadí
překrývajících se zásahů zepředu dozadu, deduplikaci ploch a cyklický návrat ve
stejné kandidátní sadě.

Plochy kvádru mají sémantické klíče `x_min`, `x_max`, `y_min`, `y_max`,
`z_min`, `z_max` vlastněné stabilním ID kontejneru. OCCT adaptér propaguje
jejich původ přes Boolean `Modified/Generated` historii. Rozporný původ
výsledné plochy se nenahrazuje náhodnou volbou; taková plocha zůstane
nenabízená, dokud nebude reference jednoznačně opravitelná.

Stejný viewer packet nyní obsahuje původní sémantické hrany a vrcholy kvádru.
Vrchol je určen kombinací `x_min/x_max : y_min/y_max : z_min/z_max`; hrana je
kanonicky seřazená dvojice svých koncových vrcholů. Dvanáct hran a osm vrcholů
proto nezávisí na pořadí OCCT traversal. Boolean historie propaguje přeživší a
rozdělené původní hrany/vrcholy. Ray-edge a ray-vertex kandidáti vznikají pouze
z hotového ZIMA viewer packetu a jsou řazeni zepředu dozadu bez dotazu do OCCT.

Viewer nyní nejprve vytvoří jediný společný ordered `ViewerCandidate` seznam
pro Container, Face, Edge a Vertex. Explicitní selection contract pouze
odfiltruje povolené druhy z tohoto již existujícího seznamu a nikdy nespouští
druhý picker. Běžný Part nabízí pouze původní kontejner; aktivní příkaz může
povolit plochu, hranu nebo vrchol. Hover, LMB potvrzení a RMB cycling stále
spotřebují stejné pořadí. Zvýraznění respektuje přesnou úroveň kandidáta:
všechny vlastněné plochy kontejneru, jednu sémantickou plochu, jednu ZIMA
polyline hrany nebo jeden persistovaný vrchol.

## Hlavní cíle

- vysoká rychlost aplikace, zejména vieweru, pickingu, velkých sestav a práce
  s rozsáhlými datovými strukturami;
- rychlá inkrementální kompilace při běžném vývoji;
- malé moduly s úzkými veřejnými rozhraními;
- zachování oddělení technického záměru, persistovaného modelu, viewer dat a
  dočasné OCCT geometrie;
- možnost postupné migrace a průběžného porovnávání s Python verzí.

## Navržené moduly

```text
Document Core
├── dokumenty a historie
├── parametry a relace
└── serializace

Geometry
├── vlastní geometrická data
├── stabilní reference
└── úzký OCCT adaptér

Viewer
├── mesh a scéna
├── picking
├── hover a potvrzený výběr
└── OpenGL renderer

Sketcher
├── geometrie skici
├── vazby a kóty
└── solver

Assembly
├── stabilní instance paths
├── dependency graph
├── sestavové vazby
└── explicitní regenerace

Drawing
├── listy a pohledy
├── kóty
└── rámečky a razítka

Application
├── Qt okna
├── dialogy
└── příkazy
```

Každý modul vlastní svá interní data. Změna Qt dialogu nesmí vyžadovat
rekompilaci Sketcheru nebo geometrického jádra; změna rendereru nesmí měnit
dokumentový model. Rozhraní mezi moduly používají datové typy ZIMA-CAD, jako
jsou `BodyResult`, `FaceReference` a `ViewerMesh`, nikoliv živé OCCT objekty.

## Hranice OpenCascade

OCCT se nebude kopírovat do zdrojového stromu ZIMA-CAD ani běžně kompilovat se
ZIMA-CAD. Aplikace bude proti předkompilovaným OCCT DLL/SO dynamicky linkovat
přes jeden úzký C++ adaptér. Samostatný OCCT proces a IPC se nezavádí, pokud
pozdější měření neprokáže konkrétní potřebu izolace procesu.

OCCT zůstává solid-modeling kernelem pro explicitní výpočty těles. Dokumenty,
historie, stabilní reference, viewer, picking, hover a UI zůstávají vlastnictvím
ZIMA-CAD. Těžké OCCT hlavičky mají být omezeny na implementaci adaptéru, aby
jejich změny nezpůsobovaly plošnou rekompilaci aplikace.

## Rychlost sestavení

Preferovaný build používá CMake a Ninja. Veřejné hlavičky mají být malé;
použijí se dopředné deklarace, PImpl a podle měření vhodné předkompilované
hlavičky. OCCT a Qt runtime se při běžném buildu aplikace znovu nekompilují.

Měří se odděleně:

- čistý build celé aplikace;
- inkrementální build po změně jednoho dialogu nebo příkazu;
- linkování;
- spuštění cílených a úplných testů;
- start aplikace, regenerace modelu, picking a vykreslení velké sestavy.

Rychlost kompilace ani runtime se nesmí pouze předpokládat; před a během
migrace se zaznamenají reprodukovatelné benchmarky.

## Pořadí migrace

1. Dokončit rozpracované zásadní funkce Python verze.
2. Stabilizovat aktuální dokumentový model a odstranit dočasné duplicitní
   cesty; staré formáty se nemigrují.
3. Doplnit testy, které fungují jako přenositelná specifikace chování.
4. Vyhlásit feature freeze; Python verze poté přijímá pouze opravy nutné pro
   referenční chování.
5. Vytvořit C++ základ s Document Core, Qt aplikací, viewerem a OCCT adaptérem.
6. Migrovat po vertikálních řezech, vždy s načtením dokumentu, výpočtem,
   zobrazením a interakcí potřebnou pro jeden úplný scénář.
7. Porovnávat každý řez s Python verzí na stejných datech a testech.
8. Přepnout hlavní aplikaci až po dosažení definované funkční a datové parity.

Mechanicky přeložené množství C++ kódu není měřítkem dokončení. Rozhodující je
ověřená shoda kontraktů a odstranění potřeby udržovat dvě aktivně se měnící
implementace stejné funkce.
