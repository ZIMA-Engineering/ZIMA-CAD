# Plechové díly

## Než začnete

Aktivujte upravitelné těleso dílu a pomocí tlačítek pracovních prostředí zvolte **Plechy**. Pro
následující postup rozvinutí použijte vypočtený plechový model s definovanými
tabulemi a ohyby.

## Postup

1. Otevřete **Vlastnosti plechu…**. Zkontrolujte tloušťku materiálu a K faktor
   používaný v dílu; potvrďte požadované nastavení.
2. Prohlédněte ohnutý model. Před vytvořením rozvinutého stavu ověřte směry ohybů
   a strany materiálu.
3. Zvolte **Rozvinout**. Ponechte všechny použitelné oblasti, nebo zapněte výběr
   jednotlivých prvků a požadované oblasti vyberte ve stromu či pohledu.
4. Potvrďte **OK** a prohlédněte rozvinutou geometrii. Uložte díl a potom použijte příkaz **DXF** v panelu plechových nástrojů pro podporovaný export rozvinu.
5. Pokud navazující historie opět potřebuje ohnutý stav, použijte **Ohnout zpět**.

## Co zkontrolovat

Rozvinout a Ohnout zpět jsou operace historie; předchozí definice zůstávají
dostupné. Změny provádějte opětovným otevřením vlastností nebo pomocí Zpět/Znovu
u již potvrzených akcí.

Ověřte rozměry rozvinu podle vašeho výrobního postupu. Výchozí K faktor není
kalibrací pro konkrétní materiál. **Plech z tělesa** rozpoznává podporované
tečně navazující rovinné a válcové povrchy; libovolná volná geometrie do tohoto
rozpoznávání nespadá.

Viz [plechy](../../SHEET_METAL.md), [stavy plechu](../../SHEET_STATE_DEVELOPMENT.md)
a [převod a DXF](../../SHEET_EXCHANGE.md).
