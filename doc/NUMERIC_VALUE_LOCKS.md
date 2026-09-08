# Zámky číselných hodnot

Za délkou nebo úhlem ve vlastnostech je ikona zámku. Zamčené pole nelze
přepsat ani měnit kolečkem; přímá editace téhož rozměru ve View zámek také
respektuje. Rozměr lze ve View zamknout či odemknout kontextovým menu.
Zamčená hodnota je černá; hover a výběr si ponechávají oranžovou a azurovou.
Šířka číselného pole počítá s ikonou, jednotkou a přesností dokumentu.

## Zadání reference

- **Prázdný řádek:** zapnutí zámku připraví jednorázové převzetí současné
  hodnoty. Po výběru roviny se změří vzdálenost aktuálního počátku,
  zapíše se odsazení a zámek se automaticky odemkne. V sestavě se hodnota
  převezme po vyplnění obou referencí; u úhlové vazby se změří současný úhel.
- **Vyplněný řádek:** zapnutí zámku trvale ochrání hodnotu. Při výměně
  reference se převezme aktuální vzdálenost nebo úhel k nové referenci
  a zámek zůstane zapnutý.
- **Bod a osa:** koincidence přisune počátek na vybranou geometrii,
  nastaví nulové odsazení a automaticky je zamkne. Ostatní volné směry
  určuje existující řešič vazeb.
- Když současnou hodnotu nelze určit, reference se nepřepíše a dialog
  oznámí chybu. Přípravu jednorázového převzetí lze opětovným kliknutím zrušit.

Příklad: počátek leží na X = 12 mm. Zapnutí zámku v prázdném řádku a výběr
roviny X = 0 vyplní 12 mm a odemkne hodnotu. Trvalé zamčení vyplněného řádku
následované výměnou za rovinu X = 5 vyplní 7 mm a zachová zámek.

## Rozsah a ukládání

Zámky jsou součástí vlastností primitiv, tažení, konstrukčních objektů,
umístění těles a komponent, roztečí a úhlu pole, obrázků a oblastí BOM.
Vlastnosti výkresového pohledu chrání také jeho polohu a měřítko.
Zámky obrázku chrání šířku a výšku i před změnou druhého rozměru při
zachovávání poměru stran. Zamčená souřadnice pohledu omezuje ruční tažení.

U orientace řízené referencemi rozměr RX/RY/RZ ve View představuje místní
úhlovou korekci a zamyká právě její pole. Absolutní úhel a korekce mají
samostatné uložené zámky.

OK uloží hodnoty i zámky společně, Zrušit zahodí celý návrh. Bez otevřených
vlastností je přepnutí zámku ve View samostatná změna dokumentu s Undo/Redo.
Trvalé zámky se ukládají do dokumentu; jednorázové převzetí je pouze stav
otevřeného dialogu. Změna samotného zámku nevyvolává výpočet solidu v OCCT.

Zámek chrání ruční editaci hodnoty. Aktualizace závislého rozměru řešičem
zůstává možná. V sestavě se počet fyzických stupňů volnosti počítá z vazeb;
ruční tažení navíc respektuje zámky souřadnic. Pokud posuv po šikmé ose
vyžaduje změnit zamčené X, tažení se neprovede. Měřené kóty výkresu
neřídí zdrojový díl, proto z nich zámek nevytváří novou geometrickou vazbu.

## Ověření

`zima_cpp_numeric_value_locks_contract` kontroluje zadání, přímou editaci,
OK/Zrušit, ukládání, jednorázové převzetí, výměnu reference a přisunutí na
bod či osu. Sestavové testy ověřují zámky při volném i šikmém posuvu,
měření úhlů a ukládání. Integrační test vlastností tělesa kontroluje
propojení zámku úhlové korekce s View, Undo/Redo a uložení zámku mimo dialog.
Test číselných polí používá 3, 4, 6, 9 a 12 desetinných míst a větší písmo.

Převzetí odsazení sestavové roviny vychází z její podepsané vzdálenosti od
počátku komponenty. Nezávisí na zvoleném vrcholu triangulace a zachová
polohu počátku i při potřebném zarovnání původně nakloněných rovin.

## Jazyk ovládání

Nápověda prázdného řádku popisuje jednorázové převzetí současné hodnoty
při výběru reference a následné odemčení. Po jeho zapnutí nabízí zrušení
převzetí. Vyplněný řádek používá **Zamknout hodnotu / Odemknout hodnotu**.
Tyto texty, chybová hlášení i akce ve View jsou dostupné ve čtyřech jazycích
aplikace; podrobnosti jsou v [dokumentaci překladů](LOCALIZATION.md).
