# Reference bodů vložené dráhy Sweep3D

`construction.reference.set` přijímá také stabilní ID bodu vlastněné dráhy
Sweep3D. Bod zůstává uvnitř svého tažení. Poziční a orientační řádky používají
stejnou přípravu návrhu jako samostatné konstrukce a sdílený vstup referencí
z Vlastností. Úspěšná změna se potvrzuje přes `commit_sweep`: jeden výpočet,
jedna revize, jeden krok Undo/Redo.

`construction.get` vrací `owning_feature`, `parent_construction` a
`coordinate_owner`. Souřadnice bodu jsou místní vůči jeho dráze; postupně
se uplatní rám tažení a jeho tělesa. Pomocný rám je pouze v paměti.
Při přípravě reference se používají nativní data, bez OCCT. Výpočet tělesa
nastává až při výslovném potvrzení změny.

## Použití

~~~json
{"command":"construction.reference.set","arguments":{"construction":"ID_BODU","index":0,"reference":{"owner":"ID_ZDROJE","key":"origin:plane:yz"},"offset_mm":3,"derive_orientation":false}}
{"command":"construction.get","arguments":{"construction":"ID_BODU"}}
{"command":"placement.reference.remove","arguments":{"object":"ID_BODU","index":0}}
~~~

Poziční indexy jsou 0–2, FRONT 3 a TOP 4. Příkaz zachovává zámek vzdálenosti,
párování orientace a kontrolu duplicit. Při skutečné změně vrátí
`body_calculated: true`; při bezezměnovém zadání false a nepřidá historii.

Přípustné jsou místní původní zdroje z historie před tažením, rám dráhy
a bod/osa/rovina dřívějšího bodu stejné dráhy. Sebereference, pozdější bod,
výsledná hrana dráhy či vlastní výsledné těleso jsou odmítnuté.
Kontrola zdrojů z jiných kontejnerů používá stejnou hranici historie jako
reference ostatních prvků. Neplatný výsledný tvar, neaktivní/odvozené těleso
a souběžná otevřená editace nemění dokument.

Celé umístění tažení se mění přes ID kontejneru. Hodnoty, poloměry, tečny
a pořadí jeho bodů se mění přes `sweep3d.set` / `path.points` s úplným
seznamem ID. `construction.set` nezakládá z vložené dráhy samostatný objekt.
Příkazy `placement.get/set/reference.set` se používají pro kořenové
umístění tažení a samostatné konstrukce; pro reference vloženého bodu
slouží výše uvedený `construction.reference.set`.

## Ověření

Výchozí test na předchozí verzi reprodukoval odmítnutí vlastněného bodu
(`construction_not_found`). Nová regrese navíc odhalila chybějící rámy
ostatních bodů při kontrole číselné dávky. Adaptéry samostatné křivky
i vložené dráhy nyní předávají úplné nativní rámy, takže změna vázané
souřadnice přes `path.points` nemůže obejít její omezení.

Modelové scénáře měří objemy kruhového tažení s poloměrem 2 mm:
4π√425, 4π√409 a 4π√1713 mm³. Ověřují transformaci zdroje do natočeného
rámu tažení, navíc v posunutém a natočeném tělese, řetězení bodů,
orientační reference, zamčenou vzdálenost, neplatné zdroje a zhroucenou
dráhu. Kontrolují rovněž atomické chyby, no-op, Undo/Redo a nativní uložení.

Procesový test spouští skutečné CLI, přiřadí referenci, provede Undo/Redo
a načte uložené těleso. GUI test mění odsazení v okně bodu: OK dítěte
ponechá návrh v rodiči, Cancel rodiče jej zahodí a OK rodiče potvrdí.
Stejná změna z GUI a CLI musí uložit shodnou celou definici tažení.

Širší regrese prošla **12/12 za 185,54 s**, včetně procesu CLI, GUI
konzole, referencí všech domén používajících sdílenou přípravu, odstranění
referencí a příkazů tažení. Obě aplikace i všechny testovací cíle jsou
sestavené. Logy: `build/sweep-point-all-build.log` a
`build/sweep-point-regression-tests.log`. Nejde o nový úplný běh všech testů.
Po doplnění ověření zámku přes skutečný `value_lock.set` prošel znovu
cílený test **1/1 za 1,03 s** (`build/sweep-point-final-test.log`).
Katalog má 289 příkazů a CTest 157 testů. Souborový formát a startovní
šablony se nemění; všechna potřebná data zůstávají v nativních dokumentech.
