# Budoucí migrace ZIMA-CAD do C++

## Stav rozhodnutí

Tento dokument zachycuje schválený dlouhodobý směr, nikoliv právě aktivní
přepis. Nejprve se dokončí rozpracované zásadní funkce současné Python verze,
stabilizuje se datový model a uživatelské kontrakty a doplní se deterministické
testy. Potom následuje feature freeze a řízená migrace do C++.

Python verze zůstane během migrace referenční implementací. C++ část nesmí být
považována za náhradu, dokud nad stejnými vstupy neposkytne stejné persistované
výstupy, geometrické výsledky, chybové stavy a uživatelské chování.

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
