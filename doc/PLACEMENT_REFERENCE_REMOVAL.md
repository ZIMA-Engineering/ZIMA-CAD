# Odebrání referencí umístění přes GUI a CLI

Příkaz `placement.reference.remove` používá stejnou datovou operaci řádků
jako křížek ve společné sekci Umístění kontejneru. Změnu potvrzuje existující
transakce daného objektu; nerozepisuje dokument obcházením jeho validace.

```json
{"command":"placement.reference.remove","arguments":{"object":"ID_OBJEKTU","index":0}}
```

`object` je stabilní ID objektu v aktivním dokumentu. Volitelný `document`
chrání před změnou aktivního dokumentu. Indexy 0–2 znamenají poziční reference,
3 FRONT a 4 TOP. Chybné indexy se odmítají, již prázdné pole je beze změny.
Výsledek obsahuje `document`, `object`, `index`, `changed`,
`body_calculated` a novou `revision`. Názvy příkazů zůstávají anglické;
nápověda a chybová hlášení jsou lokalizované.

## Podporované objekty

| Objekt | ID předané příkazu | Potvrzení |
| --- | --- | --- |
| Těleso Partu | ID tělesa | Body Properties a výpočet |
| Bod, osa, rovina nebo samostatná 3D křivka v Partu/Assembly | ID konstrukce | Konstrukční vlastnosti, bez výpočtu těles |
| Bod samostatné 3D křivky | Vlastní ID bodu | Konstrukční transakce, místní rám rodiče |
| Kvádr a ostatní primitiva | ID kontejneru | Příslušné vlastnosti a výpočet |
| Vytažení a rotace Partu | ID kontejneru | Profilová transakce včetně vlastní skici |
| Profilový odečet Assembly | ID odečtu | Výpočet vlastního odečtu a zachování zdrojových Partů |
| Sweep2D, Sweep3D a šroubové tažení | ID kontejneru | Společná transakce tažení |
| Bod vložené dráhy Sweep3D | Vlastní ID bodu | Jedno potvrzení rodičovského tažení |
| Hole a současný Otvor | ID kontejneru | Příslušná transakce otvoru |
| Importované těleso | ID kontejneru | Vlastnosti importu, zachovaný uložený zdroj |
| Řez Partu/Assembly | ID řezu | Sekční transakce bez výpočtu těles |

Vložené komponenty Assembly mají vlastní správu vazeb. U vložené dráhy
se umístění celého tažení mění přes ID jeho kontejneru, nikoli přes ID
kořene vlastněné dráhy. Vlastnictví, aktivní těleso a ochrana odvozených
těles zůstávají součástí příslušných transakcí.

## Řádky a orientace

V rozpracovaném GUI zůstane po smazání pozičního řádku mezera; následující
řádky se nepřesunou. Zámek smazaného řádku se uvolní. Pokud stejnou geometrii
obsahuje i orientační řádek, odstraní se tato spárovaná orientace. Párování
zahrnuje vlastníka, geometrický klíč i celou cestu výskytu.

Po odstranění spárované orientace se zbývající orientace přeznačí FRONT/TOP
podle dosavadního pravidla GUI. Inspekční zvýraznění zůstává na stejné
přeživší referenci. Přímé smazání FRONT nebo TOP jinou orientaci neposouvá
ani nemaže odpovídající poziční referenci.

Při potvrzení se prázdné poziční řádky filtrují stejně jako doposud.
Index dalšího CLI příkazu tedy odpovídá aktuálním uloženým pozičním
referencím. Nejde o zavedení trvalých prázdných slotů ani změnu formátu.

## Transakce a chybějící zdroj

Odebrání nevyžaduje dohledat geometrii právě odstraňovaného zdroje.
Lze tak opravit neplatnou referenci. Zbývající reference a výsledek musí
projít běžnou validací; neplatný návrh nemění dokument ani Undo historii.

Jedna skutečná změna je jeden krok Undo/Redo. Prázdný řádek nepřidá krok
historie. Rozpracované vlastnosti blokují současný zápis přes konzoli.
GUI Cancel zahodí návrh; u bodu uvnitř křivky OK dítěte upraví pouze návrh
rodiče a až OK rodiče potvrzuje dokument.

Všechna data zůstávají v `.prtz`, `.asmz` a `.drwz` a jejich běžných
nativních závislostech. Tato změna nemění formát ani startovní šablony.

## Ověření

Cílená doménová sada prošla **8/8 za 8,64 s**. Zahrnuje šest primitiv,
Part/Assembly konstrukce, těleso s měřeným posunem geometrie, všechna tažení,
opravu chybějícího zdroje bodu vložené dráhy, Hole/varianty Otvoru, importy,
profily Partu, profilové odečty Assembly a řezy obou dokumentů. Kontroluje
skutečné objemy, plochy řezů, zachování zdrojových Partů, atomické chyby,
Undo/Redo a nativní uložení. Log: `build/reference-removal-domain-tests.log`.

Následující sada prošla **6/6 za 169,70 s**, včetně skutečného procesu CLI,
katalogu, obecných GUI kontraktů, zámků na Windows a konzole s okny vlastností.
GUI Cancel/OK, dvojí potvrzení bodu/rodiče a Undo/Redo jsou ověřené.
Tentýž model uložený po odstranění z GUI a CLI má shodnou nativní definici
konstrukce i primitiva. Log: `build/reference-removal-gui-tests.log`.

Po kontrole callbacků je doplněno předání změněného inspekčního zvýraznění
do View po obnově náhledu a před opětovným zadáváním reference.
Závěrečná úplná regrese prošla **156/156 za 662,55 s**, bez chyb.
Obě aplikace a všechny testovací programy jsou sestavené.
Logy: `build/reference-removal-final-build.log` a
`build/reference-removal-full-tests.log`. Katalog obsahuje **289 příkazů**.
