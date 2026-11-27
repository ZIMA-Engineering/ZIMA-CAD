"""Validate generated public pages, localization, references and packaging."""
import hashlib
import json
import posixpath
import tempfile
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

from build import ROOT, WEB, build


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.references = []
        self.ids = set()
        self.duplicates = []
        self.language = None
        self.alternates = set()

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        if tag == "html": self.language = data.get("lang")
        if tag == "link" and data.get("rel") == "alternate": self.alternates.add(data.get("hreflang"))
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
    for path, data in outputs.items():
        if path.endswith(".html"):
            page = Page(); page.feed(data.decode("utf-8")); parsed[path] = page
            assert not page.duplicates, (path, page.duplicates)
            if path != "index.html":
                assert page.language in {"cs", "en", "de", "fr", "ru"}
                assert page.alternates == {"cs", "en", "de", "fr", "ru"}
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
