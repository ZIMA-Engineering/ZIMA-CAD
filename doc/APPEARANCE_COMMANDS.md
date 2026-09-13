# Příkazy vzhledu

`appearance.get/set/reset/faces/palette` používají společné datové operace
s dialogem Barvy a vzhled. `set` a `reset` mění uložené styly a jednu položku
historie; nevolají OCCT, solver vazeb ani regeneraci závislostí.

## Rozsah

Bez `instance_path` je cílem Part. Vynechané `body` znamená aktivní těleso;
explicitní prázdné `body` označuje výchozí styl celého dokumentu. Čtení přijímá
ID jiného tělesa, jeho změna vyžaduje aktivaci. U odvozené kopie se vzhled
upravuje explicitním ID tělesa bez aktivace geometrie. Styl tělesa přebíjí základní
styl a skupina ploch přebíjí styl tělesa.

V Assembly je nutné `instance_path` s právě jedním výskytem bezprostředně
vlastněného Partu. Vnořený Part se upravuje po aktivaci jeho vlastnící
Assembly. Plná zobrazovaná cesta GUI se převádí existujícím resolverem na
stejného vlastníka. Přepsání se ukládá pouze na vybraný komponentový výskyt,
nikoli na zdrojový Part nebo jiné instance. `body` zde může určit těleso
zdrojového Partu uvnitř tohoto přepsání. Prázdný rozsah mění základní styl
výskytu a ponechává jeho případné zvláštní styly těles a skupin.

Otevřený zdrojový Part je autoritativní i pro zděděný vzhled. Jeho neuložená
změna se čte přímo, bez regenerace. Pro zavřený zdroj se dotazy opírají o
snímek uložený v komponentě; neotevírají soubory ani nové taby.

## Argumenty

```json
{"command":"appearance.set","arguments":{"style":{"color":"#225FC2","roughness":0.12,"metallic":0.75}}}
{"command":"appearance.faces","arguments":{"offset":0,"limit":100}}
{"command":"appearance.set","arguments":{"groups":[{"id":"polished","name":"Leštěné plochy","style":{"roughness":0.04,"metallic":1},"faces":[{"owner":"<container-id>","key":"<persisted-result-key>"}]}]}}
{"command":"appearance.get","arguments":{}}
{"command":"appearance.reset","arguments":{"instance_path":"<direct-occurrence-path>","inherit":true}}
```

- `style` je patch: `color`, `roughness` a `metallic`. Barva je `#RRGGBB`
  nebo Qt `#AARRGGBB`; drsnost je 0,04 až 1 a kovový charakter 0 až 1.
  Neznámé položky a neplatná čísla se odmítnou.
- `groups` nahrazuje skupiny pouze ve vybraném rozsahu tělesa. Ostatní
  skupiny zůstanou stejné. Prázdný seznam je odstraní. Skupina má `id`,
  `name`, `style`, `faces`. Bez ID vznikne nové stabilní ID. U existujícího
  ID lze vynechat nezměněné vlastnosti; vynechaná skupina se odstraní.
- `faces` obsahuje dvojice `owner` a `key` z `appearance.faces`. Jde o
  identitu uložené výsledné plochy určenou výhradně pro vzhled. Kontext
  konkrétního výskytu nese `instance_path` příkazu.
- `reset` odstraní skupiny a styl ve vybraném rozsahu. `inherit:true`
  u celého výskytu odstraní jeho přepsání a vrátí vzhled zdroje. Reset
  jednotlivého tělesa ponechává ostatní styly výskytu.
- `palette` vrací vestavěné styly se jménem, kategorií, ID a hodnotami.
  Uživatelovu paletu v configu tento příkaz nemění.
- `get` vrací výsledný styl daného rozsahu, jeho skupiny, revizi, vlastníka
  a příznaky přepsání. Mutace navíc vracejí `changed`.
- `faces` přijímá `offset >= 0` a `limit` od 1 do 10 000 (výchozí 2 000).
  Vrací seřazený seznam unikátních dvojic a celkový počet.

## Společné potvrzení a GUI

`prepare_appearance_edit` připraví vlastníka a jeho původní nastavení.
`commit_appearance` ověří revizi a identitu otevřeného dokumentu, rozsah,
platnost stylů i nově přiřazené plochy. Zastaralý návrh se odmítne před
změnou stavu. Dříve uložená chybějící plocha se zachová při nesouvisející
úpravě stylu; novou neexistující plochu nelze přiřadit.

GUI náhled je přechodný. OK potvrzuje společnou operací, Cancel vrátí původní
zobrazení. Změna barvy zachovává přesnou drsnost a kovový charakter, pokud
se jejich posuvníky nezměnily. Konzole má samostatné oznámení změny vzhledu,
aby obnovila styly View bez zbytečného sestavování celé scény.

Soubor uživatelské palety se zapisuje jen po změně jejího obsahu. Vybraný
styl a všechny skupiny jsou vždy úplně uloženy v `.prtz` nebo `.asmz`;
paleta není nutná pro znovuotevření dokumentu. Formát ani start šablony se
nemění. Všechny nové texty mají překlady v pěti jazykových souborech configu.

## Ověření

Výchozí test selhal na chybějícím `appearance.faces` (0/1, 0,12 s).
První modelová sada a katalog prošly 2/2 za 0,53 s. Regrese kontrolují
šest původně vypočtených výsledných ploch kvádru, nezměněný objem, dotazy
bez změny alokace cache, skupiny, atomické odmítnutí chyb, historii,
oddělené instance a nativní uložení. Rozšíření doplňuje zastaralý návrh,
neaktivní těleso, dědění neuloženého zdroje, skutečný CLI proces a GUI
OK/Cancel se zachováním přesnosti.

Samostatná regrese odhalila chybějící mapování vlastníků výsledných ploch
zrcadleného tělesa (0/1). Mapování nyní zahrnuje i odvozená tělesa a používá
se stejně při vykreslení, vložení zdroje i obnovení dat v Assembly. Kontrola
následného obarvení původně chybně očekávala aktivní zdrojové těleso, přestože
Mirror po vytvoření aktivaci ukončuje. Opravený test porovnává stav těsně před
a po obarvení a samostatně ověřuje sdílení stejného snímku geometrie.
Rozšířený modelový test prošel **1/1 za 0,20 s** včetně zavřeného zdrojového
Partu. Log: `build/appearance-scope-tests.log`. Po úplném překladu obou aplikací a testovacích programů prošla **celá sada
122/122 za 531,19 s**. Obsahuje modely, skutečný CLI proces, GUI konzoli
(55,43 s), vizuální kontrakt vzhledu, sestavy, start aplikace a překlady.
Log: `build/appearance-full-tests.log`. Katalog má **218 příkazů**.
