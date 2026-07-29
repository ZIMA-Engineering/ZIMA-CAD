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

## Pomocná geometrie kontejneru

Viditelnost pomocných bodů, os a rovin se ovládá v tree pravým tlačítkem nad
položkou **Počátek** příslušného kontejneru:

- **Skrýt** pomocnou geometrii,
- **Odkrýt** pomocnou geometrii.

Nastavení je součástí dokumentu.
