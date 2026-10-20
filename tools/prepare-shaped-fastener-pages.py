"""Add reviewed shaped-fastener tables with localized native reference art."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,groups):
    data['labels']['wheel_nuts_title']=['Kolové matice','Wheel nuts','Radmuttern','Écrous de roue','Колёсные гайки']
    data['pages'].append(dict(path='05-NUTS/12-WHEEL',title='wheel_nuts_title',headers=['symbol','meaning'],rows=[],notes=[],sources=[],sections=[]))
    titles={
        'flange':['Matice s přírubou','Flange nuts','Flanschmuttern','Écrous à embase','Фланцевые гайки'],
        'spherical':['Matice s kulovým nákružkem','Nuts with spherical collar','Muttern mit Kugelbund','Écrous à collerette sphérique','Гайки со сферическим буртиком'],
        'conical':['Matice s kuželovým sedlem','Nuts with conical seat','Muttern mit Kegelbund','Écrous à portée conique','Гайки с коническим седлом'],
        'washer':['Kulové pružné podložky','Spherical spring washers','Kugelbund-Federringe','Rondelles élastiques sphériques','Сферические пружинные шайбы']}
    notes={
        'flange':[
            'Jmenovitý CAD model DIN ≈6923, pouze hrubé stoupání M5–M20. Dosedací ozubení je podle zadání vynecháno. Horní sražení šestihranu 15°, ústí závitu 90°, sklon horní strany příruby 40°. Zvolené dolní zaoblení (dc−dw_min)/4 a horní zaoblení c/5 jsou přímo ve skice. Přechod šestihranu do příruby je ostrý; tabulkové r_max je horní mez, nikoliv předepsaný nenulový poloměr. Pozinkování a tolerance se nemodelují; nejde o úplný výrobní předpis ani rozměrově shodnou náhradu ISO 4161.',
            'Nominal DIN approximately 6923 CAD model, coarse pitch M5–M20 only. Seating serrations are omitted as requested. Upper hexagon bevel 15°, thread mouth 90°, flange roof slope 40°. Selected lower round (dc−dw_min)/4 and upper round c/5 are authored directly in the Sketch. The hexagon/flange junction is sharp; tabulated r_max is an upper limit, not a required nonzero radius. Zinc coating and tolerances are not modeled; this is not a complete manufacturing specification or a dimensionally identical ISO 4161 replacement.',
            'Nennmaß-CAD-Modell DIN ungefähr 6923, nur Regelgewinde M5–M20. Die Auflageverzahnung entfällt auf Wunsch. Obere Sechskantfase 15°, Gewindemündung 90°, Flanschoberseite 40°. Gewählte untere Rundung (dc−dw_min)/4 und obere Rundung c/5 sind direkt in der Skizze definiert. Der Übergang vom Sechskant zum Flansch ist scharf; r_max ist eine Obergrenze, kein vorgeschriebener Radius ungleich null. Verzinkung und Toleranzen werden nicht modelliert; keine vollständige Fertigungsvorschrift und kein maßgleicher Ersatz für ISO 4161.',
            'Modèle CAO nominal DIN approximativement 6923, pas gros M5–M20 uniquement. La denture d’appui est omise sur demande. Chanfrein supérieur de l’hexagone 15°, entrée de filetage 90°, pente supérieure d’embase 40°. Arrondis choisis inférieur (dc−dw_min)/4 et supérieur c/5 définis dans l’esquisse. Le raccord hexagone/embase est vif ; r_max est une limite supérieure, pas un rayon non nul imposé. Zingage et tolérances non modélisés ; ce n’est ni une spécification complète de fabrication ni un remplacement dimensionnellement identique d’ISO 4161.',
            'Номинальная модель САПР DIN приблизительно 6923, только крупный шаг M5–M20. Опорные насечки исключены по заданию. Верхняя фаска шестигранника 15°, вход резьбы 90°, уклон верха фланца 40°. Выбранные скругления нижнее (dc−dw_min)/4 и верхнее c/5 заданы прямо в эскизе. Переход шестигранника во фланец острый; r_max — верхняя граница, а не обязательный ненулевой радиус. Цинкование и допуски не моделируются; это не полное производственное предписание и не размерно идентичная замена ISO 4161.'],
        'spherical':[
            'DIN 74361, část 2, vydání 1982: doložená kolová matice A M20×1,5, s = 27 mm. Skutečný kulový nákružek R18, spodní odlehčení Ø20,5 do hloubky b = 3 mm, navázaný průchozí závit. Horní sražení 30° je zvoleno z přípustného rozsahu; ústí závitu 90°. Spodní rovina nákružku je na Z = 0, osa v počátku, výška do +Z. Jde o jmenovitý CAD model se schematickým závitem, bez simulace povlaku a výrobních tolerancí.',
            'DIN 74361 Part 2, 1982 edition: evidenced type A M20×1.5 wheel nut, s = 27 mm. Real R18 spherical collar, lower Ø20.5 clearance to depth b = 3 mm and dependent through thread. Upper bevel 30° is selected within the permitted range; thread mouth 90°. The collar bottom datum is Z = 0, its axis passes through the Origin and height extends in +Z. Nominal CAD model with schematic thread; coating and manufacturing tolerances are not simulated.',
            'DIN 74361 Teil 2, Ausgabe 1982: belegte Radmutter A M20×1,5, s = 27 mm. Echter Kugelbund R18, unterer Freistich Ø20,5 bis Tiefe b = 3 mm und davon abhängiges Durchgangsgewinde. Obere Fase 30° aus dem zulässigen Bereich gewählt; Gewindemündung 90°. Unteres Bund-Bezugsniveau Z = 0, Achse durch den Ursprung, Höhe in +Z. Nennmaß-CAD-Modell mit schematischem Gewinde; Beschichtung und Fertigungstoleranzen werden nicht simuliert.',
            'DIN 74361 partie 2, édition 1982 : écrou de roue A M20×1,5 attesté, s = 27 mm. Collerette sphérique réelle R18, dégagement inférieur Ø20,5 jusqu’à b = 3 mm et filetage traversant lié. Chanfrein supérieur 30° choisi dans la plage admise ; entrée de filetage 90°. Référence inférieure de collerette Z = 0, axe à l’origine, hauteur vers +Z. Modèle CAO nominal à filetage schématique ; revêtement et tolérances de fabrication non simulés.',
            'DIN 74361, часть 2, издание 1982 года: подтверждённая колёсная гайка A M20×1,5, s = 27 мм. Реальный сферический буртик R18, нижняя проточка Ø20,5 глубиной b = 3 мм и связанная сквозная резьба. Верхняя фаска 30° выбрана в допустимом диапазоне; вход резьбы 90°. Нижняя базовая плоскость буртика Z = 0, ось через начало координат, высота вдоль +Z. Номинальная модель САПР с условной резьбой; покрытие и производственные допуски не моделируются.'],
        'conical':[
            'DIN 74361, část 2, vydání 1982: matice F M12×1,5 se dvěma samostatnými vrcholovými úhly sedla 60° a 90°. d3 je koncový průměr kužele, d4 průměr spodního ústí. Horní sražení 30° je zvolený CAD detail. Schematický průchozí závit; každý úhel má vlastní nativní soubor a identitu. Spodní rovina Z = 0, osa v počátku, výška do +Z. Nezaměňovat s typem A ani s jiným vydáním normy.',
            'DIN 74361 Part 2, 1982 edition: type F M12×1.5 nuts with separately identified 60° and 90° included seat angles. d3 is the cone end diameter; d4 is the lower mouth diameter. Upper bevel 30° is a selected CAD detail. Schematic through thread; each angle has its own native file and identity. Bottom datum Z = 0, axis through the Origin, height in +Z. Do not interchange with type A or another standard edition.',
            'DIN 74361 Teil 2, Ausgabe 1982: Muttern F M12×1,5 mit getrennten Kegelsitz-Öffnungswinkeln 60° und 90°. d3 ist der Kegelenddurchmesser, d4 der untere Mündungsdurchmesser. Die obere Fase 30° ist ein gewähltes CAD-Detail. Schematisches Durchgangsgewinde; jeder Winkel hat eine eigene native Datei und Identität. Untere Bezugsebene Z = 0, Achse im Ursprung, Höhe in +Z. Nicht mit Form A oder einer anderen Normausgabe austauschen.',
            'DIN 74361 partie 2, édition 1982 : écrous F M12×1,5 à angles inclus de portée 60° et 90°, identifiés séparément. d3 est le diamètre du bout conique ; d4 celui de l’entrée inférieure. Le chanfrein supérieur 30° est un détail CAO choisi. Filetage traversant schématique ; chaque angle possède son fichier natif et son identité. Référence inférieure Z = 0, axe à l’origine, hauteur vers +Z. Ne pas confondre avec la forme A ou une autre édition de la norme.',
            'DIN 74361, часть 2, издание 1982 года: гайки F M12×1,5 с отдельными углами конусного седла 60° и 90°. d3 — диаметр конца конуса, d4 — диаметр нижнего входа. Верхняя фаска 30° — выбранная деталь САПР. Условная сквозная резьба; у каждого угла свой нативный файл и идентификатор. Нижняя базовая плоскость Z = 0, ось в начале координат, высота вдоль +Z. Не заменять типом A или изделием по другому изданию стандарта.'],
        'washer':[
        "Kulové pružné podložky C podle DIN 74361 části 2 z roku 1982, v dotaženém stavu. Nabízené nativní otvory: 12,5 / 14,5 / 18,5 / 20,5 / 22,5 mm; rozměry jsou uvedeny v tabulce. Zvolený rovnoběžný řez 45° s mezerou 1 mm a dvě zaoblení R0,1 jsou CAD detaily. Skutečná osa v Body sleduje výšku. Hmotnost se počítá z geometrie s hustotou 7 850 kg/m³. Volný pružný stav ani předpětí se nesimulují. Tabulka uvádí skutečné výchozí CAD rozměry. Pro otvor 12,5 mm je zvoleno R12,5 a h6 = 6 mm (jmenovitě R12 a h6 = 5 mm); pro 14,5 mm R14,5 a h6 = 6 mm (jmenovitě R14). Volby leží v mezích normy R ±0,5 mm a h6 +1/0 mm.",
        "Type C spherical spring washers from DIN 74361 Part 2, 1982, in the compressed, tightened condition. Offered native bores: 12.5 / 14.5 / 18.5 / 20.5 / 22.5 mm; dimensions are listed in the table. Selected parallel 45° split with a 1 mm gap and two R0.1 rounds are CAD details. A real native Body axis follows height. Mass is calculated from geometry at 7,850 kg/m³. Free spring shape and preload are not simulated. The table lists actual initial CAD dimensions. Bore 12.5 mm uses selected R12.5 and h6 = 6 mm (nominal R12 and h6 = 5 mm); bore 14.5 mm uses R14.5 and h6 = 6 mm (nominal R14). These choices lie within the standard limits R ±0.5 mm and h6 +1/0 mm.",
        "Kugelbund-Federringe C nach DIN 74361 Teil 2 von 1982 im zusammengedrückten, angezogenen Zustand. Angebotene native Bohrungen: 12,5 / 14,5 / 18,5 / 20,5 / 22,5 mm; Maße stehen in der Tabelle. Gewählter paralleler 45°-Schnitt mit 1 mm Spalt und zwei Rundungen R0,1 sind CAD-Details. Eine echte native Body-Achse folgt der Höhe. Masse aus der Geometrie bei 7.850 kg/m³. Freie Federform und Vorspannung werden nicht simuliert. Die Tabelle enthält die tatsächlichen CAD-Ausgangsmaße. Bohrung 12,5 mm verwendet R12,5 und h6 = 6 mm (Nennwerte R12 und h6 = 5 mm); Bohrung 14,5 mm verwendet R14,5 und h6 = 6 mm (Nennwert R14). Die Auswahl liegt innerhalb der Normgrenzen R ±0,5 mm und h6 +1/0 mm.",
        "Rondelles élastiques sphériques C selon DIN 74361 partie 2 de 1982, à l’état comprimé et serré. Alésages natifs proposés : 12,5 / 14,5 / 18,5 / 20,5 / 22,5 mm ; dimensions dans le tableau. Coupe parallèle à 45° avec jeu de 1 mm et deux arrondis R0,1 choisis comme détails CAO. Un véritable axe natif du Body suit la hauteur. Masse calculée sur la géométrie à 7 850 kg/m³. Forme libre et précharge non simulées. Le tableau indique les dimensions CAO initiales réelles. L’alésage 12,5 mm utilise R12,5 et h6 = 6 mm (valeurs nominales R12 et h6 = 5 mm) ; l’alésage 14,5 mm utilise R14,5 et h6 = 6 mm (rayon nominal R14). Ces choix respectent les limites de la norme R ±0,5 mm et h6 +1/0 mm.",
        "Сферические пружинные шайбы C по DIN 74361, часть 2, 1982 года, в сжатом, затянутом состоянии. Предлагаемые нативные отверстия: 12,5 / 14,5 / 18,5 / 20,5 / 22,5 мм; размеры приведены в таблице. Параллельный разрез 45° с зазором 1 мм и два скругления R0,1 — выбранные детали САПР. Настоящая нативная ось Body следует высоте. Масса рассчитана по геометрии при 7 850 кг/м³. Свободная форма пружины и преднатяг не моделируются. В таблице указаны фактические исходные размеры САПР. Для отверстия 12,5 мм выбраны R12,5 и h6 = 6 мм (номинальные R12 и h6 = 5 мм); для 14,5 мм — R14,5 и h6 = 6 мм (номинальный R14). Выбор находится в пределах стандарта R ±0,5 мм и h6 +1/0 мм."]}
    for kind,models,art in groups:
        rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants']
        figures=json.loads((art/'illustrations.json').read_text(encoding='utf-8'))
        key='shaped_'+kind;data['labels'][key]=titles[kind];data['labels'][key+'_scope']=notes[kind]
        standard=rows[0]['standard'];data['labels'][key+'_heading']=[standard]*5
        source=key+'_source';data['sources'][source]=dict(label='Aspen Fasteners · DIN 6923 nominal dimensions' if kind=='flange' else 'DIN 74361 Part 2 · original 1982 edition',url=rows[0]['source_url'])
        path=('06-WASHERS/' if kind=='washer' else '05-NUTS/')+rows[0]['family'];assert not any(p['path']==path for p in data['pages'])
        if kind=='flange':fields=[('P','pitch_mm'),('s','across_flats_mm'),('h','height_mm'),('dc','flange_diameter_mm'),('c min','flange_edge_height_mm'),('dw min','minimum_bearing_diameter_mm'),("m' min",'minimum_wrench_height_mm'),('r max','maximum_root_radius_mm')]
        elif kind=='spherical':fields=[('P','pitch_mm'),('s','across_flats_mm'),('h1','height_mm'),('d2','flange_diameter_mm'),('d3','end_diameter_mm'),('d4','clearance_diameter_mm'),('h2','collar_height_mm'),('b','clearance_depth_mm'),('R','sphere_radius_mm')]
        elif kind=='conical':fields=[('P','pitch_mm'),('s','across_flats_mm'),('h5','height_mm'),('d3','end_diameter_mm'),('d4','clearance_diameter_mm'),('α','included_angle_degrees')]
        else:fields=[('d7','outside_mm'),('d8','transition_diameter_mm'),('h6','height_mm'),('R','sphere_radius_mm')]
        headers=[key+'_d'];data['labels'][headers[0]]=['d6 [mm]' if kind=='washer' else 'd']*5
        for i,(symbol,_) in enumerate(fields):
            label=key+'_col_'+str(i);headers.append(label);data['labels'][label]=[symbol+(' [°]' if symbol=='α' else ' [mm]')]*5
        table=[]
        for v in rows:
            first=format(v['bore_mm'],'g') if kind=='washer' else 'M'+format(v['diameter_mm'],'g')+('×'+format(v['pitch_mm'],'g') if kind in ('spherical','conical') else '')
            table.append([first,*[format(v[field],'.6g') for _,field in fields]])
        pics=[]
        for figure in figures:
            stem=Path(figure['part']).stem;caption=key+'_art_'+str(figure.get('angle_degrees',0))
            data['labels'][caption]=[standard+(f' · {figure["angle_degrees"]:g}°' if figure.get('angle_degrees') else '')+' — '+text for text in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
            pics.append(dict(path=stem+'.png',caption=caption,localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES}))
        data['pages'].append(dict(path=path,title=key,heading=key+'_heading',headers=headers,rows=table,notes=['units',key+'_scope'],sources=[source],sections=[],
            freeze_header=True,localized_illustrations=True,figures=pics,figures_side_by_side=False))
    return data


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for kind in ('flange','spherical','conical','washer'):
        parser.add_argument('--'+kind+'-models',type=Path,required=True);parser.add_argument('--'+kind+'-art',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    groups=[(kind,getattr(args,kind+'_models'),getattr(args,kind+'_art')) for kind in ('flange','spherical','conical','washer')]
    args.output.write_text(json.dumps(prepare(data,groups),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
