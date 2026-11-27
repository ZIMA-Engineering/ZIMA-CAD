"""Validate generated public pages, localization, references and packaging."""
import hashlib
import json
import posixpath
import tempfile
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

from build import ROOT, WEB, build, ini, load_command_catalogs


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.references = []
        self.ids = set()
        self.duplicates = []
        self.language = None
        self.alternates = set()
        self.guide_copies = []
        self.native_images = []
        self.command = None
        self.guide_groups = set()

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        if tag == "html": self.language = data.get("lang")
        if tag == "link" and data.get("rel") == "alternate": self.alternates.add(data.get("hreflang"))
        if "guide-copy" in data.get("class", "").split():
            self.guide_copies.append(data.get("lang"))
            self.command = data.get("data-command")
        if tag == "img" and "data-native-ui-lang" in data:
            self.native_images.append((data["src"], data["data-native-ui-lang"]))
        if "data-guide-group" in data: self.guide_groups.add(data["data-guide-group"])
        if "id" in data:
            if data["id"] in self.ids: self.duplicates.append(data["id"])
            self.ids.add(data["id"])
        for key in ("src", "href"):
            if key in data: self.references.append(data[key])


def main():
    outputs = build(check=True)
    manifest = json.loads((WEB / "_source/outputs.json").read_text(encoding="utf-8"))
    assert manifest == {path: hashlib.sha256(data).hexdigest() for path, data in sorted(outputs.items())}
    parsed = {}
    languages = {"cs", "en", "de", "fr", "ru"}
    docs = ini(WEB / "0300-documentation/0000-index/metadata.ini")
    commands = load_command_catalogs(docs, languages)
    captures = json.loads((ROOT / 'doc/guides/images/capture-manifest.json').read_text(encoding='utf-8'))
    evidence = {entry['path']: entry for entry in captures['images']}
    public_images = {path.removeprefix('0300-documentation/images/'): data for path, data in outputs.items()
                     if path.startswith('0300-documentation/images/')}
    assert len(evidence) == len(captures['images']) and set(evidence) == set(public_images), 'Native capture inventory differs from public guides'
    for path, data in public_images.items():
        entry = evidence[path]
        assert hashlib.sha256(data).hexdigest() == entry['sha256'], f'Native capture changed: {path}'
        assert entry['language'] in languages and entry['language'] in Path(path).parts[:-1], path
        if entry['kind'] == 'native command':
            assert entry['text'].replace('&', '') == commands[entry['action']]['locales'][entry['language']]['title'], path
    for path, data in outputs.items():
        if path.endswith(".html"):
            page = Page(); page.feed(data.decode("utf-8")); parsed[path] = page
            assert not page.duplicates, (path, page.duplicates)
            if path != "index.html":
                assert page.language in languages
                assert page.alternates == languages
            assert all(language == page.language for language in page.guide_copies), (path, "Mixed-language guide copy")
            for reference, language in page.native_images:
                assert language == page.language, (path, "Mixed-language native screenshot")
                target = posixpath.normpath(posixpath.join(posixpath.dirname(path), unquote(reference)))
                assert f"/commands/{language}/" in target or f"/dialogs/{language}/" in target, (path, target)
    for language in languages:
        index = parsed[f"0300-documentation/0000-index/index_{language}.html"]
        assert index.guide_groups == {"overview"} | {entry["group"] for entry in commands.values()}
        for slug in docs["Guides"]:
            path = f"0300-documentation/{slug}/0000-index/index_{language}.html"
            assert parsed[path].guide_copies == [language], path
        for action, entry in commands.items():
            path = f"0300-documentation/{entry['slug']}/0000-index/index_{language}.html"
            page = parsed[path]
            assert page.guide_copies == [language] and page.command == action, path
            images = {posixpath.normpath(posixpath.join(posixpath.dirname(path), unquote(reference))) for reference, _ in page.native_images}
            expected = {f"0300-documentation/images/commands/{language}/{action}.png"}
            if "dialogCapture" in entry:
                expected.add(f"0300-documentation/images/dialogs/{language}/{entry['dialogCapture']}.png")
            assert images == expected, (path, images, expected)
            for target in expected:
                source = ROOT / "doc/guides/images" / target.removeprefix("0300-documentation/images/")
                assert outputs[target] == source.read_bytes(), f"Changed native screenshot: {target}"
    count = 0
    for path, page in parsed.items():
        for reference in page.references:
            url = urlsplit(reference)
            if url.scheme or url.netloc: continue
            target = posixpath.normpath(posixpath.join(posixpath.dirname(path), unquote(url.path))) if url.path else path
            assert target in outputs, (path, reference)
            if url.fragment and target in parsed:
                assert unquote(url.fragment) in parsed[target].ids, (path, reference)
            count += 1
    import importlib.util
    spec = importlib.util.spec_from_file_location("distribution_package", ROOT / "tools/distribution/package.py")
    package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
    with tempfile.TemporaryDirectory(prefix="zima-public-site-") as folder:
        runtime = Path(folder)
        package.copy_public_site(ROOT, runtime)
        copied = {p.relative_to(runtime / "www").as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in (runtime / "www").rglob("*") if p.is_file()}
        assert copied == manifest, "Runtime guide copy differs from checked public output"
    print(f"Passed: {len(parsed)} HTML pages, {count} local references, five-language coverage and exact runtime/public file inventory.")


if __name__ == "__main__":
    main()
