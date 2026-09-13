# Měření ve View

Nástroj **Měření** je na liště nad 3D pohledem Partu a sestavy a lze jej
použít také v otevřeném Sketcheru. Měří aktuální vypočtenou geometrii;
nevyvolává regeneraci modelu ani jeho závislostí.

## Výběr a výsledky

Okno obsahuje dvě reference nad sebou. První výběr rovnou vypíše vlastnosti
entity a aktivuje druhé pole. Druhá reference je volitelná. Pod druhým polem
se zobrazují vlastnosti druhé entity; pod oběma souhrny je společná nejkratší
vzdálenost. Dvě tělesa tak mají každý vlastní objem, obsah a hmotnost.

| Entita | Informace |
| --- | --- |
| Bod | Souřadnice X, Y, Z v zobrazeném prostoru |
| Hrana nebo křivka | Délka |
| Ohraničená plocha | Obsah |
| Těleso nebo komponenta | Obsah povrchu, objem; hmotnost při známé hustotě |
| Osa | Bod a směr nekonečné osy |
| Konstrukční rovina | Bod a nekonečná rovina |

Se dvěma referencemi se navíc zobrazí nejkratší vzdálenost a spojnice jejích
koncových bodů. Plocha se měří pouze v rámci svého obrysu. Osy a konstrukční
roviny se pro vzdálenost chápou jako nekonečné. Překrývající se tělesa a bod
uvnitř uzavřeného tělesa mají vzdálenost nula.

Při výběru platí společné pořadí kandidátů ve View; pravé tlačítko přepíná
překryté kandidáty. Kliknutí do textu reference aktivuje její zadání či
nahrazení (zelený rámeček). Oko nezávisle zapíná azurovou inspekci uložené
reference. Křížek referenci odstraní.

- Krátké prostřední tlačítko ukončí zadávání a dočasné inspekční zvýraznění.
  Reference a výsledek zůstanou v okně.
- Prostřední tažení ovládá pohled.
- Dvojklik prostředním, také nad View, provede OK a zavře okno.
- **OK** ani **Zrušit** neukládají měření do historie.
- **Uložit** uloží pojmenované měření do stromu zobrazeného dokumentu a zavře okno.
  Rozbalí jeho nadřazené větve, označí nový záznam a posune strom k němu.
  Položka je dostupná také při zobrazení stromu Sketcheru.

## Přesnost a jednotky

Délky hran a obsahy ploch, které jsou k dispozici z explicitního výpočtu
tělesa, se uloží spolu s jeho geometrií. Inspector je čte bez dalšího volání
OCCT. Objem a hmotnost celé komponenty využívají poslední vypočtené údaje
jejího zdroje. Hmotnost se neodhaduje bez materiálové hustoty.

Vzdálenosti ke zakřivené geometrii používají její polygonální zobrazení.
Takové výsledky, stejně jako délky či obsahy bez dostupné přesné uložené
hodnoty, jsou označeny **≈**. Přesnost těchto hodnot závisí na rozlišení
zobrazené geometrie. Vzdálenosti mezi body, přímými hranami a rovnými
polygonálními plochami se počítají přímo.

Výstup používá délkové a hmotnostní jednotky dokumentu; obsah a objem
používají druhou a třetí mocninu délkové jednotky. Desetinným oddělovačem
je čárka. Po zaokrouhlení na nastavený počet míst se odstraní koncové nuly:
například `12mm`, `240mm²` nebo `0,047kg`.

## Uložené měření

Měření je informační položka, ne operace tělesa. V Partu se vloží k aktuálnímu
místu historie tělesa, v sestavě do stromu sestavy. Nese název, stabilní
reference včetně cesty instance a poslední uložené výsledky. V sestavovém
kontextu se ukládá do zobrazené sestavy. Pro uložení nebo odstranění musí
být zobrazený dokument také aktivní; během aktivace vnořeného dílu zůstává
inspector dostupný pro čtení, jeho Uložit je vypnuté. Nesmí zapisovat do
pasivní nadřazené sestavy za aktivní díl.

**Vlastnosti** znovu vyhodnotí reference nad aktuální geometrií. Chybějící
reference se označí červeně a lze ji nahradit výběrem do téhož pole. Uložení
je do opravy blokované. Zrušení opravy původní záznam zachová.
Kontextové menu obsahuje také **Odstranit**. Uložení, změna i odstranění
podporují dokumentové Undo/Redo.

Uložené hodnoty jsou snímkem posledního uložení měření; otevření vlastností
je aktualizuje pro právě zobrazený vypočtený model. Změny zdrojového Partu
se zobrazí i v jejích výskytech bez regenerace sestavy. Vazby a vlastní operace
sestavy se přepočítávají pouze výslovným příkazem **Regenerovat**. Čtení
měření tyto výpočty nespouští.

## Související ovládání kót

Jeden klik na komponentu sestavy ji pouze označí azurově. Dvojklik zobrazí
kóty jejího uložení. Kóta se vybírá samostatně nad hodnotou; dvojklik otevře
úpravu její hodnoty, pravé tlačítko nabídne vlastnosti kóty. Zamknutí a meze
hodnoty zůstávají účinné. Kliknutí do prázdna nebo ukončení zobrazení kót je skryje.

Generované průměrové kóty ve Sketcheru, Partu, sestavě a Drawingu používají
**⌀ (U+2300)** ze společného seznamu symbolů. Tato značka se přenáší také
do výstupů používajících společné formátování kót.

## Ověření

- `zima_cpp_measurement_contract_tests`: analytické vzdálenosti, ohraničené
  plochy, průniky, uzavřená tělesa, posunutý model, uložené přesné veličiny
  válce, persistence Part/Assembly, Undo a značka průměru.
- `zima_cpp_measurement_inspector_ui_contract`: skutečný společný picker,
  informace o první a druhé entitě, prostřední tlačítka nad View, Uložit,
  rozbalení a výběr uloženého záznamu ve stromu, opětovné otevření a oprava
  chybějící reference. Dvě komponenty s rozdílnou hustotou ověřují oba souhrny
  a analytickou vzdálenost. Přechod z uzavřených vlastností výkresové kóty
  zpět do Partu ověřuje uvolnění dialogu a opětovné spuštění Měření bez pádu.
- `zima_cpp_assembly_refresh_ui_contract`: výběr komponenty, dvojklik,
  dostupnost kóty a její vlastní editor.


## Čtecí CLI příkazy (2026-09-13)

- `measurement.list [offset] [limit] [document]` vypíše uložené informační
  záznamy. Výchozí limit je 2000, povolený 1–10000; offset je nezáporný.
- `measurement.get object [document]` vrátí původní reference a poslední
  uložené hodnoty, včetně bodů nejkratší vzdálenosti. `saved_values: true`
  výslovně označuje uložený snímek. Dotaz nic neaktualizuje ani neukládá.
- `measurement.evaluate references [document]` nově vyhodnotí jednu nebo
  dvě reference proti aktuálním vypočteným datům. Nevyžaduje otevřené GUI,
  nevytváří historii, neukládá soubor a nevolá OCCT ani řešení vazeb.

```json
{"command":"measurement.evaluate","arguments":{"references":[{"kind":"plane","owner":"<part-id>:origin","key":"origin:plane:xy"},{"kind":"face","owner":"<original-feature-id>","key":"<original-face-key>"}]}}
```

Typ reference je `point`, `curve`, `face`, `object`, `axis` nebo `plane`.
Topologické reference potřebují původní `owner/key`; souřadnice či náhradní
geometrie se nepřijímají. Celý objekt používá `kind: object`, jeho `owner`
a prázdný klíč. Celá komponenta používá prázdného vlastníka a přesný
`instance_path`. Vnořené reference rozlišuje celá kódovaná cesta výskytu,
nikoli jméno nebo společné ID zdrojového Partu. Typ musí odpovídat geometrii:
například zakřivenou plochu nelze vydávat za nekonečnou rovinu.

Při chybějící geometrii vrátí vyhodnocení `missing_reference` a nulou
číslovaný `reference_index`. Poslední uložený záznam zůstane čitelný.
Dotazy lze směrovat na jiný otevřený dokument bez jeho aktivace a lze je
používat i během otevřeného dialogu. Měření výkresu je samostatná oblast
kót; tyto příkazy pracují s Partem nebo sestavou.

Strojový výstup používá vždy **mm, mm², mm³ a kg**, nezávisle na jednotkách
formátovaných GUI. Objekt `units` je popisuje pro jednotlivé hodnoty.
Každá délka, obsah, objem, hmotnost či vzdálenost obsahuje `value` a
`approximate`. Chybějící hodnota je `null`, nikoli nula. Přesná uložená
délka kruhové hrany zůstává přesná i při hrubém zobrazení; vzdálenosti
k zakřivené síti závisejí na jejím rozlišení.

Společný modul `zima_measurement` závisí pouze na `zima_kernel_api`, nikoli
na Qt. Obsahuje původní geometrický algoritmus inspectoru, který sdílejí
GUI i CLI. Viewer převádí jen kandidáta výběru na měřicí referenci.
Workspace doplňuje dostupné autoritativní objemy, plochy a hmotnosti;
GUI zachovává měření celého výskytu také při zobrazení řezu.

První modelová sada prošla **2/2 za 0,35 s**: analytická geometrie,
nejkratší vzdálenost, nezávislé objemy a jednotky, přesná kruhová délka,
neplatné vstupy, nezměněná historie/cache, opakované vnořené výskyty,
čtení neaktivního dokumentu a nativní uložení Partu i sestavy.
Konečné integrační výsledky jsou uvedeny níže.
Tato etapa zavádí tři čtecí příkazy; tvorba, změna a odstranění uložených
záznamů přes CLI následují samostatnou transakcí sdílenou s GUI.


Sestavily se obě aplikace a všechny testovací programy. Integrační sada
prošla **5/6 za 87,48 s**; jediná chyba byla stará očekávaná velikost
katalogu 226 místo 229. Po její aktualizaci prošly katalog a úplný start
aplikace včetně překladů **2/2 za 96,19 s**. Dodatečná kontrola platné osy
a rozdílu mezi přesnou kruhovou délkou a aproximovanou vzdáleností prošla
**1/1 za 0,25 s**. Kontrola kruhového okraje používá osovou rovinu válce,
aby nezávisela na pořadí jeho horního a dolního okraje.

GUI regrese porovnává skutečné hodnoty a oba body nejkratší vzdálenosti
s dotazem konzole, také během otevřeného inspectoru. Čte i měření uložené
GUI. Samostatný CLI proces běží s úmyslně neplatným názvem Qt platformy;
nové příkazy tedy nevyžadují inicializaci okna. Byla ověřena i zachovaná
přesná kruhová délka a odmítnutí zakřivené plochy jako roviny či kružnice
jako přímé osy. Katalog má **229 příkazů**, celá sada **128 testů**.

Logy: `build/measurement-query-integration-build.log`,
`build/measurement-query-integration-tests.log`,
`build/measurement-query-catalog-startup-tests.log`,
`build/measurement-query-precision-tests.log`.


## Ukládání a úpravy přes CLI (2026-09-13)

- `measurement.create` přijímá `references` a volitelný `name`. Vytvoří
  stabilní záznam; v Partu jej ukotví k aktuálnímu tělesu a místu historie.
- `measurement.set` přijímá `object`, volitelný `name` a `references`.
  Vynechané reference zůstanou zachované. Příkaz s pouhým `object`
  výslovně obnoví poslední uložené hodnoty podle současné vypočtené geometrie.
- `measurement.delete object` odstraní právě jeden uložený záznam.

```json
{"command":"measurement.create","arguments":{"name":"Kontrolní vzdálenost","references":[{"kind":"plane","owner":"<part-id>:origin","key":"origin:plane:xy"},{"kind":"face","owner":"<original-feature-id>","key":"<original-face-key>"}]}}
```

Všechny tři mutace používají stejnou transakci jako Uložit nebo Odstranit
v GUI. Vstupem nejsou vypočtené hodnoty, náhradní souřadnice ani místo
historie. Sdílená operace vyhodnotí původní reference v soukromém návrhu;
teprve úplný platný výsledek potvrdí jedním krokem dokumentové historie.
Zachovává původní identitu a ukotvení. Název ořízne o vnější bílé znaky,
ověří jej a odmítne duplicitu. Ztráta reference či jiná chyba nepřepíše
poslední uložené výsledky. Nezměněné uložení nepřidává Undo krok.

Příkazy pracují se zobrazeným aktivním Partem nebo sestavou. Při aktivaci
vnořeného dílu musí uživatel nejprve aktivaci ukončit nebo otevřít zdroj
samostatně. Tím zůstává jednoznačný vlastník a souřadný systém měření;
inspekce celého zobrazeného modelu zůstává možná. Otevřený inspector je
chráněn revizí, generací dat a runtime identitou dokumentu. Změna a následné
Undo nebo zavření a opětovné otevření dokumentu nesmějí obnovit platnost
starého editovacího návrhu.

Body, vazby ani umístění se při těchto mutacích nepřepočítávají. Ukládají
se pouze existující nativní záznamy měření; přípony, formát a start šablony
se nemění. Part i Assembly podporují Undo/Redo a nativní uložení/reopen.

Modelová sada po opravě dvou názvů testovacích vstupů prošla **3/3 za
0,67 s**. Ověřuje vznik a změnu hodnot, smazání, no-op, atomické chyby,
Undo/Redo, stárnutí návrhu při Undo a reopen, identitu a historii,
oddělené výskyty, zachování B-Rep a sdílení zdrojových dat sestavy.
Konečné integrační výsledky jsou uvedeny níže.


Úplné sestavení obou aplikací a testovacích programů prošlo. Integrační
sada prošla **8/8 za 223,07 s**: skutečný CLI proces, celý inspector
měření, všechny příkazové testy, katalog, GUI konzole a start s překlady.
GUI test porovnává změnu CLI → Vlastnosti → Uložit, nezměněné uložení,
Delete/Undo a dostupnou inspekci bez možnosti zápisu do pasivní sestavy.

Dodatečná regrese pro celé skici/solidy/roviny nejprve selhala
(**0/1 za 0,18 s**): picker přenášel do měřicí reference pomocnou kategorii
zobrazení. Celý objekt má identitu vlastníka a výskytu; klíč podentity je
prázdný. Po normalizaci na hranici pickeru regrese prošla **1/1 za 0,17 s**.
Po novém sestavení obou aplikací prošla závěrečná měřicí sada **4/4 za
3,52 s**, včetně GUI a uložení měření skutečné samostatné skici. Její
vzdálenost se počítá z konečné geometrie a nevzniká fiktivní objem.

Katalog má **232 příkazů**, celá sada **129 testů**. Logy:
`build/measurement-edit-integration-build.log`,
`build/measurement-edit-integration-tests.log`,
`build/measurement-object-identity-baseline-tests.log`,
`build/measurement-object-identity-tests.log`,
`build/measurement-edit-final-build.log`, `build/measurement-edit-final-tests.log`.
