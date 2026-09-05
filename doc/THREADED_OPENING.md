# Otvor s volitelným závitem

Příkaz **Otvor** používá společné Vlastnosti pro vytvoření i editaci
jednoho kontejneru. Typ otvoru vybírá hladký otvor, metrický ISO závit,
Whitworth BSW nebo válcový trubkový G závit. Hladký otvor má číselný průměr;
závit má katalogový rozměr a odvozený, případně ručně zadaný průměr předvrtání.
Použití je vždy vnitřní. Vnější závity nejsou součástí tohoto příkazu.

- Hloubka otvoru určuje konec válcové části. Otvor může končit také na cíli
  **Až k** nebo projít **Skrz vše**.
- Délka závitu se měří od lokálního počátku otvoru ke konci válce závitu.
  Výběh o délce zadaného násobku stoupání pokračuje za touto délkou.
- Volitelné sražení má osovou hloubku a vrcholový úhel (výchozí 90°).
  Ořízne závitovou plochu, ale neposune počátek měření ani konec délky závitu.
- Volitelná vrtací špička pokračuje za zadanou válcovou hloubkou slepého
  otvoru. Pro Až k a Skrz vše se nepřidává.

Výpočet při OK provede postupně odečet profilu otvoru vytažením, volitelný
odečet špičky rotací, vytvoření technologické závitové plochy a nakonec
odečet vstupního sražení rotací. Poslední odečet ořízne solid i technologické
plochy. Vše tvoří jedinou hranici historie. Profilové skici a jejich identity
se ukládají do dokumentu; náhled a kóty používají ZIMA data bez výpočtu OCCT.
Cancel neukládá rozpracované změny.

Otvor vlastní trvalou referenční osu `axis:primary`. Je viditelná i mimo
Vlastnosti a ukládá se s vypočteným modelem. Vede od lokálního počátku
po konec válcové části otvoru; špička ani vstupní sražení její délku nemění.
Původní samostatný příkaz Otvor již nabídka nástrojů neobsahuje.

Nový otvor má špičku i sražení předem zapnuté. Analytický drátový náhled
používá kružnice a jednu spojovací úsečku pro každou válcovou či kuželovou
plochu. Zvýraznění kontejneru vychází z uloženého obrysu spojeného řezného
tělesa; nezobrazuje zaniklé kružnice mezi mezikroky vrtání a sražení.

Spojovací čáry všech ploch leží ve stejné podélné polorovině určené osou
otvoru a jeho lokálním radiálním směrem. Shodně ji používá analytický
náhled i kruhový profil při explicitním výpočtu OCCT.

Změna katalogu předává náhledu až kompletní rozměr. Přepnutí na závit
podle potřeby prodlouží otvor s pevnou hloubkou tak, aby pojal délku
závitu včetně výběhu. Nový otvor má výchozí hloubku 20 mm.
Hladký otvor nepoužívá katalog; návrat k závitu zachová zvolený rozměr.

Pro výkresové řezy je dostupný příznak `FaceReference::is_thread_surface()`
na referencích trojúhelníků zobrazené i původní geometrie. Vychází z uložené
role `thread:surface:nominal`, zachovává se po oříznutí i uložení a načtení.
Výběh, stěna předvrtání, sražení a špička tento příznak nemají. Zjištění
nevyžaduje OCCT ani duplicitní údaj oddělený od významu dané plochy.

Kóty v otevřených vlastnostech mění pouze rozpracované parametry: hloubka
mění otvor, délka závitu mění válcovou část závitu měřenou od počátku.
Průměr předvrtání má skutečnou číselnou hodnotu a kotví na válci za sražením.
U závitu je tato kóta pouze informativní (`driving=false`, role
`measurement:bore_diameter`); u hladkého otvoru je editovatelná. Označení
např. M10 kotví na jmenovitém válci závitu; vyvolání editace otevře existující
výběr katalogového rozměru ve vlastnostech. Do modelu se změny zapíší až OK.

Zapnuté sražení nabízí hloubku a vrcholový úhel, zapnutá špička slepého
otvoru nabízí vrcholový úhel. Úhlové kóty používají existující druh Skicáře `AngleSymmetric` (A–B–A)
a jeho společné vykreslení. Dočasný osový řez obsahuje jednu povrchovou
přímku a osu; druhou stranu zrcadlí sama symetrická kóta. Nejde o další
editovatelnou skicu ani druhý zdroj parametrů. Parametry
se mění přes stejné ovládací prvky vlastností. Zobrazení nic nepočítá v OCCT.
Tyto kóty používají společný datový typ ViewerDimension; vlastní přenos
kót otvoru do výkresu je následná práce.

Katalog vyvolaný z kóty čeká na uvolnění levého tlačítka dvojkliku, aby
jej koncová událost hned nezavřela. Informativní průměr předvrtání používá
barvu měřených kót Skicáře; jeho odkazová čára míří na opačnou stranu
než označení závitu, takže se popisky nepřekrývají ani v osovém pohledu.

Dvojklik na označení závitu v režimu Edit otevře samostatný inline katalog.
Volba položky explicitně přepočítá a uloží změnu a vrátí pohled ke kótám;
zavření seznamu nic nemění. Vlastnosti se při tom neotevírají. Když jsou
Vlastnosti už otevřené, katalog mění jejich rozpracované parametry až do OK.
Oba vstupy používají stejný zdroj katalogových dat.

Informativní průměr má značku ⌀ a zůstává dostupný pro inspekci; jeho
dvojklik nespouští editaci. U závitu kotví na válcové části pod vstupem,
aby se nepletl s kótou sražení. Integrační test ověřuje hnědé pixely ve
3D obrazu, návrat ke kótám po volbě M12 i zrušení další volby bez změny.

Vlastnosti otvoru nemají pole Text kóty ani vlastní přepis označení.
Závitová kóta používá označení z katalogu a průměr díry skutečnou hodnotu.

Volba Až k přebírá vstup z referencí umístění pomocí stejné obsluhy cíle
jako Vytažení. Vypne aktivní řádek umístění i automatický přechod na další
řádek; existující hodnoty umístění zachová. Po výběru cíle či změně zpět
na Délku zůstává výběr umístění vypnutý až do explicitního kliknutí na něj.
Hloubka ke šikmé rovině vychází z průsečíku osy s rovinou. Integrační test
prochází výběr skutečné plochy, OK, uložení reference i kontrolu objemu.

U Až k (stejně jako Skrz vše) je špička nejen vynechaná z výpočtu, ale
i odškrtnutá a vypnutá ve vlastnostech. Po návratu na pevnou délku ji lze
znovu zapnout. Pomocné vytažení ke šikmé rovině pokrývá celý kruhový
profil před oříznutím, nikoli jen jeho jediný vrchol na švu.

Délka závitu má vlastní zakončení Délka / Až k a samostatnou referenci na rovinu nebo rovinnou plochu. Až k ořezává nominální závitovou plochu cílovou rovinou i při jejím sklonu, nevytváří výběh a skrývá jeho nastavení i číselnou délku závitu. Hloubka a zakončení předvrtání zůstávají nezávislé. Náhled používá analytický průsečík; OCCT ořez probíhá až při výpočtu.

Následné sražení a zaoblení hrany zachovávají technologické závitové plochy v každé hranici historie. Odečítají z nich pouze objem odebraný danou úpravou hrany; vlastník a příznak nominálního závitu zůstávají zachované. Regresní test průchozího otvoru ověřuje i polohu konce závitové plochy na výstupním sražení.

Tree rozepisuje kontejner Otvor na základní Otvor a zapnutý Závit, Sražení a Špičku. Kliknutí vybírá azurově pouze existující hrany odpovídajícího podprvku; vazba používá uložené rodiče profilů a přesnou cestu výskytu. Kontextové Edit zobrazuje ve View jen kóty tohoto podprvku. Vlastnosti otevírají společný dialog otvoru. Delete je pouze u volitelných podprvků a vypíná jejich parametr s výpočtem a Undo. Odebrání závitu přepíná otvor na hladký při zachování průměru předvrtání; Tree pak používá původní ikonu válcového otvoru. Vypnuté prvky lze obnovit ve Vlastnostech.

Stejný výběrový mechanismus ve Tree používají Sražení a Zaoblení: kontejner obsahuje trasy a sbalené segmenty. Jejich azurový drát pochází z uložené hranice před operací. Delete odebírá výběr hrany, nikoli geometrii zdrojového tělesa. Rozdělené souvislé části tvoří samostatné trasy, počátky R1 se odvozují z uložených koncových bodů původní trasy. Delete poslední trasy odstraní kontejner standardní undoovatelnou cestou.
