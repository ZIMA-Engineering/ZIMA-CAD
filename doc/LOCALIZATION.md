# Překlady uživatelského rozhraní

Jazyk aplikace se vybírá v **Globálním nastavení → Jazyk aplikace**.
Dostupné jsou čeština (`cs`), angličtina (`en`), němčina (`de`) a francouzština (`fr`).
Použije se `Application/Language` a adresář `Paths/Localization` z platné
konfigurace. Místní `config.ini` pracovního adresáře má přednost před
základním `config/config.ini`; jeho výběr se zachová i po potvrzení nastavení.

Nově otevírané vlastnosti používají změněný jazyk ihned. Pro sjednocení všech
již otevřených nabídek, panelů a dialogů aplikaci restartujte; upozornění
je také v Globálním nastavení. Změna jazyka nepřekládá uživatelské názvy
objektů, souborů, texty razítek ani uložené hodnoty modelu.

## Nově přeložené funkce

Všechny čtyři jazyky obsahují texty zámků hodnot, jednorázového převzetí
vzdálenosti nebo úhlu, obrázků v razítku a oblasti kusovníku. Přeložené
jsou jejich příkazy, vlastnosti, zarovnání, směry opakování, nápovědy,
chyby zadání a uložení, filtry souborů a společná tlačítka OK/Zrušit.

| Česky | English | Deutsch | Français |
| --- | --- | --- | --- |
| Zamknout hodnotu | Lock value | Wert sperren | Verrouiller la valeur |
| Odemknout hodnotu | Unlock value | Wert entsperren | Déverrouiller la valeur |
| Obrázek | Image | Bild | Image |
| Vlastnosti obrázku | Image Properties | Bildeigenschaften | Propriétés de l’image |
| Oblast kusovníku | BOM region | Stücklistenbereich | Zone de nomenclature |
| Zachovat poměr stran | Keep aspect ratio | Seitenverhältnis beibehalten | Conserver les proportions |
| Zrušit | Cancel | Abbrechen | Annuler |

Chování zámků popisují [Zámky číselných hodnot](NUMERIC_VALUE_LOCKS.md).
Vkládání obrázků PNG/SVG a opakování kusovníku popisují [Výkresy](DRAWINGS.md).

## Úprava jazykových souborů

Katalogy jsou soubory UTF-8 `config/localization/{cs,en,de,fr}.ini`.
C++ načítá dvě oddělené sekce:

- `[Translations]`: dosavadní pojmenované klíče pro `ApplicationSettings::text`,
  například `global.language`. Tyto klíče používají také nabídky a výběr souborů.
- `[QtTranslations]`: zdrojový text pro C++ `tr()` / `QObject::tr()`, například
  `Zamknout hodnotu = Lock value`. Překladač `QTranslator` je vlastněný aplikací
  a při změně konfigurace se nahradí; nevzniká řetězec starých jazyků.

Pokud stejný text potřebuje jiný překlad podle kontextu Qt, použijte klíč
`Kontext|Zdrojový text`. Má přednost před společným zdrojovým textem.
Kontext určuje třída s `Q_OBJECT`, od které pochází `tr()`; nemusí být shodný
s názvem odvozeného dialogu. Neznámý text se zobrazí ve zdrojovém jazyce.
Tato sekce je určena pro texty bez množných tvarů; zprávy s `n` vyžadují
překladový katalog s podporou plurálů.

Při přidání zprávy doplňte stejné klíče ve všech čtyřech jazycích.
Zachovejte přesně zástupné značky `%1`, `%2` atd., tokeny `&bom.item_number`
a `&bom.quantity` i přípony ve filtrech souborů. Řádek se dělí na prvním
`=`; klíč je tedy nesmí obsahovat. Texty jsou jednořádkové, mezery na
okrajích se ořezávají. Nepřekládejte interní identifikátory, například
`center`, `middle`, `up` nebo klíče zámků.

## Ověření

`zima_cpp_translations_contract` načte všechny čtyři skutečné katalogy přes
místní konfiguraci, zkontroluje shodné klíče, zástupné značky, výměnu překladače,
kontext a návrat ke zdrojovému textu. Ve vlastnostech kvádru ověří nápovědy
trvalého zámku i obou stavů jednorázového převzetí a tlačítko Zrušit.

Integrační režim `ZIMA_VERIFY_TEMPLATES_ONLY=1` testu
`zima_cpp_workspace_startup_contract` otevře skutečné vlastnosti obrázku
i oblasti kusovníku ve všech čtyřech jazycích. Ověří texty a zachování
uloženého zarovnání, pořídí snímky do `Projects/test/image-properties-*.png`
a `Projects/test/bom-properties-*.png`.
