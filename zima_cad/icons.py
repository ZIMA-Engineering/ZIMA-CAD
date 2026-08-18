from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QByteArray, QSize, Qt
from PySide6.QtGui import QIcon, QPainter, QPalette, QPixmap
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import QApplication

from zima_cad.paths import app_path


DOCUMENT_TYPE_ICON_NAMES: dict[str, str] = {
    "part": "part",
    "assembly": "assembly",
    "drawing": "drawing",
    "drawing_format": "drawing-format",
    "title_block": "title-block",
}

DOCUMENT_SUFFIX_ICON_NAMES: dict[str, str] = {
    ".prtz": "part",
    ".asmz": "assembly",
    ".drwz": "drawing",
    ".frmz": "drawing-format",
    ".tblz": "title-block",
}

_RESOURCE_ICON_CACHE: dict[tuple[str, str], QIcon] = {}


def document_type_icon_name(document_type: str) -> str:
    """Return the one document icon name shared by every UI surface."""
    return DOCUMENT_TYPE_ICON_NAMES.get(str(document_type), "part")


def document_file_icon_name(file_path: str | Path) -> str | None:
    """Resolve a current ZIMA document suffix without opening the file."""
    return DOCUMENT_SUFFIX_ICON_NAMES.get(Path(file_path).suffix.casefold())


def resource_icon(name: str) -> QIcon:
    application = QApplication.instance()
    if application is None:
        return QIcon(str(app_path("resources", "icons", f"{name}.svg")))

    # Qt does not consistently resolve SVG currentColor against the widget
    # palette. Render the icon with an explicit palette colour so transparent
    # SVG artwork remains legible in both light and dark themes.
    color = application.palette().color(QPalette.ColorRole.WindowText).name()
    cache_key = (name, color)
    if cache_key in _RESOURCE_ICON_CACHE:
        return _RESOURCE_ICON_CACHE[cache_key]
    path = app_path("resources", "icons", f"{name}.svg")
    svg = path.read_text(encoding="utf-8").replace("currentColor", color)
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    if not renderer.isValid():
        return QIcon(str(path))

    icon = QIcon()
    for size in (16, 18, 20, 24, 32, 48):
        pixmap = QPixmap(QSize(size, size))
        pixmap.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pixmap)
        renderer.render(painter)
        painter.end()
        icon.addPixmap(pixmap)
    _RESOURCE_ICON_CACHE[cache_key] = icon
    return icon
