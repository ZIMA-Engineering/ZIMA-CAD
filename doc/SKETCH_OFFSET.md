# Offset ve skicáři

## Ovládání

Příkaz **Offset** je v pravé nabídce vedle Ořezu a Zrcadlení. Otevírá
**Vlastnosti offsetu**: vlastní křivka skici, kladná vzdálenost a **Flip**.
Lze použít předvýběr nebo pole aktivovat a vybrat křivku ve View.
Fialový náhled a šipka od začátku křivky ukazují stranu odsazení. Šipka má
minimální velikost na obrazovce, takže nezmizí při malém odsazení.

Stejný dialog se otevírá dvojklikem na výslednou křivku nebo přes Vlastnosti
ve View a stromu. OK vytvoří/upraví jednu revizi; Zrušit neukládá náhled.
Sdílený PropertiesSubWindow zajišťuje také potvrzení dvojklikem MMB nad View.
Krátké MMB ukončí zadávání reference a její dočasnou inspekci.
Volba **Osvobodit** při OK odstraní vazbu a ponechá aktuální nativní geometrii.

## Vlastnictví a ořez

Offset odkazuje výhradně na vlastní křivku téže skici. Externí hranu je
nejprve nutné promítnout do vlastní křivky. Tato křivka může mít externí
návaznost; offset žádnou samostatnou externí referenci nevytváří.

Podkladová křivka a ponechaný interval jsou oddělené. Ořez zdroje zachovává
celý podklad i jeho identitu. Existující offset proto ořezem zdroje neztratí
svůj tvar. Nový offset vytvořený z již ořezané křivky převezme její vybraný
interval. Ořez offsetu mění ponechaný interval, nikoli jeho podporující tvar.
Při rozdělení má každý viditelný zbytek vlastní stabilní ID. Zbytky jednoho
offsetu sdílejí identitu operace a vzdálenost/Flip se upraví společně.

Průsečíkové konce uchovávají identitu protínající křivky a parametry stejné
větve. Malé změny se sledují lokálním numerickým řešením. Ztracený průsečík
nebo přesun mimo sledovanou větev zachová poslední tvar a označí návaznost
jako neplatnou. Neplatné křivky jsou červené; výpočet profilu je odmítne.

Závislé výsledky nemají volně posuvný řídicí polygon. Body jsou v řešiči
chráněné a ve View se nenabízejí vnitřní řídicí body. Osvobození ponechá
pole bodů, uzlů, vah a aktuální identitu křivky. Navazující offsety se při
osvobození ořezaného zdroje přeparametrizují, aby se znovu neořízly. Pokud
využívají i skrytou část mimo osvobozovaný úsek, je nejprve nutné osvobodit
je; příkaz takovou ztrátu podkladu odmítne.

## Geometrie

Vstupy jsou úsečky, kružnice, oblouky, elipsy, eliptické oblouky a B-spline
včetně racionálních STEP projekcí, periodických a interpolačních spline.
Výpočet pracuje v lokální rovině skici bez OCCT. OCCT se používá až při
explicitním modelování tělesa a v nezávislých geometrických testech.

Podklady používají racionální B-spline se zachovanými uzly a vahami.
Ořez provádí přesné vložení uzlů a rozdělení. Úsečky a kruhové oblouky
se odsadí přesně; ostatní pravidelné křivky používají adaptivní kubické
úseky proti matematickému offsetu, s uloženou tolerancí 0,00001 mm.
Tolerance je nezávislá na jemnosti trojúhelníků pro zobrazení importu.
Výpočet odmítne neurčitou tečnu, lokální obrácení u hrotu a případy,
kde kontrola aproximace nesplní toleranci.

První příkaz pracuje s jednou křivkou. Automatické spojování řetězců,
rohové spojnice a automatický výběr větví smyček nepřidává; spojnice
se kreslí ručně. Velké změny průsečíků mohou vyžadovat opravu ořezu.

## Ukládání

Sketch 33 ukládá `curve_supports`, `curve_trims` a `offsets`, včetně intervalů,
průsečíkových vazeb a poslední vypočtené nativní geometrie. Vše zůstává
uvnitř `.prtz`, `.asmz`, `.drwz`; žádné povinné doprovodné soubory.
Aktuální verze: Part 19 / JSON 43, Assembly 16 / JSON 25,
Drawing 15 / JSON 7. Šablony v config a testovací dokumenty se aktualizují
společně. Běžné načítání nepřevádí staré formáty.

## Ověření

Numerické testy pokrývají přesný ořez racionální spline, obě strany offsetu,
periodickou křivku, změnu zdroje, zachování podkladu po ořezu, průsečíkový
konec, ztrátu protínající křivky, ochranu bodů, osvobození a uložení/načtení.
Kernelový test ověřuje návaznost po ořezu a změně STEP projekce a přesný
objem vytaženého mezikruží z kružnice a jejího offsetu. Uzavřená přesná spline
má vlastní uzavřenost, nezávislou na periodické parametrizaci. Kružnice zůstávají analytickými kružnicemi i při sestavení profilu; kontrola
nezamění malé mezery 0,001/0,0001 mm za dotyk polygonů. Obrysy se třídí
podle vzájemného vnoření, takže odsazení může být otvorem i vnějším obrysem.

Tento test odhalil nepřesnou integraci objemu v OCCT pro racionální vytaženou
plochu. Pro B-spline/Bezier a plochy vytažení/rotace proto objem používá
adaptivní Gaussovu–Kronrodovu integraci se zohledněním uzlových úseků.
Geometrie se touto změnou nemění.

Audit prvních 100 spline hran souboru `63113_0H030_mg___773WF0593_01.stp`
zkusil XY/XZ/YZ a odsazení ±0,1 mm. Z 600 případů bylo přijato 587.
Na 1025 parametrických místech každého přijatého výsledku byla největší
odchylka od nezávislé pozice a tečny OCCT 0,00000247619 mm.
Odmítnuto: 9 hrotů, 2 singularity tečny a 2 nesplněné tolerance.
Jde o bodové ověření tohoto vzorku, nikoli formální důkaz pro všechny křivky.

Závěrečný Windows Release build dne 2026-09-11: všech 48 CTest testů
prošlo (418,32 s). Integrační test hlavního okna ověřil výběr vlastní
křivky, Flip, náhled, potvrzení MMB dvojklikem, opětovné otevření dvojklikem,
změnu vzdálenosti, Cancel a uložení/načtení. Vzhled dialogu a fialové šipky
byl zkontrolován také ze snímku skutečného View.
