# Samostatné instance ZIMA-CAD

Dvojklik na soubor `.prtz`, `.asmz` nebo `.drwz` v Průzkumníku spustí nový
proces ZIMA-CAD a otevře v něm vybraný dokument. Stejný způsob otevření platí
pro šablony `.frmz` a `.tblz`. Dokument určí pracovní adresář a místní konfiguraci
své instance; pracovní adresář Průzkumníka ani desktopového zástupce jej nenahradí.

Okna lze umístit vedle sebe nebo na různé monitory a porovnávat projekty.
Každé má vlastní otevřené dokumenty, výběr, kameru a historii Undo/Redo.
Zavření jednoho procesu neukončí ostatní.

Titulek má například podobu **ZIMA-CAD — Instance 2 — ZE0001.asmz**.
Číslo se nemění při přepínání či zavření dokumentů ani při zavření jiné instance.
Po ukončení instance se její číslo uvolní pro další spuštění; existující okna
se nepřečíslují. Pokud systém nedovolí rezervovat číslo, titulek použije PID
procesu a aplikace se přesto spustí.

**Okno → Nové okno** také spustí samostatný proces. Začne bez dokumentů
v pracovním adresáři původního okna. Přes **Soubor → Nastavit pracovní adresář**
lze v novém okně zvolit jiný projekt; tato změna platí jen v dané instanci.
**Soubor → Otevřít** a otevírání komponent
uvnitř aplikace nadále pracují v aktuální instanci, takže projekt může mít
otevřenou sestavu, její díly i související výkresy v záložkách.

## Registrace ve Windows

Po místním sestavení spustit:

```powershell
./tools/register-windows-file-types.ps1
```

Volitelný parametr `-Executable` určuje konkrétní C++ EXE. Registrace je pouze
pro přihlášeného uživatele, nevyžaduje správce a ukazuje přímo na GUI aplikaci.
Příkaz otevření má podobu `"cesta k EXE" "%1"`; soubor s mezerami zůstane
jedním argumentem a každé spuštění vytvoří samostatný proces bez konzole.
Po přesunutí EXE je potřeba registraci zopakovat.

Skript nemění chráněnou volbu Windows `UserChoice`. Pokud si uživatel dříve
výslovně vybral jiný program, lze ZIMA-CAD zvolit v Průzkumníku přes
**Otevřít v programu → Zvolit jinou aplikaci**.

## Rozsah oddělení

Instance oddělují rozpracované dokumenty v paměti. Soubory a globální nastavení
na disku zůstávají společné. Číslo instance není zámek dokumentu ani mechanismus
synchronizace souběžných úprav stejného souboru.

Rezervace čísel používají `QLockFile` v uživatelském datovém adresáři
`ZIMA-CAD/instances`. Nemají časovou expiraci běžícího procesu, automaticky
uvolní záznam po zániku procesu a nikdy neslouží jako omezení na jednu instanci.

## Ověření

`zima_cpp_instance_startup_contract` spouští skutečné GUI procesy s Partem,
Assembly a Drawingem současně. Ověřuje odlišné PID a číslo, dokument i pracovní
adresář každého procesu, další proces přes **Nové okno**, stabilní titulek po
zavření dokumentu a nezávislé ukončení. Samostatně kontroluje zpracování
argumentů, cesty s mezerami, přípony a uvolňování čísel instancí.