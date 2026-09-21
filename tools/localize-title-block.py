"""Generate ZE title-block language variants using the native text outline builder."""
import argparse
import configparser
import json
import io
from pathlib import Path
import subprocess
import tempfile

LABELS = {
    'POLOTOVAR': ['STOCK', 'HALBZEUG', 'BRUT', 'ЗАГОТОВКА'],
    'položky': ['item', 'Position', 'repère', 'позиции'],
    'Číslo': ['Number', 'Nummer', 'Numéro', 'Номер'],
    'VÝKRES – NORMA': ['DRAWING – STANDARD', 'ZEICHNUNG – NORM', 'PLAN – NORME', 'ЧЕРТЕЖ – СТАНДАРТ'],
    'MATERIÁL': ['MATERIAL', 'WERKSTOFF', 'MATIÈRE', 'МАТЕРИАЛ'],
    '[ks]': ['[pcs]', '[Stk]', '[pcs]', '[шт.]'],
    'Hmotnost:': ['Mass:', 'Masse:', 'Masse :', 'Масса:'],
    'Množství:': ['Quantity:', 'Menge:', 'Quantité :', 'Кол-во:'],
    'Formát:': ['Format:', 'Format:', 'Format :', 'Формат:'],
    'Promítání:': ['Projection:', 'Projektion:', 'Projection :', 'Проекция:'],
    'Přesnost:': ['Accuracy:', 'Genauigkeit:', 'Précision :', 'Точность:'],
    'Drsnost povrchu:': ['Surface roughness:', 'Oberflächenrauheit:', 'Rugosité de surface :', 'Шероховатость:'],
    'Hrany:': ['Edges:', 'Kanten:', 'Arêtes :', 'Кромки:'],
    'Měřítko:': ['Scale:', 'Maßstab:', 'Échelle :', 'Масштаб:'],
    'Tolerování:': ['Tolerancing:', 'Tolerierung:', 'Tolérancement :', 'Допуски:'],
    'Kreslil:': ['Drawn by:', 'Gezeichnet:', 'Dessiné par :', 'Разработал:'],
    'Název:': ['Title:', 'Benennung:', 'Désignation :', 'Наименование:'],
    'Schválil:': ['Approved by:', 'Freigegeben:', 'Approuvé par :', 'Утвердил:'],
    'Dne:': ['Date:', 'Datum:', 'Date :', 'Дата:'],
    'Chráněno podle ISO 16016': ['Protected per ISO 16016', 'Geschützt nach ISO 16016', 'Protégé selon ISO 16016', 'Защищено по ISO 16016'],
    'Číslo výkresu:': ['Drawing number:', 'Zeichnungsnummer:', 'Numéro de plan :', 'Номер чертежа:'],
    'List: &sheet.position': ['Sheet: &sheet.position', 'Blatt: &sheet.position', 'Feuille : &sheet.position', 'Лист: &sheet.position'],
    'NÁZEV – OZNAČENÍ': ['NAME – DESIGNATION', 'NAME – BEZEICHNUNG', 'NOM – DÉSIGNATION', 'НАИМЕНОВАНИЕ'],
}


def read(path):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(path, encoding='utf-8')
    return ini, json.loads(ini['Sketch']['Data'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = root / 'config/formats/ZE-RAZITKO.tblz'
    original = source.read_bytes()
    _, sketch = read(source)
    unchanged = {'[Kg]', 'ZIMA-Engineering', 'www.zima-engineering.cz',
                 'e-mail: vladimir.zima@zima-engineering.cz', 'tel.: +420 774 206 965'}
    for text in sketch['texts']:
        value = text['value']
        if not value.startswith('&') and value not in LABELS and value not in unchanged:
            raise ValueError('Add translations for the new title-block label: ' + value)
    for index, language in enumerate(('cs', 'en', 'de', 'fr', 'ru')):
        target = source.with_stem(source.stem + '-' + language)
        operations = [{'command': 'sketch.geometry.delete', 'arguments': {'geometry': text['id']}}
                      for text in sketch['texts'] if text['value'] == 'ZIMA-Engineering']
        for text in sketch['texts']:
            if language != 'cs' and text['value'] in LABELS:
                operations.append({'command': 'sketch.text.set', 'arguments': {
                    'text': text['id'], 'value': LABELS[text['value']][index - 1]}})
        commands = [{'command': 'template.open', 'arguments': {'path': str(source)}}]
        if operations:
            commands.append({'command': 'template.sketch.edit', 'arguments': {'operations': operations}})
        if not sketch['drawing_template']['images']:
            commands.append({'command': 'template.image.create', 'arguments': {
                'path': str(source.with_name('ZIMA-Engineering.svg')),
                'x_mm': 189, 'y_mm': 22.5, 'height_mm': 7, 'lock_aspect': True}})
        with tempfile.TemporaryDirectory(prefix='zima-title-block-') as temporary:
            generated = Path(temporary) / target.name
            commands.append({'command': 'template.save', 'arguments': {
                'path': str(generated), 'copy': True}})
            script = Path(temporary) / 'commands.jsonl'
            script.write_text('\n'.join(json.dumps(c, ensure_ascii=False) for c in commands), encoding='utf-8')
            result = subprocess.run([str(args.cli.resolve()), '--working-directory', str(root),
                                     '--script', str(script)], capture_output=True, encoding='utf-8')
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            ini, translated = read(generated)
        ini['TitleBlock']['Locale'] = language
        ini['TitleBlock']['Name'] = 'ZE RAZITKO ' + language.upper()
        translated['drawing_template']['sections']['TitleBlock'].update(dict(ini['TitleBlock']))
        ini['Sketch']['Data'] = json.dumps(translated, ensure_ascii=False, separators=(',', ':'))
        output = io.StringIO()
        ini.write(output)
        target.write_text(output.getvalue().rstrip() + '\n', encoding='utf-8')
        # Everything except text glyphs/labels and locale must retain its identity.
        for key in ('points', 'segments', 'arcs', 'circles', 'constraints', 'dimensions'):
            assert translated[key] == sketch[key], (language, key)
        after_texts = {text['id']: text for text in translated['texts']}
        for before in sketch['texts']:
            if before['id'] == 'p126:text':
                assert before['id'] not in after_texts
                continue
            after = after_texts[before['id']]
            assert before['id'] == after['id']
            if before['value'].startswith('&'):
                assert {k: v for k, v in before.items() if k != 'contours'} == {
                    k: v for k, v in after.items() if k != 'contours'}, (language, before['id'])
            if before['value'] != after['value']:
                assert after['contours'] and before['contours'] != after['contours']
        print(target.name)
    assert source.read_bytes() == original, 'Source template was changed'


if __name__ == '__main__':
    main()
