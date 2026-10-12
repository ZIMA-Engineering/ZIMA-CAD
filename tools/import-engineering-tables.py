#!/usr/bin/env python3
"""Import the reviewed engineering-table content into the factory library."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import unicodedata
import urllib.parse
import uuid
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path


ALLOWED_SUFFIXES = {
    ".css", ".csv", ".gif", ".htm", ".html", ".jpeg", ".jpg", ".js",
    ".ods", ".pdf", ".png", ".svg", ".tif", ".tiff", ".txt", ".webp",
}
LEGACY_CAD_SUFFIXES = {".asm", ".drw", ".dwg", ".frm", ".iges", ".igs", ".neu", ".prt", ".step", ".stl", ".stp", ".xpr"}
LEGACY_CAD_SUPPORT_SUFFIXES = {".dtl", ".idx", ".out", ".pro", ".sh", ".zip"}
LEGACY_VERSIONED_CAD = re.compile(r"\.(?:asm|drw|frm|neu|prt)\.\d+$", re.IGNORECASE)
LANGUAGES = ("cs", "en", "de", "fr", "ru")
NAMESPACE = uuid.UUID("aa8cf669-b83c-57af-991a-9f966393422f")
PACKAGE_PREFIX = "ZIMA-CAD/windows/9999999999/config/lib/03-ENGINEERING-TABLES/"
DIRECTORY_CATALOG = json.loads((Path(__file__).parent / "data" / "engineering-table-directories.json").read_text(encoding="utf-8"))


def reviewed_directory(directory: Path) -> dict | None:
    for root in (directory, *directory.parents):
        if root.name == "04-MECHANICAL_TABLES":
            return DIRECTORY_CATALOG.get(directory.relative_to(root).as_posix())
    return None


@dataclass(frozen=True)
class ImportItem:
    source: Path
    target: Path
    payload: bytes | None = None

ENGLISH_NAME_OVERRIDES = {
    "01-Kluzna loziska": "Plain bearings",
    "02-Valiva loziska": "Rolling bearings",
    "21-Vrtani": "Drilling",
    "22-Ukosy": "Chamfers",
    "01-Plechy": "Sheets",
    "02-Pasy a pruhy": "Strips and bands",
    "03-Tyce kruhove": "Round bars",
    "04-Draty na pruziny": "Spring wires",
    "05-Tyce 4HR": "Square bars",
    "06-Tyce ploche": "Flat bars",
    "07-Tyce 6HR": "Hexagonal bars",
    "08-Trubky": "Tubes",
    "09-Tyce prurezu rovnoramenneho L": "Equal angle bars",
    "10-Tyce prurezu nerovnoramenneho L": "Unequal angle bars",
    "11-Tyce prurezu I": "I sections",
    "12-Tyce prurezu U": "U sections",
    "13-Tyce prurezu UE": "UE sections",
    "14-Hmotnosti polotovaru": "Semi-finished product weights",
    "01-Materialy": "Materials",
    "11-DREVO": "Wood",
    "17-TECHNICKE KRESLENI": "Technical drawing",
    "51-ENGINEERING_CALCULATIONS": "Engineering calculations",
    "01-RETEZOVE KOLA": "Chain sprockets",
    "02-E-konstukter": "E-constructor",
    "02-Korunove matice nizke": "Low castellated nuts",
    "03-Male_matice": "Small nuts",
    "04-Uzavrene matice": "Cap nuts",
    "05-Privarovaci_matice": "Weld nuts",
    "06-Pojistne_matice_dvoudilne": "Two-piece locknuts",
    "07-Samojistne_matice": "Self-locking nuts",
    "DIN_ PODLOZKY": "DIN washers",
    "nerez": "Stainless steel",
    "11-Zakolnik": "Cotter pins",
    "12-Zavlacka-pruzna-jednoducha": "Single spring cotter pins",
    "04-SROUBY_S_PULKULATOU_HLAVOU_S_VNITRNIM_6HR-A2": "Button head hex socket screws A2",
    "01-S kuzelovou hlavou, trida A": "Countersunk head screws, class A",
    "07-S kuzelovou hlavou, vnitrni 6HR": "Countersunk hex socket screws",
    "XX_05-STAVECI_SROUBY-s_vnitrnim_6hr_a_s_cipkem": "Hex socket set screws with dog point",
    "XX_06-STAVECI_SROUBY-s_vnitrnim_6hr_a_hrotem": "Hex socket set screws with cone point",
    "XX_DIN_913-SROUB_STAVECI_S_VNITRNIM_6HR": "DIN 913 hex socket set screws",
    "XX_DIN_914-SROUB_STAVECI_S_VNITRNIM_6HR": "DIN 914 hex socket set screws",
    "XX_DIN_915-SROUB_STAVECI_S_VNITRNIM_6HR": "DIN 915 hex socket set screws",
    "01-SROUBY_TVARU_A_a_D": "Anchor bolts, forms A and D",
    "02-SROUBY_TVARU_B_a_C": "Anchor bolts, forms B and C",
    "03-SROUBY_TVARU_E": "Anchor bolts, form E",
    "03-Jemne drazkovani": "Fine splines",
    "01-Rozmery valcovych a kuzelovych koncu hridelu": "Cylindrical and tapered shaft ends",
    "01-Jemne drazkovani s primkovym profilem boku": "Fine splines with straight flanks",
    "03-Presnost jemneho drazkovani": "Fine spline accuracy",
    "01-Rada lehka podle CSN EN ISO 14": "Light series per CSN EN ISO 14",
    "02-Rada stredni podle CSN EN ISO 14": "Medium series per CSN EN ISO 14",
    "03-Rada tezka podle CSN 01 4942": "Heavy series per CSN 01 4942",
    "04-Drsnost povrchu drazek a per": "Spline and key surface roughness",
    "05-Licovani rovnobokeho drazkovani podle ISO": "Straight-sided spline fits per ISO",
    "06-Licovani rovnobokeho drazkovani podle CSN": "Straight-sided spline fits per CSN",
    "01-Licovani evolventniho drazkovani": "Involute spline fits",
    "02-Uchylky sirky drazek a tloustky zubu": "Space-width and tooth-thickness deviations",
    "04-Drsnost povrchu evolventniho drazkovani": "Involute spline surface roughness",
    "03-Porovnani metrickych modulu s moduly Diametral Pitch": "Metric module and diametral pitch comparison",
    "04-Prevodova cisla": "Gear ratios",
    "05-Prevodova cisla snekovych prevodu": "Worm gear ratios",
    "06-Uhly sklonu sikmych zubu celnich a kuzelovych kol": "Helix angles for spur and bevel gears",
    "08-Prumery valecku pro mereni pres valecky": "Roller diameters for measurement over rollers",
    "09-Rozmer pres zuby celnich kol s primymi zuby": "Measurement over teeth of spur gears",
    "10-Stupne presnosti ozubenych kol podle CSN a ISO": "Gear accuracy grades per CSN and ISO",
    "11-Doporucena prirazeni presnosti ozubeni": "Recommended gear accuracy assignments",
    "12-Uchylky profilu zubu podle ISO": "Tooth profile deviations per ISO",
    "13-Uchylky rozteci podle ISO": "Pitch deviations per ISO",
    "14-Uchylky sklonu a tvaru boku zubu podle ISO": "Tooth flank deviations per ISO",
    "09-Staveci srouby": "Set screws",
    "10-Otocne srouby s okem": "Eye bolts",
    "11-Srouby do zdiva a zakladu": "Masonry and foundation bolts",
    "02-Korunove_matice": "Castellated nuts",
    "08-Ryhovane_matice": "Knurled nuts",
    "09-Kridlate_matice": "Wing nuts",
    "04-Ozubene podlozky": "Toothed lock washers",
    "05-Pojistne podlozky": "Lock washers",
    "01-Srouby s pulkulatou halvou s krizovou drazkou": "Cross-recessed pan head screws",
    "02-Srouby do plechu s pulkulatou hlavou a krizovou drazkou": "Cross-recessed pan head tapping screws",
    "01-S drazkou a plochym koncem": "Slotted set screws with flat point",
    "02-S drazkou a kuzelovym dulkem": "Slotted set screws with cup point",
    "03-S drazkou a s cipkem": "Slotted set screws with dog point",
    "04-S drazkou a s hrotem": "Slotted set screws with cone point",
    "05-S vnitrnim sestihranem": "Hex socket set screws",
    "03-Nizke se srazenim, trida A a B": "Chamfered low nuts, classes A and B",
    "04-Nizke s jemnym metrickym zavitem": "Low nuts with fine metric thread",
    "05-Nizke bez srazeni": "Unchamfered low nuts",
    "08-Vyrobni_trida_C_(hrube)": "Product grade C nuts",
    "01-Korunove matice CSN 02 1411": "Castellated nuts CSN 02 1411",
    "11-DIN 11024": "DIN 11024 spring cotter pins",
}

GENERIC_ENGLISH_LABELS = {"to be translated", "shaft, spur a groove"}

TOP_LEVEL_STORAGE_NAMES = {
    "01-OBECNE NORMY": "01-GENERAL-STANDARDS",
    "02-TOLEROVANI": "02-TOLERANCES",
    "03-ZAVITY": "03-THREADS",
    "04-HRIDELE, DRAZKY A OZUBENI": "04-SHAFTS-SPLINES-GEARS",
    "05-SROUBY, MATICE A PODLOZKY": "05-FASTENERS",
    "06-VSEOBECNE POZADAVKY NA SROUBY A MATICE": "06-FASTENER-REQUIREMENTS",
    "07-CEPY, POJISTKY A KOLIKY": "07-PINS-RETAINERS-DOWELS",
    "08-PERA A DRAZKY": "08-KEYS-KEYWAYS",
    "09-LOZISKA": "09-BEARINGS",
    "10-TESNENI": "10-SEALS",
    "11-HRDLA A PRIPOJKY": "11-PORTS-CONNECTIONS",
    "12-POTRUBI A ARMATURY": "12-PIPES-VALVES",
    "13-SVAROVANI A PAJENI": "13-WELDING-BRAZING",
    "14-TECHNOLOGIE": "14-MANUFACTURING",
    "15-POLOTOVARY": "15-SEMI-FINISHED",
    "16-TECHNICKE MATERIALY": "16-MATERIALS",
    "17-TECHNICKE KRESLENI": "17-TECHNICAL-DRAWING",
    "51-ENGINEERING_CALCULATIONS": "51-CALCULATIONS",
}

TOP_LEVEL_LABELS = {
    "01-OBECNE NORMY": ("OBECNÉ NORMY", "GENERAL STANDARDS", "ALLGEMEINE NORMEN", "NORMES GÉNÉRALES", "ОБЩИЕ СТАНДАРТЫ"),
    "02-TOLEROVANI": ("TOLEROVÁNÍ", "TOLERANCES", "TOLERANZEN", "TOLÉRANCES", "ДОПУСКИ"),
    "03-ZAVITY": ("ZÁVITY", "THREADS", "GEWINDE", "FILETAGES", "РЕЗЬБЫ"),
    "04-HRIDELE, DRAZKY A OZUBENI": ("HŘÍDELE, DRÁŽKY A OZUBENÍ", "SHAFTS, SPLINES AND GEARS", "WELLEN, VERZAHNUNGEN UND ZAHNRÄDER", "ARBRES, CANNELURES ET ENGRENAGES", "ВАЛЫ, ШЛИЦЫ И ЗУБЧАТЫЕ ПЕРЕДАЧИ"),
    "05-SROUBY, MATICE A PODLOZKY": ("ŠROUBY, MATICE A PODLOŽKY", "FASTENERS", "VERBINDUNGSELEMENTE", "ÉLÉMENTS DE FIXATION", "КРЕПЁЖНЫЕ ИЗДЕЛИЯ"),
    "06-VSEOBECNE POZADAVKY NA SROUBY A MATICE": ("VŠEOBECNÉ POŽADAVKY NA ŠROUBY A MATICE", "FASTENER REQUIREMENTS", "ANFORDERUNGEN AN SCHRAUBEN UND MUTTERN", "EXIGENCES RELATIVES AUX FIXATIONS", "ТРЕБОВАНИЯ К КРЕПЕЖУ"),
    "07-CEPY, POJISTKY A KOLIKY": ("ČEPY, POJISTKY A KOLÍKY", "PINS, RETAINERS AND DOWELS", "BOLZEN, SICHERUNGEN UND STIFTE", "AXES, ARRÊTS ET GOUPILLES", "ПАЛЬЦЫ, ФИКСАТОРЫ И ШТИФТЫ"),
    "08-PERA A DRAZKY": ("PERA A DRÁŽKY", "KEYS AND KEYWAYS", "PASSFEDERN UND NUTEN", "CLAVETTES ET RAINURES", "ШПОНКИ И ШПОНОЧНЫЕ ПАЗЫ"),
    "09-LOZISKA": ("LOŽISKA", "BEARINGS", "LAGER", "ROULEMENTS", "ПОДШИПНИКИ"),
    "10-TESNENI": ("TĚSNĚNÍ", "SEALS", "DICHTUNGEN", "JOINTS", "УПЛОТНЕНИЯ"),
    "11-HRDLA A PRIPOJKY": ("HRDLA A PŘÍPOJKY", "PORTS AND CONNECTIONS", "ANSCHLÜSSE UND VERBINDUNGEN", "ORIFICES ET RACCORDS", "ПАТРУБКИ И СОЕДИНЕНИЯ"),
    "12-POTRUBI A ARMATURY": ("POTRUBÍ A ARMATURY", "PIPES AND VALVES", "ROHRE UND ARMATUREN", "TUYAUX ET ROBINETTERIE", "ТРУБЫ И АРМАТУРА"),
    "13-SVAROVANI A PAJENI": ("SVAŘOVÁNÍ A PÁJENÍ", "WELDING AND BRAZING", "SCHWEISSEN UND LÖTEN", "SOUDAGE ET BRASAGE", "СВАРКА И ПАЙКА"),
    "14-TECHNOLOGIE": ("TECHNOLOGIE", "MANUFACTURING", "FERTIGUNG", "FABRICATION", "ПРОИЗВОДСТВО"),
    "15-POLOTOVARY": ("POLOTOVARY", "SEMI-FINISHED PRODUCTS", "HALBZEUGE", "PRODUITS SEMI-FINIS", "ПОЛУФАБРИКАТЫ"),
    "16-TECHNICKE MATERIALY": ("TECHNICKÉ MATERIÁLY", "ENGINEERING MATERIALS", "TECHNISCHE WERKSTOFFE", "MATÉRIAUX TECHNIQUES", "КОНСТРУКЦИОННЫЕ МАТЕРИАЛЫ"),
    "17-TECHNICKE KRESLENI": ("TECHNICKÉ KRESLENÍ", "TECHNICAL DRAWING", "TECHNISCHES ZEICHNEN", "DESSIN TECHNIQUE", "ТЕХНИЧЕСКОЕ ЧЕРЧЕНИЕ"),
    "51-ENGINEERING_CALCULATIONS": ("STROJÍRENSKÉ VÝPOČTY", "ENGINEERING CALCULATIONS", "TECHNISCHE BERECHNUNGEN", "CALCULS TECHNIQUES", "ИНЖЕНЕРНЫЕ РАСЧЁТЫ"),
}


def allowed_content(path: Path) -> bool:
    return path.name == "metadata.ini" or (
        path.suffix.lower() in ALLOWED_SUFFIXES and not path.name.endswith(("~", "#"))
    )


def ini_label(text: str, language: str) -> str:
    match = re.search(rf"(?im)^[ \t]*Label\\{re.escape(language)}[ \t]*=[ \t]*(.*)$", text)
    if not match:
        return ""
    return match.group(1).strip().strip('"')


def generated_english_label(directory: Path) -> str:
    name = directory.name
    if name in ENGLISH_NAME_OVERRIDES:
        return ENGLISH_NAME_OVERRIDES[name]
    if re.match(r"^02-Jemne drazkovani s evolvent", name, re.IGNORECASE):
        return "Fine splines with involute flanks"
    if re.match(r"^07-Tloustky zubu na tetive", name, re.IGNORECASE):
        return "Chordal tooth thicknesses and constant tooth heights"
    hole_deviations = re.match(r"^(\d+)-Uchylky rozmeru der (.+)$", name, re.IGNORECASE)
    if hole_deviations:
        return f"Hole size deviations {hole_deviations.group(2).replace('-', ' to ')}"
    range_match = re.match(r"^(\d+)-rozmery\s+(.+?)\s+az\s+(.+)$", name, re.IGNORECASE)
    if range_match:
        return f"Dimensions {range_match.group(2)} to {range_match.group(3)}"
    source = re.sub(r"^(?:\d+|XX)[-_ ]*", "", name).replace("_", " ").strip()
    return source or "Directory"


def english_label(directory: Path) -> str:
    reviewed = reviewed_directory(directory)
    if reviewed:
        return reviewed["labels"]["en"]
    metadata = directory / "0000-index" / "metadata.ini"
    text = metadata.read_text(encoding="utf-8-sig", errors="replace") if metadata.is_file() else ""
    existing = ini_label(text, "en")
    czech = ini_label(text, "cs")
    def comparable(value: str) -> str:
        plain = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")
        return re.sub(r"[^a-z0-9]+", "", plain.casefold())
    if existing and existing.casefold() not in GENERIC_ENGLISH_LABELS and comparable(existing) != comparable(czech):
        return existing
    return generated_english_label(directory)


def english_slug(text: str) -> str:
    ascii_text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")
    ascii_text = ascii_text.upper().replace("&", " AND ")
    slug = re.sub(r"[^A-Z0-9]+", "-", ascii_text).strip("-")
    return slug or "DIRECTORY"


def compact_storage_name(directory: Path, sequence: int, maximum: int = 30) -> str:
    reviewed = reviewed_directory(directory)
    if reviewed:
        return reviewed["storage"]
    prefix_match = re.match(r"^(\d+|XX)[-_ ]*", directory.name, re.IGNORECASE)
    prefix = prefix_match.group(1).upper() if prefix_match else f"{sequence:02d}"
    label = re.sub(r"^(?:\d+|XX)[-_ ]*", "", english_label(directory), flags=re.IGNORECASE)
    slug = english_slug(label)
    available = max(8, maximum - len(prefix) - 1)
    slug = slug[:available].rstrip("-") or "DIRECTORY"
    return f"{prefix}-{slug}"


def directory_metadata(text: str, label: str, english: str, identity: str, root: bool,
                       localized_labels: tuple[str, str, str, str, str] | None = None) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n").lstrip("\ufeff")
    match = re.search(r"(?im)^\[Directory\]\s*$", text)
    if match:
        start = match.end()
        next_section = re.search(r"(?m)^\[[^]]+\]\s*$", text[start:])
        end = start + next_section.start() if next_section else len(text)
        body = text[start:end]
        body = re.sub(r"(?im)^\s*(Version|Kind|Id)\s*=.*(?:\n|$)", "", body)
        if root:
            body = body.replace("Label\\cs=STROJNICKÉ TAUBULKY", "Label\\cs=STROJNICKÉ TABULKY")
        if not re.search(r"(?im)^\s*Label\\(?:cs|en|de|fr|ru)\s*=", body):
            body = f"\nLabel\\cs={label}\n" + body.lstrip("\n")
        if re.search(r'(?im)^[ \t]*Label\\en[ \t]*=[ \t]*(?:"")?[ \t]*$', body):
            body = re.sub(r"(?im)^[ \t]*Label\\en[ \t]*=.*$", lambda _: f"Label\\en={english}", body)
        elif not re.search(r"(?im)^\s*Label\\en\s*=", body):
            body = f"Label\\en={english}\n" + body.lstrip("\n")
        replacement = (
            "\nVersion=3\n"
            "Kind=directory\n"
            f"Id={identity}\n"
            + body.lstrip("\n")
        )
        text = text[:start] + replacement + text[end:]
    else:
        text = (
            "[Directory]\n"
            "Version=3\n"
            "Kind=directory\n"
            f"Id={identity}\n"
            f"Label\\cs={label}\n"
            f"Label\\en={english}\n"
        )
    if localized_labels:
        for language, value in zip(LANGUAGES, localized_labels, strict=True):
            key = rf"Label\\{language}"
            if re.search(rf"(?im)^[ \t]*{key}[ \t]*=.*$", text):
                text = re.sub(rf"(?im)^[ \t]*{key}[ \t]*=.*$", lambda _: f"Label\\{language}={value}", text)
            else:
                directory_end = re.search(r"(?m)^\[[^]]+\]\s*$", text[text.index("\n", text.index("[Directory]")) + 1:])
                insert_at = (text.index("\n", text.index("[Directory]")) + 1 + directory_end.start()) if directory_end else len(text)
                text = text[:insert_at] + f"Label\\{language}={value}\n" + text[insert_at:]
    return text.rstrip() + "\n"


def build_mapping(source: Path, included_directories: set[Path]) -> dict[Path, Path]:
    relative_mapping = {source: Path()}
    queue = deque([source])
    while queue:
        parent = queue.popleft()
        children = sorted(
            (path for path in included_directories if path.parent == parent),
            key=lambda path: (path.name.casefold(), path.name),
        )
        visible_number = 0
        used: set[str] = set()
        for child in children:
            if child.name.casefold() == "0000-index":
                target_name = "0000-index"
            elif "0000-index" in {part.casefold() for part in child.relative_to(source).parts}:
                target_name = "thumbnails" if child.name.casefold() == "thubnails" else child.name
            else:
                visible_number += 1
                target_name = TOP_LEVEL_STORAGE_NAMES.get(child.name) if parent==source else None
                if not target_name:target_name = compact_storage_name(child, visible_number,30 if parent==source else 12)
            original_target=target_name;collision=1
            while target_name.casefold() in used:
                collision+=1;target_name=f"{original_target[:26].rstrip('-')}-{collision:02d}"
            used.add(target_name.casefold())
            relative_mapping[child] = relative_mapping[parent] / target_name
            queue.append(child)
    return relative_mapping


def plan_import(source: Path) -> tuple[dict[Path, Path], list[Path], Counter[str]]:
    files = [path for path in source.rglob("*") if path.is_file()]
    included_files = [path for path in files if allowed_content(path)]
    exclusions: Counter[str] = Counter()
    for path in files:
        if path in included_files:
            continue
        if path.name.endswith(("~", "#")) or path.name in {".directory", "Thumbs.db", "desktop.ini"}:
            exclusions["backup-or-desktop"] += 1
        elif path.name == "metadata.ini.v1":
            exclusions["obsolete-metadata-backup"] += 1
        elif path.suffix.lower() in LEGACY_CAD_SUFFIXES or LEGACY_VERSIONED_CAD.search(path.name):
            exclusions["legacy-cad"] += 1
        elif path.suffix.lower() in LEGACY_CAD_SUPPORT_SUFFIXES:
            exclusions["legacy-cad-support"] += 1
        else:
            exclusions["unsupported"] += 1

    included_directories = {source}
    for path in included_files:
        parent = path.parent
        while parent != source.parent:
            included_directories.add(parent)
            if parent == source:
                break
            parent = parent.parent
    return build_mapping(source, included_directories), included_files, exclusions


HTML_REFERENCE = re.compile(r"(?P<prefix>(?:href|src)\s*=\s*[\"'])(?P<url>[^\"']+)(?P<suffix>[\"'])", re.IGNORECASE)
CSS_REFERENCE = re.compile(r"(?P<prefix>url\(\s*[\"']?)(?P<url>[^\"')]+)(?P<suffix>[\"']?\s*\))", re.IGNORECASE)
WEB_ASSET_SUFFIXES = {".css", ".gif", ".jpeg", ".jpg", ".js", ".png", ".svg", ".webp"}


def decoded_text(data: bytes) -> tuple[str, str]:
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig"), "utf-8-sig"
    try:
        return data.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        return data.decode("cp1250"), "cp1250"


def content_pairs(source: Path, destination: Path) -> tuple[list[ImportItem], dict[Path, Path], Counter[str]]:
    mapping, included_files, exclusions = plan_import(source)
    included_file_set = {path.resolve() for path in included_files}
    targets: dict[str, ImportItem] = {}

    def add(item: ImportItem) -> None:
        key = item.target.as_posix().casefold()
        previous = targets.get(key)
        if previous is not None:
            previous_bytes = previous.payload if previous.payload is not None else previous.source.read_bytes()
            item_bytes = item.payload if item.payload is not None else item.source.read_bytes()
            if previous_bytes != item_bytes:
                raise RuntimeError(f"Case-insensitive target collision: {previous.source} and {item.source}")
            return
        targets[key] = item

    for path in included_files:
        if path.name != "metadata.ini":
            add(ImportItem(path, destination / mapping[path.parent] / path.name))

    for html in (path for path in included_files if path.suffix.lower() in {".htm", ".html"}):
        target = destination / mapping[html.parent] / html.name
        original = html.read_bytes()
        text, encoding = decoded_text(original)
        assets: dict[Path, str] = {}
        used_names: dict[str, Path] = {}

        def add_asset(asset: Path) -> str:
            resolved = asset.resolve()
            if resolved in assets:
                return assets[resolved]
            name = asset.name
            key = name.casefold()
            if key in used_names and used_names[key] != resolved:
                name = f"{asset.stem}-{hashlib.sha256(asset.read_bytes()).hexdigest()[:8]}{asset.suffix.lower()}"
                key = name.casefold()
            used_names[key] = resolved
            assets[resolved] = name
            payload = None
            if asset.suffix.lower() == ".css":
                original_css = asset.read_bytes()
                css_text, css_encoding = decoded_text(original_css)
                def replace_css_reference(match: re.Match[str]) -> str:
                    value = match.group("url")
                    parsed = urllib.parse.urlsplit(value)
                    if parsed.scheme or not parsed.path or value.startswith(("#", "//", "/")):
                        return match.group(0)
                    dependency = (asset.parent / urllib.parse.unquote(parsed.path)).resolve()
                    try:
                        dependency.relative_to(source)
                    except ValueError:
                        return match.group(0)
                    if not dependency.is_file() or dependency.suffix.lower() not in WEB_ASSET_SUFFIXES:
                        return match.group(0)
                    local_url = urllib.parse.urlunsplit(("", "", urllib.parse.quote(add_asset(dependency)), parsed.query, parsed.fragment))
                    return match.group("prefix") + local_url + match.group("suffix")
                localized_css = CSS_REFERENCE.sub(replace_css_reference, css_text).encode(css_encoding)
                if localized_css != original_css:
                    payload = localized_css
            add(ImportItem(asset, target.parent / "assets" / name, payload))
            return name

        def replace_reference(match: re.Match[str]) -> str:
            value = match.group("url")
            parsed = urllib.parse.urlsplit(value)
            if parsed.scheme or not parsed.path or value.startswith(("#", "//", "/")):
                return match.group(0)
            referenced = (html.parent / urllib.parse.unquote(parsed.path)).resolve()
            try:
                referenced.relative_to(source)
            except ValueError:
                return match.group(0)
            if referenced.is_file() and referenced.parent != html.parent and referenced.suffix.lower() in WEB_ASSET_SUFFIXES:
                local_path = "assets/" + urllib.parse.quote(add_asset(referenced))
            elif referenced.is_file() and referenced in included_file_set:
                referenced_target = destination / mapping[referenced.parent] / referenced.name
                local_path = Path(os.path.relpath(referenced_target, target.parent)).as_posix()
                local_path = "/".join(urllib.parse.quote(part) for part in local_path.split("/"))
            elif referenced.is_dir() and referenced in mapping:
                referenced_target = destination / mapping[referenced]
                local_path = Path(os.path.relpath(referenced_target, target.parent)).as_posix()
                local_path = "/".join(urllib.parse.quote(part) for part in local_path.split("/")) + "/"
            else:
                return match.group(0)
            local_url = urllib.parse.urlunsplit(("", "", local_path, parsed.query, parsed.fragment))
            return match.group("prefix") + local_url + match.group("suffix")

        localized = HTML_REFERENCE.sub(replace_reference, text).encode(encoding)
        if localized != original:
            targets[target.as_posix().casefold()] = ImportItem(html, target, localized)

    return sorted(targets.values(), key=lambda item: item.target.as_posix().casefold()), mapping, exclusions


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_import(source: Path, destination: Path) -> dict[str, object]:
    source = source.resolve()
    destination = destination.resolve()
    pairs, mapping, _ = content_pairs(source, destination)
    content_digest = hashlib.sha256()
    total_bytes = 0
    transformed = 0
    for item in pairs:
        if not item.target.is_file():
            raise RuntimeError(f"Imported file is missing: {item.target}")
        expected = item.payload if item.payload is not None else item.source.read_bytes()
        expected_digest = hashlib.sha256(expected).hexdigest()
        if item.target.stat().st_size != len(expected) or file_sha256(item.target) != expected_digest:
            raise RuntimeError(f"Imported file differs from its planned content: {item.target}")
        relative = item.target.relative_to(destination).as_posix()
        content_digest.update(relative.encode("utf-8"))
        content_digest.update(b"\0")
        content_digest.update(bytes.fromhex(expected_digest))
        total_bytes += len(expected)
        transformed += item.payload is not None

    identities: set[str] = set()
    metadata_count = 0
    for directory, relative_target in mapping.items():
        if any(part.casefold() == "0000-index" for part in directory.relative_to(source).parts):
            continue
        metadata = destination / relative_target / "0000-index" / "metadata.ini"
        if not metadata.is_file():
            raise RuntimeError(f"Converted metadata is missing: {metadata}")
        text = metadata.read_text(encoding="utf-8")
        identity = re.search(r"(?im)^Id=([0-9a-f-]{36})\s*$", text)
        if not re.search(r"(?im)^Version=3\s*$", text) or not re.search(r"(?im)^Kind=directory\s*$", text) or identity is None:
            raise RuntimeError(f"Converted metadata is invalid: {metadata}")
        if identity.group(1) in identities:
            raise RuntimeError(f"Duplicate converted metadata identity: {identity.group(1)}")
        identities.add(identity.group(1))
        reviewed = reviewed_directory(directory)
        if reviewed:
            for language in LANGUAGES:
                if ini_label(text, language) != reviewed["labels"][language]:
                    raise RuntimeError(f"Reviewed directory label differs ({language}): {metadata}")
        metadata_count += 1

    unresolved_html: list[str] = []
    for html in destination.rglob("*.htm*"):
        html_text, _ = decoded_text(html.read_bytes())
        for match in HTML_REFERENCE.finditer(html_text):
            value = match.group("url")
            parsed = urllib.parse.urlsplit(value)
            if parsed.scheme or not parsed.path or value.startswith(("#", "//", "/")):
                continue
            referenced = (html.parent / urllib.parse.unquote(parsed.path)).resolve()
            if not referenced.exists():
                relative = html.relative_to(destination).as_posix()
                unresolved_html.append(f"{relative}: {value}")
                if html.parent.name.casefold() == "0000-index" and html.name.casefold().startswith("index"):
                    raise RuntimeError(f"Overview HTML has an unresolved local reference: {relative}: {value}")

    return {
        "verifiedFiles": len(pairs),
        "verifiedBytes": total_bytes,
        "verifiedMetadata": metadata_count,
        "verifiedTransformedFiles": transformed,
        "unresolvedLocalHtmlReferences": len(unresolved_html),
        "contentTreeSha256": content_digest.hexdigest(),
    }


def import_tree(source: Path, destination: Path, dry_run: bool = False) -> dict[str, object]:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise RuntimeError(f"Source directory does not exist: {source}")
    if source == destination or source in destination.parents or destination in source.parents:
        raise RuntimeError("Source and destination trees must be independent")
    if destination.exists():
        unexpected = [path for path in destination.iterdir() if path.name != "README.md"]
        if unexpected:
            raise RuntimeError(f"Destination is not an empty reserved library: {destination}")

    copy_pairs, mapping, exclusions = content_pairs(source, destination)

    metadata_pairs: list[tuple[Path, Path, str]] = []
    for directory, relative_target in mapping.items():
        relative_source = directory.relative_to(source)
        if any(part.casefold() == "0000-index" for part in relative_source.parts):
            continue
        old_metadata = directory / "0000-index" / "metadata.ini"
        old_text = old_metadata.read_text(encoding="utf-8-sig") if old_metadata.is_file() else ""
        identity = str(uuid.uuid5(NAMESPACE, relative_source.as_posix() or "."))
        label = "STROJNICKÉ TABULKY" if directory == source else directory.name
        reviewed = reviewed_directory(directory)
        localized = tuple(reviewed["labels"][language] for language in LANGUAGES) if reviewed else None
        metadata_pairs.append((
            old_metadata,
            destination / relative_target / "0000-index" / "metadata.ini",
            directory_metadata(
                old_text, label, english_label(directory) if directory != source else "MECHANICAL TABLES",
                identity, directory == source,
                localized or (("STROJNICKÉ TABULKY", "MECHANICAL TABLES", "MASCHINENBAUTABELLEN",
                 "TABLES DE CONSTRUCTION MÉCANIQUE", "МАШИНОСТРОИТЕЛЬНЫЕ ТАБЛИЦЫ")
                if directory == source else TOP_LEVEL_LABELS.get(directory.name)),
            ),
        ))

    all_relative_targets = [item.target.relative_to(destination).as_posix() for item in copy_pairs]
    all_relative_targets.extend(target.relative_to(destination).as_posix() for _, target, _ in metadata_pairs)
    longest_path = max(all_relative_targets,key=lambda path:len(PACKAGE_PREFIX+path),default="")
    longest_member = len(PACKAGE_PREFIX + longest_path)
    if longest_member > 180:
        raise RuntimeError(f"Imported package path exceeds 180 UTF-16 code units: {longest_member}: {longest_path}")

    report = {
        "source": source.name,
        "directories": len(metadata_pairs),
        "files": len(copy_pairs),
        "bytes": sum(len(item.payload) if item.payload is not None else item.source.stat().st_size for item in copy_pairs),
        "localizedHtml": sum(item.payload is not None and item.target.suffix.lower() in {".htm", ".html"} for item in copy_pairs),
        "localizedWebFiles": sum(item.payload is not None for item in copy_pairs),
        "htmlFiles": sum(item.target.suffix.lower() in {".htm", ".html"} for item in copy_pairs),
        "metadataVersion": 3,
        "maximumPackageMemberLength": longest_member,
        "excluded": dict(sorted(exclusions.items())),
    }
    if dry_run:
        return report

    destination.mkdir(parents=True, exist_ok=True)
    for item in copy_pairs:
        item.target.parent.mkdir(parents=True, exist_ok=True)
        if item.payload is None:
            shutil.copy2(item.source, item.target)
        else:
            item.target.write_bytes(item.payload)
    for _, target, text in metadata_pairs:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
    report.update(verify_import(source, destination))
    (destination / "0000-index" / "import.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.dry_run and args.verify_only:
        parser.error("--dry-run and --verify-only cannot be combined")
    result = verify_import(args.source, args.destination) if args.verify_only else import_tree(args.source, args.destination, args.dry_run)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
