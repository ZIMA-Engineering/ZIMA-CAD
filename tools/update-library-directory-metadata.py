"""Generate repository-owned library labels and symbol artwork, never user data."""
from pathlib import Path
import uuid

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = ('cs', 'en', 'de', 'fr', 'ru')
LABELS = {
 'formats': ('Formáty výkresů','Drawing formats','Zeichnungsformate','Formats de dessin','Форматы чертежей'),
 'templates': ('Šablony','Templates','Vorlagen','Modèles','Шаблоны'),
 '02-NATIVE': ('Nativní knihovna','Native library','Native Bibliothek','Bibliothèque native','Нативная библиотека'),
 'materials': ('Materiály','Materials','Werkstoffe','Matériaux','Материалы'),
 '01_steels': ('Oceli','Steels','Stähle','Aciers','Стали'),
 '02_nonferrous_metals': ('Neželezné kovy','Non-ferrous metals','Nichteisenmetalle','Métaux non ferreux','Цветные металлы'),
 '03_cast_irons': ('Litiny','Cast irons','Gusseisen','Fontes','Чугуны'),
 '04_plastics': ('Plasty','Plastics','Kunststoffe','Plastiques','Пластмассы'),
 '05_glass_ceramics': ('Sklo a keramika','Glass and ceramics','Glas und Keramik','Verre et céramiques','Стекло и керамика'),
 '06_construction': ('Stavební materiály','Construction materials','Baustoffe','Matériaux de construction','Строительные материалы'),
 'stainless': ('Korozivzdorné oceli','Stainless steels','Nichtrostende Stähle','Aciers inoxydables','Коррозионностойкие стали'),
 'structural': ('Konstrukční oceli','Structural steels','Baustähle','Aciers de construction','Конструкционные стали'),
 'aluminum': ('Hliník a jeho slitiny','Aluminium and its alloys','Aluminium und seine Legierungen','Aluminium et ses alliages','Алюминий и его сплавы'),
 'copper_and_alloys': ('Měď a její slitiny','Copper and its alloys','Kupfer und seine Legierungen','Cuivre et ses alliages','Медь и её сплавы'),
 'magnesium': ('Hořčík a jeho slitiny','Magnesium and its alloys','Magnesium und seine Legierungen','Magnésium et ses alliages','Магний и его сплавы'),
 'titanium': ('Titan a jeho slitiny','Titanium and its alloys','Titan und seine Legierungen','Titane et ses alliages','Титан и его сплавы'),
 'brass': ('Mosazi','Brasses','Messing','Laitons','Латуни'),
 'bronze': ('Bronzy','Bronzes','Bronze','Bronzes','Бронзы'),
 'copper': ('Měď','Copper','Kupfer','Cuivre','Медь'),
 'engineering_plastics': ('Technické plasty','Engineering plastics','Technische Kunststoffe','Plastiques techniques','Конструкционные пластмассы'),
 'polyamides': ('Polyamidy','Polyamides','Polyamide','Polyamides','Полиамиды'),
 'polyolefins': ('Polyolefiny','Polyolefins','Polyolefine','Polyoléfines','Полиолефины'),
 'concrete': ('Beton','Concrete','Beton','Béton','Бетон'),
 'symbols': ('Symboly','Symbols','Symbole','Symboles','Обозначения'),
 'annotations': ('Popisy','Annotations','Beschriftungen','Annotations','Надписи'),
 'drawing-conventions': ('Značky kreslení','Drawing conventions','Zeichnungskonventionen','Conventions de dessin','Условные обозначения на чертежах'),
 'general': ('Obecné značky','General symbols','Allgemeine Symbole','Symboles généraux','Общие обозначения'),
 'geometric-tolerances': ('Geometrické tolerance','Geometrical tolerances','Geometrische Toleranzen','Tolérances géométriques','Геометрические допуски'),
 'machine-elements': ('Strojní součásti','Machine elements','Maschinenelemente','Éléments de machines','Детали машин'),
 'sheetm': ('Plechové díly','Sheet metal','Blechbearbeitung','Tôlerie','Листовые детали'),
 'surface-texture': ('Struktura povrchu','Surface texture','Oberflächenbeschaffenheit','État de surface','Текстура поверхности'),
 'welding': ('Svařování','Welding','Schweißen','Soudage','Сварка'),
 '01-SHEETMETAL': ('Plechové díly','Sheet metal','Blechbearbeitung','Tôlerie','Листовые детали'),
 '01-FORM': ('Tvarové prvky','Form features','Formelemente','Éléments de forme','Формообразующие элементы'),
}
ARTWORK = {
 'annotations': '<path d="M18 49 32 15 46 49M23 37h18M12 55h40"/>',
 'drawing-conventions': '<path d="M12 23h22l12 7v12l-12 7H12zM12 23v26M34 23v26"/><circle cx="52" cy="36" r="8"/>',
 'general': '<path d="M12 20h40v24H12zM22 20v24M42 20v24M16 52h32"/>',
 'geometric-tolerances': '<rect x="8" y="22" width="48" height="22" rx="2"/><path d="M25 22v22M43 22v22M13 33h7M16.5 29v8"/><circle cx="34" cy="33" r="6"/>',
 'machine-elements': '<path d="M22 13h20l7 10-7 10H22l-7-10zM26 33v22h12V33M26 40h12M26 47h12"/>',
 'sheetm': '<path d="M11 48h27V16h7v39H11zM16 48v7M38 20h7"/>',
 'surface-texture': '<path d="m12 30 10 15 18-30h16M33 25h20M41 15v10"/>',
 'welding': '<path d="M10 44h46M18 44V25l18 19M34 25h16M42 20v10"/>',
}

FORMAT_NAMES = {'ZE-TITLE-BLOCK.tblz': {'cs': 'Razítko ZIMA-Engineering', 'en': 'ZIMA-Engineering title block', 'de': 'ZIMA-Engineering Schriftfeld', 'fr': 'Cartouche ZIMA-Engineering', 'ru': 'Основная надпись ZIMA-Engineering'}, 'ZE-DRAWING-FRAME.frmz': {'cs': 'Výkresový rámeček ZIMA-Engineering', 'en': 'ZIMA-Engineering drawing frame', 'de': 'ZIMA-Engineering Zeichnungsrahmen', 'fr': 'Cadre de dessin ZIMA-Engineering', 'ru': 'Рамка чертежа ZIMA-Engineering'}}
PARAMETER_LABELS = {'cs': 'Název', 'en': 'Name', 'de': 'Name', 'fr': 'Nom', 'ru': 'Название'}

def main():
    roots = [ROOT/'config/formats', ROOT/'config/templates', ROOT/'config/lib/02-NATIVE', ROOT/'config/materials', ROOT/'config/symbols', ROOT/'config/lib/01-SHEETMETAL']
    directories=[]
    for root in roots:
        directories.append(root)
        directories.extend(p for p in root.rglob('*') if p.is_dir() and '0000-index' not in p.parts)
    for directory in directories:
        if directory.name not in LABELS: raise ValueError(f'Unreviewed directory: {directory}')
        index=directory/'0000-index';index.mkdir(exist_ok=True)
        lines=['[Directory]','Version=3','Kind=directory',f'Id={uuid.uuid5(uuid.NAMESPACE_URL,directory.relative_to(ROOT).as_posix())}','PreventRemoval=true','PreventParameterEdit=false','AutoIndex=true','SubdirectoriesAsParts=true']
        for language,label in zip(LANGUAGES,LABELS[directory.name]):lines.append(f'Label\\{language}={label}')
        if directory.parent==ROOT/'config/symbols':
            lines.extend(['LogoMode=logo-text','Logo=logo-text.svg'])
            svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect x="3" y="3" width="58" height="58" rx="10" fill="#4AA3E8" fill-opacity=".12"/><g fill="none" stroke="#428BCC" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">'+ARTWORK[directory.name]+'</g></svg>\n'
            (index/'logo-text.svg').write_text(svg,encoding='utf-8')
        if directory==ROOT/'config/formats':
            lines.insert(7,'Parameters=name')
            lines.extend(['','[Parameters]'])
            for language,label in PARAMETER_LABELS.items():lines.append(f'name\\Label\\{language}={label}')
            lines.extend(['','[Parts]'])
            for filename,values in FORMAT_NAMES.items():
                identifier=Path(filename).stem;lines.append(f'{identifier}\\name={values["en"]}')
                for language,value in values.items():lines.append(f'{identifier}\\name\\{language}={value}')
            lines.extend(['','[Files]'])
            for filename in FORMAT_NAMES:
                identifier=Path(filename).stem;lines.extend([f'{identifier}\\Provider=zima-cad',f'{identifier}\\Path={filename}'])
        (index/'metadata.ini').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'{len(directories)} factory directories; {len(directories)*5} localized labels; 8 SVG category logos')

if __name__=='__main__':main()
