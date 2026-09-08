# Zrcadlo a Pole

Oba příkazy vytvářejí odkazované kopie s vlastním počátkem a standardním
umístěním kontejneru. Zdroj lze vybrat před spuštěním příkazu nebo následně
zeleným polem Zdroj ve View či Tree.

- **Zrcadlo** používá rovinu nebo rovinnou plochu. Tlačítka XY/YZ/XZ vyberou
  rovinu vlastního počátku kontejneru.
- **Pole – lineární** používá jednu až tři různé osy vlastního počátku.
  Kliknutí na referenční pole směru nabídne pod kurzorem osy X/Y/Z kontejneru
  Pole. Každý směr má kladnou rozteč a vlastní počet. Dva směry vytvoří mřížku,
  tři směry prostorové pole. Křížek u reference odstraní příslušný směr.
- Rozložení každého směru může být **Vpřed**, **Vzad**, **Oboustranně** nebo
  **Symetricky**. Počet zahrnuje zdroj; u oboustranného rozložení pole **Vzad**
  přidává nezávislý počet kopií za zdroj. Symetrický počet je lichý (3, 5, 7…)
  a zdroj zůstává uprostřed. Například 3 × 2 × 2 znamená 12 výskytů celkem,
  tedy zdroj a 11 kopií. Celkový limit je 1000 výskytů.
- **Pole – kruhové** používá zvolenou osu, přímou hranu nebo kruhovou hranu.
  Lze rozdělit celý kruh nebo zadat úhel mezi výskyty. Osa a její reference se
  zachovají při přepnutí na lineární režim, ve kterém je ovládání kruhové osy skryté.
  Nastavení lineárních směrů se rovněž zachová při přepnutí do kruhového režimu.
- Počet Pole zahrnuje původní zdroj. Kontejner přidává zbývající kopie;
  původní objekt zůstává samostatný.

V dílu je Zrcadlo i celé Pole samostatný výsledek typu těleso. Lze jej vybrat,
skrýt a použít jako nástroj nebo cíl operace Boolean. Pole představuje jeden
společný výsledek zahrnující jeho kopie. Geometrie kopií nemá vlastní
editovatelnou historii; rozměry se mění ve zdroji.

Při spuštění z aktivního tělesa se Zrcadlo nebo Pole vkládá bezprostředně
za toto těleso. Zdroj lze vybrat jen z výsledků před touto hranicí; pozdější
tělesa zůstávají ve View potlačená a v Tree šedá již od otevření Vlastností,
i před vyplněním zdroje. Cancel obnoví původní stav historie.
V nabídce aktivního tělesa jsou Zrcadlo a Pole pod Vrtací špičkou, před
zeleným oddělovačem a příkazem Kvádr.

V sestavě odkazuje kontejner na bezprostředně vlastněnou komponentu. Pole
ukládá pro každou kopii vlastní cestu výskytu, včetně kopií podsestav.
U lineárního pole je identita odvozena od celočíselné pozice na místních
osách; zvýšení počtu v jiném směru nemění identitu existující kopie.
Vlastnosti geometrie a aktivace kopie vedou na původní zdroj. Vlastnosti
kontejneru v Tree umožňují změnit jeho umístění, zdroj a rovinu/parametry Pole.
Při editaci zůstává okolní sestava pasivním kontextem a zobrazuje se vstupní
geometrie. Storno zachová původní dokument; OK vypočítá a uloží změnu.

Změny otevřených zdrojových dokumentů se do sestavy přenášejí příkazem
Regenerovat. Přepnutí záložky nebo obnovení View geometrické kopie nepočítá.
Cyklus přes zdroj, umístění nebo konstrukční reference je odmítnut.

Výpočet B-Rep provádí OCCT jen při explicitním výpočtu. Náhled, výběr,
reference a vlastnosti využívají uložené ZIMA geometrické údaje. Identity
odvozené topologie ukládají zdrojového vlastníka a jeho sémantický klíč;
pořadí průchodu OCCT nikdy neurčuje identitu kopie.

Ověření: `zima_cpp_derived_copy_contract_tests`; GUI scénář
`ZIMA_VERIFY_DERIVED_COPY_ONLY=1` s testem `zima_cpp_workspace_startup_contract`.
