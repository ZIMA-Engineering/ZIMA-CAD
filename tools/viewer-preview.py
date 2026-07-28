#!/usr/bin/env python3
from __future__ import annotations

import sys
from math import sqrt
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QMainWindow,
    QMessageBox,
    QToolBar,
)

from zima_cad.storage import load_part_document
from zima_cad.viewer import ZimaOpenGLViewer
from zima_cad.viewer_scene import build_document_viewer_scene
from zima_cad.viewer_mesh import (
    combine_viewer_meshes,
    datum_plane_mesh,
    origin_axes_mesh,
    polyline_mesh,
    triangulate_shape,
)


def main() -> int:
    app = QApplication(sys.argv)
    real_document_scene = "--real" in sys.argv[1:]
    file_arguments = [
        argument
        for argument in sys.argv[1:]
        if not argument.startswith("--")
    ]
    file_path = (
        Path(file_arguments[0]).expanduser()
        if file_arguments
        else _select_document()
    )
    if file_path is None:
        return 0
    try:
        document = load_part_document(file_path.resolve())
        shape = document.build_active_shape()
        body_mesh = triangulate_shape(
            shape,
            owner_id=document.root.object_id,
        )
        diagonal = sqrt(
            sum(
                (body_mesh.bounds_max[axis] - body_mesh.bounds_min[axis]) ** 2
                for axis in range(3)
            )
        )
        mesh = combine_viewer_meshes(
            (
                body_mesh,
                origin_axes_mesh(
                    owner_id="test-origin",
                    length=max(diagonal * 0.6, 10.0),
                    center=body_mesh.bounds_min,
                ),
                datum_plane_mesh(
                    owner_id="test-plane",
                    size=max(diagonal * 0.45, 12.0),
                    center=(
                        body_mesh.bounds_min[0] - diagonal * 0.45,
                        body_mesh.bounds_min[1] - diagonal * 0.45,
                        body_mesh.bounds_min[2],
                    ),
                    plane="xy",
                    label="XY",
                ),
                polyline_mesh(
                    owner_id="test-sketch",
                    element_kind="sketch",
                    color=(0.898, 0.722, 0.18),
                    polylines=(
                        (
                            (
                                body_mesh.bounds_min[0],
                                body_mesh.bounds_min[1],
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                            (
                                body_mesh.bounds_max[0],
                                body_mesh.bounds_min[1],
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                            (
                                body_mesh.bounds_max[0],
                                body_mesh.bounds_max[1],
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                            (
                                body_mesh.bounds_min[0],
                                body_mesh.bounds_max[1],
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                            (
                                body_mesh.bounds_min[0],
                                body_mesh.bounds_min[1],
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                        ),
                    ),
                ),
                polyline_mesh(
                    owner_id="test-dimension",
                    element_kind="dimension",
                    color=(1.0, 0.941, 0.416),
                    polylines=(
                        (
                            (
                                body_mesh.bounds_min[0],
                                body_mesh.bounds_max[1] + diagonal * 0.12,
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                            (
                                body_mesh.bounds_max[0],
                                body_mesh.bounds_max[1] + diagonal * 0.12,
                                body_mesh.bounds_max[2] + diagonal * 0.12,
                            ),
                        ),
                    ),
                ),
            )
        )
        if real_document_scene:
            mesh = build_document_viewer_scene(
                document,
                show_document_origin=True,
            )
    except Exception as exc:
        QMessageBox.critical(None, "ZIMA-CAD Viewer", str(exc))
        return 1
    if mesh.is_empty:
        QMessageBox.information(
            None,
            "ZIMA-CAD Viewer",
            "Dokument neobsahuje zobrazitelné Body.",
        )
        return 0

    viewer = ZimaOpenGLViewer()
    window = QMainWindow()
    window.setCentralWidget(viewer)
    toolbar = QToolBar("Výběr")
    selection_filter = QComboBox()
    for label, value in (
        ("Vše", "all"),
        ("Plochy", "face"),
        ("Body", "point"),
        ("Osy", "axis"),
        ("Roviny", "plane"),
    ):
        selection_filter.addItem(label, value)
    def apply_selection_filter(index: int) -> None:
        viewer.set_selection_filter(
            str(selection_filter.itemData(index))
        )

    selection_filter.currentIndexChanged.connect(apply_selection_filter)
    toolbar.addWidget(selection_filter)
    toolbar.addSeparator()
    fit_action = toolbar.addAction("Přizpůsobit")
    fit_action.triggered.connect(viewer.fit_all)
    standard_view = QComboBox()
    for label, value in (
        ("Pohled", ""),
        ("Výchozí", "default"),
        ("Přední", "front"),
        ("Zadní", "back"),
        ("Levý", "left"),
        ("Pravý", "right"),
        ("Horní", "top"),
        ("Dolní", "bottom"),
    ):
        standard_view.addItem(label, value)

    def apply_standard_view(index: int) -> None:
        view_name = str(standard_view.itemData(index))
        if view_name:
            viewer.set_standard_view(view_name)
            standard_view.setCurrentIndex(0)

    standard_view.currentIndexChanged.connect(apply_standard_view)
    toolbar.addWidget(standard_view)
    toolbar.addSeparator()
    display_mode = QComboBox()
    for label, value in (
        ("Stínovaný s hranami", "shaded_with_edges"),
        ("Drátový", "wire"),
        ("Stínovaný", "shaded"),
    ):
        display_mode.addItem(label, value)

    def apply_display_mode(index: int) -> None:
        viewer.set_display_mode(str(display_mode.itemData(index)))

    display_mode.currentIndexChanged.connect(apply_display_mode)
    toolbar.addWidget(display_mode)
    window.addToolBar(toolbar)
    viewer.set_mesh(mesh)
    # Show the test marker's bounding-box corner from the front so
    # point-on-solid picking can be verified immediately.
    if not real_document_scene:
        viewer.camera.yaw_degrees = 45.0
        viewer.camera.pitch_degrees = -135.0
    title_prefix = f"ZIMA-CAD New Viewer Preview — {file_path.name}"

    def update_detection_title(*_args) -> None:
        base_title = (
            f"{title_prefix} — FILTR: {viewer.selection_filter.upper()}"
        )
        edge_type = "HRANA/OSA"
        if viewer._hovered_edge is not None:
            edge_type = next(
                (
                    "NÁČRT"
                    if edge.element_kind == "sketch"
                    else "KÓTA"
                    if edge.element_kind == "dimension"
                    else "OSA"
                    if edge.element_kind == "axis"
                    else "HRANA"
                    for edge in mesh.edges
                    if (
                        edge.owner_id,
                        edge.edge_index,
                    ) == viewer._hovered_edge
                ),
                edge_type,
            )
        detected = (
            ("BOD", viewer._hovered_point),
            (edge_type, viewer._hovered_edge),
            ("ROVINA", viewer._hovered_plane),
            ("PLOCHA", viewer._hovered_face),
        )
        for label, key in detected:
            if key is not None:
                window.setWindowTitle(
                    f"{base_title} — {label} {key[0]}:{key[1]}"
                )
                return
        window.setWindowTitle(base_title)

    viewer.selectionFilterChanged.connect(update_detection_title)
    viewer.hoveredPointChanged.connect(update_detection_title)
    viewer.hoveredEdgeChanged.connect(update_detection_title)
    viewer.hoveredFaceChanged.connect(update_detection_title)
    viewer.hoveredPlaneChanged.connect(update_detection_title)
    update_detection_title()
    window.resize(1200, 800)
    window.show()
    return app.exec()


def _select_document() -> Path | None:
    file_name, _selected_filter = QFileDialog.getOpenFileName(
        None,
        "Otevřít díl pro nový Viewer",
        str(PROJECT_ROOT),
        "Díl ZIMA-CAD (*.prtz)",
    )
    return Path(file_name) if file_name else None


if __name__ == "__main__":
    raise SystemExit(main())
