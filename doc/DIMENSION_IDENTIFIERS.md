# Identifikace kót v dokumentu

Každá kóta dostává vedle stávajícího interního ID neměnné označení `d1`,
`d2`, … Jedna číselná řada patří jednomu dokumentu Part, Assembly nebo Drawing.
Výkres sdílí jednu řadu mezi všemi listy. Vložený díl si ponechává vlastní
řadu; rozměry jeho umístění a vazeb patří do řady bezprostředně vlastnící sestavy.

Číslo se přiděluje při potvrzení změny dokumentu. Hodnota nula, potlačení,
neviditelnost ani neaktivní volitelný rozměr přidělení nebrání. Katalog zahrnuje
kóty skic, rohové poloměry, rozměrové parametry funkcí, umístění, odsazení vazeb
včetně zadaných mezí, body a poloměry 3D křivek, vložené skici Sweepů a výkresové
kóty. Rozpoznání kóty nezávisí na její číselné hodnotě ani na kreslení ve View.

Změna hodnoty, názvu nebo pořadí objektu nemění jeho identifikátory.
Přidělená čísla zůstávají rezervovaná i po smazání a přes Undo/Redo, včetně
vytvoření nové větve historie. Cancel nezapisuje rozpracované kóty do registru.

## Datový kontrakt

`DimensionIdentifiers` je samostatný registr metadat dokumentu. Klíčem je
existující dvojice vlastníka a sémantického klíče kóty. Registr uchovává přidělená
čísla i následující volné číslo. Ukládá se spolu s dokumentem; duplicity čísel,
prázdné identity a neplatná číselná řada se při načtení odmítají.

Rozměrové parametry umístění jsou identifikovány svými stávajícími parametrovými
sloty. Vazba Assembly používá stávající identitu
`(assembly_id, placement-reference:occurrence_id:row_index)`; změna reference
nebo hodnoty v tomto slotu zachovává jeho označení. Registr nezavádí druhý objekt
vazby, neřeší umístění a nemění vlastnictví či výpočet vazeb.

Číslování nevytváří geometrii a nevolá OCCT. Katalog obsahuje pouze identity;
čísla nejsou součástí identit topologie ani geometrického výpočtu.

## Rozhraní

- Vlastnosti skicové kóty obsahují pole **Identifikace kóty** jen pro čtení.
- Okno **Relace** obsahuje přehled označení, objektů a parametrů, včetně nulových
  kót. Interní identita je dostupná v nápovědě buňky.
- Přímý editor hodnoty ve View poskytuje označení v nápovědě.
- Výběr výkresové kóty ukáže její označení ve stavovém řádku.
- Vyhodnocování výrazů s `dN` zatím není implementováno.

Aktuální INI verze: Part 14, Assembly 12, Drawing 12. Starší formáty nemají
kompatibilní načítací větev. Dodané šablony a testovací dokumenty používají
aktuální schéma.
