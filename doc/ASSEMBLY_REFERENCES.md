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
ve stromu vyplní **právě zvolenou stranu** tří řádků jeho bodem a osami X/Y.
Druhý počátek se zadá na opačné straně. Uloží se přesný vybraný vlastník a
výskyt; počátek tělesa se nenahrazuje počátkem dokumentu.

Typ vazby se odvodí z geometrie: bod–bod, osa–osa nebo plocha–plocha. U os a
ploch lze zvolit také úhlovou vazbu. Nabídka neumožní kombinaci typu vazby s
neodpovídající geometrií. Změna druhu reference vyprázdní neslučitelnou druhou
stranu. Dialog také sjednotí nesprávný typ rozpracovaného řádku při otevření;
změna se uloží až potvrzením **OK**.

Vlastnosti zobrazují zbývající stupně volnosti a vypočtené souřadnice
X/Y/Z a RX/RY/RZ. Souřadnice, které platné vazby určují, nejsou editovatelné.
Počet volností vychází z nezávislosti geometrických rovnic, nikoliv z počtu
vyplněných řádků. Souosé osy ponechají posuv a rotaci podél osy; přidaná čelní
rovina ponechá pouze rotaci. Shodné celé počátky odeberou všech šest volností.
U šikmého směru může jediný volný pohyb měnit více souřadnic současně.

Náhled používá uloženou referenční geometrii bez OCCT a zachová okolní
komponenty i při editaci v podsestavě. **OK** uloží vyřešenou polohu a vazby;
**Zrušit** obnoví původní stav komponenty.

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
