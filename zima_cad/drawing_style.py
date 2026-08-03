from __future__ import annotations

import configparser
from functools import lru_cache
from pathlib import Path

from zima_cad.paths import app_path


DEFAULT_PENS = {
    "YELLOW": {"color": "#E6C85C", "width": 0.25},
    "GREEN": {"color": "#4DD811", "width": 0.25},
    "WHITE": {"color": "#FFFFFF", "width": 0.50},
}


@lru_cache(maxsize=1)
def load_drawing_style() -> dict:
    path = app_path("config", "drawing.ini").resolve()
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(path, encoding="utf-8-sig")
    font_text = parser.get("Font", "File", fallback="fonts/osifont-lgpl3fe.ttf")
    font_path = Path(font_text)
    if not font_path.is_absolute():
        font_path = path.parent / font_path

    pens = {}
    for name, fallback in DEFAULT_PENS.items():
        pens[name] = {
            "color": parser.get(
                "Pens", f"{name.title()}Color", fallback=fallback["color"]
            ),
            "width": parser.getfloat(
                "Pens", f"{name.title()}Width", fallback=fallback["width"]
            ),
        }
    return {
        "path": path,
        "workspace": {
            "paper_boundary_color": parser.get(
                "Workspace", "PaperBoundaryColor", fallback="#808080"
            ),
        },
        "font": {
            "path": font_path.resolve(),
            "family": parser.get("Font", "Family", fallback="osifont"),
            "fallback_family": parser.get(
                "Font", "FallbackFamily", fallback="sans-serif"
            ),
            "small_height": parser.getfloat("Font", "SmallHeight", fallback=1.8),
            "normal_height": parser.getfloat("Font", "NormalHeight", fallback=2.5),
            "large_height": parser.getfloat("Font", "LargeHeight", fallback=3.5),
        },
        "pens": pens,
    }


@lru_cache(maxsize=1)
def drawing_font_family() -> str:
    """Register and return the bundled drawing font for this process."""
    from PySide6.QtGui import QFontDatabase

    font = load_drawing_style()["font"]
    font_id = QFontDatabase.addApplicationFont(str(font["path"]))
    if font_id >= 0:
        families = QFontDatabase.applicationFontFamilies(font_id)
        if families:
            return str(families[0])
    return str(font["fallback_family"])
