# Reference a vazby sestavy

Tento dokument vymezuje, jak ZIMA-CAD vybírá a ukládá geometrii pro sestavové
vazby. Vazba patří původní geometrii komponenty, nikoliv dočasné výsledné
ploše celé sestavy.

## Zdrojová a cílová reference

Při editaci vložené komponenty se zadává dvojice:

1. **reference dílu** patří původním tělesům právě umisťované komponenty;
2. **reference sestavy** smí patřit pouze komponentě, která je ve stromu
   historie před právě umisťovanou komponentou, případně podporované pomocné
   geometrii sestavy.

Toto pořadí brání cyklickým závislostem. Celý kontejner ani výsledné těleso
sestavy nejsou náhradou za konkrétní plochu, hranu, osu nebo rovinu.

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
smí být vyvolán pouze explicitním výpočtem tělesa, například přes **Použít**,
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
3. Part uložte;
4. vraťte se do sestavy a znovu otevřete vlastnosti komponenty.

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
