# ZIMA-CAD – strojírenské kóty a geometrické symboly

Tento dokument slouží jako návrhový základ pro kóty ve Sketcheru, modelu
a rozpracovaném Drawing modulu. Vychází z běžné praxe ISO 129, ISO GPS
a ASME Y14.5.

## Základní pravidlo

Kóta nesmí být interně uložena jen jako výsledný text. Musí oddělovat:

- geometrickou hodnotu používanou solverem,
- význam a typ kóty,
- grafické symboly,
- uživatelský text před a za hodnotou,
- tolerance,
- přesnost a způsob zobrazení.

Příklad:

```json
{
  "dimension_type": "diameter",
  "value": 25.0,
  "prefix_symbols": ["diameter"],
  "prefix_text": "",
  "suffix_text": "H7",
  "tolerance": null
}
```

Zobrazení:

```text
⌀25H7
```

## Typy kót

- lineární rozměr,
- průměr,
- poloměr,
- sférický průměr,
- sférický poloměr,
- řízený poloměr,
- délka oblouku,
- tloušťka,
- hloubka,
- čtvercový průřez,
- kuželovitost a sklon,
- úkos,
- závit,
- roztečná kružnice,
- počet opakování,
- referenční a typický rozměr,
- minimální a maximální hodnota,
- tolerance.

## Značky před hodnotou

| Zápis | Význam | Příklad |
|---|---|---|
| `⌀` | průměr | `⌀20` |
| `R` | poloměr | `R5` |
| `SR` | sférický poloměr | `SR50` |
| `S⌀` | sférický průměr | `S⌀100` |
| `CR` | řízený poloměr | `CR12` |
| `□` | čtvercový průřez | `□30` |
| `⌴` | válcové zahloubení | `⌀10⌴⌀18` |
| `⌵` | kuželové zahloubení | `⌀6⌵90°` |
| `↧` | hloubka | `↧15` |
| `⌒` | délka oblouku | `⌒50` |
| `t` | tloušťka | `t3` |
| `M` | metrický závit | `M10` |
| `G` | trubkový závit BSPP | `G1/2` |
| `Rp` | vnitřní trubkový závit | `Rp1/2` |
| `Rc` | kuželový vnitřní závit | `Rc1/2` |
| `R` | kuželový vnější závit | `R1/2` |
| `Tr` | trapézový závit | `Tr40×7` |

Další závitové zápisy zahrnují `UNC`, `UNF`, `ACME` a označení levého
závitu `LH`.

## Značky a text za hodnotou

| Zápis | Význam | Příklad |
|---|---|---|
| `±` | symetrická tolerance | `20±0,1` |
| `MAX` | maximální hodnota | `R0,5MAX` |
| `MIN` | minimální hodnota | `3MIN` |
| `REF` | referenční rozměr | `35REF` |
| `TYP` | typický rozměr | `R5TYP` |
| `4X` | počet opakování | `4X⌀8` |
| `6 PLCS` | počet míst | `⌀5 6 PLCS` |
| `THRU` | skrz celý materiál | `⌀10THRU` |
| `EQ SP` | rovnoměrné rozmístění | `8X EQ SP` |
| `AF` | rozměr přes plochy | `17AF` |

Texty jako `TYP`, `REF`, `MAX`, `MIN`, `THRU` a `H7` zůstávají běžnými
řetězci. Není nutné pro ně vytvářet grafické symboly.

## Mezní úchylky

Editor kóty musí podporovat tři způsoby zápisu tolerance.

### Symetrická tolerance

Jedna hodnota se znakem `±` (U+00B1) se zobrazí v jednom řádku za
jmenovitým rozměrem:

```text
20±0,010
```

### Jedna mezní úchylka

Za jmenovitým rozměrem lze zobrazit jednu úchylku s kladným nebo
záporným znaménkem:

```text
20+0,020
20−0,010
```

Zadané znaménko je součástí údaje a aplikace je nesmí automaticky
změnit.

### Horní a dolní mezní úchylka

Nesymetrická tolerance se neukládá ani nezobrazuje jako jediný text
oddělený lomítkem. Tvoří ji dvě samostatné hodnoty:

- horní úchylka,
- dolní úchylka.

Ve výkresu se horní úchylka vykreslí nad dolní úchylkou napravo od
jmenovité hodnoty. Obě úchylky mají menší písmo než jmenovitá hodnota:

```text
      +0,020
20
      −0,010
```

Povolené jsou také případy, kdy jsou obě úchylky nezáporné nebo kdy je
horní úchylka nulová:

```text
      +0,100           +0,000
50                 30
      +0,000           −0,050
```

Znaménko je součástí úchylky a musí se zobrazit i u nulové hodnoty.
Interně proto nestačí uložit pouze absolutní číslo. Datový model musí
zachovat znaménko a rozlišení horní/dolní úchylky, například:

```json
{
  "tolerance_mode": "deviations",
  "upper_deviation": "+0.020",
  "lower_deviation": "-0.010"
}
```

Datový model rozlišuje režimy `symmetric`, `single_deviation`
a `deviations`. Prázdný režim znamená kótu bez tolerance.

Desetinný oddělovač se při zobrazení řídí nastavením dokumentu nebo
lokalizací. Interní numerická reprezentace může používat desetinnou
tečku. Počet desetinných míst úchylek se nesmí automaticky ořezat,
protože koncové nuly vyjadřují jejich předepsanou přesnost.

## Úkosy, kuželovitost a rozteče

Příklady:

```text
2×45°
C2
1×30°
1:10
1:50
5%
⌀100PCD
6X⌀8EQ SP
```

## Závity

Příklady:

```text
M10×1,5
M12-6H
M16×2-6g
G1/2
Tr40×7
M8×1LH
1/4-20UNC
1/4-28UNF
1"-5ACME
```

## První sada grafických CAD symbolů

Tyto symboly mají být připravené jako vlastní vektorové značky. Unicode
slouží pouze jako textový fallback:

| Symbol | Unicode | Význam |
|---|---|---|
| `⌀` | U+2300 | průměr |
| `□` | U+25A1 | čtverec |
| `⌴` | U+2334 | válcové zahloubení |
| `⌵` | U+2335 | kuželové zahloubení |
| `↧` | U+21A7 | hloubka |
| `⌒` | U+2312 | délka oblouku |
| `∠` | U+2220 | úhel |
| `°` | U+00B0 | stupně |
| `±` | U+00B1 | plus/minus |
| `×` | U+00D7 | násobení |
| `≈` | U+2248 | přibližně |

Znak `∅` (U+2205, prázdná množina) se nesmí používat jako náhrada
strojírenské značky průměru `⌀`.

První verze editoru vlastností kóty nabízí tuto sadu v symbolové paletě
u textu před i za hodnotou. Vybraný symbol se vloží na aktuální pozici
kurzoru. Znak `∅` paleta záměrně nenabízí.

## GPS – geometrické tolerance

Pro GPS značky je vhodné vlastní vektorové kreslení:

- přímost,
- rovinnost,
- kruhovitost,
- válcovitost,
- profil čáry,
- profil plochy,
- rovnoběžnost,
- kolmost,
- sklon,
- poloha,
- souosost,
- symetrie,
- kruhové házení,
- celkové házení.

Unicode reprezentace není u všech GPS značek dostupná ani typograficky
spolehlivá.

## Povrch a svary

Samostatná vektorová knihovna bude později potřeba také pro:

- základní značku drsnosti,
- povrch bez obrábění,
- povrch s obráběním,
- koutový, V, X, U a J svar,
- bodový a švový svar,
- svar po obvodu,
- svar prováděný na montáži.

Svary se budou řídit požadavky rozvíjeného Drawing modulu a ISO 2553.

## Zobrazovací pravidla ZIMA-CAD

- Pasivní kóta nezobrazuje jednotku délky.
- Koncové nuly se v pasivní kótě skrývají.
- Přesná numerická hodnota se ukáže při editaci.
- Prefix, hodnota a suffix se skládají bez automatických mezer.
- Text začíná za odkazovou čárou a roste zleva doprava.
- Symboly musí mít opticky sjednocenou velikost, tloušťku a účaří.
- Kritické geometrické značky se kreslí vektorem, nikoli pomocí náhodného
  glyphu dostupného systémového fontu.
- Jednotky zůstávají uložené v dokumentu a používají se pro výpočty
  a převody, i když nejsou v pasivní kótě vypsané.
