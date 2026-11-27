# Sestava

## Než začnete

Připravte nativní díly nebo podsestavy a vytvořte sestavu (`.asmz`). Každý
vložený výskyt má vlastní identitu a umístění.

## Postup

1. Pomocí **Vložit komponentu** přidejte díl nebo vnořenou sestavu. Zkontrolujte její výskyt
   ve stromu.
2. Otevřete **Vlastnosti** výskytu. Aktivujte pole reference komponenty,
   vyberte její geometrii a poté odpovídající referenci vlastnící sestavy.
3. Zvolte nabízenou vazbu a hodnotu. Přidávejte nezávislé reference, dokud
   nezůstanou požadované posuvné a rotační stupně volnosti.
4. Zkontrolujte umístění a potvrďte **OK**. **Zrušit** vrátí nepotvrzené změny,
   včetně závislých umístění.
5. Pro úpravu zdroje komponenty použijte **Aktivní**. Díl nabídne modelovací
   nástroje; podsestava vlastní nástroje sestavy. Zpět se vraťte příkazem
   **Zpět do sestavy**.

## Co zkontrolovat

Komponentu umisťuje pouze její bezprostředně nadřazená sestava. Před úpravou
vnitřních umístění aktivujte příslušnou podsestavu. Scéna nejvyšší sestavy zůstává
viditelná jako kontext a externí geometrie slouží pouze jako reference pro čtení.

Aktuálně vypočtená geometrie dílu se zobrazuje v jeho výskytech. **Regenerovat**
výslovně řeší vazby a operace vlastněné sestavou; přepínání záložek nikoli.
Nové materiálové prvky se vytvářejí v dílech, nikoli na úrovni sestavy.

Viz [práce se sestavou](../../USER_MANUAL.md#basic-assembly-workflow)
a [reference sestavy](../../ASSEMBLY_REFERENCES.md).
