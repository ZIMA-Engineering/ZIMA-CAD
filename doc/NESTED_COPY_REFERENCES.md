# Původní reference vnořeného Zrcadla a Pole

Nezávislý test odhalil nesoulad analytických ploch vnořených kopií.
Zdrojová rovina souhlasila s vrcholy své triangulace na přibližně
4,7 × 10⁻¹⁴ mm, ale po zrcadlení činila odchylka až **51,5766 mm**.
U Pole se navíc odmítala uložená cesta přes virtuální výskyt.

## Oprava souřadných rámců

Vzorky v sestavovém paketu již obsahují vnořená umístění, zatímco
analytická plocha zůstává v souřadnicích svého původního výskytu.
Výpočet Zrcadla/Pole transformuje celý paket jedním převodem.
Před tímto výslovným výpočtem se proto pouze analytické údaje převedou
do rámce paketu; vypočítaná kopie je vrátí do rámce své přesné uložené
hierarchie. Používají se existující transformační funkce.

Vzorky, póly spline, topologické identity, řešení vazeb a hodnoty umístění
se tímto dodatečným převodem nemění. Analytická data jednotlivé plochy
jsou sdílená mezi jejími trojúhelníky. Zobrazený a původní paket mají
samostatné sdílení, aby se nesmíchaly jejich údaje o straně materiálu.
Chybějící přesná zdrojová cesta se odmítne.

Společné čtení uložených cest nyní přijímá i uzly Pole. Jejich virtuální
výskyty vlastní obklopující skutečná Assembly. Vnitřní Part vložené
podsestavy nadále vlastní tato zdrojová podsestava. Aktivace odvozeného
výskytu přechází na jeho přesný původní zdroj a ponechá hlavní sestavu
zobrazenou. Dotazy ani projekce nespouštějí výpočet kopie.

Přípony, struktura nativních souborů a šablony se nemění. Dříve vypočtené
kopie získají opravené analytické údaje při příštím výslovném přepočtu;
čtení a přepnutí karty je automaticky nepřepočítávají. Katalog zůstává
na **209 příkazech**.

## Ověření

Původní reprodukce selhala **0/1** (0,15 s),
`build/nested-copy-reference-baseline-tests.log`. Po opravě prošla první
sada **3/3** (1,17 s), `build/nested-copy-reference-tests.log`; odchylka
zrcadla i pole klesla přibližně na 2,5 × 10⁻¹⁴ mm.

Rozšířená regrese prošla **1/1** (1,07 s),
`build/nested-copy-reference-expanded-tests.log`. Nezávisle ověřuje
rovnice roviny a válce ve vrcholech jejich triangulace, jednotkovou osu,
objem 10 × 12 × 14 + π × 3² × 18 mm³, meze zrcadla, dvojitého zrcadla
a lineárního pole. Obsahuje dvě tělesa, složená prostorová natočení,
všechny tři základní roviny zrcadlení, kruhové i lineární pole,
zrcadlo pole a pole dalšího pole. Stejné kontroly probíhají po převedení
referencí do jiného Partu a po nativním uložení a opětovném načtení.

Po doplnění vlastnictví a aktivace virtuálních uzlů, pěti překladů a
sestavení všech programů prošla širší sada **21/21** (142,57 s),
`build/nested-copy-reference-integration-tests.log`. Zahrnuje celé GUI,
vlastnosti a tvorbu kopií, původní reference, kontextový refresh,
Workspace, native/cache testy, zdrojové dokumenty, skutečný CLI proces
a překlady. Sestavení: `build/nested-copy-reference-all-build.log`.
