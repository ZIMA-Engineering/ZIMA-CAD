# Reference a vazby sestavy

Tento dokument vymezuje, jak ZIMA-CAD vybírá a ukládá geometrii pro sestavové
vazby. Vazba patří původní geometrii komponenty, nikoliv dočasné výsledné
ploše celé sestavy.

## Zdrojová a cílová reference

Při editaci vložené komponenty se zadává dvojice:

1. **reference dílu** patří původním tělesům právě umisťované komponenty;
2. **reference sestavy** patří jinému přímému výskytu komponenty nebo
   počátku či pomocné geometrii bezprostředně vlastnící sestavy.

Opakované výskyty stejného dílu se rozlišují cestou instance. Komponenta se
nesmí navázat sama na sebe. Nadřazená sestava nesmí přímo umisťovat vnitřní
komponentu podsestavy; nejprve se aktivuje tato podsestava.

## Počátky, druh vazby a stupně volnosti

Vestavěný počátek dokumentu dílu vychází z uložené identity zdrojového
Partu a polohy konkrétního výskytu. Nepotřebuje cache výsledného solidu ani
otevřený zdrojový soubor. Reference počátků těles a kontejnerů se předávají
v referenční geometrii komponenty při vložení a explicitní regeneraci.

Do referencí lze ve stromu i ve View vybrat bod, osu nebo rovinu počátku dílu,
tělesa či kontejneru, případně počátku vlastnící sestavy. Výběr celého počátku
vlastnící sestavy ve stromu automaticky spáruje oba počátky třemi vazbami
**XY–XY, YZ–YZ a XZ–XZ**, s nulovým odsazením. Stačí mít otevřené
**Vlastnosti dílu** nebo **Vlastnosti sestavy**; není nutné nejdříve vybrat
zdrojový počátek ani aktivovat cílové pole. Zadané řádky se nahradí najednou
jako rozpracovaná změna, kterou uloží až OK. V aktivované podsestavě se vybírá
její vlastní počátek; počátek vyšší sestavy tento výběr nenahradí.

Výběr jiného celého počátku vyplní právě zvolenou stranu tří řádků jeho bodem
a osami X/Y. Uloží se přesný vybraný vlastník a výskyt; počátek tělesa se
nenahrazuje počátkem dokumentu. Jednotlivé roviny, osy a body lze zadat ručně.

Typ vazby se odvodí z geometrie: bod–bod, osa–osa nebo plocha–plocha. U ploch
lze zvolit také úhlovou vazbu. Osy nabízejí pouze souosost; Úhel os byl
odstraněn. Nabídka neumožní kombinaci typu vazby s neodpovídající geometrií. Změna druhu reference vyprázdní neslučitelnou druhou
stranu. Dialog také sjednotí nesprávný typ rozpracovaného řádku při otevření;
změna se uloží až potvrzením **OK**.

Vlastnosti zobrazují zbývající stupně volnosti a vypočtené souřadnice
X/Y/Z a RX/RY/RZ. Souřadnice, které platné vazby určují, nejsou editovatelné.
Počet volností vychází z nezávislosti geometrických rovnic, nikoliv z počtu
vyplněných řádků. Souosé osy ponechají posuv a rotaci podél osy; přidaná čelní
rovina ponechá pouze rotaci. Shodné celé počátky odeberou všech šest volností.
U šikmého směru může jediný volný pohyb měnit více souřadnic současně.
Počet se počítá ze skutečných posuvů a rotací v prostoru. Singularita zápisu
Eulerových úhlů u RY = 90° nevytváří další volnost. U plně zavazbeného dílu
zůstanou všechna pole polohy a natočení zamknutá i v této orientaci.

Všechny platné řádky jedné komponenty se řeší společně. Úhel ploch využije
pohyb, který dovolují ostatní vazby: u souosého a čelně ustaveného dílu
otáčí právě kolem společné osy a zachová dosednutí. Výsledek se přijme pouze
při splnění všech rovnic. Pořadí řádků neopravňuje pozdější vazbu porušit
předchozí vazbu. **Obrátit** u souososti a shody ploch přepíná mezi shodným
a opačným směrem normál/os. Při výběru nové dvojice se bližší orientace uloží
do přepínače; následný výpočet ji už nemění podle aktuální polohy. Opakované
zapnutí a vypnutí proto skutečně vrací směr zpět, také po uložení dokumentu.
Flip úhlové vazby požaduje doplňkový úhel.

Nesplnitelná kombinace vyvolá v dialogu **Konflikt vazeb**. Náhled ponechá
poslední platnou polohu, **OK** neuloží chybný výsledek a **Zrušit** zahodí
rozpracované změny. Totéž platí pro neplatný krok při tažení hodnotové vazby.


Náhled používá uloženou referenční geometrii bez OCCT a zachová okolní
komponenty i při editaci v podsestavě. **OK** uloží vyřešenou polohu a vazby;
**Zrušit** obnoví původní stav komponenty.

## Přesouvání za počátek

Označený přímý díl nebo podsestava zobrazí v počátku zdrojového dokumentu
fialový bod stejné velikosti jako manipulátory v Partu (poloměr 6 obrazových
bodů). Úchyt používá společný výběr ve View a funguje i bez otevřených
vlastností. Při výběru jiného objektu nebo zrušení výběru zmizí.

Tažení promítá pohyb kurzoru do zbývajících **posuvných** volností ze stejných
geometrických rovnic jako výpočet vazeb. Volný díl se přesouvá v rovině
pohledu; souosost dovolí posuv podél osy, rovinná vazba pohyb v rovině.
Uzemněný nebo plně zavazbený díl se nepohne. Samotná rotační volnost
neopravňuje úchyt počátku měnit polohu ani úhly; pro rotaci slouží úhlové
hodnoty a jejich ovladače. To platí i pro šikmé směry a natočené podsestavy.

Bez vlastností uvolnění myši uloží jeden přesun, který vrátí jediné **Zpět**.
**Escape** během tažení obnoví polohu před začátkem gesta. Jsou-li otevřené
vlastnosti, úchyt respektuje jejich rozpracované vazby a přesun zůstane
náhledem až do **OK**; **Zrušit** vrátí původní polohu i reference. Podsestava
vždy vlastní umístění svých přímých komponent; pro přesun vnitřního dílu
se nejprve aktivuje tato podsestava. Okolní sestava zůstává viditelná.

## Co se ve view skutečně vybírá

Sestava může materiál komponent odečítat a její zobrazený výsledek se proto
může lišit od zdrojového dílu. Picker vazeb přesto testuje persistované meshe
původních zdrojových těles jednotlivých komponent. Neprochází výsledný OCCT
compound a nevytváří reference z ploch vzniklých až sestavovým řezem.

Z toho plyne:

- sražení, zaoblení ani sestavový odečet nesmějí přesměrovat výběr na jinou
  výslednou plochu;
- stejná původní plocha zůstává referencí i tehdy, když je ve výsledku sestavy
  částečně odříznutá;
- finální plocha sestavy bez původního vlastníka se při zadávání vazby odmítne;
- oranžový hover a azurový výběr odpovídají stejné kanonické referenci, která
  se uloží do dokumentu.

## Persistovaný tvar reference

Plocha se ukládá jako `AssemblyFaceRef`, kruhová hrana jako
`AssemblyEdgeRef`. Descriptor obsahuje identitu konkrétní instance a stabilní
zdrojovou `FaceRef` nebo `EdgeRef`. Do `.asmz` se neukládá pořadové číslo
plochy či hrany ve výsledném meshi.

Analytická data kruhové hrany (`origin`, `direction`, `radius`) a válcové
plochy (`origin`, `axis`, `radius`) jsou součástí persistovaných viewer dat.
Stejný mechanismus proto může v Partu i v aktivované instanci sestavy vytvořit
kontejner Osa ve středu válce bez živého procházení OCCT topologie. Volitelná
následující rovinná reference určí počátek této osy jako průsečík roviny se
středovou přímkou.

Při otevření vlastností nebo zvýraznění se používají již uložená data. OCCT
smí být vyvolán pouze explicitním výpočtem tělesa, například přes
**OK** nebo regeneraci modelu; hover, výběr a otevření dialogu nesmějí skrytě
přepočítávat topologii.

## Chybějící zdrojová data

Nově regenerovaný Part ukládá do `BodyResult.source_bodies` původní tělesa a
sémantické mapování jejich ploch, hran a vrcholů. Starší nebo neúplná cache
může tato data postrádat. Aplikace v takovém případě nesmí odhadnout referenci
z výsledné plochy.

Náprava:

1. otevřete zdrojový Part;
2. spusťte **Regenerovat**;
3. vraťte se do sestavy a spusťte **Regenerovat**, aby převzala aktuální
   referenční data z otevřeného Partu;
4. otevřete vlastnosti komponenty. Zdrojový Part není nutné předem ukládat.

Jednokontejnerový importovaný Part může použít své finální importované těleso
jako původní zdroj, protože před ním neexistuje jiný parametrický výsledek.

## Stav po změně modelu

Reference má vždy výslovný stav `RESOLVED`, `MISSING` nebo `AMBIGUOUS`.
Chybějící či nejednoznačná vazba se zachová v dokumentu, ale neúčastní se
řešení. Nikdy se automaticky nenahradí podobnou plochou s jiným runtime
indexem. Cache obsahující pouze runtime identitu se považuje za zastaralou a
znovu se sestaví z persistovaných zdrojových dat.

Podrobnosti jsou v dokumentu
[Stable Topology Naming](STABLE_TOPOLOGY_NAMING.md).

## Potlačení a graf závislostí

Potlačení komponenty není synonymum pro skrytí. **Skrýt/Odkrýt** mění pouze
prezentaci instance; komponenta nadále poskytuje všechny své vazby a reference.
**Potlačit** vyřadí instanci z aktivního modelu sestavy.

Závislosti se odvozují přímo z persistovaných deskriptorů sestavových vazeb a
z externích referencí skic s rozsahem `assembly_component`. Hrana grafu vede
od závislé instance ke konkrétní instanci poskytující cílovou referenci. Graf
pracuje se stabilní identitou výskytu, nikoliv se jménem komponenty nebo pouze
s identitou sdíleného zdrojového Partu. Jeho sestavení neprochází OCCT geometrii
a nevyvolává výpočet tělesa.

Ručně potlačená instance spustí rekurzivní efektivní potlačení všech následníků.
Tento odvozený stav se nezapisuje jako ruční příznak na následníky. Po obnovení
zdroje se proto uvolní pouze následníci, kteří už nemají jiný potlačený zdroj;
ostatní ruční potlačení zůstávají zachována. Neexistující komponenta nebo
nevyřešená persistovaná reference se ze záznamu vazby neodstraňuje. Aktivní
komponenta s takovou chybou se zobrazí červeně a zůstává opravitelná.

## Ruční přesun komponenty

Fialový bod počátku ve vlastnostech komponenty slouží k interaktivnímu
přesunu. Návrh posunu se ještě před změnou souřadnic promítne do volných
translačních stupňů volnosti určených platnými vazbami:

- rovinná vazba odebere složku pohybu ve směru normály cílové roviny;
- souosá vazba odebere obě složky kolmé k cílové ose, takže dovolí pouze pohyb
  podél osy;
- několik vazeb vytvoří průnik povolených směrů; pokud nezůstane žádný
  translační stupeň volnosti, komponenta se tažením neposune;
- úhlová vazba sama o sobě translační pohyb neomezuje.

Během pohybu se nespouští sestavový solver. Přesouvá se komponenta a její
závislý řetězec, přičemž překreslení view je sloučeno do krátkých intervalů.
Po uvolnění tlačítka solver pouze ověří a uloží výsledný stav. Referenční rámce
zdrojové komponenty se během tažení posouvají společně s ní, aby následné
ověření neinterpretovalo povolený pohyb jako změnu lokální reference.


Číselná pole a rozměry ve View používají [společné zámky hodnot](NUMERIC_VALUE_LOCKS.md),
včetně jednorázového převzetí současné hodnoty při zadávání reference.

## Orientované úhly a otevření zdroje (2026-09-09)

Úhel plochy přijímá −180° až +180°. Znaménko se určuje v orientovaném rámci
cílových referencí: přednost má osa nebo rovina dalších vazeb komponenty,
jinak rámec cílového počátku. Flip obrací cílovou normálu; číselné zadání,
měření a tažení kóty používají stejnou orientaci. Nenulová úhlová kóta se
zobrazuje při ustavování i při opětovném otevření vlastností, také pro vazbu
k vlastnímu počátku sestavy. V 0° a ±180° jsou normály rovnoběžné a počet
rotačních stupňů volnosti tomu odpovídá.

Kontextové menu komponenty ve stromu i ve View nabízí **Otevřít**.
Zdrojový Part nebo podsestava se zobrazí na vlastní kartě; již otevřený
dokument se použije v jeho aktuálním stavu bez vytvoření druhé kopie.
**Aktivní** nadále slouží k editaci komponenty v kontextu horní sestavy.
Regenerace je dostupná nad View, nikoli duplicitně pod Rotací v pravém panelu.

Vlastní skica sestavového Vytažení a Rotace přežije návrat do vlastností,
opětovný vstup do Skicáře a uložení. Při editaci uložené operace je nad jejím
vstupním modelem viditelná aktivní skica. OK potvrdí celý kontejner; Zrušit
obnoví původní stav.


## Regenerace a zobrazení kót (2026-09-10)

**Regenerovat** obnoví zdroje v celé vnořené sestavě. Otevřený Part nebo
Assembly má přednost svým aktuálním stavem v paměti; zavřený zdroj se načte
z uloženého `.prtz`/`.asmz` relativně ke své vlastní sestavě. Obnova neotvírá
nové záložky. Identita dokumentu musí souhlasit s vloženou referencí a cyklus
nebo nedostupný zdroj výpočet odmítne před zápisem nového stavu rodiče.
Pouhé přepnutí záložky ani uložení dílu rodičovskou sestavu nepřepočítá.
Regenerace zachovává přiblížení, natočení a posunutí pohledu; přizpůsobení
celého modelu zůstává samostatným příkazem.

Přepínač **Kóty** v nabídce Zobrazení a nad View skrývá/zobrazuje modelové
kóty. Skryté kóty se nenabízejí k výběru. Tento přepínač nemění parametry
vazeb ani Show/Erase ve výkresech.

Úhlová kóta rovina–rovina používá střed na cílové ose existující souosé vazby,
pokud je tato osa rovnoběžná s osou úhlové kóty. Bez odpovídající souosé vazby
leží střed na průsečnici rovin poblíž vybraných referencí. Ramena oblouku
sledují směry rovin namísto jejich normál. Jde pouze o prezentaci; reference,
hodnota úhlu a řešení umístění komponent se nemění.


Po potvrzení komponenty ve View nebo ve stromu se nabídnou pouze její kóty
uložení v právě editované sestavě. Prázdné kliknutí, zrušení výběru nebo společné
ukončení prostředním tlačítkem je skryje. Přepínač Kóty tuto dočasnou viditelnost
pouze povoluje; nezobrazuje všechny vazby celé sestavy naráz. Stejný filtr platí
pro kreslení i kandidáty výběru a nemění uložené reference.


## Počátky během tvorby prvků (2026-09-11)

Dialogy prvků se společnou funkcí **Počátek** (například Vytažení, Rotace,
Zrcadlo a konstrukční prvky) při práci nad sestavou automaticky nenabízejí
počátky všech jejích komponent. Ve výchozím stavu zůstává hlavní počátek
zobrazené sestavy. Ostatní počátky lze vyžádat tlačítkem **Počátek** a
kliknutím na konkrétní díl či podsestavu ve View nebo ve stromu. Další
kliknutí její počátek skryje. Vnořenou podsestavu lze zvolit přímo ve stromu.

Viditelnost patří přesné cestě výskytu; nezapíná současně další kopie
stejného zdrojového dokumentu. Dosavadní volba lokálních počátků kontejnerů
v aktivním Partu používá stejnou funkci. Skrytý počátek není nabízen ani na
hover či při potvrzení: kreslení i společný seznam kandidátů používají
stejný filtr. Zavření dialogu ukončí dočasný režim. Pravidla umístění,
vlastnictví a ukládání referencí se tím nemění.


Ověření: všech 44 Windows CTest testů prošlo (425,67 s). Po doplnění
integračního scénáře do `verify_component_references` byl program znovu
sestaven a cílený CTest `zima_cpp_workspace_startup_contract` s
`ZIMA_VERIFY_COMPONENT_REFERENCE_ONLY=1` prošel (8,10 s). Scénář otevírá
Protrusion a Mirror nad vnořenou sestavou, vyžádá počátek podsestavy přes
skutečné tlačítko a strom a ověří izolaci výskytu i návrat po Cancel.
