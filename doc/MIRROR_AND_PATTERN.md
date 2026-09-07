# Zrcadlo a Pole

Oba příkazy vytvářejí odkazované kopie s vlastním počátkem a standardním
umístěním kontejneru. Zdroj lze vybrat před spuštěním příkazu nebo následně
zeleným polem Zdroj ve View či Tree.

- **Zrcadlo** používá rovinu nebo rovinnou plochu. Tlačítka XY/YZ/XZ vyberou
  rovinu vlastního počátku kontejneru.
- **Pole – lineární** používá místní směr X/Y/Z a podepsanou rozteč.
- **Pole – kruhové** používá zvolenou osu, přímou hranu nebo kruhovou hranu.
  Lze rozdělit celý kruh nebo zadat úhel mezi výskyty. Osa a její reference se
  zachovají při přepnutí na lineární režim, ve kterém je ovládání osy skryté.
- Počet Pole zahrnuje původní zdroj. Kontejner přidává zbývající kopie;
  původní objekt zůstává samostatný.

V dílu je Zrcadlo i celé Pole samostatný výsledek typu těleso. Lze jej vybrat,
skrýt a použít jako nástroj nebo cíl operace Boolean. Pole představuje jeden
společný výsledek zahrnující jeho kopie. Geometrie kopií nemá vlastní
editovatelnou historii; rozměry se mění ve zdroji.

V sestavě odkazuje kontejner na bezprostředně vlastněnou komponentu. Pole
ukládá pro každou kopii vlastní cestu výskytu, včetně kopií podsestav.
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
