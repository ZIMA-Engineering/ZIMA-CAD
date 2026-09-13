# Souhrny referencí při historii a regeneraci Assembly

Part ukládá skutečnou externí referenci, Assembly její odvozenou závislost
mezi bezprostředními větvemi. Vrácení nezávislé změny sestavy nesmí obnovit
starý souhrn, který už neodpovídá současným Partům.

## Transakce historie

`step_assembly_document_history` přesune historii pouze v soukromém kandidátu.
Společná příprava zkontroluje kandidátní hierarchii a dotčené otevřené kontexty,
srovná souhrny se zdrojovými Party a připraví potřebné stavy vlastníků.
Publikace je bez další alokace. Při chybě zůstane živý dokument, jeho revize,
historie i ostatní dokumenty beze změny. Kandidátní historie se do výsledku
přesune jednou; zdrojové Party se nepřepočítávají ani nekopírují do sestavy.

Stejný průchod uloženou cestou výskytu umí při přípravě použít kandidátní
zdrojovou Assembly. To umožňuje vrátit vnořenou větev, která už v dosavadním
zobrazeném stromu není. Jde o čtení identit; nedochází k výpočtu umístění,
vazeb nebo těles.

Pokud zdroj není dostupný, zachovají se dosavadní i historické souhrnné hrany,
jejichž oba konce v kandidátu existují. Jejich případný cyklus odmítne celý
krok. Po zpřístupnění zdroje lze tentýž krok opakovat. Již známé závislosti
si při obnově zachovají uloženou identitu.

Při změně směru reference se nejprve odstraní všechny prokazatelně zastaralé
hrany dotčeného vlastníka. Teprve pak se přidají potřebné nové hrany. Stará
opačná závislost tak nevyvolá nepravdivé odmítnutí cyklu.

## Výslovná regenerace

`regenerate_assembly` nejprve srovná souhrny v požadované hierarchii, včetně
zavřených nativních podsestav. Otevřené dokumenty mají přednost před soubory.
Zavřené zdrojové Party se pro kontrolu čtou soukromě; nevznikají jejich taby.
V paměti průchodu zůstávají malé množiny referencí podle identity Partu,
nikoli načtená vypočtená tělesa. Nativní loader zatím přečte celý Part.

Změněný zavřený vlastník se zveřejní jako otevřená neuložená Assembly,
aby šlo opravené údaje uložit do jejího `.asmz`. Změna souhrnu nevytváří
samostatnou položku Undo. Pokud se nic nezměnilo, další dokument se neotevře.
Zdrojové Party a celou opravenou sestavu je třeba obvyklým způsobem uložit.

Atomická je příprava a publikace souhrnů. Následná dosavadní regenerace modelu
nadále probíhá po dokumentech; tato etapa nezavádí jednu globální transakci
celého výpočtu. Přepnutí tabu výpočet nevyvolává. Nezměnily se nativní formáty,
šablony ani řešení vazeb; nevznikají externí povinné cache nebo sidecary.

## Ověření

Původní regrese selhala: Assembly Undo odstranilo souhrn reference, která
v Partu stále existovala (`build/assembly-reference-summary-baseline-tests.log`).
Po změně prošly oba modelové testy **2/2 za 0,69 s**
(`build/assembly-reference-summary-identity-tests.log`). Zahrnují:

- vytvoření i odpojení reference mezi dvěma nezávislými kroky historie sestavy,
- poslední neuložená data Partu a zachování jeho vypočtené alokace,
- úpravu Partu při zavřeném kontextu, nové načtení Top a opravu nativního vlastníka,
- novou opačnou referenci po odpojení původní při zavřeném kontextu,
- neověřitelné zdroje, atomické odmítnutí cyklu a pozdější úspěšné opakování,
- Undo obnovující vnořeného vlastníka mimo dosavadní zobrazený strom.

Procesová regrese a GUI kontrola jsou rozšířené o nezávislé Assembly Undo.
GUI navíc kontroluje chybu čtení vlastního testovacího zdroje: odmítnutý krok
se musí ohlásit a zachovat historii pro opakování po opravě souboru.
Úplné sestavení obou aplikací a všech testovacích programů prošlo
(`build/assembly-reference-summary-integration-build.log`). Procesové CLI
s oběma modelovými testy prošlo **3/3 za 21,22 s**; samostatná GUI regrese
prošla **1/1 za 18,57 s**. Následně prošla **celá sada 119/119 za 528,05 s**
(`build/assembly-reference-summary-full-tests.log`), včetně stejné GUI regrese,
konzole, načtení aplikace, výkresů, překladů, skicáře a nativních formátů.

Katalog má stále 209 příkazů. Tato etapa doplňuje jejich společnou datovou
transakci; neznačí dokončení ostatních oblastí CLI. Další je společné mazání
kořenových konstrukcí podle [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
