# Vazby ve Skicáři

Příkaz Vazby je tlačítko otevírající společné interní okno. Název a značka
spouštějí původní ruční příkaz. Přepínač Automaticky určuje, které vztahy smějí
vznikat při zadávání nové geometrie. Výchozí nastavení má všechny automatické
vazby zapnuté. Nastavení platí pro aktuální okno aplikace; existující vazby se
nemění. Fixovat bod zůstává ruční příkaz.

OK přijme nastavení, Cancel ho zahodí. Ruční volba vazby nechá úzké okno
otevřené a spustí opakovaný příkaz. Aktivní vazba i tlačítko Vazby jsou zelené.
Ukončení příkazu nebo přechod na jiný nástroj odstraní zvýraznění řádku.
OK (také prostředním dvojklikem nad View) ukončí zadávání a zavře okno;
potvrzení poskytuje společný PropertiesSubWindow. Již vytvořené ruční vazby
jsou samostatné operace skici, Cancel zahazuje pouze nastavení automatických vazeb.

Volné nově zadávané body a počátky geometrie odvozují vodorovnost a svislost
od všech uložených bodů skici. Stejná funkce se používá pro náhled i potvrzení;
tolerance je v pixelech. Druhý konec úsečky používá existující nabídku variant
inference, nově filtrovanou nastavením Automaticky. Geometrické definice
obdélníku ani již omezené body na oblouku se tím nemění.

Značky vazeb nesou přesné sémantické identity účastníků. Výběr v Tree nebo
View zvýrazňuje tyto body, křivky či osy azurově. Automatická inference
zvýrazňuje své podpory oranžově. Používá se pouze geometrie Skicáře a data
Vieweru, bez OCCT výpočtu a bez přebarvení celého objektu.

Vodorovnost a svislost přijímají v prvním kroku buď celou úsečku (značka
uprostřed), nebo první bod dvojice. Stejnost přijímá také falešné poloměry
rohů. Solver svazuje jejich uložené hodnoty, respektuje řídicí kótu a vazba
se ukládá se skicou; náhled používá stejnou identitu rohu.
