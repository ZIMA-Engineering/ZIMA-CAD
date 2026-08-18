# Kóty, výrobní tolerance a omezený pohyb

## Účel

Jednotný model kóty podporuje parametrické řízení, omezený pohyb ve
Sketcheru a pohyb komponent sestavy. Výrobní tolerance a pohybové meze jsou
dvě rozdílné vlastnosti a nesmějí se vzájemně ovlivňovat.

Stejný princip platí pro lineární, úhlové, poloměrové a průměrové kóty.

## Datový model

Kóta obsahuje:

- `nominal_value` – editovatelnou jmenovitou hodnotu, která zároveň určuje
  aktuální polohu geometrie;
- `range_enabled` – zda jsou zapnuté pohybové meze;
- `lower_limit` – absolutní dolní mez ve stejné soustavě jako kóta;
- `upper_limit` – absolutní horní mez ve stejné soustavě jako kóta;
- samostatná metadata výrobní tolerance.

Meze nejsou odchylky od jmenovité hodnoty. Všechny tři číselné hodnoty
mají stejnou absolutní nulu a stejné jednotky.

Příklad:

```text
Jmenovitá hodnota: 100 mm
Dolní mez:          90 mm
Horní mez:         110 mm
Jmenovitá hodnota po posunu: 105 mm
```

Povoleno je i rozmezí, ve kterém jsou obě meze kladné nebo obě záporné,
například `20 .. 30 mm` nebo `-30 .. -20 mm`.

## Pravidla

Při zapnutém rozsahu musí platit:

```text
lower_limit <= nominal_value <= upper_limit
```

Neplatné nebo obrácené meze se nesmějí uložit. Změna jmenovité hodnoty
přímo změní polohu geometrie; absolutní meze se neposouvají.

Ve view se zobrazuje jedna kóta. Nevzniká druhá kóta pro rozsah, která by
například u pístu zobrazovala vedle vazby další matoucí nulu.

## GUI a editace

Vlastnosti kóty obsahují jmenovitou hodnotu, volbu omezení pohybu a dolní a
horní mez. Všechna tři číselná pole mají šipky, dovolují přepsat celou
hodnotu a používají počet desetinných míst dokumentu ze Settings. Samostatné
pole Aktuální hodnota se nezobrazuje. `OK` vše validuje, vypočítá model,
uloží a zavře. `Cancel` změny zahodí.

Při prvním zapnutí rozsahu se meze předvyplní mezi nulou a jmenovitou
hodnotou: pro `20` tedy `0 .. 20`, pro `-20` pak `-20 .. 0`.

Přímá editace kóty ani změna aktuálního posunutí ve Vlastnostech
komponenty nesmí překročit uložené meze. Číselný ovladač ve Vlastnostech
komponenty hodnotu na mezi zastaví; dialog Vlastnosti kóty odmítne neplatné
potvrzení.

## Sketcher solver

Kóta bez rozsahu přidá běžnou rovnici `measured_value = nominal_value`.
Odemknutá kóta s rozsahem ponechá daný stupeň volnosti pohyblivý, ale při
zadání i tažení omezí aktuální hodnotu absolutními mezemi. Po uvolnění
zůstane geometrie v poslední platné poloze.

## Sestavy

U sestavové vazby je `nominal_value` editovatelné posunutí nebo úhel použitý
solverem. Vazba je jediným vlastníkem pohybových dat. Sloupec **Aktuální
posunutí / úhel** ve Vlastnostech komponenty a **Jmenovitá hodnota** ve
Vlastnostech kóty editují tutéž hodnotu. Tabulka komponenty nezobrazuje
samostatný sloupec povoleného rozsahu; meze se nastavují ve Vlastnostech kóty.
`dimension_styles` uchovává pouze vzhled a výrobní toleranci.

## Výrobní tolerance

Výrobní tolerance zůstává samostatným údajem pro výkres a výrobu. Nevstupuje
do solveru, neomezuje tažení, nemění aktuální polohu a automaticky se
nepřevádí na pohybové meze ani opačně.

Jednotka tolerance vždy odpovídá typu kóty. Lineární, poloměrová a průměrová
kóta používá jednotku délky dokumentu; tolerance úhlové kóty je ve stupních
a ve view nese znak `°`.

## Persistenční pravidla

Dialog, viewer, Sketcher a sestava používají jeden datový model. Po načtení
dokumentu se aktuální poloha obnoví deterministicky. Staré relativní režimy
rozsahu nejsou podporovány; podle pravidel formátu se nepřidává migrační větev.
