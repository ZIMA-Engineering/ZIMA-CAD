# Hmotnost, jednotky a Parameters

## Výpočet

Základní geometrická jednotka výpočtu tělesa je milimetr. Uložený objem
je v mm³ a plocha v mm². Nastavení délkové jednotky dokumentu nemění
velikost geometrie; při zpřístupnění `model.volume` a `model.area` se tyto
veličiny převedou na třetí, respektive druhou mocninu délkové jednotky dokumentu.

Hmotnost dílu je **objem × hustota materiálu**. Hodnota `MASS_DENSITY`
a její vlastní jednotka patří do materiálových dat dokumentu. Podporované
jednotky hustoty jsou `kg/mm^3`, `kg/m^3`, `g/cm^3` a `lb/in^3`.
Samotné číslo hustoty bez známé jednotky se nesmí považovat za ověřený údaj.

`model.mass` používá jednotku **File Settings → Mass** (`kg`, `g`, `t`, `lb`).
`material.density` používá stejnou hmotnostní jednotku na třetí mocninu
zvolené délkové jednotky. Například ocelová krychle 100 × 100 × 100 mm při
hustotě 7850 kg/m³ má objem 1 000 000 mm³ a hmotnost 7,85 kg = 7850 g.
Změna jednotky nesmí změnit fyzickou hmotnost.

Nový dokument přebírá jednotky z platného `config.ini`, včetně místního
nastavení pracovního adresáře. Existující dokument používá vlastní uložené
jednotky. Přesnost textového výsledku určuje `decimal_places` v nastavení souboru.

## Vztahy a historie

Výchozí šablony mají vztah `mass = model.mass`. Fyzikální vztahy a jejich
navazující vztahy se aktualizují v transakci zdrojového dokumentu při změně
materiálu, jednotek nebo uloženého výsledku výpočtu. Hodnota se zapisuje do
Parameters jako sdílená hodnota pro všechny jazyky. Undo/Redo vrací současně
modelová data, jednotky a vypočtené parametry.

Výpočet těchto veličin čte uložený objem a plochu. Nevolá OCCT z dialogu,
hoveru, kreslení ani přepínání karet. Nemění rozměry řízené jinými vztahy.
Výkres pouze načítá výsledný parametr; hmotnost řízenou vztahem nelze
přepsat ručně přes razítko.

## Sestavy

Sestava sčítá fyzickou hmotnost všech nepotlačených výskytů komponent.
Opakovaná vložení se počítají opakovaně. Pouhé skrytí komponenty její
hmotnost neodstraní. Potlačení komponenty ji z výpočtu vyřadí.
Každý vnořený součet se převádí přes kilogramy do jednotek vlastníka.

Hustota dílu a hmotnost vložené podsestavy se uchovávají jako součást
snímku vložené komponenty. Změna otevřeného zdroje sama nepřepočítá rodiče;
nový snímek převezme rodič až při výslovném **Regenerate**. U dříve uložených
sestav bez těchto údajů je potřeba otevřít zdroje a provést Regenerate sestavy.

Chybějící či neplatná hustota se nezaměňuje za nulu: závislá hmotnost je
nedostupná a razítko zobrazí zástupný údaj. Obdobně nelze z podílu objemů
odhadovat přesnou hmotnost po výřezu do podsestavy složené z různých
materiálů. Pokud takový výřez změní objem složeného snímku, hmotnost je
nedostupná; pro její přesný výpočet dosud chybí materiálové rozdělení objemu
po výřezu. Výřez do přímo vloženého dílu používá jeho hustotu a skutečný
zbylý objem.

## Razítko

Token `&document.mass_unit` zobrazuje jednotku zdrojového dokumentu.
Dodávaná razítka používají `[&document.mass_unit]` místo pevného `[kg]`.
Údaj a jednotka tak zůstávají ve shodě. Řádky kusovníku nesou i jednotku
svého zdroje. Podrobnosti editace jsou v [návodu k výkresům](DRAWINGS.md).
