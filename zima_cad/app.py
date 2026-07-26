from __future__ import annotations

import sys
import copy
import configparser
import io
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Callable

from OCC.Display.backend import load_backend

load_backend("pyside6")

from OCC.Display.qtDisplay import qtViewer3d
from OCC.Core.Graphic3d import (
    Graphic3d_TMF_ZoomPers,
    Graphic3d_TransformPers,
    Graphic3d_ZLayerId_TopOSD,
    Graphic3d_ZLayerId_Topmost,
)
from OCC.Core.Aspect import Aspect_TOL_DOTDASH
from OCC.Core.AIS import AIS_Shaded, AIS_WireFrame
from OCC.Core.Quantity import (
    Quantity_Color,
    Quantity_NOC_BLACK,
    Quantity_NOC_BLUE1,
    Quantity_NOC_GREEN,
    Quantity_NOC_RED,
    Quantity_NOC_YELLOW,
    Quantity_TOC_RGB,
)
from OCC.Core.Prs3d import (
    Prs3d_Drawer,
    Prs3d_TypeOfHighlight_Dynamic,
    Prs3d_TypeOfHighlight_LocalDynamic,
    Prs3d_TypeOfHighlight_LocalSelected,
    Prs3d_TypeOfHighlight_Selected,
)
from OCC.Core.gp import gp_Dir, gp_Lin, gp_Pnt
from OCC.Core.IntCurvesFace import IntCurvesFace_ShapeIntersector
from OCC.Core.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_REVERSED
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.BRepAdaptor import BRepAdaptor_Surface
from OCC.Core.GeomAbs import GeomAbs_Plane
from PySide6.QtGui import QAction, QActionGroup, QBrush, QColor
from PySide6.QtCore import QPoint, QTimer, Qt, Signal
from PySide6.QtWidgets import (
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
    QTabBar,
    QTableWidget,
    QTableWidgetItem,
    QToolBar,
    QTreeWidget,
    QTreeWidgetItem,
    QHBoxLayout,
    QSizeGrip,
    QVBoxLayout,
    QWidget,
)

from zima_cad.model import (
    CombineMode,
    ObjectKind,
    SOLID_KINDS,
    SketchRole,
    PartDocument,
    PlaneOnFaceAttachment,
    TreeExposure,
    ZimaObject,
    apply_object_to_shape,
    solid_face_frames,
    coordinate_system_transform,
    create_empty_part,
    identity_transform,
    make_sketch_shape,
    make_axis_label_points,
    make_datum_axis_shape,
    make_plane_label_points,
    make_origin_shapes,
    multiply_transforms,
    transform_point,
    transform_shape,
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
from zima_cad.storage import (
    ObjectEntityLimitError,
    load_part_document,
    save_part_document,
)
from zima_cad.versioned_io import validate_ini_file, write_text_versioned


PLANE_COLOR_RGB = (0.43, 0.24, 0.08)
PLANE_COLOR = Quantity_Color(*PLANE_COLOR_RGB, Quantity_TOC_RGB)
BLACK = Quantity_Color(Quantity_NOC_BLACK)
BLUE = Quantity_Color(Quantity_NOC_BLUE1)
GREEN = Quantity_Color(Quantity_NOC_GREEN)
RED = Quantity_Color(Quantity_NOC_RED)
YELLOW = Quantity_Color(Quantity_NOC_YELLOW)
CENTERLINE_COLOR_RGB = (0.18, 0.18, 0.18)
CENTERLINE_COLOR = Quantity_Color(*CENTERLINE_COLOR_RGB, Quantity_TOC_RGB)


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

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._dragging_rollback = False
        self._pending_history_object_id: str | None = None
        self._dragging_history_object = False
        self._drag_start = QPoint()

    def mousePressEvent(self, event) -> None:
        item = self.itemAt(event.position().toPoint())
        if (
            event.button() == Qt.MouseButton.LeftButton
            and item is not None
            and item.data(0, self.ROLLBACK_ROLE)
        ):
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
            self.viewport().setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return
        if self._dragging_history_object:
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event) -> None:
        if self._dragging_history_object:
            object_id = self._pending_history_object_id
            self._pending_history_object_id = None
            self._dragging_history_object = False
            self.viewport().unsetCursor()
            y = event.position().toPoint().y()
            target_index = 0
            for index in range(self.topLevelItemCount()):
                item = self.topLevelItem(index)
                if (
                    not item.data(0, self.HISTORY_OBJECT_ROLE)
                    or item.data(0, Qt.ItemDataRole.UserRole) == object_id
                ):
                    continue
                if self.visualItemRect(item).center().y() < y:
                    target_index += 1
            if object_id is not None:
                self.historyObjectMoved.emit(object_id, target_index)
            event.accept()
            return
        self._pending_history_object_id = None
        if not self._dragging_rollback:
            super().mouseReleaseEvent(event)
            return
        self._dragging_rollback = False
        self.viewport().unsetCursor()
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
    OBJECT = "object"
    FACE = "face"


class ViewSelectionFilter(str, Enum):
    ALL = "all"
    OBJECT = "object"
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


def canonical_document_path(file_path: Path) -> Path:
    """Normalize relative paths and symlinks for open-document comparisons."""
    return file_path.expanduser().resolve(strict=False)


class NewDocumentDialog(QDialog):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.new.title"))

        layout = QVBoxLayout(self)

        form = QFormLayout()
        self.file_name_edit = QLineEdit("part")
        form.addRow(tr("dialog.new.file_name"), self.file_name_edit)
        layout.addLayout(form)

        layout.addWidget(QLabel(tr("dialog.new.document_type")))

        self.part_radio = QRadioButton(tr("dialog.new.part"))
        self.assembly_radio = QRadioButton(tr("dialog.new.assembly"))
        self.drawing_radio = QRadioButton(tr("dialog.new.drawing"))
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


class ObjectPropertiesDialog(QDialog):
    applied = Signal()

    def __init__(self, obj: ZimaObject, document: PartDocument, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("dialog.properties.title", name=obj.name))
        self.object = obj
        self.document = document
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
            tr("dialog.properties.object_type"),
            QLabel(obj.object_type.value),
        )
        layout.addRow("X", self.x_spin)
        layout.addRow("Y", self.y_spin)
        layout.addRow("Z", self.z_spin)
        layout.addRow("RX", self.rx_spin)
        layout.addRow("RY", self.ry_spin)
        layout.addRow("RZ", self.rz_spin)

        attachment = obj.attachment
        if attachment is not None:
            target = document.find_object(attachment.target_object_id)
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
        spinbox.setDecimals(3)
        spinbox.setSingleStep(1.0)
        spinbox.setSuffix(" mm")
        return spinbox

    def _create_rotation_spinbox(self) -> QDoubleSpinBox:
        spinbox = QDoubleSpinBox()
        spinbox.setRange(-360_000.0, 360_000.0)
        spinbox.setDecimals(3)
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
        ObjectKind.BOX: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        ObjectKind.SPHERE: (
            ("diameter", "primitive.parameter.diameter", 0.001),
        ),
        ObjectKind.CYLINDER: (
            ("diameter", "primitive.parameter.diameter", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        ObjectKind.CONE: (
            ("bottom_diameter", "primitive.parameter.bottom_diameter", 0.001),
            ("top_diameter", "primitive.parameter.top_diameter", 0.0),
            ("height", "primitive.parameter.height", 0.001),
        ),
        ObjectKind.PYRAMID: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
        ),
        ObjectKind.WEDGE: (
            ("length", "primitive.parameter.length", 0.001),
            ("width", "primitive.parameter.width", 0.001),
            ("height", "primitive.parameter.height", 0.001),
            ("top_offset", "primitive.parameter.top_offset", 0.0),
        ),
    }

    def __init__(self, primitive: ZimaObject, parent=None) -> None:
        super().__init__(parent)
        self.primitive = primitive
        self.setWindowTitle(
            tr("dialog.primitive.title", name=primitive.name)
        )

        layout = QFormLayout(self)
        self.name_edit = QLineEdit(primitive.name)
        layout.addRow(tr("dialog.properties.name"), self.name_edit)
        layout.addRow(
            tr("dialog.properties.object_type"),
            QLabel(tr(f"primitive.{primitive.kind.value}")),
        )

        self.parameter_edits: dict[str, QDoubleSpinBox] = {}
        for key, label_key, minimum in self.PARAMETER_DEFINITIONS[primitive.kind]:
            spinbox = QDoubleSpinBox()
            spinbox.setRange(minimum, 1_000_000.0)
            spinbox.setDecimals(3)
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


class DatumAxisPropertiesDialog(QDialog):
    applied = Signal()

    def __init__(self, axis: ZimaObject, parent=None) -> None:
        super().__init__(parent)
        self.axis = axis
        self.setWindowTitle(tr("dialog.axis.title", name=axis.name))
        layout = QFormLayout(self)
        self.name_edit = QLineEdit(axis.name)
        self.direction_combo = QComboBox()
        for direction in ("X", "Y", "Z"):
            self.direction_combo.addItem(direction, direction.lower())
        self.direction_combo.setCurrentIndex(
            max(0, self.direction_combo.findData(axis.parameters.get("axis", "z")))
        )
        self.length_spin = QDoubleSpinBox()
        self.length_spin.setRange(0.001, 1_000_000.0)
        self.length_spin.setDecimals(3)
        self.length_spin.setSuffix(" mm")
        self.length_spin.setValue(float(axis.parameters.get("length", 100.0)))
        layout.addRow(tr("dialog.properties.name"), self.name_edit)
        layout.addRow(tr("dialog.axis.display"), QLabel(tr("dialog.axis.centerline")))
        layout.addRow(tr("dialog.axis.direction"), self.direction_combo)
        layout.addRow(tr("dialog.axis.length"), self.length_spin)
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

    def apply_to_axis(self) -> bool:
        name = self.name_edit.text().strip()
        if not name:
            return False
        self.axis.name = name
        self.axis.parameters["display_style"] = "centerline"
        self.axis.parameters["axis"] = str(self.direction_combo.currentData())
        self.axis.parameters["length"] = str(self.length_spin.value())
        return True

    def _apply_without_closing(self) -> None:
        if self.apply_to_axis():
            self.applied.emit()


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


class ZimaViewer(qtViewer3d):
    rotation_sensitivity = 1.8

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._select_cycled_detection = False
        self._ignore_next_left_release = False
        self.selection_enabled = True

    def _occ_mouse_position(self, event) -> tuple[int, int]:
        return self.occ_position(event.position())

    def occ_position(self, point) -> tuple[int, int]:
        pixel_ratio = float(self.devicePixelRatioF())
        return (
            int(round(point.x() * pixel_ratio)),
            int(round(point.y() * pixel_ratio)),
        )

    def mouseReleaseEvent(self, event) -> None:
        if (
            event.button() == Qt.MouseButton.LeftButton
            and self._ignore_next_left_release
        ):
            self._ignore_next_left_release = False
            event.accept()
            return
        if (
            event.button() == Qt.MouseButton.LeftButton
            and not self.selection_enabled
        ):
            event.accept()
            return
        if event.button() != Qt.MouseButton.LeftButton:
            super().mouseReleaseEvent(event)
            return

        x, y = self._occ_mouse_position(event)
        window = self.window()
        if (
            event.modifiers() == Qt.KeyboardModifier.NoModifier
            and hasattr(window, "_confirm_preview_selection")
            and window._confirm_preview_selection()
        ):
            event.accept()
            return
        if event.modifiers() == Qt.KeyboardModifier.ShiftModifier:
            self._display.ShiftSelect(x, y)
        elif self._select_cycled_detection and self._display.Context.HasDetected():
            self._display.Context.SelectDetected()
            self._display.Context.InitSelected()
            self._display.selected_shapes = []
            if (
                self._display.Context.MoreSelected()
                and self._display.Context.HasSelectedShape()
            ):
                self._display.selected_shapes.append(
                    self._display.Context.SelectedShape()
                )
            for callback in self._display._select_callbacks:
                callback(self._display.selected_shapes, x, y)
            self._display.Context.UpdateSelected(True)
        else:
            self._display.Select(x, y)
        if self._display.selected_shapes is not None:
            self.sig_topods_selected.emit(self._display.selected_shapes)
        self.cursor = "arrow"

    def mouseDoubleClickEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._ignore_next_left_release = True
            event.accept()
            return
        super().mouseDoubleClickEvent(event)

    def mouseMoveEvent(self, event) -> None:
        point = event.pos()
        buttons = event.buttons()

        if (
            buttons == Qt.MouseButton.MiddleButton
            and event.modifiers() == Qt.KeyboardModifier.ShiftModifier
        ):
            dx = point.x() - self.dragStartPosX
            dy = point.y() - self.dragStartPosY
            self.dragStartPosX = point.x()
            self.dragStartPosY = point.y()
            self.cursor = "pan"
            self._display.Pan(dx, -dy)
            self._drawbox = False
            return

        if buttons == Qt.MouseButton.MiddleButton:
            dx = point.x() - self.dragStartPosX
            dy = point.y() - self.dragStartPosY
            sensitive_x = self.dragStartPosX + int(dx * self.rotation_sensitivity)
            sensitive_y = self.dragStartPosY + int(dy * self.rotation_sensitivity)
            self.cursor = "rotate"
            self._display.Rotation(sensitive_x, sensitive_y)
            self._drawbox = False
            return

        if buttons == Qt.MouseButton.RightButton:
            self.cursor = "arrow"
            self._drawbox = False
            return

        self._drawbox = False
        if not self.selection_enabled:
            window = self.window()
            if hasattr(window, "_on_view_hover_changed"):
                window._on_view_hover_changed(None)
            self.cursor = "arrow"
            return
        x, y = self._occ_mouse_position(event)
        self._select_cycled_detection = False
        self._display.MoveTo(x, y)
        window = self.window()
        if hasattr(window, "_on_view_hover_changed"):
            detected_shape = None
            detected_interactive = None
            if self._display.Context.HasDetectedShape():
                detected_shape = self._display.Context.DetectedShape()
            if self._display.Context.HasDetected():
                detected_interactive = self._display.Context.DetectedInteractive()
            window._on_view_hover_changed(detected_shape, detected_interactive)
        self.cursor = "arrow"

    def wheelEvent(self, event) -> None:
        delta = event.angleDelta().y()
        if delta == 0:
            return

        zoom_factor = 1.25 if delta > 0 else 0.8
        self._display.ZoomFactor(zoom_factor)
        self._display.Repaint()
        event.accept()


class MainWindow(QMainWindow):
    def __init__(self, startup_context: StartupContext | None = None) -> None:
        super().__init__()

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
        self.setWindowTitle(tr("app.title"))
        self.resize(1200, 800)
        self.document_sessions: list[DocumentSession] = []
        self.active_document_index = -1
        self.document: PartDocument | None = None
        self.current_file_path: Path | None = None
        self.working_directory = self.startup_context.working_directory

        self.tree = HistoryTreeWidget()
        self.tree.setHeaderLabels(
            [
                tr("tree.header"),
                tr("tree.visibility"),
                tr("tree.state"),
            ]
        )
        self.tree.setMinimumWidth(280)
        self.tree.setStyleSheet(
            """
            QTreeWidget::item:selected,
            QTreeWidget::item:selected:active,
            QTreeWidget::item:selected:!active {
                background-color: #245D8F;
                color: #FFFFFF;
            }
            QTreeWidget::item:hover {
                background-color: #245D8F;
                color: #FFFFFF;
            }
            """
        )
        self.tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)

        self.selected_object_id: str | None = None
        self._plane_label_structures = []
        self._coordinate_labels_by_object_id: dict[str, Any] = {}
        self._hovered_coordinate_object_id: str | None = None
        self._origin_transform_persistence = Graphic3d_TransformPers(
            Graphic3d_TMF_ZoomPers,
            gp_Pnt(0.0, 0.0, 0.0),
        )
        self.view_display_mode = ViewDisplayMode.SHADED_WITH_EDGES
        self.view_selection_mode = ViewSelectionMode.OBJECT
        self.view_selection_filter = ViewSelectionFilter.ALL
        self.active_application = ApplicationMode.MODELING
        self.view_selection_enabled = True
        self._model_ais_by_object_id: dict[str, list[Any]] = {}
        self._model_object_id_by_ais: list[tuple[Any, str]] = []
        self._model_edge_ais_by_object_id: dict[str, list[Any]] = {}
        self._sketch_ais_by_object_id: dict[str, list[Any]] = {}
        self._selectable_model_shapes: list[tuple[Any, str]] = []
        self._coordinate_shapes: list[tuple[Any, str]] = []
        self._coordinate_ais_shapes: list[Any] = []
        self._coordinate_ais_by_object_id: dict[str, list[Any]] = {}
        self._coordinate_overlay_sources_by_object_id: dict[
            str,
            list[tuple[Any, Any]],
        ] = {}
        self._coordinate_highlight_overlay_ais: list[Any] = []
        self._nonselectable_ais_shapes: list[Any] = []
        self._cached_document = None
        self._cached_model_shapes: list[tuple[Any, str]] = []
        self.selected_face = None
        self.selected_face_object_id: str | None = None
        self._selected_face_overlay_ais: list[Any] = []
        self._selected_model_overlay_ais: list[Any] = []
        self._hovered_model_overlay_ais: list[Any] = []
        self._view_selection_confirmed = False
        self._history_source_cycle_index = -1
        self._history_source_cycle_ids: tuple[str, ...] = ()
        self._history_source_cycle_active = False
        self._cycled_history_source_id: str | None = None
        self._reference_cycle_preview_id: str | None = None
        self._pending_attachment_plane_id: str | None = None

        self.viewer = ZimaViewer(self)

        self.view_toolbar = QToolBar(tr("toolbar.view"))
        self.view_toolbar.setMovable(False)
        self.view_toolbar.setStyleSheet(
            """
            QToolButton:hover:enabled {
                background-color: #245D8F;
                color: #FFFFFF;
                border: 1px solid #245D8F;
                border-radius: 4px;
            }
            QToolButton:checked {
                background-color: #245D8F;
                color: #FFFFFF;
                border: 1px solid #245D8F;
                border-radius: 4px;
            }
            QToolButton:pressed {
                background-color: #17436A;
                color: #FFFFFF;
                border: 1px solid #17436A;
                border-radius: 4px;
            }
            """
        )
        reset_view_action = self.view_toolbar.addAction(tr("toolbar.reset_view"))
        reset_view_action.triggered.connect(self.reset_view)
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
            ("selection.filter.object", ViewSelectionFilter.OBJECT),
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
        self.show_points_action = self._add_reference_visibility_action(
            tr("toolbar.show_points"), tr("toolbar.show_points.tooltip")
        )
        self.show_axes_action = self._add_reference_visibility_action(
            tr("toolbar.show_axes"), tr("toolbar.show_axes.tooltip")
        )
        self.show_planes_action = self._add_reference_visibility_action(
            tr("toolbar.show_planes"), tr("toolbar.show_planes.tooltip")
        )

        view_layout = QVBoxLayout()
        view_layout.setContentsMargins(0, 0, 0, 0)
        view_layout.setSpacing(0)
        view_layout.addWidget(self.view_toolbar)
        view_layout.addWidget(self.viewer, 1)

        self.view_panel = QWidget()
        self.view_panel.setLayout(view_layout)

        self.tools_toolbar = QToolBar(tr("menu.tools"))
        self.tools_toolbar.setMovable(False)
        self.tools_toolbar.setOrientation(Qt.Orientation.Vertical)
        self.tools_toolbar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
        self.tools_toolbar.setMinimumWidth(170)
        self.tools_toolbar.setStyleSheet(
            """
            QToolButton {
                padding: 6px 10px;
                text-align: left;
            }
            QToolButton:checked {
                background-color: #245D8F;
                color: #FFFFFF;
                border: 1px solid #245D8F;
                border-radius: 4px;
            }
            QToolButton#applicationCommandButton:hover:enabled {
                background-color: #245D8F;
                color: #FFFFFF;
            }
            QToolButton#applicationCommandButton:pressed:enabled {
                background-color: #17436A;
                color: #FFFFFF;
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
        self.viewer.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.viewer.customContextMenuRequested.connect(self._show_viewer_context_menu)
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
        self.viewer.selection_enabled = enabled
        self.selection_filter_combo.setEnabled(enabled)
        self.viewer._select_cycled_detection = False
        if hasattr(self, "_viewer_initialized"):
            context = self.viewer._display.Context
            if not enabled:
                context.ClearDetected(True)
                self._on_view_hover_changed(None)
            self.viewer._display.Repaint()
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

        heading = QLabel(tr(f"application.{self.active_application.value}"))
        heading.setAlignment(Qt.AlignmentFlag.AlignCenter)
        heading.setStyleSheet("font-weight: 600; padding: 6px;")
        self.tools_toolbar.addWidget(heading)

        if self.active_application == ApplicationMode.MODELING:
            new_object_action = self.tools_toolbar.addAction(
                tr("menu.context.create_object")
            )
            self._mark_application_command(new_object_action)
            new_object_action.triggered.connect(self.create_new_object)
            self.tools_toolbar.addSeparator()
            point_action = self.tools_toolbar.addAction(tr("primitive.point"))
            self._mark_application_command(point_action)
            point_action.triggered.connect(self._create_point_object)
            axis_action = self.tools_toolbar.addAction(tr("primitive.axis"))
            self._mark_application_command(axis_action)
            axis_action.triggered.connect(self._create_axis_object)
            self.tools_toolbar.addSeparator()
            for kind, text_key in (
                (ObjectKind.BOX, "primitive.box"),
                (ObjectKind.SPHERE, "primitive.sphere"),
                (ObjectKind.CYLINDER, "primitive.cylinder"),
                (ObjectKind.CONE, "primitive.cone"),
                (ObjectKind.PYRAMID, "primitive.pyramid"),
                (ObjectKind.WEDGE, "primitive.wedge"),
            ):
                primitive_action = self.tools_toolbar.addAction(tr(text_key))
                self._mark_application_command(primitive_action)
                primitive_action.triggered.connect(
                    lambda _checked=False, primitive_kind=kind:
                    self._create_primitive_object(primitive_kind)
                )
            self.tools_toolbar.addSeparator()
            sketch_action = self.tools_toolbar.addAction(
                tr("menu.context.create_sketch")
            )
            self._mark_application_command(sketch_action)
            sketch_action.triggered.connect(self._create_sketch_from_selection)
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

    def _create_sketch_from_selection(self) -> None:
        selected = self._selected_object()
        if selected is None:
            QMessageBox.information(
                self,
                tr("menu.context.create_sketch"),
                tr("message.select_sketch_parent"),
            )
            return
        if selected.kind == ObjectKind.PLANE:
            self.create_sketch_on_plane(selected.object_id)
            return
        if selected.kind == ObjectKind.OBJECT:
            self.create_sketch(selected.object_id)
            return
        QMessageBox.information(
            self,
            tr("menu.context.create_sketch"),
            tr("message.select_sketch_parent"),
        )

    def _create_primitive_object(self, kind: ObjectKind) -> None:
        if self.document is None:
            return
        text_key = {
            ObjectKind.BOX: "primitive.box",
            ObjectKind.SPHERE: "primitive.sphere",
            ObjectKind.CYLINDER: "primitive.cylinder",
            ObjectKind.CONE: "primitive.cone",
            ObjectKind.PYRAMID: "primitive.pyramid",
            ObjectKind.WEDGE: "primitive.wedge",
        }[kind]
        selected = self._selected_object()
        target = None
        if selected is not None and selected.kind in SOLID_KINDS:
            parent = self.document.find_parent(selected.object_id)
            if parent is not None and parent.kind == ObjectKind.OBJECT:
                target = parent
        created_object = None
        if target is None:
            created_object = self.document.create_object(tr(text_key))
            target = created_object
        primitive = self.document.create_primitive(target.object_id, kind)
        if primitive is None:
            if created_object is not None:
                self.document.delete_object(created_object.object_id)
            return
        self._populate_tree()
        self._select_tree_object(primitive.object_id)
        self.rebuild_view(fit=False)

    def _create_point_object(self) -> None:
        if self.document is None:
            return
        obj = self.document.create_object(tr("primitive.point"))
        point = self.document.create_point(obj.object_id)
        if point is None:
            self.document.delete_object(obj.object_id)
            return
        self._populate_tree()
        self._select_tree_object(point.object_id)
        self.rebuild_view(fit=False)

    def _create_axis_object(self) -> None:
        if self.document is None:
            return
        obj = self.document.create_object(tr("primitive.axis"))
        axis = self.document.create_datum_axis(obj.object_id)
        if axis is None:
            self.document.delete_object(obj.object_id)
            return
        self._populate_tree()
        self._select_tree_object(axis.object_id)
        self.rebuild_view(fit=False)
        self.show_axis_properties(axis)

    def _create_menu_bar(self) -> None:
        file_menu = self.menuBar().addMenu(tr("menu.file"))

        new_action = file_menu.addAction(tr("menu.file.new"))
        new_action.triggered.connect(self.new_document)

        open_action = file_menu.addAction(tr("menu.file.open"))
        open_action.triggered.connect(self.open_document)

        close_action = file_menu.addAction(tr("menu.file.close"))
        close_action.triggered.connect(self.close_document)

        save_action = file_menu.addAction(tr("menu.file.save"))
        save_action.triggered.connect(self.save_document)

        save_as_action = file_menu.addAction(tr("menu.file.save_as"))
        save_as_action.triggered.connect(self.save_document_as)

        file_menu.addSeparator()

        set_working_directory_action = file_menu.addAction(
            tr("menu.file.working_directory")
        )
        set_working_directory_action.triggered.connect(self.set_working_directory)

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
        global_settings_action.triggered.connect(self.show_options_dialog)

        self.window_menu = self.menuBar().addMenu(tr("menu.window"))
        self._refresh_window_menu()

        help_menu = self.menuBar().addMenu(tr("menu.help"))
        about_action = help_menu.addAction(tr("menu.help.about"))
        about_action.triggered.connect(self.show_about_dialog)

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
                return

            origins = [
                obj for obj in self.document.root.children
                if obj.kind == ObjectKind.ORIGIN
            ]
            for obj in origins:
                item = self._create_tree_item(obj)
                if item is not None:
                    self.tree.addTopLevelItem(item)

            history = self.document.history_objects()
            cursor = self.document.history_cursor()
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

            self.tree.expandAll()
            self.tree.resizeColumnToContents(0)
            self.tree.resizeColumnToContents(1)
            self.tree.resizeColumnToContents(2)
        finally:
            self.tree.blockSignals(signals_were_blocked)

    def _create_rollback_item(self) -> QTreeWidgetItem:
        item = QTreeWidgetItem([tr("tree.insert_here"), "", ""])
        item.setData(0, HistoryTreeWidget.ROLLBACK_ROLE, True)
        item.setFlags(Qt.ItemFlag.ItemIsEnabled)
        item.setFirstColumnSpanned(True)
        item.setTextAlignment(0, Qt.AlignmentFlag.AlignCenter)
        item.setToolTip(0, tr("tree.insert_here.tooltip"))
        font = item.font(0)
        font.setBold(True)
        item.setFont(0, font)
        item.setForeground(0, QBrush(QColor("#d18b00")))
        return item

    def _on_history_cursor_moved(self, cursor: int) -> None:
        if self.document is None or cursor == self.document.history_cursor():
            return
        self.document.set_history_cursor(cursor)
        self.selected_face = None
        self.selected_face_object_id = None
        selected_id = self.selected_object_id
        self._populate_tree()
        if selected_id is not None:
            self._select_tree_object(selected_id)
        self.rebuild_view(fit=False)

    def _on_history_object_moved(
        self,
        object_id: str,
        target_index: int,
    ) -> None:
        if self.document is None:
            return
        if not self.document.move_history_object(object_id, target_index):
            return
        self.selected_face = None
        self.selected_face_object_id = None
        self.selected_object_id = object_id
        self._history_source_cycle_index = -1
        self._populate_tree()
        self._select_tree_object(object_id)
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

    def _on_document_tab_changed(self, index: int) -> None:
        if index == self.active_document_index:
            return

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
            return

        new_index = min(index, len(self.document_sessions) - 1)
        self.active_document_index = -1
        self.document_tabs.setCurrentIndex(new_index)
        self._on_document_tab_changed(new_index)

    def new_document(self) -> None:
        dialog = NewDocumentDialog(self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return

        document_type = dialog.selected_document_type()
        file_stem = dialog.file_stem()
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
        file_path = self.working_directory / f"{file_stem}.prtz"
        self._add_document_session(document, file_path)

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
        except ObjectEntityLimitError as exc:
            QMessageBox.critical(
                self,
                tr("message.open_failed"),
                tr(
                    "message.object.entity_limit_details",
                    object=exc.object_name,
                    entities=", ".join(exc.entity_names),
                ),
            )
            return False
        except Exception as exc:
            QMessageBox.critical(self, tr("message.open_failed"), str(exc))
            return False

        self._add_document_session(document, canonical_path)
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
        except ObjectEntityLimitError as exc:
            QMessageBox.critical(
                self,
                tr("message.save_failed"),
                tr(
                    "message.object.entity_limit_details",
                    object=exc.object_name,
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
        return True

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
            f"{tr('app.title')} - {file_label} - WD: {self.working_directory}"
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

    def show_about_dialog(self) -> None:
        QMessageBox.information(
            self,
            tr("menu.help.about"),
            tr("dialog.about.text"),
        )

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

        display = self.viewer._display
        display.View_Iso()
        display.FitAll()
        display.Repaint()

    def _on_standard_view_changed(self, index: int) -> None:
        view_name = str(self.standard_view_combo.itemData(index) or "")
        if not view_name or not hasattr(self, "_viewer_initialized"):
            return
        display = self.viewer._display
        view_method = {
            "default": display.View_Iso,
            "front": display.View_Front,
            "back": display.View_Rear,
            "left": display.View_Left,
            "right": display.View_Right,
            "top": display.View_Top,
            "bottom": display.View_Bottom,
        }[view_name]
        view_method()
        display.Repaint()
        self.standard_view_combo.blockSignals(True)
        self.standard_view_combo.setCurrentIndex(0)
        self.standard_view_combo.blockSignals(False)

    def set_view_display_mode(self, display_mode: ViewDisplayMode) -> None:
        self.view_display_mode = display_mode
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _ensure_viewer_initialized(self) -> None:
        if hasattr(self, "_viewer_initialized"):
            self._sync_viewer_size()
            return

        if getattr(self, "_viewer_initializing", False):
            return

        if not self.viewer.isVisible():
            return

        self._viewer_initializing = True
        try:
            self.viewer.InitDriver()
            self._configure_view_highlight()
            self.viewer._display.register_select_callback(self._on_view_selection)
            self._viewer_initialized = True
        finally:
            self._viewer_initializing = False

        self._sync_viewer_size()

    def _configure_view_highlight(self) -> None:
        context = self.viewer._display.Context
        yellow = Quantity_Color(Quantity_NOC_YELLOW)
        for style_type in (
            Prs3d_TypeOfHighlight_Dynamic,
            Prs3d_TypeOfHighlight_LocalDynamic,
            Prs3d_TypeOfHighlight_Selected,
            Prs3d_TypeOfHighlight_LocalSelected,
        ):
            style = context.HighlightStyle(style_type)
            style.SetColor(yellow)
            style.SetZLayer(Graphic3d_ZLayerId_Topmost)
            if style.LineAspect() is not None:
                style.LineAspect().SetWidth(3.0)
            if style.WireAspect() is not None:
                style.WireAspect().SetWidth(3.0)

    def _sync_viewer_size(self) -> None:
        if not hasattr(self, "_viewer_initialized"):
            return

        self.viewer.updateGeometry()
        self.viewer.update()
        QApplication.processEvents()

        display = self.viewer._display
        if hasattr(display, "View") and hasattr(display.View, "MustBeResized"):
            display.View.MustBeResized()

    def _create_tree_item(self, obj: ZimaObject) -> QTreeWidgetItem | None:
        if obj.tree_exposure == TreeExposure.HIDDEN:
            return None
        if obj.kind == ObjectKind.POINT:
            name = self._point_display_name(obj)
        elif obj.kind == ObjectKind.BODY:
            suffix = obj.name.removeprefix("Body")
            name = (
                f"{tr('tree.body')}{suffix}"
                if suffix.isdigit()
                else obj.name
            )
        elif obj.kind == ObjectKind.SKETCH and obj.sketch_role() is not None:
            name = f"{obj.name} [{obj.sketch_role().value}]"
        else:
            name = obj.name
        operation_source = obj
        if obj.kind == ObjectKind.OBJECT:
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
        effectively_visible = (
            self.document.is_effectively_visible(obj.object_id)
            if self.document is not None
            else obj.user_visible
        )
        effectively_suppressed = (
            self.document.is_effectively_suppressed(obj.object_id)
            if self.document is not None
            else obj.suppressed
        )
        visibility_symbol = "◉" if effectively_visible else "○"
        state_symbol = "S" if effectively_suppressed else ""
        item = QTreeWidgetItem([name, visibility_symbol, state_symbol])
        item.setData(0, Qt.ItemDataRole.UserRole, obj.object_id)
        item.setTextAlignment(1, Qt.AlignmentFlag.AlignCenter)
        item.setTextAlignment(2, Qt.AlignmentFlag.AlignCenter)
        item.setToolTip(
            1,
            tr(
                "tree.visibility.visible"
                if effectively_visible
                else "tree.visibility.hidden"
            ),
        )
        if effectively_suppressed:
            font = item.font(0)
            font.setItalic(True)
            font.setStrikeOut(True)
            item.setFont(0, font)
            brush = QBrush(QColor("#808080"))
            for column in range(3):
                item.setForeground(column, brush)
            item.setToolTip(2, tr("tree.state.suppressed"))
        for child in obj.children:
            child_item = self._create_tree_item(child)
            if child_item is not None:
                item.addChild(child_item)
        return item

    def _point_display_name(self, point: ZimaObject) -> str:
        if self.document is None:
            return "Point"
        owner = self.document.find_owning_object(point.object_id)
        if owner is not None and owner.kind == ObjectKind.OBJECT:
            suffix = owner.name.removeprefix("Object")
            return f"Point{suffix}" if suffix.isdigit() else f"Point ({owner.name})"
        return "Point"

    def _on_selection_filter_changed(self) -> None:
        value = self.selection_filter_combo.currentData()
        self.view_selection_filter = ViewSelectionFilter(value)
        self.view_selection_mode = (
            ViewSelectionMode.FACE
            if self.view_selection_filter == ViewSelectionFilter.FACE
            else ViewSelectionMode.OBJECT
        )
        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _on_tree_selection_changed(self) -> None:
        self._history_source_cycle_active = False
        self._cycled_history_source_id = None
        self._reference_cycle_preview_id = None
        self.selected_face = None
        self.selected_face_object_id = None
        selected = self.tree.selectedItems()
        if not selected:
            self.selected_object_id = None
            self._view_selection_confirmed = False
        else:
            self.selected_object_id = selected[0].data(0, Qt.ItemDataRole.UserRole)
            self._view_selection_confirmed = self.selected_object_id is not None
        self.viewer._select_cycled_detection = False

        if hasattr(self, "_viewer_initialized"):
            self.rebuild_view(fit=False, rebuild_geometry=False)

    def _on_tree_item_clicked(
        self,
        item: QTreeWidgetItem,
        column: int,
    ) -> None:
        if column != 1:
            return
        obj = self._object_from_tree_item(item)
        if obj is None or obj.locked:
            return
        obj.user_visible = not obj.user_visible
        object_id = obj.object_id
        self._populate_tree()
        self._select_tree_object(object_id)
        self.rebuild_view(fit=False)

    def _on_tree_item_double_clicked(
        self,
        item: QTreeWidgetItem,
        _column: int,
    ) -> None:
        obj = self._object_from_tree_item(item)
        if (
            obj is not None
            and obj.kind != ObjectKind.OBJECT
            and obj.kind not in SOLID_KINDS
        ):
            self.show_properties(obj)

    def _confirm_preview_selection(self) -> bool:
        if self.document is None:
            return False
        preview_id = (
            self._reference_cycle_preview_id
            or self._cycled_history_source_id
        )
        preview = (
            self.document.find_object(preview_id)
            if preview_id is not None
            else None
        )
        if preview is None:
            return False
        self.selected_object_id = preview.object_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._view_selection_confirmed = True
        self._select_tree_object(preview.object_id)
        self._history_source_cycle_active = False
        self._cycled_history_source_id = None
        self._reference_cycle_preview_id = None
        self._hovered_coordinate_object_id = preview.object_id
        self._highlight_selected_in_view()
        self._update_coordinate_label_highlights()
        return True

    def _on_view_selection(self, shapes, _x: int, _y: int) -> None:
        if not self.view_selection_enabled:
            return
        if not shapes:
            self._clear_view_selection()
            return
        selected_shape = shapes[0]
        object_id = self._object_id_for_selected_shape(selected_shape)
        if object_id is None:
            return

        obj = self.document.find_object(object_id) if self.document is not None else None
        if (
            self._reference_cycle_preview_id is not None
            and self.document is not None
        ):
            reference = self.document.find_object(
                self._reference_cycle_preview_id
            )
            if reference is not None:
                object_id = reference.object_id
                obj = reference
        if (
            obj is not None
            and obj.kind == ObjectKind.PART
            and self._cycled_history_source_id is not None
            and self.document is not None
        ):
            cycled_source = self.document.find_object(
                self._cycled_history_source_id
            )
            if cycled_source is not None:
                object_id = cycled_source.object_id
                obj = cycled_source
        if obj is not None and obj.kind == ObjectKind.PART:
            self._history_source_cycle_index = -1
            self._history_source_cycle_active = False
        if (
            self._pending_attachment_plane_id is not None
            and obj is not None
            and obj.kind in (ObjectKind.BOX, ObjectKind.WEDGE)
            and selected_shape.ShapeType() == TopAbs_FACE
        ):
            face_role = self._solid_face_role(obj, selected_shape)
            if face_role is not None:
                source_plane_id = self._pending_attachment_plane_id
                self._pending_attachment_plane_id = None
                QTimer.singleShot(
                    0,
                    lambda: self._finish_plane_attachment(
                        source_plane_id,
                        obj.object_id,
                        face_role,
                    ),
                )
        if self.view_selection_mode == ViewSelectionMode.FACE:
            is_reference_entity = obj is not None and obj.kind in (
                ObjectKind.POINT,
                ObjectKind.AXIS,
                ObjectKind.PLANE,
                ObjectKind.SKETCH,
            )
            if selected_shape.ShapeType() != TopAbs_FACE and not is_reference_entity:
                return
            self.selected_face = None if is_reference_entity else selected_shape
            self.selected_face_object_id = object_id
            name = obj.name if obj is not None else object_id
            self.statusBar().showMessage(
                tr("selection.status.selected_face", name=name)
            )

        root = self.tree.invisibleRootItem()
        item = self._find_tree_item(root, object_id)
        self.tree.blockSignals(True)
        if item is not None:
            self.tree.setCurrentItem(item)
        else:
            self.tree.clearSelection()
            self.tree.setCurrentItem(None)
        self.tree.blockSignals(False)
        self.selected_object_id = object_id
        self._view_selection_confirmed = True
        if obj is not None and obj.kind in (
            ObjectKind.POINT,
            ObjectKind.AXIS,
            ObjectKind.PLANE,
            ObjectKind.SKETCH,
        ):
            self.rebuild_view(fit=False, rebuild_geometry=False)
            return
        self._highlight_selected_in_view()
        self._update_coordinate_label_highlights()

    def _on_view_hover_changed(self, shape, interactive=None) -> None:
        if not self.view_selection_enabled:
            shape = None
            interactive = None
        if shape is None and interactive is None:
            self._history_source_cycle_active = False
            self._cycled_history_source_id = None
            self._reference_cycle_preview_id = None
        object_id = None
        if (
            (shape is not None and not shape.IsNull())
            or interactive is not None
        ):
            candidate_id = self._object_id_for_detected(shape, interactive)
            candidate = (
                self.document.find_object(candidate_id)
                if self.document is not None and candidate_id is not None
                else None
            )
            if candidate is not None and candidate.kind in (
                ObjectKind.PART,
                ObjectKind.OBJECT,
                ObjectKind.BODY,
                ObjectKind.POINT,
                ObjectKind.PLANE,
                ObjectKind.AXIS,
                ObjectKind.SKETCH,
                *SOLID_KINDS,
            ):
                object_id = candidate_id
                if (
                    candidate.kind == ObjectKind.PART
                    and self._history_source_cycle_active
                ):
                    object_id = None
        if self._reference_cycle_preview_id is not None:
            object_id = self._reference_cycle_preview_id

        if object_id == self._hovered_coordinate_object_id:
            return
        self._hovered_coordinate_object_id = object_id
        self._update_coordinate_label_highlights()

    def _update_coordinate_label_highlights(self) -> None:
        if not hasattr(self, "_viewer_initialized"):
            return
        selected_ids = {self.selected_object_id} if self.selected_object_id else set()
        selected = self.document.find_object(self.selected_object_id) if (
            self.document is not None and self.selected_object_id is not None
        ) else None
        if selected is not None and selected.kind in (
            ObjectKind.OBJECT,
            ObjectKind.BODY,
            ObjectKind.ORIGIN,
        ):
            selected_ids.update(self._descendant_object_ids(selected))
        hovered_ids = (
            {self._hovered_coordinate_object_id}
            if self._hovered_coordinate_object_id is not None
            else set()
        )
        hovered = self.document.find_object(self._hovered_coordinate_object_id) if (
            self.document is not None
            and self._hovered_coordinate_object_id is not None
        ) else None
        if hovered is not None and hovered.kind in (
            ObjectKind.OBJECT,
            ObjectKind.BODY,
        ):
            hovered_ids.update(self._descendant_object_ids(hovered))
        whole_object_ids: set[str] = set()
        if selected is not None and selected.kind in (
            ObjectKind.OBJECT,
            ObjectKind.BODY,
        ):
            whole_object_ids.update(selected_ids)
        if hovered is not None and hovered.kind in (
            ObjectKind.OBJECT,
            ObjectKind.BODY,
        ):
            whole_object_ids.update(hovered_ids)

        context = self.viewer._display.Context
        for overlay_shape in self._hovered_model_overlay_ais:
            context.Erase(overlay_shape, False)
        self._hovered_model_overlay_ais.clear()
        for overlay_shape in self._coordinate_highlight_overlay_ais:
            context.Erase(overlay_shape, False)
        self._coordinate_highlight_overlay_ais.clear()
        for shape, owner_id in self._selectable_model_shapes:
            if owner_id not in hovered_ids:
                continue
            owner = self.document.find_object(owner_id) if self.document else None
            if owner is None or owner.kind not in SOLID_KINDS:
                continue
            overlay_shapes = self.viewer._display.DisplayShape(
                shape,
                color=YELLOW,
                update=False,
            )
            for overlay_shape in overlay_shapes:
                self._set_ais_display_mode(overlay_shape, AIS_WireFrame)
                overlay_shape.SetWidth(3.0)
                overlay_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
                context.Deactivate(overlay_shape)
            self._hovered_model_overlay_ais.extend(overlay_shapes)
        active_coordinate_ids: set[str] = set()
        for object_id, edge_shapes in self._model_edge_ais_by_object_id.items():
            selected_edge_ids = set() if self.selected_face is not None else selected_ids
            highlighted = (
                object_id in selected_edge_ids
                or object_id in hovered_ids
                or object_id in whole_object_ids
            )
            color = YELLOW if highlighted else BLACK
            for edge_shape in edge_shapes:
                edge_shape.SetColor(color)
                edge_shape.SetWidth(3.0 if highlighted else 1.0)
                context.Redisplay(edge_shape, False)
        for object_id, coordinate_shapes in self._coordinate_ais_by_object_id.items():
            coordinate = self.document.find_object(object_id) if self.document else None
            if coordinate is None:
                continue
            coordinate_owner = (
                self.document.find_owning_object(object_id)
                if self.document is not None
                else None
            )
            owner_id = (
                coordinate_owner.object_id
                if coordinate_owner is not None
                else None
            )
            active = (
                object_id in selected_ids
                or object_id in hovered_ids
                or owner_id in whole_object_ids
            )
            if active:
                color = YELLOW
            elif coordinate.kind == ObjectKind.PLANE:
                color = PLANE_COLOR
            elif coordinate.kind == ObjectKind.POINT:
                color = BLACK
            elif coordinate.kind == ObjectKind.AXIS:
                if coordinate.parameters.get("display_style") == "centerline":
                    color = CENTERLINE_COLOR
                else:
                    color = {
                        "x": RED,
                        "y": GREEN,
                        "z": BLUE,
                    }.get(str(coordinate.parameters.get("axis")), BLACK)
            else:
                continue
            for coordinate_shape in coordinate_shapes:
                coordinate_shape.SetColor(color)
                if coordinate.kind == ObjectKind.PLANE:
                    coordinate_shape.SetWidth(3.0 if active else 1.0)
                elif coordinate.kind == ObjectKind.AXIS:
                    coordinate_shape.SetWidth(
                        3.0
                        if active
                        else (
                            2.0
                            if coordinate.parameters.get("display_style")
                            == "centerline"
                            else 1.0
                        )
                    )
                context.Redisplay(coordinate_shape, False)
            if active:
                active_coordinate_ids.add(object_id)
        for object_id in active_coordinate_ids:
            for shape, transform_persistence in (
                self._coordinate_overlay_sources_by_object_id.get(object_id, [])
            ):
                overlay_shapes = self.viewer._display.DisplayShape(
                    shape,
                    color=YELLOW,
                    update=False,
                )
                for overlay_shape in overlay_shapes:
                    overlay_shape.SetZLayer(Graphic3d_ZLayerId_TopOSD)
                    overlay_shape.SetTransformPersistence(transform_persistence)
                    context.Deactivate(overlay_shape)
                self._coordinate_highlight_overlay_ais.extend(overlay_shapes)
        active_coordinate_label_structures = []
        for object_id, structures in self._coordinate_labels_by_object_id.items():
            normal_structure, highlighted_structure = structures
            coordinate_owner = (
                self.document.find_owning_object(object_id)
                if self.document is not None
                else None
            )
            owner_id = (
                coordinate_owner.object_id
                if coordinate_owner is not None
                else None
            )
            active = (
                object_id in selected_ids
                or object_id in hovered_ids
                or owner_id in whole_object_ids
            )
            normal_structure.Erase()
            highlighted_structure.Erase()
            if active:
                active_coordinate_label_structures.append(highlighted_structure)
            else:
                normal_structure.Display()
        # Coincident coordinate systems also have coincident labels. Display the
        # active label after every normal label so the selected plane keeps its
        # yellow text regardless of object creation order.
        for highlighted_structure in active_coordinate_label_structures:
            highlighted_structure.Display()
        self.viewer._display.Repaint()

    def _solid_face_role(self, solid: ZimaObject, face) -> str | None:
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

    def _object_id_for_selected_shape(self, selected_shape) -> str | None:
        for model_shape, object_id in [
            *self._coordinate_shapes,
            *self._selectable_model_shapes,
        ]:
            if selected_shape.IsSame(model_shape):
                return self._selection_filtered_object_id(object_id)
            explorer = TopExp_Explorer(model_shape, selected_shape.ShapeType())
            while explorer.More():
                if selected_shape.IsSame(explorer.Current()):
                    return self._selection_filtered_object_id(object_id)
                explorer.Next()
        return None

    def _object_id_for_detected(self, shape, interactive) -> str | None:
        if shape is not None and not shape.IsNull():
            object_id = self._object_id_for_selected_shape(shape)
            if object_id is not None:
                return object_id
        if interactive is None:
            return None
        for ais_shape, object_id in self._model_object_id_by_ais:
            try:
                matches = ais_shape == interactive or ais_shape.IsSame(interactive)
            except (AttributeError, TypeError):
                matches = ais_shape == interactive
            if matches:
                return self._selection_filtered_object_id(object_id)
        return None

    def _selection_filtered_object_id(self, object_id: str) -> str:
        if (
            self.view_selection_filter != ViewSelectionFilter.OBJECT
            or self.document is None
        ):
            return object_id
        obj = self.document.find_object(object_id)
        while obj is not None and obj.kind != ObjectKind.OBJECT:
            obj = self.document.find_parent(obj.object_id)
        return obj.object_id if obj is not None else object_id

    def _show_tree_context_menu(self, position: QPoint) -> None:
        if self.document is None:
            return

        item = self.tree.itemAt(position)
        if item is not None:
            self.tree.setCurrentItem(item)

        obj = self._object_from_tree_item(item)
        menu = QMenu(self)
        attach_action = None
        create_action = None
        create_axis_action = None
        create_sketch_action = None
        create_sketch_actions: dict[Any, SketchRole] = {}
        properties_action = None
        delete_action = None
        normal_view_action = None
        visibility_action = None
        suppress_action = None
        add_action = None
        subtract_action = None

        if obj is None or obj.kind == ObjectKind.PART:
            create_action = menu.addAction(tr("menu.context.create_object"))
        elif self._is_system_reference_plane(obj):
            normal_view_action = menu.addAction(tr("menu.context.view_normal"))
            if self._is_object_reference_plane(obj):
                menu.addSeparator()
                attach_action = menu.addAction(tr("menu.context.attach_to_face"))
                create_sketch_actions = self._add_sketch_role_menu(menu, obj)
        else:
            if obj.kind == ObjectKind.OBJECT:
                create_axis_action = menu.addAction(
                    tr("menu.context.create_axis")
                )
                create_axis_action.setEnabled(
                    obj.can_accept_entity(ObjectKind.AXIS)
                )
                create_sketch_action = menu.addAction(
                    tr("menu.context.create_sketch")
                )
                create_sketch_action.setEnabled(
                    obj.can_accept_entity(ObjectKind.SKETCH, SketchRole.PROFILE)
                )
                if not obj.locked:
                    properties_action = menu.addAction(
                        tr("menu.context.edit")
                    )
                    delete_action = menu.addAction(
                        tr("menu.context.delete_object")
                    )
            elif not obj.locked:
                if obj.kind in SOLID_KINDS:
                    properties_action = menu.addAction(tr("menu.context.edit"))
                elif obj.kind == ObjectKind.AXIS:
                    properties_action = menu.addAction(tr("menu.context.properties"))
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
                menu.addSeparator()
                visibility_action = menu.addAction(
                    tr(
                        "menu.context.hide"
                        if obj.user_visible
                        else "menu.context.show"
                    )
                )
                if obj.kind == ObjectKind.OBJECT or obj.kind == ObjectKind.BODY or obj.kind in SOLID_KINDS:
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
            self._begin_plane_attachment(obj.object_id)
        elif normal_view_action is not None and action == normal_view_action and obj is not None:
            self._view_normal_to_reference_plane(obj)
        elif create_action is not None and action == create_action:
            self.create_new_object()
        elif (
            create_axis_action is not None
            and action == create_axis_action
            and obj is not None
        ):
            self.create_datum_axis(obj.object_id)
        elif (
            create_sketch_action is not None
            and action == create_sketch_action
            and obj is not None
        ):
            self.create_sketch(obj.object_id)
        elif (
            action in create_sketch_actions
            and obj is not None
        ):
            role = create_sketch_actions[action]
            if obj.kind == ObjectKind.OBJECT:
                self.create_sketch(obj.object_id, role)
            else:
                self.create_sketch_on_plane(obj.object_id, role)
        elif (
            properties_action is not None
            and action == properties_action
            and obj is not None
        ):
            self.show_properties(obj)
        elif delete_action is not None and action == delete_action and obj is not None:
            self.delete_object(obj.object_id)
        elif (
            visibility_action is not None
            and action == visibility_action
            and obj is not None
        ):
            self._set_object_visibility(obj, not obj.user_visible)
        elif (
            suppress_action is not None
            and action == suppress_action
            and obj is not None
        ):
            self._set_object_suppressed(obj, not obj.suppressed)
        elif obj is not None and action in (add_action, subtract_action):
            target = self._operation_target(obj)
            if target is not None:
                target.combine_mode = (
                    CombineMode.ADD if action == add_action else CombineMode.SUBTRACT
                )
                self._populate_tree()
                self._select_tree_object(obj.object_id)
                self.rebuild_view(fit=False)

    def _show_viewer_context_menu(self, position: QPoint) -> None:
        if self.document is None or not self.view_selection_enabled:
            return

        x, y = self.viewer.occ_position(position)
        self.viewer._display.MoveTo(x, y)
        context = self.viewer._display.Context
        if (
            self.view_selection_mode == ViewSelectionMode.OBJECT
            and context.HasDetected()
        ):
            detected_shape = (
                context.DetectedShape()
                if context.HasDetectedShape()
                else None
            )
            candidate_id = self._object_id_for_detected(
                detected_shape,
                context.DetectedInteractive(),
            )
            candidate = self.document.find_object(candidate_id) if (
                candidate_id is not None
            ) else None
            if candidate is not None and candidate.kind == ObjectKind.PART:
                selected = self._selected_object()
                if (
                    self._view_selection_confirmed
                    and not self._history_source_cycle_active
                    and selected is not None
                    and selected.kind in (ObjectKind.OBJECT, *SOLID_KINDS)
                ):
                    self._show_selected_view_context_menu(
                        selected,
                        self.viewer.mapToGlobal(position),
                    )
                    return
                self._cycle_history_source(
                    self._history_sources_under_cursor(x, y)
                )
                return

        if self._view_selection_confirmed:
            obj = self._selected_object()
            if obj is not None:
                self._show_selected_view_context_menu(
                    obj,
                    self.viewer.mapToGlobal(position),
                )
                return

        if not self.viewer._display.Context.HasDetected():
            self.viewer._display.MoveTo(x, y)
        if context.HasDetected():
            previous_object_id = self._hovered_coordinate_object_id
            rank = 1
            detected_shape = None
            for _attempt in range(64):
                if context.HasNextDetected():
                    rank = context.HilightNextDetected(
                        self.viewer._display.View,
                        True,
                    )
                else:
                    context.ClearDetected(False)
                    self.viewer._display.MoveTo(x, y)
                    rank = 1
                detected_shape = (
                    context.DetectedShape()
                    if context.HasDetectedShape()
                    else None
                )
                candidate_id = (
                    self._object_id_for_detected(
                        detected_shape,
                        context.DetectedInteractive()
                        if context.HasDetected()
                        else None,
                    )
                )
                if candidate_id is not None and candidate_id != previous_object_id:
                    break
            candidate = (
                self.document.find_object(candidate_id)
                if candidate_id is not None
                else None
            )
            if candidate is not None and candidate.kind in (
                ObjectKind.ORIGIN,
                ObjectKind.POINT,
                ObjectKind.AXIS,
                ObjectKind.PLANE,
                ObjectKind.SKETCH,
            ):
                self._reference_cycle_preview_id = candidate.object_id
                self._history_source_cycle_active = False
                self._cycled_history_source_id = None
            self._on_view_hover_changed(
                detected_shape,
                context.DetectedInteractive()
                if context.HasDetected()
                else None,
            )
            self.viewer._select_cycled_detection = True
            self.statusBar().showMessage(
                tr(
                    "selection.status.cycled_face"
                    if self.view_selection_mode == ViewSelectionMode.FACE
                    else "selection.status.cycled_object",
                    rank=rank,
                )
            )
            return

        menu = QMenu(self)
        create_action = menu.addAction(tr("menu.context.create_object"))
        action = menu.exec(self.viewer.mapToGlobal(position))

        if action == create_action:
            self.create_new_object()

    def _show_selected_view_context_menu(self, obj: ZimaObject, global_position) -> None:
        menu = QMenu(self)
        if (
            self.selected_face is not None
            and self.view_selection_filter != ViewSelectionFilter.OBJECT
        ):
            empty_action = menu.addAction(" ")
            empty_action.setEnabled(False)
            menu.exec(global_position)
            return

        attach_action = None
        create_axis_action = None
        create_sketch_action = None
        create_sketch_actions: dict[Any, SketchRole] = {}
        properties_action = None
        delete_action = None
        normal_view_action = None
        visibility_action = None
        suppress_action = None

        if obj.kind == ObjectKind.PART:
            pass
        elif self._is_system_reference_plane(obj):
            normal_view_action = menu.addAction(tr("menu.context.view_normal"))
            if self._is_object_reference_plane(obj):
                menu.addSeparator()
                attach_action = menu.addAction(tr("menu.context.attach_to_face"))
                create_sketch_actions = self._add_sketch_role_menu(menu, obj)
        else:
            if obj.kind == ObjectKind.OBJECT:
                create_axis_action = menu.addAction(
                    tr("menu.context.create_axis")
                )
                create_axis_action.setEnabled(
                    obj.can_accept_entity(ObjectKind.AXIS)
                )
                create_sketch_action = menu.addAction(
                    tr("menu.context.create_sketch")
                )
                create_sketch_action.setEnabled(
                    obj.can_accept_entity(ObjectKind.SKETCH, SketchRole.PROFILE)
                )
                if not obj.locked:
                    properties_action = menu.addAction(
                        tr("menu.context.edit")
                    )
                    delete_action = menu.addAction(
                        tr("menu.context.delete_object")
                    )
            elif not obj.locked:
                if obj.kind in SOLID_KINDS:
                    properties_action = menu.addAction(tr("menu.context.edit"))
                elif obj.kind == ObjectKind.AXIS:
                    properties_action = menu.addAction(tr("menu.context.properties"))
                delete_action = menu.addAction(tr("menu.context.delete_entity"))
            if not obj.locked:
                menu.addSeparator()
                visibility_action = menu.addAction(
                    tr(
                        "menu.context.hide"
                        if obj.user_visible
                        else "menu.context.show"
                    )
                )
                if obj.kind == ObjectKind.OBJECT or obj.kind == ObjectKind.BODY or obj.kind in SOLID_KINDS:
                    suppress_action = menu.addAction(
                        tr(
                            "menu.context.resume"
                            if obj.suppressed
                            else "menu.context.suppress"
                        )
                    )

        if self.selected_face is not None and normal_view_action is None:
            normal_view_action = menu.addAction(tr("menu.context.view_normal"))

        if menu.isEmpty():
            return
        action = menu.exec(global_position)
        if attach_action is not None and action == attach_action:
            self._begin_plane_attachment(obj.object_id)
        elif normal_view_action is not None and action == normal_view_action:
            if obj.kind == ObjectKind.PLANE:
                self._view_normal_to_reference_plane(obj)
            else:
                self._view_normal_to_selected_face()
        elif create_axis_action is not None and action == create_axis_action:
            self.create_datum_axis(obj.object_id)
        elif create_sketch_action is not None and action == create_sketch_action:
            self.create_sketch(obj.object_id)
        elif action in create_sketch_actions:
            role = create_sketch_actions[action]
            if obj.kind == ObjectKind.OBJECT:
                self.create_sketch(obj.object_id, role)
            else:
                self.create_sketch_on_plane(obj.object_id, role)
        elif properties_action is not None and action == properties_action:
            self.show_properties(obj)
        elif delete_action is not None and action == delete_action:
            self.delete_object(obj.object_id)
        elif visibility_action is not None and action == visibility_action:
            self._set_object_visibility(obj, not obj.user_visible)
        elif suppress_action is not None and action == suppress_action:
            self._set_object_suppressed(obj, not obj.suppressed)

    def _history_sources_under_cursor(
        self,
        x: int,
        y: int,
    ) -> list[ZimaObject]:
        if self.document is None:
            return []
        px, py, pz, dx, dy, dz = (
            self.viewer._display.View.ConvertWithProj(x, y)
        )
        try:
            line = gp_Lin(gp_Pnt(px, py, pz), gp_Dir(dx, dy, dz))
        except RuntimeError:
            return []
        hits: list[tuple[float, ZimaObject]] = []
        for source in self.document.active_history_objects():
            shape = self.document.build_standalone_shape(source)
            if shape is None:
                continue
            intersector = IntCurvesFace_ShapeIntersector()
            intersector.Load(shape, 1e-7)
            intersector.Perform(line, 0.0, 1e100)
            if intersector.NbPnt() > 0:
                hits.append((intersector.WParameter(1), source))
        hits.sort(key=lambda item: item[0])
        return [source for _distance, source in hits]

    def _cycle_history_source(self, sources: list[ZimaObject]) -> None:
        if self.document is None:
            return
        if not sources:
            return
        source_ids = tuple(source.object_id for source in sources)
        if source_ids != self._history_source_cycle_ids:
            self._history_source_cycle_ids = source_ids
            self._history_source_cycle_index = 0
        else:
            self._history_source_cycle_index = (
                self._history_source_cycle_index + 1
            ) % len(sources)
        source = sources[self._history_source_cycle_index]
        self.selected_object_id = source.object_id
        self.selected_face = None
        self.selected_face_object_id = None
        self._view_selection_confirmed = False
        self._history_source_cycle_active = True
        self._cycled_history_source_id = source.object_id
        self._reference_cycle_preview_id = None
        self._hovered_coordinate_object_id = None
        self.viewer._display.Context.ClearDetected(False)
        self._highlight_selected_in_view()
        self._update_coordinate_label_highlights()
        self.statusBar().showMessage(
            tr(
                "selection.status.cycled_source",
                rank=self._history_source_cycle_index + 1,
                count=len(sources),
                name=source.name,
            )
        )

    def _view_normal_to_reference_plane(self, plane: ZimaObject) -> None:
        if self.document is None or plane.kind != ObjectKind.PLANE:
            return
        owner = self.document.find_owning_object(plane.object_id)
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

    def _world_transform_for_object(self, obj: ZimaObject | None):
        chain: list[ZimaObject] = []
        while obj is not None and obj.kind not in (ObjectKind.PART, ObjectKind.ORIGIN):
            chain.append(obj)
            obj = self.document.find_parent(obj.object_id) if self.document else None
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
        up = (0.0, 1.0, 0.0) if abs(nz) > 0.9 else (0.0, 0.0, 1.0)
        view = self.viewer._display.View
        view.SetProj(nx, ny, nz)
        view.SetUp(*up)
        self.viewer._display.FitAll()
        self._clear_view_selection()
        self.viewer._display.Repaint()

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
        context = self.viewer._display.Context
        context.ClearSelected(False)
        for ais_shape in self._selected_face_overlay_ais:
            context.Erase(ais_shape, False)
        self._selected_face_overlay_ais.clear()
        context.UpdateSelected(True)
        self.rebuild_view(fit=False, rebuild_geometry=False)

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
        source_plane = self.document.find_object(source_plane_id)
        source_object = self.document.find_owning_object(source_plane_id)
        target = self.document.find_object(target_object_id)
        if (
            source_plane is None
            or source_object is None
            or target is None
            or source_plane.kind != ObjectKind.PLANE
            or source_object.kind != ObjectKind.OBJECT
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
            target_object_id=target.object_id,
            target_face_role=face_role,
            primary_axis=dialog.primary_axis,
            secondary_axis=dialog.secondary_axis,
            switch_angle=45.0,
            flip_normal=dialog.flip_checkbox.isChecked(),
        )
        self.document.resolve_attachments()
        self.selected_object_id = source_object.object_id
        self._populate_tree()
        self._select_tree_object(source_object.object_id)
        self.rebuild_view(fit=False)

    def create_new_object(self) -> None:
        if self.document is None:
            return

        obj = self.document.create_object()
        self._populate_tree()
        self._select_tree_object(obj.object_id)
        self.rebuild_view(fit=False)

    def _operation_target(self, obj: ZimaObject) -> ZimaObject | None:
        if self.document is None:
            return None
        if obj.kind in SOLID_KINDS:
            return obj
        if obj.kind != ObjectKind.OBJECT:
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

        sketch = self.document.create_sketch_on_plane(plane_id, role)
        if sketch is None:
            self._show_entity_limit_message(plane_id, ObjectKind.SKETCH)
            return

        self._populate_tree()
        self._select_tree_object(sketch.object_id)
        self.rebuild_view(fit=False)

    def create_sketch(
        self,
        parent_id: str,
        role: SketchRole = SketchRole.PROFILE,
    ) -> None:
        if self.document is None:
            return
        sketch = self.document.create_sketch(parent_id, role=role)
        if sketch is None:
            self._show_entity_limit_message(parent_id, ObjectKind.SKETCH)
            return
        self._populate_tree()
        self._select_tree_object(sketch.object_id)
        self.rebuild_view(fit=False)

    def create_cube(self, source_id: str) -> None:
        if self.document is None:
            return

        cube = self.document.create_cube(source_id)
        if cube is None:
            self._show_entity_limit_message(source_id, ObjectKind.BOX)
            return

        self._populate_tree()
        self._select_tree_object(cube.object_id)
        self.rebuild_view(fit=False)

    def create_wedge(self, source_id: str) -> None:
        if self.document is None:
            return

        wedge = self.document.create_wedge(source_id)
        if wedge is None:
            self._show_entity_limit_message(source_id, ObjectKind.WEDGE)
            return

        self._populate_tree()
        self._select_tree_object(wedge.object_id)
        self.rebuild_view(fit=False)

    def create_datum_axis(self, parent_id: str) -> None:
        if self.document is None:
            return
        axis = self.document.create_datum_axis(parent_id)
        if axis is None:
            self._show_entity_limit_message(parent_id, ObjectKind.AXIS)
            return
        self._populate_tree()
        self._select_tree_object(axis.object_id)
        self.rebuild_view(fit=False)
        self.show_axis_properties(axis)

    def delete_object(self, object_id: str) -> None:
        if self.document is None:
            return

        if self.document.delete_object(object_id):
            self.selected_object_id = None
            self._populate_tree()
            self.rebuild_view(fit=False)

    def _set_object_visibility(self, obj: ZimaObject, visible: bool) -> None:
        obj.user_visible = visible
        self._populate_tree()
        self._select_tree_object(obj.object_id)
        self.rebuild_view(fit=False)

    def _set_object_suppressed(self, obj: ZimaObject, suppressed: bool) -> None:
        if (
            obj.kind != ObjectKind.OBJECT
            and obj.kind != ObjectKind.BODY
            and obj.kind not in SOLID_KINDS
        ):
            return
        obj.suppressed = suppressed
        self.selected_face = None
        self.selected_face_object_id = None
        self._populate_tree()
        self._select_tree_object(obj.object_id)
        self.rebuild_view(fit=False)

    def show_object_properties(self, obj: ZimaObject) -> None:
        if obj.kind != ObjectKind.OBJECT:
            return

        dialog = ObjectPropertiesDialog(obj, self.document, self)
        dialog.applied.connect(lambda: self._refresh_object_properties(obj))
        if dialog.exec() == QDialog.DialogCode.Accepted and dialog.apply_to_object():
            self._refresh_object_properties(obj)

    def show_axis_properties(self, axis: ZimaObject) -> None:
        if axis.kind != ObjectKind.AXIS or axis.locked:
            return
        dialog = DatumAxisPropertiesDialog(axis, self)
        dialog.applied.connect(lambda: self._refresh_object_properties(axis))
        if dialog.exec() == QDialog.DialogCode.Accepted and dialog.apply_to_axis():
            self._refresh_object_properties(axis)

    def show_primitive_properties(self, primitive: ZimaObject) -> None:
        if primitive.kind not in SOLID_KINDS:
            return
        dialog = PrimitivePropertiesDialog(primitive, self)
        dialog.applied.connect(
            lambda: self._refresh_object_properties(primitive)
        )
        if (
            dialog.exec() == QDialog.DialogCode.Accepted
            and dialog.apply_to_primitive()
        ):
            self._refresh_object_properties(primitive)

    def show_properties(self, obj: ZimaObject) -> None:
        if obj.kind == ObjectKind.OBJECT:
            self.show_object_properties(obj)
        elif obj.kind in SOLID_KINDS:
            self.show_primitive_properties(obj)
        elif obj.kind == ObjectKind.AXIS and not obj.locked:
            self.show_axis_properties(obj)

    def _refresh_object_properties(self, obj: ZimaObject) -> None:
        self._populate_tree()
        self._select_tree_object(obj.object_id)
        self.rebuild_view(fit=False)

    def _add_sketch_role_menu(
        self,
        menu: QMenu,
        source: ZimaObject,
    ) -> dict[Any, SketchRole]:
        if self.document is None:
            return {}
        owner = (
            source
            if source.kind == ObjectKind.OBJECT
            else self.document.find_owning_object(source.object_id)
        )
        if owner is None or owner.kind != ObjectKind.OBJECT:
            return {}
        sketch_menu = menu.addMenu(tr("menu.context.create_sketch"))
        actions: dict[Any, SketchRole] = {}
        any_enabled = False
        for role in SketchRole:
            action = sketch_menu.addAction(tr(f"sketch.role.{role.value.lower()}"))
            enabled = owner.can_accept_entity(ObjectKind.SKETCH, role)
            action.setEnabled(enabled)
            any_enabled = any_enabled or enabled
            actions[action] = role
        sketch_menu.setEnabled(any_enabled)
        return actions

    def _is_object_reference_plane(self, obj: ZimaObject) -> bool:
        if self.document is None or obj.kind != ObjectKind.PLANE:
            return False
        parent = self.document.find_owning_object(obj.object_id)
        return parent is not None and parent.kind == ObjectKind.OBJECT

    def _is_system_reference_plane(self, obj: ZimaObject) -> bool:
        if self.document is None or obj.kind != ObjectKind.PLANE:
            return False
        parent = self.document.find_parent(obj.object_id)
        return parent is not None and parent.kind == ObjectKind.ORIGIN

    def _can_create_cube_from(self, obj: ZimaObject) -> bool:
        if self.document is None:
            return False
        if obj.kind == ObjectKind.OBJECT:
            return obj.can_accept_entity(ObjectKind.BOX)
        if obj.kind != ObjectKind.POINT:
            return False
        parent = self.document.find_owning_object(obj.object_id)
        return parent is not None and parent.can_accept_entity(ObjectKind.BOX)

    def _show_entity_limit_message(
        self,
        source_id: str,
        requested_kind: ObjectKind,
    ) -> None:
        if self.document is None:
            return
        source = self.document.find_object(source_id)
        if source is None:
            return
        owner = (
            source
            if source.kind == ObjectKind.OBJECT
            else self.document.find_owning_object(source_id)
        )
        if owner is not None and not owner.can_accept_entity(requested_kind):
            QMessageBox.information(
                self,
                tr("message.object.entity_limit_title"),
                tr("message.object.entity_limit"),
            )

    def _object_from_tree_item(self, item: QTreeWidgetItem | None) -> ZimaObject | None:
        if item is None:
            return None
        object_id = item.data(0, Qt.ItemDataRole.UserRole)
        if object_id is None:
            return None
        if self.document is None:
            return None
        return self.document.find_object(object_id)

    def _select_tree_object(self, object_id: str) -> None:
        root = self.tree.invisibleRootItem()
        item = self._find_tree_item(root, object_id)
        if item is not None:
            self.tree.setCurrentItem(item)

    def _find_tree_item(self, parent: QTreeWidgetItem, object_id: str):
        for index in range(parent.childCount()):
            child = parent.child(index)
            if child.data(0, Qt.ItemDataRole.UserRole) == object_id:
                return child
            found = self._find_tree_item(child, object_id)
            if found is not None:
                return found
        return None

    def _selected_object(self) -> ZimaObject | None:
        if self.document is None or self.selected_object_id is None:
            return None
        return self.document.find_object(self.selected_object_id)

    def _editable_selected_object(self) -> ZimaObject | None:
        obj = self._selected_object()
        if obj is None or obj.kind != ObjectKind.OBJECT:
            return None
        return obj

    def rebuild_view(self, fit: bool = True, rebuild_geometry: bool = True) -> None:
        if self.document is not None and not hasattr(self, "_viewer_initialized"):
            self._ensure_viewer_initialized()

        if not hasattr(self, "_viewer_initialized"):
            return

        self._sync_viewer_size()
        display = self.viewer._display
        self._clear_plane_labels()
        display.EraseAll()
        self._model_ais_by_object_id.clear()
        self._model_object_id_by_ais.clear()
        self._model_edge_ais_by_object_id.clear()
        self._sketch_ais_by_object_id.clear()
        self._selectable_model_shapes.clear()
        self._coordinate_shapes.clear()
        self._coordinate_ais_shapes.clear()
        self._coordinate_ais_by_object_id.clear()
        self._coordinate_overlay_sources_by_object_id.clear()
        self._coordinate_highlight_overlay_ais.clear()
        self._nonselectable_ais_shapes.clear()
        self._selected_face_overlay_ais.clear()
        self._selected_model_overlay_ais.clear()
        self._hovered_model_overlay_ais.clear()

        if self.document is not None:
            if rebuild_geometry or self._cached_document is not self.document:
                self.document.resolve_attachments()
                self._cached_document = self.document
                self._cached_model_shapes = []
                shape = self.document.build_active_shape()
                active_objects = self.document.active_history_objects()
                if shape is not None and active_objects:
                    self._cached_model_shapes.append(
                        (shape, self.document.root.object_id)
                    )
            for shape, owner_id in self._cached_model_shapes:
                self._display_model_shape(shape, owner_id)

            self._display_origin()
            self._display_sketches()

        if fit:
            display.FitAll()
        if self.view_selection_mode == ViewSelectionMode.FACE:
            display.SetSelectionModeFace()
        else:
            display.SetSelectionModeShape()
            for ais_shapes in self._model_ais_by_object_id.values():
                for ais_shape in ais_shapes:
                    display.Context.Activate(ais_shape, 0, True)
        coordinate_kind_for_filter = {
            ViewSelectionFilter.POINT: ObjectKind.POINT,
            ViewSelectionFilter.AXIS: ObjectKind.AXIS,
            ViewSelectionFilter.PLANE: ObjectKind.PLANE,
        }.get(self.view_selection_filter)
        coordinates_enabled = self.view_selection_filter != ViewSelectionFilter.FACE
        for object_id, ais_shapes in self._coordinate_ais_by_object_id.items():
            obj = self.document.find_object(object_id) if self.document is not None else None
            enabled = coordinates_enabled and (
                coordinate_kind_for_filter is None
                or (obj is not None and obj.kind == coordinate_kind_for_filter)
            )
            for ais_shape in ais_shapes:
                if enabled:
                    display.Context.Activate(ais_shape, 0, True)
                else:
                    display.Context.Deactivate(ais_shape)
        if self.view_selection_filter in (
            ViewSelectionFilter.POINT,
            ViewSelectionFilter.AXIS,
            ViewSelectionFilter.PLANE,
        ):
            for ais_shapes in self._model_ais_by_object_id.values():
                for ais_shape in ais_shapes:
                    display.Context.Deactivate(ais_shape)
        for ais_shape in self._nonselectable_ais_shapes:
            display.Context.Deactivate(ais_shape)
        sketch_selection_enabled = self.view_selection_filter not in (
            ViewSelectionFilter.POINT,
            ViewSelectionFilter.AXIS,
            ViewSelectionFilter.PLANE,
        )
        for ais_shapes in self._sketch_ais_by_object_id.values():
            for ais_shape in ais_shapes:
                if sketch_selection_enabled:
                    display.Context.Activate(ais_shape, 0, True)
                else:
                    display.Context.Deactivate(ais_shape)
        self._highlight_selected_in_view()
        self._update_coordinate_label_highlights()
        display.Repaint()

    def _selection_owner_id(self, obj: ZimaObject) -> str:
        if obj.kind == ObjectKind.BODY:
            return obj.object_id
        solid_children = [
            child
            for child in obj.children
            if not child.locked
            and child.kind in SOLID_KINDS
        ]
        return solid_children[0].object_id if len(solid_children) == 1 else obj.object_id

    def _result_visibility_id(self, obj: ZimaObject) -> str:
        return obj.object_id

    def _display_model_shape(self, shape, object_id: str) -> None:
        display = self.viewer._display
        self._selectable_model_shapes.append((shape, object_id))

        if self.view_display_mode == ViewDisplayMode.WIRE:
            for ais_shape in display.DisplayShape(shape, color=BLACK, update=False):
                self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                self._configure_model_hover(ais_shape)
                self._model_ais_by_object_id.setdefault(object_id, []).append(ais_shape)
                self._model_object_id_by_ais.append((ais_shape, object_id))
                self._model_edge_ais_by_object_id.setdefault(
                    object_id,
                    [],
                ).append(ais_shape)
            return

        for ais_shape in display.DisplayShape(shape, update=False):
            self._hide_ais_edges(ais_shape)
            self._set_ais_display_mode(ais_shape, AIS_Shaded)
            self._configure_model_hover(ais_shape)
            self._model_ais_by_object_id.setdefault(object_id, []).append(ais_shape)
            self._model_object_id_by_ais.append((ais_shape, object_id))

        if self.view_display_mode == ViewDisplayMode.SHADED_WITH_EDGES:
            for ais_shape in display.DisplayShape(shape, color=BLACK, update=False):
                self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                display.Context.Deactivate(ais_shape)
                self._nonselectable_ais_shapes.append(ais_shape)
                self._model_edge_ais_by_object_id.setdefault(
                    object_id, []
                ).append(ais_shape)

    def _configure_model_hover(self, ais_shape) -> None:
        if self.view_selection_mode != ViewSelectionMode.OBJECT:
            return
        highlight = Prs3d_Drawer()
        highlight.SetColor(YELLOW)
        highlight.SetDisplayMode(AIS_WireFrame)
        highlight.SetZLayer(Graphic3d_ZLayerId_Topmost)
        ais_shape.SetDynamicHilightAttributes(highlight)

    def _highlight_selected_in_view(self) -> None:
        if not hasattr(self, "_viewer_initialized"):
            return
        context = self.viewer._display.Context
        if self.selected_face is None:
            context.ClearSelected(False)
        self._clear_selected_shape_overlays()
        for edge_shapes in self._model_edge_ais_by_object_id.values():
            for edge_shape in edge_shapes:
                edge_shape.SetColor(BLACK)
                edge_shape.SetWidth(1.0)
                context.Redisplay(edge_shape, False)
        for sketch_shapes in self._sketch_ais_by_object_id.values():
            for sketch_shape in sketch_shapes:
                sketch_shape.SetColor(BLUE)
                context.Redisplay(sketch_shape, False)
        if self.selected_object_id is None:
            context.UpdateSelected(True)
            return
        selected_ids = {self.selected_object_id}
        selected = self.document.find_object(self.selected_object_id) if self.document else None
        if selected is not None and selected.kind in (
            ObjectKind.OBJECT,
            ObjectKind.BODY,
            ObjectKind.ORIGIN,
        ):
            selected_ids.update(self._descendant_object_ids(selected))
        if self.selected_face is not None:
            self._display_selected_face_boundary()
            context.UpdateSelected(True)
            return
        for object_id in selected_ids:
            for edge_shape in self._model_edge_ais_by_object_id.get(object_id, []):
                edge_shape.SetColor(YELLOW)
                edge_shape.SetWidth(3.0)
                context.Redisplay(edge_shape, False)
            for sketch_shape in self._sketch_ais_by_object_id.get(object_id, []):
                sketch_shape.SetColor(YELLOW)
                context.Redisplay(sketch_shape, False)
        for shape, owner_id in self._selectable_model_shapes:
            if owner_id not in selected_ids:
                continue
            owner = self.document.find_object(owner_id) if self.document else None
            if owner is None or owner.kind != ObjectKind.SKETCH:
                continue
            ais_shapes = self.viewer._display.DisplayShape(
                shape,
                color=YELLOW,
                update=False,
            )
            for ais_shape in ais_shapes:
                self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                ais_shape.SetWidth(3.0)
                ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
                context.Deactivate(ais_shape)
            self._selected_model_overlay_ais.extend(ais_shapes)
        if self.view_display_mode == ViewDisplayMode.SHADED:
            for shape, owner_id in self._cached_model_shapes:
                if owner_id not in selected_ids:
                    continue
                ais_shapes = self.viewer._display.DisplayShape(
                    shape,
                    color=YELLOW,
                    update=False,
                )
                for ais_shape in ais_shapes:
                    self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                    ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
                    context.Deactivate(ais_shape)
                self._selected_model_overlay_ais.extend(ais_shapes)
        source_object = selected
        if (
            source_object is not None
            and source_object.kind != ObjectKind.OBJECT
            and self.document is not None
        ):
            source_object = self.document.find_owning_object(source_object.object_id)
        if (
            source_object is not None
            and self.document is not None
            and source_object in self.document.active_history_objects()
        ):
            for source_shape in self.document.source_highlight_shapes(source_object):
                ais_shapes = self.viewer._display.DisplayShape(
                    source_shape,
                    color=YELLOW,
                    update=False,
                )
                for ais_shape in ais_shapes:
                    self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                    ais_shape.SetWidth(3.0)
                    ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
                    context.Deactivate(ais_shape)
                self._selected_model_overlay_ais.extend(ais_shapes)
        context.UpdateSelected(True)

    def _display_selected_face_boundary(self) -> None:
        if self.selected_face is None:
            return
        context = self.viewer._display.Context
        explorer = TopExp_Explorer(self.selected_face, TopAbs_EDGE)
        while explorer.More():
            ais_shapes = self.viewer._display.DisplayShape(
                explorer.Current(),
                color=YELLOW,
                update=False,
            )
            for ais_shape in ais_shapes:
                self._set_ais_display_mode(ais_shape, AIS_WireFrame)
                ais_shape.SetWidth(3.0)
                ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
                context.Deactivate(ais_shape)
            self._selected_face_overlay_ais.extend(ais_shapes)
            explorer.Next()

    def _clear_selected_shape_overlays(self) -> None:
        context = self.viewer._display.Context
        for ais_shape in self._selected_model_overlay_ais:
            context.Erase(ais_shape, False)
        self._selected_model_overlay_ais.clear()
        for ais_shape in self._selected_face_overlay_ais:
            context.Erase(ais_shape, False)
        self._selected_face_overlay_ais.clear()

    def _descendant_object_ids(self, parent: ZimaObject) -> set[str]:
        result: set[str] = set()
        for child in parent.children:
            result.add(child.object_id)
            result.update(self._descendant_object_ids(child))
        return result

    def _set_ais_display_mode(self, ais_shape, display_mode: int) -> None:
        context = self.viewer._display.Context
        context.SetDisplayMode(ais_shape, display_mode, False)
        context.Redisplay(ais_shape, False)

    def _hide_ais_edges(self, ais_shape) -> None:
        attributes = ais_shape.Attributes()
        attributes.SetFaceBoundaryDraw(False)
        attributes.SetWireDraw(False)
        attributes.SetFreeBoundaryDraw(False)
        attributes.SetUnFreeBoundaryDraw(False)
        attributes.SetIsoOnTriangulation(False)
        ais_shape.SetAttributes(attributes)

    def _display_origin(self) -> None:
        selected_key = self._selected_origin_shape_key()
        selected_obj = self._selected_object()
        selected_owner = None
        if (
            self.document is not None
            and selected_obj is not None
        ):
            if selected_obj.kind == ObjectKind.OBJECT:
                selected_owner = selected_obj
            elif selected_obj.kind in (
                ObjectKind.ORIGIN,
                ObjectKind.POINT,
                ObjectKind.AXIS,
                ObjectKind.PLANE,
            ):
                selected_owner = (
                    self.document.find_owning_object(selected_obj.object_id)
                    or self.document.find_parent(selected_obj.object_id)
                )
        global_origin = next(
            (
                child
                for child in self.document.root.children
                if child.kind == ObjectKind.ORIGIN
            ),
            None,
        ) if self.document is not None else None
        self._display_coordinate_system(
            origin=(0.0, 0.0, 0.0),
            selected_key=selected_key if selected_owner is global_origin else None,
            transform_persistence=self._origin_transform_persistence,
            coordinate_owner=global_origin,
        )

        if self.document is not None:
            visible_objects = [
                obj
                for obj in self.document.visible_objects()
                if self.document.is_effectively_visible(obj.object_id)
            ]
            if selected_owner is not None:
                visible_objects = sorted(
                    visible_objects,
                    key=lambda obj: obj is selected_owner,
                )
            for obj in visible_objects:
                self._display_object_coordinate_systems(
                    obj,
                    identity_transform(),
                    selected_owner,
                    selected_key,
                )

    def _display_object_coordinate_systems(
        self,
        obj: ZimaObject,
        parent_transform,
        selected_owner: ZimaObject | None,
        selected_key: str | None,
    ) -> None:
        world_transform = multiply_transforms(
            parent_transform,
            coordinate_system_transform(obj.coordinate_system),
        )
        if obj.kind == ObjectKind.OBJECT:
            origin = transform_point(world_transform, (0.0, 0.0, 0.0))
            transform_persistence = Graphic3d_TransformPers(
                Graphic3d_TMF_ZoomPers,
                gp_Pnt(*origin),
            )
            self._display_coordinate_system(
                origin=origin,
                selected_key=selected_key if selected_owner is obj else None,
                transform_persistence=transform_persistence,
                size=140.0,
                coordinate_transform=world_transform,
                coordinate_owner=obj,
            )
            for child in obj.children:
                if (
                    child.kind == ObjectKind.AXIS
                    and not child.locked
                    and child.parameters.get("display_style") == "centerline"
                    and (
                        self.show_axes_action.isChecked()
                        or self.selected_object_id == child.object_id
                    )
                ):
                    self._display_datum_axis(child, world_transform)
        for child in obj.children:
            if not child.locked and child.kind == ObjectKind.OBJECT:
                self._display_object_coordinate_systems(
                    child,
                    world_transform,
                    selected_owner,
                    selected_key,
                )

    def _display_datum_axis(self, axis: ZimaObject, parent_transform) -> None:
        shape = make_datum_axis_shape(axis, parent_transform)
        if shape is None:
            return
        self._coordinate_shapes.append((shape, axis.object_id))
        ais_shapes = self.viewer._display.DisplayShape(
            shape, color=CENTERLINE_COLOR, update=False
        )
        context = self.viewer._display.Context
        for ais_shape in ais_shapes:
            self._set_ais_display_mode(ais_shape, AIS_WireFrame)
            attributes = ais_shape.Attributes()
            attributes.SetOwnLineAspects()
            for aspect_getter, aspect_setter in (
                (attributes.LineAspect, attributes.SetLineAspect),
                (attributes.WireAspect, attributes.SetWireAspect),
                (attributes.FreeBoundaryAspect, attributes.SetFreeBoundaryAspect),
                (attributes.UnFreeBoundaryAspect, attributes.SetUnFreeBoundaryAspect),
                (attributes.SeenLineAspect, attributes.SetSeenLineAspect),
            ):
                line_aspect = aspect_getter()
                line_aspect.SetColor(CENTERLINE_COLOR)
                line_aspect.SetTypeOfLine(Aspect_TOL_DOTDASH)
                line_aspect.SetWidth(2.0)
                aspect_setter(line_aspect)
            ais_shape.SetAttributes(attributes)
            ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
            context.RecomputeSelectionOnly(ais_shape)
            try:
                context.SetSelectionSensitivity(ais_shape, 0, 24)
            except Exception:
                pass
        self._coordinate_ais_shapes.extend(ais_shapes)
        self._coordinate_ais_by_object_id[axis.object_id] = ais_shapes

        length = float(axis.parameters.get("length", 100.0))
        direction = {
            "x": (1.0, 0.0, 0.0),
            "y": (0.0, 1.0, 0.0),
            "z": (0.0, 0.0, 1.0),
        }.get(str(axis.parameters.get("axis", "z")), (0.0, 0.0, 1.0))
        label_point = gp_Pnt(
            *transform_point(
                parent_transform,
                tuple(value * length * 0.56 for value in direction),
            )
        )
        structures = []
        for color in (CENTERLINE_COLOR_RGB, (0.95, 0.85, 0.2)):
            structure = self.viewer._display.DisplayMessage(
                label_point,
                axis.name,
                height=24.0,
                message_color=color,
                update=False,
            )
            structure.SetZLayer(Graphic3d_ZLayerId_Topmost)
            structures.append(structure)
            self._plane_label_structures.append(structure)
        structures[1].Erase()
        self._coordinate_labels_by_object_id[axis.object_id] = structures

    def _display_sketches(self) -> None:
        if self.document is None:
            return

        for obj in self.document.visible_objects():
            self._display_object_sketches(obj, identity_transform())

    def _display_object_sketches(self, obj: ZimaObject, parent_transform) -> None:
        if (
            self.document is not None
            and not self.document.is_effectively_visible(obj.object_id)
        ):
            return
        world_transform = multiply_transforms(
            parent_transform,
            coordinate_system_transform(obj.coordinate_system),
        )
        for child in obj.children:
            if child.kind == ObjectKind.SKETCH:
                if not self.document.is_effectively_visible(child.object_id):
                    continue
                shape = make_sketch_shape(obj, child, world_transform)
                if shape is not None:
                    self._selectable_model_shapes.append((shape, child.object_id))
                    ais_shapes = self.viewer._display.DisplayShape(
                        shape,
                        color=BLUE,
                        update=False,
                    )
                    self._sketch_ais_by_object_id.setdefault(
                        child.object_id,
                        [],
                    ).extend(ais_shapes)
            elif not child.locked:
                self._display_object_sketches(child, world_transform)

    def _display_coordinate_system(
        self,
        origin: tuple[float, float, float],
        selected_key: str | None,
        transform_persistence,
        size: float | None = None,
        coordinate_transform=None,
        coordinate_owner: ZimaObject | None = None,
    ) -> None:
        shape_origin = (0.0, 0.0, 0.0) if coordinate_transform is not None else origin
        shapes = (
            make_origin_shapes(origin=shape_origin, plane_scale=2.0)
            if size is None
            else make_origin_shapes(size=size, origin=shape_origin, plane_scale=2.0)
        )
        if coordinate_transform is not None:
            shapes = {
                key: self._transform_origin_shape(shape, coordinate_transform)
                for key, shape in shapes.items()
            }
        visible_keys = {
            key for key in shapes if self._coordinate_key_visible(coordinate_owner, key)
        }
        for key, color in (
            ("xy_plane", PLANE_COLOR),
            ("yz_plane", PLANE_COLOR),
            ("xz_plane", PLANE_COLOR),
            ("point", BLACK),
            ("x_axis", RED),
            ("y_axis", GREEN),
            ("z_axis", BLUE),
        ):
            if key in visible_keys:
                self._display_origin_shape(
                    key,
                    shapes[key],
                    color,
                    0.0,
                    selected_key,
                    transform_persistence,
                    coordinate_owner,
                )
        self._display_coordinate_labels(
            origin,
            selected_key,
            transform_persistence,
            size,
            coordinate_transform,
            coordinate_owner,
            visible_keys,
        )

    def _coordinate_key_visible(
        self,
        owner: ZimaObject | None,
        key: str,
    ) -> bool:
        child_id = self._coordinate_child_id(owner, key)
        if child_id is not None and child_id == self.selected_object_id:
            return True
        if owner is not None and self.selected_object_id == owner.object_id:
            return True
        if owner is not None and owner.kind == ObjectKind.OBJECT:
            internal_origin = next(
                (child for child in owner.children if child.kind == ObjectKind.ORIGIN),
                None,
            )
            if (
                internal_origin is not None
                and self.selected_object_id == internal_origin.object_id
            ):
                return True
        coordinate = (
            self.document.find_object(child_id)
            if self.document is not None and child_id is not None
            else None
        )
        if coordinate is not None and coordinate.locked:
            return self.show_origins_action.isChecked()
        if key == "point":
            return self.show_points_action.isChecked()
        if key.endswith("_axis"):
            return self.show_axes_action.isChecked()
        if key.endswith("_plane"):
            return self.show_planes_action.isChecked()
        return self.show_origins_action.isChecked()

    def _transform_origin_shape(self, shape, coordinate_transform):
        if isinstance(shape, list):
            return [transform_shape(item, coordinate_transform) for item in shape]
        return transform_shape(shape, coordinate_transform)

    def _display_origin_shape(
        self,
        key: str,
        shape,
        color: Any,
        transparency: float,
        selected_key: str | None,
        transform_persistence,
        coordinate_owner: ZimaObject | None,
    ) -> None:
        coordinate_object_id = self._coordinate_child_id(coordinate_owner, key)
        if coordinate_object_id is not None:
            for item in shape if isinstance(shape, list) else [shape]:
                self._coordinate_shapes.append((item, coordinate_object_id))
                self._coordinate_overlay_sources_by_object_id.setdefault(
                    coordinate_object_id,
                    [],
                ).append((item, transform_persistence))
        if key == selected_key:
            ais_shapes = self._display_overlay_shape(
                shape,
                color=YELLOW,
                transparency=0.0,
                transform_persistence=transform_persistence,
                selection_sensitivity=32 if key.endswith("_axis") or key == "point" else 18,
            )
            if coordinate_object_id is not None:
                self._coordinate_ais_shapes.extend(ais_shapes)
                self._coordinate_ais_by_object_id.setdefault(
                    coordinate_object_id, []
                ).extend(ais_shapes)
            return

        ais_shapes = self._display_overlay_shape(
            shape,
            color=color,
            transparency=transparency,
            transform_persistence=transform_persistence,
            selection_sensitivity=32 if key.endswith("_axis") or key == "point" else 18,
        )
        if key.endswith("_axis") or key == "point":
            highlight = Prs3d_Drawer()
            highlight.SetColor(Quantity_Color(Quantity_NOC_YELLOW))
            highlight.SetDisplayMode(AIS_Shaded)
            highlight.SetZLayer(Graphic3d_ZLayerId_Topmost)
            for ais_shape in ais_shapes:
                ais_shape.SetDynamicHilightAttributes(highlight)
                ais_shape.SetHilightAttributes(highlight)
        if coordinate_object_id is not None:
            self._coordinate_ais_shapes.extend(ais_shapes)
            self._coordinate_ais_by_object_id.setdefault(
                coordinate_object_id, []
            ).extend(ais_shapes)

    def _coordinate_child_id(
        self,
        owner: ZimaObject | None,
        key: str,
    ) -> str | None:
        if owner is None:
            return None
        coordinate_parent = owner
        if owner.kind == ObjectKind.OBJECT:
            if key == "point":
                point_entity = next(
                    (
                        child
                        for child in owner.children
                        if child.kind == ObjectKind.POINT and not child.locked
                    ),
                    None,
                )
                if point_entity is not None:
                    return point_entity.object_id
            coordinate_parent = next(
                (
                    child
                    for child in owner.children
                    if child.kind == ObjectKind.ORIGIN
                ),
                owner,
            )
        for child in coordinate_parent.children:
            if child.kind == ObjectKind.PLANE and key == f"{child.parameters.get('plane')}_plane":
                return child.object_id
            if child.kind == ObjectKind.AXIS and key == f"{child.parameters.get('axis')}_axis":
                return child.object_id
            if child.kind == ObjectKind.POINT and key == "point":
                return child.object_id
        return None

    def _display_overlay_shape(
        self,
        shape,
        color: Any,
        transparency: float,
        transform_persistence,
        selection_sensitivity: int = 18,
    ):
        ais_shapes = self.viewer._display.DisplayShape(
            shape,
            color=color,
            transparency=transparency,
            update=False,
        )
        for ais_shape in ais_shapes:
            ais_shape.SetZLayer(Graphic3d_ZLayerId_Topmost)
            ais_shape.SetTransformPersistence(transform_persistence)
            self.viewer._display.Context.RecomputeSelectionOnly(ais_shape)
            for selection_mode in (0,):
                try:
                    self.viewer._display.Context.SetSelectionSensitivity(
                        ais_shape, selection_mode, selection_sensitivity
                    )
                except Exception:
                    pass
        return ais_shapes

    def _selected_origin_shape_key(self) -> str | None:
        if self.selected_object_id is None:
            return None

        if self.document is None:
            return None

        obj = self.document.find_object(self.selected_object_id)
        if obj is None:
            return None

        if obj.kind == ObjectKind.POINT:
            return "point"
        if obj.kind == ObjectKind.AXIS:
            if obj.parameters.get("display_style") == "centerline":
                return None
            return f"{obj.parameters.get('axis')}_axis"
        if obj.kind == ObjectKind.PLANE:
            return f"{obj.parameters.get('plane')}_plane"
        return None

    def _display_coordinate_labels(
        self,
        origin: tuple[float, float, float],
        selected_key: str | None,
        transform_persistence,
        size: float | None = None,
        coordinate_transform=None,
        coordinate_owner: ZimaObject | None = None,
        visible_keys: set[str] | None = None,
    ) -> None:
        label_origin = (0.0, 0.0, 0.0) if coordinate_transform is not None else origin
        plane_points = (
            make_plane_label_points(origin=label_origin, plane_scale=2.0)
            if size is None
            else make_plane_label_points(
                size=size, origin=label_origin, plane_scale=2.0
            )
        )
        axis_points = (
            make_axis_label_points(origin=label_origin)
            if size is None
            else make_axis_label_points(size=size, origin=label_origin)
        )
        point_offset = (size if size is not None else 320.0) * 0.055
        label_points = {
            **plane_points,
            **axis_points,
            "point": gp_Pnt(
                label_origin[0] + point_offset,
                label_origin[1] + point_offset,
                label_origin[2] + point_offset,
            ),
        }
        if coordinate_transform is not None:
            label_points = {
                key: gp_Pnt(
                    *transform_point(
                        coordinate_transform,
                        (point.X(), point.Y(), point.Z()),
                    )
                )
                for key, point in label_points.items()
            }
        labels = {
            "xy_plane": "XY",
            "yz_plane": "YZ",
            "xz_plane": "XZ",
            "x_axis": "X",
            "y_axis": "Y",
            "z_axis": "Z",
            "point": self._coordinate_point_label(coordinate_owner),
        }
        normal_colors = {
            "xy_plane": PLANE_COLOR_RGB,
            "yz_plane": PLANE_COLOR_RGB,
            "xz_plane": PLANE_COLOR_RGB,
            "x_axis": (1.0, 0.0, 0.0),
            "y_axis": (0.0, 1.0, 0.0),
            "z_axis": (0.0, 0.0, 1.0),
            "point": (0.0, 0.0, 0.0),
        }

        for key, label in labels.items():
            if visible_keys is not None and key not in visible_keys:
                continue
            object_id = self._coordinate_child_id(coordinate_owner, key)
            active = key == selected_key or object_id in (
                self.selected_object_id,
                self._hovered_coordinate_object_id,
            )
            structures = []
            for color in (normal_colors[key], (0.95, 0.85, 0.2)):
                structure = self.viewer._display.DisplayMessage(
                    label_points[key],
                    label,
                    height=24.0,
                    message_color=color,
                    update=False,
                )
                if hasattr(structure, "SetZLayer"):
                    structure.SetZLayer(Graphic3d_ZLayerId_Topmost)
                if hasattr(structure, "SetTransformPersistence"):
                    structure.SetTransformPersistence(transform_persistence)
                structures.append(structure)
                self._plane_label_structures.append(structure)

            normal_structure, highlighted_structure = structures
            if active:
                normal_structure.Erase()
            else:
                highlighted_structure.Erase()
            if object_id is not None:
                self._coordinate_labels_by_object_id[object_id] = structures

    def _coordinate_point_label(self, owner: ZimaObject | None) -> str:
        if owner is not None and owner.kind == ObjectKind.OBJECT:
            suffix = owner.name.removeprefix("Object")
            return f"Point{suffix}" if suffix.isdigit() else f"Point ({owner.name})"
        return "Point"

    def _clear_plane_labels(self) -> None:
        for structure in self._plane_label_structures:
            if hasattr(structure, "Erase"):
                structure.Erase()
        self._plane_label_structures.clear()
        self._coordinate_labels_by_object_id.clear()


def main() -> int:
    try:
        startup_context, qt_arguments = resolve_startup_context(sys.argv[1:])
    except ValueError as exc:
        app = QApplication(sys.argv)
        QMessageBox.critical(None, "ZIMA-CAD", str(exc))
        return 2

    app = QApplication([sys.argv[0], *qt_arguments])
    window = MainWindow(startup_context)
    window.showMaximized()
    if startup_context.document_path is not None:
        QTimer.singleShot(
            0,
            lambda: window.open_document_path(startup_context.document_path),
        )
    return app.exec()
