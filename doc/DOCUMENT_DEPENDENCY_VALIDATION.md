# Společná kontrola závislostí dokumentů

Vložení komponenty a přidání externí reference Partu používají stejný
průchod orientovaným grafem dokumentů. Nová hrana vlastník → zdroj se
odmítne, pokud zdroj přímo nebo nepřímo závisí na vlastníkovi.

Průchod zahrnuje vložené komponenty a reference všech skic: kořenové,
vlastněné profily prvků i řezy. Otevřený dokument má přednost před
uloženým souborem. Zavřené zdroje se načítají soukromě z nativních souborů,
včetně zdroje pod zavřenou mezilehlou sestavou; relativní cesta se řeší
vůči skutečnému vlastníkovi. Identita načteného souboru musí souhlasit.
Neznámý zdroj se nesmí považovat za nezávislý.

Kontrola neotevírá živé karty, nemění aktivaci, historii ani vypočtená data
a nepoužívá OCCT. Opakované navštívení stejného dokumentu se vynechá,
aktivní rekurzní zásobník zachytí cyklus a hloubka je omezená na 256.
Při hledání nativního zdroje se prochází uložená hierarchie výskytů.

Tato etapa rozšiřuje kontrolu před přidáním externí závislosti, která
dříve prohlížela jen kořenové skici otevřených Partů. Neřeší dosud
společné atomické potvrzení nové skici a všech sestavových závislostí;
to zůstává navazující prací pro kontextové příkazy. Solver umístění,
formát, šablony a počet **209 příkazů** se nemění.

## Ověření

Po opravě neúplné geometrie testovacího Helical prvku prošla modelová
sada **3/3** (0,83 s), `build/document-dependency-tests.log`.
Používá platný existující geometrický fixture a řetězec zavřených Partů
s referencí ve vlastněném Helical profilu. Ověřuje cyklus, úspěšný
acyklický průchod, neuložený otevřený zdroj, chybějící zdroj, chybnou
identitu, cyklus přes vložení a nezměněný živý stav při odmítnutí.

Všechny programy jsou sestavené podle
`build/document-dependency-all-build.log`. Následná sada **9/9**
(37,09 s), `build/document-dependency-integration-tests.log`, zahrnuje
samostatný CLI proces, Workspace, reference, aktivaci, vložení a
odstranění komponent, skutečné GUI vlastností i projekce profilů.
