# Vnější závit na hřídeli

Příkaz **Závit** je samostatná kosmetická operace historie Partu. **Otvor**
nadále vytváří díru a její případný vnitřní závit. Závit nemění objem ani
průměr materiálu hřídele: vytváří patní závitovou plochu a volitelný výběh.

Vstupy jsou původní válcová plocha hřídele, počáteční rovinná plocha,
volitelné kuželové vstupní sražení a pro Až k koncová rovina, souosý válec
nebo kuželové sražení na konci hřídele.
Vnitřní válcové stěny se nenabízejí. Reference patří přesnému aktivnímu
výskytu Partu a nesmějí odkazovat na pozdější operaci historie.

Rozlišení vnitřní a vnější stěny zohledňuje orientaci analytické soustavy
i stranu materiálu po booleovské operaci. Samotný obrácený směr vytažení
nepřevrací význam válce. Tyto údaje vznikají při výpočtu a výběr je pouze čte.

Katalog používá stejná data jako Otvor. Výběr položky jednorázově přenese
vnější patní průměr, jmenovitý rozměr, označení a stoupání. Ručně změněný
patní průměr se při otevření vlastností ani při změně jiných parametrů
nepřepisuje. Kóta ve View je vždy číselný průměr a dvojklik otevírá číselný
editor. V otevřených vlastnostech mění pouze jejich pracovní hodnoty.

Délka se měří od průsečíku osy s počáteční plochou ke konci válce závitu.
Sražení počátek měření neposouvá. Výběh pokračuje za touto délkou a omezuje
se dostupnou délkou válcové části. Režimy Až k a Skrz vše výběh nevytvářejí.
Skrz vše se vztahuje k vybrané válcové části. Až k válci použije jeho vzdálený
axiální konec; Až k sražení použije průsečík patního válce s konečnou
kuželovou plochou. Pokud mělké sražení leží celé vně patního válce, závit
dojde ke koncovému čelu hřídele. Koncové sražení musí navazovat na hřídel a zužovat se
ve směru závitu. Nepodporované nebo neprotínající se cíle se odmítají.
Přepnutí na Až k nebo Skrz vše výběh také odškrtne v dialogu.

Třetí řádek referencí je vždy dostupný pro vstupní sražení. Kliknutí do něj
zapne jeho použití a aktivuje výběr. Náhled obsahuje kružnice patní plochy,
kružnici na konci výběhu a jednu společnou spojnici.

Geometrický popis zdrojových rovin, válců a kuželů se ukládá do referenčního
paketu při explicitním výpočtu tělesa. Okno, výběr a drátový náhled čtou
uložená data bez OCCT. Pokud potřebný popis ploch chybí, uživatel nejprve
explicitně regeneruje Part. OK znovu vyřeší reference vůči vstupu operace.
Patní průměr je nezávislý na průměru hřídele: i při zmenšení hřídele zůstane
závit zachován a viditelný mimo materiál. Plochy se proto neořezávají objemem
hřídele; začátek a konec určují zvolené reference. Celý závit je jedna historie/Undo
operace. Vlastnosti používají uložený vstup před editovaným závitem;
Cancel vrátí původní historii. Sdílený kontrakt umístění kontejnerů se nemění.

Regresní ověření: `zima_cpp_shaft_thread_contract_tests`,
`zima_cpp_ui_contract_tests` a `ZIMA_VERIFY_SHAFT_THREAD_ONLY=1`
při spuštění aplikace s `--verify-startup`.

Při ztrátě reference používá běžný přepočet a zobrazení poslední úspěšně
vyhodnocený geometrický popis uložený u příslušné reference. Ten se obnovuje
při úspěšném výpočtu a ukládá spolu s parametry závitu do Partu. Nenahrazuje
identitu zdrojové plochy a nevstupuje mezi nabízené reference; chybějící zdroj
proto nadále vyvolává červené označení v tree. Zachová se i závit, jehož
zdrojový hřídel byl úplně odstraněn.

Otevření vlastností vyprázdní pouze chybějící reference v pracovní kopii.
Zadání pak používá stejný dialog jako nový závit. OK vyžaduje platné povinné
reference; Cancel zachová původní uložené reference i jejich náhradní údaje.

Zvýraznění při výběru a editaci se řídí společným kontraktem
[View a Tree](MODELING_INTERACTION.md). Příkaz má vlastní ikonu vnějšího závitu;
Otvor používá stejnou ikonu válcového otvoru v nabídce i ve svém podprvku Tree.
