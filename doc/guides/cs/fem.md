# FEM

## Než začnete

Připravte vypočtené objemové těleso v dílu a přiřaďte mu vhodné materiálové
vlastnosti. Zvolte analýzu odpovídající podporovanému fyzikálnímu modelu.

## Postup

1. Deaktivujte aktuální těleso, přejděte tak na kořen dílu a zvolte **Vytvořit FEM**. V okně **Vlastnosti FEM tělesa** vyberte zdrojové těleso a analýzu, potom potvrďte **OK**.
2. Definujte podmínky: pro mechanickou úlohu podepření, síly nebo tlak;
   pro vedení tepla podporované tepelné podmínky.
3. Výslovně vygenerujte objemovou síť. Před řešením zkontrolujte diagnostiku sítě.
4. V panelu FEM spusťte odpovídající analýzu: **Lineární statika**, **Ustálená teplotní analýza**, **Tepelně-pevnostní analýza**.
5. Podle úlohy zvolte výsledky posunutí, napětí nebo teploty. Prohlédněte extrémy
   a umístěné měřicí body; porovnejte výsledky po zjemnění sítě.

## Co zkontrolovat

Mechanická analýza je izotropní lineárně elastická statika s malými posunutími.
Tepelná analýza je ustálená. Volitelná tepelná roztažnost používá jednosměrné
provázání při malých přetvořeních; kombinovaný režim zahrnuje také sílu a tlak.

Zvětšení deformace mění zobrazení, nikoli vypočtené posunutí. Animace znázorňuje
statickou úlohu, nikoli dynamiku. Kontakt, plasticita, přechodový ohřev
a teplotně závislé vlastnosti jsou mimo tento rozsah.

Definice upravujte ve vlastnostech a při změně vstupů proveďte nový výpočet.
Nativní uložení zachovává podporované definice FEM a reference měření. Samotné
úspěšné vytvoření sítě nebo dokončení řešiče nedokazuje technickou správnost výsledku.

Viz [možnosti a omezení FEM](../../FEM_STATUS.md).
