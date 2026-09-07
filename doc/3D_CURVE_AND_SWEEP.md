# 3D křivka a Sweep/Loft

3D křivka uchovává původní konstrukční body s trvalými ID. Čísla v tabulce
jsou pořadí v dráze, nikoli identita pro reference.

U lomené čáry zapíná **Zaoblení rohů** sloupec **R [mm]**. Vypnutý sloupec
zobrazuje nuly a nelze jej upravovat; uložené hodnoty se nemění. První a poslední
bod otevřené dráhy nemají zaoblení. Rádius 0 zachovává ostrý roh. Kladný rádius
vytvoří tečný kruhový oblouk v rovině sousedních úseček. Délka odříznutá z každé
úsečky je `R * tan(úhel změny směru / 2)`. Překrytí sousedních zaoblení a obrat
180° jsou neplatné. Přímé pokračování další oblouk nevytváří.

Sweep sestavuje tabulku profilů automaticky:

- 1: začátek dráhy, kolmo na první úsečku.
- N.1: konec předchozího úseku; kolmo na něj. Při zaoblení začátek oblouku.
- N.2: začátek následujícího úseku; kolmo na něj. Při zaoblení konec oblouku.
- Poslední N.2: konec dráhy, kolmo na poslední úsečku.

U interpolační spline používají výstupní stanice její tečnu. Vstupní stanice
jsou neaktivní. Parametry zaoblení se uchovávají pro návrat na lomenou čáru.

**Sketch** vytvoří nebo otevře skicu stanice. Profil je uložen podle ID původního
bodu a role vstup/výstup. Změna pořadí bodů nebo rádiusu nemění identitu skici.
Neaktivní vstupní profil zůstává uložený, ale nevstupuje do výpočtu tělesa.
Smazání původního bodu smaže jeho profily v téže nepotvrzené transakci.
První stanice musí mít vlastní neprázdný profil. Další prázdné stanice
přebírají poslední zadaný profil až do konce dráhy, včetně jeho pořadí bodů.
Přechod k dalšímu zadanému profilu proběhne na úseku, který v něm končí.

Při vypnutém **Zaoblení rohů** má každá úsečka dvě vlastní stanice. Všechny
skici jsou přístupné, včetně N.1. Mezi N.1 a N.2 nevzniká žádná spojovací
geometrie. Každý úsek má kolmá čela a vypočítá se jako sweep se stejnými
profily nebo loft s rozdílnými profily. Průniky úseků se sjednotí; nevytváří
se šikmé společné čelo na půlící rovině rohu. Párování obvodů se kontroluje
uvnitř každého úseku, nikoliv mezi dvěma oddělenými čely v rohu.

Se zapnutým zaoblením zůstává souvislé tažení přes oblouky. Nulový rádius
v tomto režimu zachovává původní společný průřez ostrého rohu. Spline také
zůstává souvislou dráhou. Obrysy musí být uzavřené a bez děr.

Čela nezaoblených úseků mají názvy například **Úsek 1 → 2.1 — konec** a
**Úsek 2.2 → 3.2 — začátek**. Identita používá ID obou původních bodů a roli
začátek/konec; přečíslování bodů nemění existující reference. Úplné původní
čelo se ukládá do referenčního paketu i tehdy, když jeho viditelný zbytek
po sjednocení zanikne. U kruhových profilů paket uchovává původní střed,
směr a poloměr pro Vrtací špičku. Oříznutá část čela odkazuje na tentýž konec.

Úpravy zůstávají návrhem až do OK společného dialogu. Cancel neukládá změny.
Náhled dráhy a umístění skic používají ZIMA data a analytickou geometrii;
OCCT se používá při explicitním výpočtu tělesa. Sdílené umísťování kontejnerů
se nemění.

Samostatná 3D trajektorie EXP je odstraněna včetně editoru a serializace.
Starý experimentální formát se nepřevádí.

Regrese: `zima_cpp_curve3d_sweep_contract_tests`, `zima_cpp_ui_contract_tests`
a obecné `zima_cpp_contract_tests`.

## Oblouky a kóty radiusů ve View

Zaoblovací oblouky jsou součástí zobrazení celé dráhy, při editaci i po jejím
potvrzení. Úseky a oblouky používají společné vykreslování a výběr. Dráha je
viditelná také u výsledného 3D Sweepu.

Kóty `R…` se zobrazují jen při otevřených vlastnostech nebo při běžném zobrazení
parametrických kót dvojklikem na kontejner. Zavření vlastností a ukončení režimu
kót prostředním tlačítkem je skryje. Samotné oblouky zůstávají viditelné.
Střed, bod na oblouku a rovina kóty vycházejí ze stejné analytické dráhy;
umístění respektuje uložený posun a natočení objektu. Změna radiusu ve
vlastnostech mění náhled. Vypnuté zaoblení, nulový radius, přímý průchod bodem
a spline kótu zaoblení nevytvářejí. Zobrazení nevolá OCCT.


### Prázdné profily 3D Sweepu

První bod dráhy musí mít vyplněnou skicu profilu. Následující body bez
skici nebo s prázdnou skicou přebírají poslední vyplněný profil ve směru
dráhy. Nová vyplněná skica se stane zdrojem pro další prázdné body.
Neaktivní stanice zdroj nemění. Převzetí se vyhodnocuje při výpočtu,
nevytváří nezávislé kopie skic; úprava zdroje se tedy projeví i dále.
Tabulka uvádí zdroj ve sloupci „Použitý profil“. Rozkreslená neuzavřená
kontura se nepovažuje za prázdnou a při výpočtu vyvolá chybu.

## Párování obvodů profilů

Za zeleným tlačítkem **Sketch** je **Pořadí bodů**. Volba prvního bodu je
uložena jeho trvalým ID v `Sweep3DProfile.correspondence_start_point_id`.
Další body následují cyklicky po obvodu. Mezi sousedními profily se spojuje
1 → 1, 2 → 2 atd.; rozdílný počet bodů výpočet odmítne. Převzaté stanice
nemají nezávislé pořadí, upravuje se jejich zdrojová skica.

View během vlastností zobrazuje všechny aktivní profily včetně převzatých.
Při otevření profilové skici se náhledové obrysy a jejich párovací značky
skryjí, aby se nepřekrývaly s editovatelnou geometrií Sketcheru. Dráha zůstává
jako kontext. Dokončení skici náhled obnoví; vymazání jejího obrysu u další
stanice obnoví převzetí předchozího profilu.
První párovací bod má popisek **1 – začátek**, ostatní pořadové číslo.
Změna začátku aktualizuje pouze náhled; Cancel ji vrátí a až OK vlastností
Sweepu provede výpočet a změnu dokumentu.

Kružnice bez bodů používají podél dráhy přenášenou orientaci švu, aby rozdílná
lokální orientace skic nezpůsobila samovolné zkroucení a zúžení. Jeden bod
s vazbou C na každé kružnici dovoluje řídit šev a pootočení. Více bodů rozdělí
kružnici na přesné oblouky; jejich identity vycházejí ze zdrojové kružnice
a dvojic bodů. Kružnice → obdélník proto vyžaduje čtyři body na kružnici
odpovídající čtyřem rohům obdélníku.

U neoznačených profilů OCCT ThruSections používá kontrolu kompatibility,
u explicitního párování zachovává pořadí stanovené ZIMA daty. Podrobnosti API:
[OCCT ThruSections](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___thru_sections.html).
Párovací data jsou součástí aktuálního Part formátu 14.

## Rozpracovaný kontejner ve stromu

Během vytváření kontejneru nahrazuje položku „Vložit zde“ jeho dočasná
položka se zeleným písmem. Zobrazuje aktuální lokální počátek, dráhu, body
a profily; zůstává dostupná i při otevřeném editoru bodu nebo skici.
Stejná prezentace platí pro kontejnery v Partu i Assembly. Při editaci se
aktualizuje existující položka a ukazatel vložení se dočasně skryje.

Kliknutí na celý počátek nadřazeného kontejneru vyplní bodu jeho tři
polohové reference stejně jako kliknutí na počátek dokumentu. Samostatný
výběr jednotlivých rovin zůstává dostupný. Vlastní počátek ani vlastní
podřízené prvky nejsou platnou referencí pro umístění jejich rodiče.

Zobrazení stromu čte rozpracovaná ZIMA data; nevkládá je do uložené historie
ani nevyvolává výpočet tělesa. OK potvrdí transakci, Cancel odstraní její
náhled. Po zavření vlastností se obnoví běžné zobrazení „Vložit zde“.
