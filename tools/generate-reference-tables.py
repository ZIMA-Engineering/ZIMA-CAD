"""Generate localized factory engineering reference pages (no CAD runtime dependency)."""
import argparse
import html
import hashlib
import json
import os
import re
import uuid
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
ROOT = REPOSITORY / "config/lib/03-ENGINEERING-TABLES/01-GENERAL-STANDARDS/01-UNITS"
DATA = REPOSITORY / "tools/data/unit-tables.json"
LANGUAGES = ("cs", "en", "de", "fr", "ru")


def table_blocks(page):
    """A logical reference can contain several readable column groups."""
    yield page
    yield from page.get('sections', [])


def diagram(kind):
    # Inline vector geometry inherits the reference page's actual text color.
    if kind == 'taper':
        drawing = '<path d="M35 70 L255 22 L255 142 L35 94 Z M15 82 H275 M35 156 H255"/>'
        text = '<text x="20" y="78">d</text><text x="265" y="87">D</text><text x="138" y="174">L</text><text x="219" y="69">α</text>'
    else:
        drawing = '<path d="M35 82 L255 32 L255 112 L35 112 Z M35 127 H255 M35 82 V112 M255 32 V112"/>'
        text = '<text x="15" y="100">h</text><text x="263" y="73">H</text><text x="138" y="148">L</text><text x="207" y="94">β</text>'
    return '<svg class="reference-diagram" viewBox="0 0 300 185" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.5">' + drawing + '</g><g fill="currentColor" font-family="Arial,sans-serif" font-size="16">' + text + '</g></svg>'


def load_data(source=DATA):
    data = json.loads(source.read_text(encoding="utf-8"))
    common = json.loads((DATA.parent / 'reference-ui.json').read_text(encoding='utf-8'))
    assert common['languages'] == list(LANGUAGES)
    data['labels'] = {**common['labels'], **data['labels']}
    assert data["languages"] == list(LANGUAGES)
    paths = {page["path"] for page in data["pages"]}
    assert len(paths) == len(data["pages"]) and "" in paths
    for key, values in data["labels"].items():
        assert len(values) == 5 and all(isinstance(v, str) and v.strip() for v in values), key
    for page in data["pages"]:
        path = page["path"]
        assert not path or re.fullmatch(r"[A-Z0-9]+(?:[-.][A-Z0-9]+)*(?:/[A-Z0-9]+(?:[-.][A-Z0-9]+)*)*", path), path
        assert not path or str(Path(path).parent).replace("\\", "/") in paths | {"."}, path
        for key in [page["title"], *page["headers"], *page["notes"]]:
            assert key in data["labels"], key
        for block in table_blocks(page):
            assert all(key in data['labels'] for key in block['headers'])
            if block.get('heading'):
                assert block['heading'] in data['labels']
            for row in block["rows"]:
                assert len(row) == len(block["headers"]), path
                for cell in row:
                    assert isinstance(cell, str) or (set(cell) == {"key"} and cell["key"] in data["labels"]), cell
        assert all(source in data["sources"] for source in page["sources"])
        if page.get('review'):
            assert page['review'] in data['labels'], page['path']
        for figure in page.get('figures', []):
            assert figure['caption'] in data['labels'], page['path']
            root = REPOSITORY / data.get('root', str(ROOT.relative_to(REPOSITORY)))
            target = (root / page['path'] / '0000-index' / figure['path']).resolve()
            assert target.is_relative_to(REPOSITORY) and target.is_file(), figure['path']
    return data


def localized(data, key, language):
    return data["labels"][key][LANGUAGES.index(language)]


def mathematical(text, language):
    # Preserve mathematical data; change only decimal punctuation and typography.
    if language == "en":
        text = re.sub(r"(?<=\d),(?=\d)", ".", text)
    else:
        text = re.sub(r"(?<=\d)\.(?=\d)", ",", text)
    escaped = html.escape(text)
    escaped = re.sub(r"\^(-?\d+)", lambda m: "<sup>" + m[1].replace("-", "−") + "</sup>", escaped)
    escaped = re.sub(r"_([0-9]+)", r"<sub>\1</sub>", escaped)
    escaped = escaped.replace(' | ', '<br>')
    return escaped


def metadata(data, page):
    identifier = page.get("id") or ("94fca23e-1489-50b3-8754-8914d71186ad" if not page["path"] else str(
        uuid.uuid5(uuid.NAMESPACE_URL, "zima-cad:factory-reference-tables/" + data.get("root", str(ROOT.relative_to(REPOSITORY))) + "/" + page["path"])))
    lines = ["[Directory]", "Version=3", "Kind=directory", "Id=" + identifier, "PreventRemoval=true"]
    for language in LANGUAGES:
        label = localized(data, page["title"], language)
        if any(c in label for c in ',;"'):
            label = '"' + label.replace('"', '\\"') + '"'
        lines.append("Label\\" + language + "=" + label)
    # Directory Properties may have changed parameters or include settings.
    # Own only the generated identity/labels; retain unrelated native settings.
    root = REPOSITORY / data.get("root", str(ROOT.relative_to(REPOSITORY)))
    existing = root / page['path'] / '0000-index/metadata.ini'
    preserved = []
    section = 'Directory'
    if existing.is_file():
        for line in existing.read_text(encoding='utf-8-sig').splitlines():
            if line.startswith('[') and line.endswith(']'):
                section = line[1:-1]
                if section != 'Directory':
                    preserved.append(line)
            elif line.strip():
                key = line.split('=', 1)[0]
                owned = key in {'Version', 'Kind', 'Id', 'PreventRemoval'} or key.startswith('Label\\')
                if section != 'Directory' or not owned:
                    preserved.append(line)
    if not any(line.startswith('Parameters=') for line in preserved):
        lines.append('Parameters=@Invalid()')
    return "\n".join([*lines, *preserved, ""])


def render(data, page, language):
    tr = lambda key: html.escape(localized(data, key, language))
    depth = len(Path(page["path"]).parts) if page["path"] else 0
    root = REPOSITORY / data.get("root", str(ROOT.relative_to(REPOSITORY)))
    stylesheet = REPOSITORY / "config/lib/03-ENGINEERING-TABLES/0000-index/css/reference-tables.css"
    css = os.path.relpath(stylesheet, root / page['path'] / '0000-index').replace('\\', '/')
    navigation = []
    navigation.extend(f'<a lang="{lang}" hreflang="{lang}" href="index_{lang}.html"'
                      + (' aria-current="page"' if lang == language else '') + f'>{lang.upper()}</a>'
                      for lang in LANGUAGES)
    parts = ["<!doctype html>", f'<html lang="{language}">', '<head><meta charset="utf-8">',
             '<meta name="viewport" content="width=device-width, initial-scale=1">',
             '<meta name="color-scheme" content="light dark">',
             f'<title>{tr(page["title"])} · ZIMA-CAD</title>',
             f'<link rel="stylesheet" href="{css}"></head><body>', '<main>',
             '<nav aria-label="' + tr("section") + '">' + ' '.join(navigation) + '</nav>',
             f'<h1>{tr(page["title"])}</h1>']
    if page.get('review'):
        parts.append('<p class="review-status">' + tr(page['review']) + '</p>')
    if page["rows"]:
        for figure in page.get('figures', []):
            parts.append('<figure class="reference-figure"><img src="' + html.escape(figure['path'], quote=True)
                         + '" alt="' + tr(figure['caption']) + '"><figcaption>' + tr(figure['caption']) + '</figcaption></figure>')
        if page.get('diagram'):
            parts.append(diagram(page['diagram']))
        for block in table_blocks(page):
            if block.get('heading'):
                parts.append('<h2>'+tr(block['heading'])+'</h2>')
            parts += ['<div class="table-scroll"><table>', f'<caption>{tr(page["title"])}</caption>',
                      '<thead><tr>' + ''.join(f'<th scope="col">{tr(key)}</th>' for key in block["headers"]) + '</tr></thead><tbody>']
            for row in block["rows"]:
                cells = []
                for column, cell in enumerate(row):
                    tag = 'th scope="row"' if column == 0 else 'td'
                    ending = 'th' if column == 0 else 'td'
                    if isinstance(cell, dict):
                        value = tr(cell["key"])
                    else:
                        value = mathematical(cell, language)
                        tag += ' class="formula"'
                    cells.append(f'<{tag}>{value}</{ending}>')
                parts.append('<tr>' + ''.join(cells) + '</tr>')
            parts += ['</tbody></table></div>']
    else:
        parts.append('<ul class="topics">')
        for child in data["pages"]:
            if child is page or not child["path"]:
                continue
            parent = str(Path(child["path"]).parent).replace('\\', '/')
            if parent == (page["path"] or '.'):
                href = '../' + Path(child["path"]).name + f'/0000-index/index_{language}.html'
                parts.append(f'<li><a href="{href}">{tr(child["title"])}</a></li>')
        parts.append('</ul>')
    parts += ['<aside>' + ''.join(f'<p>{tr(key)}</p>' for key in page["notes"]) + '</aside>']
    links = [f'<a href="{html.escape(data["sources"][key]["url"], quote=True)}">'
             + html.escape(data["sources"][key]["label"]) + '</a>' for key in page["sources"]]
    parts += [f'<footer>{tr("sources")}: ' + ' · '.join(links) + f'<br>{tr("updated")} · ZIMA Engineering',
              f'<p>{tr("reference_notice")} <span>GPL-3.0-or-later</span></p></footer>',
              '</main></body></html>', '']
    return '\n'.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check generated HTML and metadata without writing')
    parser.add_argument('--data', type=Path, default=DATA, help='Reviewed localized table dataset')
    args = parser.parse_args()
    data = load_data(args.data)
    root = REPOSITORY / data.get("root", str(ROOT.relative_to(REPOSITORY)))
    # Reviewed legacy neutral-language pages would otherwise override the
    # automatic directory overview. Remove only the exact captured source file.
    for legacy in data.get('legacy_index_files', []):
        path = (root / legacy['path']).resolve()
        assert path.is_relative_to(root.resolve()) and path.name == 'index.html'
        assert path.parent.name == '0000-index'
        assert path.parent.parent.relative_to(root.resolve()).as_posix() in {p['path'] or '.' for p in data['pages']}
        if path.exists():
            if args.check:
                raise SystemExit(f'Unexpected legacy directory page: {path.relative_to(REPOSITORY)}')
            assert hashlib.sha256(path.read_bytes()).hexdigest() == legacy['sha256'], path
            path.unlink()
    outputs = {}
    for page in data['pages']:
        index = root / page['path'] / '0000-index'
        outputs[index / 'metadata.ini'] = metadata(data, page)
        for language in LANGUAGES:
            path = index / f'index_{language}.html'
            if page['rows']:
                outputs[path] = render(data, page, language)
            elif path.exists():
                if args.check:
                    raise SystemExit(f'Unexpected directory landing page: {path.relative_to(REPOSITORY)}')
                path.unlink()  # Only the known generated landing pages, never directory contents.
    for path, text in outputs.items():
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8') != text:
                raise SystemExit(f'Generated file differs: {path.relative_to(REPOSITORY)}')
        elif not path.exists() or path.read_text(encoding='utf-8') != text:
            # Leave unchanged files alone, including metadata currently being
            # read by the native File Manager.
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8', newline='\n')
    print(f'{len(data["pages"])} directories, {sum(bool(p["rows"]) for p in data["pages"]) * 5} localized HTML pages; '
          + ('checked' if args.check else 'generated'))


if __name__ == '__main__':
    main()
