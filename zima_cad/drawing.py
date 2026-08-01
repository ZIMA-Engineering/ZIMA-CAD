from __future__ import annotations

import json
from math import cos, radians, sin
from uuid import uuid4

from PySide6.QtCore import (
    QEasingCurve,
    QPoint,
    QPointF,
    QRectF,
    Qt,
    QVariantAnimation,
    Signal,
)
from PySide6.QtGui import (
    QColor,
    QMouseEvent,
    QPainter,
    QPen,
    QPolygonF,
    QWheelEvent,
)
from PySide6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QTabBar,
    QVBoxLayout,
    QWidget,
)

from zima_cad.model import PartDocument


SHEET_FORMATS: dict[str, tuple[float, float]] = {
    "A4": (297.0, 210.0),
    "A3": (420.0, 297.0),
    "A2": (594.0, 420.0),
    "A1": (841.0, 594.0),
    "A0": (1189.0, 841.0),
}


def default_sheet(index: int = 1) -> dict:
    return {
        "id": str(uuid4()),
        "name": f"List {index}",
        "format": "A4",
        "default_scale_numerator": 1.0,
        "default_scale": 1.0,
        "orientation": "portrait",
        "views": [],
    }


def drawing_sheets(document: PartDocument) -> list[dict]:
    try:
        sheets = json.loads(document.document_settings.get("drawing_sheets", "[]"))
    except (TypeError, ValueError, json.JSONDecodeError):
        sheets = []
    if not isinstance(sheets, list) or not sheets:
        sheets = [default_sheet()]
    for sheet in sheets:
        sheet_format = str(sheet.get("format", "A4"))
        sheet["orientation"] = (
            "portrait" if sheet_format == "A4" else "landscape"
        )
        sheet.setdefault("default_scale_numerator", 1.0)
        sheet.setdefault("default_scale", 1.0)
    return sheets


def store_drawing_sheets(document: PartDocument, sheets: list[dict]) -> None:
    document.document_settings["drawing_sheets"] = json.dumps(
        sheets, ensure_ascii=False, separators=(",", ":")
    )


def project_polylines(
    polylines: list[list[tuple[float, float, float]]],
    orientation: str,
) -> list[list[list[float]]]:
    result: list[list[list[float]]] = []
    # Keep drawing projections aligned with the native viewer: +X appears on
    # the left in the front/top/isometric views.
    iso_yaw = radians(215.264)
    iso_pitch = radians(-45.0)
    for polyline in polylines:
        projected: list[list[float]] = []
        for x, y, z in polyline:
            if orientation == "top":
                u, v = -x, -y
            elif orientation == "right":
                u, v = -y, z
            elif orientation == "isometric":
                yaw_x = cos(iso_yaw) * x - sin(iso_yaw) * y
                yaw_y = sin(iso_yaw) * x + cos(iso_yaw) * y
                u = yaw_x
                v = cos(iso_pitch) * yaw_y - sin(iso_pitch) * z
            else:
                u, v = -x, z
            projected.append([float(u), float(v)])
        if len(projected) >= 2:
            result.append(projected)
    if not result:
        return []
    xs = [point[0] for line in result for point in line]
    ys = [point[1] for line in result for point in line]
    center_x = (min(xs) + max(xs)) * 0.5
    center_y = (min(ys) + max(ys)) * 0.5
    return [
        [[x - center_x, y - center_y] for x, y in line]
        for line in result
    ]


class DrawingCanvas(QWidget):
    placementRequested = Signal(float, float)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMinimumSize(320, 240)
        self._sheet: dict = default_sheet()
        self._pixels_per_mm = 2.0
        self._pan = QPointF()
        self._panning = False
        self._last_mouse = QPoint()
        self._pending_view: dict | None = None
        self._cursor_sheet_position: tuple[float, float] | None = None
        self._fit_animation: QVariantAnimation | None = None

    def set_sheet(self, sheet: dict, *, fit: bool = True) -> None:
        self._sheet = sheet
        self._pending_view = None
        if fit:
            self.fit_sheet()
        self.update()

    def sheet_size(self) -> tuple[float, float]:
        width, height = SHEET_FORMATS.get(
            str(self._sheet.get("format", "A4")), SHEET_FORMATS["A4"]
        )
        if self._sheet.get("orientation") == "portrait":
            return height, width
        return width, height

    def fit_sheet(self) -> None:
        width, height = self.sheet_size()
        margin = 36.0
        self._pixels_per_mm = max(
            0.01,
            min(
                max(1.0, self.width() - margin * 2.0) / width,
                max(1.0, self.height() - margin * 2.0) / height,
            ),
        )
        self._pan = QPointF()
        self.update()

    def animate_fit_sheet(self, duration_ms: int = 650) -> None:
        width, height = self.sheet_size()
        margin = 36.0
        target_scale = max(
            0.01,
            min(
                max(1.0, self.width() - margin * 2.0) / width,
                max(1.0, self.height() - margin * 2.0) / height,
            ),
        )
        if self._fit_animation is not None:
            self._fit_animation.stop()
        start_scale = self._pixels_per_mm
        start_pan = QPointF(self._pan)
        animation = QVariantAnimation(self)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setDuration(max(1, int(duration_ms)))
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)

        def apply_progress(raw_progress) -> None:
            progress = float(raw_progress)
            self._pixels_per_mm = (
                start_scale + (target_scale - start_scale) * progress
            )
            self._pan = start_pan * (1.0 - progress)
            self.update()

        def finished() -> None:
            if self._fit_animation is animation:
                self._fit_animation = None

        animation.valueChanged.connect(apply_progress)
        animation.finished.connect(finished)
        self._fit_animation = animation
        animation.start()

    def begin_placement(self, view: dict) -> None:
        self._pending_view = view
        self.setFocus()
        self.update()

    def cancel_placement(self) -> None:
        self._pending_view = None
        self._cursor_sheet_position = None
        self.unsetCursor()
        self.update()

    def _paper_origin(self) -> QPointF:
        width, height = self.sheet_size()
        return QPointF(
            self.width() * 0.5 + width * self._pixels_per_mm * 0.5 + self._pan.x(),
            self.height() * 0.5 + height * self._pixels_per_mm * 0.5 + self._pan.y(),
        )

    def _screen_point(self, x: float, y: float) -> QPointF:
        origin = self._paper_origin()
        return QPointF(
            origin.x() - x * self._pixels_per_mm,
            origin.y() - y * self._pixels_per_mm,
        )

    def _sheet_point(self, point: QPointF) -> tuple[float, float]:
        origin = self._paper_origin()
        return (
            (origin.x() - point.x()) / self._pixels_per_mm,
            (origin.y() - point.y()) / self._pixels_per_mm,
        )

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.fillRect(self.rect(), QColor("#000000"))
        width, height = self.sheet_size()
        lower_right = self._screen_point(0.0, 0.0)
        upper_left = self._screen_point(width, height)
        painter.setPen(QPen(QColor("#FFFFFF"), 1.4))
        painter.drawRect(QRectF(
            min(upper_left.x(), lower_right.x()),
            min(upper_left.y(), lower_right.y()),
            abs(lower_right.x() - upper_left.x()),
            abs(lower_right.y() - upper_left.y()),
        ))
        self._draw_origin_indicator(painter)
        for view in self._sheet.get("views", []):
            self._draw_view(painter, view, QColor("#FFFFFF"))
        if self._pending_view is not None and self._cursor_sheet_position is not None:
            preview = dict(self._pending_view)
            preview["x"], preview["y"] = self._cursor_sheet_position
            self._draw_view(painter, preview, QColor("#4DD811"))

    def _draw_origin_indicator(self, painter: QPainter) -> None:
        origin = self._screen_point(15.0, 15.0)
        axis_length = 24.0
        color = QColor("#4DD811")
        painter.setPen(QPen(color, 1.4))
        painter.setBrush(color)
        x_end = QPointF(origin.x() - axis_length, origin.y())
        y_end = QPointF(origin.x(), origin.y() - axis_length)
        painter.drawLine(origin, x_end)
        painter.drawLine(origin, y_end)
        painter.drawPolygon(QPolygonF((
            x_end,
            QPointF(x_end.x() + 6.0, x_end.y() - 3.5),
            QPointF(x_end.x() + 6.0, x_end.y() + 3.5),
        )))
        painter.drawPolygon(QPolygonF((
            y_end,
            QPointF(y_end.x() - 3.5, y_end.y() + 6.0),
            QPointF(y_end.x() + 3.5, y_end.y() + 6.0),
        )))
        painter.drawEllipse(origin, 2.5, 2.5)
        painter.drawText(QPointF(x_end.x() - 11.0, x_end.y() + 4.0), "X")
        painter.drawText(QPointF(y_end.x() + 5.0, y_end.y() + 4.0), "Y")

    def _draw_view(self, painter: QPainter, view: dict, color: QColor) -> None:
        painter.setPen(QPen(color, 1.0))
        scale = float(view.get("scale", 1.0))
        center_x = float(view.get("x", 0.0))
        center_y = float(view.get("y", 0.0))
        for polyline in view.get("polylines", []):
            points = [
                self._screen_point(
                    center_x - float(point[0]) * scale,
                    center_y + float(point[1]) * scale,
                )
                for point in polyline
            ]
            if len(points) >= 2:
                painter.drawPolyline(QPolygonF(points))

    def wheelEvent(self, event: QWheelEvent) -> None:
        before = self._sheet_point(event.position())
        factor = 1.15 if event.angleDelta().y() > 0 else 1.0 / 1.15
        self._pixels_per_mm = max(0.05, min(100.0, self._pixels_per_mm * factor))
        after_screen = self._screen_point(*before)
        self._pan += event.position() - after_screen
        self.update()
        event.accept()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._last_mouse = event.position().toPoint()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._pending_view is not None:
            x, y = self._sheet_point(event.position())
            self.placementRequested.emit(x, y)
            event.accept()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._panning:
            current = event.position().toPoint()
            self._pan += current - self._last_mouse
            self._last_mouse = current
            self.update()
        elif self._pending_view is not None:
            self._cursor_sheet_position = self._sheet_point(event.position())
            self.update()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton and self._panning:
            self._panning = False
            self.unsetCursor()
            event.accept()

    def keyPressEvent(self, event) -> None:
        if event.key() == Qt.Key.Key_Escape and self._pending_view is not None:
            self.cancel_placement()
            event.accept()
            return
        super().keyPressEvent(event)


class DrawingWorkspace(QWidget):
    changed = Signal()
    activeSheetChanged = Signal()
    viewPlaced = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.document: PartDocument | None = None
        self.sheets: list[dict] = []
        self.active_sheet_index = 0
        self.canvas = DrawingCanvas(self)
        self.canvas.placementRequested.connect(self._place_pending_view)
        self._pending_view: dict | None = None

        self.sheet_tabs = QTabBar()
        self.sheet_tabs.setExpanding(False)
        self.sheet_tabs.currentChanged.connect(self._select_sheet)
        add_button = QPushButton("+")
        remove_button = QPushButton("−")
        add_button.setFixedWidth(34)
        remove_button.setFixedWidth(34)
        add_button.clicked.connect(self.add_sheet)
        remove_button.clicked.connect(self.remove_sheet)
        self.format_combo = QComboBox()
        self.format_combo.addItems(SHEET_FORMATS)
        self.format_combo.currentTextChanged.connect(self._change_format)
        self.default_scale_numerator_spin = QDoubleSpinBox()
        self.default_scale_spin = QDoubleSpinBox()
        for spin in (
            self.default_scale_numerator_spin,
            self.default_scale_spin,
        ):
            spin.setRange(1.0, 1000.0)
            spin.setDecimals(0)
            spin.setSingleStep(1.0)
            spin.setValue(1.0)
            spin.valueChanged.connect(self._change_default_scale)

        bottom = QHBoxLayout()
        bottom.setContentsMargins(6, 3, 6, 3)
        bottom.addWidget(self.sheet_tabs, 1)
        bottom.addWidget(remove_button)
        bottom.addWidget(add_button)
        bottom.addSpacing(16)
        bottom.addWidget(QLabel("Měřítko:"))
        bottom.addWidget(self.default_scale_numerator_spin)
        bottom.addWidget(QLabel(":"))
        bottom.addWidget(self.default_scale_spin)
        bottom.addWidget(QLabel("Formát:"))
        bottom.addWidget(self.format_combo)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self.canvas, 1)
        layout.addLayout(bottom)

    def set_document(self, document: PartDocument | None) -> None:
        self.document = document
        self.sheets = drawing_sheets(document) if document is not None else []
        self.active_sheet_index = min(
            int(document.document_settings.get("active_sheet", "0"))
            if document is not None else 0,
            max(0, len(self.sheets) - 1),
        )
        self._refresh_controls(fit=True)

    def active_sheet(self) -> dict | None:
        if 0 <= self.active_sheet_index < len(self.sheets):
            return self.sheets[self.active_sheet_index]
        return None

    def _refresh_controls(self, *, fit: bool = False) -> None:
        self.sheet_tabs.blockSignals(True)
        while self.sheet_tabs.count():
            self.sheet_tabs.removeTab(0)
        for sheet in self.sheets:
            self.sheet_tabs.addTab(str(sheet.get("name", "List")))
        self.sheet_tabs.setCurrentIndex(self.active_sheet_index)
        self.sheet_tabs.blockSignals(False)
        sheet = self.active_sheet()
        if sheet is None:
            return
        self.format_combo.blockSignals(True)
        self.format_combo.setCurrentText(str(sheet.get("format", "A4")))
        self.format_combo.blockSignals(False)
        self.default_scale_numerator_spin.blockSignals(True)
        self.default_scale_spin.blockSignals(True)
        numerator = float(sheet.get("default_scale_numerator", 1.0))
        denominator = float(sheet.get("default_scale", 1.0))
        self.default_scale_numerator_spin.setValue(numerator)
        self.default_scale_spin.setValue(denominator)
        self.default_scale_numerator_spin.blockSignals(False)
        self.default_scale_spin.blockSignals(False)
        self.canvas.set_sheet(sheet, fit=fit)

    def _store(self) -> None:
        if self.document is None:
            return
        store_drawing_sheets(self.document, self.sheets)
        self.document.document_settings["active_sheet"] = str(self.active_sheet_index)
        self.changed.emit()

    def _select_sheet(self, index: int) -> None:
        if not 0 <= index < len(self.sheets):
            return
        self.active_sheet_index = index
        self.canvas.set_sheet(self.sheets[index], fit=True)
        self._store()
        self.activeSheetChanged.emit()

    def add_sheet(self) -> None:
        self.sheets.append(default_sheet(len(self.sheets) + 1))
        self.active_sheet_index = len(self.sheets) - 1
        self._refresh_controls(fit=True)
        self._store()

    def remove_sheet(self) -> None:
        if len(self.sheets) <= 1:
            return
        self.sheets.pop(self.active_sheet_index)
        self.active_sheet_index = min(self.active_sheet_index, len(self.sheets) - 1)
        self._refresh_controls(fit=True)
        self._store()

    def _change_format(self, value: str) -> None:
        sheet = self.active_sheet()
        if sheet is None or value not in SHEET_FORMATS:
            return
        sheet["format"] = value
        sheet["orientation"] = "portrait" if value == "A4" else "landscape"
        self.canvas.set_sheet(sheet, fit=True)
        self._store()

    def _change_default_scale(self, _value: float) -> None:
        sheet = self.active_sheet()
        if sheet is None:
            return
        sheet["default_scale_numerator"] = (
            self.default_scale_numerator_spin.value()
        )
        sheet["default_scale"] = self.default_scale_spin.value()
        self._store()

    def begin_view_placement(self, view: dict) -> None:
        pending = dict(view)
        sheet = self.active_sheet()
        numerator = (
            float(sheet.get("default_scale_numerator", 1.0))
            if sheet else 1.0
        )
        denominator = float(sheet.get("default_scale", 1.0)) if sheet else 1.0
        pending["scale"] = numerator / max(denominator, 0.001)
        self._pending_view = pending
        self.canvas.begin_placement(pending)

    def _place_pending_view(self, x: float, y: float) -> None:
        sheet = self.active_sheet()
        if sheet is None or self._pending_view is None:
            return
        view = dict(self._pending_view)
        view["x"] = x
        view["y"] = y
        sheet.setdefault("views", []).append(view)
        self._pending_view = None
        self.canvas.cancel_placement()
        self._store()
        # Rebind the active sheet after the first placement as well.  A plain
        # repaint can leave a freshly created, previously empty sheet showing
        # only its old canvas state until the document is reopened.
        self.canvas.set_sheet(sheet, fit=False)
        self.viewPlaced.emit()

    def fit_sheet(self) -> None:
        self.canvas.fit_sheet()

    def animate_fit_sheet(self) -> None:
        self.canvas.animate_fit_sheet()
