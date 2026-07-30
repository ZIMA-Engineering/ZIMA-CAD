# ZIMA-CAD – uživatelský manuál

## Ovládání 3D pohledu

| Ovládání | Funkce |
| --- | --- |
| Prostřední tlačítko + pohyb myši | Otáčení pohledu |
| Prostřední + pravé tlačítko + pohyb myši | Posun pohledu |
| Kolečko myši | Přiblížení a oddálení |
| Jeden klik prostředním tlačítkem | Použít změny v aktivním dialogu, pokud nabízí tlačítko Použít |
| Dvojklik prostředním tlačítkem | Potvrzení aktivního dialogu tlačítkem OK |
| F2 | Zavřít aktivní dokumentový tab |

Po posunu kombinací prostředního a pravého tlačítka se kontextové menu
neotevře.

## Pohled kolmo

1. V horní liště pohledu aktivujte tlačítko **Pohled kolmo**.
2. Ve 3D pohledu vyberte rovinnou plochu solidu nebo referenční rovinu.
3. Kamera se natočí kolmo k vybrané geometrii a příkaz se automaticky ukončí.

Příkaz lze před výběrem zrušit opětovným kliknutím na tlačítko nebo klávesou
`Esc`.

## Výběr referencí ve Vlastnostech

- Vybrané plochy, hrany, body, osy a roviny zůstávají ve view azurově
  zvýrazněné.
- Geometrie právě hledaná výběrovou funkcí má oranžový obrys stejně jako při
  běžném výběru modelu.
- Referenční plocha je označena pouze azurovými obvodovými hranami, nikoli
  barevnou výplní.
- Kliknutím na název reference v okně Vlastnosti lze její zvýraznění vypnout
  nebo znovu zapnout. Reference přitom zůstává součástí vazby.
- Skutečné odstranění reference provádí červené tlačítko **×** vlevo na jejím
  řádku.
- Každá nová reference se nejprve ověří v dočasném řešení. Konfliktní,
  přeurčující nebo nadbytečná reference se do seznamu nepřidá a původní
  geometrie zůstane beze změny.
- Při dosažení 0 stupňů volnosti je objekt plně určený. Geometrii lze stále
  vybrat, ale jako další reference se přijme až po odebrání některé existující
  vazby.

## Živé úpravy ve Vlastnostech

- Změna číselné hodnoty, včetně použití šipek `+` a `−`, se okamžitě projeví
  ve 3D pohledu.
- `Enter` v číselném poli dokončí zadání hodnoty, ale okno nezavře.
- **Použít** potvrdí aktuální stav a ponechá okno otevřené.
- Jeden klik prostředním tlačítkem odpovídá tlačítku **Použít**. Pravidlo
  platí globálně také v dialozích nového souboru, materiálu a nastavení.
- **OK** potvrdí změny a zavře okno.
- **Zrušit** obnoví stav při otevření okna nebo stav naposledy potvrzený
  tlačítkem **Použít**.
- Dvojklik prostředním tlačítkem odpovídá tlačítku **OK** v aktivním dialogu.

### Operace solidu

V horní části Vlastností solidu je výrazný přepínač operace:

- zelené **+ Přičíst** přidává objem,
- červené **− Odečíst** odebírá objem.

Změna operace se okamžitě projeví ve view. Stejná operace je nadále dostupná
také v kontextovém menu objektu v tree. Obě místa pracují se společným
nastavením solidu.

## Režim skici

Ve Vlastnostech skici tlačítko **SKETCH** potvrdí její umístění a otevře
samostatný režim kreslení. Tlačítko je dostupné po výběru roviny nebo rovinné
plochy, která určuje orientaci skici.

Po vstupu se pohled nastaví kolmo ke skice. Lokální osy X/Y jsou zobrazené
hnědou tenkou čárkovanou čarou přes celé view. Profilová geometrie je modrá,
body jsou žluté a konstrukční čáry jsou žluté a čerchované jako osy.

Základní nástroje jsou **Konstrukční čára**, **Bod**, **Úsečka**, **Oblouk**
a **Spline**. Pravé tlačítko zruší rozpracovaný prvek; u spline ji po zadání
alespoň dvou bodů dokončí.

Jeden klik prostředním tlačítkem potvrdí právě zadávanou entitu. Tažení
prostředním tlačítkem nadále pouze otáčí pohled a zadání nepotvrdí.

Každý bod má interní souřadnice X/Y, ty ale bez explicitní uživatelské kóty
nejsou podmínkou a nezobrazují se. Solver je používá jako aktuální polohu a smí
je měnit při řešení vazeb. Konstrukční čára i další geometrie odkazují na své
řídicí body; kliknutí do volného místa bod vytvoří a kliknutí poblíž
existujícího bodu jej znovu použije.

Řídicí souřadnicová kóta se vytvoří příkazem **Kóty → Vodorovná vzdálenost**
nebo **Kóty → Svislá vzdálenost** a následným výběrem bodu. Teprve takto
vytvořená kóta se zobrazí a její hodnota vstupuje do solveru.

Příkaz **Kóty → Vzdálenost** vytvoří po výběru dvou bodů šikmou řídicí kótu.
Její hodnota je skutečná eukleidovská vzdálenost bodů. Při změně hodnoty se
zachová dosavadní směr spojnice, pokud jej jiné vazby neurčují jinak.
Explicitní kóty jsou ve skicáři zobrazené vždy; samostatný přepínač jejich
viditelnosti se nepoužívá.

V nabídce **Vazby → Kolmá** vyberte postupně dvě úsečky nebo konstrukční
čáry. První je ta, která se má srovnat; druhá určuje referenční směr.
U první zůstane zachovaný první bod a délka a její druhý bod se dopočítá tak,
aby byly čáry kolmé. Pravé tlačítko zruší první výběr. Jako druhou referenci
lze zvolit také čárkovanou lokální osu X nebo Y skici nebo externí čárovou
referenci. Vazbu lze odstranit z kontextové nabídky ve stromu skici.

U vazby **Rovnoběžná** je pořadí stejné: první vybraná čára se srovná a nese
symbol rovnoběžnosti, druhá čára určuje referenční směr. Druhá čára se nikdy
nepřepočítává. Pokud je první čára již směrově zavazbená, ohlásí se konflikt.

Příkazy **Vazby → Vodorovná** a **Vazby → Svislá** se aplikují výběrem jedné
úsečky nebo konstrukční čáry. První bod zůstane pevný a odpovídající souřadnice
druhého bodu se dopočítá. Vodorovné a svislé vazby jsou uprostřed geometrie
označené zeleným písmenem **H** nebo **V**.

V nabídce **Vazby → Rovnoběžná** vyberte referenční a potom řízenou úsečku.
Solver zachová délku řízené úsečky a natočí ji rovnoběžně; vazbu označuje
zelený symbol **∥**.

Zobrazené explicitní kóty lze pravým tlačítkem **Zamknout** nebo
**Odemknout**. Zamknutá kóta je řídicí: solver zachová její hodnotu.
Odemknutím přestane být podmínkou a z pohledu zmizí. Stav se ukládá společně
se skicou.

Tlačítko **Pohled kolmo** s ikonou normálového pohledu kdykoliv znovu narovná
a vystředí kameru podle aktivní skici.

Nástroj **Výběr** s ikonou šipky ukončí rozpracovaný kreslicí příkaz. Kliknutím
lze označit bod nebo geometrii oranžově a odstranit ji tlačítkem
**Vymazat vybrané** nebo klávesou `Delete`. Odstranění řídicího bodu odstraní
také geometrii, která je na něj navázaná.

- **Dokončit skicu** uloží změny a obnoví předchozí 3D pohled.
- **Zrušit úpravy** zahodí změny od vstupu do režimu a vrátí se do Vlastností
  skici.

## Pomocná geometrie kontejneru

Viditelnost pomocných bodů, os a rovin se ovládá v tree pravým tlačítkem nad
položkou **Počátek** příslušného kontejneru:

- **Skrýt** pomocnou geometrii,
- **Odkrýt** pomocnou geometrii.

Nastavení je součástí dokumentu.
