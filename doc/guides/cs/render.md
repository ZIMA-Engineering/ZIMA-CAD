# Render

## Než začnete

V dílu vypočítejte zdrojová objemová tělesa. Nastavte jejich barvy a vzhled
a připravte výchozí pohled.

## Postup

1. Aktivujte kořen dílu a zvolte **Vytvořit render**.
2. Ponechte **Viditelná tělesa**, nebo pomocí **Vybraná tělesa** určete konkrétní
   zdroje. Nastavte rozměry obrázku a kvalitu vzorkování, potom potvrďte **OK**.
3. Podle potřeby přidejte nebo upravte kamery a světla. Aktivní kameru vyberte
   ve vlastnostech render tělesa.
4. Zvolte **Vyrenderovat obrázek**. Samostatná záložka renderu zobrazuje průběh
   a výsledek; **Galerie renderů** tuto záložku znovu otevře.
5. Dokončený obrázek exportujte do PNG nebo JPEG. Pokud chcete snímek uchovat
   také uvnitř nativního dílu, použijte **Uložit do dílu**.

## Co zkontrolovat

Render používá snímek již vypočtené geometrie. Úpravy zdroje nepřepisují starší
vyrenderovaný obrázek. Zavření záložky renderu ponechá úlohu běžet; **Zastavit render** ji zruší a zavření vlastnícího dokumentu úlohu zastaví.

Větší počet vzorků může snížit viditelný šum. Uložené obrázky zůstávají historickými
záznamy a zvětšují dokument. Počáteční renderer podporuje scény objemových těles
dílu; sestavy, animace, textury a lom světla ve skle jsou mimo tento rozsah.

Viz [render](../../RENDER.md).
