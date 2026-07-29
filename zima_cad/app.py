from __future__ import annotations

import sys
import copy
import configparser
import io
import json
import math
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Callable

from OCC.Core.gp import gp_Pnt
from OCC.Core.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_REVERSED, TopAbs_VERTEX
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.BRep import BRep_Tool
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCC.Core.GeomAbs import (
    GeomAbs_Line,
    GeomAbs_Plane,
)
from PySide6.QtGui import (
    QAction,
    QActionGroup,
    QBrush,
    QColor,
    QKeySequence,
    QIcon,
    QPalette,
    QPainter,
    QPen,
    QPixmap,
)
from PySide6.QtCore import (
    QByteArray,
    QEvent,
    QLibraryInfo,
    QObject,
    QPoint,
    QPointF,
    QSettings,
    QSize,
    QTime,
    QTimer,
    QTranslator,
    Qt,
    Signal,
)
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import (
    QAbstractSpinBox,
    QApplication,
    QCheckBox,
    QDoubleSpinBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPushButton,
    QRadioButton,
    QSpinBox,
    QSplitter,
    QSizePolicy,
    QTabBar,
    QTableWidget,
    QTableWidgetItem,
    QToolBar,
    QToolButton,
    QTreeWidget,
    QTreeWidgetItem,
    QHBoxLayout,
    QSizeGrip,
    QSplashScreen,
    QVBoxLayout,
    QWidget,
)

from zima_cad.model import (
    CombineMode,
    ContainerType,
    EntityKind,
    OriginScope,
    SOLID_KINDS,
    SketchRole,
    PartDocument,
    PlaneOnFaceAttachment,
    TreeExposure,
    ZimaEntity,
    solid_face_frames,
    coordinate_system_transform,
    create_empty_part,
    identity_transform,
    multiply_transforms,
    entity_world_transform,
    transform_point,
)
from zima_cad.paths import app_path, application_root, ensure_application_directories
from zima_cad.settings import (
    ApplicationSettings,
    StartupContext,
    load_application_settings,
    portable_config_path,
    resolve_startup_context,
)
from zima_cad.localization import configure_localization, tr
from zima_cad.viewer import LinearDimension, ZimaOpenGLViewer
from zima_cad.viewer_scene import (
    DocumentViewerScene,
    build_document_viewer_scene_data,
)
from zima_cad.viewer_mesh import ViewerMesh, triangulate_shape
from zima_cad.storage import (
    ContainerEntityLimitError,
    load_part_document,
    save_part_document,
)
from zima_cad.versioned_io import validate_ini_file, write_text_versioned

_RESOURCE_ICON_CACHE: dict[tuple[str, str], QIcon] = {}


def display_decimal_places(
    parent: QWidget | None = None,
    document: PartDocument | None = None,
) -> int:
    active_document = document or getattr(parent, "document", None)
    try:
        value = int(
            active_document.document_precision.get("decimal_places", "3")
        )
    except (AttributeError, TypeError, ValueError):
        value = 3
    return max(0, min(12, value))


def resource_icon(name: str) -> QIcon:
    path = app_path("resources", "icons", f"{name}.svg")
    application = QApplication.instance()
    if application is None:
        return QIcon(str(path))

    # Qt does not consistently resolve SVG currentColor against the widget
    # palette. Render the icon with an explicit palette colour so transparent
    # SVG artwork remains legible in both light and dark themes.
    color = application.palette().color(QPalette.ColorRole.WindowText).name()
    cache_key = (name, color)
    if cache_key in _RESOURCE_ICON_CACHE:
        return _RESOURCE_ICON_CACHE[cache_key]
    svg = path.read_text(encoding="utf-8").replace("currentColor", color)
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    if not renderer.isValid():
        return QIcon(str(path))

    icon = QIcon()
    for size in (16, 20, 24, 32, 48):
        pixmap = QPixmap(QSize(size, size))
        pixmap.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pixmap)
        renderer.render(painter)
        painter.end()
        icon.addPixmap(pixmap)
    _RESOURCE_ICON_CACHE[cache_key] = icon
    return icon


TREE_ICON_NAMES = {
    EntityKind.PART: "part",
    EntityKind.CONTAINER: "part",
    EntityKind.BODY: "part",
    EntityKind.ORIGIN: "origin",
    EntityKind.POINT: "point",
    EntityKind.AXIS: "axis",
    EntityKind.PLANE: "plane",
    EntityKind.SKETCH: "sketch",
    EntityKind.BOX: "box",
    EntityKind.SPHERE: "sphere",
    EntityKind.CYLINDER: "cylinder",
    EntityKind.CONE: "cone",
    EntityKind.PYRAMID: "pyramid",
    EntityKind.WEDGE: "wedge",
}


def localize_dialog_buttons(buttons: QDialogButtonBox) -> None:
    ok_button = buttons.button(QDialogButtonBox.StandardButton.Ok)
    cancel_button = buttons.button(QDialogButtonBox.StandardButton.Cancel)
    apply_button = buttons.button(QDialogButtonBox.StandardButton.Apply)
    if ok_button is not None:
        ok_button.setText(tr("button.ok"))
    if cancel_button is not None:
        cancel_button.setText(tr("button.cancel"))
    if apply_button is not None:
        apply_button.setText(tr("button.apply"))


class DialogMiddleButtonFilter(QObject):
    """Provide the shared middle-click Apply/OK convention to dialogs."""

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._dialog: QDialog | None = None
        self._origin: QPointF | None = None
        self._moved = False
        self._chord = False
        self._suppress_release = False

    @staticmethod
    def _event_dialog(watched) -> QDialog | None:
        if not isinstance(watched, QWidget):
            return None
        widget: QWidget | None = watched
        while widget is not None:
            if isinstance(widget, QDialog):
                return (
                    None
                    if getattr(
                        widget,
                        "_handles_middle_confirmation",
                        False,
                    )
                    else widget
                )
            widget = widget.parentWidget()
        application = QApplication.instance()
        target = (
            getattr(
                application,
                "_middle_confirmation_target",
                None,
            )
            if application is not None
            else None
        )
        if (
            isinstance(target, QDialog)
            and target.isVisible()
            and not getattr(
                target,
                "_handles_middle_confirmation",
                False,
            )
        ):
            parent = target.parentWidget()
            if (
                watched.window() is target
                or (
                    parent is not None
                    and watched.window() is parent.window()
                )
            ):
                return target
        window = watched.window()
        if (
            not isinstance(window, QDialog)
            or getattr(window, "_handles_middle_confirmation", False)
        ):
            return None
        return window

    @staticmethod
    def _click_standard_button(
        dialog: QDialog,
        standard_button: QDialogButtonBox.StandardButton,
    ) -> bool:
        for button_box in dialog.findChildren(QDialogButtonBox):
            button = button_box.button(standard_button)
            if (
                button is not None
                and button.isVisible()
                and button.isEnabled()
            ):
                button.click()
                return True
        return False

    def eventFilter(self, watched, event) -> bool:
        dialog = self._event_dialog(watched)
        if dialog is None:
            return False
        if (
            event.type() == QEvent.Type.MouseButtonPress
            and event.button() == Qt.MouseButton.MiddleButton
        ):
            self._dialog = dialog
            self._origin = event.globalPosition()
            self._moved = False
            self._chord = bool(
                event.buttons() & Qt.MouseButton.RightButton
            )
            self._suppress_release = False
        elif (
            event.type() == QEvent.Type.MouseButtonPress
            and event.button() == Qt.MouseButton.RightButton
            and self._origin is not None
        ):
            self._chord = True
        elif (
            event.type() == QEvent.Type.MouseMove
            and self._origin is not None
            and event.buttons() & Qt.MouseButton.MiddleButton
        ):
            delta = event.globalPosition() - self._origin
            if abs(delta.x()) + abs(delta.y()) > 3.0:
                self._moved = True
            if event.buttons() & Qt.MouseButton.RightButton:
                self._chord = True
        elif (
            event.type() == QEvent.Type.MouseButtonDblClick
            and event.button() == Qt.MouseButton.MiddleButton
        ):
            self._suppress_release = True
            self._origin = None
            self._click_standard_button(
                dialog,
                QDialogButtonBox.StandardButton.Ok,
            )
            event.accept()
            return True
        elif (
            event.type() == QEvent.Type.MouseButtonRelease
            and event.button() == Qt.MouseButton.MiddleButton
        ):
            apply = (
                self._dialog is dialog
                and self._origin is not None
                and not self._moved
                and not self._chord
                and not self._suppress_release
            )
            self._dialog = None
            self._origin = None
            self._moved = False
            self._chord = False
            self._suppress_release = False
            if apply:
                self._click_standard_button(
                    dialog,
                    QDialogButtonBox.StandardButton.Apply,
                )
        return False


def position_dialog_top_right(dialog: QDialog) -> None:
    parent = dialog.parentWidget()
    if (
        parent is not None
        and dialog.windowFlags() & Qt.WindowType.SubWindow
    ):
        margin = 12
        frame = dialog.frameGeometry()
        bounds = (
            parent.centralWidget().geometry()
            if (
                isinstance(parent, QMainWindow)
                and parent.centralWidget() is not None
            )
            else parent.rect()
        )
        dialog.move(
            max(bounds.left() + margin, bounds.right() - frame.width() - margin),
            bounds.top() + margin,
        )
        dialog.raise_()
        return
    reference = parent.window() if parent is not None else None
    screen = reference.screen() if reference is not None else dialog.screen()
    available = screen.availableGeometry()
    margin = 24
    frame = dialog.frameGeometry()
    x = available.right() - frame.width() - margin
    y = available.top() + margin
    dialog.move(max(available.left() + margin, x), y)


def position_dialog_top_right_after_show(dialog: QDialog) -> None:
    # Some window managers replace the requested geometry while mapping the
    # window. Reapply it after both the first and the settled event cycle.
    position_dialog_top_right(dialog)
    QTimer.singleShot(0, lambda: position_dialog_top_right(dialog))
    QTimer.singleShot(100, lambda: position_dialog_top_right(dialog))


def create_saved_status_label() -> QLabel:
    label = QLabel("")
    label.setStyleSheet("color: #2e9b4f;")
    label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    return label


class NoWheelComboBox(QComboBox):
    def wheelEvent(self, event) -> None:
        event.ignore()


class HistoryTreeWidget(QTreeWidget):
    historyCursorMoved = Signal(int)
    historyObjectMoved = Signal(str, int)
    ROLLBACK_ROLE = int(Qt.ItemDataRole.UserRole) + 1
    HISTORY_OBJECT_ROLE = int(Qt.ItemDataRole.UserRole) + 2
    SKETCH_ENTITY_ROLE = int(Qt.ItemDataRole.UserRole) + 3
    SKETCH_REFERENCE_ROLE = int(Qt.ItemDataRole.UserRole) + 4
    SKETCH_EXTERNAL_REFERENCE_ROLE = int(Qt.ItemDataRole.UserRole) + 5
    SKETCH_CONSTRAINT_ROLE = int(Qt.ItemDataRole.UserRole) + 6
    SKETCH_POINT_LINK_ROLE = int(Qt.ItemDataRole.UserRole) + 7
    SKETCH_GEOMETRY_CONSTRAINT_ROLE = int(
        Qt.ItemDataRole.UserRole
    ) + 8

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._dragging_rollback = False
        self._pending_history_object_id: str | None = None
        self._dragging_history_object = False
        self._drag_start = QPoint()
        self._insertion_line_y: int | None = None

    def _set_history_drag_active(self, active: bool) -> None:
        if bool(self.property("historyDragActive")) == active:
            return
        self.setProperty("historyDragActive", active)
        if active:
            self.clearSelection()
            self.setCurrentItem(None)
        self.style().unpolish(self)
        self.style().polish(self)
        self.viewport().update()

    def _set_rollback_hidden(self, hidden: bool) -> None:
        for index in range(self.topLevelItemCount()):
            item = self.topLevelItem(index)
            if item.data(0, self.ROLLBACK_ROLE):
                item.setHidden(hidden)

    def _update_insertion_line(self, position: QPoint) -> None:
        boundaries: list[int] = []
        for index in range(self.topLevelItemCount()):
            item = self.topLevelItem(index)
            if not item.data(0, self.HISTORY_OBJECT_ROLE) or item.isHidden():
                continue
            rect = self.visualItemRect(item)
            boundaries.append(rect.top())
            boundaries.append(rect.bottom() + 1)
        self._insertion_line_y = (
            min(boundaries, key=lambda value: abs(value - position.y()))
            if boundaries
            else position.y()
        )
        self.viewport().update()

    def _finish_insertion_line(self) -> None:
        self._insertion_line_y = None
        self._set_rollback_hidden(False)
        self._set_history_drag_active(False)
        self.viewport().update()

    def paintEvent(self, event) -> None:
        super().paintEvent(event)
        if self._insertion_line_y is None:
            return
        painter = QPainter(self.viewport())
        painter.setPen(QPen(QColor("#4DD811"), 2))
        margin = 4
        painter.drawLine(
            margin,
            self._insertion_line_y,
            self.viewport().width() - margin,
            self._insertion_line_y,
        )

    def mousePressEvent(self, event) -> None:
        item = self.itemAt(event.position().toPoint())
        if (
            event.button() == Qt.MouseButton.RightButton
            and item is not None
            and (
                item.data(0, self.SKETCH_CONSTRAINT_ROLE) is not None
                or item.data(
                    0,
                    self.SKETCH_GEOMETRY_CONSTRAINT_ROLE,
                )
                is not None
            )
        ):
            # Keep the expanded point branch intact. QTreeWidget otherwise
            # selects the constraint's parent geometry on right press, which
            # rebuilds the Sketch tree before its context menu can open.
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and item is not None
            and item.data(0, self.ROLLBACK_ROLE)
            and self.columnAt(event.position().toPoint().x()) == 0
        ):
            self.setCurrentItem(item)
            self._dragging_rollback = True
            self.viewport().setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        super().mousePressEvent(event)
        if (
            event.button() == Qt.MouseButton.LeftButton
            and item is not None
            and item.data(0, self.HISTORY_OBJECT_ROLE)
        ):
            self._pending_history_object_id = item.data(
                0, Qt.ItemDataRole.UserRole
            )
            self._drag_start = event.position().toPoint()

    def mouseMoveEvent(self, event) -> None:
        if self._dragging_rollback:
            self._set_history_drag_active(True)
            self._set_rollback_hidden(True)
            self._update_insertion_line(event.position().toPoint())
            event.accept()
            return
        if (
            self._pending_history_object_id is not None
            and event.buttons() & Qt.MouseButton.LeftButton
            and (
                event.position().toPoint() - self._drag_start
            ).manhattanLength() >= QApplication.startDragDistance()
        ):
            self._dragging_history_object = True
            self._set_history_drag_active(True)
            self._set_rollback_hidden(True)
            self._update_insertion_line(event.position().toPoint())
            self.viewport().setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        if self._dragging_history_object:
            self._update_insertion_line(event.position().toPoint())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        if self._dragging_history_object:
            entity_id = self._pending_history_object_id
            self._pending_history_object_id = None
            self._dragging_history_object = False
            self.viewport().unsetCursor()
            self._finish_insertion_line()
            y = event.position().toPoint().y()
            target_index = 0
            for index in range(self.topLevelItemCount()):
                item = self.topLevelItem(index)
                if (
                    not item.data(0, self.HISTORY_OBJECT_ROLE)
                    or item.data(0, Qt.ItemDataRole.UserRole) == entity_id
                ):
                    continue
                if self.visualItemRect(item).center().y() < y:
                    target_index += 1
            if entity_id is not None:
                self.historyObjectMoved.emit(entity_id, target_index)
            event.accept()
            return
        self._pending_history_object_id = None
        if not self._dragging_rollback:
            super().mouseReleaseEvent(event)
            return
        self._dragging_rollback = False
        self.viewport().unsetCursor()
        self._finish_insertion_line()
        y = event.position().toPoint().y()
        cursor = 0
        for index in range(self.topLevelItemCount()):
            item = self.topLevelItem(index)
            if not item.data(0, self.HISTORY_OBJECT_ROLE):
                continue
            if self.visualItemRect(item).center().y() < y:
                cursor += 1
        self.historyCursorMoved.emit(cursor)
        event.accept()


class ViewDisplayMode(str, Enum):
    WIRE = "wire"
    SHADED_WITH_EDGES = "shaded_with_edges"
    SHADED = "shaded"


class ViewSelectionMode(str, Enum):
    CONTAINER = "container"
    FACE = "face"


class ViewSelectionFilter(str, Enum):
    ALL = "all"
    FACE = "face"
    POINT = "point"
    AXIS = "axis"
    PLANE = "plane"


class ApplicationMode(str, Enum):
    MODELING = "modeling"
    SHEET_METAL = "sheet_metal"
    SURFACE = "surface"
    PIPING = "piping"


@dataclass
class DocumentSession:
    document: PartDocument
    file_path: Path | None
    selected_object_id: str | None = None
    active_application: ApplicationMode = ApplicationMode.MODELING


class ApplicationWorkspace(QObject):
    sessionsChanged = Signal(object)
    documentChanged = Signal(object, object)

    def __init__(self) -> None:
        super().__init__()
        self.document_sessions: list[DocumentSession] = []
        self.windows: list[MainWindow] = []


def canonical_document_path(file_path: Path) -> Path:
    """Normalize relative paths and symlinks for open-document comparisons."""
    return file_path.expanduser().resolve(strict=False)


class NewDocumentDialog(QDialog):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.new.title"))
        self.setMinimumWidth(420)

        layout = QVBoxLayout(self)

        form = QFormLayout()
        self.file_name_edit = QLineEdit("part")
        form.addRow(tr("dialog.new.file_name"), self.file_name_edit)
        layout.addLayout(form)

        layout.addWidget(QLabel(tr("dialog.new.document_type")))

        self.part_radio = QRadioButton(tr("dialog.new.part"))
        self.assembly_radio = QRadioButton(tr("dialog.new.assembly"))
        self.drawing_radio = QRadioButton(tr("dialog.new.drawing"))
        self.part_radio.setIcon(resource_icon("part"))
        self.assembly_radio.setIcon(resource_icon("assembly"))
        self.drawing_radio.setIcon(resource_icon("drawing"))
        self.part_radio.setChecked(True)

        layout.addWidget(self.part_radio)
        layout.addWidget(self.assembly_radio)
        layout.addWidget(self.drawing_radio)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def selected_document_type(self) -> str:
        if self.assembly_radio.isChecked():
            return "assembly"
        if self.drawing_radio.isChecked():
            return "drawing"
        return "part"

    def file_stem(self) -> str:
        return Path(self.file_name_edit.text().strip()).stem

    def accept(self) -> None:
        if not self.file_stem():
            QMessageBox.information(
                self, tr("dialog.new.title"), tr("message.required.file_name")
            )
            return
        super().accept()


class ContainerSummaryDialog(QDialog):
    applied = Signal()

    def __init__(self, obj: ZimaEntity, document: PartDocument, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.properties.title", name=obj.name))
        self.object = obj
        self.document = document
        self.decimal_places = display_decimal_places(document=document)
        self.detach_requested = False
        self.primary_combo = None
        self.secondary_combo = None
        self.flip_checkbox = None

        layout = QFormLayout(self)

        self.name_edit = QLineEdit(obj.name)
        self.x_spin = self._create_position_spinbox()
        self.y_spin = self._create_position_spinbox()
        self.z_spin = self._create_position_spinbox()
        self.rx_spin = self._create_rotation_spinbox()
        self.ry_spin = self._create_rotation_spinbox()
        self.rz_spin = self._create_rotation_spinbox()
        x, y, z = obj.coordinate_system.origin
        self.x_spin.setValue(float(x))
        self.y_spin.setValue(float(y))
        self.z_spin.setValue(float(z))
        rx, ry, rz = obj.coordinate_system.rotation
        self.rx_spin.setValue(float(rx))
        self.ry_spin.setValue(float(ry))
        self.rz_spin.setValue(float(rz))

        layout.addRow(tr("dialog.properties.name"), self.name_edit)
        layout.addRow(
            tr("dialog.properties.container_type"),
            QLabel(obj.container_type.value),
        )
        layout.addRow("X", self.x_spin)
        layout.addRow("Y", self.y_spin)
        layout.addRow("Z", self.z_spin)
        layout.addRow("RX", self.rx_spin)
        layout.addRow("RY", self.ry_spin)
        layout.addRow("RZ", self.rz_spin)
        attachment = obj.attachment
        if attachment is not None:
            target = document.find_entity(attachment.target_object_id)
            target_name = target.name if target is not None else attachment.target_object_id
            self.attachment_status_label = QLabel(
                tr(f"attachment.status.{attachment.status}")
            )
            layout.addRow(tr("dialog.properties.attachment_status"), self.attachment_status_label)
            layout.addRow(
                tr("dialog.attachment.source"),
                QLabel(attachment.source_plane.upper()),
            )
            layout.addRow(
                tr("dialog.properties.attachment_target"),
                QLabel(f"{target_name} / {attachment.target_face_role}"),
            )
            self.primary_combo = QComboBox()
            self.secondary_combo = QComboBox()
            for axis in ("X", "Y", "Z"):
                self.primary_combo.addItem(axis, axis.lower())
                self.secondary_combo.addItem(axis, axis.lower())
            self.primary_combo.setCurrentIndex(
                max(0, self.primary_combo.findData(attachment.primary_axis))
            )
            self.secondary_combo.setCurrentIndex(
                max(0, self.secondary_combo.findData(attachment.secondary_axis))
            )
            self.flip_checkbox = QCheckBox(tr("dialog.attachment.flip_normal"))
            self.flip_checkbox.setChecked(attachment.flip_normal)
            layout.addRow(tr("dialog.attachment.primary_axis"), self.primary_combo)
            layout.addRow(tr("dialog.attachment.secondary_axis"), self.secondary_combo)
            layout.addRow(self.flip_checkbox)
            detach_button = QPushButton(tr("dialog.properties.detach_attachment"))
            detach_button.clicked.connect(self._request_detach)
            layout.addRow(detach_button)
            if attachment.status in ("resolved", "fallback_axis"):
                self._set_transform_read_only(True)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        apply_button = buttons.button(QDialogButtonBox.StandardButton.Apply)
        apply_button.clicked.connect(self._apply_without_closing)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        position_dialog_top_right_after_show(self)

    def _apply_without_closing(self) -> None:
        if not self._validate_attachment_axes():
            return
        if not self.apply_to_object():
            return
        self.applied.emit()
        self._load_transform_values()

    def _load_transform_values(self) -> None:
        x, y, z = self.object.coordinate_system.origin
        self.x_spin.setValue(float(x))
        self.y_spin.setValue(float(y))
        self.z_spin.setValue(float(z))
        rx, ry, rz = self.object.coordinate_system.rotation
        self.rx_spin.setValue(float(rx))
        self.ry_spin.setValue(float(ry))
        self.rz_spin.setValue(float(rz))

    def _set_transform_read_only(self, read_only: bool) -> None:
        for spinbox in (
            self.x_spin,
            self.y_spin,
            self.z_spin,
            self.rx_spin,
            self.ry_spin,
            self.rz_spin,
        ):
            spinbox.setReadOnly(read_only)

    def _request_detach(self) -> None:
        self.detach_requested = True
        self._set_transform_read_only(False)
        self.attachment_status_label.setText(
            tr("dialog.properties.attachment_will_detach")
        )
        if self.primary_combo is not None:
            self.primary_combo.setEnabled(False)
        if self.secondary_combo is not None:
            self.secondary_combo.setEnabled(False)
        if self.flip_checkbox is not None:
            self.flip_checkbox.setEnabled(False)

    def accept(self) -> None:
        if not self._validate_attachment_axes():
            return
        super().accept()

    def _validate_attachment_axes(self) -> bool:
        invalid = (
            not self.detach_requested
            and self.object.attachment is not None
            and self.primary_combo is not None
            and self.secondary_combo is not None
            and self.primary_combo.currentData() == self.secondary_combo.currentData()
        )
        if invalid:
            QMessageBox.information(
                self,
                tr("dialog.properties.title", name=self.object.name),
                tr("message.attachment.axes_must_differ"),
            )
            return False
        return True

    def _create_position_spinbox(self) -> QDoubleSpinBox:
        spinbox = QDoubleSpinBox()
        spinbox.setRange(-1_000_000.0, 1_000_000.0)
        spinbox.setDecimals(self.decimal_places)
        spinbox.setSingleStep(1.0)
        spinbox.setSuffix(" mm")
        return spinbox

    def _create_rotation_spinbox(self) -> QDoubleSpinBox:
        spinbox = QDoubleSpinBox()
        spinbox.setRange(-360_000.0, 360_000.0)
        spinbox.setDecimals(self.decimal_places)
        spinbox.setSingleStep(5.0)
        spinbox.setSuffix(" deg")
        return spinbox

    def apply_to_object(self) -> bool:
        name = self.name_edit.text().strip()
        if not name:
            return False

        self.object.name = name
        self.object.coordinate_system.origin = (
            self.x_spin.value(),
            self.y_spin.value(),
            self.z_spin.value(),
        )
        self.object.coordinate_system.rotation = (
            self.rx_spin.value(),
            self.ry_spin.value(),
            self.rz_spin.value(),
        )
        self.object.show_internal_entities = True
        if self.detach_requested:
            self.object.attachment = None
        elif self.object.attachment is not None and self.primary_combo is not None:
            self.object.attachment.primary_axis = str(self.primary_combo.currentData())
            self.object.attachment.secondary_axis = str(
                self.secondary_combo.currentData()
            )
            self.object.attachment.flip_normal = self.flip_checkbox.isChecked()
        return True


class PrimitivePropertiesDialog(QDialog):
    applied = Signal()
    PARAMETER_DEFINITIONS = {
        EntityKind.BOX: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        EntityKind.SPHERE: (
            ("diameter", "primitive.parameter.diameter", 0.001),
        ),
        EntityKind.CYLINDER: (
            ("diameter", "primitive.parameter.diameter", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        EntityKind.CONE: (
            ("bottom_diameter", "primitive.parameter.bottom_diameter", 0.001),
            ("top_diameter", "primitive.parameter.top_diameter", 0.0),
            ("height", "primitive.parameter.height", 0.001),
        ),
        EntityKind.PYRAMID: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        EntityKind.WEDGE: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
            ("top_offset", "primitive.parameter.top_offset", 0.0),
        ),
    }

    def __init__(self, primitive: ZimaEntity, parent=None) -> None:
        super().__init__(parent)
        self.primitive = primitive
        self.decimal_places = display_decimal_places(parent)
        self.setWindowTitle(
            tr("dialog.container_properties.title", name=primitive.name)
        )

        layout = QFormLayout(self)
        self.name_edit = QLineEdit(primitive.name)
        layout.addRow(tr("dialog.properties.name"), self.name_edit)
        layout.addRow(
            tr("dialog.properties.container_type"),
            QLabel(tr(f"primitive.{primitive.kind.value}")),
        )

        self.parameter_edits: dict[str, QDoubleSpinBox] = {}
        for key, label_key, minimum in self.PARAMETER_DEFINITIONS[primitive.kind]:
            spinbox = QDoubleSpinBox()
            spinbox.setRange(minimum, 1_000_000.0)
            spinbox.setDecimals(self.decimal_places)
            spinbox.setSingleStep(1.0)
            spinbox.setSuffix(" mm")
            spinbox.setValue(float(primitive.parameters.get(key, minimum)))
            self.parameter_edits[key] = spinbox
            layout.addRow(tr(label_key), spinbox)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self._apply_without_closing
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        position_dialog_top_right_after_show(self)

    def _apply_without_closing(self) -> None:
        if self.apply_to_primitive():
            self.applied.emit()

    def apply_to_primitive(self) -> bool:
        name = self.name_edit.text().strip()
        if not name:
            return False
        self.primitive.name = name
        for key, spinbox in self.parameter_edits.items():
            self.primitive.parameters[key] = f"{spinbox.value():.12g}"
        return True


class PointConstraintDialog(QDialog):
    createRequested = Signal(list, tuple, str, bool, bool)
    updateRequested = Signal(list, tuple, str, bool, bool)
    referenceActivated = Signal(dict)
    definitionChanged = Signal()
    applied = Signal()
    entityAdopted = Signal()

    def __init__(
        self,
        solve_callback,
        parent=None,
        *,
        point_object: ZimaEntity | None = None,
        point_entity: ZimaEntity | None = None,
        suggested_name: str = "",
        reference_exists_callback: Callable[[str], bool] | None = None,
        reference_kind_callback: Callable[[str], EntityKind | None] | None = None,
    ) -> None:
        super().__init__(parent)
        self._handles_middle_confirmation = True
        if isinstance(parent, QWidget):
            self.setWindowFlags(
                Qt.WindowType.SubWindow
                | Qt.WindowType.WindowTitleHint
                | Qt.WindowType.WindowCloseButtonHint
            )
            self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
            self.setAutoFillBackground(True)
        self.solve_callback = solve_callback
        self.decimal_places = display_decimal_places(parent)
        self.point_object = point_object
        self.point_entity = point_entity
        self.reference_exists_callback = (
            reference_exists_callback or (lambda _object_id: True)
        )
        self.reference_kind_callback = (
            reference_kind_callback or (lambda _object_id: None)
        )
        self.edit_mode = point_object is not None and point_entity is not None
        self.references = self._stored_references(point_entity)
        self.highlighted_reference_keys = {
            str(reference.get("key", ""))
            for reference in self.references
        }
        self._middle_click_origin: QPointF | None = None
        self._middle_click_moved = False
        self._middle_click_chord = False
        self._title_drag_origin: QPointF | None = None
        self._title_drag_window_origin: QPoint | None = None
        self._references_being_removed: set[str] = set()
        self.setModal(False)
        self.resize(460, 520)

        layout = QVBoxLayout(self)
        if self.windowFlags() & Qt.WindowType.SubWindow:
            self.setObjectName("propertiesSubWindow")
            self.setStyleSheet(
                "QDialog#propertiesSubWindow {"
                " background: palette(window);"
                " border: 1px solid palette(mid);"
                " border-radius: 5px;"
                "}"
            )
            layout.setContentsMargins(8, 6, 8, 8)
            self._internal_title_bar = QWidget(self)
            self._internal_title_bar.setObjectName("propertiesTitleBar")
            self._internal_title_bar.setFixedHeight(34)
            self._internal_title_bar.setCursor(
                Qt.CursorShape.SizeAllCursor
            )
            self._internal_title_bar.setStyleSheet(
                "QWidget#propertiesTitleBar {"
                " background: palette(midlight);"
                " border: 1px solid palette(mid);"
                " border-radius: 4px;"
                "}"
            )
            title_layout = QHBoxLayout(self._internal_title_bar)
            title_layout.setContentsMargins(10, 2, 4, 2)
            self._internal_title_label = QLabel(self.windowTitle())
            title_font = self._internal_title_label.font()
            title_font.setBold(True)
            self._internal_title_label.setFont(title_font)
            title_layout.addWidget(self._internal_title_label, 1)
            close_button = QPushButton("×")
            close_button.setFixedSize(27, 26)
            close_button.setToolTip(tr("button.cancel"))
            close_button.setStyleSheet(
                "QPushButton { border: none; border-radius: 4px;"
                " font-size: 18px; font-weight: 700; }"
                "QPushButton:hover { background: #b83232; color: white; }"
            )
            close_button.clicked.connect(self.reject)
            title_layout.addWidget(close_button)
            self._internal_title_bar.installEventFilter(self)
            self.windowTitleChanged.connect(
                self._internal_title_label.setText
            )
            layout.addWidget(self._internal_title_bar)
        general = QFormLayout()
        self.name_edit = QLineEdit(
            point_object.name if point_object is not None else suggested_name
        )
        self.name_edit.textChanged.connect(self._update_window_title)
        self._update_window_title()
        general.addRow(tr("dialog.properties.name"), self.name_edit)
        self.container_type_combo = QComboBox()
        for container_type in ContainerType:
            self.container_type_combo.addItem(
                tr(f"container.type.{container_type.value.lower()}"),
                container_type.value,
            )
        self.container_type_combo.setCurrentIndex(
            max(
                0,
                self.container_type_combo.findData(
                    ContainerType.POINT.value
                ),
            )
        )
        self.container_type_combo.setEnabled(False)
        general.addRow(
            tr("dialog.properties.container_type"),
            self.container_type_combo,
        )
        layout.addLayout(general)
        layout.addWidget(QLabel(tr("dialog.point_constraints.instructions")))
        self.reference_status_label = QLabel()
        self.reference_status_label.setStyleSheet(
            "color: #80AA1A; font-weight: 700;"
        )
        self.reference_status_label.setWordWrap(True)
        layout.addWidget(self.reference_status_label)
        self._normalize_reference_orientation_roles()
        self.reference_list = QTableWidget(0, 4)
        self.reference_list.setStyleSheet(
            "QTableWidget::item:selected {"
            " background-color: #00d1ff; color: #102027;"
            "}"
        )
        self.reference_list.setHorizontalHeaderLabels(
            [
                "",
                tr("dialog.point_constraints.reference"),
                tr("dialog.point_constraints.offset"),
                tr("dialog.point_constraints.orientation"),
            ]
        )
        self.reference_list.horizontalHeader().setSectionResizeMode(
            0,
            QHeaderView.ResizeMode.ResizeToContents,
        )
        self.reference_list.horizontalHeader().setSectionResizeMode(
            1,
            QHeaderView.ResizeMode.Stretch,
        )
        self.reference_list.horizontalHeader().setSectionResizeMode(
            2,
            QHeaderView.ResizeMode.ResizeToContents,
        )
        self.reference_list.horizontalHeader().setSectionResizeMode(
            3,
            QHeaderView.ResizeMode.ResizeToContents,
        )
        for reference in self.references:
            self._append_reference_row(reference)
        self._refresh_reference_item_warnings()
        self.reference_list.cellClicked.connect(
            self._reference_cell_clicked
        )
        layout.addWidget(self.reference_list, 1)
        coordinates = QFormLayout()
        self.coordinate_edits: list[QDoubleSpinBox] = []
        for axis in ("X", "Y", "Z"):
            edit = QDoubleSpinBox()
            edit.setRange(-1_000_000_000.0, 1_000_000_000.0)
            edit.setDecimals(self.decimal_places)
            edit.setSuffix(" mm")
            edit.valueChanged.connect(self._update_solution)
            coordinates.addRow(axis, edit)
            self.coordinate_edits.append(edit)
        fallback = self._stored_fallback(point_object, point_entity)
        for edit, value in zip(self.coordinate_edits, fallback):
            edit.blockSignals(True)
            edit.setValue(value)
            edit.blockSignals(False)
        layout.addLayout(coordinates)
        self.dof_label = QLabel()
        self.result_label = QLabel()
        layout.addWidget(self.dof_label)
        layout.addWidget(self.result_label)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        self.ok_button = buttons.button(QDialogButtonBox.StandardButton.Ok)
        self.ok_button.clicked.connect(self._submit_and_accept)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self._apply
        )
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self._update_solution()
        application = QApplication.instance()
        if application is not None:
            application.installEventFilter(self)

    def _set_container_type(
        self,
        container_type: ContainerType,
        *,
        editable: bool = False,
    ) -> None:
        self.container_type_combo.setCurrentIndex(
            max(
                0,
                self.container_type_combo.findData(container_type.value),
            )
        )
        self.container_type_combo.setEnabled(editable)

    def eventFilter(self, watched, event) -> bool:
        if watched is getattr(self, "_internal_title_bar", None):
            if (
                event.type() == QEvent.Type.MouseButtonPress
                and event.button() == Qt.MouseButton.LeftButton
            ):
                self._title_drag_origin = event.globalPosition()
                self._title_drag_window_origin = self.pos()
                event.accept()
                return True
            if (
                event.type() == QEvent.Type.MouseMove
                and self._title_drag_origin is not None
                and self._title_drag_window_origin is not None
                and event.buttons() & Qt.MouseButton.LeftButton
            ):
                delta = event.globalPosition() - self._title_drag_origin
                parent = self.parentWidget()
                target = self._title_drag_window_origin + QPoint(
                    int(delta.x()),
                    int(delta.y()),
                )
                if parent is not None:
                    target.setX(
                        max(
                            0,
                            min(target.x(), parent.width() - self.width()),
                        )
                    )
                    target.setY(
                        max(
                            0,
                            min(target.y(), parent.height() - 34),
                        )
                    )
                self.move(target)
                event.accept()
                return True
            if (
                event.type() == QEvent.Type.MouseButtonRelease
                and event.button() == Qt.MouseButton.LeftButton
            ):
                self._title_drag_origin = None
                self._title_drag_window_origin = None
                event.accept()
                return True
        spinbox = (
            watched
            if isinstance(watched, QAbstractSpinBox)
            else (
                watched.parentWidget()
                if isinstance(watched, QWidget)
                and isinstance(watched.parentWidget(), QAbstractSpinBox)
                else None
            )
        )
        if (
            event.type() == QEvent.Type.KeyPress
            and event.key() in (
                Qt.Key.Key_Return,
                Qt.Key.Key_Enter,
            )
            and spinbox is not None
            and watched.window() is self
        ):
            spinbox.interpretText()
            event.accept()
            return True
        if (
            event.type() == QEvent.Type.MouseButtonPress
            and event.button() == Qt.MouseButton.MiddleButton
        ):
            self._middle_click_origin = event.globalPosition()
            self._middle_click_moved = False
            self._middle_click_chord = bool(
                event.buttons() & Qt.MouseButton.RightButton
            )
        elif (
            event.type() == QEvent.Type.MouseButtonPress
            and event.button() == Qt.MouseButton.RightButton
            and self._middle_click_origin is not None
        ):
            self._middle_click_chord = True
        elif (
            event.type() == QEvent.Type.MouseMove
            and self._middle_click_origin is not None
            and event.buttons() & Qt.MouseButton.MiddleButton
        ):
            delta = event.globalPosition() - self._middle_click_origin
            if abs(delta.x()) + abs(delta.y()) > 3.0:
                self._middle_click_moved = True
            if event.buttons() & Qt.MouseButton.RightButton:
                self._middle_click_chord = True
        elif (
            event.type() == QEvent.Type.MouseButtonRelease
            and event.button() == Qt.MouseButton.MiddleButton
            and self._middle_click_origin is not None
        ):
            should_apply = (
                not self._middle_click_moved
                and not self._middle_click_chord
            )
            self._middle_click_origin = None
            self._middle_click_moved = False
            self._middle_click_chord = False
            if should_apply:
                self._apply()
        if (
            event.type() == QEvent.Type.MouseButtonDblClick
            and event.button() == Qt.MouseButton.MiddleButton
            and isinstance(watched, QWidget)
            and watched.window()
            in (
                self,
                self.parent().window()
                if isinstance(self.parent(), QWidget)
                else None,
            )
        ):
            self._middle_click_origin = None
            self._middle_click_moved = False
            self._middle_click_chord = False
            self._submit_and_accept()
            event.accept()
            return True
        return super().eventFilter(watched, event)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        position_dialog_top_right_after_show(self)

    def done(self, result: int) -> None:
        application = QApplication.instance()
        if application is not None:
            application.removeEventFilter(self)
        super().done(result)

    def _append_reference_row(self, reference: dict[str, Any]) -> None:
        row = self.reference_list.rowCount()
        self.reference_list.insertRow(row)
        remove_button = QPushButton("×")
        remove_button.setToolTip(
            tr("dialog.point_constraints.delete_reference")
        )
        remove_button.setFixedSize(28, 26)
        remove_button.setStyleSheet(
            "QPushButton { color: #ffffff; background: #8b2424;"
            " border: 1px solid #b94a4a; border-radius: 4px;"
            " font-size: 18px; font-weight: 700; padding: 0; }"
            "QPushButton:hover { background: #b83232;"
            " border-color: #ed7777; }"
            "QPushButton:pressed { background: #6f1d1d; }"
        )
        remove_button.clicked.connect(
            lambda _checked=False, descriptor=reference:
                self._remove_reference_descriptor(descriptor)
        )
        self.reference_list.setCellWidget(row, 0, remove_button)
        label = reference.get("label", reference.get("key", ""))
        self.reference_list.setItem(
            row,
            1,
            QTableWidgetItem(f"{row + 1}. {label}"),
        )
        offset = QDoubleSpinBox()
        offset.setRange(-1_000_000_000.0, 1_000_000_000.0)
        offset.setDecimals(self.decimal_places)
        offset.setSuffix(" mm")
        offset.setValue(float(reference.get("offset", 0.0)))
        offset.setEnabled(self._reference_supports_offset(reference))
        offset.valueChanged.connect(
            lambda value, descriptor=reference:
                self._set_reference_offset(descriptor, value)
        )
        self.reference_list.setCellWidget(row, 2, offset)
        orientation = QComboBox()
        for role in self._reference_orientation_options(reference):
            orientation.addItem(
                tr(f"dialog.point_constraints.orientation.{role}"),
                role,
            )
        role = str(reference.get("orientation_role", "none"))
        role_index = orientation.findData(role)
        orientation.setCurrentIndex(max(0, role_index))
        orientation.currentIndexChanged.connect(
            lambda _index, descriptor=reference, combo=orientation:
                self._set_reference_orientation_role(
                    descriptor,
                    str(combo.currentData()),
                )
        )
        self.reference_list.setCellWidget(row, 3, orientation)

    def _reference_orientation_options(
        self,
        reference: dict[str, Any],
    ) -> tuple[str, ...]:
        reference_type = reference.get("type")
        reference_kind = (
            self.reference_kind_callback(
                str(reference.get("entity_id", ""))
            )
            if reference_type == "entity"
            else None
        )
        if reference_type == "face" or reference_kind == EntityKind.PLANE:
            return (
                "none",
                "normal",
                "opposite_normal",
                "up",
                "down",
                "right",
                "left",
            )
        if reference_type == "edge" or reference_kind == EntityKind.AXIS:
            return ("none", "up", "down", "right", "left")
        return ("none",)

    def _normalize_reference_orientation_roles(self) -> None:
        has_explicit_roles = any(
            "orientation_role" in reference
            for reference in self.references
        )
        normal_reference = next(
            (
                reference
                for reference in self.references
                if reference.get("orientation_role")
                in ("normal", "opposite_normal")
            ),
            None,
        )
        if normal_reference is None and not has_explicit_roles:
            normal_reference = next(
                (
                    reference
                    for reference in self.references
                    if reference.get("plane_role") == "orientation"
                ),
                None,
            )
        if normal_reference is None:
            normal_reference = next(
                (
                    reference
                    for reference in self.references
                    if reference.get("type") == "face"
                    or (
                        reference.get("type") == "entity"
                        and self.reference_kind_callback(
                            str(reference.get("entity_id", ""))
                        )
                        == EntityKind.PLANE
                    )
                ),
                None,
            )
        direction_seen = False
        for reference in self.references:
            role = str(reference.get("orientation_role", "none"))
            if reference is normal_reference:
                role = (
                    role
                    if role in ("normal", "opposite_normal")
                    else "normal"
                )
            elif role in ("normal", "opposite_normal"):
                role = "none"
            if role in ("normal", "opposite_normal"):
                reference["plane_role"] = "orientation"
            else:
                reference.pop("plane_role", None)
            if role in ("up", "down", "right", "left"):
                if direction_seen:
                    role = "none"
                else:
                    direction_seen = True
            reference["orientation_role"] = role

    def _set_reference_orientation_role(
        self,
        reference: dict[str, Any],
        role: str,
    ) -> None:
        normal_roles = {"normal", "opposite_normal"}
        direction_roles = {"up", "down", "right", "left"}
        if role in normal_roles:
            for existing in self.references:
                if existing is not reference and str(
                    existing.get("orientation_role", "none")
                ) in normal_roles:
                    existing["orientation_role"] = "none"
                    existing.pop("plane_role", None)
        elif role in direction_roles:
            for existing in self.references:
                if existing is not reference and str(
                    existing.get("orientation_role", "none")
                ) in direction_roles:
                    existing["orientation_role"] = "none"
        reference["orientation_role"] = role
        if role in normal_roles:
            reference["plane_role"] = "orientation"
        else:
            reference.pop("plane_role", None)
        self._refresh_reference_orientation_combos()
        self._update_solution()

    def _refresh_reference_orientation_combos(self) -> None:
        for row, reference in enumerate(self.references):
            combo = self.reference_list.cellWidget(row, 3)
            if not isinstance(combo, QComboBox):
                continue
            index = combo.findData(
                str(reference.get("orientation_role", "none"))
            )
            combo.blockSignals(True)
            combo.setCurrentIndex(max(0, index))
            combo.blockSignals(False)

    def _reference_supports_offset(
        self,
        reference: dict[str, Any],
    ) -> bool:
        if reference.get("type") == "face":
            return True
        if reference.get("type") != "entity":
            return False
        return self.reference_kind_callback(
            str(reference.get("entity_id", ""))
        ) == EntityKind.PLANE

    def _set_reference_offset(
        self,
        reference: dict[str, Any],
        value: float,
    ) -> None:
        reference["offset"] = value
        self._update_solution()

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr(
                "dialog.container_properties.title",
                name=self.name_edit.text().strip(),
            )
        )

    def adopt_created_entity(
        self,
        point_object: ZimaEntity,
        point_entity: ZimaEntity,
    ) -> None:
        """Continue a create dialog as an editor after its first Apply."""
        self.point_object = point_object
        self.point_entity = point_entity
        self.edit_mode = True
        self.entityAdopted.emit()

    def adopt_created_point(
        self,
        point_object: ZimaEntity,
        point_entity: ZimaEntity,
    ) -> None:
        self.adopt_created_entity(point_object, point_entity)

    @staticmethod
    def _stored_references(
        point_entity: ZimaEntity | None,
    ) -> list[dict[str, Any]]:
        if point_entity is None:
            return []
        try:
            references = json.loads(
                str(point_entity.parameters.get("constraint_refs", "[]"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            return []
        return references if isinstance(references, list) else []

    @staticmethod
    def _stored_fallback(
        point_object: ZimaEntity | None,
        point_entity: ZimaEntity | None,
    ) -> tuple[float, float, float]:
        if point_object is None:
            return (0.0, 0.0, 0.0)
        values = point_object.coordinate_system.origin
        if point_entity is None:
            return values
        try:
            return tuple(
                float(point_entity.parameters.get(f"fallback_{axis}", values[index]))
                for index, axis in enumerate(("x", "y", "z"))
            )
        except (TypeError, ValueError):
            return values

    def add_reference(self, reference: ZimaEntity) -> None:
        if reference.entity_id in {
            self.point_object.entity_id if self.point_object is not None else "",
            self.point_entity.entity_id if self.point_entity is not None else "",
        }:
            return
        if reference.kind == EntityKind.ORIGIN:
            for child in reference.children:
                if child.kind == EntityKind.PLANE:
                    self.add_reference(child)
            return
        if reference.kind not in (
            EntityKind.POINT,
            EntityKind.AXIS,
            EntityKind.PLANE,
        ):
            return
        self._add_reference(
            {
                "type": "entity",
                "key": f"entity:{reference.entity_id}",
                "entity_id": reference.entity_id,
                "label": reference.name,
            }
        )

    def add_shape_reference(
        self,
        entity_id: str,
        label: str,
        shape_type: str,
        equations: list[list[float]],
        topology_key: str,
        metadata: dict[str, Any] | None = None,
    ) -> None:
        descriptor = {
            "type": shape_type,
            "key": f"{shape_type}:{entity_id}:{topology_key}",
            "entity_id": entity_id,
            "label": label,
            "equations": equations,
            "topology_key": topology_key,
        }
        if metadata is not None:
            descriptor.update(metadata)
        self._add_reference(descriptor)

    def _add_reference(self, reference: dict[str, Any]) -> None:
        if str(reference.get("key", "")) in self._references_being_removed:
            return
        if any(
            existing["key"] == reference["key"]
            for existing in self.references
        ):
            return
        is_surface_reference = (
            reference.get("type") == "face"
            or (
                reference.get("type") == "entity"
                and self.reference_kind_callback(
                    str(reference.get("entity_id", ""))
                )
                == EntityKind.PLANE
            )
        )
        is_first_surface_reference = (
            is_surface_reference
            and not any(
                existing.get("type") == "face"
                or (
                    existing.get("type") == "entity"
                    and self.reference_kind_callback(
                        str(existing.get("entity_id", ""))
                    )
                    == EntityKind.PLANE
                )
                for existing in self.references
            )
        )
        is_orientation_candidate = (
            is_surface_reference
            or reference.get("type") == "edge"
            or (
                reference.get("type") == "entity"
                and self.reference_kind_callback(
                    str(reference.get("entity_id", ""))
                )
                == EntityKind.AXIS
            )
        )
        if is_first_surface_reference:
            reference["plane_role"] = "orientation"
            reference["orientation_role"] = "normal"
        fallback = tuple(edit.value() for edit in self.coordinate_edits)
        trial_solution, trial_dof, _status, _constrained = (
            self.solve_callback(
                [*self.references, reference],
                fallback,
            )
        )
        current_dof = getattr(self, "dof", 3)
        if (
            current_dof == 0
            or trial_solution is None
            or trial_dof >= current_dof
        ):
            if is_orientation_candidate:
                reference["position_role"] = "orientation_only"
                reference.setdefault("orientation_role", "none")
                self.references.append(reference)
                self.highlighted_reference_keys.add(
                    str(reference.get("key", ""))
                )
                self._append_reference_row(reference)
                self._refresh_reference_item_warnings()
                self._update_solution()
                return
            reference.pop("plane_role", None)
            reference["orientation_role"] = "none"
            self.reference_status_label.setStyleSheet(
                "color: #ed7777; font-weight: 700;"
            )
            self.reference_status_label.setText(
                tr("dialog.point_constraints.rejected_reference")
            )
            return
        self.references.append(reference)
        self.highlighted_reference_keys.add(str(reference.get("key", "")))
        self._append_reference_row(reference)
        self._refresh_reference_item_warnings()
        self._update_solution()

    def _reference_cell_clicked(self, row: int, column: int) -> None:
        if column != 1 or not 0 <= row < len(self.references):
            return
        key = str(self.references[row].get("key", ""))
        if key in self.highlighted_reference_keys:
            self.highlighted_reference_keys.remove(key)
        else:
            self.highlighted_reference_keys.add(key)
        self._refresh_reference_item_warnings()
        self.reference_list.clearSelection()
        self.definitionChanged.emit()

    def _remove_reference_descriptor(
        self,
        descriptor: dict[str, Any],
    ) -> None:
        try:
            row = self.references.index(descriptor)
        except ValueError:
            return
        self._remove_reference_at(row)

    def _remove_reference_at(self, row: int) -> None:
        if not 0 <= row < len(self.references):
            return
        removed_key = str(self.references[row].get("key", ""))
        self._references_being_removed.add(removed_key)
        self.highlighted_reference_keys.discard(removed_key)
        self.references.pop(row)
        self.reference_list.removeRow(row)
        self._normalize_reference_orientation_roles()
        self._refresh_reference_orientation_combos()
        for index, reference in enumerate(self.references):
            item = self.reference_list.item(index, 1)
            if item is not None:
                item.setText(f"{index + 1}. {reference['label']}")
        if not self.references:
            self.reference_list.clearSelection()
            self.reference_list.setCurrentCell(-1, -1)
        try:
            self._refresh_reference_item_warnings()
            self._update_solution()
        finally:
            self._references_being_removed.discard(removed_key)

    def _refresh_reference_item_warnings(self) -> None:
        for index, reference in enumerate(self.references):
            item = self.reference_list.item(index, 1)
            if item is None:
                continue
            entity_id = str(reference.get("entity_id", "")).strip()
            missing = bool(entity_id) and not self.reference_exists_callback(
                entity_id
            )
            if missing:
                label = str(
                    reference.get("label", reference.get("key", entity_id))
                )
                item.setBackground(QBrush(QColor("#8b2424")))
                item.setForeground(QBrush(QColor("#ffffff")))
                item.setToolTip(
                    tr(
                        "dialog.point_constraints.missing_reference",
                        name=label,
                    )
                )
            else:
                highlighted = (
                    str(reference.get("key", ""))
                    in self.highlighted_reference_keys
                )
                item.setBackground(
                    QBrush(QColor("#00d1ff")) if highlighted else QBrush()
                )
                item.setForeground(
                    QBrush(QColor("#102027")) if highlighted else QBrush()
                )
                item.setToolTip("")

    def _activate_reference(self, row: int) -> None:
        if 0 <= row < len(self.references):
            self.referenceActivated.emit(self.references[row])

    def clear_active_reference(self) -> None:
        self.reference_list.clearSelection()
        self.reference_list.setCurrentCell(-1, -1)

    def _update_solution(self, _value: float | None = None) -> None:
        fallback = tuple(edit.value() for edit in self.coordinate_edits)
        solution, dof, status, constrained = self.solve_callback(
            self.references,
            fallback,
        )
        self.dof = dof
        self.solution = solution
        for index, edit in enumerate(self.coordinate_edits):
            edit.setEnabled(not constrained[index])
            edit.setStyleSheet(
                "QDoubleSpinBox:disabled { background: #303030; color: #dddddd; }"
            )
        self.dof_label.setText(
            tr("dialog.point_constraints.dof", count=dof)
        )
        self.reference_status_label.setText(
            tr("dialog.point_constraints.fully_constrained")
            if dof == 0
            else ""
        )
        self.reference_status_label.setStyleSheet(
            "color: #80AA1A; font-weight: 700;"
        )
        if solution is None:
            self.result_label.setText(tr(status))
        else:
            for index, edit in enumerate(self.coordinate_edits):
                if constrained[index]:
                    edit.blockSignals(True)
                    edit.setValue(solution[index])
                    edit.blockSignals(False)
            self.result_label.setText(
                tr(
                    "dialog.point_constraints.result",
                    x=f"{solution[0]:.{self.decimal_places}f}",
                    y=f"{solution[1]:.{self.decimal_places}f}",
                    z=f"{solution[2]:.{self.decimal_places}f}",
                )
            )
        self.ok_button.setEnabled(solution is not None)
        self.definitionChanged.emit()

    def _show_auxiliary_geometry(self) -> bool:
        return (
            self.point_object.show_auxiliary_geometry
            if self.point_object is not None
            else False
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if self.solution is None or not name:
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
        )
        if self.edit_mode:
            self.updateRequested.emit(*arguments)
        else:
            self.createRequested.emit(*arguments)
        return True

    def _submit_and_accept(self) -> None:
        if not self._submit():
            return
        self.accept()

    def _apply(self) -> None:
        if self._submit():
            self.applied.emit()


class AxisConstraintDialog(PointConstraintDialog):
    createAxisRequested = Signal(
        list, tuple, str, bool, bool, tuple, str, float
    )
    updateAxisRequested = Signal(
        list, tuple, str, bool, bool, tuple, str, float
    )

    def __init__(
        self,
        solve_callback,
        parent=None,
        *,
        axis_object: ZimaEntity | None = None,
        axis_entity: ZimaEntity | None = None,
        suggested_name: str = "",
        reference_exists_callback: Callable[[str], bool] | None = None,
        reference_kind_callback: Callable[[str], EntityKind | None] | None = None,
    ) -> None:
        super().__init__(
            solve_callback,
            parent,
            point_object=axis_object,
            point_entity=axis_entity,
            suggested_name=suggested_name,
            reference_exists_callback=reference_exists_callback,
            reference_kind_callback=reference_kind_callback,
        )
        self._set_container_type(ContainerType.AXIS)
        axis_form = QFormLayout()
        self.rotation_edits: list[QDoubleSpinBox] = []
        has_rotation_offsets = (
            axis_entity is not None
            and all(
                f"rotation_offset_{axis}" in axis_entity.parameters
                for axis in ("x", "y", "z")
            )
        )
        rotation = (
            tuple(
                float(
                    axis_entity.parameters[f"rotation_offset_{axis}"]
                )
                for axis in ("x", "y", "z")
            )
            if has_rotation_offsets
            else (
                axis_object.coordinate_system.rotation
                if axis_object is not None
                else (0.0, 0.0, 0.0)
            )
        )
        for label, value in zip(("RX", "RY", "RZ"), rotation):
            spinbox = QDoubleSpinBox()
            spinbox.setRange(-360_000.0, 360_000.0)
            spinbox.setDecimals(self.decimal_places)
            spinbox.setSingleStep(5.0)
            spinbox.setSuffix(" deg")
            spinbox.setValue(float(value))
            spinbox.valueChanged.connect(
                lambda _value: self.definitionChanged.emit()
            )
            axis_form.addRow(label, spinbox)
            self.rotation_edits.append(spinbox)
        self.direction_combo = QComboBox()
        for direction in ("X", "Y", "Z"):
            self.direction_combo.addItem(direction, direction.lower())
        self.direction_combo.setCurrentIndex(
            max(
                0,
                self.direction_combo.findData(
                    axis_entity.parameters.get("axis", "z")
                    if axis_entity is not None
                    else "z"
                ),
            )
        )
        self.direction_combo.currentIndexChanged.connect(
            lambda _index: self.definitionChanged.emit()
        )
        self.length_spin = QDoubleSpinBox()
        self.length_spin.setRange(0.001, 1_000_000.0)
        self.length_spin.setDecimals(self.decimal_places)
        self.length_spin.setSuffix(" mm")
        self.length_spin.setValue(
            float(
                axis_entity.parameters.get("length", 50.0)
                if axis_entity is not None
                else 50.0
            )
        )
        self.length_spin.valueChanged.connect(
            lambda _value: self.definitionChanged.emit()
        )
        axis_form.addRow(
            tr("dialog.axis.display"),
            QLabel(tr("dialog.axis.centerline")),
        )
        axis_form.addRow(tr("dialog.axis.direction"), self.direction_combo)
        axis_form.addRow(tr("dialog.axis.length"), self.length_spin)
        self.direction_label = axis_form.labelForField(self.direction_combo)
        self.length_label = axis_form.labelForField(self.length_spin)
        dialog_layout = self.layout()
        if isinstance(dialog_layout, QVBoxLayout):
            dialog_layout.insertLayout(dialog_layout.count() - 1, axis_form)
        available_height = self.screen().availableGeometry().height()
        self.resize(500, min(720, max(400, available_height - 48)))
        self._update_window_title()

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr(
                "dialog.container_properties.title",
                name=self.name_edit.text().strip(),
            )
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if self.solution is None or not name:
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
            tuple(edit.value() for edit in self.rotation_edits),
            str(self.direction_combo.currentData()),
            self.length_spin.value(),
        )
        if self.edit_mode:
            self.updateAxisRequested.emit(*arguments)
        else:
            self.createAxisRequested.emit(*arguments)
        return True


class PlaneConstraintDialog(AxisConstraintDialog):
    createPlaneRequested = Signal(
        list, tuple, str, bool, bool, tuple, str, float
    )
    updatePlaneRequested = Signal(
        list, tuple, str, bool, bool, tuple, str, float
    )

    def __init__(
        self,
        solve_callback,
        parent=None,
        *,
        plane_object: ZimaEntity | None = None,
        plane_entity: ZimaEntity | None = None,
        suggested_name: str = "",
        reference_exists_callback=None,
        reference_kind_callback=None,
    ) -> None:
        super().__init__(
            solve_callback,
            parent,
            axis_object=plane_object,
            axis_entity=plane_entity,
            suggested_name=suggested_name,
            reference_exists_callback=reference_exists_callback,
            reference_kind_callback=reference_kind_callback,
        )
        self._set_container_type(ContainerType.PLANE)
        self.direction_combo.blockSignals(True)
        self.direction_combo.clear()
        for label, value in (("XY", "xy"), ("YZ", "yz"), ("XZ", "xz")):
            self.direction_combo.addItem(label, value)
        self.direction_combo.setCurrentIndex(
            max(
                0,
                self.direction_combo.findData(
                    plane_entity.parameters.get("plane", "xy")
                    if plane_entity is not None
                    else "xy"
                ),
            )
        )
        self.direction_combo.blockSignals(False)
        if plane_entity is not None:
            for index, axis in enumerate(("x", "y", "z")):
                self.rotation_edits[index].setValue(
                    float(plane_entity.parameters.get(f"rotation_offset_{axis}", 0.0))
                )
        if self.direction_label is not None:
            self.direction_label.setVisible(False)
        self.direction_combo.setVisible(False)
        if self.length_label is not None:
            self.length_label.setText(tr("dialog.plane.size"))
        self.length_spin.setValue(
            float(
                plane_entity.parameters.get("size", 50.0)
                if plane_entity is not None
                else 50.0
            )
        )
        if type(self) is PlaneConstraintDialog:
            self._normalize_orientation_reference()
        self._update_window_title()

    def _is_orientation_reference(
        self,
        reference: dict[str, Any],
    ) -> bool:
        if reference.get("type") == "face":
            return True
        return (
            reference.get("type") == "entity"
            and self.reference_kind_callback(
                str(reference.get("entity_id", ""))
            )
            == EntityKind.PLANE
        )

    def _normalize_orientation_reference(self) -> None:
        self._normalize_reference_orientation_roles()
        if hasattr(self, "reference_list"):
            self._refresh_reference_orientation_combos()

    def _add_reference(self, reference: dict[str, Any]) -> None:
        if type(self) is not PlaneConstraintDialog:
            super()._add_reference(reference)
            return
        is_first_orientation = (
            self._is_orientation_reference(reference)
            and not any(
                existing.get("plane_role") == "orientation"
                for existing in self.references
            )
        )
        if is_first_orientation:
            reference["plane_role"] = "orientation"
            reference["orientation_role"] = "normal"
        previous_count = len(self.references)
        super()._add_reference(reference)
        if len(self.references) != previous_count:
            return
        if not is_first_orientation:
            reference.pop("plane_role", None)
            if reference.get("orientation_role") in (
                "normal",
                "opposite_normal",
            ):
                reference["orientation_role"] = "none"
            return

        # A plane's first surface reference also carries orientation.  If its
        # origin is already fully located (for example by a point), retain the
        # surface as an orientation-only reference instead of rejecting it as
        # a redundant or conflicting positional equation.
        reference["position_role"] = "orientation_only"
        self.references.append(reference)
        self.highlighted_reference_keys.add(str(reference.get("key", "")))
        self._append_reference_row(reference)
        self._refresh_reference_item_warnings()
        self._update_solution()

    def _remove_reference_at(self, row: int) -> None:
        if type(self) is not PlaneConstraintDialog:
            super()._remove_reference_at(row)
            return
        removed_orientation = (
            0 <= row < len(self.references)
            and self.references[row].get("plane_role") == "orientation"
        )
        super()._remove_reference_at(row)
        if removed_orientation:
            self._normalize_orientation_reference()
            self.definitionChanged.emit()

    def add_reference(self, reference: ZimaEntity) -> None:
        if reference.kind == EntityKind.ORIGIN:
            return
        super().add_reference(reference)
        if reference.kind != EntityKind.PLANE:
            return
        plane = str(reference.parameters.get("plane", ""))
        index = self.direction_combo.findData(plane)
        if index >= 0:
            self.direction_combo.setCurrentIndex(index)

    def has_orientation_reference(self) -> bool:
        return any(
            reference.get("type") == "face"
            or (
                reference.get("type") == "entity"
                and self.reference_kind_callback(
                    str(reference.get("entity_id", ""))
                )
                == EntityKind.PLANE
            )
            for reference in self.references
        )

    def _update_solution(self, _value: float | None = None) -> None:
        super()._update_solution(_value)
        self.ok_button.setEnabled(self.solution is not None)

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr(
                "dialog.container_properties.title",
                name=self.name_edit.text().strip(),
            )
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if (
            self.solution is None
            or not name
        ):
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
            tuple(edit.value() for edit in self.rotation_edits),
            str(self.direction_combo.currentData()),
            self.length_spin.value(),
        )
        if self.edit_mode:
            self.updatePlaneRequested.emit(*arguments)
        else:
            self.createPlaneRequested.emit(*arguments)
        return True


class SolidConstraintDialog(AxisConstraintDialog):
    createSolidRequested = Signal(
        list, tuple, str, bool, bool, tuple, dict, str
    )
    updateSolidRequested = Signal(
        list, tuple, str, bool, bool, tuple, dict, str
    )

    def __init__(
        self,
        solve_callback,
        solid_kind: EntityKind,
        parent=None,
        *,
        solid_object: ZimaEntity | None = None,
        solid_entity: ZimaEntity | None = None,
        suggested_name: str = "",
        reference_exists_callback=None,
        reference_kind_callback=None,
    ) -> None:
        self.solid_kind = solid_kind
        super().__init__(
            solve_callback,
            parent,
            axis_object=solid_object,
            axis_entity=solid_entity,
            suggested_name=suggested_name,
            reference_exists_callback=reference_exists_callback,
            reference_kind_callback=reference_kind_callback,
        )
        self._set_container_type(
            ContainerType(solid_kind.value.upper())
        )
        operation_form = QFormLayout()
        operation_widget = QWidget()
        operation_layout = QHBoxLayout(operation_widget)
        operation_layout.setContentsMargins(0, 0, 0, 0)
        operation_layout.setSpacing(8)
        self.add_operation_button = QPushButton(
            tr("dialog.operation.add")
        )
        self.subtract_operation_button = QPushButton(
            tr("dialog.operation.subtract")
        )
        for button in (
            self.add_operation_button,
            self.subtract_operation_button,
        ):
            button.setCheckable(True)
            button.setMinimumHeight(40)
            button.setSizePolicy(
                QSizePolicy.Policy.Expanding,
                QSizePolicy.Policy.Fixed,
            )
        self.add_operation_button.setStyleSheet(
            "QPushButton { border: 2px solid #54703a; border-radius: 6px;"
            " font-weight: 700; padding: 7px 14px; }"
            "QPushButton:checked { background: #80AA1A; color: #101510;"
            " border-color: #a7d52b; }"
        )
        self.subtract_operation_button.setStyleSheet(
            "QPushButton { border: 2px solid #713d3d; border-radius: 6px;"
            " font-weight: 700; padding: 7px 14px; }"
            "QPushButton:checked { background: #c64b4b; color: #ffffff;"
            " border-color: #ed7777; }"
        )
        operation_layout.addWidget(self.add_operation_button)
        operation_layout.addWidget(self.subtract_operation_button)
        operation_form.addRow(
            tr("dialog.properties.operation"),
            operation_widget,
        )
        current_operation = (
            solid_entity.combine_mode
            if solid_entity is not None
            else CombineMode.ADD
        )
        self._set_operation(current_operation, emit_change=False)
        self.add_operation_button.clicked.connect(
            lambda _checked: self._set_operation(CombineMode.ADD)
        )
        self.subtract_operation_button.clicked.connect(
            lambda _checked: self._set_operation(CombineMode.SUBTRACT)
        )
        dialog_layout = self.layout()
        if isinstance(dialog_layout, QVBoxLayout):
            dialog_layout.insertLayout(1, operation_form)
        self.direction_combo.setVisible(False)
        self.length_spin.setVisible(False)
        if self.direction_label is not None:
            self.direction_label.setVisible(False)
        if self.length_label is not None:
            self.length_label.setVisible(False)
        self.parameter_edits: dict[str, QDoubleSpinBox] = {}
        parameter_form = QFormLayout()
        for key, label_key, minimum in PrimitivePropertiesDialog.PARAMETER_DEFINITIONS[
            solid_kind
        ]:
            edit = QDoubleSpinBox()
            edit.setRange(minimum, 1_000_000.0)
            edit.setDecimals(self.decimal_places)
            edit.setSingleStep(1.0)
            edit.setSuffix(" mm")
            defaults = {
                EntityKind.BOX: {
                    "length": 40.0, "width": 30.0, "height": 20.0,
                },
                EntityKind.SPHERE: {"diameter": 30.0},
                EntityKind.CYLINDER: {"diameter": 30.0, "height": 50.0},
                EntityKind.CONE: {
                    "bottom_diameter": 40.0,
                    "top_diameter": 0.0,
                    "height": 50.0,
                },
                EntityKind.PYRAMID: {
                    "length": 40.0, "width": 40.0, "height": 50.0,
                },
                EntityKind.WEDGE: {
                    "length": 60.0,
                    "width": 40.0,
                    "height": 40.0,
                    "top_offset": 30.0,
                },
            }
            default = defaults[solid_kind].get(key, minimum)
            edit.setValue(
                float(solid_entity.parameters.get(key, default))
                if solid_entity is not None
                else default
            )
            edit.valueChanged.connect(
                lambda _value: self.definitionChanged.emit()
            )
            self.parameter_edits[key] = edit
            parameter_form.addRow(tr(label_key), edit)
        layout = self.layout()
        if isinstance(layout, QVBoxLayout):
            layout.insertLayout(layout.count() - 1, parameter_form)
        self._update_window_title()

    def _set_operation(
        self,
        operation: CombineMode,
        *,
        emit_change: bool = True,
    ) -> None:
        self.add_operation_button.setChecked(operation == CombineMode.ADD)
        self.subtract_operation_button.setChecked(
            operation == CombineMode.SUBTRACT
        )
        if emit_change:
            self.definitionChanged.emit()

    def operation(self) -> CombineMode:
        return (
            CombineMode.SUBTRACT
            if self.subtract_operation_button.isChecked()
            else CombineMode.ADD
        )

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr(
                "dialog.container_properties.title",
                name=self.name_edit.text().strip(),
            )
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if self.solution is None or not name:
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
            tuple(edit.value() for edit in self.rotation_edits),
            {
                key: edit.value()
                for key, edit in self.parameter_edits.items()
            },
            self.operation().value,
        )
        if self.edit_mode:
            self.updateSolidRequested.emit(*arguments)
        else:
            self.createSolidRequested.emit(*arguments)
        return True


class ContainerPropertiesDialog(AxisConstraintDialog):
    createContainerRequested = Signal(
        list, tuple, str, bool, bool, tuple, str
    )
    updateContainerRequested = Signal(
        list, tuple, str, bool, bool, tuple, str
    )

    def __init__(
        self,
        solve_callback,
        parent=None,
        *,
        edited_object=None,
        suggested_name="",
        reference_exists_callback=None,
        reference_kind_callback=None,
    ) -> None:
        super().__init__(
            solve_callback,
            parent,
            axis_object=edited_object,
            axis_entity=edited_object,
            suggested_name=suggested_name,
            reference_exists_callback=reference_exists_callback,
            reference_kind_callback=reference_kind_callback,
        )
        self.direction_combo.setVisible(False)
        self.length_spin.setVisible(False)
        if self.direction_label is not None:
            self.direction_label.setVisible(False)
        if self.length_label is not None:
            self.length_label.setVisible(False)
        current_type = (
            edited_object.container_type
            if edited_object is not None
            else ContainerType.EMPTY
        )
        self._set_container_type(
            current_type,
            editable=not self.edit_mode,
        )
        self._update_window_title()

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr("dialog.container_properties.title", name=self.name_edit.text().strip())
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if self.solution is None or not name:
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
            tuple(edit.value() for edit in self.rotation_edits),
            str(self.container_type_combo.currentData()),
        )
        if self.edit_mode:
            self.updateContainerRequested.emit(*arguments)
        else:
            self.createContainerRequested.emit(*arguments)
        return True


class SketchConstraintDialog(PlaneConstraintDialog):
    createSketchRequested = Signal(
        list, tuple, str, bool, bool, tuple, float
    )
    updateSketchRequested = Signal(
        list, tuple, str, bool, bool, tuple, float
    )
    enterSketchRequested = Signal()

    def __init__(
        self,
        solve_callback,
        parent=None,
        *,
        sketch_object=None,
        sketch_entity=None,
        suggested_name="",
        reference_exists_callback=None,
        reference_kind_callback=None,
    ) -> None:
        super().__init__(
            solve_callback,
            parent,
            plane_object=sketch_object,
            plane_entity=sketch_entity,
            suggested_name=suggested_name,
            reference_exists_callback=reference_exists_callback,
            reference_kind_callback=reference_kind_callback,
        )
        self._set_container_type(ContainerType.SKETCH)
        self.length_spin.setVisible(False)
        if self.length_label is not None:
            self.length_label.setVisible(False)
        self.diameter_spin = QDoubleSpinBox()
        self.diameter_spin.setRange(0.001, 1_000_000.0)
        self.diameter_spin.setDecimals(self.decimal_places)
        self.diameter_spin.setSuffix(" mm")
        self.diameter_spin.setValue(
            float(sketch_entity.parameters.get("diameter", 10.0))
            if sketch_entity is not None
            else 10.0
        )
        self.diameter_spin.valueChanged.connect(
            lambda _value: self.definitionChanged.emit()
        )
        self.diameter_spin.setVisible(False)
        buttons = self.findChild(QDialogButtonBox)
        dialog_layout = self.layout()
        if buttons is not None and isinstance(dialog_layout, QVBoxLayout):
            self.sketch_button = QPushButton("SKETCH")
            self.sketch_button.setStyleSheet(
                "QPushButton { background: #4DD811; color: #102027;"
                " font-weight: 700; padding: 9px 18px;"
                " border-radius: 4px; }"
                "QPushButton:hover { background: #65ec2c; }"
            )
            self.sketch_button.setMinimumHeight(40)
            dialog_layout.insertWidget(
                dialog_layout.indexOf(buttons),
                self.sketch_button,
            )
            self.sketch_button.clicked.connect(self._submit_and_enter_sketch)
            self._update_solution()
        self._update_window_title()

    def _update_solution(self, _value: float | None = None) -> None:
        super()._update_solution(_value)
        button = getattr(self, "sketch_button", None)
        if button is not None:
            button.setEnabled(
                self.solution is not None
                and self.has_orientation_reference()
            )

    def _submit_and_enter_sketch(self) -> None:
        if not self._submit():
            return
        self.enterSketchRequested.emit()
        self.accept()

    def _update_window_title(self, _name: str | None = None) -> None:
        self.setWindowTitle(
            tr("dialog.container_properties.title", name=self.name_edit.text().strip())
        )

    def _submit(self) -> bool:
        name = self.name_edit.text().strip()
        if (
            self.solution is None
            or not name
        ):
            return False
        arguments = (
            self.references,
            tuple(edit.value() for edit in self.coordinate_edits),
            name,
            True,
            self._show_auxiliary_geometry(),
            tuple(edit.value() for edit in self.rotation_edits),
            self.diameter_spin.value(),
        )
        if self.edit_mode:
            self.updateSketchRequested.emit(*arguments)
        else:
            self.createSketchRequested.emit(*arguments)
        return True


class PlaneAttachmentDialog(QDialog):
    def __init__(self, source_name: str, target_name: str, face_role: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.attachment.title"))
        layout = QFormLayout(self)
        layout.addRow(tr("dialog.attachment.source"), QLabel(source_name))
        layout.addRow(tr("dialog.attachment.target"), QLabel(target_name))
        layout.addRow(tr("dialog.attachment.face"), QLabel(face_role))
        self.primary_combo = QComboBox()
        self.secondary_combo = QComboBox()
        for axis in ("X", "Y", "Z"):
            self.primary_combo.addItem(axis, axis.lower())
            self.secondary_combo.addItem(axis, axis.lower())
        self.secondary_combo.setCurrentIndex(1)
        self.flip_checkbox = QCheckBox(tr("dialog.attachment.flip_normal"))
        layout.addRow(tr("dialog.attachment.primary_axis"), self.primary_combo)
        layout.addRow(tr("dialog.attachment.secondary_axis"), self.secondary_combo)
        layout.addRow(self.flip_checkbox)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def accept(self) -> None:
        if self.primary_axis == self.secondary_axis:
            QMessageBox.information(
                self,
                tr("dialog.attachment.title"),
                tr("message.attachment.axes_must_differ"),
            )
            return
        super().accept()

    @property
    def primary_axis(self) -> str:
        return str(self.primary_combo.currentData())

    @property
    def secondary_axis(self) -> str:
        return str(self.secondary_combo.currentData())


class UserParametersDialog(QDialog):
    KEY_COLUMN = 0
    SHARED_COLUMN = 1
    LABEL_COLUMN = 2
    VALUE_COLUMN = 3

    def __init__(
        self,
        document: PartDocument,
        language: str,
        parent=None,
        save_callback: Callable[[], bool] | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.parameters.title"))
        self.resize(920, 560)
        self.setMinimumSize(760, 420)
        self.setSizeGripEnabled(True)
        self.document = document
        self.language = language
        self.save_callback = save_callback
        self.order = list(document.user_parameter_order)
        self.labels = copy.deepcopy(document.user_parameter_labels)
        self.values = copy.deepcopy(document.user_parameter_values)

        layout = QVBoxLayout(self)

        language_form = QFormLayout()
        self.language_combo = QComboBox()
        self.language_combo.setEditable(True)
        for item in self._available_languages(language):
            self.language_combo.addItem(item)
        self.language_combo.setCurrentText(language)
        language_form.addRow(tr("label.language"), self.language_combo)
        layout.addLayout(language_form)

        self.table = QTableWidget(0, 4)
        self.table.setAlternatingRowColors(True)
        self.table.setHorizontalHeaderLabels(
            [tr("column.key"), tr("column.shared"), tr("column.label"), tr("column.value")]
        )
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(
            self.KEY_COLUMN, QHeaderView.ResizeMode.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.SHARED_COLUMN, QHeaderView.ResizeMode.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.LABEL_COLUMN, QHeaderView.ResizeMode.Stretch
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.VALUE_COLUMN, QHeaderView.ResizeMode.Stretch
        )
        layout.addWidget(self.table)

        actions_layout = QHBoxLayout()
        add_button = QPushButton(tr("button.add"))
        delete_button = QPushButton(tr("button.delete"))
        add_button.clicked.connect(self._add_row)
        delete_button.clicked.connect(self._delete_selected_rows)
        actions_layout.addWidget(add_button)
        actions_layout.addWidget(delete_button)
        actions_layout.addStretch(1)
        actions_layout.addWidget(QSizeGrip(self))
        layout.addLayout(actions_layout)

        self.saved_status = create_saved_status_label()
        layout.addWidget(self.saved_status)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self.apply_changes
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        self.language_combo.activated.connect(
            lambda _index: self._change_language(self.language_combo.currentText())
        )
        if self.language_combo.lineEdit() is not None:
            self.language_combo.lineEdit().editingFinished.connect(
                lambda: self._change_language(self.language_combo.currentText())
            )
        self._populate_table()

    def _available_languages(self, current_language: str) -> list[str]:
        languages = {"cs", "de", "en", "fr", current_language}
        for language_values in [*self.labels.values(), *self.values.values()]:
            languages.update(language for language in language_values if language)
        return sorted(language for language in languages if language)

    def _change_language(self, language: str) -> None:
        language = language.strip()
        if not language or language == self.language:
            return
        self._commit_pending_table_edit()
        if self._read_table():
            self.language = language
            self._populate_table()

    def _populate_table(self) -> None:
        self.table.setRowCount(0)
        for key in self.order:
            self._insert_row(key)

    def _insert_row(self, key: str) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)

        key_item = QTableWidgetItem(key)
        self.table.setItem(row, self.KEY_COLUMN, key_item)

        shared_checkbox = QCheckBox()
        shared_checkbox.setChecked("" in self.values.get(key, {}))
        shared_checkbox.setToolTip(tr("tooltip.shared_value"))
        shared_cell = QWidget()
        shared_layout = QHBoxLayout(shared_cell)
        shared_layout.setContentsMargins(0, 0, 0, 0)
        shared_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        shared_layout.addWidget(shared_checkbox)
        self.table.setCellWidget(row, self.SHARED_COLUMN, shared_cell)

        label = self.labels.get(key, {}).get(self.language, "")
        value_map = self.values.get(key, {})
        value = value_map.get("", "") if "" in value_map else value_map.get(self.language, "")
        self.table.setItem(row, self.LABEL_COLUMN, QTableWidgetItem(label))
        self.table.setItem(row, self.VALUE_COLUMN, QTableWidgetItem(value))

    def _add_row(self) -> None:
        if not self._read_table():
            return

        index = 1
        while f"param{index:03}" in self.order:
            index += 1
        key = f"param{index:03}"
        self.order.append(key)
        self.labels[key] = {self.language: key}
        self.values[key] = {"": ""}
        self._populate_table()

    def _delete_selected_rows(self) -> None:
        selected_rows = sorted(
            {index.row() for index in self.table.selectedIndexes()},
            reverse=True,
        )
        for row in selected_rows:
            key_item = self.table.item(row, self.KEY_COLUMN)
            if key_item is None:
                continue
            key = key_item.text().strip()
            if key in self.order:
                self.order.remove(key)
            self.labels.pop(key, None)
            self.values.pop(key, None)
            self.table.removeRow(row)

    def _read_table(self) -> bool:
        self._commit_pending_table_edit()
        new_order: list[str] = []
        new_labels = copy.deepcopy(self.labels)
        new_values = copy.deepcopy(self.values)
        seen = set()

        for row in range(self.table.rowCount()):
            key_item = self.table.item(row, self.KEY_COLUMN)
            key = key_item.text().strip() if key_item is not None else ""
            if not key:
                QMessageBox.information(
                    self, tr("dialog.parameters.title"), tr("message.required.parameter_key")
                )
                return False
            if key in seen:
                QMessageBox.information(
                    self,
                    tr("dialog.parameters.title"),
                    tr("message.duplicate_key", key=key),
                )
                return False

            seen.add(key)
            new_order.append(key)
            new_labels.setdefault(key, {})
            new_values.setdefault(key, {})

            label_item = self.table.item(row, self.LABEL_COLUMN)
            value_item = self.table.item(row, self.VALUE_COLUMN)
            label = label_item.text() if label_item is not None else ""
            value = value_item.text() if value_item is not None else ""

            if label:
                new_labels[key][self.language] = label
            else:
                new_labels[key].pop(self.language, None)

            is_shared = self._is_shared_checked(row)
            if is_shared:
                new_values[key][""] = value
                new_values[key].pop(self.language, None)
            else:
                new_values[key][self.language] = value
                new_values[key].pop("", None)

        self.order = new_order
        self.labels = {key: new_labels.get(key, {}) for key in new_order}
        self.values = {key: new_values.get(key, {"": ""}) for key in new_order}
        return True

    def _is_shared_checked(self, row: int) -> bool:
        cell_widget = self.table.cellWidget(row, self.SHARED_COLUMN)
        if cell_widget is None:
            return False
        checkbox = cell_widget.findChild(QCheckBox)
        return checkbox is not None and checkbox.isChecked()

    def _commit_pending_table_edit(self) -> None:
        self.table.clearFocus()
        QApplication.processEvents()

    def _apply_to_document(self) -> bool:
        if not self._read_table():
            return False

        self.document.user_parameter_order = self.order
        self.document.user_parameter_labels = self.labels
        self.document.user_parameter_values = self.values
        self.document.user_parameters = {
            key: values.get("", "")
            for key, values in self.values.items()
            if "" in values
        }
        return True

    def apply_changes(self) -> bool:
        self.saved_status.clear()
        if not self._apply_to_document():
            return False
        if self.save_callback is not None and not self.save_callback():
            return False
        self.saved_status.setText(tr("status.changes_saved"))
        return True

    def accept(self) -> None:
        if not self.apply_changes():
            return
        super().accept()


class FileSettingsDialog(QDialog):
    UNIT_CHOICES = {
        "Length": ("mm", "cm", "m", "in"),
        "Angle": ("deg", "rad"),
        "Mass": ("kg", "g", "t", "lb"),
        "Time": ("s", "min"),
        "Temperature": ("C", "K", "F"),
        "Stress": ("Pa", "kPa", "MPa", "GPa", "psi"),
    }

    def __init__(
        self,
        document: PartDocument,
        parent=None,
        save_callback: Callable[[], bool] | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.file_settings.title"))
        self.document = document
        self.save_callback = save_callback

        layout = QVBoxLayout(self)
        form = QFormLayout()
        self.unit_edits: dict[str, QComboBox] = {}
        for unit_name, choices in self.UNIT_CHOICES.items():
            combo = NoWheelComboBox()
            combo.setEditable(True)
            combo.addItems(choices)
            combo.setCurrentText(document.document_units.get(unit_name, choices[0]))
            self.unit_edits[unit_name] = combo
            form.addRow(tr(f"document.unit.{unit_name.lower()}"), combo)

        self.linear_tolerance = self._precision_spinbox(
            document.document_precision.get("linear_tolerance", "0.001"),
            decimals=9,
        )
        form.addRow(tr("document.precision.linear_tolerance"), self.linear_tolerance)

        self.angular_tolerance = self._precision_spinbox(
            document.document_precision.get("angular_tolerance", "0.001"),
            decimals=9,
        )
        form.addRow(tr("document.precision.angular_tolerance"), self.angular_tolerance)

        self.mesh_deflection = self._precision_spinbox(
            document.document_precision.get("mesh_deflection", "0.1"),
            decimals=9,
        )
        form.addRow(tr("document.precision.mesh_deflection"), self.mesh_deflection)

        self.decimal_places = QSpinBox()
        self.decimal_places.setRange(0, 12)
        self.decimal_places.setValue(
            int(document.document_precision.get("decimal_places", "3"))
        )
        form.addRow(tr("document.precision.decimal_places"), self.decimal_places)
        layout.addLayout(form)

        self.saved_status = create_saved_status_label()
        layout.addWidget(self.saved_status)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self.apply_changes
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _precision_spinbox(self, value: str, decimals: int) -> QDoubleSpinBox:
        spinbox = QDoubleSpinBox()
        spinbox.setDecimals(decimals)
        spinbox.setRange(0.0, 1_000_000.0)
        spinbox.setValue(float(value))
        return spinbox

    def _apply_to_document(self) -> None:
        self.document.document_units = {
            unit_name: combo.currentText().strip()
            for unit_name, combo in self.unit_edits.items()
        }
        self.document.document_precision = {
            "linear_tolerance": f"{self.linear_tolerance.value():.12g}",
            "angular_tolerance": f"{self.angular_tolerance.value():.12g}",
            "mesh_deflection": f"{self.mesh_deflection.value():.12g}",
            "decimal_places": str(self.decimal_places.value()),
        }

    def apply_changes(self) -> bool:
        self.saved_status.clear()
        self._apply_to_document()
        if self.save_callback is not None and not self.save_callback():
            return False
        self.saved_status.setText(tr("status.changes_saved"))
        return True

    def accept(self) -> None:
        if not self.apply_changes():
            return
        super().accept()


class OptionsDialog(QDialog):
    LANGUAGE_CHOICES = ("cs", "de", "en", "fr")
    UNIT_CHOICES = FileSettingsDialog.UNIT_CHOICES
    PATH_KEYS = ("Materials", "Templates", "Localization")

    def __init__(
        self,
        config_path: Path,
        language: str,
        parent=None,
        applied_callback: Callable[[], None] | None = None,
        settings: ApplicationSettings | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.options.title"))
        self.setMinimumWidth(620)
        self.config_path = config_path
        self.applied_callback = applied_callback
        self.settings = settings
        self.effective_paths = {
            path_name: (
                getattr(settings, f"{path_name.lower()}_path")
                if settings is not None
                else None
            )
            for path_name in self.PATH_KEYS
        }

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"{tr('label.options')}: {config_path}"))

        config = configparser.ConfigParser()
        config.optionxform = str
        config.read(config_path, encoding="utf-8-sig")

        form = QFormLayout()
        self.language_combo = NoWheelComboBox()
        self.language_combo.addItems(self.LANGUAGE_CHOICES)
        self.language_combo.setCurrentText(
            settings.language
            if settings is not None
            else config.get("Application", "Language", fallback=language)
        )
        form.addRow(tr("global.language"), self.language_combo)

        self.unit_combos: dict[str, QComboBox] = {}
        for unit_name, choices in self.UNIT_CHOICES.items():
            combo = NoWheelComboBox()
            combo.addItems(choices)
            configured = (
                settings.units.get(unit_name, choices[0])
                if settings is not None
                else config.get("Units", unit_name, fallback=choices[0])
            )
            combo.setCurrentText(configured if configured in choices else choices[0])
            self.unit_combos[unit_name] = combo
            form.addRow(tr(f"document.unit.{unit_name.lower()}"), combo)

        self.path_edits: dict[str, QLineEdit] = {}
        for path_name in self.PATH_KEYS:
            row_widget = QWidget()
            row_layout = QHBoxLayout(row_widget)
            row_layout.setContentsMargins(0, 0, 0, 0)
            is_local_layer = (
                settings is not None
                and settings.local_config_path is not None
                and config_path.resolve() == settings.local_config_path.resolve()
            )
            configured_path = config.get(
                "Paths",
                path_name,
                fallback="" if is_local_layer else path_name.lower(),
            )
            path_edit = QLineEdit(configured_path)
            if is_local_layer and not configured_path.strip():
                effective_path = getattr(settings, f"{path_name.lower()}_path")
                path_edit.setPlaceholderText(
                    tr("global.path.inherited", path=str(effective_path))
                )
            browse_button = QPushButton(tr("button.browse"))
            browse_button.clicked.connect(
                lambda _checked=False, key=path_name: self._browse_directory(key)
            )
            row_layout.addWidget(path_edit, 1)
            row_layout.addWidget(browse_button)
            self.path_edits[path_name] = path_edit
            form.addRow(tr(f"global.path.{path_name.lower()}"), row_widget)
        layout.addLayout(form)

        self.saved_status = create_saved_status_label()
        layout.addWidget(self.saved_status)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self.apply_changes
        )
        save_as_button = QPushButton(tr("button.save_as"))
        save_as_button.clicked.connect(self.save_as)
        buttons.addButton(save_as_button, QDialogButtonBox.ButtonRole.ActionRole)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _browse_directory(self, path_name: str) -> None:
        path_edit = self.path_edits[path_name]
        path_text = path_edit.text().strip()
        effective_path = self.effective_paths.get(path_name)
        if not path_text and effective_path is not None:
            configured_path = effective_path
        else:
            configured_path = Path(path_text)
            if not configured_path.is_absolute():
                configured_path = self.config_path.parent / configured_path
        directory = QFileDialog.getExistingDirectory(
            self,
            tr("file.select_directory"),
            str(configured_path),
        )
        if not directory:
            return
        selected_path = Path(directory)
        try:
            display_path = selected_path.relative_to(self.config_path.parent)
        except ValueError:
            display_path = selected_path
        path_edit.setText(str(display_path))

    def _configuration(self) -> configparser.ConfigParser:
        config = configparser.ConfigParser(interpolation=None)
        config.optionxform = str
        config["Application"] = {"Language": self.language_combo.currentText()}
        config["Paths"] = {
            path_name: portable_config_path(path_edit.text())
            for path_name, path_edit in self.path_edits.items()
        }
        config["Units"] = {
            unit_name: combo.currentText()
            for unit_name, combo in self.unit_combos.items()
        }
        return config

    def _write_configuration(self, target_path: Path) -> bool:
        try:
            buffer = io.StringIO()
            self._configuration().write(buffer)
            write_text_versioned(
                target_path,
                buffer.getvalue().rstrip() + "\n",
                validator=validate_ini_file,
            )
        except (OSError, configparser.Error) as exc:
            QMessageBox.critical(self, tr("message.save_failed"), str(exc))
            return False
        return True

    def apply_changes(self) -> bool:
        self.saved_status.clear()
        if not self._write_configuration(self.config_path):
            return False
        if self.applied_callback is not None:
            self.applied_callback()
        self.saved_status.setText(tr("status.changes_saved"))
        return True

    def save_as(self) -> bool:
        self.saved_status.clear()
        file_name, _ = QFileDialog.getSaveFileName(
            self,
            tr("file.save_config"),
            str(self.config_path.parent / "config.ini"),
            tr("file.filter.ini"),
        )
        if not file_name:
            return False
        target_path = Path(file_name)
        if target_path.suffix.lower() != ".ini":
            target_path = target_path.with_suffix(".ini")
        if not self._write_configuration(target_path):
            return False
        self.saved_status.setText(
            tr("status.config_saved_as", path=str(target_path))
        )
        return True

    def accept(self) -> None:
        if not self.apply_changes():
            return
        super().accept()


class MaterialDialog(QDialog):
    PROPERTY_COLUMN = 0
    VALUE_COLUMN = 1
    UNIT_COLUMN = 2
    DESCRIPTION_COLUMN = 3
    UNIT_CHOICES = (
        "",
        "1",
        "mm",
        "cm",
        "m",
        "in",
        "deg",
        "rad",
        "kg",
        "g",
        "t",
        "lb",
        "s",
        "min",
        "C",
        "K",
        "F",
        "Pa",
        "kPa",
        "MPa",
        "GPa",
        "psi",
        "kg/mm^3",
        "g/cm^3",
        "kg/m^3",
        "lb/in^3",
        "1/C",
        "1/K",
        "1/F",
        "mm*kg/(s^3*C)",
        "W/(m*K)",
        "mm^2/(s^2*C)",
        "J/(kg*K)",
    )
    TEXT_PROPERTIES = {
        "MATERIAL_NAME",
        "HARDNESS",
        "CONDITION",
    }
    DIMENSIONLESS_PROPERTIES = {
        "POISSON_RATIO",
        "STRUCTURAL_DAMPING_COEFFICIENT",
        "EMISSIVITY",
        "SHEETMETAL_K_FACTOR",
    }
    PROPERTY_UNIT_CHOICES = {
        "YOUNG_MODULUS": ("MPa", "GPa", "kPa", "Pa", "psi"),
        "SHEAR_MODULUS": ("MPa", "GPa", "kPa", "Pa", "psi"),
        "STRESS_LIMIT_FOR_TENSION": ("MPa", "GPa", "kPa", "Pa", "psi"),
        "STRESS_LIMIT_FOR_COMPRESSION": ("MPa", "GPa", "kPa", "Pa", "psi"),
        "STRESS_LIMIT_FOR_SHEAR": ("MPa", "GPa", "kPa", "Pa", "psi"),
        "MASS_DENSITY": ("kg/mm^3", "kg/m^3", "g/cm^3", "lb/in^3"),
        "THERMAL_EXPANSION_COEFFICIENT": ("1/C", "1/K", "1/F"),
        "THERM_EXPANSION_REF_TEMPERATURE": ("C", "K", "F"),
        "THERMAL_CONDUCTIVITY": ("mm*kg/(s^3*C)", "W/(m*K)"),
        "SPECIFIC_HEAT": ("mm^2/(s^2*C)", "J/(kg*K)"),
    }

    def __init__(
        self,
        document: PartDocument,
        materials_path: Path,
        language: str,
        default_units: dict[str, str],
        parent=None,
        save_callback: Callable[[], bool] | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.material.title"))
        self.resize(1100, 700)
        self.setMinimumSize(620, 380)
        self.setSizeGripEnabled(True)
        self.document = document
        self.materials_path = materials_path
        self.language = language
        self.default_units = default_units
        self.save_callback = save_callback
        self.parameter_descriptions: dict[str, dict[str, str]] = copy.deepcopy(
            document.material_parameter_descriptions
        )
        self.parameter_units: dict[str, str] = dict(document.physical_parameter_units)

        layout = QVBoxLayout(self)

        top_layout = QHBoxLayout()
        top_layout.addWidget(QLabel(tr("dialog.material.current_data")))
        top_layout.addStretch(1)
        load_button = QPushButton(tr("dialog.material.load"))
        load_button.clicked.connect(self._load_from_library)
        top_layout.addWidget(load_button)
        layout.addLayout(top_layout)

        self.table = QTableWidget(0, 4)
        self.table.setAlternatingRowColors(True)
        self.table.setHorizontalHeaderLabels(
            [
                tr("column.parameter"),
                tr("column.value"),
                tr("column.unit"),
                tr("column.description"),
            ]
        )
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(
            self.PROPERTY_COLUMN, QHeaderView.ResizeMode.Stretch
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.VALUE_COLUMN, QHeaderView.ResizeMode.Stretch
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.UNIT_COLUMN, QHeaderView.ResizeMode.ResizeToContents
        )
        self.table.horizontalHeader().setSectionResizeMode(
            self.DESCRIPTION_COLUMN, QHeaderView.ResizeMode.Stretch
        )
        layout.addWidget(self.table)

        actions_layout = QHBoxLayout()
        add_button = QPushButton(tr("button.add"))
        delete_button = QPushButton(tr("button.delete"))
        add_button.clicked.connect(self._add_row)
        delete_button.clicked.connect(self._delete_selected_rows)
        actions_layout.addWidget(add_button)
        actions_layout.addWidget(delete_button)
        actions_layout.addStretch(1)
        actions_layout.addWidget(QSizeGrip(self))
        layout.addLayout(actions_layout)

        self.saved_status = create_saved_status_label()
        layout.addWidget(self.saved_status)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(
            self.apply_changes
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        for key, value in document.physical_parameters.items():
            self._insert_row(
                key,
                value,
                self.parameter_units.get(key, self._default_unit(key)),
                self._description(key),
            )

    def _load_from_library(self) -> None:
        file_name, _ = QFileDialog.getOpenFileName(
            self,
            tr("file.select_material"),
            str(self.materials_path),
            tr("file.filter.material"),
        )
        if not file_name:
            return

        material_file = Path(file_name)
        (
            properties,
            self.parameter_descriptions,
            self.parameter_units,
        ) = parse_material_file(material_file)
        self.table.setRowCount(0)
        for key, value in properties.items():
            self._insert_row(
                key,
                value,
                self.parameter_units.get(key, self._default_unit(key)),
                self._description(key),
            )

    def _description(self, property_name: str) -> str:
        descriptions = self.parameter_descriptions.get(property_name, {})
        return descriptions.get(self.language, descriptions.get("", ""))

    def _default_unit(self, property_name: str) -> str:
        unit_key = {
            "YOUNG_MODULUS": "Stress",
            "SHEAR_MODULUS": "Stress",
            "STRESS_LIMIT_FOR_TENSION": "Stress",
            "STRESS_LIMIT_FOR_COMPRESSION": "Stress",
            "STRESS_LIMIT_FOR_SHEAR": "Stress",
            "MASS_DENSITY": "Density",
            "THERMAL_EXPANSION_COEFFICIENT": "ThermalExpansion",
            "THERM_EXPANSION_REF_TEMPERATURE": "Temperature",
            "THERMAL_CONDUCTIVITY": "ThermalConductivity",
            "SPECIFIC_HEAT": "SpecificHeat",
        }.get(property_name)
        if property_name in {
            "POISSON_RATIO",
            "STRUCTURAL_DAMPING_COEFFICIENT",
            "EMISSIVITY",
            "SHEETMETAL_K_FACTOR",
        }:
            return "1"
        return self.default_units.get(unit_key, "") if unit_key else ""

    def _insert_row(
        self,
        property_name: str,
        value: str,
        unit: str,
        description: str,
    ) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)
        property_item = QTableWidgetItem(property_name)
        property_item.setData(Qt.ItemDataRole.UserRole, property_name)
        self.table.setItem(row, self.PROPERTY_COLUMN, property_item)
        self.table.setItem(row, self.VALUE_COLUMN, QTableWidgetItem(value))
        unit_combo = NoWheelComboBox(self.table)
        unit_combo.setEditable(False)
        allowed_units = self._allowed_units(property_name)
        unit_combo.addItems(allowed_units)
        selected_unit = unit if unit in allowed_units else self._default_unit(property_name)
        if selected_unit not in allowed_units:
            selected_unit = allowed_units[0]
        unit_combo.setCurrentText(selected_unit)
        unit_combo.setEnabled(property_name not in self.TEXT_PROPERTIES)
        self.table.setCellWidget(row, self.UNIT_COLUMN, unit_combo)
        description_item = QTableWidgetItem(description)
        description_item.setFlags(
            description_item.flags() & ~Qt.ItemFlag.ItemIsEditable
        )
        self.table.setItem(row, self.DESCRIPTION_COLUMN, description_item)

    def _allowed_units(self, property_name: str) -> tuple[str, ...]:
        if property_name in self.TEXT_PROPERTIES:
            return ("",)
        if property_name in self.DIMENSIONLESS_PROPERTIES:
            return ("1",)
        return self.PROPERTY_UNIT_CHOICES.get(property_name, self.UNIT_CHOICES)

    def _add_row(self) -> None:
        self._insert_row("NEW_PROPERTY", "", "", "")

    def _delete_selected_rows(self) -> None:
        selected_rows = sorted(
            {index.row() for index in self.table.selectedIndexes()},
            reverse=True,
        )
        for row in selected_rows:
            self.table.removeRow(row)

    def _apply_to_document(self) -> bool:
        self.table.clearFocus()
        QApplication.processEvents()

        physical_parameters: dict[str, str] = {}
        physical_parameter_units: dict[str, str] = {}
        for row in range(self.table.rowCount()):
            property_item = self.table.item(row, self.PROPERTY_COLUMN)
            value_item = self.table.item(row, self.VALUE_COLUMN)
            unit_combo = self.table.cellWidget(row, self.UNIT_COLUMN)
            property_name = ""
            if property_item is not None:
                stored_name = property_item.data(Qt.ItemDataRole.UserRole)
                property_name = str(stored_name or property_item.text()).strip()
            value = value_item.text() if value_item is not None else ""
            unit = (
                unit_combo.currentText().strip()
                if isinstance(unit_combo, QComboBox)
                else ""
            )
            if not property_name:
                QMessageBox.information(
                    self, tr("dialog.material.title"), tr("message.required.property_name")
                )
                return False
            physical_parameters[property_name] = value
            if unit:
                physical_parameter_units[property_name] = unit

        self.document.physical_parameters = physical_parameters
        self.document.physical_parameter_units = physical_parameter_units
        self.document.material_parameter_descriptions = copy.deepcopy(
            self.parameter_descriptions
        )
        return True

    def apply_changes(self) -> bool:
        self.saved_status.clear()
        if not self._apply_to_document():
            return False
        if self.save_callback is not None and not self.save_callback():
            return False
        self.saved_status.setText(tr("status.changes_saved"))
        return True

    def accept(self) -> None:
        if not self.apply_changes():
            return
        super().accept()


def parse_material_file(
    material_file: Path,
) -> tuple[dict[str, str], dict[str, dict[str, str]], dict[str, str]]:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(material_file, encoding="utf-8-sig")

    properties: dict[str, str] = {}
    if config.has_section("Material"):
        properties["MATERIAL_NAME"] = config.get("Material", "Name", fallback="")
    if config.has_section("Properties"):
        properties.update(dict(config["Properties"]))
    units = dict(config["PropertyUnits"]) if config.has_section("PropertyUnits") else {}

    descriptions: dict[str, dict[str, str]] = {}
    if config.has_section("ParameterDescriptions"):
        for raw_key, value in config["ParameterDescriptions"].items():
            if "\\" in raw_key:
                key, language = raw_key.rsplit("\\", 1)
            else:
                key, language = raw_key, ""
            descriptions.setdefault(key, {})[language] = value
    return properties, descriptions, units


class DimensionTextLabel(QLabel):
    """Dimension text with optical alignment for technical glyphs."""

    OPTICALLY_CENTERED_SYMBOLS = frozenset(
        "⌀○●□⌴⌵↧⌒∠"
    )

    def paintEvent(self, event) -> None:
        text = self.text()
        painter = QPainter(self)
        painter.setFont(self.font())
        painter.setPen(self.palette().color(QPalette.ColorRole.WindowText))
        metrics = painter.fontMetrics()
        baseline = (
            (self.height() - metrics.height()) // 2 + metrics.ascent()
        )
        reference_center = metrics.boundingRect("0").center().y()
        x = 0
        for character in text:
            vertical_offset = 0
            if character in self.OPTICALLY_CENTERED_SYMBOLS:
                glyph_center = metrics.boundingRect(
                    character
                ).center().y()
                vertical_offset = max(
                    -2,
                    min(2, reference_center - glyph_center),
                )
            painter.drawText(
                x,
                baseline + vertical_offset,
                character,
            )
            x += metrics.horizontalAdvance(character)


class ParameterEditOverlay(QWidget):
    """Small in-view prototype for editing one geometric dimension."""

    valueCommitted = Signal(str)
    selected = Signal()
    contextMenuRequested = Signal(object)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setFixedHeight(24)
        self.setMinimumWidth(96)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self._original_value = ""
        self._display_value = ""
        self._edit_value = ""
        self._editing = False
        self._selected = False
        self._passive_width = 1

        layout = QHBoxLayout(self)
        self._content_layout = layout
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.prefix_label = DimensionTextLabel(self)
        self.value_label = DimensionTextLabel(self)
        self.value_edit = QLineEdit(self)
        self.value_edit.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.value_edit.setFrame(False)
        self.value_edit.setTextMargins(0, 0, 0, 0)
        self.value_edit.setReadOnly(True)
        self.value_edit.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.value_edit.hide()
        self.suffix_label = DimensionTextLabel(self)
        self.tolerance_label = QLabel(self)
        self.tolerance_label.setObjectName("stackedTolerance")
        self.tolerance_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.prefix_label)
        layout.addWidget(self.value_label)
        layout.addWidget(self.value_edit)
        layout.addWidget(self.suffix_label)
        layout.addWidget(self.tolerance_label)
        for widget in (
            self,
            self.prefix_label,
            self.value_label,
            self.value_edit,
            self.suffix_label,
            self.tolerance_label,
        ):
            widget.installEventFilter(self)
        self._update_style(selected=False)
        self.value_edit.returnPressed.connect(self._commit_edit)
        self.hide()

    def _update_style(self, *, selected: bool) -> None:
        self._selected = selected
        text_color = "#202020" if self._editing else (
            "#00d1ff" if selected else "#fff06a"
        )
        background = "#ffffff" if self._editing else "transparent"
        border = "2px solid #00d1ff" if self._editing else "none"
        self._content_layout.setContentsMargins(
            4 if self._editing else 0,
            1 if self._editing else 0,
            4 if self._editing else 0,
            1 if self._editing else 0,
        )
        self.setStyleSheet(
            "ParameterEditOverlay {"
            f"background-color: {background};"
            f"border: {border};"
            "border-radius: 2px;"
            "}"
            "QLabel {"
            "background: transparent;"
            f"color: {text_color};"
            "font-weight: bold;"
            "border: none;"
            "}"
            "QLabel#stackedTolerance {"
            "line-height: 1em;"
            "}"
            "QLineEdit {"
            "background: transparent;"
            f"color: {text_color};"
            "border: none;"
            "padding: 0px;"
            "font-weight: bold;"
            "}"
        )

    def show_value(
        self,
        value: str,
        suffix: str = "",
        prefix: str = "",
        *,
        display_value: str | None = None,
        tolerance_mode: str = "",
        tolerance_value: str = "",
        upper_deviation: str = "",
        lower_deviation: str = "",
    ) -> None:
        self._editing = False
        self._selected = False
        self._edit_value = value
        self._display_value = (
            value if display_value is None else display_value
        )
        self._original_value = self._edit_value
        self.prefix_label.setText(prefix)
        self.value_label.setText(self._display_value)
        self.value_label.show()
        self.value_edit.setText(self._display_value)
        self.value_edit.hide()
        self.value_edit.setReadOnly(True)
        self.value_edit.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        inline_tolerance = (
            f"±{tolerance_value}"
            if tolerance_mode == "symmetric" and tolerance_value
            else tolerance_value
            if tolerance_mode == "single_deviation"
            else ""
        )
        self.suffix_label.setText(f"{suffix}{inline_tolerance}")
        stacked_tolerance = ""
        if tolerance_mode == "deviations":
            stacked_tolerance = "\n".join(
                value
                for value in (upper_deviation, lower_deviation)
                if value
            )
        self.tolerance_label.setText(stacked_tolerance)
        self.tolerance_label.setVisible(bool(stacked_tolerance))
        self.tolerance_label.adjustSize()
        self.setFixedHeight(
            max(34, self.tolerance_label.sizeHint().height())
            if stacked_tolerance
            else 24
        )
        for widget in (
            self.prefix_label,
            self.value_label,
            self.value_edit,
            self.suffix_label,
        ):
            self._content_layout.setAlignment(
                widget,
                (
                    Qt.AlignmentFlag.AlignBottom
                    if stacked_tolerance
                    else Qt.AlignmentFlag.AlignBaseline
                ),
            )
        self._content_layout.setAlignment(
            self.tolerance_label,
            (
                Qt.AlignmentFlag.AlignBottom
                if stacked_tolerance
                else Qt.AlignmentFlag.AlignBaseline
            ),
        )
        self.setToolTip(
            " ".join(
                part
                for part in (
                    prefix,
                    self._display_value,
                    suffix,
                    inline_tolerance,
                    stacked_tolerance.replace("\n", " / "),
                )
                if part
            )
        )
        metrics = self.fontMetrics()
        value_width = max(
            1,
            metrics.horizontalAdvance(self._display_value) + 2,
        )
        self.value_label.setFixedWidth(value_width)
        self.prefix_label.adjustSize()
        self.suffix_label.adjustSize()
        prefix_width = (
            self.prefix_label.sizeHint().width() if prefix else 0
        )
        suffix_width = (
            self.suffix_label.sizeHint().width()
            if self.suffix_label.text()
            else 0
        )
        self.prefix_label.setFixedWidth(prefix_width)
        self.suffix_label.setFixedWidth(suffix_width)
        content_width = prefix_width + value_width + suffix_width
        if stacked_tolerance:
            self.tolerance_label.adjustSize()
            tolerance_width = max(
                self.tolerance_label.fontMetrics().horizontalAdvance(value)
                for value in self.tolerance_label.text().splitlines()
            )
            self.tolerance_label.setFixedWidth(tolerance_width + 2)
            content_width += tolerance_width + 2
        self._passive_width = max(1, content_width + 4)
        self.setFixedWidth(self._passive_width)
        self._update_style(selected=False)
        self.show()
        self.raise_()

    def _select_dimension(self) -> None:
        self.selected.emit()
        self._update_style(selected=True)

    def set_selected(self, selected: bool) -> None:
        self._update_style(selected=selected)

    def _begin_edit(self) -> None:
        self._select_dimension()
        self._editing = True
        self._original_value = self._edit_value
        self.value_label.hide()
        self.value_edit.show()
        self.value_edit.setText(self._edit_value)
        self.value_edit.setFixedWidth(
            max(
                64,
                self.fontMetrics().horizontalAdvance(self._edit_value)
                + 16,
            )
        )
        self.setFixedWidth(
            max(
                96,
                self.prefix_label.sizeHint().width()
                + self.value_edit.width()
                + self.suffix_label.sizeHint().width()
                + self.tolerance_label.sizeHint().width()
                + 12,
            )
        )
        self.value_edit.setReadOnly(False)
        self.value_edit.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.value_edit.setFocus(Qt.FocusReason.MouseFocusReason)
        self.value_edit.selectAll()
        self._update_style(selected=True)

    def _commit_edit(self) -> None:
        if not self._editing:
            return
        self._editing = False
        self.value_edit.hide()
        self.value_label.show()
        self.value_edit.setReadOnly(True)
        self.value_edit.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setFixedWidth(self._passive_width)
        self._update_style(selected=self._selected)
        self.valueCommitted.emit(self.value_edit.text())

    def _cancel_edit(self) -> None:
        self._editing = False
        self.value_edit.hide()
        self.value_label.show()
        self.value_edit.setText(self._display_value)
        self.value_edit.setFixedWidth(
            max(
                1,
                self.fontMetrics().horizontalAdvance(
                    self._display_value
                )
                + 2,
            )
        )
        self.setFixedWidth(self._passive_width)
        self.value_edit.setReadOnly(True)
        self.value_edit.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._update_style(selected=self._selected)

    def move_to(self, position: QPoint) -> None:
        x = max(
            0,
            min(
                position.x(),
                self.parent().width() - self.width(),
            ),
        )
        y = max(
            0,
            min(
                position.y()
                - 12
                - max(0, self.height() - 24),
                self.parent().height() - self.height(),
            ),
        )
        self.move(x, y)

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self.valueCommitted.emit(self.value_edit.text())
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def eventFilter(self, watched, event) -> bool:
        if event.type() == QEvent.Type.MouseButtonPress:
            if event.button() == Qt.MouseButton.RightButton:
                self._select_dimension()
                self.contextMenuRequested.emit(
                    event.globalPosition().toPoint()
                )
                event.accept()
                return True
            if event.button() == Qt.MouseButton.LeftButton:
                self._select_dimension()
                event.accept()
                return True
        if event.type() == QEvent.Type.MouseButtonDblClick:
            if event.button() == Qt.MouseButton.LeftButton:
                self._begin_edit()
                event.accept()
                return True
        if (
            watched is self.value_edit
            and event.type() == QEvent.Type.KeyPress
            and self._editing
        ):
            if event.key() == Qt.Key.Key_Escape:
                self._cancel_edit()
                event.accept()
                return True
        if (
            watched is self.value_edit
            and event.type() == QEvent.Type.FocusOut
            and self._editing
        ):
            self._commit_edit()
        if (
            watched is self.value_edit
            and event.type() in (
                QEvent.Type.MouseButtonPress,
                QEvent.Type.MouseButtonRelease,
            )
            and event.button() == Qt.MouseButton.MiddleButton
        ):
            if event.type() == QEvent.Type.MouseButtonRelease:
                self.valueCommitted.emit(self.value_edit.text())
            event.accept()
            return True
        return super().eventFilter(watched, event)


class DimensionPropertiesDialog(QDialog):
    applied = Signal(dict)

    DIMENSION_SYMBOLS = (
        ("⌀", "dialog.dimension_properties.symbol.diameter"),
        ("○", "dialog.dimension_properties.symbol.circle"),
        ("●", "dialog.dimension_properties.symbol.filled_circle"),
        ("R", "dialog.dimension_properties.symbol.radius"),
        ("SR", "dialog.dimension_properties.symbol.spherical_radius"),
        ("S⌀", "dialog.dimension_properties.symbol.spherical_diameter"),
        ("□", "dialog.dimension_properties.symbol.square"),
        ("⌴", "dialog.dimension_properties.symbol.counterbore"),
        ("⌵", "dialog.dimension_properties.symbol.countersink"),
        ("↧", "dialog.dimension_properties.symbol.depth"),
        ("⌒", "dialog.dimension_properties.symbol.arc_length"),
        ("∠", "dialog.dimension_properties.symbol.angle"),
        ("°", "dialog.dimension_properties.symbol.degrees"),
        ("±", "dialog.dimension_properties.symbol.plus_minus"),
        ("×", "dialog.dimension_properties.symbol.multiplication"),
        ("≈", "dialog.dimension_properties.symbol.approximately"),
    )

    def __init__(
        self,
        style: dict[str, Any],
        default_decimal_places: int,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.dimension_properties.title"))
        self._title_drag_origin: QPointF | None = None
        self._title_drag_window_origin: QPoint | None = None
        if isinstance(parent, QWidget):
            self.setWindowFlags(
                Qt.WindowType.SubWindow
                | Qt.WindowType.WindowTitleHint
                | Qt.WindowType.WindowCloseButtonHint
            )
            self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
            self.setAutoFillBackground(True)
        self.setMinimumSize(460, 300)
        self.resize(540, 360)
        layout = QVBoxLayout(self)
        if self.windowFlags() & Qt.WindowType.SubWindow:
            self.setObjectName("dimensionPropertiesSubWindow")
            self.setStyleSheet(
                "QDialog#dimensionPropertiesSubWindow {"
                " background: palette(window);"
                " border: 1px solid palette(mid);"
                " border-radius: 5px;"
                "}"
            )
            layout.setContentsMargins(8, 6, 8, 8)
            self._internal_title_bar = QWidget(self)
            self._internal_title_bar.setObjectName("propertiesTitleBar")
            self._internal_title_bar.setFixedHeight(34)
            self._internal_title_bar.setCursor(
                Qt.CursorShape.SizeAllCursor
            )
            self._internal_title_bar.setStyleSheet(
                "QWidget#propertiesTitleBar {"
                " background: palette(midlight);"
                " border: 1px solid palette(mid);"
                " border-radius: 4px;"
                "}"
            )
            title_layout = QHBoxLayout(self._internal_title_bar)
            title_layout.setContentsMargins(10, 2, 4, 2)
            title_label = QLabel(self.windowTitle())
            title_font = title_label.font()
            title_font.setBold(True)
            title_label.setFont(title_font)
            title_layout.addWidget(title_label, 1)
            close_button = QPushButton("×")
            close_button.setFixedSize(27, 26)
            close_button.setToolTip(tr("button.cancel"))
            close_button.setStyleSheet(
                "QPushButton { border: none; border-radius: 4px;"
                " font-size: 18px; font-weight: 700; }"
                "QPushButton:hover { background: #b83232; color: white; }"
            )
            close_button.clicked.connect(self.reject)
            title_layout.addWidget(close_button)
            self._internal_title_bar.installEventFilter(self)
            layout.addWidget(self._internal_title_bar)
        form = QFormLayout()
        self.prefix_edit = QLineEdit(str(style.get("prefix", "")))
        self.suffix_edit = QLineEdit(str(style.get("suffix", "")))
        self.upper_tolerance_edit = QLineEdit(
            str(style.get("upper_tolerance", ""))
        )
        self.lower_tolerance_edit = QLineEdit(
            str(style.get("lower_tolerance", ""))
        )
        self.symmetric_tolerance_edit = QLineEdit(
            str(style.get("symmetric_tolerance", ""))
        )
        self.single_tolerance_edit = QLineEdit(
            str(style.get("single_tolerance", ""))
        )
        self.tolerance_mode_combo = QComboBox()
        for mode, key in (
            ("", "dialog.dimension_properties.tolerance.none"),
            ("symmetric", "dialog.dimension_properties.tolerance.symmetric"),
            (
                "single_deviation",
                "dialog.dimension_properties.tolerance.single",
            ),
            (
                "deviations",
                "dialog.dimension_properties.tolerance.deviations",
            ),
        ):
            self.tolerance_mode_combo.addItem(tr(key), mode)
        tolerance_mode = str(style.get("tolerance_mode", ""))
        if not tolerance_mode:
            upper = self.upper_tolerance_edit.text().strip()
            lower = self.lower_tolerance_edit.text().strip()
            if upper and lower:
                tolerance_mode = "deviations"
            elif upper or lower:
                tolerance_mode = "single_deviation"
                self.single_tolerance_edit.setText(upper or lower)
        mode_index = self.tolerance_mode_combo.findData(tolerance_mode)
        self.tolerance_mode_combo.setCurrentIndex(max(0, mode_index))
        self.decimal_places_spin = QSpinBox()
        self.decimal_places_spin.setRange(0, 12)
        try:
            decimal_places = int(
                style.get("decimal_places", default_decimal_places)
            )
        except (TypeError, ValueError):
            decimal_places = default_decimal_places
        self.decimal_places_spin.setValue(decimal_places)
        form.addRow(
            tr("dialog.dimension_properties.prefix"),
            self._symbol_edit_row(self.prefix_edit),
        )
        form.addRow(
            tr("dialog.dimension_properties.suffix"),
            self._symbol_edit_row(self.suffix_edit),
        )
        form.addRow(
            tr("dialog.dimension_properties.tolerance_mode"),
            self.tolerance_mode_combo,
        )
        form.addRow(
            tr("dialog.dimension_properties.symmetric_tolerance"),
            self.symmetric_tolerance_edit,
        )
        form.addRow(
            tr("dialog.dimension_properties.single_tolerance"),
            self.single_tolerance_edit,
        )
        form.addRow(
            tr("dialog.dimension_properties.upper_deviation"),
            self.upper_tolerance_edit,
        )
        form.addRow(
            tr("dialog.dimension_properties.lower_deviation"),
            self.lower_tolerance_edit,
        )
        form.addRow(
            tr("dialog.dimension_properties.decimal_places"),
            self.decimal_places_spin,
        )
        layout.addLayout(form)
        self._tolerance_form = form
        self.tolerance_mode_combo.currentIndexChanged.connect(
            self._update_tolerance_fields
        )
        self._update_tolerance_fields()
        layout.addStretch(1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Apply
            | QDialogButtonBox.StandardButton.Cancel
        )
        localize_dialog_buttons(buttons)
        buttons.button(
            QDialogButtonBox.StandardButton.Apply
        ).clicked.connect(self._apply_without_closing)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        button_row = QHBoxLayout()
        button_row.addWidget(buttons, 1)
        if self.windowFlags() & Qt.WindowType.SubWindow:
            size_grip = QSizeGrip(self)
            size_grip.setToolTip(
                tr("dialog.dimension_properties.resize")
            )
            button_row.addWidget(
                size_grip,
                0,
                Qt.AlignmentFlag.AlignRight
                | Qt.AlignmentFlag.AlignBottom,
            )
        layout.addLayout(button_row)

    def _apply_without_closing(self) -> None:
        self.applied.emit(self.dimension_style())

    def _symbol_edit_row(self, edit: QLineEdit) -> QWidget:
        row = QWidget(self)
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)
        layout.addWidget(edit)

        button = QToolButton(row)
        button.setText("⌀")
        button.setToolTip(
            tr("dialog.dimension_properties.insert_symbol")
        )
        button.setPopupMode(
            QToolButton.ToolButtonPopupMode.InstantPopup
        )
        menu = QMenu(button)
        for symbol, translation_key in self.DIMENSION_SYMBOLS:
            action = menu.addAction(f"{symbol}   {tr(translation_key)}")
            action.triggered.connect(
                lambda _checked=False, value=symbol, target=edit:
                self._insert_symbol(target, value)
            )
        button.setMenu(menu)
        layout.addWidget(button)
        return row

    @staticmethod
    def _insert_symbol(edit: QLineEdit, symbol: str) -> None:
        edit.insert(symbol)
        edit.setFocus()

    def _update_tolerance_fields(self) -> None:
        mode = str(self.tolerance_mode_combo.currentData() or "")
        visibility = (
            (self.symmetric_tolerance_edit, mode == "symmetric"),
            (self.single_tolerance_edit, mode == "single_deviation"),
            (self.upper_tolerance_edit, mode == "deviations"),
            (self.lower_tolerance_edit, mode == "deviations"),
        )
        for field, visible in visibility:
            field.setVisible(visible)
            label = self._tolerance_form.labelForField(field)
            if label is not None:
                label.setVisible(visible)

    def eventFilter(self, watched, event) -> bool:
        if watched is getattr(self, "_internal_title_bar", None):
            if (
                event.type() == QEvent.Type.MouseButtonPress
                and event.button() == Qt.MouseButton.LeftButton
            ):
                self._title_drag_origin = event.globalPosition()
                self._title_drag_window_origin = self.pos()
                event.accept()
                return True
            if (
                event.type() == QEvent.Type.MouseMove
                and self._title_drag_origin is not None
                and self._title_drag_window_origin is not None
                and event.buttons() & Qt.MouseButton.LeftButton
            ):
                delta = event.globalPosition() - self._title_drag_origin
                target = self._title_drag_window_origin + QPoint(
                    int(delta.x()),
                    int(delta.y()),
                )
                parent = self.parentWidget()
                if parent is not None:
                    target.setX(max(
                        0,
                        min(target.x(), parent.width() - self.width()),
                    ))
                    target.setY(max(
                        0,
                        min(target.y(), parent.height() - 34),
                    ))
                self.move(target)
                event.accept()
                return True
            if (
                event.type() == QEvent.Type.MouseButtonRelease
                and event.button() == Qt.MouseButton.LeftButton
            ):
                self._title_drag_origin = None
                self._title_drag_window_origin = None
                event.accept()
                return True
        return super().eventFilter(watched, event)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        parent = self.parentWidget()
        if (
            parent is not None
            and self.windowFlags() & Qt.WindowType.SubWindow
        ):
            self.setMaximumSize(parent.size())
        application = QApplication.instance()
        if application is not None:
            application._middle_confirmation_target = self
        position_dialog_top_right_after_show(self)

    def done(self, result: int) -> None:
        application = QApplication.instance()
        if (
            application is not None
            and getattr(
                application,
                "_middle_confirmation_target",
                None,
            ) is self
        ):
            application._middle_confirmation_target = None
        super().done(result)

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        parent = self.parentWidget()
        if (
            parent is None
            or not self.windowFlags() & Qt.WindowType.SubWindow
        ):
            return
        self.move(
            max(0, min(self.x(), parent.width() - self.width())),
            max(0, min(self.y(), parent.height() - self.height())),
        )

    def dimension_style(self) -> dict[str, Any]:
        return {
            "prefix": self.prefix_edit.text(),
            "suffix": self.suffix_edit.text(),
            "tolerance_mode": str(
                self.tolerance_mode_combo.currentData() or ""
            ),
            "symmetric_tolerance": self.symmetric_tolerance_edit.text(),
            "single_tolerance": self.single_tolerance_edit.text(),
            "upper_tolerance": self.upper_tolerance_edit.text(),
            "lower_tolerance": self.lower_tolerance_edit.text(),
            "decimal_places": self.decimal_places_spin.value(),
        }


class MainWindow(QMainWindow):
    def _install_qt_translations(
        self,
        language: str,
    ) -> list[QTranslator]:
        application = QApplication.instance()
        if application is None:
            return []
        locale_name = language.replace("-", "_")
        translations_path = QLibraryInfo.path(
            QLibraryInfo.LibraryPath.TranslationsPath
        )
        for catalog in (
            f"qt_{locale_name}",
            f"qtbase_{locale_name}",
        ):
            translator = QTranslator(self)
            if translator.load(catalog, translations_path):
                application.installTranslator(translator)
                return [translator]
        return []

    def __init__(
        self,
        startup_context: StartupContext | None = None,
        workspace: ApplicationWorkspace | None = None,
    ) -> None:
        super().__init__()

        application = QApplication.instance()
        if (
            application is not None
            and not hasattr(application, "_dialog_middle_button_filter")
        ):
            application._dialog_middle_button_filter = (
                DialogMiddleButtonFilter(application)
            )
            application.installEventFilter(
                application._dialog_middle_button_filter
            )

        self.workspace = workspace or ApplicationWorkspace()
        self.workspace.windows.append(self)
        self.workspace.sessionsChanged.connect(
            self._sync_workspace_sessions
        )
        self.workspace.documentChanged.connect(
            self._sync_workspace_document
        )
        self._handling_workspace_update = False
        self.application_root = application_root()
        ensure_application_directories()
        self.startup_context = startup_context or StartupContext(
            working_directory=Path.cwd().resolve()
        )
        self.settings = load_application_settings(
            self.startup_context.local_config_path
        )
        configure_localization(
            self.settings.localization_path,
            self.settings.language,
        )
        self._qt_translators = self._install_qt_translations(
            self.settings.language
        )
        self.setWindowTitle(tr("app.title"))
        self.setWindowIcon(
            QIcon(str(app_path("resources", "branding", "app-icon.svg")))
        )
        self.resize(1200, 800)
        self.document_sessions = self.workspace.document_sessions
        self.active_document_index = -1
        self.document: PartDocument | None = None
        self.current_file_path: Path | None = None
        self.working_directory = self.startup_context.working_directory

        self.tree = HistoryTreeWidget()
        self.tree.setColumnCount(1)
        self.tree.setHeaderLabels(["PART"])
        self.tree.setMinimumWidth(280)
        self.tree.setStyleSheet(
            """
            QTreeWidget::item:selected,
            QTreeWidget::item:selected:active,
            QTreeWidget::item:selected:!active {
                background-color: #356E22;
                color: #FFFFFF;
            }
            QTreeWidget::item:hover {
                background-color: #356E22;
                color: #FFFFFF;
            }
            QTreeWidget[historyDragActive="true"]::item:hover,
            QTreeWidget[historyDragActive="true"]::item:selected,
            QTreeWidget[historyDragActive="true"]::item:selected:active,
            QTreeWidget[historyDragActive="true"]::item:selected:!active {
                background-color: transparent;
                color: #E7EAF0;
            }
            """
        )
        self.tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)

        self.selected_object_id: str | None = None
        self._hovered_coordinate_object_id: str | None = None
        self.view_display_mode = ViewDisplayMode.SHADED_WITH_EDGES
        self.view_selection_mode = ViewSelectionMode.CONTAINER
        self.view_selection_filter = ViewSelectionFilter.ALL
        self.active_application = ApplicationMode.MODELING
        self.view_selection_enabled = True
        self._selectable_model_shapes: list[tuple[Any, str]] = []
        self._cached_document = None
        self._cached_history_boundary: int | None = None
        self._cached_model_shapes: list[tuple[Any, str]] = []
        self._cached_source_model_shapes: list[tuple[Any, str]] = []
        self.selected_face = None
        self.selected_face_object_id: str | None = None
        self._view_selection_confirmed = False
        self._history_source_cycle_index = -1
        self._history_source_cycle_ids: tuple[str, ...] = ()
        self._history_source_cycle_active = False
        self._cycled_history_source_id: str | None = None
        self._reference_cycle_preview_id: str | None = None
        self._view_candidate_cycle_ids: tuple[str, ...] = ()
        self._view_candidate_cycle_index = -1
        self._point_constraint_cycle_keys: tuple[str, ...] = ()
        self._point_constraint_cycle_index = -1
        self._point_constraint_preview: tuple[str, Any] | None = None
        self.point_constraint_dialog: PointConstraintDialog | None = None
        self._definition_dialog_depth = 0
        self._definition_edit_objects: list[ZimaEntity] = []
        self._pending_attachment_plane_id: str | None = None
        self._normal_view_selection_active = False
        self._sketch_edit_entity_id: str | None = None
        self._sketch_previous_camera = None
        self._sketch_baseline_parameters: dict[str, str] | None = None
        self._sketch_tool: str | None = None
        self._sketch_pending_points: list[tuple[float, float]] = []
        self._sketch_pending_point_ids: list[str] = []
        self._sketch_pending_new_point_ids: set[str] = set()
        self._sketch_pending_constraint: str | None = None
        self._sketch_coincident_first_point_id: str | None = None
        self._sketch_selected_entity_id: str | None = None
        self._sketch_selected_reference: tuple[str, str, int] | None = None
        self._sketch_show_all_dimensions = False
        self._sketch_reference_mode = False
        self._sketch_selected_external_reference_id: str | None = None
        self._sketch_delete_action = QAction(
            tr("sketch.command.delete"),
            self,
        )
        self._sketch_delete_action.setShortcut(QKeySequence("Delete"))
        self._sketch_delete_action.setShortcutContext(
            Qt.ShortcutContext.ApplicationShortcut
        )
        self._sketch_delete_action.setEnabled(False)
        self._sketch_delete_action.triggered.connect(
            self._delete_selected_sketch_entity
        )
        self.addAction(self._sketch_delete_action)

        self.native_viewer = ZimaOpenGLViewer(self)
        self._native_viewer_scene: DocumentViewerScene | None = None
        self._dimension_overlays: dict[str, ParameterEditOverlay] = {}
        self._dimension_object_id: str | None = None
        self._dimension_bindings: dict[str, tuple[Any, ...]] = {}
        self.native_viewer.navigationChanged.connect(
            lambda _camera: self._position_dimension_overlays()
        )
        self.native_viewer.viewportResized.connect(
            lambda _width, _height, _ratio:
            self._position_dimension_overlays()
        )
        self.native_viewer.selectedEdgeChanged.connect(
            self._on_native_edge_selected
        )
        self.native_viewer.selectedFaceChanged.connect(
            self._on_native_face_selected
        )
        self.native_viewer.selectedPointChanged.connect(
            lambda owner_id, index: self._on_native_coordinate_selected(
                owner_id, index, "point"
            )
        )
        self.native_viewer.selectedPlaneChanged.connect(
            lambda owner_id, index: self._on_native_coordinate_selected(
                owner_id, index, "plane"
            )
        )
        self.native_viewer.selectedObjectChanged.connect(
            self._on_native_object_selected
        )
        self.native_viewer.objectDoubleClicked.connect(
            self._on_native_object_double_clicked
        )
        self.native_viewer.dimensionsDismissRequested.connect(
            self._dismiss_dimension_overlays
        )
        self.native_viewer.selectionPreviewConfirmed.connect(
            self._on_view_selection_preview_confirmed
        )
        self.native_viewer.sketchPositionClicked.connect(
            self._on_sketch_position_clicked
        )
        self.native_viewer.sketchReferencePositionClicked.connect(
            self._on_sketch_reference_position_clicked
        )
        self.native_viewer.sketchPlacementClicked.connect(
            self._on_sketch_placement_clicked
        )
        self.native_viewer.sketchReferenceHovered.connect(
            self._on_sketch_reference_hovered
        )
        self.native_viewer.sketchCancelCurrentRequested.connect(
            self._cancel_current_sketch_entity
        )
        self.native_viewer.sketchConfirmCurrentRequested.connect(
            self._confirm_current_sketch_entity
        )
        self.native_viewer.sketchEntitySelected.connect(
            self._select_sketch_entity
        )
        self.native_viewer.sketchEntityHovered.connect(
            self._on_sketch_entity_hovered
        )
        self.regenerate_action = QAction(
            tr("command.regenerate"),
            self,
        )
        self.regenerate_action.setShortcut("F5")
        self.regenerate_action.setToolTip(
            tr("command.regenerate.tooltip")
        )
        self.regenerate_action.triggered.connect(self.regenerate_model)

        self.view_toolbar = QToolBar(tr("toolbar.view"))
        self.view_toolbar.setMovable(False)
        self.view_toolbar.setStyleSheet(
            """
            QToolButton:hover:enabled {
                background-color: rgba(255, 255, 255, 32);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            QToolButton:checked {
                background-color: rgba(77, 216, 17, 125);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            QToolButton:pressed {
                background-color: rgba(77, 216, 17, 165);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            """
        )
        self.view_toolbar.addAction(self.regenerate_action)
        self.view_toolbar.addSeparator()
        self.reset_view_action = self.view_toolbar.addAction(
            tr("toolbar.reset_view")
        )
        self.reset_view_action.setIcon(resource_icon("view-fit"))
        self.reset_view_action.triggered.connect(self.reset_view)
        self.normal_view_action = self.view_toolbar.addAction(
            tr("toolbar.view.normal")
        )
        self.normal_view_action.setIcon(resource_icon("view-normal"))
        self.normal_view_action.setCheckable(True)
        self.normal_view_action.setToolTip(
            tr("toolbar.view.normal.tooltip")
        )
        self.normal_view_action.toggled.connect(
            self._toggle_normal_view_selection
        )
        self.cancel_normal_view_action = QAction(self)
        self.cancel_normal_view_action.setShortcut("Esc")
        self.cancel_normal_view_action.setShortcutContext(
            Qt.ShortcutContext.ApplicationShortcut
        )
        self.cancel_normal_view_action.setEnabled(False)
        self.cancel_normal_view_action.triggered.connect(
            self._cancel_normal_view_selection
        )
        self.addAction(self.cancel_normal_view_action)
        self.close_active_tab_action = QAction(self)
        self.close_active_tab_action.setShortcut(QKeySequence("F2"))
        self.close_active_tab_action.setShortcutContext(
            Qt.ShortcutContext.ApplicationShortcut
        )
        self.close_active_tab_action.triggered.connect(self.close_document)
        self.addAction(self.close_active_tab_action)
        self.standard_view_combo = QComboBox()
        for text_key, view_name in (
            ("toolbar.standard_views", ""),
            ("toolbar.view.default", "default"),
            ("toolbar.view.front", "front"),
            ("toolbar.view.back", "back"),
            ("toolbar.view.left", "left"),
            ("toolbar.view.right", "right"),
            ("toolbar.view.top", "top"),
            ("toolbar.view.bottom", "bottom"),
        ):
            self.standard_view_combo.addItem(tr(text_key), view_name)
        self.standard_view_combo.currentIndexChanged.connect(
            self._on_standard_view_changed
        )
        self.view_toolbar.addWidget(self.standard_view_combo)
        self.view_selection_action = QAction(
            tr("application.command.selection"),
            self,
        )
        self.view_selection_action.setCheckable(True)
        self.view_selection_action.setChecked(True)
        self.view_selection_action.setToolTip(
            tr("application.command.selection.tooltip")
        )
        self.view_selection_action.toggled.connect(
            self.set_view_selection_enabled
        )
        self.view_toolbar.addAction(self.view_selection_action)
        self.selection_filter_combo = QComboBox()
        self.selection_filter_combo.setToolTip(tr("selection.filter.tooltip"))
        for text_key, filter_value in (
            ("selection.filter.all", ViewSelectionFilter.ALL),
            ("selection.filter.face", ViewSelectionFilter.FACE),
            ("selection.filter.point", ViewSelectionFilter.POINT),
            ("selection.filter.axis", ViewSelectionFilter.AXIS),
            ("selection.filter.plane", ViewSelectionFilter.PLANE),
        ):
            self.selection_filter_combo.addItem(tr(text_key), filter_value.value)
        self.selection_filter_combo.currentIndexChanged.connect(
            self._on_selection_filter_changed
        )
        self.view_toolbar.addWidget(self.selection_filter_combo)
        self.view_toolbar.addSeparator()

        self.display_mode_actions = QActionGroup(self)
        self.display_mode_actions.setExclusive(True)
        self.wire_action = self._add_display_mode_action(
            tr("toolbar.wire"),
            tr("toolbar.wire.tooltip"),
            ViewDisplayMode.WIRE,
        )
        self.edges_action = self._add_display_mode_action(
            tr("toolbar.edges"),
            tr("toolbar.edges.tooltip"),
            ViewDisplayMode.SHADED_WITH_EDGES,
        )
        self.shaded_action = self._add_display_mode_action(
            tr("toolbar.solid"),
            tr("toolbar.solid.tooltip"),
            ViewDisplayMode.SHADED,
        )
        self.edges_action.setChecked(True)
        self.view_toolbar.addSeparator()
        self.show_origins_action = self._add_reference_visibility_action(
            tr("toolbar.show_origins"), tr("toolbar.show_origins.tooltip")
        )
        self.show_origins_action.setIcon(resource_icon("origin"))
        self.show_points_action = self._add_reference_visibility_action(
            tr("toolbar.show_points"), tr("toolbar.show_points.tooltip")
        )
        self.show_points_action.setIcon(resource_icon("point"))
        self.show_axes_action = self._add_reference_visibility_action(
            tr("toolbar.show_axes"), tr("toolbar.show_axes.tooltip")
        )
        self.show_axes_action.setIcon(resource_icon("axis"))
        self.show_planes_action = self._add_reference_visibility_action(
            tr("toolbar.show_planes"), tr("toolbar.show_planes.tooltip")
        )
        self.show_planes_action.setIcon(resource_icon("plane"))

        view_layout = QVBoxLayout()
        view_layout.setContentsMargins(0, 0, 0, 0)
        view_layout.setSpacing(0)
        view_layout.addWidget(self.view_toolbar)

        view_layout.addWidget(self.native_viewer, 1)

        self.view_panel = QWidget()
        self.view_panel.setLayout(view_layout)

        self.tools_toolbar = QToolBar(tr("menu.tools"))
        self.tools_toolbar.setMovable(False)
        self.tools_toolbar.setOrientation(Qt.Orientation.Vertical)
        self.tools_toolbar.setToolButtonStyle(
            Qt.ToolButtonStyle.ToolButtonTextBesideIcon
        )
        self.tools_toolbar.setMinimumWidth(170)
        self.tools_toolbar.setStyleSheet(
            """
            QToolButton {
                padding: 6px 10px;
                text-align: left;
            }
            QToolButton:checked {
                background-color: rgba(77, 216, 17, 125);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            QToolButton#applicationCommandButton:hover:enabled {
                background-color: rgba(77, 216, 17, 90);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            QToolButton#applicationCommandButton:pressed:enabled {
                background-color: rgba(77, 216, 17, 165);
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
            }
            """
        )

        tools_layout = QVBoxLayout()
        tools_layout.setContentsMargins(0, 0, 0, 0)
        tools_layout.setSpacing(0)
        tools_layout.addSpacing(self.view_toolbar.sizeHint().height())
        tools_layout.addWidget(self.tools_toolbar, 1)

        self.tools_panel = QWidget()
        self.tools_panel.setLayout(tools_layout)

        workspace_layout = QHBoxLayout()
        workspace_layout.setContentsMargins(0, 0, 0, 0)
        workspace_layout.setSpacing(0)
        workspace_layout.addWidget(self.view_panel, 1)
        workspace_layout.addWidget(self.tools_panel)

        self.workspace_panel = QWidget()
        self.workspace_panel.setLayout(workspace_layout)

        self.document_splitter = QSplitter(Qt.Horizontal)
        self.document_splitter.addWidget(self.tree)
        self.document_splitter.addWidget(self.workspace_panel)
        self.document_splitter.setStretchFactor(0, 0)
        self.document_splitter.setStretchFactor(1, 1)

        self.document_tabs = QTabBar()
        self.document_tabs.setTabsClosable(True)
        self.document_tabs.setMovable(False)
        self.document_tabs.setExpanding(False)
        self.document_tabs.setUsesScrollButtons(True)
        self.document_tabs.hide()
        self.document_splitter.hide()

        main_layout = QVBoxLayout()
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        main_layout.addWidget(self.document_tabs)
        main_layout.addWidget(self.document_splitter, 1)

        self.main_panel = QWidget()
        self.main_panel.setLayout(main_layout)
        self.setCentralWidget(self.main_panel)
        # Create the status bar before the 3D viewer is initialized.  Creating
        # it lazily for the first selection would shrink the viewport and make
        # the model appear to jump backwards.
        self.statusBar().clearMessage()

        self._create_menu_bar()
        self._rebuild_application_toolbar()
        self._populate_tree()
        self.document_tabs.currentChanged.connect(self._on_document_tab_changed)
        self.document_tabs.tabCloseRequested.connect(self.close_document_tab)
        self.tree.itemSelectionChanged.connect(self._on_tree_selection_changed)
        self.tree.itemClicked.connect(self._on_tree_item_clicked)
        self.tree.itemDoubleClicked.connect(self._on_tree_item_double_clicked)
        self.tree.historyCursorMoved.connect(self._on_history_cursor_moved)
        self.tree.historyObjectMoved.connect(self._on_history_object_moved)
        self.tree.customContextMenuRequested.connect(self._show_tree_context_menu)
        self.native_viewer.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )
        self.native_viewer.customContextMenuRequested.connect(
            self._show_native_viewer_context_menu
        )
        self._sync_workspace_sessions(None)
        self._update_window_title()

    def _add_display_mode_action(
        self,
        text: str,
        tooltip: str,
        display_mode: ViewDisplayMode,
    ):
        action = self.view_toolbar.addAction(text)
        action.setCheckable(True)
        action.setToolTip(tooltip)
        action.setData(display_mode.value)
        action.triggered.connect(
            lambda _checked=False, mode=display_mode: self.set_view_display_mode(mode)
        )
        self.display_mode_actions.addAction(action)
        return action

    def _add_reference_visibility_action(self, text: str, tooltip: str):
        action = self.view_toolbar.addAction(text)
        action.setCheckable(True)
        action.setChecked(True)
        action.setToolTip(tooltip)
        action.toggled.connect(self._on_reference_visibility_changed)
        return action

    def _on_reference_visibility_changed(self, _checked: bool) -> None:
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def set_view_selection_enabled(self, enabled: bool) -> None:
        self.view_selection_enabled = enabled
        self.native_viewer.set_selection_enabled(enabled)
        self.selection_filter_combo.setEnabled(enabled)
        self.statusBar().showMessage(
            tr(
                "selection.status.enabled"
                if enabled
                else "selection.status.navigation_only"
            )
        )

    def set_active_application(self, mode: ApplicationMode) -> None:
        if self.document is None:
            return
        self.active_application = mode
        if 0 <= self.active_document_index < len(self.document_sessions):
            self.document_sessions[
                self.active_document_index
            ].active_application = mode
        self._sync_application_actions()
        self._rebuild_application_toolbar()

    def _sync_application_actions(self) -> None:
        for mode, action in getattr(self, "application_actions", {}).items():
            action.blockSignals(True)
            action.setChecked(mode == self.active_application)
            action.blockSignals(False)

    def _rebuild_application_toolbar(self) -> None:
        if not hasattr(self, "tools_toolbar"):
            return
        self.tools_toolbar.clear()

        heading = QLabel(
            tr("application.sketch")
            if self._sketch_edit_entity_id is not None
            else tr(f"application.{self.active_application.value}")
        )
        heading.setAlignment(Qt.AlignmentFlag.AlignCenter)
        heading.setStyleSheet("font-weight: 600; padding: 6px;")
        self.tools_toolbar.addWidget(heading)
        heading_separator = QWidget()
        heading_separator.setObjectName("applicationHeadingSeparator")
        heading_separator.setFixedHeight(2)
        heading_separator.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Fixed,
        )
        heading_separator.setStyleSheet(
            "background-color: #4DD811;"
        )
        self.tools_toolbar.addWidget(heading_separator)

        self._sketch_delete_action.setEnabled(False)
        if self._sketch_edit_entity_id is not None:
            self._sketch_delete_action.setEnabled(
                self._sketch_selected_entity_id is not None
            )
            normal_view_action = self.tools_toolbar.addAction(
                tr("sketch.command.normal_view")
            )
            normal_view_action.setIcon(resource_icon("view-normal"))
            normal_view_action.setToolTip(
                tr("sketch.command.normal_view.tooltip")
            )
            normal_view_action.triggered.connect(
                self._align_view_to_active_sketch
            )
            self._mark_application_command(normal_view_action)
            self.tools_toolbar.addSeparator()
            select_action = self.tools_toolbar.addAction(
                tr("sketch.tool.select")
            )
            select_action.setIcon(resource_icon("select"))
            select_action.setCheckable(True)
            select_action.setChecked(
                self._sketch_tool == "select"
                and not self._sketch_reference_mode
            )
            select_action.triggered.connect(
                lambda: self._set_sketch_tool("select")
            )
            self._mark_application_command(select_action)
            reference_action = self.tools_toolbar.addAction(
                tr("sketch.command.reference")
            )
            reference_action.setIcon(resource_icon("sketch-reference"))
            reference_action.setToolTip(
                tr("sketch.command.reference.tooltip")
            )
            reference_action.setCheckable(True)
            reference_action.setChecked(self._sketch_reference_mode)
            reference_action.triggered.connect(
                self._toggle_sketch_reference_mode
            )
            self._mark_application_command(reference_action)
            dimensions_visibility_action = self.tools_toolbar.addAction(
                tr("sketch.command.show_dimensions")
            )
            dimensions_visibility_action.setIcon(
                resource_icon("sketch-dimensions")
            )
            dimensions_visibility_action.setToolTip(
                tr("sketch.command.show_dimensions.tooltip")
            )
            dimensions_visibility_action.setCheckable(True)
            dimensions_visibility_action.setChecked(
                self._sketch_show_all_dimensions
            )
            dimensions_visibility_action.triggered.connect(
                self._toggle_all_sketch_dimensions
            )
            self._mark_application_command(dimensions_visibility_action)
            self.tools_toolbar.addSeparator()
            for tool, text_key, icon_name in (
                (
                    "construction",
                    "sketch.tool.construction",
                    "sketch-construction",
                ),
                ("point", "sketch.tool.point", "point"),
                ("segment", "sketch.tool.segment", "sketch-segment"),
                ("arc", "sketch.tool.arc", "sketch-arc"),
                ("spline", "sketch.tool.spline", "sketch-spline"),
            ):
                action = self.tools_toolbar.addAction(tr(text_key))
                action.setIcon(resource_icon(icon_name))
                action.setCheckable(True)
                action.setChecked(tool == self._sketch_tool)
                action.triggered.connect(
                    lambda _checked=False, selected_tool=tool:
                    self._set_sketch_tool(selected_tool)
                )
                self._mark_application_command(action)
            self.tools_toolbar.addSeparator()
            self._add_sketch_command_menu(
                "sketch.constraints",
                "sketch-constraints",
                (
                    "sketch.constraint.horizontal",
                    "sketch.constraint.vertical",
                    "sketch.constraint.parallel",
                    "sketch.constraint.perpendicular",
                    "sketch.constraint.coincident",
                    "sketch.constraint.tangent",
                    "sketch.constraint.concentric",
                ),
            )
            self._add_sketch_command_menu(
                "sketch.dimensions",
                "sketch-dimensions",
                (
                    "sketch.dimension.length",
                    "sketch.dimension.distance",
                    "sketch.dimension.horizontal_distance",
                    "sketch.dimension.vertical_distance",
                    "sketch.dimension.angle",
                    "sketch.dimension.radius",
                    "sketch.dimension.diameter",
                ),
            )
            self.tools_toolbar.addSeparator()
            finish_action = self.tools_toolbar.addAction(
                tr("sketch.command.finish")
            )
            finish_action.setIcon(resource_icon("sketch"))
            finish_action.triggered.connect(self._finish_sketch_edit)
            self._mark_application_command(finish_action)
            finish_button = self.tools_toolbar.widgetForAction(finish_action)
            if finish_button is not None:
                finish_button.setStyleSheet(
                    "background: rgba(77, 216, 17, 150);"
                    "color: white; font-weight: 700;"
                )
            cancel_action = self.tools_toolbar.addAction(
                tr("sketch.command.cancel")
            )
            cancel_action.setIcon(resource_icon("settings"))
            cancel_action.setToolTip(
                tr("sketch.command.cancel.tooltip")
            )
            cancel_action.triggered.connect(self._cancel_sketch_edit)
            self._mark_application_command(cancel_action)
            cancel_button = self.tools_toolbar.widgetForAction(cancel_action)
            if cancel_button is not None:
                cancel_button.setStyleSheet(
                    "background: rgba(184, 50, 50, 170);"
                    "color: white; font-weight: 700;"
                )
        elif self.active_application == ApplicationMode.MODELING:
            new_container_action = self.tools_toolbar.addAction(
                tr("menu.context.create_container")
            )
            new_container_action.setIcon(resource_icon("part"))
            self._mark_application_command(new_container_action)
            new_container_action.triggered.connect(self.create_new_container)
            self.tools_toolbar.addSeparator()
            point_action = self.tools_toolbar.addAction(tr("primitive.point"))
            point_action.setIcon(resource_icon("point"))
            self._mark_application_command(point_action)
            point_action.triggered.connect(self._create_point_object)
            axis_action = self.tools_toolbar.addAction(tr("primitive.axis"))
            axis_action.setIcon(resource_icon("axis"))
            self._mark_application_command(axis_action)
            axis_action.triggered.connect(self._create_axis_object)
            plane_action = self.tools_toolbar.addAction(tr("primitive.plane"))
            plane_action.setIcon(resource_icon("plane"))
            self._mark_application_command(plane_action)
            plane_action.triggered.connect(self._create_plane_object)
            sketch_action = self.tools_toolbar.addAction(
                tr("menu.context.create_sketch")
            )
            sketch_action.setIcon(resource_icon("sketch"))
            self._mark_application_command(sketch_action)
            sketch_action.triggered.connect(self._create_sketch_from_selection)
            self.tools_toolbar.addSeparator()
            for kind, text_key in (
                (EntityKind.BOX, "primitive.box"),
                (EntityKind.SPHERE, "primitive.sphere"),
                (EntityKind.CYLINDER, "primitive.cylinder"),
                (EntityKind.CONE, "primitive.cone"),
                (EntityKind.PYRAMID, "primitive.pyramid"),
                (EntityKind.WEDGE, "primitive.wedge"),
            ):
                primitive_action = self.tools_toolbar.addAction(tr(text_key))
                icon_name = {
                    EntityKind.BOX: "box",
                    EntityKind.SPHERE: "sphere",
                    EntityKind.CYLINDER: "cylinder",
                    EntityKind.CONE: "cone",
                    EntityKind.PYRAMID: "pyramid",
                    EntityKind.WEDGE: "wedge",
                }.get(kind)
                if icon_name is not None:
                    primitive_action.setIcon(resource_icon(icon_name))
                self._mark_application_command(primitive_action)
                primitive_action.triggered.connect(
                    lambda _checked=False, primitive_kind=kind:
                    self._create_primitive_object(primitive_kind)
                )
            self.tools_toolbar.addSeparator()
            profile_action = self.tools_toolbar.addAction(
                tr("application.command.profile_on_geometry")
            )
            self._mark_application_command(profile_action)
            profile_action.setToolTip(
                tr("application.command.profile_on_geometry.tooltip")
            )
            profile_action.setEnabled(False)
        else:
            placeholder = self.tools_toolbar.addAction(
                tr(f"application.placeholder.{self.active_application.value}")
            )
            self._mark_application_command(placeholder)
            placeholder.setEnabled(False)

        has_document = self.document is not None
        self.view_selection_action.setEnabled(has_document)
        for action in self.tools_toolbar.actions():
            if action is not self.view_selection_action and action.isEnabled():
                action.setEnabled(has_document)

    def _mark_application_command(self, action: QAction) -> None:
        button = self.tools_toolbar.widgetForAction(action)
        if button is not None:
            button.setObjectName("applicationCommandButton")
            button.setSizePolicy(
                QSizePolicy.Policy.Expanding,
                QSizePolicy.Policy.Preferred,
            )
            button.setMinimumWidth(
                max(0, self.tools_toolbar.minimumWidth() - 12)
            )

    def _add_sketch_command_menu(
        self,
        title_key: str,
        icon_name: str,
        command_keys: tuple[str, ...],
    ) -> None:
        menu = QMenu(self.tools_toolbar)
        for command_key in command_keys:
            action = menu.addAction(
                resource_icon(icon_name),
                tr(command_key),
            )
            if command_key == "sketch.constraint.coincident":
                action.setEnabled(True)
                action.triggered.connect(
                    lambda _checked=False:
                    self._set_sketch_constraint_tool("coincident")
                )
            else:
                action.setEnabled(False)
        button = QToolButton(self.tools_toolbar)
        button.setObjectName("applicationCommandButton")
        button.setText(tr(title_key))
        button.setIcon(resource_icon(icon_name))
        button.setToolButtonStyle(
            Qt.ToolButtonStyle.ToolButtonTextBesideIcon
        )
        button.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        button.setMenu(menu)
        button.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Preferred,
        )
        button.setMinimumWidth(
            max(0, self.tools_toolbar.minimumWidth() - 12)
        )
        self.tools_toolbar.addWidget(button)

    def _create_sketch_from_selection(self) -> None:
        selected = self._selected_object()
        reference = (
            selected
            if selected is not None and selected.kind == EntityKind.PLANE
            else None
        )
        self._create_sketch_definition(reference)

    def _create_sketch_definition(
        self,
        initial_reference: ZimaEntity | None = None,
    ) -> None:
        if self.document is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = SketchConstraintDialog(
            self._solve_point_constraints,
            self,
            suggested_name=self.document.next_container_name(
                tr("container.type.sketch")
            ),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createSketchRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, diameter: self._apply_new_constrained_sketch(
                dialog, references, fallback, name, show_internal,
                show_auxiliary, rotation, diameter,
            )
        )
        dialog.updateSketchRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, diameter: self._update_sketch_object(
                dialog.point_object, dialog.point_entity, references, fallback,
                name, show_internal, show_auxiliary, rotation, diameter,
            )
            if dialog.point_object is not None
            and dialog.point_entity is not None
            else None
        )
        dialog.enterSketchRequested.connect(
            lambda: self._queue_sketch_edit(dialog)
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        if initial_reference is not None:
            dialog.add_reference(initial_reference)
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _create_primitive_object(self, kind: EntityKind) -> None:
        if self.document is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        text_key = {
            EntityKind.BOX: "primitive.box",
            EntityKind.SPHERE: "primitive.sphere",
            EntityKind.CYLINDER: "primitive.cylinder",
            EntityKind.CONE: "primitive.cone",
            EntityKind.PYRAMID: "primitive.pyramid",
            EntityKind.WEDGE: "primitive.wedge",
        }[kind]
        dialog = SolidConstraintDialog(
            self._solve_point_constraints,
            kind,
            self,
            suggested_name=self.document.next_container_name(tr(text_key)),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createSolidRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, parameters, operation: self._apply_new_constrained_solid(
                dialog, kind, references, fallback, name, show_internal,
                show_auxiliary, rotation, parameters, operation,
            )
        )
        dialog.updateSolidRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, parameters, operation: self._update_solid_object(
                dialog.point_object, dialog.point_entity, references, fallback,
                name, show_internal, show_auxiliary, rotation, parameters,
                operation,
            )
            if dialog.point_object is not None
            and dialog.point_entity is not None
            else None
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _create_point_object(self) -> None:
        if self.document is None:
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        ):
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = PointConstraintDialog(
            self._solve_point_constraints,
            self,
            suggested_name=self.document.next_container_name(tr("primitive.point")),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary:
                self._apply_new_constrained_point(
                    dialog,
                    references,
                    fallback,
                    name,
                    show_internal,
                    show_auxiliary,
                )
        )
        dialog.updateRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary:
                self._update_point_object(
                    dialog.point_object,
                    dialog.point_entity,
                    references,
                    fallback,
                    name,
                    show_internal,
                    show_auxiliary,
                )
                if dialog.point_object is not None
                and dialog.point_entity is not None
                else None
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _apply_new_constrained_point(
        self,
        dialog: PointConstraintDialog,
        constraint_references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal_entities: bool,
        show_auxiliary_geometry: bool,
    ) -> None:
        created = self._create_constrained_point(
            constraint_references,
            fallback,
            name,
            show_internal_entities,
            show_auxiliary_geometry,
        )
        if created is not None:
            dialog.adopt_created_point(*created)

    def _point_constraint_dialog_finished(self, _result: int) -> None:
        self.point_constraint_dialog = None
        self._point_constraint_cycle_keys = ()
        self._point_constraint_cycle_index = -1
        self._point_constraint_preview = None
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _create_constrained_point(
        self,
        constraint_references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal_entities: bool,
        show_auxiliary_geometry: bool,
    ) -> tuple[ZimaEntity, ZimaEntity] | None:
        if self.document is None:
            return None
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            constraint_references,
            fallback,
        )
        if solution is None:
            return None
        obj = self.document.create_container(
            tr("primitive.point"),
            ContainerType.POINT,
        )
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = self._plane_reference_rotation(
            constraint_references
        )
        obj.show_internal_entities = show_internal_entities
        obj.show_auxiliary_geometry = show_auxiliary_geometry
        origin = next(
            (
                child
                for child in obj.children
                if child.kind == EntityKind.ORIGIN
            ),
            None,
        )
        point = next(
            (
                child
                for child in origin.children
                if child.kind == EntityKind.POINT
            ),
            None,
        ) if origin is not None else None
        if point is None:
            self.document.delete_container(obj.entity_id)
            return None
        point.parameters.update(
            {
                "constraint_refs": json.dumps(
                    constraint_references,
                    ensure_ascii=False,
                ),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "reference_orientation": "true",
            }
        )
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)
        return obj, point

    def _edit_point_object(
        self,
        obj: ZimaEntity,
        point: ZimaEntity,
    ) -> None:
        if self.document is None:
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        ):
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = PointConstraintDialog(
            self._solve_point_constraints,
            self,
            point_object=obj,
            point_entity=point,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updateRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary:
                self._update_point_object(
                    obj,
                    point,
                    references,
                    fallback,
                    name,
                    show_internal,
                    show_auxiliary,
                )
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _show_properties_dialog(
        self,
        dialog: PointConstraintDialog,
    ) -> None:
        def enable_live_preview() -> None:
            if (
                getattr(dialog, "_live_preview_connected", False)
                or not dialog.edit_mode
                or dialog.point_object is None
            ):
                return
            dialog._live_preview_connected = True
            target = dialog.point_object
            baseline = [copy.deepcopy(target)]

            def preview_changes() -> None:
                if dialog.isVisible():
                    dialog._submit()

            def accept_preview_as_baseline() -> None:
                baseline[0] = copy.deepcopy(target)

            def restore_baseline() -> None:
                restored = copy.deepcopy(baseline[0].__dict__)
                target.__dict__.clear()
                target.__dict__.update(restored)
                self._populate_tree()
                self._refresh_object_properties(target)

            dialog.definitionChanged.connect(preview_changes)
            dialog.applied.connect(accept_preview_as_baseline)
            dialog.rejected.connect(restore_baseline)

        dialog.entityAdopted.connect(enable_live_preview)
        enable_live_preview()
        dialog.show()
        position_dialog_top_right_after_show(dialog)

    def _activate_point_reference(self, descriptor: dict[str, Any]) -> None:
        if self.document is None:
            return
        entity_id = str(descriptor.get("entity_id", ""))
        reference = self.document.find_entity(entity_id)
        if reference is None:
            return

        tree_object_id = reference.entity_id
        if reference.tree_exposure == TreeExposure.INTERNAL:
            owner = self.document.find_owning_object(reference.entity_id)
            if owner is not None and not owner.show_internal_entities:
                tree_object_id = owner.entity_id

        self.selected_object_id = reference.entity_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._point_constraint_preview = None
        reference_type = descriptor.get("type")
        active_reference_label = str(
            descriptor.get("label", reference.name)
        )
        if reference_type in ("face", "edge", "vertex"):
            try:
                topology_index = int(
                    descriptor.get(
                        "vertex_index"
                        if reference_type == "vertex"
                        else "topology_key",
                        "0",
                    )
                )
            except (TypeError, ValueError):
                topology_index = 0
            shape_type = {
                "face": TopAbs_FACE,
                "edge": TopAbs_EDGE,
                "vertex": TopAbs_VERTEX,
            }[reference_type]
            active_reference_label = self._topology_reference_label(
                reference,
                str(reference_type),
                topology_index,
            )
            model_shape = self._shape_for_reference_descriptor(
                descriptor,
                reference,
            )
            selected_shape = self._subshape_from_shape(
                model_shape,
                shape_type,
                topology_index,
            ) if model_shape is not None else None
            if reference_type == "face":
                self.selected_face = selected_shape
            elif selected_shape is not None:
                self._point_constraint_preview = (
                    reference.entity_id,
                    selected_shape,
                )
            if reference_type == "face" and self.selected_face is not None:
                self.selected_face_object_id = reference.entity_id

        root = self.tree.invisibleRootItem()
        tree_item = self._find_tree_item(root, tree_object_id)
        self.tree.blockSignals(True)
        if tree_item is not None:
            self.tree.setCurrentItem(tree_item)
        else:
            self.tree.clearSelection()
        self.tree.blockSignals(False)
        self._view_selection_confirmed = True
        self.rebuild_view(fit=False, rebuild_geometry=False)
        self.statusBar().showMessage(
            tr(
                "selection.status.reference",
                name=active_reference_label,
            )
        )

    def _update_point_object(
        self,
        obj: ZimaEntity,
        point: ZimaEntity,
        constraint_references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal_entities: bool,
        show_auxiliary_geometry: bool,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            constraint_references,
            fallback,
        )
        if solution is None:
            return
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = self._plane_reference_rotation(
            constraint_references
        )
        obj.show_internal_entities = show_internal_entities
        obj.show_auxiliary_geometry = show_auxiliary_geometry
        if not point.locked:
            point.name = name
        point.parameters.update(
            {
                "constraint_refs": json.dumps(
                    constraint_references,
                    ensure_ascii=False,
                ),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "reference_orientation": "true",
            }
        )
        self._refresh_object_properties(obj)

    def _solve_point_constraints(
        self,
        references: list[dict[str, Any]],
        fallback: tuple[float, float, float] = (0.0, 0.0, 0.0),
    ) -> tuple[
        tuple[float, float, float] | None,
        int,
        str,
        tuple[bool, bool, bool],
    ]:
        equations: list[list[float]] = []
        for descriptor in references:
            if descriptor.get("position_role") == "orientation_only":
                continue
            if descriptor.get("type") != "entity":
                resolved = self._resolved_shape_reference_equations(descriptor)
                rows = (
                    resolved
                    if resolved is not None
                    else [list(row) for row in descriptor.get("equations", ())]
                )
                if descriptor.get("type") == "face" and rows:
                    rows[0][3] += float(descriptor.get("offset", 0.0))
                equations.extend(rows)
                continue
            reference = (
                self.document.find_entity(str(descriptor.get("entity_id", "")))
                if self.document is not None
                else None
            )
            if reference is None:
                continue
            if reference.kind in (EntityKind.ORIGIN, EntityKind.POINT):
                point = self._reference_origin(reference)
                equations.extend(
                    [
                        [1.0, 0.0, 0.0, point[0]],
                        [0.0, 1.0, 0.0, point[1]],
                        [0.0, 0.0, 1.0, point[2]],
                    ]
                )
            elif reference.kind == EntityKind.PLANE:
                point = self._reference_origin(reference)
                local_normal = {
                    "xy": (0.0, 0.0, 1.0),
                    "yz": (1.0, 0.0, 0.0),
                    "xz": (0.0, 1.0, 0.0),
                }.get(
                    str(reference.parameters.get("plane", "xy")),
                    (0.0, 0.0, 1.0),
                )
                normal = self._reference_direction(reference, local_normal)
                offset = float(descriptor.get("offset", 0.0))
                equations.append(
                    [
                        normal[0],
                        normal[1],
                        normal[2],
                        sum(normal[index] * point[index] for index in range(3))
                        + offset,
                    ]
                )
            elif reference.kind == EntityKind.AXIS:
                point = self._reference_origin(reference)
                local_direction = {
                    "x": (1.0, 0.0, 0.0),
                    "y": (0.0, 1.0, 0.0),
                    "z": (0.0, 0.0, 1.0),
                }.get(
                    str(reference.parameters.get("axis", "z")),
                    (0.0, 0.0, 1.0),
                )
                direction = self._reference_direction(
                    reference,
                    local_direction,
                )
                helper = (
                    (1.0, 0.0, 0.0)
                    if abs(direction[0]) < 0.9
                    else (0.0, 1.0, 0.0)
                )
                first = self._normalized_vector(
                    self._cross_product(direction, helper)
                )
                second = self._normalized_vector(
                    self._cross_product(direction, first)
                )
                for normal in (first, second):
                    equations.append(
                        [
                            normal[0],
                            normal[1],
                            normal[2],
                            sum(
                                normal[index] * point[index]
                                for index in range(3)
                            ),
                        ]
                    )

        matrix = [row[:] for row in equations]
        rank = 0
        pivot_columns: list[int] = []
        tolerance = 1e-9
        for column in range(3):
            pivot = next(
                (
                    row
                    for row in range(rank, len(matrix))
                    if abs(matrix[row][column]) > tolerance
                ),
                None,
            )
            if pivot is None:
                continue
            matrix[rank], matrix[pivot] = matrix[pivot], matrix[rank]
            divisor = matrix[rank][column]
            matrix[rank] = [value / divisor for value in matrix[rank]]
            for row in range(len(matrix)):
                if row == rank:
                    continue
                factor = matrix[row][column]
                matrix[row] = [
                    matrix[row][index] - factor * matrix[rank][index]
                    for index in range(4)
                ]
            pivot_columns.append(column)
            rank += 1
        inconsistent = any(
            all(abs(row[column]) <= tolerance for column in range(3))
            and abs(row[3]) > tolerance
            for row in matrix
        )
        dof = max(0, 3 - rank)
        constrained = tuple(column in pivot_columns for column in range(3))
        if inconsistent:
            return (
                None,
                dof,
                "dialog.point_constraints.conflict",
                constrained,
            )
        for column in range(3):
            if column not in pivot_columns:
                matrix.append(
                    [
                        1.0 if index == column else 0.0
                        for index in range(3)
                    ]
                    + [fallback[column]]
                )
        if dof:
            rank = 0
            pivot_columns = []
            for column in range(3):
                pivot = next(
                    (
                        row
                        for row in range(rank, len(matrix))
                        if abs(matrix[row][column]) > tolerance
                    ),
                    None,
                )
                if pivot is None:
                    continue
                matrix[rank], matrix[pivot] = matrix[pivot], matrix[rank]
                divisor = matrix[rank][column]
                matrix[rank] = [value / divisor for value in matrix[rank]]
                for row in range(len(matrix)):
                    if row == rank:
                        continue
                    factor = matrix[row][column]
                    matrix[row] = [
                        matrix[row][index] - factor * matrix[rank][index]
                        for index in range(4)
                    ]
                pivot_columns.append(column)
                rank += 1
        solution = [0.0, 0.0, 0.0]
        for row, column in enumerate(pivot_columns):
            solution[column] = matrix[row][3]
        return (
            (solution[0], solution[1], solution[2]),
            dof,
            (
                "dialog.point_constraints.absolute_fallback"
                if dof
                else "dialog.point_constraints.solved"
            ),
            constrained,
        )

    def _resolved_shape_reference_equations(
        self,
        descriptor: dict[str, Any],
    ) -> list[list[float]] | None:
        shape_reference_type = descriptor.get("type")
        if (
            shape_reference_type not in ("vertex", "face", "edge")
            or self.document is None
        ):
            return None
        reference = self.document.find_entity(
            str(descriptor.get("entity_id", ""))
        )
        if reference is None:
            return None
        model_shape = self._shape_for_reference_descriptor(
            descriptor,
            reference,
        )
        if model_shape is None:
            return None

        if shape_reference_type in ("face", "edge"):
            try:
                topology_index = int(descriptor.get("topology_key", "0"))
            except (TypeError, ValueError):
                return None
            shape_type = (
                TopAbs_FACE
                if shape_reference_type == "face"
                else TopAbs_EDGE
            )
            subshape = self._subshape_from_shape(
                model_shape,
                shape_type,
                topology_index,
            )
            if subshape is None:
                return None
            if shape_reference_type == "face":
                adaptor = BRepAdaptor_Surface(subshape)
                if adaptor.GetType() != GeomAbs_Plane:
                    return None
                plane = adaptor.Plane()
                location = plane.Location()
                normal = plane.Axis().Direction()
                sign = -1.0 if subshape.Orientation() == TopAbs_REVERSED else 1.0
                return [
                    [
                        sign * normal.X(),
                        sign * normal.Y(),
                        sign * normal.Z(),
                        sign
                        * (
                            normal.X() * location.X()
                            + normal.Y() * location.Y()
                            + normal.Z() * location.Z()
                        ),
                    ]
                ]

            adaptor = BRepAdaptor_Curve(subshape)
            if adaptor.GetType() != GeomAbs_Line:
                return None
            line = adaptor.Line()
            location = line.Location()
            direction = (
                line.Direction().X(),
                line.Direction().Y(),
                line.Direction().Z(),
            )
            helper = (
                (1.0, 0.0, 0.0)
                if abs(direction[0]) < 0.9
                else (0.0, 1.0, 0.0)
            )
            first = self._normalized_vector(
                self._cross_product(direction, helper)
            )
            second = self._normalized_vector(
                self._cross_product(direction, first)
            )
            point = (location.X(), location.Y(), location.Z())
            return [
                [
                    normal[0],
                    normal[1],
                    normal[2],
                    sum(
                        normal[index] * point[index]
                        for index in range(3)
                    ),
                ]
                for normal in (first, second)
            ]

        point = None
        try:
            edge_index = int(descriptor.get("edge_index", "0"))
        except (TypeError, ValueError):
            edge_index = 0
        endpoint = str(descriptor.get("endpoint", ""))
        if edge_index > 0 and endpoint in ("start", "end"):
            edge = self._subshape_from_shape(
                model_shape,
                TopAbs_EDGE,
                edge_index,
            )
            if edge is not None:
                try:
                    adaptor = BRepAdaptor_Curve(edge)
                    parameter = (
                        adaptor.FirstParameter()
                        if endpoint == "start"
                        else adaptor.LastParameter()
                    )
                    point = adaptor.Value(parameter)
                except (AttributeError, RuntimeError):
                    point = None

        if point is None:
            try:
                vertex_index = int(
                    descriptor.get(
                        "vertex_index",
                        descriptor.get("topology_key", "0"),
                    )
                )
            except (TypeError, ValueError):
                vertex_index = 0
            vertex = self._subshape_from_shape(
                model_shape,
                TopAbs_VERTEX,
                vertex_index,
            )
            if vertex is not None:
                try:
                    point = BRep_Tool.Pnt(vertex)
                except (TypeError, RuntimeError):
                    point = None
        if point is None:
            return None
        return [
            [1.0, 0.0, 0.0, point.X()],
            [0.0, 1.0, 0.0, point.Y()],
            [0.0, 0.0, 1.0, point.Z()],
        ]

    def _shape_for_reference_descriptor(
        self,
        descriptor: dict[str, Any],
        reference: ZimaEntity,
    ):
        if (
            descriptor.get("reference_scope") == "history_result"
            or reference.kind == EntityKind.PART
        ):
            raw_source_ids = descriptor.get("history_object_ids", [])
            source_ids = (
                [
                    str(entity_id)
                    for entity_id in raw_source_ids
                    if str(entity_id)
                ]
                if isinstance(raw_source_ids, list)
                else []
            )
            if source_ids:
                model_shape = self.document.build_shape_for_object_ids(
                    source_ids
                )
            else:
                try:
                    boundary = int(
                        descriptor.get(
                            "history_cursor",
                            self._definition_history_boundary(),
                        )
                    )
                except (TypeError, ValueError):
                    boundary = self._definition_history_boundary()
                return self.document.build_shape_at(boundary)
            return model_shape
        return self.document.build_standalone_shape(reference)

    def _reference_origin(
        self,
        reference: ZimaEntity,
    ) -> tuple[float, float, float]:
        owner = (
            self.document.find_owning_object(reference.entity_id)
            if self.document is not None
            else None
        )
        transform = self._world_transform_for_object(owner)
        return transform_point(transform, (0.0, 0.0, 0.0))

    def _reference_direction(
        self,
        reference: ZimaEntity,
        local_direction: tuple[float, float, float],
    ) -> tuple[float, float, float]:
        owner = (
            self.document.find_owning_object(reference.entity_id)
            if self.document is not None
            else None
        )
        transform = self._world_transform_for_object(owner)
        direction = tuple(
            sum(
                transform[row][column] * local_direction[column]
                for column in range(3)
            )
            for row in range(3)
        )
        return self._normalized_vector(direction)

    @staticmethod
    def _cross_product(first, second) -> tuple[float, float, float]:
        return (
            first[1] * second[2] - first[2] * second[1],
            first[2] * second[0] - first[0] * second[2],
            first[0] * second[1] - first[1] * second[0],
        )

    @staticmethod
    def _normalized_vector(vector) -> tuple[float, float, float]:
        length = sum(value * value for value in vector) ** 0.5
        if length <= 1e-12:
            return (0.0, 0.0, 0.0)
        return tuple(value / length for value in vector)

    def _create_axis_object(self) -> None:
        if self.document is None:
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        ):
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = AxisConstraintDialog(
            self._solve_point_constraints,
            self,
            suggested_name=self.document.next_container_name(tr("primitive.axis")),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createAxisRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, direction, length: self._apply_new_constrained_axis(
                dialog,
                references,
                fallback,
                name,
                show_internal,
                show_auxiliary,
                rotation,
                direction,
                length,
            )
        )
        dialog.updateAxisRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, direction, length: self._update_axis_object(
                dialog.point_object,
                dialog.point_entity,
                references,
                fallback,
                name,
                show_internal,
                show_auxiliary,
                rotation,
                direction,
                length,
            )
            if dialog.point_object is not None
            and dialog.point_entity is not None
            else None
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _create_plane_object(self) -> None:
        if self.document is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = PlaneConstraintDialog(
            self._solve_point_constraints,
            self,
            suggested_name=self.document.next_container_name(tr("primitive.plane")),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createPlaneRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, plane, size: self._apply_new_constrained_plane(
                dialog, references, fallback, name, show_internal,
                show_auxiliary, rotation, plane, size,
            )
        )
        dialog.updatePlaneRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, plane, size: self._update_plane_object(
                dialog.point_object, dialog.point_entity, references, fallback,
                name, show_internal, show_auxiliary, rotation, plane, size,
            )
            if dialog.point_object is not None
            and dialog.point_entity is not None
            else None
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _apply_new_constrained_axis(
        self,
        dialog: AxisConstraintDialog,
        references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal: bool,
        show_auxiliary: bool,
        rotation: tuple[float, float, float],
        direction: str,
        length: float,
    ) -> None:
        created = self._create_constrained_axis(
            references,
            fallback,
            name,
            show_internal,
            show_auxiliary,
            rotation,
            direction,
            length,
        )
        if created is not None:
            dialog.adopt_created_entity(*created)

    def _create_constrained_axis(
        self,
        references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal: bool,
        show_auxiliary: bool,
        rotation: tuple[float, float, float],
        direction: str,
        length: float,
    ) -> tuple[ZimaEntity, ZimaEntity] | None:
        if self.document is None:
            return None
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references,
            fallback,
        )
        if solution is None:
            return None
        obj = self.document.create_container(
            tr("primitive.axis"),
            ContainerType.AXIS,
        )
        axis = self.document.create_datum_axis(obj.entity_id)
        if axis is None:
            self.document.delete_container(obj.entity_id)
            return None
        self._set_axis_definition(
            obj,
            axis,
            references,
            fallback,
            name,
            show_internal,
            show_auxiliary,
            rotation,
            direction,
            length,
            solution,
        )
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)
        return obj, axis

    def _update_axis_object(
        self,
        obj: ZimaEntity,
        axis: ZimaEntity,
        references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal: bool,
        show_auxiliary: bool,
        rotation: tuple[float, float, float],
        direction: str,
        length: float,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references,
            fallback,
        )
        if solution is None:
            return
        self._set_axis_definition(
            obj,
            axis,
            references,
            fallback,
            name,
            show_internal,
            show_auxiliary,
            rotation,
            direction,
            length,
            solution,
        )
        self._refresh_object_properties(obj)

    def _set_axis_definition(
        self,
        obj: ZimaEntity,
        axis: ZimaEntity,
        references: list[dict[str, Any]],
        fallback: tuple[float, float, float],
        name: str,
        show_internal: bool,
        show_auxiliary: bool,
        rotation: tuple[float, float, float],
        direction: str,
        length: float,
        solution: tuple[float, float, float],
    ) -> None:
        base_rotation = self._plane_reference_rotation(references)
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = tuple(
            base_rotation[index] + rotation[index]
            for index in range(3)
        )
        obj.show_internal_entities = show_internal
        obj.show_auxiliary_geometry = show_auxiliary
        axis.name = name
        axis.tree_exposure = TreeExposure.INTERNAL
        axis.parameters.update(
            {
                "display_style": "centerline",
                "axis": direction,
                "length": f"{length:.12g}",
                "unit": "mm",
                "constraint_refs": json.dumps(
                    references,
                    ensure_ascii=False,
                ),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "reference_orientation": "true",
                "rotation_offset_x": f"{rotation[0]:.12g}",
                "rotation_offset_y": f"{rotation[1]:.12g}",
                "rotation_offset_z": f"{rotation[2]:.12g}",
            }
        )

    def _apply_new_constrained_plane(
        self,
        dialog: PlaneConstraintDialog,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        plane,
        size,
    ) -> None:
        if self.document is None:
            return
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        obj = self.document.create_container(
            tr("primitive.plane"),
            ContainerType.PLANE,
        )
        entity = self.document.create_datum_plane(obj.entity_id)
        if entity is None:
            self.document.delete_container(obj.entity_id)
            return
        self._set_plane_definition(
            obj, entity, references, fallback, name, show_internal,
            show_auxiliary, rotation, plane, size, solution,
        )
        dialog.adopt_created_entity(obj, entity)
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)

    def _update_plane_object(
        self,
        obj,
        entity,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        plane,
        size,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        self._set_plane_definition(
            obj, entity, references, fallback, name, show_internal,
            show_auxiliary, rotation, plane, size, solution,
        )
        self._refresh_object_properties(obj)

    def _set_plane_definition(
        self,
        obj,
        entity,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        plane,
        size,
        solution,
    ) -> None:
        base_rotation = self._plane_reference_rotation(references)
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = tuple(
            base_rotation[index] + rotation[index]
            for index in range(3)
        )
        obj.show_internal_entities = show_internal
        obj.show_auxiliary_geometry = show_auxiliary
        entity.name = name
        entity.tree_exposure = TreeExposure.INTERNAL
        entity.parameters.update(
            {
                "display_style": "datum",
                "plane": "xy",
                "size": f"{size:.12g}",
                "unit": "mm",
                "constraint_refs": json.dumps(references, ensure_ascii=False),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "rotation_offset_x": f"{rotation[0]:.12g}",
                "rotation_offset_y": f"{rotation[1]:.12g}",
                "rotation_offset_z": f"{rotation[2]:.12g}",
            }
        )

    def _plane_reference_rotation(
        self,
        references: list[dict[str, Any]],
    ) -> tuple[float, float, float]:
        normal = None
        has_explicit_roles = any(
            "orientation_role" in reference
            for reference in references
        )
        ordered_references = sorted(
            enumerate(references),
            key=lambda item: (
                item[1].get("orientation_role")
                not in ("normal", "opposite_normal")
                and item[1].get("plane_role") != "orientation",
                item[0],
            ),
        )
        for _index, descriptor in ordered_references:
            if (
                has_explicit_roles
                and descriptor.get("orientation_role")
                not in ("normal", "opposite_normal")
            ):
                continue
            normal = self._orientation_reference_vector(
                descriptor,
                allow_frame_fallback=True,
            )
            if normal != (0.0, 0.0, 0.0):
                if (
                    descriptor.get("orientation_role")
                    == "opposite_normal"
                ):
                    normal = tuple(-value for value in normal)
                break
        if normal is None:
            return (0.0, 0.0, 0.0)
        nx, ny, nz = self._normalized_vector(normal)
        # The datum plane is locally XY, therefore its normal is local +Z.
        # With our Rz * Ry * Rx transform and Rz fixed to zero, transformed
        # +Z is (sin(ry) cos(rx), -sin(rx), cos(ry) cos(rx)).
        # Solve those equations directly; the previous atan2-based RX formula
        # failed for vertical planes whose normal has nz == 0 (notably the
        # sloped face of a wedge).
        rx = math.degrees(math.asin(max(-1.0, min(1.0, -ny))))
        ry = (
            0.0
            if math.hypot(nx, nz) <= 1e-12
            else math.degrees(math.atan2(nx, nz))
        )
        direction_descriptor = next(
            (
                descriptor
                for descriptor in references
                if descriptor.get("orientation_role")
                in ("up", "down", "right", "left")
            ),
            None,
        )
        if direction_descriptor is None:
            return (rx, ry, 0.0)
        direction = self._orientation_reference_vector(
            direction_descriptor,
            allow_frame_fallback=False,
        )
        dot = sum(direction[index] * (nx, ny, nz)[index] for index in range(3))
        projected = self._normalized_vector(
            tuple(
                direction[index] - dot * (nx, ny, nz)[index]
                for index in range(3)
            )
        )
        if projected == (0.0, 0.0, 0.0):
            return (rx, ry, 0.0)

        role = str(direction_descriptor.get("orientation_role"))
        if role in ("down", "left"):
            projected = tuple(-value for value in projected)
        z_axis = (nx, ny, nz)
        if role in ("up", "down"):
            y_axis = projected
            x_axis = self._normalized_vector(
                self._cross_product(y_axis, z_axis)
            )
        else:
            x_axis = projected
            y_axis = self._normalized_vector(
                self._cross_product(z_axis, x_axis)
            )

        # Columns are the transformed local X/Y/Z basis. Extract Euler angles
        # for the application's Rz * Ry * Rx convention.
        matrix = (
            (x_axis[0], y_axis[0], z_axis[0]),
            (x_axis[1], y_axis[1], z_axis[1]),
            (x_axis[2], y_axis[2], z_axis[2]),
        )
        ry_radians = math.asin(max(-1.0, min(1.0, -matrix[2][0])))
        if abs(math.cos(ry_radians)) > 1e-10:
            rx_radians = math.atan2(matrix[2][1], matrix[2][2])
            rz_radians = math.atan2(matrix[1][0], matrix[0][0])
        else:
            rx_radians = math.atan2(-matrix[0][1], matrix[1][1])
            rz_radians = 0.0
        return tuple(
            math.degrees(value)
            for value in (rx_radians, ry_radians, rz_radians)
        )

    def _orientation_reference_vector(
        self,
        descriptor: dict[str, Any],
        *,
        allow_frame_fallback: bool,
    ) -> tuple[float, float, float]:
        reference_type = descriptor.get("type")
        if reference_type == "face":
            rows = self._resolved_shape_reference_equations(descriptor)
            if rows is None:
                rows = [
                    list(row)
                    for row in descriptor.get("equations", ())
                    if isinstance(row, (list, tuple)) and len(row) >= 3
                ]
            return (
                self._normalized_vector(rows[0][:3])
                if rows
                else (0.0, 0.0, 0.0)
            )
        if (
            reference_type == "edge"
            and not allow_frame_fallback
            and self.document is not None
        ):
            reference = self.document.find_entity(
                str(descriptor.get("entity_id", ""))
            )
            if reference is None:
                return (0.0, 0.0, 0.0)
            shape = self._shape_for_reference_descriptor(
                descriptor,
                reference,
            )
            try:
                topology_index = int(descriptor.get("topology_key", "0"))
            except (TypeError, ValueError):
                topology_index = 0
            edge = (
                self._subshape_from_shape(
                    shape,
                    TopAbs_EDGE,
                    topology_index,
                )
                if shape is not None and topology_index > 0
                else None
            )
            if edge is None:
                return (0.0, 0.0, 0.0)
            try:
                adaptor = BRepAdaptor_Curve(edge)
                if adaptor.GetType() != GeomAbs_Line:
                    return (0.0, 0.0, 0.0)
                direction = adaptor.Line().Direction()
                return self._normalized_vector(
                    (direction.X(), direction.Y(), direction.Z())
                )
            except (AttributeError, RuntimeError):
                return (0.0, 0.0, 0.0)
        if reference_type != "entity" or self.document is None:
            return (0.0, 0.0, 0.0)
        reference = self.document.find_entity(
            str(descriptor.get("entity_id", ""))
        )
        if reference is None:
            return (0.0, 0.0, 0.0)
        if reference.kind == EntityKind.PLANE:
            local_direction = {
                "xy": (0.0, 0.0, 1.0),
                "yz": (1.0, 0.0, 0.0),
                "xz": (0.0, 1.0, 0.0),
            }.get(str(reference.parameters.get("plane", "xy")))
        elif reference.kind == EntityKind.AXIS and not allow_frame_fallback:
            local_direction = {
                "x": (1.0, 0.0, 0.0),
                "y": (0.0, 1.0, 0.0),
                "z": (0.0, 0.0, 1.0),
            }.get(str(reference.parameters.get("axis", "z")))
        elif allow_frame_fallback and reference.kind in (
            EntityKind.POINT,
            EntityKind.ORIGIN,
        ):
            local_direction = (0.0, 0.0, 1.0)
        else:
            local_direction = None
        return (
            self._reference_direction(reference, local_direction)
            if local_direction is not None
            else (0.0, 0.0, 0.0)
        )

    def _apply_new_constrained_solid(
        self,
        dialog,
        kind,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        parameters,
        operation,
    ) -> None:
        if self.document is None:
            return
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        obj = self.document.create_container(
            tr(f"primitive.{kind.value}"),
            ContainerType(kind.value.upper()),
        )
        solid = self.document.create_primitive(obj.entity_id, kind)
        if solid is None:
            self.document.delete_container(obj.entity_id)
            return
        self._set_solid_definition(
            obj, solid, references, fallback, name, show_internal,
            show_auxiliary, rotation, parameters, solution, operation,
        )
        dialog.adopt_created_entity(obj, solid)
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)

    def _apply_new_experimental_container(
        self,
        dialog,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        container_type,
    ) -> None:
        if self.document is None:
            return
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        obj = self.document.create_container(
            name,
            ContainerType(container_type),
        )
        self._set_experimental_container_definition(
            obj, references, fallback, name, show_internal,
            show_auxiliary, rotation, solution, container_type,
        )
        dialog.adopt_created_entity(obj, obj)
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)

    def _update_experimental_container(
        self,
        obj,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        container_type,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        self._set_experimental_container_definition(
            obj, references, fallback, name, show_internal,
            show_auxiliary, rotation, solution, container_type,
        )
        self._refresh_object_properties(obj)

    def _set_experimental_container_definition(
        self,
        obj,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        solution,
        container_type,
    ) -> None:
        base_rotation = self._plane_reference_rotation(references)
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = tuple(
            base_rotation[index] + rotation[index]
            for index in range(3)
        )
        obj.show_internal_entities = show_internal
        obj.show_auxiliary_geometry = show_auxiliary
        obj.parameters.update(
            {
                "experimental_container": "true",
                "container_type": ContainerType(container_type).value,
                "constraint_refs": json.dumps(references, ensure_ascii=False),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "reference_orientation": "true",
                "rotation_offset_x": f"{rotation[0]:.12g}",
                "rotation_offset_y": f"{rotation[1]:.12g}",
                "rotation_offset_z": f"{rotation[2]:.12g}",
            }
        )

    def _update_solid_object(
        self,
        obj,
        solid,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        parameters,
        operation,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        self._set_solid_definition(
            obj, solid, references, fallback, name, show_internal,
            show_auxiliary, rotation, parameters, solution, operation,
        )
        self._refresh_object_properties(obj)

    def _set_solid_definition(
        self,
        obj,
        solid,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        parameters,
        solution,
        operation,
    ) -> None:
        base_rotation = self._plane_reference_rotation(references)
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = tuple(
            base_rotation[index] + rotation[index]
            for index in range(3)
        )
        obj.show_internal_entities = show_internal
        obj.show_auxiliary_geometry = show_auxiliary
        solid.name = name
        solid.combine_mode = CombineMode(operation)
        solid.tree_exposure = TreeExposure.INTERNAL
        solid.parameters.update(
            {
                **{
                    key: f"{value:.12g}"
                    for key, value in parameters.items()
                },
                "unit": "mm",
                "constraint_refs": json.dumps(references, ensure_ascii=False),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "reference_orientation": "true",
                "rotation_offset_x": f"{rotation[0]:.12g}",
                "rotation_offset_y": f"{rotation[1]:.12g}",
                "rotation_offset_z": f"{rotation[2]:.12g}",
            }
        )

    def _apply_new_constrained_sketch(
        self,
        dialog,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        diameter,
    ) -> None:
        if self.document is None:
            return
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        obj = self.document.create_container(
            tr("container.type.sketch"),
            ContainerType.SKETCH,
        )
        sketch = self.document.create_sketch(
            obj.entity_id,
            plane="xy",
            role=SketchRole.PROFILE,
            name_prefix=tr("container.type.sketch"),
        )
        if sketch is None:
            self.document.delete_container(obj.entity_id)
            return
        self._set_sketch_definition(
            obj, sketch, references, fallback, name, show_internal,
            show_auxiliary, rotation, diameter, solution,
        )
        dialog.adopt_created_entity(obj, sketch)
        self._populate_tree()
        self._select_tree_object_without_reference_event(obj.entity_id)
        self.rebuild_view(fit=False)

    def _update_sketch_object(
        self,
        obj,
        sketch,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        diameter,
    ) -> None:
        solution, _dof, _status, _constrained = self._solve_point_constraints(
            references, fallback
        )
        if solution is None:
            return
        self._set_sketch_definition(
            obj, sketch, references, fallback, name, show_internal,
            show_auxiliary, rotation, diameter, solution,
        )
        self._refresh_object_properties(obj)

    def _set_sketch_definition(
        self,
        obj,
        sketch,
        references,
        fallback,
        name,
        show_internal,
        show_auxiliary,
        rotation,
        diameter,
        solution,
    ) -> None:
        base_rotation = self._plane_reference_rotation(references)
        obj.name = name
        obj.coordinate_system.origin = solution
        obj.coordinate_system.rotation = tuple(
            base_rotation[index] + rotation[index]
            for index in range(3)
        )
        obj.show_internal_entities = show_internal
        obj.show_auxiliary_geometry = show_auxiliary
        sketch.name = name
        sketch.tree_exposure = TreeExposure.INTERNAL
        sketch.parameters.update(
            {
                "plane": "xy",
                "profile": str(
                    sketch.parameters.get("profile", "entities")
                ),
                "sketch_entities": str(
                    sketch.parameters.get("sketch_entities", "[]")
                ),
                "unit": "mm",
                "role": SketchRole.PROFILE.value,
                "constraint_refs": json.dumps(references, ensure_ascii=False),
                "constraint_type": "linear_entities",
                "fallback_x": f"{fallback[0]:.12g}",
                "fallback_y": f"{fallback[1]:.12g}",
                "fallback_z": f"{fallback[2]:.12g}",
                "rotation_offset_x": f"{rotation[0]:.12g}",
                "rotation_offset_y": f"{rotation[1]:.12g}",
                "rotation_offset_z": f"{rotation[2]:.12g}",
            }
        )

    def _create_menu_bar(self) -> None:
        file_menu = self.menuBar().addMenu(tr("menu.file"))

        self.new_document_action = file_menu.addAction(tr("menu.file.new"))
        self.new_document_action.setIcon(resource_icon("new"))
        self.new_document_action.triggered.connect(self.new_document)

        self.open_document_action = file_menu.addAction(tr("menu.file.open"))
        self.open_document_action.setIcon(resource_icon("open"))
        self.open_document_action.triggered.connect(self.open_document)

        close_action = file_menu.addAction(tr("menu.file.close"))
        close_action.triggered.connect(self.close_document)

        self.save_document_action = file_menu.addAction(tr("menu.file.save"))
        self.save_document_action.setIcon(resource_icon("save"))
        self.save_document_action.setShortcuts(
            [
                QKeySequence.StandardKey.Save,
                QKeySequence("F1"),
            ]
        )
        self.save_document_action.setShortcutContext(
            Qt.ShortcutContext.ApplicationShortcut
        )
        self.save_document_action.triggered.connect(self.save_document)

        save_as_action = file_menu.addAction(tr("menu.file.save_as"))
        save_as_action.setShortcut(QKeySequence.StandardKey.SaveAs)
        save_as_action.setShortcutContext(
            Qt.ShortcutContext.ApplicationShortcut
        )
        save_as_action.triggered.connect(self.save_document_as)

        self.delete_file_menu = file_menu.addMenu(tr("menu.file.delete"))
        self.delete_old_versions_action = self.delete_file_menu.addAction(
            tr("menu.file.delete.old_versions")
        )
        self.delete_old_versions_action.setIcon(resource_icon("delete"))
        self.delete_old_versions_action.triggered.connect(
            self.delete_old_file_versions
        )
        self.delete_all_versions_action = self.delete_file_menu.addAction(
            tr("menu.file.delete.all_versions")
        )
        self.delete_all_versions_action.triggered.connect(
            self.delete_all_file_versions
        )
        self.delete_file_menu.aboutToShow.connect(
            self._refresh_delete_file_actions
        )
        self._refresh_delete_file_actions()

        file_menu.addSeparator()

        set_working_directory_action = file_menu.addAction(
            tr("menu.file.working_directory")
        )
        set_working_directory_action.triggered.connect(self.set_working_directory)

        self.edit_menu = self.menuBar().addMenu(tr("menu.edit"))
        self.edit_menu.addAction(self.regenerate_action)

        view_menu = self.menuBar().addMenu(tr("menu.view"))
        view_menu.addAction(self.reset_view_action)
        standard_views_menu = view_menu.addMenu(tr("toolbar.standard_views"))
        for text_key, view_name in (
            ("toolbar.view.default", "default"),
            ("toolbar.view.front", "front"),
            ("toolbar.view.back", "back"),
            ("toolbar.view.left", "left"),
            ("toolbar.view.right", "right"),
            ("toolbar.view.top", "top"),
            ("toolbar.view.bottom", "bottom"),
        ):
            action = standard_views_menu.addAction(tr(text_key))
            action.triggered.connect(
                lambda _checked=False, selected_view=view_name:
                self._set_standard_view(selected_view)
            )
        view_menu.addSeparator()
        view_menu.addAction(self.view_selection_action)
        view_menu.addSeparator()
        view_menu.addAction(self.wire_action)
        view_menu.addAction(self.edges_action)
        view_menu.addAction(self.shaded_action)
        view_menu.addSeparator()
        view_menu.addAction(self.show_origins_action)
        view_menu.addAction(self.show_points_action)
        view_menu.addAction(self.show_axes_action)
        view_menu.addAction(self.show_planes_action)

        self.applications_menu = self.menuBar().addMenu(tr("menu.applications"))
        self.application_action_group = QActionGroup(self)
        self.application_action_group.setExclusive(True)
        self.application_actions: dict[ApplicationMode, Any] = {}
        for mode in ApplicationMode:
            action = self.applications_menu.addAction(
                tr(f"application.{mode.value}")
            )
            action.setCheckable(True)
            action.setData(mode.value)
            action.triggered.connect(
                lambda _checked=False, selected_mode=mode: self.set_active_application(
                    selected_mode
                )
            )
            self.application_action_group.addAction(action)
            self.application_actions[mode] = action
        self.application_actions[self.active_application].setChecked(True)

        tools_menu = self.menuBar().addMenu(tr("menu.tools"))
        self.material_action = tools_menu.addAction(tr("menu.tools.material"))
        self.material_action.triggered.connect(self.show_material_dialog)

        self.parameters_action = tools_menu.addAction(tr("menu.tools.parameters"))
        self.parameters_action.triggered.connect(self.show_user_parameters_dialog)

        tools_menu.addSeparator()

        self.file_settings_action = tools_menu.addAction(
            tr("menu.tools.file_settings")
        )
        self.file_settings_action.triggered.connect(self.show_file_settings_dialog)

        global_settings_action = tools_menu.addAction(
            tr("menu.tools.global_settings")
        )
        global_settings_action.setIcon(resource_icon("settings"))
        global_settings_action.triggered.connect(self.show_options_dialog)

        self.window_menu = self.menuBar().addMenu(tr("menu.window"))
        self._refresh_window_menu()

        help_menu = self.menuBar().addMenu(tr("menu.help"))
        about_action = help_menu.addAction(tr("menu.help.about"))
        about_action.triggered.connect(self.show_about_dialog)

        self.main_toolbar = QToolBar(self)
        self.main_toolbar.setObjectName("mainToolbar")
        self.main_toolbar.setMovable(False)
        self.main_toolbar.setIconSize(QSize(24, 24))
        self.main_toolbar.addAction(self.new_document_action)
        self.main_toolbar.addAction(self.open_document_action)
        self.main_toolbar.addAction(self.save_document_action)
        self.main_toolbar.addSeparator()
        self.main_toolbar.addAction(global_settings_action)

        toolbar_spacer = QWidget()
        toolbar_spacer.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Preferred,
        )
        self.main_toolbar.addWidget(toolbar_spacer)

        logo_widget = QWidget()
        logo_layout = QHBoxLayout(logo_widget)
        logo_layout.setContentsMargins(8, 2, 12, 2)
        logo_text = QLabel(
            '<span style="color:#80AA1A">ZIMA</span>-CAD'
        )
        logo_font = logo_text.font()
        logo_font.setBold(True)
        logo_font.setPointSizeF(max(11.0, logo_font.pointSizeF()))
        logo_text.setFont(logo_font)
        logo_layout.addWidget(logo_text)
        self.main_toolbar.addWidget(logo_widget)
        self.addToolBar(
            Qt.ToolBarArea.TopToolBarArea,
            self.main_toolbar,
        )
        self._restore_window_layout()

    @staticmethod
    def _window_settings() -> QSettings:
        return QSettings("ZIMA-Engineering", "ZIMA-CAD")

    def _restore_window_layout(self) -> None:
        settings = self._window_settings()
        geometry = settings.value("main_window/geometry")
        if isinstance(geometry, QByteArray) and not geometry.isEmpty():
            self.restoreGeometry(geometry)
        state = settings.value("main_window/state")
        if isinstance(state, QByteArray) and not state.isEmpty():
            self.restoreState(state)
        QTimer.singleShot(0, self._ensure_window_on_available_screen)

    def _ensure_window_on_available_screen(self) -> None:
        frame = self.frameGeometry()
        if any(
            frame.intersected(screen.availableGeometry()).width() >= 100
            and frame.intersected(screen.availableGeometry()).height() >= 60
            for screen in QApplication.screens()
        ):
            return
        screen = QApplication.primaryScreen()
        if screen is None:
            return
        available = screen.availableGeometry()
        if not self.isMaximized():
            self.resize(
                min(self.width(), available.width()),
                min(self.height(), available.height()),
            )
        frame = self.frameGeometry()
        frame.moveCenter(available.center())
        self.move(frame.topLeft())

    def closeEvent(self, event) -> None:
        settings = self._window_settings()
        settings.setValue("main_window/geometry", self.saveGeometry())
        settings.setValue("main_window/state", self.saveState())
        settings.sync()
        if self in self.workspace.windows:
            self.workspace.windows.remove(self)
        super().closeEvent(event)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if self.document is not None:
            self._ensure_viewer_initialized()
            self.rebuild_view()

    def _populate_tree(self) -> None:
        signals_were_blocked = self.tree.blockSignals(True)
        try:
            self.tree.clear()
            self._update_document_area_visibility()
            if self.document is None:
                self.tree.setHeaderLabels(["PART"])
                return
            if self._sketch_edit_entity_id is not None:
                sketch = self.document.find_entity(
                    self._sketch_edit_entity_id
                )
                if sketch is not None and sketch.kind == EntityKind.SKETCH:
                    self._populate_sketch_tree(sketch)
                    return

            self.tree.setHeaderLabels(["PART"])

            origins = [
                obj for obj in self.document.root.children
                if obj.kind == EntityKind.ORIGIN
            ]
            for obj in origins:
                item = self._create_tree_item(obj)
                if item is not None:
                    self.tree.addTopLevelItem(item)

            history = self.document.history_objects()
            cursor = self._definition_history_boundary()
            for index, obj in enumerate(history):
                if index == cursor:
                    self.tree.addTopLevelItem(self._create_rollback_item())
                item = self._create_tree_item(obj)
                if item is not None:
                    item.setData(
                        0,
                        HistoryTreeWidget.HISTORY_OBJECT_ROLE,
                        True,
                    )
                    self.tree.addTopLevelItem(item)
            if cursor == len(history):
                self.tree.addTopLevelItem(self._create_rollback_item())

            self.tree.collapseAll()
            self.tree.resizeColumnToContents(0)
        finally:
            self.tree.blockSignals(signals_were_blocked)

    def _populate_sketch_tree(self, sketch: ZimaEntity) -> None:
        self.tree.setHeaderLabels([f"SKETCHER — {sketch.name}"])
        owner = (
            self.document.find_owning_object(sketch.entity_id)
            if self.document is not None
            else None
        )
        origin_entity = next(
            (
                child
                for child in owner.children
                if child.kind == EntityKind.ORIGIN
            ),
            None,
        ) if owner is not None else None
        if origin_entity is not None:
            origin_item = QTreeWidgetItem(
                [f"{tr('tree.origin.container')} — {sketch.name}"]
            )
            origin_item.setIcon(0, resource_icon("origin"))
            origin_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            reference_items = (
                (tr("tree.origin.local"), "point", 1, "point"),
                ("X", "edge", 1, "axis"),
                ("Y", "edge", 2, "axis"),
                ("Z", "edge", 3, "axis"),
                ("XY", "plane", 1, "plane"),
                ("YZ", "plane", 2, "plane"),
                ("XZ", "plane", 3, "plane"),
            )
            selected_reference_item = None
            for label, kind, index, icon_name in reference_items:
                child_item = QTreeWidgetItem([label])
                reference = (kind, origin_entity.entity_id, index)
                child_item.setData(
                    0,
                    HistoryTreeWidget.SKETCH_REFERENCE_ROLE,
                    reference,
                )
                child_item.setIcon(0, resource_icon(icon_name))
                origin_item.addChild(child_item)
                if reference == self._sketch_selected_reference:
                    selected_reference_item = child_item
            self.tree.addTopLevelItem(origin_item)
            origin_item.setExpanded(True)
            if selected_reference_item is not None:
                self.tree.setCurrentItem(selected_reference_item)
        external_references = self._stored_sketch_external_references(sketch)
        if external_references:
            resolved_by_id = {
                str(reference.get("id", "")): reference
                for reference in self._resolved_sketch_external_references(
                    sketch
                )
            }
            references_item = QTreeWidgetItem(
                [tr("tree.sketch.references")]
            )
            references_item.setIcon(
                0,
                resource_icon("sketch-reference"),
            )
            references_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            selected_external_item = None
            for reference in external_references:
                reference_id = str(reference.get("id", ""))
                source = (
                    self.document.find_entity(
                        str(reference.get("owner_id", ""))
                    )
                    if self.document is not None
                    else None
                )
                source_name = (
                    source.name
                    if source is not None
                    else tr("tree.sketch.missing_reference")
                )
                source_kind = str(
                    reference.get("source_kind", "reference")
                )
                source_kind_label = tr(
                    f"sketch.reference.kind.{source_kind}"
                )
                try:
                    element_index = int(
                        reference.get("element_index", 0)
                    )
                except (TypeError, ValueError):
                    element_index = 0
                broken = bool(
                    resolved_by_id.get(reference_id, {}).get("broken")
                )
                label = (
                    f"{source_name} · {source_kind_label} {element_index}"
                    + (
                        f" — {tr('tree.sketch.reference_broken')}"
                        if broken
                        else ""
                    )
                )
                child_item = QTreeWidgetItem([label])
                child_item.setData(
                    0,
                    HistoryTreeWidget.SKETCH_EXTERNAL_REFERENCE_ROLE,
                    reference_id,
                )
                child_item.setIcon(
                    0,
                    resource_icon("sketch-reference"),
                )
                references_item.addChild(child_item)
                if (
                    reference_id
                    == self._sketch_selected_external_reference_id
                ):
                    selected_external_item = child_item
            self.tree.addTopLevelItem(references_item)
            references_item.setExpanded(True)
            if selected_external_item is not None:
                self.tree.setCurrentItem(selected_external_item)
        sketch_entities = self._stored_sketch_entities(sketch)
        point_labels = {
            str(entity.get("id", "")): (
                f"{tr('sketch.tool.point')}{point_index:03d}"
            )
            for point_index, entity in enumerate(
                (
                    entity
                    for entity in sketch_entities
                    if entity.get("type") == "point"
                    and str(entity.get("id", ""))
                ),
                start=1,
            )
        }
        type_counts: dict[str, int] = {}
        selected_item = None
        for entity in sketch_entities:
            entity_id = str(entity.get("id", ""))
            entity_type = str(entity.get("type", ""))
            if not entity_id or not entity_type:
                continue
            type_counts[entity_type] = type_counts.get(entity_type, 0) + 1
            label_key = {
                "point": "sketch.tool.point",
                "segment": "sketch.tool.segment",
                "construction": "sketch.tool.construction",
                "arc": "sketch.tool.arc",
                "spline": "sketch.tool.spline",
            }.get(entity_type)
            label = (
                tr(label_key)
                if label_key is not None
                else entity_type.replace("_", " ").title()
            )
            item = QTreeWidgetItem(
                [f"{label}{type_counts[entity_type]:03d}"]
            )
            item.setData(
                0,
                HistoryTreeWidget.SKETCH_ENTITY_ROLE,
                entity_id,
            )
            icon_name = "point" if entity_type == "point" else "sketch"
            item.setIcon(0, resource_icon(icon_name))
            if entity_type == "point":
                constraints = entity.get("constraints", ())
                if isinstance(constraints, list):
                    for constraint_index, constraint in enumerate(
                        constraints
                    ):
                        if not isinstance(constraint, dict):
                            continue
                        constraint_type = str(
                            constraint.get("type", "")
                        )
                        reference_id = str(
                            constraint.get("reference_id", "")
                        )
                        if constraint_type == "coincident":
                            target_point_id = str(
                                constraint.get("point_id", "")
                            )
                            constraint_label = tr(
                                "sketch.constraint.coincident_with",
                                point=point_labels.get(
                                    target_point_id,
                                    target_point_id,
                                ),
                            )
                        elif constraint_type != "point_on_reference":
                            continue
                        elif reference_id == "sketch_origin":
                            constraint_label = tr(
                                "sketch.constraint.point_at_origin"
                            )
                        else:
                            constraint_label = tr(
                                "sketch.constraint.point_on_reference",
                                reference=self._sketch_reference_display_name(
                                    sketch,
                                    reference_id,
                                ),
                            )
                        constraint_item = QTreeWidgetItem(
                            [constraint_label]
                        )
                        constraint_item.setIcon(
                            0,
                            resource_icon("sketch-constraints"),
                        )
                        constraint_item.setData(
                            0,
                            HistoryTreeWidget.SKETCH_ENTITY_ROLE,
                            entity_id,
                        )
                        constraint_item.setData(
                            0,
                            HistoryTreeWidget.SKETCH_CONSTRAINT_ROLE,
                            (entity_id, constraint_index),
                        )
                        constraint_item.setFlags(
                            Qt.ItemFlag.ItemIsEnabled
                            | Qt.ItemFlag.ItemIsSelectable
                        )
                        item.addChild(constraint_item)
            else:
                point_ids = entity.get("point_ids", ())
                if (
                    entity_type in ("segment", "construction")
                    and isinstance(point_ids, list)
                ):
                    endpoint_labels = (
                        "tree.sketch.start_point",
                        "tree.sketch.end_point",
                    )
                    for endpoint_index, point_id in enumerate(
                        map(str, point_ids[:2])
                    ):
                        endpoint_item = QTreeWidgetItem(
                            [
                                tr(
                                    endpoint_labels[endpoint_index],
                                    point=point_labels.get(
                                        point_id,
                                        tr(
                                            "tree.sketch.missing_point"
                                        ),
                                    ),
                                )
                            ]
                        )
                        endpoint_item.setIcon(
                            0,
                            resource_icon("point"),
                        )
                        endpoint_item.setData(
                            0,
                            HistoryTreeWidget.SKETCH_POINT_LINK_ROLE,
                            point_id,
                        )
                        endpoint_item.setFlags(
                            Qt.ItemFlag.ItemIsEnabled
                            | Qt.ItemFlag.ItemIsSelectable
                        )
                        item.addChild(endpoint_item)
                constraints = entity.get("constraints", ())
                if isinstance(constraints, list):
                    for constraint_index, constraint in enumerate(
                        constraints
                    ):
                        if not isinstance(constraint, dict):
                            continue
                        constraint_type = str(
                            constraint.get("type", "")
                        )
                        label_key = {
                            "horizontal":
                            "sketch.constraint.horizontal",
                            "vertical":
                            "sketch.constraint.vertical",
                        }.get(constraint_type)
                        if label_key is None:
                            continue
                        constraint_item = QTreeWidgetItem(
                            [tr(label_key)]
                        )
                        constraint_item.setIcon(
                            0,
                            resource_icon("sketch-constraints"),
                        )
                        constraint_item.setData(
                            0,
                            HistoryTreeWidget.SKETCH_GEOMETRY_CONSTRAINT_ROLE,
                            (entity_id, constraint_index),
                        )
                        constraint_item.setFlags(
                            Qt.ItemFlag.ItemIsEnabled
                            | Qt.ItemFlag.ItemIsSelectable
                        )
                        item.addChild(constraint_item)
            self.tree.addTopLevelItem(item)
            if entity_id == self._sketch_selected_entity_id:
                selected_item = item
        if selected_item is not None:
            self.tree.setCurrentItem(selected_item)
        self.tree.resizeColumnToContents(0)

    def _create_rollback_item(self) -> QTreeWidgetItem:
        suppressed = self.document.body_is_suppressed()
        item = QTreeWidgetItem(
            [tr("tree.insert_here")]
        )
        item.setData(0, HistoryTreeWidget.ROLLBACK_ROLE, True)
        item.setFlags(
            Qt.ItemFlag.ItemIsEnabled
            | Qt.ItemFlag.ItemIsSelectable
        )
        item.setToolTip(0, tr("tree.insert_here.tooltip"))
        if suppressed:
            item.setToolTip(0, tr("tree.body.suppressed"))
        font = item.font(0)
        font.setBold(True)
        item.setFont(0, font)
        insert_here_color = QBrush(QColor("#4DD811"))
        for column in range(item.columnCount()):
            item.setForeground(column, insert_here_color)
        return item

    def _on_history_cursor_moved(self, cursor: int) -> None:
        if self.document is None or cursor == self.document.history_cursor():
            return
        self.document.set_history_cursor(cursor)
        self._mark_model_for_regeneration()
        self.selected_face = None
        self.selected_face_object_id = None
        selected_id = self.selected_object_id
        self._populate_tree()
        if selected_id is not None:
            self._select_tree_object(selected_id)
        self.rebuild_view(fit=False)

    def _on_history_object_moved(
        self,
        entity_id: str,
        target_index: int,
    ) -> None:
        if self.document is None:
            return
        if not self.document.move_history_object(entity_id, target_index):
            return
        self._mark_model_for_regeneration()
        self.selected_face = None
        self.selected_face_object_id = None
        self.selected_object_id = entity_id
        self._history_source_cycle_index = -1
        self._populate_tree()
        self._select_tree_object(entity_id)
        self.rebuild_view(fit=False)

    def _update_document_area_visibility(self) -> None:
        has_document = self.document is not None
        self.document_tabs.setVisible(has_document)
        self.document_splitter.setVisible(has_document)
        for action_name in (
            "material_action",
            "parameters_action",
            "file_settings_action",
        ):
            action = getattr(self, action_name, None)
            if action is not None:
                action.setEnabled(has_document)
        if hasattr(self, "view_selection_action"):
            self.view_selection_action.setEnabled(has_document)
        for action in getattr(self, "application_actions", {}).values():
            action.setEnabled(has_document)

    def _store_active_session(self) -> None:
        if 0 <= self.active_document_index < len(self.document_sessions):
            if self.document is None:
                return
            session = self.document_sessions[self.active_document_index]
            session.document = self.document
            session.file_path = self.current_file_path
            session.selected_object_id = self.selected_object_id
            session.active_application = self.active_application

    def _sync_workspace_sessions(self, source_window) -> None:
        if source_window is self:
            return
        active_document = self.document
        self.document_tabs.blockSignals(True)
        while self.document_tabs.count():
            self.document_tabs.removeTab(0)
        for session in self.document_sessions:
            self.document_tabs.addTab(
                self._file_label(session.file_path, session.document)
            )
        self.document_tabs.blockSignals(False)

        if not self.document_sessions:
            self.active_document_index = -1
            self.document = None
            self.current_file_path = None
            self.selected_object_id = None
            self._populate_tree()
            self.rebuild_view(fit=True)
            self._update_window_title()
            self._refresh_window_menu()
            return

        index = next(
            (
                session_index
                for session_index, session in enumerate(self.document_sessions)
                if session.document is active_document
            ),
            0,
        )
        self.active_document_index = -1
        self.document_tabs.setCurrentIndex(index)
        self._on_document_tab_changed(index)

    def _sync_workspace_document(
        self,
        source_window,
        document: PartDocument,
    ) -> None:
        if source_window is self or self.document is not document:
            return
        self._handling_workspace_update = True
        try:
            selected_id = self.selected_object_id
            self._populate_tree()
            if selected_id is not None:
                self._select_tree_object(selected_id)
            self.rebuild_view(fit=False, rebuild_geometry=True)
        finally:
            self._handling_workspace_update = False

    def _add_document_session(
        self,
        document: PartDocument,
        file_path: Path | None,
    ) -> None:
        self._store_active_session()
        session = DocumentSession(document=document, file_path=file_path)
        self.document_sessions.append(session)
        self.document_tabs.addTab(self._file_label(file_path, document))
        new_index = len(self.document_sessions) - 1
        self.document_tabs.setCurrentIndex(new_index)
        if self.active_document_index != new_index:
            self._on_document_tab_changed(new_index)
        self._refresh_window_menu()
        self.workspace.sessionsChanged.emit(self)

    def _on_document_tab_changed(self, index: int) -> None:
        if index == self.active_document_index:
            return

        if self._dimension_overlays:
            self._clear_dimension_overlays()
        self._store_active_session()
        self.active_document_index = index

        if not 0 <= index < len(self.document_sessions):
            self.document = None
            self.current_file_path = None
            self.selected_object_id = None
            self.active_application = ApplicationMode.MODELING
        else:
            session = self.document_sessions[index]
            self.document = session.document
            self.current_file_path = session.file_path
            self.selected_object_id = session.selected_object_id
            self.active_application = session.active_application

        self._sync_application_actions()
        self._rebuild_application_toolbar()
        self._populate_tree()
        if self.selected_object_id is not None:
            self._select_tree_object(self.selected_object_id)
        self._ensure_viewer_initialized()
        self.rebuild_view(fit=True)
        self._update_window_title()
        self._refresh_window_menu()

    def close_document_tab(self, index: int) -> None:
        if not 0 <= index < len(self.document_sessions):
            return

        if index == self.active_document_index:
            self._store_active_session()

        self.document_tabs.blockSignals(True)
        self.document_sessions.pop(index)
        self.document_tabs.removeTab(index)
        self.document_tabs.blockSignals(False)

        if not self.document_sessions:
            self.active_document_index = -1
            self.document = None
            self.current_file_path = None
            self.selected_object_id = None
            self._populate_tree()
            self.rebuild_view(fit=True)
            self._update_window_title()
            self._refresh_window_menu()
            self.workspace.sessionsChanged.emit(self)
            return

        new_index = min(index, len(self.document_sessions) - 1)
        self.active_document_index = -1
        self.document_tabs.setCurrentIndex(new_index)
        self._on_document_tab_changed(new_index)
        self.workspace.sessionsChanged.emit(self)

    def new_document(self) -> None:
        dialog = NewDocumentDialog(self)
        while True:
            if dialog.exec() != QDialog.DialogCode.Accepted:
                return
            document_type = dialog.selected_document_type()
            file_stem = dialog.file_stem()
            extension = {
                "part": ".prtz",
                "assembly": ".asmz",
                "drawing": ".drwz",
            }[document_type]
            file_path = self.working_directory / f"{file_stem}{extension}"
            if not self._document_file_name_exists(file_path):
                break
            QMessageBox.information(
                self,
                tr("dialog.new.title"),
                tr(
                    "message.file_already_exists",
                    file=file_path.name,
                ),
            )

        if document_type != "part":
            QMessageBox.information(
                self,
                tr("dialog.new.title"),
                tr("message.not_implemented", document_type=document_type.title()),
            )
            return

        document = create_empty_part()
        for unit_name in document.document_units:
            if unit_name in self.settings.units:
                document.document_units[unit_name] = self.settings.units[unit_name]
        self._add_document_session(document, file_path)

    def _document_file_name_exists(self, file_path: Path) -> bool:
        target_name = file_path.name.casefold()
        target_directory = canonical_document_path(file_path.parent)
        for session in self.document_sessions:
            if session.file_path is None:
                continue
            session_path = canonical_document_path(session.file_path)
            if (
                session_path.parent == target_directory
                and session_path.name.casefold() == target_name
            ):
                return True
        try:
            return any(
                entry.name.casefold() == target_name
                for entry in file_path.parent.iterdir()
            )
        except FileNotFoundError:
            return False

    def close_document(self) -> None:
        self.close_document_tab(self.active_document_index)

    def open_document(self) -> None:
        file_name, _ = QFileDialog.getOpenFileName(
            self,
            tr("file.open_part"),
            str(self.working_directory),
            tr("file.filter.part"),
        )
        if not file_name:
            return

        self.open_document_path(Path(file_name))

    def open_document_path(self, file_path: Path) -> bool:
        canonical_path = canonical_document_path(file_path)
        for index, session in enumerate(self.document_sessions):
            if session.file_path is None:
                continue
            if canonical_document_path(session.file_path) == canonical_path:
                self.document_tabs.setCurrentIndex(index)
                if self.active_document_index != index:
                    self._on_document_tab_changed(index)
                return True

        try:
            document = load_part_document(canonical_path)
        except ContainerEntityLimitError as exc:
            QMessageBox.critical(
                self,
                tr("message.open_failed"),
                tr(
                    "message.container.entity_limit_details",
                    container=exc.container_name,
                    entities=", ".join(exc.entity_names),
                ),
            )
            return False
        except Exception as exc:
            QMessageBox.critical(self, tr("message.open_failed"), str(exc))
            return False

        self._add_document_session(document, canonical_path)
        # Stored coordinates are only the last evaluated state.  Always
        # resolve parametric references against the freshly built geometry
        # before the opened document is used.
        self.regenerate_model()
        return True

    def save_document(self) -> bool:
        if self.document is None:
            QMessageBox.information(
                self, tr("menu.file.save"), tr("message.no_document")
            )
            return False

        if self.current_file_path is None:
            return self.save_document_as()

        return self._save_to_path(self.current_file_path)

    def save_document_as(self) -> bool:
        if self.document is None:
            QMessageBox.information(
                self, tr("menu.file.save_as"), tr("message.no_document")
            )
            return False

        default_path = self.working_directory / "part.prtz"
        file_name, _ = QFileDialog.getSaveFileName(
            self,
            tr("file.save_part"),
            str(default_path),
            tr("file.filter.part"),
        )
        if not file_name:
            return False

        file_path = Path(file_name)
        if file_path.suffix.lower() != ".prtz":
            file_path = file_path.with_suffix(".prtz")

        return self._save_to_path(file_path)

    def _save_to_path(self, file_path: Path) -> bool:
        try:
            if self.document is None:
                return False
            save_part_document(self.document, file_path)
        except ContainerEntityLimitError as exc:
            QMessageBox.critical(
                self,
                tr("message.save_failed"),
                tr(
                    "message.container.entity_limit_details",
                    container=exc.container_name,
                    entities=", ".join(exc.entity_names),
                ),
            )
            return False
        except Exception as exc:
            QMessageBox.critical(self, tr("message.save_failed"), str(exc))
            return False

        self.current_file_path = file_path
        if 0 <= self.active_document_index < len(self.document_sessions):
            self.document_sessions[self.active_document_index].file_path = file_path
        self._update_window_title()
        self.workspace.sessionsChanged.emit(self)
        self.statusBar().showMessage(
            tr(
                "status.file_saved",
                file=file_path.name,
                time=QTime.currentTime().toString("HH:mm:ss"),
            ),
            5000,
        )
        return True

    def _document_archive_paths(self, file_path: Path) -> list[Path]:
        target = canonical_document_path(file_path)
        prefix = f"{target.name}."
        archives: list[tuple[int, Path]] = []
        if target.parent.is_dir():
            for candidate in target.parent.iterdir():
                if not candidate.is_file() or not candidate.name.startswith(prefix):
                    continue
                suffix = candidate.name[len(prefix):]
                if suffix.isdigit():
                    archives.append((int(suffix), candidate))
        return [path for _version, path in sorted(archives)]

    def _refresh_delete_file_actions(self) -> None:
        target = (
            canonical_document_path(self.current_file_path)
            if self.current_file_path is not None
            else None
        )
        has_saved_document = (
            self.document is not None
            and target is not None
            and target.is_file()
        )
        archives = (
            self._document_archive_paths(target)
            if has_saved_document
            else []
        )
        self.delete_old_versions_action.setEnabled(bool(archives))
        self.delete_all_versions_action.setEnabled(has_saved_document)

    def delete_old_file_versions(self) -> None:
        if self.current_file_path is None:
            return
        target = canonical_document_path(self.current_file_path)
        archives = self._document_archive_paths(target)
        if not archives:
            QMessageBox.information(
                self,
                tr("menu.file.delete.old_versions"),
                tr("message.delete.no_old_versions", file=target.name),
            )
            return
        answer = QMessageBox.question(
            self,
            tr("menu.file.delete.old_versions"),
            tr(
                "message.delete.old_versions.confirm",
                count=len(archives),
                file=target.name,
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            for archive in archives:
                archive.unlink()
        except OSError as exc:
            QMessageBox.critical(
                self,
                tr("message.delete.failed"),
                str(exc),
            )
            return
        self.statusBar().showMessage(
            tr(
                "status.delete.old_versions",
                count=len(archives),
                file=target.name,
            ),
            5000,
        )

    def delete_all_file_versions(self) -> None:
        if self.current_file_path is None:
            return
        target = canonical_document_path(self.current_file_path)
        archives = self._document_archive_paths(target)
        existing_paths = [
            path
            for path in [*archives, target]
            if path.is_file()
        ]
        answer = QMessageBox.warning(
            self,
            tr("menu.file.delete.all_versions"),
            tr(
                "message.delete.all_versions.confirm",
                count=len(existing_paths),
                file=target.name,
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            for path in existing_paths:
                path.unlink()
        except OSError as exc:
            QMessageBox.critical(
                self,
                tr("message.delete.failed"),
                str(exc),
            )
            return
        deleted_name = target.name
        self.close_document_tab(self.active_document_index)
        self.statusBar().showMessage(
            tr(
                "status.delete.all_versions",
                count=len(existing_paths),
                file=deleted_name,
            ),
            5000,
        )

    def _apply_and_save_document(self) -> bool:
        self._store_active_session()
        return self.save_document()

    def _reload_application_settings(self) -> None:
        self.settings = load_application_settings(self.settings.local_config_path)
        configure_localization(
            self.settings.localization_path,
            self.settings.language,
        )

    def set_working_directory(self) -> None:
        directory = QFileDialog.getExistingDirectory(
            self,
            tr("file.set_working_directory"),
            str(self.working_directory),
        )
        if not directory:
            return

        self.working_directory = Path(directory)
        self.working_directory.mkdir(parents=True, exist_ok=True)
        self._update_window_title()

    def _update_window_title(self) -> None:
        file_label = self._file_label(self.current_file_path, self.document)
        self.setWindowTitle(
            f"{tr('app.title')} — {file_label}"
        )
        if 0 <= self.active_document_index < self.document_tabs.count():
            self.document_tabs.setTabText(self.active_document_index, file_label)
        self._refresh_window_menu()

    def _file_label(
        self,
        file_path: Path | None,
        document: PartDocument | None,
    ) -> str:
        if file_path is not None:
            return file_path.name
        return tr("status.untitled") if document is not None else tr("status.no_document")

    def _refresh_window_menu(self) -> None:
        if not hasattr(self, "window_menu"):
            return

        self.window_menu.clear()
        new_window_action = self.window_menu.addAction(
            tr("menu.window.new_window")
        )
        new_window_action.triggered.connect(self.open_new_window)
        self.window_menu.addSeparator()
        if not self.document_sessions:
            no_windows_action = self.window_menu.addAction(tr("status.no_open_documents"))
            no_windows_action.setEnabled(False)
            return

        for index, session in enumerate(self.document_sessions):
            action = self.window_menu.addAction(
                self._file_label(session.file_path, session.document)
            )
            action.setCheckable(True)
            action.setChecked(index == self.active_document_index)
            action.triggered.connect(
                lambda _checked=False, tab_index=index: self.document_tabs.setCurrentIndex(
                    tab_index
                )
            )

    def open_new_window(self) -> None:
        window = MainWindow(self.startup_context, self.workspace)
        window.showNormal()
        if len(self.document_sessions) > 1:
            window.document_tabs.setCurrentIndex(
                (self.active_document_index + 1)
                % len(self.document_sessions)
            )

        screens = QApplication.screens()
        current_screen = self.screen()
        target_screen = next(
            (screen for screen in screens if screen is not current_screen),
            current_screen,
        )
        if target_screen is None:
            return
        available = target_screen.availableGeometry()
        window.resize(
            min(max(self.width(), 900), available.width()),
            min(max(self.height(), 650), available.height()),
        )
        frame = window.frameGeometry()
        frame.moveCenter(available.center())
        window.move(frame.topLeft())
        window.showMaximized()
        window.raise_()
        window.activateWindow()

    def show_about_dialog(self) -> None:
        dialog = QMessageBox(self)
        dialog.setWindowTitle(tr("menu.help.about"))
        dialog.setWindowIcon(self.windowIcon())
        artwork = QPixmap(
            str(app_path("resources", "branding", "about.svg"))
        )
        if not artwork.isNull():
            dialog.setIconPixmap(
                artwork.scaled(
                    480,
                    260,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                )
            )
        dialog.setText(tr("dialog.about.text"))
        dialog.exec()

    def show_user_parameters_dialog(self) -> None:
        if self.document is None:
            QMessageBox.information(
                self, tr("dialog.parameters.title"), tr("message.no_document")
            )
            return

        dialog = UserParametersDialog(
            self.document,
            self.settings.language,
            self,
            save_callback=self._apply_and_save_document,
        )
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._store_active_session()

    def show_material_dialog(self) -> None:
        if self.document is None:
            QMessageBox.information(
                self, tr("dialog.material.title"), tr("message.no_document")
            )
            return

        dialog = MaterialDialog(
            self.document,
            self.settings.materials_path,
            self.settings.language,
            self.settings.units,
            self,
            save_callback=self._apply_and_save_document,
        )
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._store_active_session()

    def show_file_settings_dialog(self) -> None:
        if self.document is None:
            QMessageBox.information(
                self,
                tr("dialog.file_settings.title"),
                tr("message.no_document"),
            )
            return

        dialog = FileSettingsDialog(
            self.document,
            self,
            save_callback=self._apply_and_save_document,
        )
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._store_active_session()

    def show_options_dialog(self) -> None:
        dialog = OptionsDialog(
            self.settings.config_path,
            self.settings.language,
            self,
            applied_callback=self._reload_application_settings,
            settings=self.settings,
        )
        dialog.exec()

    def reset_view(self) -> None:
        if not hasattr(self, "_viewer_initialized") or self.document is None:
            return

        self.native_viewer.set_standard_view("default")
        self.native_viewer.fit_all()

    def _on_standard_view_changed(self, index: int) -> None:
        view_name = str(self.standard_view_combo.itemData(index) or "")
        if not view_name or not hasattr(self, "_viewer_initialized"):
            return
        self._set_standard_view(view_name)
        self.standard_view_combo.blockSignals(True)
        self.standard_view_combo.setCurrentIndex(0)
        self.standard_view_combo.blockSignals(False)

    def _set_standard_view(self, view_name: str) -> None:
        if not hasattr(self, "_viewer_initialized"):
            return
        self.native_viewer.set_standard_view(view_name)

    def set_view_display_mode(self, display_mode: ViewDisplayMode) -> None:
        self.view_display_mode = display_mode
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _ensure_viewer_initialized(self) -> None:
        if hasattr(self, "_viewer_initialized"):
            return

        # The native Viewer is the only graphical context.  OCCT remains
        # available for geometry calculations, but its AIS/V3d driver must
        # not be initialized alongside QOpenGLWidget: that combination
        # crashes inside the native OpenGL drivers after a short delay.
        self._viewer_initialized = True

    def _create_tree_item(
        self,
        obj: ZimaEntity,
        show_internal: bool = True,
    ) -> QTreeWidgetItem | None:
        if obj.tree_exposure == TreeExposure.HIDDEN:
            return None
        if obj.tree_exposure == TreeExposure.INTERNAL and not show_internal:
            return None
        if obj.kind == EntityKind.POINT:
            name = self._point_display_name(obj)
        elif obj.kind == EntityKind.ORIGIN:
            name = tr(
                {
                    OriginScope.PART: "tree.origin.part",
                    OriginScope.ASSEMBLY: "tree.origin.assembly",
                    OriginScope.CONTAINER: "tree.origin.container",
                    OriginScope.LOCAL: "tree.origin.local",
                }.get(
                    obj.origin_scope or OriginScope.LOCAL,
                    "tree.origin.local",
                )
            )
        elif obj.kind == EntityKind.BODY:
            suffix = obj.name.removeprefix("Body")
            name = (
                f"{tr('tree.body')}{suffix}"
                if suffix.isdigit()
                else obj.name
            )
        elif obj.kind == EntityKind.SKETCH and obj.sketch_role() is not None:
            name = f"{obj.name} [{obj.sketch_role().value}]"
        else:
            name = obj.name
        operation_source = obj
        if obj.kind == EntityKind.CONTAINER:
            solids = [
                child
                for child in obj.children
                if not child.locked and child.kind in SOLID_KINDS
            ]
            if len(solids) == 1:
                operation_source = solids[0]
        if operation_source.kind in SOLID_KINDS:
            operation = {
                CombineMode.ADD: "+",
                CombineMode.SUBTRACT: "−",
            }.get(operation_source.combine_mode, "")
            name = f"{operation} {name}".strip()
        effectively_suppressed = (
            self._is_effectively_suppressed_at_boundary(obj)
            if self.document is not None
            else obj.suppressed
        )
        item = QTreeWidgetItem([name])
        icon_name = TREE_ICON_NAMES.get(obj.kind)
        if icon_name is not None:
            item.setIcon(0, resource_icon(icon_name))
        item.setData(0, Qt.ItemDataRole.UserRole, obj.entity_id)
        if effectively_suppressed:
            font = item.font(0)
            font.setItalic(True)
            font.setStrikeOut(True)
            item.setFont(0, font)
            brush = QBrush(QColor("#808080"))
            for column in range(item.columnCount()):
                item.setForeground(column, brush)
            item.setToolTip(0, tr("tree.state.suppressed"))
        missing_references = self._missing_reference_labels(obj)
        if missing_references and not effectively_suppressed:
            warning_brush = QBrush(QColor("#8b2424"))
            text_brush = QBrush(QColor("#ffffff"))
            for column in range(item.columnCount()):
                item.setBackground(column, warning_brush)
                item.setForeground(column, text_brush)
            item.setToolTip(
                0,
                tr(
                    "tree.state.missing_references",
                    names=", ".join(missing_references),
                ),
            )
        for child in obj.children:
            child_item = self._create_tree_item(
                child,
                True,
            )
            if child_item is not None:
                item.addChild(child_item)
        return item

    def _is_effectively_suppressed_at_boundary(
        self,
        obj: ZimaEntity,
    ) -> bool:
        if self.document is None:
            return obj.suppressed
        if self.document.is_effectively_suppressed(obj.entity_id):
            return True
        history_object = (
            obj
            if obj.kind == EntityKind.CONTAINER
            else self.document.find_owning_object(obj.entity_id)
        )
        if history_object is None:
            return False
        index = self.document.history_index(history_object.entity_id)
        return (
            index is not None
            and index >= self._definition_history_boundary()
        )

    def _missing_reference_labels(self, obj: ZimaEntity) -> list[str]:
        if self.document is None:
            return []
        labels: list[str] = []
        candidates = [obj, *self._descendant_objects(obj)]
        for candidate in candidates:
            if candidate.kind not in (
                EntityKind.POINT,
                EntityKind.AXIS,
                EntityKind.PLANE,
            ):
                continue
            try:
                references = json.loads(
                    str(candidate.parameters.get("constraint_refs", "[]"))
                )
            except (TypeError, ValueError, json.JSONDecodeError):
                continue
            if not isinstance(references, list):
                continue
            for reference in references:
                if not isinstance(reference, dict):
                    continue
                entity_id = str(reference.get("entity_id", "")).strip()
                if (
                    not entity_id
                    or self.document.find_entity(entity_id) is not None
                ):
                    continue
                label = str(
                    reference.get("label", reference.get("key", entity_id))
                )
                if label not in labels:
                    labels.append(label)
        return labels

    def _descendant_objects(self, parent: ZimaEntity) -> list[ZimaEntity]:
        result: list[ZimaEntity] = []
        for child in parent.children:
            result.append(child)
            result.extend(self._descendant_objects(child))
        return result

    def _point_display_name(self, point: ZimaEntity) -> str:
        if self.document is None:
            return "Point"
        owner = self.document.find_owning_object(point.entity_id)
        if owner is not None and owner.kind == EntityKind.CONTAINER:
            suffix = owner.name.removeprefix("Container")
            return f"Point{suffix}" if suffix.isdigit() else f"Point ({owner.name})"
        return "Point"

    @staticmethod
    def _user_point_entity(obj: ZimaEntity | None) -> ZimaEntity | None:
        if obj is None or obj.kind != EntityKind.CONTAINER:
            return None
        point = next(
            (
                child
                for child in obj.children
                if child.kind == EntityKind.POINT and not child.locked
            ),
            None,
        )
        if point is not None:
            return point
        if obj.container_type != ContainerType.POINT:
            return None
        origin = next(
            (
                child
                for child in obj.children
                if child.kind == EntityKind.ORIGIN
            ),
            None,
        )
        if origin is None:
            return None
        return next(
            (
                child
                for child in origin.children
                if child.kind == EntityKind.POINT
            ),
            None,
        )

    @staticmethod
    def _user_axis_entity(obj: ZimaEntity | None) -> ZimaEntity | None:
        if obj is None or obj.kind != EntityKind.CONTAINER:
            return None
        return next(
            (
                child
                for child in obj.children
                if child.kind == EntityKind.AXIS and not child.locked
            ),
            None,
        )

    def _on_selection_filter_changed(self) -> None:
        value = self.selection_filter_combo.currentData()
        self.view_selection_filter = ViewSelectionFilter(value)
        self.view_selection_mode = (
            ViewSelectionMode.FACE
            if self.view_selection_filter == ViewSelectionFilter.FACE
            else ViewSelectionMode.CONTAINER
        )
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _on_native_edge_selected(
        self,
        owner_id: str,
        edge_index: int,
    ) -> None:
        if not owner_id or edge_index <= 0:
            return
        if self._sketch_reference_mode:
            owner = (
                self.document.find_entity(owner_id)
                if self.document is not None
                else None
            )
            self._add_sketch_external_reference(
                (
                    "axis"
                    if owner is not None
                    and owner.kind in (
                        EntityKind.ORIGIN,
                        EntityKind.AXIS,
                    )
                    else "edge"
                ),
                owner_id,
                edge_index,
            )
            return
        self.native_viewer.set_object_overlay(None)
        if self.document is not None:
            owner = self.document.find_entity(owner_id)
            if owner is not None and owner.kind in (
                EntityKind.ORIGIN,
                EntityKind.AXIS,
            ):
                self._on_native_coordinate_selected(
                    owner_id,
                    edge_index,
                    "axis",
                )
                return
        scene = self._native_viewer_scene
        if scene is None:
            return
        shape = scene.resolve_topology(owner_id, "edge", edge_index)
        if shape is None:
            self._on_native_coordinate_selected(
                owner_id, edge_index, "axis"
            )
            return
        self._apply_native_view_selection(owner_id, shape)

    def _on_native_object_selected(self, owner_id: str) -> None:
        if self.document is None:
            return
        self.native_viewer.set_selected_container_contents(set())
        self.native_viewer.set_selected_container_origin(None)
        if not owner_id:
            if self._dimension_overlays:
                self._clear_dimension_overlays()
            self.native_viewer.set_object_overlay(None)
            self.native_viewer.set_selected_reference_owner(None)
            self.tree.blockSignals(True)
            self.tree.clearSelection()
            self.tree.setCurrentItem(None)
            self.tree.blockSignals(False)
            self.selected_object_id = None
            self.selected_face = None
            self.selected_face_object_id = None
            self._view_selection_confirmed = False
            self._history_source_cycle_active = False
            self.statusBar().clearMessage()
            return
        if (
            owner_id == self.document.root.entity_id
            and self._history_source_cycle_active
            and 0 <= self._history_source_cycle_index
            < len(self._history_source_cycle_ids)
        ):
            token = self._history_source_cycle_ids[
                self._history_source_cycle_index
            ]
            parts = token.split(":", 2)
            cycled_owner_id = parts[1] if len(parts) == 3 else ""
            if parts[0] == "object" and cycled_owner_id not in {
                "",
                self.document.root.entity_id,
            }:
                source = self.document.find_entity(cycled_owner_id)
                source_shape = (
                    self.document.build_standalone_shape(source)
                    if source is not None
                    else None
                )
                self._select_native_tree_object(cycled_owner_id)
                self.native_viewer._selected_object_id = None
                self.native_viewer._hovered_object_id = None
                self.native_viewer.set_object_overlay(
                    triangulate_shape(
                        source_shape,
                        owner_id=cycled_owner_id,
                    )
                    if source_shape is not None
                    else None,
                    selected=True,
                    anchor=self._native_object_origin(source),
                )
                self._history_source_cycle_active = False
                return
        self.native_viewer.set_object_overlay(None)
        tree_object_id = owner_id
        if owner_id == self.document.root.entity_id:
            bodies = self.document.root.body_children()
            if bodies:
                tree_object_id = bodies[-1].entity_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._history_source_cycle_active = False
        self._select_native_tree_object(tree_object_id)

    def _on_native_object_double_clicked(self) -> None:
        obj = self._selected_object()
        if obj is None or not self._view_selection_confirmed:
            return
        target = obj
        if obj.kind in (EntityKind.PART, EntityKind.BODY):
            if self.document is None:
                return
            target = next(
                (
                    candidate
                    for candidate in reversed(
                        self.document.active_history_objects()
                    )
                    if self._first_editable_solid(candidate) is not None
                ),
                obj,
            )
        target = self._activate_object_for_editing(target)
        self._show_edit_overlays(
            target,
            QPoint(
                self.native_viewer.width() // 2,
                self.native_viewer.height() // 2,
            ),
        )

    def _on_native_face_selected(
        self,
        owner_id: str,
        face_index: int,
    ) -> None:
        if not owner_id or face_index <= 0:
            return
        if self._sketch_reference_mode:
            self._add_sketch_external_reference(
                "face",
                owner_id,
                face_index,
            )
            return
        self.native_viewer.set_object_overlay(None)
        scene = self._native_viewer_scene
        if scene is None:
            return
        shape = scene.resolve_topology(owner_id, "face", face_index)
        if self._normal_view_selection_active and shape is not None:
            adaptor = BRepAdaptor_Surface(shape)
            if adaptor.GetType() != GeomAbs_Plane:
                return
            self.selected_face = shape
            self.selected_face_object_id = owner_id
            self._view_normal_to_selected_face()
            self.normal_view_action.setChecked(False)
            return
        if shape is not None:
            self._apply_native_view_selection(owner_id, shape)

    def _on_native_coordinate_selected(
        self,
        owner_id: str,
        element_index: int,
        element_kind: str,
    ) -> None:
        if not owner_id or self.document is None:
            return
        if self._sketch_reference_mode:
            self._add_sketch_external_reference(
                element_kind,
                owner_id,
                element_index,
            )
            return
        if (
            self._normal_view_selection_active
            and element_kind == "plane"
        ):
            plane = self._plane_entity_from_view_key(
                owner_id,
                element_index,
            )
            if plane is not None:
                self._view_normal_to_reference_plane(plane)
                self.normal_view_action.setChecked(False)
            return
        self.native_viewer.set_selected_container_contents(set())
        self.native_viewer.set_selected_container_origin(None)
        obj = self.document.find_entity(owner_id)
        if obj is None:
            return
        selected_id = obj.entity_id
        if obj.kind == EntityKind.ORIGIN:
            values = {
                "axis": ("x", "y", "z"),
                "plane": ("xy", "yz", "xz"),
            }.get(element_kind)
            if values is not None and 1 <= element_index <= 3:
                value = values[element_index - 1]
                child_kind = (
                    EntityKind.AXIS
                    if element_kind == "axis"
                    else EntityKind.PLANE
                )
                child = next(
                    (
                        item
                        for item in obj.children
                        if item.kind == child_kind
                        and item.parameters.get(element_kind) == value
                    ),
                    None,
                )
                if child is not None:
                    selected_id = child.entity_id
            elif element_kind == "point":
                child = next(
                    (
                        item
                        for item in obj.children
                        if item.kind == EntityKind.POINT
                    ),
                    None,
                )
                if child is not None:
                    selected_id = child.entity_id
        selected_reference = self.document.find_entity(selected_id)
        if (
            selected_reference is not None
            and selected_reference.kind in (
                EntityKind.POINT,
                EntityKind.AXIS,
                EntityKind.PLANE,
            )
        ):
            owner = self.document.find_owning_object(
                selected_reference.entity_id
            )
            if owner is not None and owner.kind == EntityKind.CONTAINER:
                owner_item = self._find_tree_item(
                    self.tree.invisibleRootItem(),
                    owner.entity_id,
                )
                if owner_item is None or not owner_item.isExpanded():
                    selected_id = owner.entity_id
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
            and selected_reference is not None
        ):
            self.point_constraint_dialog.add_reference(
                self._user_axis_entity(selected_reference)
                or selected_reference
            )
        self._select_native_tree_object(selected_id)

    def _apply_native_view_selection(self, owner_id: str, shape) -> None:
        if self.document is None or (
            not self.view_selection_enabled
            and not self._sketch_reference_mode
        ):
            return
        obj = self.document.find_entity(owner_id)
        if obj is None:
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
            and shape.ShapeType() in (TopAbs_VERTEX, TopAbs_FACE, TopAbs_EDGE)
        ):
            self._add_point_shape_constraint(obj, shape)
        if self.view_selection_mode == ViewSelectionMode.FACE:
            if shape.ShapeType() != TopAbs_FACE:
                return
            self.selected_face = shape
            self.selected_face_object_id = owner_id
            self.statusBar().showMessage(
                tr("selection.status.selected_face", name=obj.name)
            )
        self._select_native_tree_object(owner_id)

    def _select_native_tree_object(self, entity_id: str) -> None:
        root = self.tree.invisibleRootItem()
        item = self._find_tree_item(root, entity_id)
        self.tree.blockSignals(True)
        if item is not None:
            self.tree.setCurrentItem(item)
        else:
            self.tree.clearSelection()
            self.tree.setCurrentItem(None)
        self.tree.blockSignals(False)
        self.selected_object_id = entity_id
        self._view_selection_confirmed = True

    def _on_tree_selection_changed(self) -> None:
        if self._dimension_overlays:
            self._clear_dimension_overlays()
        self.native_viewer.set_selection_preview_pending(False)
        self._history_source_cycle_active = False
        self._cycled_history_source_id = None
        self._reference_cycle_preview_id = None
        self._view_candidate_cycle_ids = ()
        self._view_candidate_cycle_index = -1
        self.selected_face = None
        self.selected_face_object_id = None
        selected = self.tree.selectedItems()
        if not selected:
            self.selected_object_id = None
            self._view_selection_confirmed = False
        else:
            sketch_point_link = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_POINT_LINK_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and sketch_point_link is not None
            ):
                self._select_sketch_constraint(
                    str(sketch_point_link),
                    -1,
                )
                return
            sketch_geometry_constraint = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_GEOMETRY_CONSTRAINT_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and isinstance(sketch_geometry_constraint, tuple)
                and len(sketch_geometry_constraint) == 2
            ):
                self._select_sketch_geometry_child(
                    str(sketch_geometry_constraint[0])
                )
                return
            sketch_constraint = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_CONSTRAINT_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and isinstance(sketch_constraint, tuple)
                and len(sketch_constraint) == 2
            ):
                self._select_sketch_constraint(
                    str(sketch_constraint[0]),
                    int(sketch_constraint[1]),
                )
                return
            external_reference_id = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_EXTERNAL_REFERENCE_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and external_reference_id is not None
            ):
                self._select_sketch_external_reference(
                    str(external_reference_id)
                )
                return
            sketch_reference = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_REFERENCE_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and isinstance(sketch_reference, tuple)
                and len(sketch_reference) == 3
            ):
                self._select_sketch_reference(
                    str(sketch_reference[0]),
                    str(sketch_reference[1]),
                    int(sketch_reference[2]),
                )
                return
            sketch_entity_id = selected[0].data(
                0,
                HistoryTreeWidget.SKETCH_ENTITY_ROLE,
            )
            if (
                self._sketch_edit_entity_id is not None
                and sketch_entity_id is not None
            ):
                if self._sketch_tool != "select":
                    self._set_sketch_tool("select")
                self._select_sketch_entity(str(sketch_entity_id))
                return
            self.selected_object_id = selected[0].data(0, Qt.ItemDataRole.UserRole)
            self._view_selection_confirmed = self.selected_object_id is not None
            if (
                self.point_constraint_dialog is not None
                and self.point_constraint_dialog.isVisible()
                and self.document is not None
            ):
                reference = self.document.find_entity(self.selected_object_id)
                if reference is not None:
                    self.point_constraint_dialog.add_reference(
                        self._user_axis_entity(reference) or reference
                    )
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _on_tree_item_clicked(
        self,
        item: QTreeWidgetItem,
        column: int,
    ) -> None:
        return

    def _on_tree_item_double_clicked(
        self,
        item: QTreeWidgetItem,
        _column: int,
    ) -> None:
        obj = self._object_from_tree_item(item)
        if obj is None or obj.kind in SOLID_KINDS:
            return
        if self._switch_tree_properties_dialog(obj):
            return
        if obj.kind == EntityKind.SKETCH:
            self._enter_sketch_edit(obj.entity_id)
            return
        self.show_properties(obj)

    def _switch_tree_properties_dialog(self, obj: ZimaEntity) -> bool:
        """Close another object's properties or focus the current one."""
        dialog = self.point_constraint_dialog
        if dialog is None or not dialog.isVisible():
            return False
        target = obj
        if obj.kind != EntityKind.CONTAINER and self.document is not None:
            owner = self.document.find_owning_object(obj.entity_id)
            if owner is not None:
                target = owner
        current = dialog.point_object
        if (
            current is not None
            and current.entity_id == target.entity_id
        ):
            dialog.raise_()
            dialog.activateWindow()
            return True
        # Reject restores the last applied baseline before the next object's
        # live properties dialog is opened.
        dialog.reject()
        return False

    def _view_hover_selection_locked(self) -> bool:
        point_dialog_active = (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        )
        return self._view_selection_confirmed and not point_dialog_active

    def _solid_face_role(self, solid: ZimaEntity, face) -> str | None:
        if self.document is None:
            return None
        adaptor = BRepAdaptor_Surface(face)
        if adaptor.GetType() != GeomAbs_Plane:
            return None
        direction = adaptor.Plane().Axis().Direction()
        sign = -1.0 if face.Orientation() == TopAbs_REVERSED else 1.0
        selected_normal = (
            sign * direction.X(),
            sign * direction.Y(),
            sign * direction.Z(),
        )
        candidates = []
        for role, (_point, normal) in solid_face_frames(self.document, solid).items():
            agreement = sum(selected_normal[index] * normal[index] for index in range(3))
            candidates.append((agreement, role))
        return max(candidates)[1] if candidates else None

    def _add_point_shape_constraint(self, obj: ZimaEntity, shape) -> None:
        dialog = self.point_constraint_dialog
        if dialog is None:
            return
        shape_type = shape.ShapeType()
        topology_index = self._subshape_index(obj.entity_id, shape)
        reference_metadata = self._shape_reference_metadata(obj)
        if shape_type == TopAbs_VERTEX:
            point = BRep_Tool.Pnt(shape)
            endpoint_identity = self._vertex_endpoint_identity(
                obj.entity_id,
                shape,
            )
            topology_key = (
                f"edge:{endpoint_identity[0]}:{endpoint_identity[1]}"
                if endpoint_identity is not None
                else str(topology_index)
            )
            dialog.add_shape_reference(
                obj.entity_id,
                self._topology_reference_label(
                    obj,
                    "vertex",
                    topology_index,
                ),
                "vertex",
                [
                    [1.0, 0.0, 0.0, point.X()],
                    [0.0, 1.0, 0.0, point.Y()],
                    [0.0, 0.0, 1.0, point.Z()],
                ],
                topology_key,
                {
                    **reference_metadata,
                    "vertex_index": topology_index,
                    **(
                        {
                            "edge_index": endpoint_identity[0],
                            "endpoint": endpoint_identity[1],
                        }
                        if endpoint_identity is not None
                        else {}
                    ),
                },
            )
            return
        if shape_type == TopAbs_FACE:
            adaptor = BRepAdaptor_Surface(shape)
            if adaptor.GetType() != GeomAbs_Plane:
                self.statusBar().showMessage(
                    tr("dialog.point_constraints.unsupported_curved_face")
                )
                return
            plane = adaptor.Plane()
            location = plane.Location()
            normal = plane.Axis().Direction()
            sign = -1.0 if shape.Orientation() == TopAbs_REVERSED else 1.0
            equation = [
                sign * normal.X(),
                sign * normal.Y(),
                sign * normal.Z(),
                sign
                * (
                    normal.X() * location.X()
                    + normal.Y() * location.Y()
                    + normal.Z() * location.Z()
                ),
            ]
            dialog.add_shape_reference(
                obj.entity_id,
                self._topology_reference_label(
                    obj,
                    "face",
                    topology_index,
                ),
                "face",
                [equation],
                str(topology_index),
                reference_metadata,
            )
            return
        if shape_type == TopAbs_EDGE:
            adaptor = BRepAdaptor_Curve(shape)
            if adaptor.GetType() != GeomAbs_Line:
                self.statusBar().showMessage(
                    tr("dialog.point_constraints.unsupported_curved_edge")
                )
                return
            line = adaptor.Line()
            location = line.Location()
            direction = (
                line.Direction().X(),
                line.Direction().Y(),
                line.Direction().Z(),
            )
            helper = (
                (1.0, 0.0, 0.0)
                if abs(direction[0]) < 0.9
                else (0.0, 1.0, 0.0)
            )
            first = self._normalized_vector(
                self._cross_product(direction, helper)
            )
            second = self._normalized_vector(
                self._cross_product(direction, first)
            )
            point = (location.X(), location.Y(), location.Z())
            equations = [
                [
                    normal[0],
                    normal[1],
                    normal[2],
                    sum(normal[index] * point[index] for index in range(3)),
                ]
                for normal in (first, second)
            ]
            dialog.add_shape_reference(
                obj.entity_id,
                self._topology_reference_label(
                    obj,
                    "edge",
                    topology_index,
                ),
                "edge",
                equations,
                str(topology_index),
                reference_metadata,
            )

    @staticmethod
    def _topology_reference_label(
        obj: ZimaEntity,
        reference_type: str,
        topology_index: int,
    ) -> str:
        owner_name = tr("tree.body") if obj.kind == EntityKind.PART else obj.name
        return tr(
            f"dialog.point_constraints.{reference_type}_reference",
            name=owner_name,
            index=topology_index,
        )

    def _shape_reference_metadata(
        self,
        obj: ZimaEntity,
    ) -> dict[str, Any]:
        if self.document is None or obj.kind != EntityKind.PART:
            return {"source_object_id": obj.entity_id}
        boundary = self._definition_history_boundary()
        source_ids = [
            source.entity_id
            for source in self.document.history_objects_at(boundary)
        ]
        return {
            "reference_scope": "history_result",
            "history_cursor": boundary,
            "history_object_ids": source_ids,
        }

    def _subshape_index(self, entity_id: str, selected_shape) -> int:
        for model_shape, candidate_id in [
            *self._selectable_model_shapes,
            *self._cached_source_model_shapes,
        ]:
            if candidate_id != entity_id:
                continue
            index = 0
            seen: list[Any] = []
            explorer = TopExp_Explorer(
                model_shape,
                selected_shape.ShapeType(),
            )
            while explorer.More():
                candidate = explorer.Current()
                if (
                    selected_shape.ShapeType() == TopAbs_EDGE
                    and any(candidate.IsSame(existing) for existing in seen)
                ):
                    explorer.Next()
                    continue
                seen.append(candidate)
                index += 1
                if selected_shape.IsSame(candidate):
                    return index
                explorer.Next()
        return 0

    def _selectable_shape_owner_and_index(
        self,
        selected_shape,
    ) -> tuple[str, int] | None:
        shape_type = selected_shape.ShapeType()
        for model_shape, entity_id in [
            *self._selectable_model_shapes,
            *self._cached_source_model_shapes,
        ]:
            index = 0
            explorer = TopExp_Explorer(model_shape, shape_type)
            while explorer.More():
                index += 1
                if selected_shape.IsSame(explorer.Current()):
                    return entity_id, index
                explorer.Next()
        return None

    def _point_reference_shapes(self) -> list[tuple[Any, str]]:
        """Return the visible history result followed by its source features."""
        shapes: list[tuple[Any, str]] = []
        for model_shape, entity_id in [
            *self._selectable_model_shapes,
            *self._cached_source_model_shapes,
        ]:
            if any(
                entity_id == existing_id
                and model_shape.IsSame(existing_shape)
                for existing_shape, existing_id in shapes
            ):
                continue
            shapes.append((model_shape, entity_id))
        return shapes

    def _subshape_by_index(
        self,
        entity_id: str,
        shape_type: int,
        topology_index: int,
    ):
        if topology_index <= 0:
            return None
        for model_shape, candidate_id in [
            *self._selectable_model_shapes,
            *self._cached_source_model_shapes,
        ]:
            if candidate_id != entity_id:
                continue
            index = 0
            seen: list[Any] = []
            explorer = TopExp_Explorer(model_shape, shape_type)
            while explorer.More():
                candidate = explorer.Current()
                if (
                    shape_type == TopAbs_EDGE
                    and any(candidate.IsSame(existing) for existing in seen)
                ):
                    explorer.Next()
                    continue
                seen.append(candidate)
                index += 1
                if index == topology_index:
                    return candidate
                explorer.Next()
        return None

    @staticmethod
    def _subshape_from_shape(
        model_shape,
        shape_type: int,
        topology_index: int,
    ):
        if topology_index <= 0:
            return None
        index = 0
        seen: list[Any] = []
        explorer = TopExp_Explorer(model_shape, shape_type)
        while explorer.More():
            candidate = explorer.Current()
            if (
                shape_type == TopAbs_EDGE
                and any(candidate.IsSame(existing) for existing in seen)
            ):
                explorer.Next()
                continue
            seen.append(candidate)
            index += 1
            if index == topology_index:
                return candidate
            explorer.Next()
        return None

    def _vertex_endpoint_identity(
        self,
        entity_id: str,
        vertex,
    ) -> tuple[int, str] | None:
        try:
            vertex_point = BRep_Tool.Pnt(vertex)
        except (TypeError, RuntimeError):
            return None
        for model_shape, candidate_id in [
            *self._cached_source_model_shapes,
            *self._selectable_model_shapes,
        ]:
            if candidate_id != entity_id:
                continue
            edge_index = 0
            explorer = TopExp_Explorer(model_shape, TopAbs_EDGE)
            while explorer.More():
                edge_index += 1
                try:
                    adaptor = BRepAdaptor_Curve(explorer.Current())
                    endpoints = (
                        ("start", adaptor.Value(adaptor.FirstParameter())),
                        ("end", adaptor.Value(adaptor.LastParameter())),
                    )
                except (AttributeError, RuntimeError):
                    explorer.Next()
                    continue
                for endpoint, point in endpoints:
                    distance_squared = (
                        (point.X() - vertex_point.X()) ** 2
                        + (point.Y() - vertex_point.Y()) ** 2
                        + (point.Z() - vertex_point.Z()) ** 2
                    )
                    if distance_squared <= 1e-14:
                        return edge_index, endpoint
                explorer.Next()
        return None

    def _detected_items(context) -> list[tuple[Any, Any]]:
        """Return OCCT detections without invoking its native highlighting."""
        items: list[tuple[Any, Any]] = []
        try:
            context.InitDetected()
            while context.MoreDetected():
                items.append(
                    (
                        context.DetectedCurrentShape(),
                        context.DetectedCurrentObject(),
                    )
                )
                context.NextDetected()
        except (AttributeError, RuntimeError):
            return items
        return items

    def _world_point_pixels(
        view,
        world_point: tuple[float, float, float],
        viewport_width: float,
        viewport_height: float,
    ) -> tuple[float, float] | None:
        if viewport_width <= 0.0 or viewport_height <= 0.0:
            return None
        try:
            projected = view.Camera().Project(gp_Pnt(*world_point))
            return (
                (float(projected.X()) + 1.0) * 0.5 * viewport_width,
                (1.0 - float(projected.Y())) * 0.5 * viewport_height,
            )
        except (AttributeError, TypeError, RuntimeError):
            return None

    def _selection_filtered_object_id(self, entity_id: str) -> str:
        if self.document is None:
            return entity_id
        obj = self.document.find_entity(entity_id)
        if obj is None:
            return entity_id
        if obj.kind in (
            EntityKind.POINT,
            EntityKind.AXIS,
            EntityKind.PLANE,
        ):
            owner = self.document.find_owning_object(obj.entity_id)
            if (
                owner is not None
                and owner.kind == EntityKind.CONTAINER
                and owner.show_auxiliary_geometry
            ):
                # View visibility and picking are controlled solely by
                # show_auxiliary_geometry.  Whether the same entity is exposed
                # in the tree must not change the ID returned by view picking.
                return obj.entity_id
        if obj.tree_exposure == TreeExposure.INTERNAL:
            owner = self.document.find_owning_object(obj.entity_id)
            if owner is not None and not owner.show_internal_entities:
                if (
                    self.point_constraint_dialog is not None
                    and self.point_constraint_dialog.isVisible()
                    and obj.kind in (
                        EntityKind.POINT,
                        EntityKind.AXIS,
                        EntityKind.PLANE,
                    )
                ):
                    return obj.entity_id
                return owner.entity_id
        return entity_id

    def _show_tree_context_menu(self, position: QPoint) -> None:
        if self.document is None:
            return

        item = self.tree.itemAt(position)
        sketch_external_reference_id = (
            item.data(
                0,
                HistoryTreeWidget.SKETCH_EXTERNAL_REFERENCE_ROLE,
            )
            if (
                item is not None
                and self._sketch_edit_entity_id is not None
            )
            else None
        )
        sketch_constraint = (
            item.data(
                0,
                HistoryTreeWidget.SKETCH_CONSTRAINT_ROLE,
            )
            if (
                item is not None
                and self._sketch_edit_entity_id is not None
            )
            else None
        )
        sketch_geometry_constraint = (
            item.data(
                0,
                HistoryTreeWidget.SKETCH_GEOMETRY_CONSTRAINT_ROLE,
            )
            if (
                item is not None
                and self._sketch_edit_entity_id is not None
            )
            else None
        )
        if item is not None:
            if (
                sketch_constraint is not None
                or sketch_geometry_constraint is not None
            ):
                signals_were_blocked = self.tree.blockSignals(True)
                try:
                    self.tree.setCurrentItem(item)
                finally:
                    self.tree.blockSignals(signals_were_blocked)
            else:
                self.tree.setCurrentItem(item)
        if self._sketch_edit_entity_id is not None:
            if (
                isinstance(sketch_geometry_constraint, tuple)
                and len(sketch_geometry_constraint) == 2
            ):
                menu = QMenu(self)
                delete_constraint_action = menu.addAction(
                    resource_icon("delete"),
                    tr("menu.context.delete_constraint"),
                )
                action = menu.exec(
                    self.tree.viewport().mapToGlobal(position)
                )
                if action == delete_constraint_action:
                    self._delete_sketch_geometry_constraint(
                        str(sketch_geometry_constraint[0]),
                        int(sketch_geometry_constraint[1]),
                    )
                return
            if (
                isinstance(sketch_constraint, tuple)
                and len(sketch_constraint) == 2
            ):
                menu = QMenu(self)
                delete_constraint_action = menu.addAction(
                    resource_icon("delete"),
                    tr("menu.context.delete_constraint"),
                )
                action = menu.exec(
                    self.tree.viewport().mapToGlobal(position)
                )
                if action == delete_constraint_action:
                    self._delete_sketch_point_constraint(
                        str(sketch_constraint[0]),
                        int(sketch_constraint[1]),
                    )
                return
            if sketch_external_reference_id is None:
                return
            menu = QMenu(self)
            delete_reference_action = menu.addAction(
                resource_icon("delete"),
                tr("menu.context.delete_reference"),
            )
            action = menu.exec(
                self.tree.viewport().mapToGlobal(position)
            )
            if action == delete_reference_action:
                self._delete_sketch_external_reference(
                    str(sketch_external_reference_id)
                )
            return

        obj = self._object_from_tree_item(item)
        menu = QMenu(self)
        attach_action = None
        create_action = None
        create_point_action = None
        create_axis_action = None
        create_plane_action = None
        create_sketch_action = None
        create_sketch_actions: dict[Any, SketchRole] = {}
        edit_sketch_action = None
        edit_values_action = None
        properties_action = None
        delete_action = None
        suppress_action = None
        body_suppress_action = None
        add_action = None
        subtract_action = None
        auxiliary_visibility_action = None

        if (
            item is not None
            and item.data(0, HistoryTreeWidget.ROLLBACK_ROLE)
        ):
            body_suppress_action = menu.addAction(
                tr(
                    "menu.context.resume"
                    if self.document.body_is_suppressed()
                    else "menu.context.suppress"
                )
            )
        elif obj is None or obj.kind == EntityKind.PART:
            create_action = menu.addAction(tr("menu.context.create_container"))
        elif obj.kind == EntityKind.ORIGIN:
            owner = self.document.find_parent(obj.entity_id)
            if owner is not None and owner.kind == EntityKind.CONTAINER:
                auxiliary_visibility_action = menu.addAction(
                    tr(
                        "menu.context.hide"
                        if owner.show_auxiliary_geometry
                        else "menu.context.unhide"
                    )
                )
        elif self._is_system_reference_plane(obj):
            return
        else:
            if obj.kind == EntityKind.SKETCH and not obj.locked:
                edit_sketch_action = menu.addAction(
                    resource_icon("sketch"),
                    tr("menu.context.edit_sketch"),
                )
                edit_sketch_action.setEnabled(
                    self._sketch_edit_entity_id is None
                    and self.point_constraint_dialog is None
                )
                menu.addSeparator()
            if obj.kind == EntityKind.CONTAINER:
                is_generic = obj.parameters.get("experimental_container") == "true"
                if is_generic:
                    create_point_action = menu.addAction(
                        tr("menu.context.create_point")
                    )
                    create_axis_action = menu.addAction(
                        tr("menu.context.create_axis")
                    )
                    create_axis_action.setEnabled(
                        obj.can_accept_entity(EntityKind.AXIS)
                    )
                    create_plane_action = menu.addAction(
                        tr("menu.context.create_plane")
                    )
                    create_plane_action.setEnabled(
                        obj.can_accept_entity(EntityKind.PLANE)
                    )
                    create_sketch_action = menu.addAction(
                        tr("menu.context.create_sketch")
                    )
                    create_sketch_action.setEnabled(
                        obj.can_accept_entity(
                            EntityKind.SKETCH,
                            SketchRole.PROFILE,
                        )
                    )
                if not obj.locked:
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                    delete_action = menu.addAction(
                        self._delete_container_label(obj)
                    )
            elif not obj.locked:
                if obj.kind in SOLID_KINDS:
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                elif obj.kind in (
                    EntityKind.POINT,
                    EntityKind.AXIS,
                    EntityKind.PLANE,
                    EntityKind.SKETCH,
                ):
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                delete_action = menu.addAction(tr("menu.context.delete_entity"))
            operation_target = self._operation_target(obj)
            if operation_target is not None:
                operation_menu = menu.addMenu(tr("menu.context.operation"))
                add_action = operation_menu.addAction(tr("menu.context.operation.add"))
                subtract_action = operation_menu.addAction(
                    tr("menu.context.operation.subtract")
                )
                add_action.setCheckable(True)
                subtract_action.setCheckable(True)
                add_action.setChecked(operation_target.combine_mode == CombineMode.ADD)
                subtract_action.setChecked(
                    operation_target.combine_mode == CombineMode.SUBTRACT
                )
            if not obj.locked:
                if obj.kind == EntityKind.CONTAINER or obj.kind == EntityKind.BODY or obj.kind in SOLID_KINDS:
                    menu.addSeparator()
                    suppress_action = menu.addAction(
                        tr(
                            "menu.context.resume"
                            if obj.suppressed
                            else "menu.context.suppress"
                        )
                    )

        if menu.isEmpty():
            return

        action = menu.exec(self.tree.viewport().mapToGlobal(position))

        if attach_action is not None and action == attach_action and obj is not None:
            self._begin_plane_attachment(obj.entity_id)
        elif create_action is not None and action == create_action:
            self.create_new_container()
        elif (
            create_point_action is not None
            and action == create_point_action
            and obj is not None
        ):
            self._create_generic_entity(obj, EntityKind.POINT)
        elif (
            create_axis_action is not None
            and action == create_axis_action
            and obj is not None
        ):
            self.create_datum_axis(obj.entity_id)
        elif (
            create_plane_action is not None
            and action == create_plane_action
            and obj is not None
        ):
            self._create_generic_entity(obj, EntityKind.PLANE)
        elif (
            create_sketch_action is not None
            and action == create_sketch_action
            and obj is not None
        ):
            if obj.parameters.get("experimental_container") == "true":
                self._create_generic_entity(obj, EntityKind.SKETCH)
            else:
                self.create_sketch(obj.entity_id)
        elif (
            action in create_sketch_actions
            and obj is not None
        ):
            role = create_sketch_actions[action]
            if obj.kind == EntityKind.CONTAINER:
                self.create_sketch(obj.entity_id, role)
            else:
                self.create_sketch_on_plane(obj.entity_id, role)
        elif (
            edit_sketch_action is not None
            and action == edit_sketch_action
            and obj is not None
        ):
            self._enter_sketch_edit(obj.entity_id)
        elif (
            edit_values_action is not None
            and action == edit_values_action
            and obj is not None
        ):
            self._show_edit_overlays(
                obj,
                QPoint(
                    self.native_viewer.width() // 2,
                    self.native_viewer.height() // 2,
                ),
            )
        elif (
            properties_action is not None
            and action == properties_action
            and obj is not None
        ):
            self._activate_object_for_editing(obj)
            self.show_properties(self._selected_object() or obj)
        elif delete_action is not None and action == delete_action and obj is not None:
            self.delete_container(obj.entity_id)
        elif (
            suppress_action is not None
            and action == suppress_action
            and obj is not None
        ):
            self._set_object_suppressed(obj, not obj.suppressed)
        elif body_suppress_action is not None and action == body_suppress_action:
            self._set_body_suppressed(not self.document.body_is_suppressed())
        elif (
            auxiliary_visibility_action is not None
            and action == auxiliary_visibility_action
        ):
            owner = (
                self.document.find_parent(obj.entity_id)
                if obj is not None
                else None
            )
            if owner is not None and owner.kind == EntityKind.CONTAINER:
                owner.show_auxiliary_geometry = not owner.show_auxiliary_geometry
                self.rebuild_view(fit=False)
        elif obj is not None and action in (add_action, subtract_action):
            target = self._operation_target(obj)
            if target is not None:
                target.combine_mode = (
                    CombineMode.ADD if action == add_action else CombineMode.SUBTRACT
                )
                self._populate_tree()
                self._select_tree_object(obj.entity_id)
                self.rebuild_view(fit=False)

    def _show_native_viewer_context_menu(self, position: QPoint) -> None:
        if self.native_viewer.consume_context_menu_suppression():
            return
        if self.document is None or not self.view_selection_enabled:
            return
        if self._sketch_reference_mode:
            candidates = self.native_viewer.topology_candidates_at(
                QPointF(position)
            )
            if not candidates:
                self._view_candidate_cycle_ids = ()
                self._view_candidate_cycle_index = -1
                self.native_viewer._cycled_topology_candidate = None
                self.native_viewer._clear_topology_hover()
                return
            cycle_ids = tuple(
                f"{kind}:{owner_id}:{element_index}"
                for kind, owner_id, element_index in candidates
            )
            if cycle_ids != self._view_candidate_cycle_ids:
                self._view_candidate_cycle_index = 0
            else:
                self._view_candidate_cycle_index = (
                    self._view_candidate_cycle_index + 1
                ) % len(candidates)
            self._view_candidate_cycle_ids = cycle_ids
            self.native_viewer.preview_topology_candidate(
                candidates[self._view_candidate_cycle_index]
            )
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_container",
                    rank=self._view_candidate_cycle_index + 1,
                )
            )
            return
        if self._normal_view_selection_active:
            scene = self._native_viewer_scene
            if scene is None:
                return
            candidates: list[tuple[str, str, int]] = []
            for kind, owner_id, element_index in (
                self.native_viewer.topology_candidates_at(
                    QPointF(position)
                )
            ):
                if kind != "face":
                    continue
                shape = scene.resolve_topology(
                    owner_id,
                    "face",
                    element_index,
                )
                if shape is None:
                    continue
                adaptor = BRepAdaptor_Surface(shape)
                if adaptor.GetType() == GeomAbs_Plane:
                    candidates.append(
                        (kind, owner_id, element_index)
                    )
            candidates = list(dict.fromkeys(candidates))
            if not candidates:
                self._view_candidate_cycle_ids = ()
                self._view_candidate_cycle_index = -1
                self.native_viewer._cycled_topology_candidate = None
                self.native_viewer._clear_topology_hover()
                return
            cycle_ids = tuple(
                f"{kind}:{owner_id}:{element_index}"
                for kind, owner_id, element_index in candidates
            )
            if cycle_ids != self._view_candidate_cycle_ids:
                self._view_candidate_cycle_index = 0
            else:
                self._view_candidate_cycle_index = (
                    self._view_candidate_cycle_index + 1
                ) % len(candidates)
            self._view_candidate_cycle_ids = cycle_ids
            self.native_viewer.preview_topology_candidate(
                candidates[self._view_candidate_cycle_index]
            )
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_container",
                    rank=self._view_candidate_cycle_index + 1,
                )
            )
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        ):
            candidates = self.native_viewer.topology_candidates_at(
                QPointF(position)
            )
            if not candidates:
                return
            cycle_ids = tuple(
                f"{kind}:{owner_id}:{element_index}"
                for kind, owner_id, element_index in candidates
            )
            if cycle_ids != self._history_source_cycle_ids:
                self._history_source_cycle_index = 0
            else:
                self._history_source_cycle_index = (
                    self._history_source_cycle_index + 1
                ) % len(candidates)
            self._history_source_cycle_ids = cycle_ids
            self._history_source_cycle_active = True
            self.native_viewer.preview_topology_candidate(
                candidates[self._history_source_cycle_index]
            )
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_container",
                    rank=self._history_source_cycle_index + 1,
                )
            )
            return
        if self.native_viewer._interaction_mode == "object":
            selected = self._selected_object()
            if self._view_selection_confirmed and selected is not None:
                self._show_selected_view_context_menu(
                    selected,
                    self.native_viewer.mapToGlobal(position),
                )
                return
            candidates = list(
                self.native_viewer.selection_candidates_at(
                    QPointF(position)
                )
            )
            source_meshes: dict[str, ViewerMesh] = {}
            if (
                "object",
                self.document.root.entity_id,
                0,
            ) in candidates:
                insert_at = candidates.index(
                    ("object", self.document.root.entity_id, 0)
                ) + 1
                for source in self.document.history_objects_at(
                    self._definition_history_boundary()
                ):
                    source_shape = self.document.build_standalone_shape(source)
                    if source_shape is None:
                        continue
                    source_mesh = triangulate_shape(
                        source_shape,
                        owner_id=source.entity_id,
                    )
                    if self.native_viewer.mesh_is_under_cursor(
                        source_mesh,
                        QPointF(position),
                    ):
                        candidates.insert(
                            insert_at,
                            ("object", source.entity_id, 0),
                        )
                        source_meshes[source.entity_id] = source_mesh
                        insert_at += 1
                candidates.remove(
                    ("object", self.document.root.entity_id, 0)
                )
                candidates.append(
                    ("object", self.document.root.entity_id, 0)
                )
            if not candidates:
                self._clear_view_selection()
                return
            cycle_ids = tuple(
                f"{kind}:{owner_id}:{element_index}"
                for kind, owner_id, element_index in candidates
            )
            if (
                not self._history_source_cycle_active
                or cycle_ids != self._history_source_cycle_ids
            ):
                self._history_source_cycle_index = 0
            else:
                self._history_source_cycle_index = (
                    self._history_source_cycle_index + 1
                ) % len(cycle_ids)
            self._history_source_cycle_ids = cycle_ids
            self._history_source_cycle_active = True
            kind, selected_owner_id, element_index = candidates[
                self._history_source_cycle_index
            ]
            self.native_viewer._clear_topology_hover()
            self.native_viewer._set_hovered_object(None)
            self.native_viewer.set_object_overlay(None)
            if kind == "object":
                if selected_owner_id == self.document.root.entity_id:
                    bodies = self.document.root.body_children()
                    tree_object_id = (
                        bodies[-1].entity_id
                        if bodies
                        else self.document.root.entity_id
                    )
                    selected_shape = self.document.build_shape_at(
                        self._definition_history_boundary()
                    )
                    selected_mesh = (
                        triangulate_shape(
                            selected_shape,
                            owner_id=selected_owner_id,
                        )
                        if selected_shape is not None
                        else None
                    )
                else:
                    tree_object_id = selected_owner_id
                    selected_mesh = source_meshes.get(selected_owner_id)
                self._select_native_tree_object(tree_object_id)
                self._view_selection_confirmed = False
                self.native_viewer._selected_object_id = None
                self.native_viewer._hovered_object_id = None
                selected_object = self.document.find_entity(
                    selected_owner_id
                )
                if (
                    selected_object is not None
                    and selected_object.kind == EntityKind.SKETCH
                ):
                    self.native_viewer._set_hovered_object(
                        selected_owner_id
                    )
                else:
                    self.native_viewer.set_object_overlay(
                        selected_mesh,
                        anchor=self._native_object_origin(selected_object),
                    )
            else:
                self._select_native_tree_object(selected_owner_id)
                self._view_selection_confirmed = False
                {
                    "point": self.native_viewer._set_hovered_point,
                    "edge": self.native_viewer._set_hovered_edge,
                    "plane": self.native_viewer._set_hovered_plane,
                }[kind](
                    (selected_owner_id, element_index)
                )
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_container",
                    rank=self._history_source_cycle_index + 1,
                )
            )
            self.native_viewer.set_selection_preview_pending(True)
            return

        detected: tuple[str, tuple[str, int]] | None = None
        for kind, key in (
            ("point", self.native_viewer._hovered_point),
            ("edge", self.native_viewer._hovered_edge),
            ("plane", self.native_viewer._hovered_plane),
            ("face", self.native_viewer._hovered_face),
        ):
            if key is not None:
                detected = kind, key
                break
        if detected is None:
            self._clear_view_selection()
            return
        kind, (owner_id, element_index) = detected
        if (
            owner_id == self.document.root.entity_id
            and kind in ("edge", "face")
        ):
            source_ids = tuple(
                obj.entity_id
                for obj in self.document.history_objects_at(
                    self._definition_history_boundary()
                )
            )
            if not source_ids:
                return
            current_id = self.selected_object_id
            self._history_source_cycle_ids = source_ids
            self._history_source_cycle_index = (
                (source_ids.index(current_id) + 1) % len(source_ids)
                if current_id in source_ids
                else 0
            )
            self.native_viewer._set_selected_edge(None)
            self.native_viewer._set_selected_face(None)
            self._select_native_tree_object(
                source_ids[self._history_source_cycle_index]
            )
            source = self.document.find_entity(
                source_ids[self._history_source_cycle_index]
            )
            source_shape = (
                self.document.build_standalone_shape(source)
                if source is not None
                else None
            )
            self.native_viewer.set_object_overlay(
                triangulate_shape(
                    source_shape,
                    owner_id=source.entity_id,
                )
                if source_shape is not None
                else None
            )
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_container",
                    rank=self._history_source_cycle_index + 1,
                )
            )
            return
        selected = self._selected_object()
        if (
            self._view_selection_confirmed
            and selected is not None
            and selected.entity_id == owner_id
            and selected.kind != EntityKind.PART
        ):
            self._show_selected_view_context_menu(
                selected,
                self.native_viewer.mapToGlobal(position),
            )
            return
        if kind == "face":
            self.native_viewer._set_selected_edge(None)
            self.native_viewer._set_selected_point(None)
            self.native_viewer._set_selected_plane(None)
            self.native_viewer._set_selected_face(
                (owner_id, element_index)
            )
        elif kind == "edge":
            self.native_viewer._set_selected_face(None)
            self.native_viewer._set_selected_point(None)
            self.native_viewer._set_selected_plane(None)
            self.native_viewer._set_selected_edge(
                (owner_id, element_index)
            )
        elif kind == "point":
            self.native_viewer._set_selected_edge(None)
            self.native_viewer._set_selected_face(None)
            self.native_viewer._set_selected_plane(None)
            self.native_viewer._set_selected_point(
                (owner_id, element_index)
            )
        else:
            self.native_viewer._set_selected_edge(None)
            self.native_viewer._set_selected_face(None)
            self.native_viewer._set_selected_point(None)
            self.native_viewer._set_selected_plane(
                (owner_id, element_index)
            )

    def _point_to_segment_pixel_distance(
        px: float,
        py: float,
        ax: float,
        ay: float,
        bx: float,
        by: float,
    ) -> float:
        dx = bx - ax
        dy = by - ay
        length_squared = dx * dx + dy * dy
        if length_squared <= 1e-12:
            return ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
        parameter = max(
            0.0,
            min(
                1.0,
                ((px - ax) * dx + (py - ay) * dy)
                / length_squared,
            ),
        )
        closest_x = ax + parameter * dx
        closest_y = ay + parameter * dy
        return (
            (px - closest_x) ** 2
            + (py - closest_y) ** 2
        ) ** 0.5

    def _show_selected_view_context_menu(self, obj: ZimaEntity, global_position) -> None:
        menu = QMenu(self)
        if (
            self.selected_face is not None
            and self.view_selection_mode == ViewSelectionMode.FACE
        ):
            empty_action = menu.addAction(" ")
            empty_action.setEnabled(False)
            menu.exec(global_position)
            return

        attach_action = None
        create_axis_action = None
        create_sketch_action = None
        create_sketch_actions: dict[Any, SketchRole] = {}
        edit_sketch_action = None
        edit_values_action = None
        properties_action = None
        delete_action = None
        normal_view_action = None

        if self._is_system_reference_plane(obj):
            normal_view_action = menu.addAction(tr("menu.context.view_normal"))
            if self._is_object_reference_plane(obj):
                menu.addSeparator()
                attach_action = menu.addAction(tr("menu.context.attach_to_face"))
                create_sketch_actions = self._add_sketch_role_menu(menu, obj)
        elif obj.kind != EntityKind.PART:
            if obj.kind == EntityKind.CONTAINER:
                if obj.parameters.get("experimental_container") == "true":
                    create_axis_action = menu.addAction(
                        tr("menu.context.create_axis")
                    )
                    create_axis_action.setEnabled(
                        obj.can_accept_entity(EntityKind.AXIS)
                    )
                    create_sketch_action = menu.addAction(
                        tr("menu.context.create_sketch")
                    )
                    create_sketch_action.setEnabled(
                        obj.can_accept_entity(
                            EntityKind.SKETCH,
                            SketchRole.PROFILE,
                        )
                    )
                if not obj.locked:
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                    delete_action = menu.addAction(
                        self._delete_container_label(obj)
                    )
            elif not obj.locked:
                if obj.kind == EntityKind.SKETCH:
                    edit_sketch_action = menu.addAction(
                        resource_icon("sketch"),
                        tr("menu.context.edit_sketch"),
                    )
                    edit_sketch_action.setEnabled(
                        self._sketch_edit_entity_id is None
                        and self.point_constraint_dialog is None
                    )
                elif obj.kind in SOLID_KINDS:
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                elif obj.kind == EntityKind.AXIS:
                    edit_values_action = menu.addAction(
                        tr("menu.context.edit_values")
                    )
                    edit_values_action.setEnabled(
                        self._first_editable_solid(obj) is not None
                    )
                    properties_action = menu.addAction(
                        tr("menu.context.properties")
                    )
                delete_action = menu.addAction(tr("menu.context.delete_entity"))

        if self.selected_face is not None and normal_view_action is None:
            normal_view_action = menu.addAction(tr("menu.context.view_normal"))

        if menu.isEmpty():
            return
        action = menu.exec(global_position)
        if attach_action is not None and action == attach_action:
            self._begin_plane_attachment(obj.entity_id)
        elif normal_view_action is not None and action == normal_view_action:
            if obj.kind == EntityKind.PLANE:
                self._view_normal_to_reference_plane(obj)
            else:
                self._view_normal_to_selected_face()
        elif create_axis_action is not None and action == create_axis_action:
            self.create_datum_axis(obj.entity_id)
        elif create_sketch_action is not None and action == create_sketch_action:
            self.create_sketch(obj.entity_id)
        elif action in create_sketch_actions:
            role = create_sketch_actions[action]
            if obj.kind == EntityKind.CONTAINER:
                self.create_sketch(obj.entity_id, role)
            else:
                self.create_sketch_on_plane(obj.entity_id, role)
        elif (
            edit_sketch_action is not None
            and action == edit_sketch_action
        ):
            self._enter_sketch_edit(obj.entity_id)
        elif edit_values_action is not None and action == edit_values_action:
            local_position = self.native_viewer.mapFromGlobal(global_position)
            self._show_edit_overlays(obj, local_position)
        elif properties_action is not None and action == properties_action:
            self._activate_object_for_editing(obj)
            self.show_properties(self._selected_object() or obj)
        elif delete_action is not None and action == delete_action:
            self.delete_container(obj.entity_id)

    @staticmethod
    def _delete_container_label(obj: ZimaEntity) -> str:
        key = {
            ContainerType.POINT: "point",
            ContainerType.AXIS: "axis",
            ContainerType.PLANE: "plane",
            ContainerType.SKETCH: "sketch",
            ContainerType.BOX: "box",
            ContainerType.SPHERE: "sphere",
            ContainerType.CYLINDER: "cylinder",
            ContainerType.CONE: "cone",
            ContainerType.PYRAMID: "pyramid",
            ContainerType.WEDGE: "wedge",
        }.get(obj.container_type)
        return tr(
            f"menu.context.delete_{key}"
            if key is not None
            else "menu.context.delete_container"
        )

    def _view_normal_to_reference_plane(self, plane: ZimaEntity) -> None:
        if self.document is None or plane.kind != EntityKind.PLANE:
            return
        owner = self.document.find_owning_object(plane.entity_id)
        local_normal = {
            "xy": (0.0, 0.0, 1.0),
            "yz": (1.0, 0.0, 0.0),
            "xz": (0.0, 1.0, 0.0),
        }.get(str(plane.parameters.get("plane", "xy")), (0.0, 0.0, 1.0))
        transform = self._world_transform_for_object(owner)
        normal = tuple(
            sum(transform[row][column] * local_normal[column] for column in range(3))
            for row in range(3)
        )
        self._set_view_normal(normal)

    def _plane_entity_from_view_key(
        self,
        owner_id: str,
        plane_index: int,
    ) -> ZimaEntity | None:
        if self.document is None:
            return None
        owner = self.document.find_entity(owner_id)
        if owner is None:
            return None
        if owner.kind == EntityKind.PLANE:
            return owner
        if owner.kind != EntityKind.ORIGIN:
            return None
        plane_name = {1: "xy", 2: "yz", 3: "xz"}.get(plane_index)
        return next(
            (
                child
                for child in owner.children
                if child.kind == EntityKind.PLANE
                and child.parameters.get("plane") == plane_name
            ),
            None,
        )

    def _toggle_normal_view_selection(self, active: bool) -> None:
        if active and self.document is None:
            self.normal_view_action.setChecked(False)
            return
        self._normal_view_selection_active = active
        self._view_candidate_cycle_ids = ()
        self._view_candidate_cycle_index = -1
        self.native_viewer._cycled_topology_candidate = None
        self.cancel_normal_view_action.setEnabled(active)
        self.selection_filter_combo.setEnabled(
            self.view_selection_enabled and not active
        )
        if active:
            if not self.view_selection_enabled:
                self.view_selection_action.setChecked(True)
            self.native_viewer.set_outline_face_highlights(True)
            self.native_viewer.set_selection_filter("normal")
            self.native_viewer.set_interaction_mode("topology")
            self.native_viewer._clear_topology_hover()
            self.native_viewer._clear_topology_selection()
            self.statusBar().showMessage(
                tr("selection.status.normal_view")
            )
            self.native_viewer.setFocus()
        else:
            point_dialog_active = (
                self.point_constraint_dialog is not None
                and self.point_constraint_dialog.isVisible()
            )
            self.native_viewer.set_outline_face_highlights(
                point_dialog_active
            )
            self.rebuild_view(fit=False, rebuild_geometry=False)
            self.statusBar().clearMessage()

    def _cancel_normal_view_selection(self) -> None:
        if self._normal_view_selection_active:
            self.normal_view_action.setChecked(False)

    def _view_normal_to_selected_face(self) -> None:
        if self.selected_face is None:
            return
        adaptor = BRepAdaptor_Surface(self.selected_face)
        if adaptor.GetType() != GeomAbs_Plane:
            return
        direction = adaptor.Plane().Axis().Direction()
        sign = -1.0 if self.selected_face.Orientation() == TopAbs_REVERSED else 1.0
        self._set_view_normal(
            (sign * direction.X(), sign * direction.Y(), sign * direction.Z())
        )

    def _world_transform_for_object(self, obj: ZimaEntity | None):
        chain: list[ZimaEntity] = []
        while obj is not None and obj.kind not in (EntityKind.PART, EntityKind.ORIGIN):
            chain.append(obj)
            obj = self.document.find_parent(obj.entity_id) if self.document else None
        transform = identity_transform()
        for item in reversed(chain):
            transform = multiply_transforms(
                transform,
                coordinate_system_transform(item.coordinate_system),
            )
        return transform

    def _set_view_normal(self, normal: tuple[float, float, float]) -> None:
        nx, ny, nz = normal
        length = (nx * nx + ny * ny + nz * nz) ** 0.5
        if length <= 1e-12:
            return
        nx, ny, nz = nx / length, ny / length, nz / length
        self.native_viewer.animate_view_normal((-nx, -ny, -nz))
        self._clear_view_selection()

    def _clear_view_selection(self) -> None:
        self.selected_object_id = None
        self.selected_face = None
        self.selected_face_object_id = None
        self._view_selection_confirmed = False
        self._hovered_coordinate_object_id = None
        self.tree.blockSignals(True)
        self.tree.clearSelection()
        self.tree.setCurrentItem(None)
        self.tree.blockSignals(False)
        if not hasattr(self, "_viewer_initialized"):
            return
        self.native_viewer.set_selection_preview_pending(False)
        self.native_viewer._set_selected_edge(None)
        self.native_viewer._set_selected_face(None)
        self.native_viewer._set_selected_point(None)
        self.native_viewer._set_selected_plane(None)
        self.native_viewer.update()

    def _on_view_selection_preview_confirmed(self) -> None:
        self._view_selection_confirmed = self.selected_object_id is not None
        selected = self._selected_object()
        if selected is not None and selected.kind == EntityKind.SKETCH:
            self.native_viewer._set_selected_object(selected.entity_id)
        self._history_source_cycle_active = False

    def _begin_plane_attachment(self, plane_id: str) -> None:
        self._pending_attachment_plane_id = plane_id
        self._view_selection_confirmed = False
        self.statusBar().showMessage(tr("message.attachment.select_face"))

    def _finish_plane_attachment(
        self,
        source_plane_id: str,
        target_object_id: str,
        face_role: str,
    ) -> None:
        if self.document is None:
            return
        source_plane = self.document.find_entity(source_plane_id)
        source_object = self.document.find_owning_object(source_plane_id)
        target = self.document.find_entity(target_object_id)
        if (
            source_plane is None
            or source_object is None
            or target is None
            or source_plane.kind != EntityKind.PLANE
            or source_object.kind != EntityKind.CONTAINER
        ):
            return
        dialog = PlaneAttachmentDialog(
            f"{source_object.name} / {source_plane.name}",
            target.name,
            face_role,
            self,
        )
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        source_object.attachment = PlaneOnFaceAttachment(
            source_plane=str(source_plane.parameters.get("plane", "xy")),
            target_object_id=target.entity_id,
            target_face_role=face_role,
            primary_axis=dialog.primary_axis,
            secondary_axis=dialog.secondary_axis,
            switch_angle=45.0,
            flip_normal=dialog.flip_checkbox.isChecked(),
        )
        self.document.resolve_attachments()
        self.selected_object_id = source_object.entity_id
        self._populate_tree()
        self._select_tree_object(source_object.entity_id)
        self.rebuild_view(fit=False)

    def create_new_container(self) -> None:
        if self.document is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = ContainerPropertiesDialog(
            self._solve_point_constraints,
            self,
            suggested_name=self.document.next_container_name("Container"),
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.createContainerRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, container_type: self._apply_new_experimental_container(
                dialog, references, fallback, name, show_internal,
                show_auxiliary, rotation, container_type,
            )
        )
        dialog.updateContainerRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, container_type: self._update_experimental_container(
                dialog.point_object, references, fallback, name,
                show_internal, show_auxiliary, rotation, container_type,
            )
            if dialog.point_object is not None
            else None
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _operation_target(self, obj: ZimaEntity) -> ZimaEntity | None:
        if self.document is None:
            return None
        if obj.kind in SOLID_KINDS:
            return obj
        if obj.kind != EntityKind.CONTAINER:
            return None
        solids = [
            child
            for child in obj.children
            if not child.locked and child.kind in SOLID_KINDS
        ]
        return solids[0] if len(solids) == 1 else None

    def create_sketch_on_plane(
        self,
        plane_id: str,
        role: SketchRole = SketchRole.PROFILE,
    ) -> None:
        if self.document is None:
            return
        plane = self.document.find_entity(plane_id)
        if plane is not None and plane.kind == EntityKind.PLANE:
            self._create_sketch_definition(plane)

    def _create_generic_entity(
        self,
        owner: ZimaEntity,
        kind: EntityKind,
    ) -> None:
        if self.document is None:
            return
        entity = None
        if kind == EntityKind.POINT:
            entity = self.document.create_point(owner.entity_id)
        elif kind == EntityKind.AXIS:
            entity = self.document.create_datum_axis(owner.entity_id)
        elif kind == EntityKind.PLANE:
            entity = self.document.create_datum_plane(owner.entity_id)
        elif kind == EntityKind.SKETCH:
            entity = self.document.create_sketch(
                owner.entity_id,
                plane="xy",
                role=SketchRole.PROFILE,
                name_prefix=tr("container.type.sketch"),
            )
        if entity is None:
            self._show_entity_limit_message(owner.entity_id, kind)
            return
        owner.show_internal_entities = True
        self._populate_tree()
        self._select_tree_object(entity.entity_id)
        self.rebuild_view(fit=False)

    def create_sketch(
        self,
        parent_id: str,
        role: SketchRole = SketchRole.PROFILE,
    ) -> None:
        self._create_sketch_definition()

    def create_cube(self, source_id: str) -> None:
        if self.document is None:
            return

        cube = self.document.create_cube(source_id)
        if cube is None:
            self._show_entity_limit_message(source_id, EntityKind.BOX)
            return

        self._populate_tree()
        self._select_tree_object(cube.entity_id)
        self.rebuild_view(fit=False)

    def create_wedge(self, source_id: str) -> None:
        if self.document is None:
            return

        wedge = self.document.create_wedge(source_id)
        if wedge is None:
            self._show_entity_limit_message(source_id, EntityKind.WEDGE)
            return

        self._populate_tree()
        self._select_tree_object(wedge.entity_id)
        self.rebuild_view(fit=False)

    def create_datum_axis(self, parent_id: str) -> None:
        if self.document is None:
            return
        axis = self.document.create_datum_axis(parent_id)
        if axis is None:
            self._show_entity_limit_message(parent_id, EntityKind.AXIS)
            return
        self._populate_tree()
        self._select_tree_object(parent_id)
        self.rebuild_view(fit=False)
        self.show_axis_properties(axis)

    def delete_container(self, entity_id: str) -> None:
        if self.document is None:
            return

        if self.document.delete_container(entity_id):
            self._mark_model_for_regeneration()
            self.selected_object_id = None
            self._populate_tree()
            self.rebuild_view(fit=False)

    def _set_object_suppressed(self, obj: ZimaEntity, suppressed: bool) -> None:
        if (
            obj.kind != EntityKind.CONTAINER
            and obj.kind != EntityKind.BODY
            and obj.kind not in SOLID_KINDS
        ):
            return
        obj.suppressed = suppressed
        self._mark_model_for_regeneration()
        self.selected_face = None
        self.selected_face_object_id = None
        self._populate_tree()
        self._select_tree_object(obj.entity_id)
        self.rebuild_view(fit=False)

    def _set_body_suppressed(self, suppressed: bool) -> None:
        if self.document is None:
            return
        self.document.set_body_suppressed(suppressed)
        self.selected_object_id = self.document.root.entity_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._populate_tree()
        self._select_tree_object(self.document.root.entity_id)
        self.rebuild_view(fit=False, rebuild_geometry=True)

    def _activate_object_for_editing(self, obj: ZimaEntity) -> ZimaEntity:
        """Synchronize the edit target between the tree and the 3D view."""
        target = obj
        if (
            self.document is not None
            and obj.tree_exposure == TreeExposure.INTERNAL
        ):
            owner = self.document.find_owning_object(obj.entity_id)
            if owner is not None and not owner.show_internal_entities:
                target = owner
        self.selected_object_id = target.entity_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._view_selection_confirmed = True
        self._populate_tree()
        self._select_tree_object(target.entity_id)
        self.rebuild_view(fit=False, rebuild_geometry=False)
        return target

    def show_object_properties(self, obj: ZimaEntity) -> None:
        if obj.kind != EntityKind.CONTAINER:
            return
        point_entity = self._user_point_entity(obj)
        if point_entity is not None:
            self._edit_point_object(obj, point_entity)
            return
        axis_entities = [
            child
            for child in obj.children
            if child.kind == EntityKind.AXIS and not child.locked
        ]
        if len(axis_entities) == 1:
            self.show_axis_properties(axis_entities[0])
            return
        plane_entities = [
            child
            for child in obj.children
            if child.kind == EntityKind.PLANE and not child.locked
        ]
        if len(plane_entities) == 1:
            self.show_plane_properties(plane_entities[0])
            return
        solid_entities = [
            child for child in obj.children if child.kind in SOLID_KINDS
        ]
        if len(solid_entities) == 1:
            self._edit_solid_object(obj, solid_entities[0])
            return
        sketch_entities = [
            child for child in obj.children if child.kind == EntityKind.SKETCH
        ]
        if len(sketch_entities) == 1:
            self._edit_sketch_object(obj, sketch_entities[0])
            return
        if obj.parameters.get("experimental_container") == "true":
            self._edit_generic_object(obj)
            return

        dialog = ContainerSummaryDialog(obj, self.document, self)
        dialog.applied.connect(lambda: self._refresh_object_properties(obj))
        self._begin_definition_edit(obj)
        try:
            if (
                dialog.exec() == QDialog.DialogCode.Accepted
                and dialog.apply_to_object()
            ):
                self._refresh_object_properties(obj)
        finally:
            self._end_definition_edit()

    def _edit_generic_object(self, obj: ZimaEntity) -> None:
        if self.document is None or self.point_constraint_dialog is not None:
            return
        dialog = ContainerPropertiesDialog(
            self._solve_point_constraints,
            self,
            edited_object=obj,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updateContainerRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, container_type: self._update_experimental_container(
                obj, references, fallback, name, show_internal,
                show_auxiliary, rotation, container_type,
            )
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def show_axis_properties(self, axis: ZimaEntity) -> None:
        if axis.kind != EntityKind.AXIS or axis.locked:
            return
        if self.document is None:
            return
        owner = self.document.find_owning_object(axis.entity_id)
        if owner is None:
            return
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        ):
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = AxisConstraintDialog(
            self._solve_point_constraints,
            self,
            axis_object=owner,
            axis_entity=axis,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updateAxisRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, direction, length: self._update_axis_object(
                owner,
                axis,
                references,
                fallback,
                name,
                show_internal,
                show_auxiliary,
                rotation,
                direction,
                length,
            )
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def show_plane_properties(self, plane: ZimaEntity) -> None:
        if plane.kind != EntityKind.PLANE or plane.locked or self.document is None:
            return
        owner = self.document.find_owning_object(plane.entity_id)
        if owner is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = PlaneConstraintDialog(
            self._solve_point_constraints,
            self,
            plane_object=owner,
            plane_entity=plane,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updatePlaneRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, plane_kind, size: self._update_plane_object(
                owner, plane, references, fallback, name, show_internal,
                show_auxiliary, rotation, plane_kind, size,
            )
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def show_primitive_properties(self, primitive: ZimaEntity) -> None:
        if primitive.kind not in SOLID_KINDS:
            return
        if self.document is None:
            return
        owner = self.document.find_owning_object(primitive.entity_id)
        if owner is not None:
            self._edit_solid_object(owner, primitive)

    def _edit_solid_object(
        self,
        obj: ZimaEntity,
        solid: ZimaEntity,
    ) -> None:
        if self.document is None:
            return
        if self.point_constraint_dialog is not None:
            self.point_constraint_dialog.raise_()
            self.point_constraint_dialog.activateWindow()
            return
        dialog = SolidConstraintDialog(
            self._solve_point_constraints,
            solid.kind,
            self,
            solid_object=obj,
            solid_entity=solid,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updateSolidRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, parameters, operation: self._update_solid_object(
                obj, solid, references, fallback, name, show_internal,
                show_auxiliary, rotation, parameters, operation,
            )
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _edit_sketch_object(
        self,
        obj: ZimaEntity,
        sketch: ZimaEntity,
    ) -> None:
        if self.document is None or self.point_constraint_dialog is not None:
            return
        dialog = SketchConstraintDialog(
            self._solve_point_constraints,
            self,
            sketch_object=obj,
            sketch_entity=sketch,
            reference_exists_callback=lambda entity_id: (
                self.document is not None
                and self.document.find_entity(entity_id) is not None
            ),
            reference_kind_callback=lambda entity_id: (
                reference.kind
                if self.document is not None
                and (reference := self.document.find_entity(entity_id)) is not None
                else None
            ),
        )
        dialog.updateSketchRequested.connect(
            lambda references, fallback, name, show_internal, show_auxiliary,
            rotation, diameter: self._update_sketch_object(
                obj, sketch, references, fallback, name, show_internal,
                show_auxiliary, rotation, diameter,
            )
        )
        dialog.enterSketchRequested.connect(
            lambda: self._queue_sketch_edit(dialog)
        )
        dialog.referenceActivated.connect(self._activate_point_reference)
        dialog.definitionChanged.connect(
            lambda: self.rebuild_view(fit=False, rebuild_geometry=False)
        )
        dialog.finished.connect(self._point_constraint_dialog_finished)
        self.point_constraint_dialog = dialog
        self._show_properties_dialog(dialog)
        self.rebuild_view(fit=False, rebuild_geometry=False)

    def _queue_sketch_edit(self, dialog: SketchConstraintDialog) -> None:
        sketch = dialog.point_entity
        if sketch is None or sketch.kind != EntityKind.SKETCH:
            return
        sketch_id = sketch.entity_id
        QTimer.singleShot(
            0,
            lambda: self._enter_sketch_edit(sketch_id),
        )

    @staticmethod
    def _stored_sketch_entities(
        sketch: ZimaEntity,
    ) -> list[dict[str, Any]]:
        try:
            entities = json.loads(
                str(sketch.parameters.get("sketch_entities", "[]"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            return []
        return (
            [entity for entity in entities if isinstance(entity, dict)]
            if isinstance(entities, list)
            else []
        )

    @staticmethod
    def _stored_sketch_external_references(
        sketch: ZimaEntity,
    ) -> list[dict[str, Any]]:
        try:
            references = json.loads(
                str(sketch.parameters.get("external_references", "[]"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            return []
        return (
            [
                reference
                for reference in references
                if isinstance(reference, dict)
            ]
            if isinstance(references, list)
            else []
        )

    def _add_sketch_external_reference(
        self,
        source_kind: str,
        owner_id: str,
        element_index: int,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None or owner_id == sketch.entity_id:
            return
        descriptor = {
            "id": f"{source_kind}:{owner_id}:{element_index}",
            "source_kind": source_kind,
            "owner_id": owner_id,
            "element_index": element_index,
        }
        if owner_id == self.document.root.entity_id:
            descriptor["reference_scope"] = "history_result"
        geometry = self._project_sketch_external_reference(
            sketch,
            descriptor,
        )
        geometry = self._infinite_sketch_reference_geometry(geometry)
        if geometry is None:
            self.statusBar().showMessage(
                tr("sketch.status.reference_unsupported")
            )
            return
        references = self._stored_sketch_external_references(sketch)
        if any(
            reference.get("id") == descriptor["id"]
            for reference in references
        ):
            return
        descriptor["cached_geometry"] = geometry
        references.append(descriptor)
        sketch.parameters["external_references"] = json.dumps(
            references,
            ensure_ascii=False,
        )
        self._sketch_selected_external_reference_id = str(
            descriptor["id"]
        )
        self._sketch_selected_entity_id = None
        self._sketch_selected_reference = None
        self._mark_model_for_regeneration()
        self._refresh_sketch_overlay()
        self.statusBar().showMessage(
            tr("sketch.status.reference_added")
        )

    def _resolved_sketch_external_references(
        self,
        sketch: ZimaEntity,
    ) -> list[dict[str, Any]]:
        references = self._stored_sketch_external_references(sketch)
        changed = False
        resolved: list[dict[str, Any]] = []
        for reference in references:
            geometry = self._project_sketch_external_reference(
                sketch,
                reference,
            )
            geometry = self._infinite_sketch_reference_geometry(geometry)
            broken = geometry is None
            if geometry is None:
                geometry = self._infinite_sketch_reference_geometry(
                    reference.get("cached_geometry")
                    if isinstance(
                        reference.get("cached_geometry"),
                        dict,
                    )
                    else None
                )
            elif geometry != reference.get("cached_geometry"):
                reference["cached_geometry"] = geometry
                changed = True
            if not isinstance(geometry, dict):
                continue
            resolved.append(
                {
                    "id": reference.get("id", ""),
                    "geometry": geometry,
                    "broken": broken,
                    "selected": (
                        reference.get("id")
                        == self._sketch_selected_external_reference_id
                    ),
                }
            )
        if changed:
            sketch.parameters["external_references"] = json.dumps(
                references,
                ensure_ascii=False,
            )
        return resolved

    @staticmethod
    def _infinite_sketch_reference_geometry(
        geometry: dict[str, Any] | None,
    ) -> dict[str, Any] | None:
        if geometry is None:
            return None
        geometry_type = geometry.get("type")
        if geometry_type in ("line", "lines", "point"):
            return geometry

        def infinite_line(raw_points) -> dict[str, Any] | None:
            if not isinstance(raw_points, (list, tuple)):
                return None
            points = [
                (float(point[0]), float(point[1]))
                for point in raw_points
                if isinstance(point, (list, tuple)) and len(point) >= 2
            ]
            if len(points) < 2:
                return None
            first = points[0]
            second = max(
                points[1:],
                key=lambda point: (
                    (point[0] - first[0]) ** 2
                    + (point[1] - first[1]) ** 2
                ),
            )
            dx = second[0] - first[0]
            dy = second[1] - first[1]
            length = math.hypot(dx, dy)
            if length <= 1e-10:
                return None
            return {
                "type": "line",
                "point": [first[0], first[1]],
                "direction": [dx / length, dy / length],
            }

        if geometry_type == "polyline":
            return infinite_line(geometry.get("points", ()))
        if geometry_type == "polylines":
            lines = [
                line
                for raw_points in geometry.get("polylines", ())
                if (
                    line := infinite_line(raw_points)
                ) is not None
            ]
            return (
                {"type": "lines", "lines": lines}
                if lines
                else None
            )
        return None

    def _project_sketch_external_reference(
        self,
        sketch: ZimaEntity,
        descriptor: dict[str, Any],
    ) -> dict[str, Any] | None:
        if self.document is None:
            return None
        frame = self._sketch_frame(sketch)
        if frame is None:
            return None
        sketch_origin, x_axis, y_axis = frame
        sketch_normal = self._normalized_vector(
            self._cross_product(x_axis, y_axis)
        )

        def local_point(point) -> tuple[float, float]:
            delta = tuple(
                float(point[index]) - sketch_origin[index]
                for index in range(3)
            )
            return (
                sum(delta[index] * x_axis[index] for index in range(3)),
                sum(delta[index] * y_axis[index] for index in range(3)),
            )

        def projected_line(point, direction):
            local_origin = local_point(point)
            local_direction = (
                sum(direction[index] * x_axis[index] for index in range(3)),
                sum(direction[index] * y_axis[index] for index in range(3)),
            )
            length = math.hypot(*local_direction)
            if length <= 1e-10:
                return None
            return {
                "type": "line",
                "point": list(local_origin),
                "direction": [
                    local_direction[0] / length,
                    local_direction[1] / length,
                ],
            }

        source_kind = str(descriptor.get("source_kind", ""))
        owner_id = str(descriptor.get("owner_id", ""))
        if descriptor.get("reference_scope") == "history_result":
            owner_id = self.document.root.entity_id
        try:
            element_index = int(descriptor.get("element_index", 0))
        except (TypeError, ValueError):
            element_index = 0
        owner = self.document.find_entity(owner_id)
        if owner is None:
            return None

        coordinate_entity = None
        if source_kind == "plane":
            coordinate_entity = self._plane_entity_from_view_key(
                owner_id,
                element_index,
            )
        elif source_kind in ("axis", "edge") and owner.kind in (
            EntityKind.ORIGIN,
            EntityKind.AXIS,
        ):
            coordinate_entity = self._coordinate_entity_from_view_key(
                owner,
                EntityKind.AXIS,
                element_index,
            )
        elif source_kind == "point":
            coordinate_entity = self._coordinate_entity_from_view_key(
                owner,
                EntityKind.POINT,
                element_index,
            )

        if coordinate_entity is not None:
            if coordinate_entity.kind == EntityKind.POINT:
                return {
                    "type": "point",
                    "point": list(
                        local_point(
                            self._reference_origin(coordinate_entity)
                        )
                    ),
                }
            if coordinate_entity.kind == EntityKind.AXIS:
                direction = {
                    "x": (1.0, 0.0, 0.0),
                    "y": (0.0, 1.0, 0.0),
                    "z": (0.0, 0.0, 1.0),
                }.get(
                    str(coordinate_entity.parameters.get("axis", "z")),
                    (0.0, 0.0, 1.0),
                )
                return projected_line(
                    self._reference_origin(coordinate_entity),
                    self._reference_direction(
                        coordinate_entity,
                        direction,
                    ),
                )
            if coordinate_entity.kind == EntityKind.PLANE:
                normal = {
                    "xy": (0.0, 0.0, 1.0),
                    "yz": (1.0, 0.0, 0.0),
                    "xz": (0.0, 1.0, 0.0),
                }.get(
                    str(coordinate_entity.parameters.get("plane", "xy")),
                    (0.0, 0.0, 1.0),
                )
                return self._project_plane_intersection(
                    sketch_origin,
                    sketch_normal,
                    self._reference_origin(coordinate_entity),
                    self._reference_direction(coordinate_entity, normal),
                    local_point,
                    projected_line,
                )

        scene = self._native_viewer_scene
        if scene is None or source_kind not in ("face", "edge", "point"):
            return None
        topology_kind = (
            "vertex" if source_kind == "point" else source_kind
        )
        shape = scene.resolve_topology(
            owner_id,
            topology_kind,
            element_index,
        )
        if shape is None:
            return None
        if source_kind == "point":
            try:
                point = BRep_Tool.Pnt(shape)
            except (TypeError, RuntimeError):
                return None
            return {
                "type": "point",
                "point": list(
                    local_point((point.X(), point.Y(), point.Z()))
                ),
            }
        if source_kind == "face":
            polylines = []
            explorer = TopExp_Explorer(shape, TopAbs_EDGE)
            seen_edges = []
            while explorer.More():
                edge = explorer.Current()
                if not any(edge.IsSame(existing) for existing in seen_edges):
                    seen_edges.append(edge)
                    polyline = self._project_edge_polyline(
                        edge,
                        local_point,
                    )
                    if polyline is not None:
                        polylines.append(polyline)
                explorer.Next()
            return (
                {"type": "polylines", "polylines": polylines}
                if polylines
                else None
            )
        try:
            adaptor = BRepAdaptor_Curve(shape)
            if adaptor.GetType() == GeomAbs_Line:
                first_point = adaptor.Value(adaptor.FirstParameter())
                last_point = adaptor.Value(adaptor.LastParameter())
                return {
                    "type": "polyline",
                    "points": [
                        list(
                            local_point(
                                (
                                    first_point.X(),
                                    first_point.Y(),
                                    first_point.Z(),
                                )
                            )
                        ),
                        list(
                            local_point(
                                (
                                    last_point.X(),
                                    last_point.Y(),
                                    last_point.Z(),
                                )
                            )
                        ),
                    ],
                }
            points = self._project_edge_polyline(shape, local_point)
            return (
                {"type": "polyline", "points": points}
                if points is not None
                else None
            )
        except (AttributeError, RuntimeError):
            return None

    @staticmethod
    def _project_edge_polyline(edge, local_point):
        try:
            adaptor = BRepAdaptor_Curve(edge)
            first = adaptor.FirstParameter()
            last = adaptor.LastParameter()
            if not math.isfinite(first) or not math.isfinite(last):
                return None
            sample_count = 2 if adaptor.GetType() == GeomAbs_Line else 25
            return [
                list(
                    local_point(
                        (
                            point.X(),
                            point.Y(),
                            point.Z(),
                        )
                    )
                )
                for point in (
                    adaptor.Value(
                        first
                        + (last - first) * index
                        / max(1, sample_count - 1)
                    )
                    for index in range(sample_count)
                )
            ]
        except (AttributeError, RuntimeError):
            return None

    def _coordinate_entity_from_view_key(
        self,
        owner: ZimaEntity,
        kind: EntityKind,
        element_index: int,
    ) -> ZimaEntity | None:
        if owner.kind == kind:
            return owner
        if owner.kind != EntityKind.ORIGIN:
            return None
        if kind == EntityKind.POINT:
            return next(
                (
                    child
                    for child in owner.children
                    if child.kind == EntityKind.POINT
                ),
                None,
            )
        axis_name = {1: "x", 2: "y", 3: "z"}.get(element_index)
        return next(
            (
                child
                for child in owner.children
                if child.kind == kind
                and child.parameters.get("axis") == axis_name
            ),
            None,
        )

    def _project_plane_intersection(
        self,
        sketch_origin,
        sketch_normal,
        reference_origin,
        reference_normal,
        local_point,
        projected_line,
    ) -> dict[str, Any] | None:
        first_normal = self._normalized_vector(sketch_normal)
        second_normal = self._normalized_vector(reference_normal)
        direction = self._cross_product(first_normal, second_normal)
        denominator = sum(value * value for value in direction)
        if denominator <= 1e-12:
            return None
        first_distance = sum(
            first_normal[index] * sketch_origin[index]
            for index in range(3)
        )
        second_distance = sum(
            second_normal[index] * reference_origin[index]
            for index in range(3)
        )
        second_cross_direction = self._cross_product(
            second_normal,
            direction,
        )
        direction_cross_first = self._cross_product(
            direction,
            first_normal,
        )
        point = tuple(
            (
                first_distance * second_cross_direction[index]
                + second_distance * direction_cross_first[index]
            )
            / denominator
            for index in range(3)
        )
        return projected_line(point, direction)

    @staticmethod
    def _sketch_point_position(
        point: dict[str, Any],
    ) -> tuple[float, float]:
        if "x" in point or "y" in point:
            return (
                float(point.get("x", 0.0)),
                float(point.get("y", 0.0)),
            )
        legacy = point.get("points", [[0.0, 0.0]])
        if isinstance(legacy, list) and legacy and len(legacy[0]) >= 2:
            return float(legacy[0][0]), float(legacy[0][1])
        return 0.0, 0.0

    @staticmethod
    def _sketch_point_reference_ids(
        point: dict[str, Any],
    ) -> tuple[str, ...]:
        constraints = point.get("constraints", ())
        if not isinstance(constraints, list):
            return ()
        return tuple(
            str(constraint.get("reference_id", ""))
            for constraint in constraints
            if isinstance(constraint, dict)
            and constraint.get("type") == "point_on_reference"
            and str(constraint.get("reference_id", ""))
        )

    def _sketch_point_locked_coordinates(
        self,
        sketch: ZimaEntity,
        point: dict[str, Any],
    ) -> set[str]:
        locked: set[str] = set()
        constraints = point.get("constraints", [])
        if not isinstance(constraints, list):
            constraints = []
        for constraint in constraints:
            if (
                isinstance(constraint, dict)
                and constraint.get("type") == "coincident"
            ):
                locked.update(("x", "y"))
                continue
            if (
                not isinstance(constraint, dict)
                or constraint.get("type") != "point_on_reference"
            ):
                continue
            reference_id = str(constraint.get("reference_id", ""))
            if reference_id == "sketch_origin":
                locked.update(("x", "y"))
            elif reference_id == "sketch_axis:x":
                locked.add("y")
            elif reference_id == "sketch_axis:y":
                locked.add("x")
            else:
                resolved = next(
                    (
                        reference
                        for reference in
                        self._resolved_sketch_external_references(sketch)
                        if str(reference.get("id", "")) == reference_id
                    ),
                    None,
                )
                geometry = (
                    resolved.get("geometry")
                    if isinstance(resolved, dict)
                    else None
                )
                if (
                    isinstance(geometry, dict)
                    and geometry.get("type") == "point"
                ):
                    locked.update(("x", "y"))
                line = self._sketch_reference_constraint_line(
                    geometry,
                    constraint,
                    self._sketch_point_position(point),
                )
                if line is not None:
                    direction = line.get("direction", ())
                    if (
                        isinstance(direction, (list, tuple))
                        and len(direction) >= 2
                    ):
                        dx = abs(float(direction[0]))
                        dy = abs(float(direction[1]))
                        scale = max(dx, dy, 1.0e-12)
                        if dy <= scale * 1.0e-8:
                            locked.add("y")
                        elif dx <= scale * 1.0e-8:
                            locked.add("x")
        point_id = str(point.get("id", ""))
        if point_id:
            for geometry in self._stored_sketch_entities(sketch):
                point_ids = geometry.get("point_ids", ())
                geometry_constraints = geometry.get("constraints", ())
                if (
                    not isinstance(point_ids, list)
                    or len(point_ids) < 2
                    or str(point_ids[1]) != point_id
                    or not isinstance(geometry_constraints, list)
                ):
                    continue
                for constraint in geometry_constraints:
                    if not isinstance(constraint, dict):
                        continue
                    if constraint.get("type") == "horizontal":
                        locked.add("y")
                    elif constraint.get("type") == "vertical":
                        locked.add("x")
        return locked

    def _apply_sketch_geometry_constraints(
        self,
        entities: list[dict[str, Any]],
    ) -> None:
        points = {
            str(entity.get("id", "")): entity
            for entity in entities
            if entity.get("type") == "point"
        }
        for geometry in entities:
            point_ids = geometry.get("point_ids", ())
            constraints = geometry.get("constraints", ())
            if (
                not isinstance(point_ids, list)
                or len(point_ids) < 2
                or not isinstance(constraints, list)
            ):
                continue
            first = points.get(str(point_ids[0]))
            second = points.get(str(point_ids[1]))
            if first is None or second is None:
                continue
            for constraint in constraints:
                if not isinstance(constraint, dict):
                    continue
                if constraint.get("type") == "horizontal":
                    second["y"] = float(first.get("y", 0.0))
                elif constraint.get("type") == "vertical":
                    second["x"] = float(first.get("x", 0.0))

    @staticmethod
    def _apply_sketch_coincident_constraints(
        entities: list[dict[str, Any]],
    ) -> None:
        points = {
            str(entity.get("id", "")): entity
            for entity in entities
            if entity.get("type") == "point"
        }
        # Resolve chains such as p3 → p2 → p1 without merging point IDs.
        for _pass in range(max(1, len(points))):
            changed = False
            for point in points.values():
                constraints = point.get("constraints", ())
                if not isinstance(constraints, list):
                    continue
                for constraint in constraints:
                    if (
                        not isinstance(constraint, dict)
                        or constraint.get("type") != "coincident"
                    ):
                        continue
                    target = points.get(
                        str(constraint.get("point_id", ""))
                    )
                    if target is None:
                        continue
                    target_x, target_y = (
                        float(target.get("x", 0.0)),
                        float(target.get("y", 0.0)),
                    )
                    if (
                        float(point.get("x", 0.0)) != target_x
                        or float(point.get("y", 0.0)) != target_y
                    ):
                        point["x"] = target_x
                        point["y"] = target_y
                        changed = True
            if not changed:
                break

    @staticmethod
    def _sketch_reference_constraint_line(
        geometry: Any,
        constraint: dict[str, Any],
        point: tuple[float, float],
    ) -> dict[str, Any] | None:
        if not isinstance(geometry, dict):
            return None
        if geometry.get("type") == "line":
            return geometry
        if geometry.get("type") != "lines":
            return None
        raw_lines = geometry.get("lines", ())
        if not isinstance(raw_lines, (list, tuple)):
            return None
        try:
            line_index = int(constraint.get("geometry_index", -1))
        except (TypeError, ValueError):
            line_index = -1
        if (
            0 <= line_index < len(raw_lines)
            and isinstance(raw_lines[line_index], dict)
        ):
            return raw_lines[line_index]

        def squared_distance(raw_line) -> float:
            if not isinstance(raw_line, dict):
                return math.inf
            raw_point = raw_line.get("point", ())
            direction = raw_line.get("direction", ())
            if (
                not isinstance(raw_point, (list, tuple))
                or not isinstance(direction, (list, tuple))
                or len(raw_point) < 2
                or len(direction) < 2
            ):
                return math.inf
            px, py = float(raw_point[0]), float(raw_point[1])
            dx, dy = float(direction[0]), float(direction[1])
            denominator = dx * dx + dy * dy
            if denominator <= 1.0e-18:
                return math.inf
            factor = (
                (point[0] - px) * dx + (point[1] - py) * dy
            ) / denominator
            return (
                point[0] - (px + factor * dx)
            ) ** 2 + (
                point[1] - (py + factor * dy)
            ) ** 2

        candidates = [
            raw_line
            for raw_line in raw_lines
            if isinstance(raw_line, dict)
        ]
        return (
            min(candidates, key=squared_distance)
            if candidates
            else None
        )

    def _apply_sketch_point_reference_constraints(
        self,
        sketch: ZimaEntity,
        point: dict[str, Any],
    ) -> None:
        x, y = self._sketch_point_position(point)
        resolved_by_id = {
            str(reference.get("id", "")): reference
            for reference in self._resolved_sketch_external_references(
                sketch
            )
        }

        def project_to_line(raw_line) -> tuple[float, float] | None:
            if not isinstance(raw_line, dict):
                return None
            raw_point = raw_line.get("point")
            raw_direction = raw_line.get("direction")
            if (
                not isinstance(raw_point, (list, tuple))
                or not isinstance(raw_direction, (list, tuple))
                or len(raw_point) < 2
                or len(raw_direction) < 2
            ):
                return None
            px, py = float(raw_point[0]), float(raw_point[1])
            dx, dy = (
                float(raw_direction[0]),
                float(raw_direction[1]),
            )
            squared_length = dx * dx + dy * dy
            if squared_length <= 1.0e-18:
                return None
            factor = ((x - px) * dx + (y - py) * dy) / squared_length
            return px + factor * dx, py + factor * dy

        constraints = point.get("constraints", ())
        if not isinstance(constraints, list):
            constraints = ()
        for constraint in constraints:
            if (
                not isinstance(constraint, dict)
                or constraint.get("type") != "point_on_reference"
            ):
                continue
            reference_id = str(constraint.get("reference_id", ""))
            if reference_id == "sketch_origin":
                x, y = 0.0, 0.0
                continue
            if reference_id == "sketch_axis:x":
                y = 0.0
                continue
            if reference_id == "sketch_axis:y":
                x = 0.0
                continue
            resolved = resolved_by_id.get(reference_id)
            geometry = (
                resolved.get("geometry")
                if isinstance(resolved, dict)
                else None
            )
            if not isinstance(geometry, dict):
                continue
            geometry_type = geometry.get("type")
            if geometry_type == "point":
                raw_point = geometry.get("point")
                if (
                    isinstance(raw_point, (list, tuple))
                    and len(raw_point) >= 2
                ):
                    x, y = float(raw_point[0]), float(raw_point[1])
            else:
                line = self._sketch_reference_constraint_line(
                    geometry,
                    constraint,
                    (x, y),
                )
                projected = project_to_line(line)
                if projected is not None:
                    x, y = projected
        point["x"] = x
        point["y"] = y

    @staticmethod
    def _next_sketch_point_id(
        entities: list[dict[str, Any]],
    ) -> str:
        used = {
            str(entity.get("id", ""))
            for entity in entities
            if entity.get("type") == "point"
        }
        index = 1
        while f"p{index}" in used:
            index += 1
        return f"p{index}"

    @staticmethod
    def _next_sketch_geometry_id(
        entities: list[dict[str, Any]],
    ) -> str:
        used = {
            str(entity.get("id", ""))
            for entity in entities
            if entity.get("type") != "point"
        }
        index = 1
        while f"g{index}" in used:
            index += 1
        return f"g{index}"

    def _ensure_sketch_entity_ids(self, sketch: ZimaEntity) -> None:
        entities = self._stored_sketch_entities(sketch)
        changed = False
        for entity in entities:
            if str(entity.get("id", "")):
                continue
            entity["id"] = (
                self._next_sketch_point_id(entities)
                if entity.get("type") == "point"
                else self._next_sketch_geometry_id(entities)
            )
            changed = True
        if changed:
            sketch.parameters["sketch_entities"] = json.dumps(
                entities,
                ensure_ascii=False,
            )

    def _snap_sketch_position(
        self,
        entities: list[dict[str, Any]],
        position: tuple[float, float],
    ) -> tuple[tuple[float, float], dict[str, Any] | None]:
        tolerance = self.native_viewer.sketch_snap_tolerance()
        nearest = None
        nearest_distance = tolerance
        for entity in entities:
            if entity.get("type") != "point":
                continue
            point_position = self._sketch_point_position(entity)
            distance = math.hypot(
                position[0] - point_position[0],
                position[1] - point_position[1],
            )
            if distance <= nearest_distance:
                nearest = entity
                nearest_distance = distance
        if nearest is not None:
            return self._sketch_point_position(nearest), nearest
        x, y = position
        if abs(x) <= tolerance:
            x = 0.0
        if abs(y) <= tolerance:
            y = 0.0
        return (x, y), None

    def _ensure_sketch_point(
        self,
        sketch: ZimaEntity,
        position: tuple[float, float],
    ) -> tuple[dict[str, Any], tuple[float, float], bool]:
        entities = self._stored_sketch_entities(sketch)
        snapped, existing = self._snap_sketch_position(
            entities,
            position,
        )
        if existing is not None:
            return existing, snapped, False
        point = {
            "type": "point",
            "id": self._next_sketch_point_id(entities),
            "x": snapped[0],
            "y": snapped[1],
        }
        entities.append(point)
        sketch.parameters["profile"] = "entities"
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        return point, snapped, True

    def _sketch_frame(
        self,
        sketch: ZimaEntity,
    ) -> tuple[
        tuple[float, float, float],
        tuple[float, float, float],
        tuple[float, float, float],
    ] | None:
        if self.document is None:
            return None
        owner = self.document.find_owning_object(sketch.entity_id)
        if owner is None:
            return None
        transform = coordinate_system_transform(owner.coordinate_system)
        origin = transform_point(transform, (0.0, 0.0, 0.0))
        x_axis = tuple(transform[row][0] for row in range(3))
        y_axis = tuple(transform[row][1] for row in range(3))
        return origin, x_axis, y_axis

    def _enter_sketch_edit(self, sketch_id: str) -> None:
        if self.document is None or self._sketch_edit_entity_id is not None:
            return
        sketch = self.document.find_entity(sketch_id)
        if sketch is None or sketch.kind != EntityKind.SKETCH:
            return
        frame = self._sketch_frame(sketch)
        if frame is None:
            return
        self._clear_dimension_overlays()
        self._ensure_sketch_entity_ids(sketch)
        self._sketch_edit_entity_id = sketch.entity_id
        self._sketch_previous_camera = copy.deepcopy(self.native_viewer.camera)
        self._sketch_baseline_parameters = copy.deepcopy(sketch.parameters)
        self._sketch_tool = "select"
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._sketch_coincident_first_point_id = None
        self._sketch_selected_entity_id = None
        self._sketch_selected_reference = None
        self._sketch_show_all_dimensions = False
        self._sketch_reference_mode = False
        self._sketch_selected_external_reference_id = None
        self._populate_tree()
        signals_were_blocked = self.native_viewer.blockSignals(True)
        try:
            self.native_viewer._clear_topology_hover()
            self.native_viewer._clear_topology_selection()
            self.native_viewer.set_selected_reference_owner(None)
            self.native_viewer.set_selected_container_origin(None)
            self.native_viewer.set_selected_container_contents(set())
            self.native_viewer.set_object_overlay(None)
        finally:
            self.native_viewer.blockSignals(signals_were_blocked)
        self.native_viewer.set_selection_enabled(False)
        self.rebuild_view(fit=False, rebuild_geometry=False)
        self._align_view_to_active_sketch()
        self.native_viewer.set_sketch_overlay(
            frame,
            self._stored_sketch_entities(sketch),
            selection_mode=True,
            external_references=self._resolved_sketch_external_references(
                sketch
            ),
        )
        self._rebuild_application_toolbar()
        self.statusBar().showMessage(tr("sketch.status.editing"))

    def _align_view_to_active_sketch(self) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        frame = self._sketch_frame(sketch)
        if frame is None:
            return
        normal = self._normalized_vector(
            self._cross_product(frame[1], frame[2])
        )
        # Keep the sketch-frame normal for datum planes. For a face-backed
        # sketch the viewer's camera convention requires the opposite
        # hemisphere from the face's signed outward normal.
        try:
            references = json.loads(
                str(sketch.parameters.get("constraint_refs", "[]"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            references = []
        if isinstance(references, list):
            ordered_references = sorted(
                (
                    reference
                    for reference in references
                    if isinstance(reference, dict)
                    and reference.get("type") == "face"
                ),
                key=lambda reference: (
                    reference.get("orientation_role")
                    not in ("normal", "opposite_normal")
                    and reference.get("plane_role") != "orientation"
                ),
            )
            for reference in ordered_references:
                equations = self._resolved_shape_reference_equations(
                    reference
                )
                if not equations:
                    equations = [
                        list(row)
                        for row in reference.get("equations", ())
                        if isinstance(row, (list, tuple))
                        and len(row) >= 3
                    ]
                if not equations:
                    continue
                # The camera looks toward the sketch plane. Keeping its view
                # direction opposite to the frame normal preserves the
                # exterior-side default while allowing Opposite normal to
                # deliberately select the other side.
                normal = tuple(-value for value in normal)
                break
        self.native_viewer.animate_view_normal(normal, frame[0])

    def _set_sketch_tool(self, tool: str) -> None:
        if self._sketch_edit_entity_id is None:
            return
        if self._sketch_reference_mode:
            self._set_sketch_reference_mode(False)
        if tool == "select":
            self._remove_pending_sketch_points()
            if not self._sketch_show_all_dimensions:
                self._clear_dimension_overlays()
        elif (
            self._sketch_tool == "spline"
            and len(self._sketch_pending_points) >= 2
        ):
            self._commit_pending_sketch_entity()
        self._sketch_tool = tool
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._sketch_coincident_first_point_id = None
        if tool != "select":
            self._sketch_selected_entity_id = None
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()

    def _set_sketch_constraint_tool(self, constraint: str) -> None:
        if (
            self._sketch_edit_entity_id is None
            or constraint != "coincident"
        ):
            return
        self._set_sketch_tool("coincident")
        self.statusBar().showMessage(
            tr("sketch.status.coincident.select_first")
        )

    def _on_sketch_reference_position_clicked(
        self,
        reference_id: str,
        x: float,
        y: float,
    ) -> None:
        self._on_sketch_position_clicked(
            x,
            y,
            reference_id=reference_id,
        )

    def _on_sketch_placement_clicked(
        self,
        x: float,
        y: float,
        reference_id: str,
        automatic_constraint: str,
    ) -> None:
        self._on_sketch_position_clicked(
            x,
            y,
            reference_id=reference_id or None,
            automatic_constraint=automatic_constraint or None,
        )

    def _on_sketch_reference_hovered(self, reference_id: str) -> None:
        if (
            not reference_id
            or self.document is None
            or self._sketch_edit_entity_id is None
        ):
            if self._sketch_edit_entity_id is not None:
                self.statusBar().showMessage(
                    tr(
                        "sketch.status.coincident.select_second"
                        if (
                            self._sketch_tool == "coincident"
                            and self._sketch_coincident_first_point_id
                        )
                        else "sketch.status.coincident.select_first"
                        if self._sketch_tool == "coincident"
                        else "sketch.status.editing"
                    )
                )
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        if self._sketch_tool == "coincident":
            self.statusBar().showMessage(
                tr(
                    "sketch.status.coincident.hover_reference",
                    reference=self._sketch_reference_display_name(
                        sketch,
                        reference_id,
                    ),
                )
            )
            return
        self.statusBar().showMessage(
            tr(
                "sketch.status.selecting_reference",
                reference=self._sketch_reference_display_name(
                    sketch,
                    reference_id,
                ),
            )
        )

    def _on_sketch_entity_hovered(self, entity_id: str) -> None:
        if self._sketch_tool != "coincident":
            return
        if entity_id:
            self.statusBar().showMessage(
                tr(
                    "sketch.status.coincident.hover_point",
                    point=entity_id,
                )
            )
            return
        self.statusBar().showMessage(
            tr(
                "sketch.status.coincident.select_second"
                if self._sketch_coincident_first_point_id
                else "sketch.status.coincident.select_first"
            )
        )

    def _sketch_reference_display_name(
        self,
        sketch: ZimaEntity,
        reference_id: str,
    ) -> str:
        intrinsic_names = {
            "sketch_origin": tr("sketch.reference.origin"),
            "sketch_axis:x": tr("sketch.reference.axis_x"),
            "sketch_axis:y": tr("sketch.reference.axis_y"),
        }
        if reference_id in intrinsic_names:
            return intrinsic_names[reference_id]
        reference = next(
            (
                candidate
                for candidate in self._stored_sketch_external_references(
                    sketch
                )
                if str(candidate.get("id", "")) == reference_id
            ),
            None,
        )
        if reference is None:
            return tr("sketch.reference.unknown")
        owner = (
            self.document.find_entity(
                str(reference.get("owner_id", ""))
            )
            if self.document is not None
            else None
        )
        owner_name = (
            owner.name
            if owner is not None
            else tr("tree.sketch.missing_reference")
        )
        source_kind = tr(
            "sketch.reference.kind."
            + str(reference.get("source_kind", "reference"))
        )
        try:
            element_index = int(reference.get("element_index", 0))
        except (TypeError, ValueError):
            element_index = 0
        return f"{owner_name} · {source_kind} {element_index}"

    def _on_sketch_position_clicked(
        self,
        x: float,
        y: float,
        *,
        reference_id: str | None = None,
        automatic_constraint: str | None = None,
    ) -> None:
        if self._sketch_edit_entity_id is None or self.document is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        if self._sketch_tool == "coincident":
            self._handle_sketch_coincident_click(
                sketch,
                (x, y),
                reference_id,
            )
            return
        point, snapped, created = self._ensure_sketch_point(
            sketch,
            (x, y),
        )
        point_id = str(point.get("id", ""))
        if created and point_id:
            self._sketch_pending_new_point_ids.add(point_id)
            if reference_id:
                point = (
                    self._add_sketch_point_reference_constraint(
                        sketch,
                        point_id,
                        reference_id,
                    )
                    or point
                )
        if self._sketch_tool == "point":
            self._sketch_pending_points[:] = [snapped]
            self._sketch_pending_point_ids[:] = [point_id]
            self._mark_model_for_regeneration()
            self.rebuild_view(fit=False)
            # A point is complete after one click. Keep the point tool active
            # for the next point, but do not leave the completed point in the
            # pending set (switching to Select would otherwise remove it).
            self._sketch_pending_points.clear()
            self._sketch_pending_point_ids.clear()
            self._sketch_pending_new_point_ids.clear()
            self._sketch_pending_constraint = None
            self._refresh_sketch_overlay()
            if self._sketch_show_all_dimensions:
                self._show_all_sketch_dimensions(sketch)
            else:
                self._show_sketch_point_dimensions(sketch, point)
            return
        if (
            point_id
            and self._sketch_pending_point_ids
            and point_id == self._sketch_pending_point_ids[-1]
        ):
            return
        self._sketch_pending_points.append(snapped)
        self._sketch_pending_point_ids.append(point_id)
        if (
            automatic_constraint in ("horizontal", "vertical")
            and self._sketch_tool in ("segment", "construction")
            and len(self._sketch_pending_points) >= 2
        ):
            self._sketch_pending_constraint = automatic_constraint
        required = {
            "point": 1,
            "segment": 2,
            "construction": 2,
            "arc": 3,
        }.get(self._sketch_tool)
        if required is not None and len(self._sketch_pending_points) >= required:
            self._commit_pending_sketch_entity()
        else:
            self._refresh_sketch_overlay()

    def _handle_sketch_coincident_click(
        self,
        sketch: ZimaEntity,
        position: tuple[float, float],
        reference_id: str | None,
    ) -> None:
        entities = self._stored_sketch_entities(sketch)
        _snapped, candidate = self._snap_sketch_position(
            entities,
            position,
        )
        candidate_id = (
            str(candidate.get("id", ""))
            if candidate is not None
            else ""
        )
        first_id = self._sketch_coincident_first_point_id
        if first_id is None:
            if not candidate_id:
                self.statusBar().showMessage(
                    tr("sketch.status.coincident.point_required")
                )
                return
            self._sketch_coincident_first_point_id = candidate_id
            self._sketch_selected_entity_id = candidate_id
            self._clear_dimension_overlays()
            self._refresh_sketch_overlay()
            self.statusBar().showMessage(
                tr("sketch.status.coincident.select_second")
            )
            return

        completed = False
        if candidate_id and candidate_id != first_id:
            completed = self._add_sketch_coincident_constraint(
                sketch,
                first_id,
                candidate_id,
            )
        elif reference_id:
            first_point = next(
                (
                    entity
                    for entity in entities
                    if entity.get("type") == "point"
                    and str(entity.get("id", "")) == first_id
                ),
                None,
            )
            constraints = (
                first_point.get("constraints", ())
                if first_point is not None
                else ()
            )
            already_constrained = (
                isinstance(constraints, list)
                and any(
                    isinstance(constraint, dict)
                    and constraint.get("type") == "point_on_reference"
                    and str(constraint.get("reference_id", ""))
                    == reference_id
                    for constraint in constraints
                )
            )
            point = (
                None
                if already_constrained
                else self._add_sketch_point_reference_constraint(
                    sketch,
                    first_id,
                    reference_id,
                )
            )
            if point is not None:
                entities = self._stored_sketch_entities(sketch)
                point = next(
                    (
                        entity
                        for entity in entities
                        if entity.get("type") == "point"
                        and str(entity.get("id", "")) == first_id
                    ),
                    None,
                )
                if point is not None:
                    self._apply_sketch_point_reference_constraints(
                        sketch,
                        point,
                    )
                    self._apply_sketch_coincident_constraints(entities)
                    sketch.parameters["sketch_entities"] = json.dumps(
                        entities,
                        ensure_ascii=False,
                    )
                    completed = True
        if not completed:
            self.statusBar().showMessage(
                tr("sketch.status.coincident.invalid_second")
            )
            return

        self._sketch_coincident_first_point_id = None
        self._sketch_selected_entity_id = None
        self._mark_model_for_regeneration()
        self.rebuild_view(fit=False)
        self._refresh_sketch_overlay()
        self.statusBar().showMessage(
            tr("sketch.status.coincident.created")
        )

    def _add_sketch_coincident_constraint(
        self,
        sketch: ZimaEntity,
        target_point_id: str,
        constrained_point_id: str,
    ) -> bool:
        entities = self._stored_sketch_entities(sketch)
        points = {
            str(entity.get("id", "")): entity
            for entity in entities
            if entity.get("type") == "point"
        }
        target = points.get(target_point_id)
        constrained = points.get(constrained_point_id)
        if target is None or constrained is None:
            return False

        connected: dict[str, set[str]] = {
            point_id: set() for point_id in points
        }
        for point_id, point in points.items():
            constraints = point.get("constraints", ())
            if not isinstance(constraints, list):
                continue
            for constraint in constraints:
                if (
                    not isinstance(constraint, dict)
                    or constraint.get("type") != "coincident"
                ):
                    continue
                other_id = str(constraint.get("point_id", ""))
                if other_id in points:
                    connected[point_id].add(other_id)
                    connected[other_id].add(point_id)
        pending = [target_point_id]
        visited: set[str] = set()
        while pending:
            point_id = pending.pop()
            if point_id == constrained_point_id:
                return False
            if point_id in visited:
                continue
            visited.add(point_id)
            pending.extend(connected.get(point_id, ()))

        constraints = constrained.get("constraints", [])
        if not isinstance(constraints, list):
            constraints = []
        constraints.append({
            "type": "coincident",
            "point_id": target_point_id,
        })
        constrained["constraints"] = constraints
        self._apply_sketch_coincident_constraints(entities)
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        return True

    def _add_sketch_point_reference_constraint(
        self,
        sketch: ZimaEntity,
        point_id: str,
        reference_id: str,
    ) -> dict[str, Any] | None:
        entities = self._stored_sketch_entities(sketch)
        point = next(
            (
                entity
                for entity in entities
                if entity.get("type") == "point"
                and str(entity.get("id", "")) == point_id
            ),
            None,
        )
        if point is None:
            return None
        raw_constraints = point.get("constraints", [])
        constraints = (
            [
                constraint
                for constraint in raw_constraints
                if isinstance(constraint, dict)
            ]
            if isinstance(raw_constraints, list)
            else []
        )
        if any(
            constraint.get("type") == "point_on_reference"
            and constraint.get("reference_id") == reference_id
            for constraint in constraints
        ):
            return point
        constraint = {
            "type": "point_on_reference",
            "reference_id": reference_id,
        }
        resolved = next(
            (
                reference
                for reference in
                self._resolved_sketch_external_references(sketch)
                if str(reference.get("id", "")) == reference_id
            ),
            None,
        )
        geometry = (
            resolved.get("geometry")
            if isinstance(resolved, dict)
            else None
        )
        if (
            isinstance(geometry, dict)
            and geometry.get("type") == "lines"
            and isinstance(geometry.get("lines"), list)
        ):
            chosen_line = self._sketch_reference_constraint_line(
                geometry,
                constraint,
                self._sketch_point_position(point),
            )
            if chosen_line is not None:
                try:
                    constraint["geometry_index"] = geometry[
                        "lines"
                    ].index(chosen_line)
                except ValueError:
                    pass
        constraints.append(constraint)
        point["constraints"] = constraints
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        return point

    def _cancel_current_sketch_entity(self) -> None:
        if self._sketch_edit_entity_id is None:
            return
        if self._sketch_tool == "coincident":
            self._sketch_coincident_first_point_id = None
            self._sketch_selected_entity_id = None
            self._refresh_sketch_overlay()
            self.statusBar().showMessage(
                tr("sketch.status.coincident.select_first")
            )
            return
        if (
            self._sketch_tool == "spline"
            and len(self._sketch_pending_points) >= 2
        ):
            self._commit_pending_sketch_entity()
            return
        self._remove_pending_sketch_points()
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._refresh_sketch_overlay()

    def _confirm_current_sketch_entity(self) -> None:
        if self._sketch_edit_entity_id is None:
            return
        if self._sketch_reference_mode:
            self._set_sketch_reference_mode(False)
            self._sketch_selected_external_reference_id = None
            self._refresh_sketch_overlay()
            self._rebuild_application_toolbar()
            return
        if self._sketch_tool == "select":
            return
        if (
            self._sketch_tool == "spline"
            and len(self._sketch_pending_points) >= 2
        ):
            self._commit_pending_sketch_entity()
            self._set_sketch_tool("select")
            return
        if (
            self._sketch_tool == "point"
            and not self._sketch_show_all_dimensions
        ):
            self._clear_dimension_overlays()
        else:
            # An unfinished multi-point entity cannot be committed. Remove
            # only the points created for that unfinished operation; snapped
            # pre-existing points remain untouched.
            self._remove_pending_sketch_points()
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._set_sketch_tool("select")

    def _remove_pending_sketch_points(self) -> None:
        if (
            self.document is None
            or self._sketch_edit_entity_id is None
            or not self._sketch_pending_new_point_ids
        ):
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        entities = self._stored_sketch_entities(sketch)
        referenced = {
            str(point_id)
            for entity in entities
            if entity.get("type") != "point"
            for point_id in entity.get("point_ids", ())
        }
        entities = [
            entity
            for entity in entities
            if not (
                entity.get("type") == "point"
                and str(entity.get("id", ""))
                in self._sketch_pending_new_point_ids
                and str(entity.get("id", "")) not in referenced
            )
        ]
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        self.rebuild_view(fit=False)

    def _select_sketch_entity(self, entity_id: str) -> None:
        if (
            self.document is None
            or self._sketch_edit_entity_id is None
            or self._sketch_tool != "select"
        ):
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        selected = next(
            (
                entity
                for entity in self._stored_sketch_entities(sketch)
                if str(entity.get("id", "")) == entity_id
            ),
            None,
        )
        self._sketch_selected_entity_id = (
            entity_id if selected is not None else None
        )
        self._sketch_selected_reference = None
        self._sketch_selected_external_reference_id = None
        signals_were_blocked = self.native_viewer.blockSignals(True)
        try:
            self.native_viewer._clear_topology_selection()
        finally:
            self.native_viewer.blockSignals(signals_were_blocked)
        if self._sketch_show_all_dimensions:
            self._show_all_sketch_dimensions(sketch)
        elif selected is not None and selected.get("type") == "point":
            self._show_sketch_point_dimensions(sketch, selected)
        else:
            self._clear_dimension_overlays()
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()

    def _select_sketch_reference(
        self,
        kind: str,
        owner_id: str,
        element_index: int,
    ) -> None:
        if self._sketch_edit_entity_id is None:
            return
        reference = (kind, owner_id, element_index)
        self._sketch_selected_reference = reference
        self._sketch_selected_entity_id = None
        self._sketch_selected_external_reference_id = None
        signals_were_blocked = self.native_viewer.blockSignals(True)
        try:
            self.native_viewer._clear_topology_selection()
            {
                "point": self.native_viewer._set_selected_point,
                "edge": self.native_viewer._set_selected_edge,
                "plane": self.native_viewer._set_selected_plane,
            }[kind]((owner_id, element_index))
        finally:
            self.native_viewer.blockSignals(signals_were_blocked)
        if not self._sketch_show_all_dimensions:
            self._clear_dimension_overlays()
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()

    def _select_sketch_external_reference(
        self,
        reference_id: str,
    ) -> None:
        if self._sketch_edit_entity_id is None:
            return
        self._sketch_selected_external_reference_id = reference_id
        self._sketch_selected_entity_id = None
        self._sketch_selected_reference = None
        self._clear_dimension_overlays()
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()

    def _select_sketch_constraint(
        self,
        point_id: str,
        _constraint_index: int,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        point = next(
            (
                entity
                for entity in self._stored_sketch_entities(sketch)
                if entity.get("type") == "point"
                and str(entity.get("id", "")) == point_id
            ),
            None,
        )
        if point is None:
            return
        self._sketch_selected_entity_id = point_id
        self._sketch_selected_reference = None
        self._sketch_selected_external_reference_id = None
        if self._sketch_show_all_dimensions:
            self._show_all_sketch_dimensions(sketch)
        else:
            self._show_sketch_point_dimensions(sketch, point)
        self._refresh_sketch_overlay(populate_tree=False)

    def _select_sketch_geometry_child(self, entity_id: str) -> None:
        if self._sketch_edit_entity_id is None:
            return
        self._sketch_selected_entity_id = entity_id
        self._sketch_selected_reference = None
        self._sketch_selected_external_reference_id = None
        self._clear_dimension_overlays()
        self._refresh_sketch_overlay(populate_tree=False)

    def _delete_sketch_external_reference(
        self,
        reference_id: str,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        references = self._stored_sketch_external_references(sketch)
        remaining = [
            reference
            for reference in references
            if str(reference.get("id", "")) != reference_id
        ]
        if len(remaining) == len(references):
            return
        sketch.parameters["external_references"] = json.dumps(
            remaining,
            ensure_ascii=False,
        )
        if self._sketch_selected_external_reference_id == reference_id:
            self._sketch_selected_external_reference_id = None
        self._mark_model_for_regeneration()
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()
        self.statusBar().showMessage(
            tr("sketch.status.reference_deleted")
        )

    def _delete_sketch_point_constraint(
        self,
        point_id: str,
        constraint_index: int,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        entities = self._stored_sketch_entities(sketch)
        point = next(
            (
                entity
                for entity in entities
                if entity.get("type") == "point"
                and str(entity.get("id", "")) == point_id
            ),
            None,
        )
        if point is None:
            return
        constraints = point.get("constraints", ())
        if (
            not isinstance(constraints, list)
            or not 0 <= constraint_index < len(constraints)
        ):
            return
        del constraints[constraint_index]
        if constraints:
            point["constraints"] = constraints
        else:
            point.pop("constraints", None)
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        self._sketch_selected_entity_id = point_id
        self._mark_model_for_regeneration()
        self._refresh_sketch_overlay()
        if self._sketch_show_all_dimensions:
            self._show_all_sketch_dimensions(sketch)
        else:
            self._show_sketch_point_dimensions(sketch, point)
        self.statusBar().showMessage(
            tr("sketch.status.constraint_deleted")
        )

    def _delete_sketch_geometry_constraint(
        self,
        entity_id: str,
        constraint_index: int,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        entities = self._stored_sketch_entities(sketch)
        geometry = next(
            (
                entity
                for entity in entities
                if str(entity.get("id", "")) == entity_id
                and entity.get("type") != "point"
            ),
            None,
        )
        if geometry is None:
            return
        constraints = geometry.get("constraints", ())
        if (
            not isinstance(constraints, list)
            or not 0 <= constraint_index < len(constraints)
        ):
            return
        del constraints[constraint_index]
        if constraints:
            geometry["constraints"] = constraints
        else:
            geometry.pop("constraints", None)
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        self._sketch_selected_entity_id = entity_id
        self._clear_dimension_overlays()
        self._mark_model_for_regeneration()
        self._refresh_sketch_overlay()
        self.statusBar().showMessage(
            tr("sketch.status.constraint_deleted")
        )

    def _delete_selected_sketch_entity(self) -> None:
        if (
            self.document is None
            or self._sketch_edit_entity_id is None
            or self._sketch_selected_entity_id is None
        ):
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        selected_id = self._sketch_selected_entity_id
        entities = self._stored_sketch_entities(sketch)
        selected = next(
            (
                entity
                for entity in entities
                if str(entity.get("id", "")) == selected_id
            ),
            None,
        )
        if selected is None:
            return
        if selected.get("type") == "point":
            entities = [
                entity
                for entity in entities
                if str(entity.get("id", "")) != selected_id
                and selected_id
                not in {
                    str(point_id)
                    for point_id in entity.get("point_ids", ())
                }
            ]
            for entity in entities:
                constraints = entity.get("constraints", ())
                if not isinstance(constraints, list):
                    continue
                remaining_constraints = [
                    constraint
                    for constraint in constraints
                    if not (
                        isinstance(constraint, dict)
                        and constraint.get("type") == "coincident"
                        and str(constraint.get("point_id", ""))
                        == selected_id
                    )
                ]
                if remaining_constraints:
                    entity["constraints"] = remaining_constraints
                else:
                    entity.pop("constraints", None)
        else:
            entities = [
                entity
                for entity in entities
                if str(entity.get("id", "")) != selected_id
            ]
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        self._sketch_selected_entity_id = None
        self._clear_dimension_overlays()
        self._mark_model_for_regeneration()
        self.rebuild_view(fit=False)
        if self._sketch_show_all_dimensions:
            self._show_all_sketch_dimensions(sketch)
        self._refresh_sketch_overlay()
        self._rebuild_application_toolbar()

    def _commit_pending_sketch_entity(self) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None or not self._sketch_pending_points:
            return
        entities = self._stored_sketch_entities(sketch)
        geometry = {
            "id": self._next_sketch_geometry_id(entities),
            "type": self._sketch_tool or "segment",
            "point_ids": list(self._sketch_pending_point_ids),
        }
        if self._sketch_pending_constraint is not None:
            geometry["constraints"] = [
                {"type": self._sketch_pending_constraint}
            ]
        entities.append(geometry)
        sketch.parameters["profile"] = "entities"
        sketch.parameters["sketch_entities"] = json.dumps(
            entities,
            ensure_ascii=False,
        )
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._mark_model_for_regeneration()
        self.rebuild_view(fit=False)
        self._refresh_sketch_overlay()

    def _refresh_sketch_overlay(
        self,
        *,
        populate_tree: bool = True,
    ) -> None:
        if self.document is None or self._sketch_edit_entity_id is None:
            self.native_viewer.set_sketch_overlay(None)
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            self.native_viewer.set_sketch_overlay(None)
            return
        frame = self._sketch_frame(sketch)
        self.native_viewer.set_sketch_overlay(
            frame,
            self._stored_sketch_entities(sketch),
            self._sketch_pending_points,
            selection_mode=self._sketch_tool == "select",
            selected_entity_id=self._sketch_selected_entity_id,
            external_references=self._resolved_sketch_external_references(
                sketch
            ),
            snap_to_external_references=self._sketch_tool
            in ("point", "segment", "construction", "coincident"),
        )
        if populate_tree:
            self._populate_tree()

    def _show_sketch_point_dimensions(
        self,
        sketch: ZimaEntity,
        point: dict[str, Any],
    ) -> None:
        self._show_sketch_points_dimensions(sketch, (point,))

    def _show_all_sketch_dimensions(self, sketch: ZimaEntity) -> None:
        self._show_sketch_points_dimensions(
            sketch,
            tuple(
                entity
                for entity in self._stored_sketch_entities(sketch)
                if entity.get("type") == "point"
            ),
        )

    def _show_sketch_points_dimensions(
        self,
        sketch: ZimaEntity,
        points: tuple[dict[str, Any], ...],
    ) -> None:
        frame = self._sketch_frame(sketch)
        if frame is None:
            return
        origin, x_axis, y_axis = frame

        def world(local_x: float, local_y: float):
            return tuple(
                origin[index]
                + local_x * x_axis[index]
                + local_y * y_axis[index]
                for index in range(3)
            )

        base_margin = max(
            self.native_viewer.sketch_snap_tolerance(18.0),
            2.0,
        )
        dimensions: list[LinearDimension] = []
        values: list[tuple[LinearDimension, str, str, float]] = []
        for point_index, point in enumerate(points):
            point_id = str(point.get("id", ""))
            if not point_id:
                continue
            x, y = self._sketch_point_position(point)
            margin = base_margin * (1.0 + 0.35 * point_index)
            locked_coordinates = self._sketch_point_locked_coordinates(
                sketch,
                point,
            )
            point_dimensions: list[
                tuple[LinearDimension, str, float]
            ] = []
            if "x" not in locked_coordinates:
                point_dimensions.append((
                    LinearDimension(
                    key=f"sketch_point:{point_id}:x",
                    first_point=world(0.0, 0.0),
                    second_point=world(x, 0.0),
                    first_dimension_point=world(0.0, -margin),
                    second_dimension_point=world(x, -margin),
                    direction=x_axis,
                ),
                    "x",
                    x,
                ))
            if "y" not in locked_coordinates:
                point_dimensions.append((
                    LinearDimension(
                    key=f"sketch_point:{point_id}:y",
                    first_point=world(x, 0.0),
                    second_point=world(x, y),
                    first_dimension_point=world(x + margin, 0.0),
                    second_dimension_point=world(x + margin, y),
                    direction=y_axis,
                    leader_anchor="second",
                ),
                    "y",
                    y,
                ))
            dimensions.extend(
                dimension
                for dimension, _coordinate, _value in point_dimensions
            )
            values.extend(
                (dimension, point_id, coordinate, value)
                for dimension, coordinate, value in point_dimensions
            )
        self._clear_dimension_overlays()
        if not dimensions:
            return
        self._dimension_object_id = sketch.entity_id
        self.native_viewer.set_dimensions(tuple(dimensions))
        for dimension, point_id, coordinate, value in values:
            overlay = ParameterEditOverlay(self.native_viewer)
            self._configure_dimension_overlay(
                overlay,
                sketch,
                dimension,
                value,
            )
            overlay.valueCommitted.connect(
                lambda raw_value, key=dimension.key:
                self._commit_dimension_value(key, raw_value)
            )
            overlay.selected.connect(
                lambda key=dimension.key:
                self._select_dimension_overlay(key)
            )
            self._dimension_overlays[dimension.key] = overlay
            self._dimension_bindings[dimension.key] = (
                "sketch_point",
                point_id,
                coordinate,
            )
        QTimer.singleShot(0, self._position_dimension_overlays)

    def _toggle_all_sketch_dimensions(self, checked: bool) -> None:
        self._sketch_show_all_dimensions = checked
        if self.document is None or self._sketch_edit_entity_id is None:
            return
        sketch = self.document.find_entity(self._sketch_edit_entity_id)
        if sketch is None:
            return
        if checked:
            self._show_all_sketch_dimensions(sketch)
            return
        selected = next(
            (
                entity
                for entity in self._stored_sketch_entities(sketch)
                if entity.get("type") == "point"
                and str(entity.get("id", ""))
                == self._sketch_selected_entity_id
            ),
            None,
        )
        if selected is not None:
            self._show_sketch_point_dimensions(sketch, selected)
        else:
            self._clear_dimension_overlays()

    def _toggle_sketch_reference_mode(self, checked: bool) -> None:
        self._set_sketch_reference_mode(checked)
        self._rebuild_application_toolbar()

    def _set_sketch_reference_mode(self, enabled: bool) -> None:
        if self._sketch_edit_entity_id is None:
            enabled = False
        self._sketch_reference_mode = enabled
        self.native_viewer.set_sketch_reference_selection_mode(enabled)
        self.native_viewer.set_outline_face_highlights(enabled)
        self.native_viewer.set_selection_enabled(
            True if enabled else False
        )
        self.native_viewer.set_selection_filter(
            "all" if enabled else self.view_selection_filter.value
        )
        self.native_viewer.set_interaction_mode(
            "topology" if enabled else "object"
        )
        self.native_viewer._clear_topology_hover()
        self.native_viewer._clear_topology_selection()
        self._view_candidate_cycle_ids = ()
        self._view_candidate_cycle_index = -1
        self.statusBar().showMessage(
            tr(
                "sketch.status.reference_mode"
                if enabled
                else "sketch.status.editing"
            )
        )

    def _finish_sketch_edit(self) -> None:
        if self._sketch_tool == "spline" and len(self._sketch_pending_points) >= 2:
            self._commit_pending_sketch_entity()
        self._leave_sketch_edit(restore=False)

    def _cancel_sketch_edit(self) -> None:
        if self._sketch_edit_entity_id is None:
            return
        sketch_id = self._sketch_edit_entity_id
        self._leave_sketch_edit(restore=False)
        QTimer.singleShot(
            0,
            lambda: self._reopen_sketch_properties(sketch_id),
        )

    def _leave_sketch_edit(self, *, restore: bool) -> None:
        if self._sketch_edit_entity_id is None:
            return
        sketch_id = self._sketch_edit_entity_id
        if self.document is not None and restore:
            sketch = self.document.find_entity(self._sketch_edit_entity_id)
            if sketch is not None and self._sketch_baseline_parameters is not None:
                sketch.parameters = copy.deepcopy(
                    self._sketch_baseline_parameters
                )
        self._clear_dimension_overlays()
        self.native_viewer.set_sketch_overlay(None)
        self._set_sketch_reference_mode(False)
        self.native_viewer.set_selection_enabled(self.view_selection_enabled)
        if self._sketch_previous_camera is not None:
            self.native_viewer.camera = self._sketch_previous_camera
            self.native_viewer.navigationChanged.emit(
                self.native_viewer.camera
            )
            self.native_viewer.update()
        self._sketch_edit_entity_id = None
        self._sketch_previous_camera = None
        self._sketch_baseline_parameters = None
        self._sketch_tool = None
        self._sketch_pending_points.clear()
        self._sketch_pending_point_ids.clear()
        self._sketch_pending_new_point_ids.clear()
        self._sketch_pending_constraint = None
        self._sketch_coincident_first_point_id = None
        self._sketch_selected_entity_id = None
        self._sketch_selected_reference = None
        self._sketch_show_all_dimensions = False
        self._sketch_reference_mode = False
        self._sketch_selected_external_reference_id = None
        self._populate_tree()
        self._rebuild_application_toolbar()
        self.rebuild_view(fit=False)
        self.statusBar().showMessage(
            tr(
                "sketch.status.cancelled"
                if restore
                else "sketch.status.finished"
            )
        )
        if restore:
            QTimer.singleShot(
                0,
                lambda: self._reopen_sketch_properties(sketch_id),
            )

    def _reopen_sketch_properties(self, sketch_id: str) -> None:
        if self.document is None:
            return
        sketch = self.document.find_entity(sketch_id)
        if sketch is not None and sketch.kind == EntityKind.SKETCH:
            self.show_properties(sketch)

    def _begin_definition_edit(self, obj: ZimaEntity) -> None:
        self._definition_dialog_depth += 1
        coordinate_owner = obj
        if (
            obj.kind != EntityKind.CONTAINER
            and self.document is not None
            and (owner := self.document.find_owning_object(obj.entity_id))
            is not None
        ):
            coordinate_owner = owner
        self._definition_edit_objects.append(coordinate_owner)
        if self._definition_dialog_depth == 1:
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _end_definition_edit(self) -> None:
        self._definition_dialog_depth = max(
            0,
            self._definition_dialog_depth - 1,
        )
        if self._definition_edit_objects:
            self._definition_edit_objects.pop()
        if self._definition_dialog_depth == 0:
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _definition_origin_is_visible(self) -> bool:
        return self._definition_dialog_depth > 0 or (
            self.point_constraint_dialog is not None
        )

    def _definition_edit_object(self) -> ZimaEntity | None:
        if (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.point_object is not None
        ):
            return self.point_constraint_dialog.point_object
        if self._definition_edit_objects:
            return self._definition_edit_objects[-1]
        return None

    def _definition_history_boundary(self) -> int:
        if self.document is None:
            return 0
        # Editing an existing definition is a live operation. Keep the full
        # active history visible instead of rolling the model back before the
        # edited object; otherwise the object disappears from the viewport
        # while its Properties dialog is open.
        return self.document.history_cursor()

    def show_properties(self, obj: ZimaEntity) -> None:
        if obj.kind == EntityKind.CONTAINER:
            self.show_object_properties(obj)
        elif obj.kind in SOLID_KINDS:
            self.show_primitive_properties(obj)
        elif obj.kind == EntityKind.AXIS and not obj.locked:
            self.show_axis_properties(obj)
        elif obj.kind == EntityKind.PLANE and not obj.locked:
            self.show_plane_properties(obj)
        elif obj.kind == EntityKind.SKETCH and self.document is not None:
            owner = self.document.find_owning_object(obj.entity_id)
            if owner is not None:
                self._edit_sketch_object(owner, obj)
        elif obj.kind == EntityKind.POINT and self.document is not None:
            owner = self.document.find_owning_object(obj.entity_id)
            if owner is not None:
                self._edit_point_object(owner, obj)

    def _refresh_object_properties(self, obj: ZimaEntity) -> None:
        self._mark_model_for_regeneration()
        self.selected_object_id = obj.entity_id
        self.regenerate_model()

    def _mark_model_for_regeneration(self) -> None:
        if self.document is not None:
            self.document.regeneration_required = True

    def _add_sketch_role_menu(
        self,
        menu: QMenu,
        source: ZimaEntity,
    ) -> dict[Any, SketchRole]:
        if self.document is None:
            return {}
        owner = (
            source
            if source.kind == EntityKind.CONTAINER
            else self.document.find_owning_object(source.entity_id)
        )
        if owner is None or owner.kind != EntityKind.CONTAINER:
            return {}
        sketch_menu = menu.addMenu(tr("menu.context.create_sketch"))
        actions: dict[Any, SketchRole] = {}
        any_enabled = False
        for role in SketchRole:
            action = sketch_menu.addAction(tr(f"sketch.role.{role.value.lower()}"))
            enabled = owner.can_accept_entity(EntityKind.SKETCH, role)
            action.setEnabled(enabled)
            any_enabled = any_enabled or enabled
            actions[action] = role
        sketch_menu.setEnabled(any_enabled)
        return actions

    def _is_object_reference_plane(self, obj: ZimaEntity) -> bool:
        if self.document is None or obj.kind != EntityKind.PLANE:
            return False
        parent = self.document.find_owning_object(obj.entity_id)
        return parent is not None and parent.kind == EntityKind.CONTAINER

    def _is_system_reference_plane(self, obj: ZimaEntity) -> bool:
        if self.document is None or obj.kind != EntityKind.PLANE:
            return False
        parent = self.document.find_parent(obj.entity_id)
        return parent is not None and parent.kind == EntityKind.ORIGIN

    def _can_create_cube_from(self, obj: ZimaEntity) -> bool:
        if self.document is None:
            return False
        if obj.kind == EntityKind.CONTAINER:
            return obj.can_accept_entity(EntityKind.BOX)
        if obj.kind != EntityKind.POINT:
            return False
        parent = self.document.find_owning_object(obj.entity_id)
        return parent is not None and parent.can_accept_entity(EntityKind.BOX)

    def _show_entity_limit_message(
        self,
        source_id: str,
        requested_kind: EntityKind,
    ) -> None:
        if self.document is None:
            return
        source = self.document.find_entity(source_id)
        if source is None:
            return
        owner = (
            source
            if source.kind == EntityKind.CONTAINER
            else self.document.find_owning_object(source_id)
        )
        if owner is not None and not owner.can_accept_entity(requested_kind):
            QMessageBox.information(
                self,
                tr("message.container.entity_limit_title"),
                tr("message.container.entity_limit"),
            )

    def _object_from_tree_item(self, item: QTreeWidgetItem | None) -> ZimaEntity | None:
        if item is None:
            return None
        entity_id = item.data(0, Qt.ItemDataRole.UserRole)
        if entity_id is None:
            return None
        if self.document is None:
            return None
        return self.document.find_entity(entity_id)

    def _select_tree_object(self, entity_id: str) -> None:
        root = self.tree.invisibleRootItem()
        item = self._find_tree_item(root, entity_id)
        if item is not None:
            self.tree.setCurrentItem(item)

    def _select_tree_object_without_reference_event(
        self,
        entity_id: str,
    ) -> None:
        self.selected_object_id = entity_id
        signals_were_blocked = self.tree.blockSignals(True)
        try:
            self._select_tree_object(entity_id)
        finally:
            self.tree.blockSignals(signals_were_blocked)

    def _find_tree_item(self, parent: QTreeWidgetItem, entity_id: str):
        for index in range(parent.childCount()):
            child = parent.child(index)
            if child.data(0, Qt.ItemDataRole.UserRole) == entity_id:
                return child
            found = self._find_tree_item(child, entity_id)
            if found is not None:
                return found
        return None

    def _selected_object(self) -> ZimaEntity | None:
        if self.document is None or self.selected_object_id is None:
            return None
        return self.document.find_entity(self.selected_object_id)

    def _show_edit_overlays(
        self,
        obj: ZimaEntity,
        _position: QPoint,
    ) -> bool:
        entity = self._first_editable_dimension_entity(obj)
        if entity is None:
            return False
        dimensions = (
            self._primitive_dimensions(entity)
            if entity.kind in SOLID_KINDS
            else self._construction_entity_dimensions(entity)
        )
        dimensions = (*dimensions, *self._reference_dimensions(entity))
        if not dimensions:
            return False
        self._clear_dimension_overlays()
        self._dimension_object_id = entity.entity_id
        self.native_viewer.set_dimensions(dimensions)
        unit = str(entity.parameters.get("unit", "mm"))
        for dimension in dimensions:
            overlay = ParameterEditOverlay(self.native_viewer)
            if dimension.key.startswith("reference_offset:"):
                reference_index = int(dimension.key.rsplit(":", 1)[1])
                references = self._constraint_references(entity)
                value = self._format_display_value(
                    references[reference_index].get("offset", 0.0)
                )
                self._dimension_bindings[dimension.key] = (
                    "reference_offset",
                    reference_index,
                )
            else:
                value = self._format_display_value(
                    entity.parameters.get(dimension.key, "0")
                )
                self._dimension_bindings[dimension.key] = (
                    "parameter",
                    dimension.key,
                )
            self._configure_dimension_overlay(
                overlay,
                entity,
                dimension,
                value,
            )
            overlay.valueCommitted.connect(
                lambda value, key=dimension.key:
                self._commit_dimension_value(key, value)
            )
            overlay.selected.connect(
                lambda key=dimension.key:
                self._select_dimension_overlay(key)
            )
            self._dimension_overlays[dimension.key] = overlay
        QTimer.singleShot(0, self._position_dimension_overlays)
        return True

    def _format_display_value(self, value: Any) -> str:
        try:
            number = float(value)
        except (TypeError, ValueError):
            return str(value)
        return f"{number:.{display_decimal_places(self)}f}"

    @staticmethod
    def _dimension_styles(entity: ZimaEntity) -> dict[str, dict[str, Any]]:
        try:
            styles = json.loads(
                str(entity.parameters.get("dimension_styles", "{}"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            return {}
        return (
            {
                str(key): value
                for key, value in styles.items()
                if isinstance(value, dict)
            }
            if isinstance(styles, dict)
            else {}
        )

    def _dimension_style(
        self,
        entity: ZimaEntity,
        dimension: LinearDimension,
    ) -> dict[str, Any]:
        style = {
            "prefix": dimension.value_prefix,
            "suffix": dimension.value_suffix,
            "decimal_places": display_decimal_places(self),
            "tolerance_mode": "",
            "symmetric_tolerance": "",
            "single_tolerance": "",
            "upper_tolerance": "",
            "lower_tolerance": "",
        }
        style.update(
            self._dimension_styles(entity).get(dimension.key, {})
        )
        return style

    def _format_dimension_display(
        self,
        value: Any,
        decimal_places: int,
    ) -> str:
        try:
            formatted = f"{float(value):.{decimal_places}f}"
        except (TypeError, ValueError):
            return str(value)
        if "." in formatted:
            formatted = formatted.rstrip("0").rstrip(".")
        if formatted in ("-0", ""):
            formatted = "0"
        if self.settings.language in ("cs", "de", "fr"):
            formatted = formatted.replace(".", ",")
        return formatted

    def _configure_dimension_overlay(
        self,
        overlay: ParameterEditOverlay,
        entity: ZimaEntity,
        dimension: LinearDimension,
        value: Any,
    ) -> None:
        style = self._dimension_style(entity, dimension)
        try:
            decimal_places = max(
                0,
                min(12, int(style.get("decimal_places", 3))),
            )
        except (TypeError, ValueError):
            decimal_places = display_decimal_places(self)
        suffix = str(style.get("suffix", ""))
        tolerance_mode = str(style.get("tolerance_mode", ""))
        upper = str(style.get("upper_tolerance", "")).strip()
        lower = str(style.get("lower_tolerance", "")).strip()
        if not tolerance_mode and (upper or lower):
            tolerance_mode = (
                "deviations" if upper and lower else "single_deviation"
            )
        symmetric_tolerance = str(
            style.get("symmetric_tolerance", "")
        ).strip()
        if symmetric_tolerance.startswith("±"):
            symmetric_tolerance = symmetric_tolerance[1:]
        single_tolerance = str(
            style.get("single_tolerance", "")
        ).strip()
        if (
            tolerance_mode == "single_deviation"
            and not single_tolerance
        ):
            single_tolerance = upper or lower
        overlay.show_value(
            self._format_display_value(value),
            suffix,
            str(style.get("prefix", "")),
            display_value=self._format_dimension_display(
                value,
                decimal_places,
            ),
            tolerance_mode=tolerance_mode,
            tolerance_value=(
                symmetric_tolerance
                if tolerance_mode == "symmetric"
                else single_tolerance
            ),
            upper_deviation=upper,
            lower_deviation=lower,
        )
        overlay.contextMenuRequested.connect(
            lambda position, key=dimension.key:
            self._show_dimension_context_menu(key, position)
        )

    def _show_dimension_context_menu(
        self,
        key: str,
        global_position: QPoint,
    ) -> None:
        self._select_dimension_overlay(key)
        menu = QMenu(self)
        properties_action = menu.addAction(
            tr("menu.context.dimension_properties")
        )
        chosen = menu.exec(global_position)
        if chosen is properties_action:
            self._edit_dimension_properties(key)

    def _edit_dimension_properties(self, key: str) -> None:
        if self.document is None or self._dimension_object_id is None:
            return
        entity = self.document.find_entity(self._dimension_object_id)
        if entity is None:
            return
        styles = self._dimension_styles(entity)
        dimension = next(
            (
                candidate
                for candidate in self.native_viewer._dimensions
                if candidate.key == key
            ),
            None,
        )
        current_style = (
            self._dimension_style(entity, dimension)
            if dimension is not None
            else styles.get(key, {})
        )
        dialog = DimensionPropertiesDialog(
            current_style,
            display_decimal_places(self),
            self,
        )
        def apply_style(style: dict[str, Any]) -> None:
            styles[key] = style
            entity.parameters["dimension_styles"] = json.dumps(
                styles,
                ensure_ascii=False,
            )
            self._mark_model_for_regeneration()
            if entity.kind == EntityKind.SKETCH:
                if self._sketch_show_all_dimensions:
                    self._show_all_sketch_dimensions(entity)
                    return
                point_id = str(
                    self._dimension_bindings.get(
                        key,
                        ("", "", ""),
                    )[1]
                )
                point = next(
                    (
                        candidate
                        for candidate in self._stored_sketch_entities(
                            entity
                        )
                        if str(candidate.get("id", "")) == point_id
                    ),
                    None,
                )
                if point is not None:
                    self._show_sketch_point_dimensions(entity, point)
            else:
                self._show_edit_overlays(
                    entity,
                    QPoint(
                        self.native_viewer.width() // 2,
                        self.native_viewer.height() // 2,
                    ),
                )

        dialog.applied.connect(apply_style)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            apply_style(dialog.dimension_style())

    def _select_dimension_overlay(self, key: str) -> None:
        self.native_viewer.set_selected_dimension(key)
        for overlay_key, overlay in self._dimension_overlays.items():
            overlay.set_selected(overlay_key == key)

    def _clear_dimension_overlays(self) -> None:
        for overlay in self._dimension_overlays.values():
            overlay.hide()
            overlay.deleteLater()
        self._dimension_overlays.clear()
        self._dimension_object_id = None
        self._dimension_bindings.clear()
        self.native_viewer.set_dimensions(())

    def _dismiss_dimension_overlays(self) -> None:
        if (
            self._sketch_edit_entity_id is not None
            and self._sketch_show_all_dimensions
        ):
            return
        self._clear_dimension_overlays()

    def _position_dimension_overlays(self) -> None:
        for key, overlay in self._dimension_overlays.items():
            position = self.native_viewer.dimension_value_position(key)
            if position is not None:
                overlay.move_to(position.toPoint())

    def _commit_dimension_value(self, key: str, raw_value: str) -> None:
        if self.document is None or self._dimension_object_id is None:
            return
        entity = self.document.find_entity(self._dimension_object_id)
        if entity is None:
            self._clear_dimension_overlays()
            return
        binding = self._dimension_bindings.get(key)
        if binding is None:
            return
        unit = str(entity.parameters.get("unit", "mm")).strip()
        value_text = raw_value.strip()
        if unit and value_text.lower().endswith(unit.lower()):
            value_text = value_text[:-len(unit)].strip()
        try:
            value = float(value_text.replace(",", "."))
        except ValueError:
            self.statusBar().showMessage(
                tr("dimension.invalid_value", value=raw_value)
            )
            return
        if (
            not math.isfinite(value)
            or (binding[0] == "parameter" and value < 0.0)
        ):
            self.statusBar().showMessage(
                tr("dimension.invalid_value", value=raw_value)
            )
            return
        if binding[0] == "sketch_point":
            point_id = str(binding[1])
            coordinate = str(binding[2])
            entities = self._stored_sketch_entities(entity)
            point = next(
                (
                    item
                    for item in entities
                    if item.get("type") == "point"
                    and str(item.get("id", "")) == point_id
                ),
                None,
            )
            if point is None or coordinate not in ("x", "y"):
                self._clear_dimension_overlays()
                return
            if coordinate in self._sketch_point_locked_coordinates(
                entity,
                point,
            ):
                self.statusBar().showMessage(
                    tr("sketch.status.coordinate_constrained")
                )
                if self._sketch_show_all_dimensions:
                    self._show_all_sketch_dimensions(entity)
                else:
                    self._show_sketch_point_dimensions(entity, point)
                return
            point[coordinate] = value
            self._apply_sketch_point_reference_constraints(
                entity,
                point,
            )
            self._apply_sketch_geometry_constraints(entities)
            self._apply_sketch_coincident_constraints(entities)
            entity.parameters["sketch_entities"] = json.dumps(
                entities,
                ensure_ascii=False,
            )
        elif binding[0] == "reference_offset":
            references = self._constraint_references(entity)
            reference_index = int(binding[1])
            if not 0 <= reference_index < len(references):
                self._clear_dimension_overlays()
                return
            references[reference_index]["offset"] = value
            entity.parameters["constraint_refs"] = json.dumps(
                references,
                ensure_ascii=False,
            )
            owner = self.document.find_owning_object(entity.entity_id)
            fallback = self._constraint_fallback(entity, owner)
            solution, _dof, _status, _constrained = (
                self._solve_point_constraints(references, fallback)
            )
            if solution is None:
                self.statusBar().showMessage(
                    tr("dimension.invalid_value", value=raw_value)
                )
                return
            if owner is not None:
                owner.coordinate_system.origin = solution
        else:
            entity.parameters[str(binding[1])] = f"{value:.12g}"
        self._mark_model_for_regeneration()
        self.rebuild_view(fit=False)
        if binding[0] == "sketch_point":
            point = next(
                (
                    item
                    for item in self._stored_sketch_entities(entity)
                    if item.get("type") == "point"
                    and str(item.get("id", "")) == str(binding[1])
                ),
                None,
            )
            if point is not None:
                if self._sketch_show_all_dimensions:
                    self._show_all_sketch_dimensions(entity)
                else:
                    self._show_sketch_point_dimensions(entity, point)
            self._refresh_sketch_overlay()
        else:
            self._show_edit_overlays(
                entity,
                QPoint(
                    self.native_viewer.width() // 2,
                    self.native_viewer.height() // 2,
                ),
            )
        self.statusBar().showMessage(
            tr(
                "dimension.value_updated",
                value=self._format_display_value(value),
                unit=unit,
            )
        )

    @staticmethod
    def _constraint_references(
        entity: ZimaEntity,
    ) -> list[dict[str, Any]]:
        try:
            references = json.loads(
                str(entity.parameters.get("constraint_refs", "[]"))
            )
        except (TypeError, ValueError, json.JSONDecodeError):
            return []
        return references if isinstance(references, list) else []

    @staticmethod
    def _constraint_fallback(
        entity: ZimaEntity,
        owner: ZimaEntity | None,
    ) -> tuple[float, float, float]:
        current = (
            owner.coordinate_system.origin
            if owner is not None
            else (0.0, 0.0, 0.0)
        )
        try:
            return tuple(
                float(
                    entity.parameters.get(
                        f"fallback_{axis}",
                        current[index],
                    )
                )
                for index, axis in enumerate(("x", "y", "z"))
            )
        except (TypeError, ValueError):
            return current

    def _construction_entity_dimensions(
        self,
        entity: ZimaEntity,
    ) -> tuple[LinearDimension, ...]:
        if (
            self.document is None
            or entity.kind not in (EntityKind.AXIS, EntityKind.PLANE)
        ):
            return ()
        transform = entity_world_transform(
            self.document,
            entity.entity_id,
        )
        if transform is None:
            return ()

        def world(point):
            return transform_point(transform, point)

        if entity.kind == EntityKind.AXIS:
            length = max(
                0.0,
                float(entity.parameters.get("length", 50.0)),
            )
            direction = {
                "x": (1.0, 0.0, 0.0),
                "y": (0.0, 1.0, 0.0),
                "z": (0.0, 0.0, 1.0),
            }.get(
                str(entity.parameters.get("axis", "z")),
                (0.0, 0.0, 1.0),
            )
            helper = (
                (0.0, 1.0, 0.0)
                if abs(direction[1]) < 0.9
                else (1.0, 0.0, 0.0)
            )
            margin = max(5.0, length * 0.16)
            first = tuple(-length * 0.5 * value for value in direction)
            second = tuple(length * 0.5 * value for value in direction)
            first_dimension = tuple(
                first[index] + margin * helper[index]
                for index in range(3)
            )
            second_dimension = tuple(
                second[index] + margin * helper[index]
                for index in range(3)
            )
            world_origin = world((0.0, 0.0, 0.0))
            world_direction_end = world(direction)
            world_direction = tuple(
                world_direction_end[index] - world_origin[index]
                for index in range(3)
            )
            return (
                LinearDimension(
                    key="length",
                    first_point=world(first),
                    second_point=world(second),
                    first_dimension_point=world(first_dimension),
                    second_dimension_point=world(second_dimension),
                    direction=world_direction,
                ),
            )

        size = max(0.0, float(entity.parameters.get("size", 50.0)))
        half = size * 0.5
        margin = max(5.0, size * 0.16)
        plane = str(entity.parameters.get("plane", "xy"))
        if plane == "yz":
            first, second = (0.0, -half, -half), (0.0, half, -half)
            offset, direction = (0.0, 0.0, -margin), (0.0, 1.0, 0.0)
        elif plane == "xz":
            first, second = (-half, 0.0, -half), (half, 0.0, -half)
            offset, direction = (0.0, 0.0, -margin), (1.0, 0.0, 0.0)
        else:
            first, second = (-half, -half, 0.0), (half, -half, 0.0)
            offset, direction = (0.0, -margin, 0.0), (1.0, 0.0, 0.0)
        first_dimension = tuple(
            first[index] + offset[index] for index in range(3)
        )
        second_dimension = tuple(
            second[index] + offset[index] for index in range(3)
        )
        world_origin = world((0.0, 0.0, 0.0))
        world_direction_end = world(direction)
        return (
            LinearDimension(
                key="size",
                first_point=world(first),
                second_point=world(second),
                first_dimension_point=world(first_dimension),
                second_dimension_point=world(second_dimension),
                direction=tuple(
                    world_direction_end[index] - world_origin[index]
                    for index in range(3)
                ),
            ),
        )

    def _reference_dimensions(
        self,
        entity: ZimaEntity,
    ) -> tuple[LinearDimension, ...]:
        if self.document is None or entity.kind == EntityKind.SKETCH:
            return ()
        references = self._constraint_references(entity)
        if not references:
            return ()
        owner = self.document.find_owning_object(entity.entity_id)
        anchor = (
            owner.coordinate_system.origin
            if owner is not None
            else self._reference_origin(entity)
        )
        dimensions = []
        for index, descriptor in enumerate(references):
            rows = None
            if descriptor.get("type") == "face":
                rows = self._resolved_shape_reference_equations(descriptor)
                if rows is None:
                    rows = descriptor.get("equations", ())
            elif descriptor.get("type") == "entity":
                reference = self.document.find_entity(
                    str(descriptor.get("entity_id", ""))
                )
                if reference is not None and reference.kind == EntityKind.PLANE:
                    point = self._reference_origin(reference)
                    local_normal = {
                        "xy": (0.0, 0.0, 1.0),
                        "yz": (1.0, 0.0, 0.0),
                        "xz": (0.0, 1.0, 0.0),
                    }.get(
                        str(reference.parameters.get("plane", "xy")),
                        (0.0, 0.0, 1.0),
                    )
                    normal = self._reference_direction(
                        reference,
                        local_normal,
                    )
                    rows = [
                        [
                            *normal,
                            sum(
                                normal[axis] * point[axis]
                                for axis in range(3)
                            ),
                        ]
                    ]
            if not rows:
                continue
            normal = self._normalized_vector(tuple(rows[0][:3]))
            if normal == (0.0, 0.0, 0.0):
                continue
            distance = float(rows[0][3])
            offset = float(descriptor.get("offset", 0.0))
            base = tuple(
                anchor[axis]
                - normal[axis]
                * (
                    sum(
                        normal[item] * anchor[item]
                        for item in range(3)
                    )
                    - distance
                )
                for axis in range(3)
            )
            offset_point = tuple(
                base[axis] + normal[axis] * offset
                for axis in range(3)
            )
            helper = (
                (1.0, 0.0, 0.0)
                if abs(normal[0]) < 0.9
                else (0.0, 1.0, 0.0)
            )
            tangent = self._normalized_vector(
                self._cross_product(normal, helper)
            )
            margin = 8.0
            first_dimension = tuple(
                base[axis] + tangent[axis] * margin
                for axis in range(3)
            )
            second_dimension = tuple(
                offset_point[axis] + tangent[axis] * margin
                for axis in range(3)
            )
            dimensions.append(
                LinearDimension(
                    key=f"reference_offset:{index}",
                    first_point=base,
                    second_point=offset_point,
                    first_dimension_point=first_dimension,
                    second_dimension_point=second_dimension,
                    direction=normal,
                )
            )
        return tuple(dimensions)

    def _primitive_dimensions(
        self,
        primitive: ZimaEntity,
    ) -> tuple[LinearDimension, ...]:
        if self.document is None:
            return ()
        transform = entity_world_transform(
            self.document,
            primitive.entity_id,
        )
        if transform is None:
            return ()

        def number(key: str, fallback: float) -> float:
            try:
                return float(primitive.parameters.get(key, fallback))
            except (TypeError, ValueError):
                return fallback

        length = number("length", 40.0)
        width = number("width", 30.0)
        height = number("height", 20.0)
        diameter = number("diameter", 30.0)
        bottom_diameter = number("bottom_diameter", 40.0)
        top_diameter = number("top_diameter", 0.0)
        top_offset = number("top_offset", 0.0)
        scale = max(
            5.0,
            abs(length),
            abs(width),
            abs(height),
            abs(diameter),
            abs(bottom_diameter),
            abs(top_diameter),
        )
        margin = max(5.0, scale * 0.16)
        specifications: list[
            tuple[str, tuple[float, float, float],
                  tuple[float, float, float],
                  tuple[float, float, float],
                  tuple[float, float, float]]
        ] = []

        if primitive.kind == EntityKind.BOX:
            specifications.extend(
                (
                    (
                        "length",
                        (-length / 2, -width / 2, -height / 2),
                        (length / 2, -width / 2, -height / 2),
                        (0.0, -margin, 0.0),
                        (1.0, 0.0, 0.0),
                    ),
                    (
                        "width",
                        (length / 2, -width / 2, -height / 2),
                        (length / 2, width / 2, -height / 2),
                        (margin, 0.0, 0.0),
                        (0.0, 1.0, 0.0),
                    ),
                    (
                        "height",
                        (length / 2, width / 2, -height / 2),
                        (length / 2, width / 2, height / 2),
                        (margin, margin, 0.0),
                        (0.0, 0.0, 1.0),
                    ),
                )
            )
        elif primitive.kind in (EntityKind.PYRAMID, EntityKind.WEDGE):
            specifications.extend(
                (
                    (
                        "length",
                        (-length / 2, -width / 2, 0.0),
                        (length / 2, -width / 2, 0.0),
                        (0.0, -margin, 0.0),
                        (1.0, 0.0, 0.0),
                    ),
                    (
                        "width",
                        (length / 2, -width / 2, 0.0),
                        (length / 2, width / 2, 0.0),
                        (margin, 0.0, 0.0),
                        (0.0, 1.0, 0.0),
                    ),
                    (
                        "height",
                        (length / 2, width / 2, 0.0),
                        (length / 2, width / 2, height),
                        (margin, margin, 0.0),
                        (0.0, 0.0, 1.0),
                    ),
                )
            )
            if primitive.kind == EntityKind.WEDGE:
                specifications.append(
                    (
                        "top_offset",
                        (-length / 2, width / 2, height),
                        (-length / 2 + top_offset, width / 2, height),
                        (0.0, margin, 0.0),
                        (1.0, 0.0, 0.0),
                    )
                )
        elif primitive.kind == EntityKind.SPHERE:
            radius = diameter / 2.0
            specifications.append(
                (
                    "diameter",
                    (-radius, 0.0, 0.0),
                    (radius, 0.0, 0.0),
                    (0.0, margin, 0.0),
                    (1.0, 0.0, 0.0),
                )
            )
        elif primitive.kind == EntityKind.CYLINDER:
            radius = diameter / 2.0
            specifications.extend(
                (
                    (
                        "diameter",
                        (-radius, 0.0, -height / 2),
                        (radius, 0.0, -height / 2),
                        (0.0, -margin, 0.0),
                        (1.0, 0.0, 0.0),
                    ),
                    (
                        "height",
                        (radius, 0.0, -height / 2),
                        (radius, 0.0, height / 2),
                        (margin, 0.0, 0.0),
                        (0.0, 0.0, 1.0),
                    ),
                )
            )
        elif primitive.kind == EntityKind.CONE:
            bottom_radius = bottom_diameter / 2.0
            top_radius = top_diameter / 2.0
            outer_radius = max(bottom_radius, top_radius)
            specifications.extend(
                (
                    (
                        "bottom_diameter",
                        (-bottom_radius, 0.0, 0.0),
                        (bottom_radius, 0.0, 0.0),
                        (0.0, -margin, 0.0),
                        (1.0, 0.0, 0.0),
                    ),
                    (
                        "top_diameter",
                        (-top_radius, 0.0, height),
                        (top_radius, 0.0, height),
                        (0.0, margin, 0.0),
                        (1.0, 0.0, 0.0),
                    ),
                    (
                        "height",
                        (outer_radius, 0.0, 0.0),
                        (outer_radius, 0.0, height),
                        (margin, 0.0, 0.0),
                        (0.0, 0.0, 1.0),
                    ),
                )
            )

        def world(point: tuple[float, float, float]):
            return transform_point(transform, point)

        dimensions = []
        for key, first, second, offset, direction in specifications:
            first_dimension = tuple(
                first[index] + offset[index] for index in range(3)
            )
            second_dimension = tuple(
                second[index] + offset[index] for index in range(3)
            )
            world_origin = world((0.0, 0.0, 0.0))
            world_direction_end = world(direction)
            world_direction = tuple(
                world_direction_end[index] - world_origin[index]
                for index in range(3)
            )
            dimensions.append(
                LinearDimension(
                    key=key,
                    first_point=world(first),
                    second_point=world(second),
                    first_dimension_point=world(first_dimension),
                    second_dimension_point=world(second_dimension),
                    direction=world_direction,
                    value_prefix=(
                        "⌀"
                        if key in {
                            "diameter",
                            "bottom_diameter",
                            "top_diameter",
                        }
                        else "R" if key == "radius" else ""
                    ),
                )
            )
        return tuple(dimensions)


    def _object_contains(self, parent: ZimaEntity, entity_id: str) -> bool:
        return any(
            child.entity_id == entity_id
            or self._object_contains(child, entity_id)
            for child in parent.children
        )

    def _first_editable_solid(self, obj: ZimaEntity) -> ZimaEntity | None:
        if obj.kind in SOLID_KINDS and not obj.locked:
            return obj
        for child in obj.children:
            solid = self._first_editable_solid(child)
            if solid is not None:
                return solid
        return None

    def _first_editable_dimension_entity(
        self,
        obj: ZimaEntity,
    ) -> ZimaEntity | None:
        if obj.kind == EntityKind.SKETCH:
            return None
        if (
            obj.kind in (
                EntityKind.POINT,
                EntityKind.AXIS,
                EntityKind.PLANE,
                *SOLID_KINDS,
            )
            and (
                obj.kind in SOLID_KINDS
                or "constraint_refs" in obj.parameters
            )
        ):
            return obj
        for child in obj.children:
            entity = self._first_editable_dimension_entity(child)
            if entity is not None:
                return entity
        return None

    def _editable_selected_object(self) -> ZimaEntity | None:
        obj = self._selected_object()
        if obj is None or obj.kind != EntityKind.CONTAINER:
            return None
        return obj

    def regenerate_model(self) -> None:
        if self.document is None:
            return

        regenerated_entities = 0
        unresolved_entities = 0
        for obj in self.document.active_history_objects():
            entity = (
                obj
                if "constraint_refs" in obj.parameters
                else next(
                (
                    child
                    for child in obj.children
                    if not child.locked
                    and (
                        child.kind
                        in (
                            EntityKind.POINT,
                            EntityKind.AXIS,
                            EntityKind.PLANE,
                            EntityKind.SKETCH,
                            *SOLID_KINDS,
                        )
                        and "constraint_refs" in child.parameters
                    )
                ),
                None,
                )
            )
            if entity is None:
                continue
            try:
                references = json.loads(
                    str(entity.parameters.get("constraint_refs", "[]"))
                )
            except (TypeError, ValueError, json.JSONDecodeError):
                references = []
            if not isinstance(references, list):
                references = []
            fallback_values = obj.coordinate_system.origin
            try:
                fallback = tuple(
                    float(
                        entity.parameters.get(
                            f"fallback_{axis}",
                            fallback_values[index],
                        )
                    )
                    for index, axis in enumerate(("x", "y", "z"))
                )
            except (TypeError, ValueError):
                fallback = fallback_values
            solution, _dof, _status, _constrained = self._solve_point_constraints(
                references,
                fallback,
            )
            if solution is None:
                unresolved_entities += 1
                continue
            obj.coordinate_system.origin = solution
            if (
                entity.kind in (EntityKind.PLANE, EntityKind.SKETCH)
                or str(
                    entity.parameters.get(
                        "reference_orientation",
                        "false",
                    )
                ).lower()
                == "true"
            ):
                base_rotation = self._plane_reference_rotation(references)
                offsets = tuple(
                    float(
                        entity.parameters.get(
                            f"rotation_offset_{axis}",
                            0.0,
                        )
                    )
                    for axis in ("x", "y", "z")
                )
                obj.coordinate_system.rotation = tuple(
                    base_rotation[index] + offsets[index]
                    for index in range(3)
                )
            regenerated_entities += 1

        self.document.resolve_attachments()
        self.document.regeneration_required = False
        selected_id = self.selected_object_id
        self._populate_tree()
        if selected_id is not None:
            self._select_tree_object(selected_id)
        self.rebuild_view(fit=False, rebuild_geometry=True)
        self.statusBar().showMessage(
            tr(
                "status.regeneration.complete"
                if unresolved_entities == 0
                else "status.regeneration.incomplete",
                count=regenerated_entities,
                unresolved=unresolved_entities,
            ),
            5000,
        )

    def rebuild_view(self, fit: bool = True, rebuild_geometry: bool = True) -> None:
        if self.document is not None and not hasattr(self, "_viewer_initialized"):
            self._ensure_viewer_initialized()

        if not hasattr(self, "_viewer_initialized"):
            return

        history_boundary = (
            self._definition_history_boundary()
            if self.document is not None
            else 0
        )
        if (
            self.document is not None
            and self._cached_history_boundary != history_boundary
        ):
            self._populate_tree()

        self._cached_document = self.document
        self._cached_history_boundary = history_boundary
        self._rebuild_native_view(history_boundary, fit)
        if (
            self.document is not None
            and not self._handling_workspace_update
        ):
            self.workspace.documentChanged.emit(self, self.document)

    def _rebuild_native_view(
        self,
        history_boundary: int,
        fit: bool,
    ) -> None:
        if self.document is None:
            self._native_viewer_scene = None
            self.native_viewer.clear_scene()
            return
        editing_object = self._definition_edit_object()
        if (
            editing_object is None
            and self._sketch_edit_entity_id is not None
        ):
            active_sketch = self.document.find_entity(
                self._sketch_edit_entity_id
            )
            if active_sketch is not None:
                editing_object = self.document.find_owning_object(
                    active_sketch.entity_id
                )
        self._native_viewer_scene = build_document_viewer_scene_data(
            self.document,
            history_boundary=history_boundary,
            # The locked global coordinate system is the document Origin.
            # Standalone datum points and axes belong to their own toggles.
            show_document_origin=self.show_origins_action.isChecked(),
            show_document_planes=self.show_planes_action.isChecked(),
            show_object_planes=self.show_planes_action.isChecked(),
            show_object_origins=self.show_origins_action.isChecked(),
            show_user_points=self.show_points_action.isChecked(),
            show_user_axes=self.show_axes_action.isChecked(),
            show_user_planes=self.show_planes_action.isChecked(),
            editing_object_id=(
                editing_object.entity_id
                if editing_object is not None
                else None
            ),
        )
        self._selectable_model_shapes = list(
            (
                shape,
                owner_id,
            )
            for owner_id, shape
            in self._native_viewer_scene.shapes_by_owner_id.items()
        )
        self._cached_model_shapes = [
            (shape, owner_id)
            for owner_id, shape
            in self._native_viewer_scene.shapes_by_owner_id.items()
            if owner_id == self.document.root.entity_id
        ]
        self._cached_source_model_shapes = []
        for source in self.document.history_objects_at(history_boundary):
            source_shape = self.document.build_standalone_shape(source)
            if source_shape is not None:
                self._cached_source_model_shapes.append(
                    (source_shape, source.entity_id)
                )
        self.native_viewer.set_mesh(
            self._native_viewer_scene.mesh,
            fit=fit,
        )
        self.native_viewer.set_display_mode(
            {
                ViewDisplayMode.WIRE: "wire",
                ViewDisplayMode.SHADED_WITH_EDGES: "shaded_with_edges",
                ViewDisplayMode.SHADED: "shaded",
            }[self.view_display_mode]
        )
        self.native_viewer.set_selection_filter(
            {
                ViewSelectionFilter.ALL: "all",
                ViewSelectionFilter.FACE: "face",
                ViewSelectionFilter.POINT: "point",
                ViewSelectionFilter.AXIS: "axis",
                ViewSelectionFilter.PLANE: "plane",
            }[self.view_selection_filter]
        )
        point_constraints_active = (
            self.point_constraint_dialog is not None
            and self.point_constraint_dialog.isVisible()
        )
        self.native_viewer.set_outline_face_highlights(
            point_constraints_active
        )
        self.native_viewer.set_interaction_mode(
            "object"
            if (
                self.view_selection_filter == ViewSelectionFilter.ALL
                and not point_constraints_active
            )
            else "topology"
        )
        self._sync_native_tree_selection()

    def _sync_native_tree_selection(self) -> None:
        if self.document is None:
            return
        signals_were_blocked = self.native_viewer.blockSignals(True)
        try:
            # A tree selection replaces every previous viewport selection.
            # Clear both object and topology highlights before applying the
            # new tree item so a previously selected face/edge cannot remain
            # highlighted alongside it.
            self.native_viewer._clear_topology_hover()
            self.native_viewer._clear_topology_selection()
            self.native_viewer.set_selected_reference_owner(None)
            self.native_viewer.set_selected_container_origin(None)
            self.native_viewer.set_selected_container_contents(set())
            self.native_viewer.set_object_overlay(None)
            self._sync_constraint_reference_highlights()
            if self._sketch_edit_entity_id is not None:
                return
            obj = self._selected_object()
            if obj is None:
                return
            selected_container = (
                obj
                if obj.kind == EntityKind.CONTAINER
                else (
                    self.document.find_parent(obj.entity_id)
                    if obj.kind in SOLID_KINDS
                    else None
                )
            )
            if (
                selected_container is not None
                and selected_container.kind == EntityKind.CONTAINER
            ):
                origin = next(
                    (
                        child
                        for child in selected_container.children
                        if child.kind == EntityKind.ORIGIN
                    ),
                    None,
                )
                if origin is not None:
                    self.native_viewer.set_selected_container_origin(
                        origin.entity_id
                    )
                content_ids: set[str] = set()

                def add_content_ids(parent: ZimaEntity) -> None:
                    for child in parent.children:
                        if child.kind == EntityKind.ORIGIN:
                            continue
                        content_ids.add(child.entity_id)
                        add_content_ids(child)

                add_content_ids(selected_container)
                self.native_viewer.set_selected_container_contents(
                    content_ids
                )
            user_reference = (
                obj
                if obj.kind in (
                    EntityKind.POINT,
                    EntityKind.AXIS,
                    EntityKind.PLANE,
                )
                and not obj.locked
                else next(
                    (
                        child
                        for child in obj.children
                        if child.kind in (
                            EntityKind.POINT,
                            EntityKind.AXIS,
                            EntityKind.PLANE,
                        )
                        and not child.locked
                    ),
                    None,
                )
            )
            if user_reference is not None:
                self.native_viewer.set_selected_reference_owner(
                    user_reference.entity_id
                )
                return
            if obj.kind == EntityKind.BODY:
                self.native_viewer._set_selected_object(
                    self.document.root.entity_id
                )
                return
            if obj.kind == EntityKind.SKETCH:
                self.native_viewer._set_selected_object(obj.entity_id)
                return
            if obj.kind == EntityKind.CONTAINER or obj.kind in SOLID_KINDS:
                container = (
                    obj
                    if obj.kind == EntityKind.CONTAINER
                    else self.document.find_parent(obj.entity_id)
                )
                if obj.kind == EntityKind.CONTAINER:
                    source_shape = self.document.build_standalone_shape(obj)
                    if source_shape is not None:
                        self.native_viewer.set_object_overlay(
                            triangulate_shape(
                                source_shape,
                                owner_id=obj.entity_id,
                            ),
                            selected=True,
                            anchor=self._native_object_origin(obj),
                        )
                    return
                self.native_viewer._set_selected_object(
                    container.entity_id
                    if (
                        self.document.body_is_suppressed()
                        and container is not None
                    )
                    else self.document.root.entity_id
                )
                return
            if obj.kind == EntityKind.ORIGIN:
                self.native_viewer.set_selected_reference_owner(obj.entity_id)
                return
            parent = self.document.find_parent(obj.entity_id)
            if parent is None or parent.kind != EntityKind.ORIGIN:
                return
            if obj.kind == EntityKind.AXIS:
                axis_index = {"x": 1, "y": 2, "z": 3}.get(
                    str(obj.parameters.get("axis", ""))
                )
                if axis_index is not None:
                    self.native_viewer._set_selected_edge(
                        (parent.entity_id, axis_index)
                    )
            elif obj.kind == EntityKind.PLANE:
                plane_index = {"xy": 1, "yz": 2, "xz": 3}.get(
                    str(obj.parameters.get("plane", ""))
                )
                if plane_index is not None:
                    self.native_viewer._set_selected_plane(
                        (parent.entity_id, plane_index)
                    )
            elif obj.kind == EntityKind.POINT:
                self.native_viewer._set_selected_point(
                    (parent.entity_id, 1)
                )
        finally:
            self.native_viewer.blockSignals(signals_were_blocked)

    def _sync_constraint_reference_highlights(self) -> None:
        dialog = self.point_constraint_dialog
        references = (
            [
                reference
                for reference in dialog.references
                if str(reference.get("key", ""))
                in dialog.highlighted_reference_keys
            ]
            if dialog is not None and dialog.isVisible()
            else []
        )
        owner_ids: set[str] = set()
        edges: set[tuple[str, int]] = set()
        points: set[tuple[str, int]] = set()
        planes: set[tuple[str, int]] = set()
        positions: set[tuple[float, float, float]] = set()
        for descriptor in references:
            entity_id = str(descriptor.get("entity_id", "")).strip()
            if not entity_id:
                continue
            reference_type = str(descriptor.get("type", ""))
            if reference_type == "entity":
                reference = (
                    self.document.find_entity(entity_id)
                    if self.document is not None
                    else None
                )
                parent = (
                    self.document.find_parent(entity_id)
                    if self.document is not None
                    else None
                )
                if (
                    reference is not None
                    and parent is not None
                    and parent.kind == EntityKind.ORIGIN
                ):
                    if reference.kind == EntityKind.AXIS:
                        index = {"x": 1, "y": 2, "z": 3}.get(
                            str(reference.parameters.get("axis", ""))
                        )
                        if index is not None:
                            edges.add((parent.entity_id, index))
                            continue
                    if reference.kind == EntityKind.PLANE:
                        index = {"xy": 1, "yz": 2, "xz": 3}.get(
                            str(reference.parameters.get("plane", ""))
                        )
                        if index is not None:
                            planes.add((parent.entity_id, index))
                            continue
                    if reference.kind == EntityKind.POINT:
                        points.add((parent.entity_id, 1))
                        continue
                owner_ids.add(entity_id)
                continue
            index_key = (
                "vertex_index"
                if reference_type == "vertex"
                else "topology_key"
            )
            try:
                topology_index = int(descriptor.get(index_key, 0))
            except (TypeError, ValueError):
                continue
            if topology_index <= 0:
                continue
            key = (entity_id, topology_index)
            if reference_type == "face":
                scene = self._native_viewer_scene
                face = (
                    scene.resolve_topology(
                        entity_id,
                        "face",
                        topology_index,
                    )
                    if scene is not None
                    else None
                )
                owner_shape = (
                    scene.shapes_by_owner_id.get(entity_id)
                    if scene is not None
                    else None
                )
                if face is not None and owner_shape is not None:
                    boundary_edges: list[Any] = []
                    face_explorer = TopExp_Explorer(face, TopAbs_EDGE)
                    while face_explorer.More():
                        boundary_edges.append(face_explorer.Current())
                        face_explorer.Next()
                    seen_edges: list[Any] = []
                    edge_index = 0
                    shape_explorer = TopExp_Explorer(
                        owner_shape,
                        TopAbs_EDGE,
                    )
                    while shape_explorer.More():
                        candidate = shape_explorer.Current()
                        if any(
                            candidate.IsSame(existing)
                            for existing in seen_edges
                        ):
                            shape_explorer.Next()
                            continue
                        seen_edges.append(candidate)
                        edge_index += 1
                        if any(
                            candidate.IsSame(boundary)
                            for boundary in boundary_edges
                        ):
                            edges.add((entity_id, edge_index))
                        shape_explorer.Next()
            elif reference_type == "edge":
                edges.add(key)
            elif reference_type == "vertex":
                points.add(key)
                equations = descriptor.get("equations", [])
                try:
                    positions.add(
                        (
                            float(equations[0][3]),
                            float(equations[1][3]),
                            float(equations[2][3]),
                        )
                    )
                except (IndexError, TypeError, ValueError):
                    pass
        self.native_viewer.set_constraint_reference_highlights(
            owner_ids=owner_ids,
            edges=edges,
            points=points,
            planes=planes,
            positions=positions,
        )

    def _native_object_origin(
        self,
        obj: ZimaEntity | None,
    ) -> tuple[float, float, float] | None:
        if self.document is None or obj is None:
            return None
        world_transform = entity_world_transform(
            self.document,
            obj.entity_id,
        )
        if world_transform is None:
            return None
        return transform_point(world_transform, (0.0, 0.0, 0.0))

def main() -> int:
    try:
        startup_context, qt_arguments = resolve_startup_context(sys.argv[1:])
    except ValueError as exc:
        app = QApplication(sys.argv)
        QMessageBox.critical(None, "ZIMA-CAD", str(exc))
        return 2

    app = QApplication([sys.argv[0], *qt_arguments])
    app_icon = QIcon(
        str(app_path("resources", "branding", "app-icon.svg"))
    )
    app.setWindowIcon(app_icon)
    splash_pixmap = QPixmap(
        str(app_path("resources", "branding", "splash.svg"))
    )
    splash = None
    if not splash_pixmap.isNull():
        splash = QSplashScreen(splash_pixmap)
        splash.setWindowIcon(app_icon)
        splash.show()
        app.processEvents()
    window = MainWindow(startup_context)
    window.showMaximized()
    if splash is not None:
        splash.finish(window)
    if startup_context.document_path is not None:
        QTimer.singleShot(
            0,
            lambda: window.open_document_path(startup_context.document_path),
        )
    return app.exec()
