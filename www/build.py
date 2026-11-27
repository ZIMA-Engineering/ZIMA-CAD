"""Build the public website and the identical local CAD guide pages."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import html
import json
import re
import shutil
import sys
import time
import uuid
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "www"
sys.path.insert(0, str(ROOT / "build/www-tools"))
try:
    import markdown
except ImportError:
    raise SystemExit("Install build dependencies: python -m pip install -r www/requirements.txt")


def ini(path):
    result = configparser.ConfigParser(interpolation=None)
    result.optionxform = str
    result.read(path, encoding="utf-8-sig")
    return result


def escape(value):
    return html.escape(str(value), quote=True)


def build(check=False):
    started = time.perf_counter()
    config = ini(WEB / "0000-index/metadata.ini")
    docs = ini(WEB / "0300-documentation/0000-index/metadata.ini")
    localization = ini(ROOT / "config/localization/metadata.ini")
    languages = dict(localization["Languages"])
    default = localization["Localization"]["DefaultLanguage"]
    translations = json.loads((WEB / "_source/translations.json").read_text(encoding="utf-8"))
    assert set(translations) == set(languages), "Website languages must match CAD"
    keys = set(translations["en"])
    for language, catalog in translations.items():
        assert set(catalog) == keys, f"Incomplete catalog: {language}"
        assert all(isinstance(value, str) and value.strip() for value in catalog.values())
    guides = dict(docs["Guides"])
    assert all(value in guides for value in docs["Commands"].values()), "Unknown command guide"
    links = dict(config["Links"])
    canonical = config["Website"]["Canonical"].rstrip("/")
    for url in [canonical, *links.values()]:
        assert urlsplit(url).scheme == "https", "Public links must use HTTPS"
    annotations_path = ROOT / "doc/guides/annotations.json"
    annotations = json.loads(annotations_path.read_text(encoding="utf-8")) if annotations_path.exists() else {}
    images = sorted((WEB / config["Website"]["Gallery"]).glob("*"), key=lambda p: p.name.casefold())
    images = [path for path in images if path.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp", ".mp4", ".webm"} and path.is_file() and ".poster." not in path.name]
    assert len({p.name.casefold() for p in images}) == len(images), "Gallery filename collision"
    outputs = {}

    def put(path, content):
        data = content.encode("utf-8") if isinstance(content, str) else content
        if Path(path).suffix in {".html", ".css", ".js", ".svg", ".ini", ".txt", ".xml"}:
            data = data.replace(b"\r\n", b"\n")
        outputs[str(path).replace("\\", "/")] = data

    def local(path, page):
        import posixpath
        return quote(posixpath.relpath(str(path), str(Path(page).parent).replace("\\", "/")), safe="/-_.")

    routes = {"home": "0000-index", "gallery": "0200-gallery/0000-index", "guides": "0300-documentation/0000-index", "download": "0400-download/0000-index"}

    def route(section, lang):
        return f"{routes[section]}/index_{lang}.html"

    def guide_route(slug, lang):
        return f"0300-documentation/{slug}/0000-index/index_{lang}.html"

    for name in ("site.css", "site.js"):
        put("0000-index/assets/" + name, (WEB / "_source" / name).read_bytes())
    for name in ("zc-dark.svg", "zc-light.svg", "app-icon.svg"):
        put("0000-index/assets/" + name, (ROOT / "resources/branding" / name).read_bytes())
    for path in images:
        put(path.relative_to(WEB).as_posix(), path.read_bytes())
    for entry in annotations.values():
        source = (ROOT / "doc/guides/images" / entry["image"]).resolve()
        assert source.is_relative_to(ROOT / "doc/guides/images") and source.is_file()
        put("0300-documentation/images/" + entry["image"], source.read_bytes())

    def metadata(labels, identity):
        return "[Directory]\nVersion=3\nKind=directory\nId=" + str(uuid.uuid5(uuid.NAMESPACE_URL, canonical + "/" + identity)) + "\n" + "".join(f"Label\\{lang}={label}\n" for lang, label in labels.items())

    for section in ("gallery", "download"):
        put(routes[section] + "/metadata.ini", metadata({lang: translations[lang][section] for lang in languages}, section))

    def shell(page, language, title, content, active="", article=False):
        t = translations[language]
        assets = local("0000-index/assets", page)
        navigation = "".join(f'<a href="{local(route(key, language), page)}"' + (' aria-current="page"' if key == active else '') + f'>{escape(t[key])}</a>' for key in routes)
        language_links = "".join(f'<a lang="{lang}" hreflang="{lang}" href="{escape(Path(page).name.replace("index_" + language, "index_" + lang))}"' + (' aria-current="true"' if lang == language else '') + f'>{escape(name)}</a>' for lang, name in languages.items())
        alternates = "".join(f'<link rel="alternate" hreflang="{lang}" href="{canonical}/{page.replace("index_" + language, "index_" + lang)}">' for lang in languages)
        return f'''<!doctype html>
<html lang="{language}" data-theme="dark"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{escape(title)} · ZIMA-CAD</title><meta name="description" content="{escape(t['intro'])}"><meta name="color-scheme" content="dark light">
<link rel="canonical" href="{canonical}/{page}">{alternates}<link rel="icon" href="{assets}/app-icon.svg" type="image/svg+xml">
<link rel="stylesheet" href="{assets}/site.css"><script defer src="{assets}/site.js"></script></head>
<body><a class="skip" href="#content">{escape(t['skip'])}</a>
<header class="site-header"><div class="header-inner"><a class="brand" href="{local(route('home', language), page)}" aria-label="ZIMA-CAD"><img class="logo-dark" src="{assets}/zc-dark.svg" alt="" width="82" height="39"><img class="logo-light" src="{assets}/zc-light.svg" alt="" width="82" height="39"><span>ZIMA-CAD</span></a>
<button class="menu-toggle" aria-expanded="false" aria-controls="main-nav" aria-label="{escape(t['menu'])}">☰</button><nav id="main-nav" aria-label="{escape(t['menu'])}">{navigation}</nav>
<div class="header-tools"><button class="theme-toggle" aria-label="{escape(t['theme'])}" title="{escape(t['theme'])}">◐</button><details class="languages"><summary aria-label="{escape(t['language'])}"><span aria-hidden="true">◎</span> {language.upper()}<span class="chevron"></span></summary><div>{language_links}</div></details></div></div></header>
<main id="content"{' class="guide-page"' if article else ''}>{content}</main>
<footer><div><a class="footer-brand" href="{local(route('home', language), page)}">ZIMA-CAD</a><span>{escape(t['footer'])}</span></div><div class="socials"><a href="{links['YouTube']}" rel="noopener">YouTube</a><a href="{links['Facebook']}" rel="noopener">Facebook</a><a href="{links['Repository']}" rel="noopener">GitHub</a></div><small>© 2026 ZIMA-Engineering · GPL-3.0-or-later</small></footer>
<dialog class="lightbox" aria-label="{escape(t['fullscreen'])}"><button class="lightbox-close" aria-label="{escape(t['close'])}">×</button><img alt=""><p></p></dialog></body></html>'''

    def caption(path, t):
        return t.get(config.get("GalleryCaptions", path.name, fallback=""), path.stem.replace("-", " ").replace("_", " "))

    def picture(path, page, t, eager=False):
        if path.suffix.lower() in {".mp4", ".webm"}:
            return f'<video class="media-video" src="{local(path.relative_to(WEB).as_posix(), page)}" muted playsinline loop controls preload="metadata" aria-label="{escape(caption(path,t))}"></video>'
        return f'<img src="{local(path.relative_to(WEB).as_posix(), page)}" alt="{escape(caption(path, t))}" loading="{"eager" if eager else "lazy"}" decoding="async">'

    def tiles(page, lang):
        t = translations[lang]
        items = []
        for slug in guides:
            name = t[slug.replace("-", "_")]
            thumb = ""
            if slug in annotations:
                thumb = f'<img src="{local("0300-documentation/images/" + annotations[slug]["image"], page)}" alt="" loading="lazy">'
            items.append(f'<a class="guide-tile" data-search="{escape(name + " " + slug)}" href="{local(guide_route(slug, lang), page)}">{thumb}<span><strong>{escape(name)}</strong><small>{escape(t["read"])}</small></span></a>')
        return '<div class="guide-grid">' + "".join(items) + '</div>'

    # Only explicitly selected documentation is published. Relative source links
    # remain links to the repository rather than copying internal project data.
    def render_markdown(source):
        text = source.read_text(encoding="utf-8")
        def rewrite(match):
            label, target = match.group(1), match.group(2)
            parsed = urlsplit(target)
            if parsed.scheme or target.startswith("#"):
                return match.group(0)
            resolved = (source.parent / unquote(parsed.path)).resolve()
            if not resolved.is_relative_to(ROOT):
                raise ValueError("Documentation link outside repository")
            url = links['Repository'] + "/blob/" + quote(config['Website']['SourceRef'], safe="/") + "/" + quote(resolved.relative_to(ROOT).as_posix(), safe="/")
            if parsed.fragment:
                url += "#" + parsed.fragment
            return f'[{label}]({url})'
        text = re.sub(r"\[([^\]]+)\]\(([^\s)]+)\)", rewrite, text)
        return markdown.markdown(text, extensions=["tables", "fenced_code", "toc"])

    for lang in languages:
        t = translations[lang]
        page = route("home", lang)
        def media(path, eager=False):
            content = picture(path, page, t, eager)
            if path.suffix.lower() in {".mp4", ".webm"}:
                return content
            return f'<button class="image-open" aria-label="{escape(t["fullscreen"])}">{content}</button>'
        slides = "".join(f'<figure class="slide"{" hidden" if i else ""}>{media(path, i == 0)}<figcaption>{escape(caption(path, t))}</figcaption></figure>' for i, path in enumerate(images))
        if not images:
            slides = f'<div class="gallery-empty">{escape(t["empty"])}</div>'
        dots = "".join(f'<button class="slide-dot" aria-label="{escape(caption(path, t))}" aria-pressed="{"true" if not i else "false"}"></button>' for i, path in enumerate(images))
        controls = f'<div class="slideshow-controls"><button class="slide-prev" aria-label="{escape(t["previous"])}">‹</button><div class="slide-dots">{dots}</div><button class="slide-next" aria-label="{escape(t["next"])}">›</button><button class="slide-pause" data-play="{escape(t["play"])}" data-pause="{escape(t["pause"])}" aria-label="{escape(t["pause"])}">Ⅱ</button></div>' if len(images) > 1 else ""
        capability = "".join(f'<a href="{local(guide_route(slug, lang), page)}"><span class="cap-icon" aria-hidden="true">{symbol}</span>{escape(t[key])}</a>' for key, slug, symbol in [("modeling","feature","◇"),("sheet","sheet-metal","⌁"),("drawing","drawings","▤"),("fem","fem","▦"),("cam","cam","⌖"),("render","render","◉")])
        content = f'''<section class="hero"><div class="hero-copy"><h1>{t['headline']}</h1><p class="lead">{escape(t['intro'])}</p><ul class="benefits">{"".join("<li>" + escape(t["benefit" + str(i)]) + "</li>" for i in range(1,6))}</ul><div class="hero-actions"><a class="button primary" href="{local(route('download',lang),page)}">{escape(t['get'])}</a><a class="button secondary" href="{links['YouTube']}"><span aria-hidden="true">▷</span>{escape(t['video'])}</a></div><p class="hero-note">Open Source · C++ · Qt · OpenCASCADE</p></div><div class="showcase" data-interval="{int(config['Website']['SlideInterval'])}"><div class="slides">{slides}</div>{controls}</div></section><nav class="capabilities" aria-label="{escape(t['guides'])}">{capability}</nav><section class="home-links"><a href="{local(route('gallery',lang),page)}"><h2>{escape(t['gallery'])}</h2><p>{escape(t['galleryText'])}</p><span>{escape(t['explore'])}</span></a><a href="{local(route('guides',lang),page)}"><h2>{escape(t['guides'])}</h2><p>{escape(t['guidesText'])}</p><span>{escape(t['allGuides'])}</span></a></section>'''
        put(page, shell(page, lang, "ZIMA-CAD", content, "home"))
        page = route("gallery", lang)
        figures = "".join(f'<figure class="gallery-item">{media(path)}<figcaption>{escape(caption(path,t))}</figcaption></figure>' for path in images)
        put(page, shell(page, lang, t['gallery'], f'<section class="page-intro"><p class="eyebrow">ZIMA-CAD</p><h1>{escape(t["galleryTitle"])}</h1><p>{escape(t["galleryText"])}</p></section><section class="gallery-grid">{figures or escape(t["empty"])}</section>', "gallery"))
        page = route("guides", lang)
        put(page, shell(page, lang, t['guides'], f'<section class="page-intro"><p class="eyebrow">ZIMA-CAD / {escape(t["guides"])}</p><h1>{escape(t["guidesTitle"])}</h1><p>{escape(t["guidesText"])}</p><label class="search"><span aria-hidden="true">⌕</span><input type="search" id="guide-search" placeholder="{escape(t["search"])}" aria-label="{escape(t["search"])}"></label><p class="language-note">{escape(t["english"])}</p></section>{tiles(page,lang)}<p id="no-results" hidden>{escape(t["noResults"])}</p>', "guides"))
        for slug, filename in guides.items():
            page = guide_route(slug, lang)
            name = t[slug.replace("-", "_")]
            visual = ""
            if slug in annotations:
                info = annotations[slug]
                markers, descriptions = [], []
                for index, marker in enumerate(info['markers'], 1):
                    x, y = float(marker['x']), float(marker['y'])
                    assert 0 <= x <= 100 and 0 <= y <= 100
                    markers.append(f'<button class="hotspot" style="left:{x}%;top:{y}%" data-marker="{index}" aria-controls="marker-{index}" aria-label="{escape(marker["title"])}" aria-pressed="false"><span>{index}</span></button>')
                    descriptions.append(f'<li id="marker-{index}"><button data-marker="{index}" aria-pressed="false"><span class="step-number">{index}</span><span><strong>{escape(marker["title"])}</strong><span>{escape(marker["body"])}</span></span></button></li>')
                image_url = local("0300-documentation/images/" + info['image'], page)
                visual = f'<section class="visual-guide" lang="en"><h2>{escape(t["visual"])}</h2><div class="annotated-image"><button class="image-open" aria-label="{escape(t["fullscreen"])}"><img src="{image_url}" alt="{escape(info["alt"])}"></button>{"".join(markers)}</div><ol class="marker-list">{"".join(descriptions)}</ol></section>'
            article = render_markdown(ROOT / filename)
            content = f'<div class="guide-heading"><a href="{local(route("guides",lang),page)}">{escape(t["allGuides"])}</a><h1>{escape(name)}</h1><p class="language-note">{escape(t["english"])}</p></div>{visual}<article class="guide-copy" lang="en">{article}</article>'
            put(page, shell(page, lang, name, content, "guides", True))
            put(f'0300-documentation/{slug}/0000-index/metadata.ini', metadata({key: translations[key][slug.replace("-", "_")] for key in languages}, slug))
        page = route("download", lang)
        content = f'<section class="download-page"><p class="eyebrow">ZIMA-CAD</p><h1>{escape(t["downloadTitle"])}</h1><p class="lead">{escape(t["downloadText"])}</p><div class="hero-actions"><a class="button primary" href="{links["Releases"]}">{escape(t["releases"])}</a><a class="button secondary" href="{links["Repository"]}">{escape(t["source"])}</a></div><div class="download-details"><span>GPL-3.0-or-later</span><span>Windows / Linux</span></div></section>'
        put(page, shell(page, lang, t['download'], content, "download"))

    # Default landing page is useful without scripting, including file:// use.
    put("index.html", f'<!doctype html><html lang="{default}"><head><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=0000-index/index_{default}.html"><title>ZIMA-CAD</title></head><body><a href="0000-index/index_{default}.html">ZIMA-CAD</a></body></html>')
    put("robots.txt", f'User-agent: *\nAllow: /\nSitemap: {canonical}/sitemap.xml\n')
    put("sitemap.xml", '<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">' + ''.join(f'<url><loc>{canonical}/{escape(path)}</loc></url>' for path in outputs if path.endswith('.html') and path != 'index.html') + '</urlset>')
    for source in ("0000-index/metadata.ini", "0300-documentation/0000-index/metadata.ini"):
        put(source, (WEB / source).read_bytes())
    changed = []
    for path, data in outputs.items():
        target = WEB / path
        if not target.exists() or target.read_bytes() != data:
            changed.append(path)
            if not check:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
    if check and changed:
        raise SystemExit("Stale generated website files: " + ", ".join(changed[:20]))
    # A public-only deployment manifest; never enumerate unrelated repository files.
    manifest = {path: hashlib.sha256(data).hexdigest() for path, data in sorted(outputs.items())}
    if not check:
        (WEB / "_source/outputs.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Website: {len(languages)} languages, {len(guides)} guides, {len(images)} gallery media, {len(outputs)} files; {len(changed)} changed; {time.perf_counter()-started:.3f}s")
    return outputs


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--stage", type=Path, help="Write only public outputs to an empty staging directory")
    args = parser.parse_args()
    outputs = build(args.check)
    if args.stage:
        stage = args.stage.resolve()
        if stage.exists() and any(stage.iterdir()):
            raise SystemExit("Staging directory must be empty")
        for path, data in outputs.items():
            target = stage / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
