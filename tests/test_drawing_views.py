import unittest
from unittest.mock import patch
import json
import tempfile
from math import cos, radians, sin
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from PySide6.QtCore import QEvent, QPoint, QPointF, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import QApplication, QTreeWidgetItem, QWidget
from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCC.Core.TopAbs import TopAbs_FACE
from OCC.Core.TopExp import TopExp_Explorer

from zima_cad.app import (
    ApplicationMode,
    AssemblyComponentPropertiesDialog,
    AxisConstraintDialog,
    ContainerSummaryDialog,
    DocumentTextInputDialog,
    EdgeTreatmentPropertiesDialog,
    EndTargetCollectionDialog,
    FamilyTableDialog,
    FileSettingsDialog,
    MainWindow,
    MaterialDialog,
    PlaneAttachmentDialog,
    PointConstraintDialog,
    ProtrusionConstraintDialog,
    RelationsDialog,
    UserParametersDialog,
    SKETCH_CONSTRAINT_SELECTION_TOOLS,
    SKETCH_ENTITY_SELECTION_TOOLS,
    ViewSelectionMode,
    canonical_document_path,
    resource_icon,
    tangent_edge_route,
)
from zima_cad.viewer import ExtentHandle
from zima_cad.body_result import BodyResult, CurveDescriptor, SurfaceDescriptor
from zima_cad.topology import EdgeRef, FaceRef
from zima_cad.drawing import (
    DrawingCanvas,
    DrawingWorkspace,
    cosmetic_pen,
    default_sheet,
    drawing_history_cursor,
    drawing_scale_text,
    delete_drawing_view,
    drawing_sheets,
    parallel_dimension_geometry,
    projected_view_orientation,
    projection_axes,
    project_polylines,
    move_drawing_view,
    technical_projection,
    shaded_projection,
    update_view_bounds,
)
from zima_cad.model import (
    active_face_registry,
    CoordinateSystem,
    ContainerType,
    EntityKind,
    create_empty_assembly,
    create_empty_drawing,
    create_empty_part,
    make_sketch_shape,
    coordinate_system_transform,
    multiply_transforms,
    transform_point,
    transform_shape,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.selection import (
    SelectionCandidate,
    SelectionController,
    SelectionKind,
    SelectionPurpose,
    SelectionRequest,
    SelectionResolution,
    TopologySource,
    ViewerDocumentContext,
    ViewerInteractionScope,
    ViewerSelectionPolicy,
)
from zima_cad.viewer import (
    CameraState,
    ViewerPickCandidate,
    ZimaOpenGLViewer,
    STANDARD_VIEW_ORIENTATIONS,
    _camera_rotation_matrix,
    _surface_pass_for_display_mode,
    _multiply_rotation_matrices,
    orbit_camera_state,
    camera_angles_for_view_direction,
    preserve_camera_for_scene_bounds,
)
from zima_cad.viewer_mesh import (
    EdgePolyline,
    PointMarker,
    SilhouetteEdge,
    ViewerMesh,
    edge_visible_in_display,
    silhouette_segments,
    silhouette_segments_from_edges,
    triangulate_shape,
    topology_subshape,
)


class DrawingViewConventionTests(unittest.TestCase):
    @staticmethod
    def _route_mesh(edges):
        points = tuple(point for edge in edges for point in edge.points)
        return ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=tuple(edges),
            points=(),
            planes=(),
            bounds_min=tuple(min(point[axis] for point in points) for axis in range(3)),
            bounds_max=tuple(max(point[axis] for point in points) for axis in range(3)),
        )

    def test_tangent_edge_route_expands_through_line_and_radius(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(
                2,
                ((1, 0, 0), (1.01, 0, 0), (1.7, 0.3, 0),
                 (2, 0.99, 0), (2, 1, 0)),
                owner_id="body",
                curve_kind="circle",
                curve_radius=1.0,
            ),
            EdgePolyline(3, ((2, 1, 0), (2, 2, 0)), owner_id="body"),
        ))

        self.assertEqual(tangent_edge_route(mesh, "body", 1), (1, 2, 3))

    def test_continuous_route_accepts_coarse_radius_endpoint_chord(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(
                2,
                ((1, 0, 0), (1.25, 0.1, 0), (1.7, 0.3, 0),
                 (1.9, 0.75, 0), (2, 1, 0)),
                owner_id="body",
                curve_kind="circle",
                curve_radius=1.0,
            ),
            EdgePolyline(3, ((2, 1, 0), (2, 2, 0)), owner_id="body"),
        ))

        self.assertEqual(tangent_edge_route(mesh, "body", 1), (1, 2, 3))

    def test_tangent_edge_route_stops_at_ambiguous_branch(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(2, ((1, 0, 0), (2, 0, 0)), owner_id="body"),
            EdgePolyline(3, ((1, 0, 0), (3, 0, 0)), owner_id="body"),
        ))

        self.assertEqual(tangent_edge_route(mesh, "body", 1), (1,))

    def test_edge_treatment_click_selects_complete_route_as_one_group(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(2, ((1, 0, 0), (2, 0, 0)), owner_id="body"),
            EdgePolyline(3, ((2, 0, 0), (3, 0, 0)), owner_id="body"),
        ))
        window = MainWindow.__new__(MainWindow)
        window._native_viewer_scene = SimpleNamespace(mesh=mesh)
        window._edge_treatment_groups = []
        window._edge_treatment_group_seeds = []
        window._selection_controller = SelectionController()
        window._selection_controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(value=EdgeRef(
                "body", "runtime", str(candidate.element_index)
            )),
            on_complete=lambda _values: None,
            maximum_count=100,
        ))

        update = window._toggle_edge_treatment_route("body", 2)

        self.assertTrue(update.accepted)
        self.assertEqual(
            window._selection_controller.candidate_keys,
            (("edge", "body", 1), ("edge", "body", 2), ("edge", "body", 3)),
        )
        self.assertEqual(
            window._edge_treatment_groups,
            [(('edge', 'body', 1), ('edge', 'body', 2), ('edge', 'body', 3))],
        )
        self.assertEqual(
            window._edge_treatment_group_seeds,
            [("edge", "body", 2)],
        )

    def test_restore_tangent_route_rebuilds_group_from_seed(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(2, ((1, 0, 0), (2, 0, 0)), owner_id="body"),
            EdgePolyline(3, ((2, 0, 0), (3, 0, 0)), owner_id="body"),
        ))
        window = MainWindow.__new__(MainWindow)
        window._native_viewer_scene = SimpleNamespace(mesh=mesh)
        window._edge_treatment_route_mesh = mesh
        window._edge_treatment_groups = []
        window._edge_treatment_group_seeds = []
        window._selection_controller = SelectionController()
        window._selection_controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(value=EdgeRef(
                "body", "runtime", str(candidate.element_index)
            )),
            on_complete=lambda _values: None,
            maximum_count=100,
        ))
        window._refresh_fillet_selection_ui = lambda: None
        window._toggle_edge_treatment_route("body", 2)
        full_group = window._edge_treatment_groups[0]
        window._selection_controller.remove_key(("edge", "body", 3))
        window._edge_treatment_groups[0] = full_group[:-1]

        window._restore_edge_treatment_route(full_group[:-1])

        self.assertEqual(window._edge_treatment_groups[0], full_group)
        self.assertEqual(len(window._selection_controller.values), 3)

    def test_edge_treatment_plain_click_keeps_existing_route(self):
        mesh = self._route_mesh((
            EdgePolyline(1, ((0, 0, 0), (1, 0, 0)), owner_id="body"),
            EdgePolyline(2, ((0, 2, 0), (1, 2, 0)), owner_id="body"),
        ))
        window = MainWindow.__new__(MainWindow)
        window._native_viewer_scene = SimpleNamespace(mesh=mesh)
        window._edge_treatment_groups = []
        window._edge_treatment_group_seeds = []
        window._selection_controller = SelectionController()
        window._selection_controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(value=EdgeRef(
                "body", "runtime", str(candidate.element_index)
            )),
            on_complete=lambda _values: None,
            maximum_count=100,
        ))

        window._toggle_edge_treatment_route("body", 1)
        window._toggle_edge_treatment_route("body", 2)

        self.assertEqual(
            window._selection_controller.candidate_keys,
            (("edge", "body", 1), ("edge", "body", 2)),
        )
        self.assertEqual(len(window._edge_treatment_groups), 2)

    def test_edge_treatment_route_describes_persisted_curve_objects(self):
        mesh = self._route_mesh((
            EdgePolyline(
                1, ((0, 0, 0), (1, 0, 0)), owner_id="body",
                curve_kind="line",
            ),
            EdgePolyline(
                2, ((1, 0, 0), (1, 1, 0)), owner_id="body",
                curve_kind="circle", curve_radius=5.0,
            ),
        ))
        window = MainWindow.__new__(MainWindow)
        window._edge_treatment_route_mesh = mesh
        groups = ((('edge', 'body', 1), ('edge', 'body', 2)),)

        descriptions = window._edge_treatment_key_descriptions(groups)

        self.assertIn("1", descriptions[("edge", "body", 1)])
        self.assertIn("5", descriptions[("edge", "body", 2)])

    def test_edge_treatment_dialog_lists_routes_in_two_columns(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        dialog = EdgeTreatmentPropertiesDialog(2.0, parent=parent)
        group = (("edge", "body", 3), ("edge", "body", 4))

        dialog.set_selected_edge_groups(
            (group,),
            {
                group[0]: "Line · edge 3",
                group[1]: "Radius R5 mm · edge 4",
            },
        )

        self.assertEqual(dialog.edge_list.columnCount(), 2)
        route = dialog.edge_list.topLevelItem(0)
        self.assertEqual(route.childCount(), 2)
        self.assertEqual(route.child(0).text(1), "Line · edge 3")
        self.assertEqual(
            route.child(1).data(0, Qt.ItemDataRole.UserRole), (group[1],)
        )
        self.assertEqual(
            route.child(1).data(0, Qt.ItemDataRole.UserRole + 1), group
        )
        dialog.close()
        parent.close()

    def test_edge_treatment_removes_only_selected_route_member(self):
        keys = tuple(("edge", "body", index) for index in (7, 9, 11, 13))
        window = MainWindow.__new__(MainWindow)
        window._selection_controller = SelectionController()
        window._selection_controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(value=EdgeRef(
                "body", "runtime", str(candidate.element_index)
            )),
            on_complete=lambda _values: None,
            maximum_count=100,
        ))
        for _kind, owner_id, edge_index in keys:
            window._selection_controller.toggle(SelectionCandidate(
                kind=SelectionKind.EDGE,
                owner_id=owner_id,
                element_index=edge_index,
            ))
        window._edge_treatment_groups = [keys]
        window._edge_treatment_group_seeds = [keys[0]]
        window._refresh_fillet_selection_ui = lambda: None

        window._remove_fillet_edge((keys[1],))

        self.assertEqual(
            window._edge_treatment_groups,
            [(keys[0], keys[2], keys[3])],
        )
        self.assertEqual(
            window._selection_controller.candidate_keys,
            (keys[0], keys[2], keys[3]),
        )

    def test_material_dialog_is_internal_and_cancel_keeps_document(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        document = create_empty_part()
        document.physical_parameters = {"MATERIAL_NAME": "Steel"}
        dialog = MaterialDialog(
            document,
            Path("."),
            "en",
            {},
            parent,
        )

        value_item = dialog.table.item(0, dialog.VALUE_COLUMN)
        self.assertIsNotNone(value_item)
        value_item.setText("Aluminium")
        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertEqual(
            document.physical_parameters,
            {"MATERIAL_NAME": "Steel"},
        )
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_document_dialog_rejection_includes_material(self):
        rejected = []
        material = SimpleNamespace(
            isVisible=lambda: True,
            reject=lambda: rejected.append("material"),
        )
        window = MainWindow.__new__(MainWindow)
        window.material_dialog = material

        window._reject_document_scoped_dialogs()

        self.assertEqual(rejected, ["material"])

    def test_user_parameters_dialog_is_internal_and_cancel_keeps_document(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        document = create_empty_part()
        document.user_parameter_order = ["length"]
        document.user_parameter_labels = {"length": {"en": "Length"}}
        document.user_parameter_values = {"length": {"": "10"}}
        document.user_parameters = {"length": "10"}
        dialog = UserParametersDialog(document, "en", parent)

        value_item = dialog.table.item(0, dialog.VALUE_COLUMN)
        self.assertIsNotNone(value_item)
        value_item.setText("20")
        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertEqual(document.user_parameters, {"length": "10"})
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_relations_dialog_is_internal_and_cancel_keeps_document(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        document = create_empty_part()
        document.relations = [{"target": "mass", "expression": "model.mass"}]
        dialog = RelationsDialog(document, parent)

        expression_item = dialog.table.item(0, 1)
        self.assertIsNotNone(expression_item)
        expression_item.setText("model.volume")
        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertEqual(
            document.relations,
            [{"target": "mass", "expression": "model.mass"}],
        )
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_file_settings_dialog_is_internal_and_cancel_keeps_document(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        document = create_empty_part()
        original_precision = dict(document.document_precision)
        dialog = FileSettingsDialog(document, parent)
        parent.resize(1000, 700)
        parent.show()
        dialog.show()
        application.processEvents()

        dialog.decimal_places.setValue(9)

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertGreater(dialog.height(), 100)
        dialog.reject()
        self.assertEqual(document.document_precision, original_precision)
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_document_text_input_is_internal_and_cancel_has_no_acceptance(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        accepted = []
        dialog = DocumentTextInputDialog(
            "Rename",
            "Name",
            "part.prtz",
            parent,
        )
        dialog.accepted.connect(lambda: accepted.append(dialog.text()))

        dialog.text_edit.setText("renamed.prtz")
        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertEqual(accepted, [])
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_family_table_add_column_uses_internal_prompt(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        dialog = FamilyTableDialog(create_empty_part(), "part.prtz", parent)

        dialog._add_column()
        prompt = dialog._add_column_dialog

        self.assertTrue(prompt.windowFlags() & Qt.WindowType.SubWindow)
        prompt.text_edit.setText("WIDTH")
        prompt.accept()
        self.assertEqual(dialog.table.columnCount(), 2)
        self.assertEqual(dialog.table.horizontalHeaderItem(1).text(), "WIDTH")
        dialog.reject()
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_container_summary_is_internal_and_cancel_keeps_object(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        document = create_empty_part()
        obj = document.root
        original_name = obj.name
        dialog = ContainerSummaryDialog(obj, document, parent)

        dialog.name_edit.setText("Changed")
        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        self.assertEqual(obj.name, original_name)
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_plane_attachment_dialog_is_internal(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        dialog = PlaneAttachmentDialog("Source", "Target", "front", parent)

        dialog.reject()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertFalse(dialog.isModal())
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_only_exact_active_component_tree_row_is_cyan(self):
        assembly = create_empty_assembly()
        first = assembly.create_container("01.prtz", ContainerType.COMPONENT)
        second = assembly.create_container("01.prtz", ContainerType.COMPONENT)
        window = MainWindow.__new__(MainWindow)
        window._active_component_entity_id = second.entity_id
        first_item = QTreeWidgetItem([first.name])
        second_item = QTreeWidgetItem([second.name])

        window._style_active_component_tree_item(first_item, first)
        window._style_active_component_tree_item(second_item, second)

        self.assertFalse(first_item.font(0).bold())
        self.assertEqual(
            first_item.background(0).style(),
            Qt.BrushStyle.NoBrush,
        )
        self.assertTrue(second_item.font(0).bold())
        self.assertEqual(
            second_item.background(0).color().name().upper(),
            "#00D1FF",
        )
        self.assertEqual(
            second_item.foreground(0).color().name().upper(),
            "#102027",
        )

    def test_tab_close_collapses_sketch_and_active_component_first(self):
        window = MainWindow.__new__(MainWindow)
        window._sketch_edit_entity_id = "sketch"
        window._active_component_return_document = object()
        calls = []
        window._reject_document_scoped_dialogs = lambda: calls.append(
            "dialogs"
        )
        window._leave_sketch_edit = lambda **_kwargs: calls.append(
            "sketch"
        )
        window._leave_active_component_context = (
            lambda **_kwargs: calls.append("component")
        )
        window._store_active_session = lambda: calls.append("session")

        window._prepare_active_document_close()

        self.assertEqual(
            calls,
            ["dialogs", "sketch", "component", "session"],
        )

    def test_assembly_cache_tracks_linked_part_file_content(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Linked", ContainerType.COMPONENT
        )
        component.parameters["source_path"] = "linked.prtz"
        with tempfile.TemporaryDirectory() as directory:
            assembly_path = Path(directory) / "assembly.asmz"
            part_path = Path(directory) / "linked.prtz"
            part_path.write_bytes(b"first revision")

            self.assertFalse(
                MainWindow._assembly_source_signatures_match(
                    assembly, assembly_path
                )
            )
            MainWindow._update_assembly_source_signatures(
                assembly, assembly_path
            )
            self.assertTrue(
                MainWindow._assembly_source_signatures_match(
                    assembly, assembly_path
                )
            )
            part_path.write_bytes(b"second revision")
            self.assertFalse(
                MainWindow._assembly_source_signatures_match(
                    assembly, assembly_path
                )
            )

    def test_assembly_cache_requires_component_and_cut_source_packets(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Part", ContainerType.COMPONENT
        )
        cut = assembly.create_container(
            "Cut", ContainerType.PROTRUSION
        )
        history = assembly.history_objects()

        self.assertFalse(MainWindow._cached_source_history_complete(
            assembly,
            history,
            SimpleNamespace(source_bodies={component.entity_id: object()}),
        ))
        self.assertTrue(MainWindow._cached_source_history_complete(
            assembly,
            history,
            SimpleNamespace(source_bodies={
                component.entity_id: object(),
                cut.entity_id: object(),
            }),
        ))

    def test_opening_part_reuses_document_edited_through_assembly(self):
        window = MainWindow.__new__(MainWindow)
        source_document = create_empty_part()
        with tempfile.TemporaryDirectory() as directory:
            source_path = canonical_document_path(
                Path(directory) / "source.prtz"
            )
            window.document_sessions = []
            window._assembly_part_documents = {
                source_path: source_document
            }
            window.workspace = SimpleNamespace(windows=[window])
            installed = []
            window._install_opened_document = (
                lambda document, path: installed.append((document, path))
            )

            with patch("zima_cad.app.load_part_document") as load:
                self.assertTrue(window.open_document_path(source_path))

            load.assert_not_called()
            self.assertEqual(installed, [(source_document, source_path)])

    def test_standalone_part_edit_is_published_to_assemblies(self):
        window = MainWindow.__new__(MainWindow)
        source_document = create_empty_part()
        with tempfile.TemporaryDirectory() as directory:
            source_path = canonical_document_path(
                Path(directory) / "source.prtz"
            )
            window.document = source_document
            window.current_file_path = source_path
            window._assembly_part_documents = {}
            window._dirty_assembly_part_paths = set()

            window._mark_model_for_regeneration()

            self.assertTrue(source_document.regeneration_required)
            self.assertIs(
                window._assembly_part_documents[source_path],
                source_document,
            )
            self.assertIn(source_path, window._dirty_assembly_part_paths)

    def test_drawing_add_sheet_inserts_at_history_cursor(self):
        document = create_empty_drawing()
        first = default_sheet(1)
        second = default_sheet(2)
        document.document_settings["drawing_history_cursor"] = "1"
        workspace = DrawingWorkspace.__new__(DrawingWorkspace)
        workspace.document = document
        workspace.sheets = [first, second]
        workspace.active_sheet_index = 0
        workspace._refresh_controls = lambda **_kwargs: None
        workspace._store = lambda: None

        workspace.add_sheet()

        self.assertEqual(len(workspace.sheets), 3)
        self.assertIs(workspace.sheets[0], first)
        self.assertIs(workspace.sheets[2], second)
        self.assertEqual(workspace.active_sheet_index, 1)
        self.assertEqual(
            drawing_history_cursor(document, workspace.sheets), 2
        )

    def test_assembly_history_cursor_controls_active_components_and_cuts(self):
        assembly = create_empty_assembly()
        first = assembly.create_container(
            "Part", ContainerType.COMPONENT
        )
        assembly.create_container("Cut", ContainerType.PROTRUSION)

        assembly.set_history_cursor(1)

        self.assertEqual(assembly.active_history_objects(), [first])

    def test_assembly_regeneration_refreshes_cut_source_face_plane(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Part", ContainerType.COMPONENT
        )
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        reference_id = f"{component.entity_id}:face:1"
        references = [{
            "type": "face",
            "reference_scope": "source_object",
            "source_object_id": component.entity_id,
            "entity_id": component.entity_id,
            "surface_reference_id": reference_id,
            "equations": [[1.0, 0.0, 0.0, 10.0]],
        }]
        body_result = SimpleNamespace(faces={
            reference_id: SurfaceDescriptor(
                reference_id=reference_id,
                kind="plane",
                origin=(25.0, 0.0, 0.0),
                normal=(1.0, 0.0, 0.0),
            )
        })

        self.assertTrue(
            window._refresh_history_result_surface_references(
                references, body_result
            )
        )
        self.assertEqual(
            references[0]["equations"],
            [[1.0, 0.0, 0.0, 25.0]],
        )
        self.assertEqual(references[0]["entity_id"], component.entity_id)

    def test_part_regeneration_refreshes_protrusion_source_face_plane(self):
        document = create_empty_part()
        source = document.create_container(
            "First protrusion", ContainerType.PROTRUSION
        )
        window = MainWindow.__new__(MainWindow)
        window.document = document
        stable_id = '{"feature_id":"first","role":"end"}'
        references = [{
            "type": "face",
            "reference_scope": "source_object",
            "source_object_id": source.entity_id,
            "entity_id": source.entity_id,
            "surface_reference_id": stable_id,
            "equations": [[0.0, 0.0, 1.0, 100.0]],
        }]
        lookup_key = f"{document.root.entity_id}:face:3"
        body_result = SimpleNamespace(faces={
            lookup_key: SurfaceDescriptor(
                reference_id=stable_id,
                kind="plane",
                origin=(0.0, 0.0, 120.0),
                normal=(0.0, 0.0, 1.0),
            )
        })

        changed = window._refresh_history_result_surface_references(
            references, body_result
        )

        self.assertTrue(changed)
        self.assertEqual(
            references[0]["equations"],
            [[0.0, 0.0, 1.0, 120.0]],
        )
        self.assertEqual(references[0]["entity_id"], source.entity_id)
        self.assertEqual(references[0]["topology_key"], "3")

    def test_sketch_axis_snap_combines_with_direction_inference(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._sketch_pending_points = [(5.0, 3.0)]
        viewer._sketch_entities = []
        viewer._sketch_external_references = []

        self.assertEqual(
            viewer._sketch_reference_direction_snap(
                "sketch_axis:y", "horizontal", (0.0, 3.4)
            ),
            (0.0, 3.0),
        )
        self.assertEqual(
            viewer._sketch_reference_direction_snap(
                "sketch_axis:x", "vertical", (5.4, 0.0)
            ),
            (5.0, 0.0),
        )

    def test_external_segment_button_tracks_selection_command(self):
        window = MainWindow.__new__(MainWindow)
        window._selection_controller = SelectionController()
        self.assertFalse(window._external_segment_command_active())

        window._selection_controller.begin(SelectionRequest(
            command_id="sketch_external_segment",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(candidate),
            on_complete=lambda _values: None,
        ))

        self.assertTrue(window._external_segment_command_active())
        window._selection_controller.cancel()
        self.assertFalse(window._external_segment_command_active())

    def test_part_function_dialog_makes_document_origin_available(self):
        window = MainWindow.__new__(MainWindow)
        window._definition_dialog_depth = 0
        window.point_constraint_dialog = object()

        self.assertTrue(window._definition_origin_is_visible())

    def test_empty_placement_table_keeps_one_explicit_target_row(self):
        application = QApplication.instance() or QApplication([])
        dialog = PointConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            suggested_name="Test",
        )

        self.assertEqual(dialog.references, [])
        self.assertEqual(dialog.reference_list.rowCount(), 1)
        self.assertEqual(
            dialog.reference_list.item(0, 1).data(
                Qt.ItemDataRole.UserRole
            ),
            "empty-reference",
        )
        dialog.close()
        self.assertIsNotNone(application)

    def test_reference_removal_survives_reentrant_picker_change(self):
        application = QApplication.instance() or QApplication([])
        dialog = PointConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            suggested_name="Test",
        )
        dialog._add_reference({
            "type": "point",
            "key": "point-1",
            "label": "Point 1",
            "equations": [],
        })
        dialog._activate_position_reference_selection = (
            lambda: dialog.references.pop(0)
        )

        dialog._remove_reference_at(0)

        self.assertEqual(dialog.references, [])
        dialog.close()
        self.assertIsNotNone(application)

    def test_placement_table_keeps_next_target_until_three_references(self):
        application = QApplication.instance() or QApplication([])
        dialog = PointConstraintDialog(
            lambda references, fallback: (
                fallback,
                max(0, 3 - len(references)),
                "",
                (False, False, False),
            ),
            suggested_name="Test",
        )

        for index in range(3):
            dialog._add_reference({
                "type": "point",
                "key": f"point-{index}",
                "label": f"Point {index + 1}",
                "equations": [],
            })
            empty_rows = [
                row
                for row in range(dialog.reference_list.rowCount())
                if dialog.reference_list.item(row, 1) is not None
                and dialog.reference_list.item(row, 1).data(
                    Qt.ItemDataRole.UserRole
                ) == "empty-reference"
            ]
            self.assertEqual(dialog.reference_list.rowCount(), min(index + 2, 3))
            self.assertEqual(empty_rows, [index + 1] if index < 2 else [])

        dialog.close()
        self.assertIsNotNone(application)

    def test_assembly_cut_exceptions_are_individual_instance_rows(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
            subtract_only=True,
            assembly_components=[
                ("instance-a", "01.prtz"),
                ("instance-b", "01.prtz"),
            ],
        )

        self.assertEqual(dialog.cut_exception_ids(), [])
        self.assertEqual(dialog.cut_exception_table.rowCount(), 1)
        expected_height = (
            dialog.cut_exception_table.horizontalHeader().sizeHint().height()
            + 3 * 34
            + dialog.cut_exception_table.frameWidth() * 2
        )
        self.assertEqual(dialog.cut_exception_table.height(), expected_height)
        dialog.toggle_cut_exception("instance-b")

        self.assertEqual(dialog.cut_exception_ids(), ["instance-b"])
        self.assertEqual(dialog.cut_exception_table.rowCount(), 2)
        dialog.toggle_cut_exception("instance-a")
        self.assertEqual(
            dialog.cut_exception_ids(),
            ["instance-b", "instance-a"],
        )
        dialog.toggle_cut_exception("instance-b")
        self.assertEqual(dialog.cut_exception_ids(), ["instance-a"])
        dialog.close()
        self.assertIsNotNone(application)

    def test_protrusion_quick_controls_cycle_their_matching_states(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )

        self.assertEqual(dialog.forward_length_spin.value(), 50.0)
        self.assertEqual(dialog.reverse_length_spin.value(), 60.0)
        self.assertFalse(dialog.reverse_length_spin.isHidden())
        self.assertFalse(dialog.reverse_length_spin.isEnabled())
        self.assertEqual(
            dialog.reverse_length_spin.graphicsEffect().opacity(), 0.0
        )
        self.assertEqual(
            dialog.layout().stretch(
                dialog.layout().indexOf(dialog.reference_list)
            ),
            0,
        )
        self.assertTrue(
            dialog.layout().alignment() & Qt.AlignmentFlag.AlignTop
        )
        dialog.result_type_flip_button.click()
        self.assertEqual(dialog.result_type_combo.currentData(), "thin")
        thin_notifications = []
        thin_refreshes = []
        dialog.definitionChanged.connect(
            lambda: thin_notifications.append(
                str(dialog.thin_mode_combo.currentData())
            )
        )
        dialog.thinPreviewRefreshRequested.connect(
            lambda: thin_refreshes.append(True)
        )
        dialog.thin_mode_switch_button.click()
        QApplication.processEvents()
        self.assertEqual(dialog.thin_mode_combo.currentData(), "other_side")
        self.assertGreaterEqual(len(thin_notifications), 1)
        self.assertEqual(thin_refreshes, [True])
        dialog.extent_switch_button.click()
        self.assertEqual(dialog.extent_mode_combo.currentData(), "two_sides")
        dialog.forward_length_spin.setValue(10.0)
        dialog.reverse_length_spin.setValue(20.0)
        dialog.direction_flip_button.click()
        self.assertEqual(dialog.forward_length_spin.value(), 20.0)
        self.assertEqual(dialog.reverse_length_spin.value(), 10.0)
        dialog.extent_switch_button.click()
        self.assertEqual(dialog.extent_mode_combo.currentData(), "symmetric")
        dialog.direction_flip_button.click()
        self.assertEqual(
            dialog.protrusion_direction_combo.currentData(), "forward"
        )
        dialog.close()
        self.assertIsNotNone(application)

    def test_protrusion_end_condition_row_tracks_operation_without_replacing_length(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )
        through_index = dialog.forward_end_condition_combo.findData(
            "through_all"
        )

        self.assertEqual(
            dialog.forward_end_condition_combo.currentData(), "length"
        )
        self.assertFalse(
            dialog.forward_end_condition_combo.model().item(
                through_index
            ).isEnabled()
        )
        dialog.forward_length_spin.setValue(37.0)
        dialog.subtract_operation_button.click()

        self.assertEqual(
            dialog.forward_end_condition_combo.currentData(), "length"
        )
        self.assertEqual(dialog.forward_length_spin.value(), 37.0)
        self.assertTrue(
            dialog.forward_end_condition_combo.model().item(
                through_index
            ).isEnabled()
        )
        dialog.forward_end_condition_combo.setCurrentIndex(through_index)
        self.assertTrue(dialog.forward_length_spin.isHidden())
        dialog.add_operation_button.click()

        self.assertEqual(
            dialog.forward_end_condition_combo.currentData(), "length"
        )
        self.assertEqual(dialog.forward_length_spin.value(), 37.0)
        dialog.forward_end_condition_combo.setCurrentIndex(
            dialog.forward_end_condition_combo.findData("up_to")
        )
        self.assertFalse(dialog.forward_end_reference_edit.isHidden())
        dialog.eventFilter(
            dialog.forward_end_reference_edit,
            QEvent(QEvent.Type.FocusIn),
        )
        self.assertFalse(dialog.end_reference_pick_active())
        dialog.eventFilter(
            dialog.forward_end_reference_edit,
            QEvent(QEvent.Type.MouseButtonPress),
        )
        self.assertTrue(dialog.end_reference_pick_active())
        self.assertTrue(dialog.select_end_reference({
            "kind": "face", "label": "Face 1",
        }))
        self.assertFalse(dialog.end_reference_pick_active())
        dialog.eventFilter(
            dialog.forward_end_reference_edit,
            QEvent(QEvent.Type.MouseButtonPress),
        )
        self.assertTrue(dialog.end_reference_pick_active())
        self.assertTrue(dialog.select_end_reference({
            "kind": "face", "label": "Face 2",
        }))
        self.assertEqual(dialog.forward_end_reference_edit.text(), "Face 2")
        dialog.clear_end_reference("forward")
        self.assertEqual(dialog.forward_end_reference_edit.text(), "")
        self.assertTrue(dialog.end_reference_pick_active())
        self.assertTrue(dialog.select_end_reference({
            "kind": "face", "label": "Face 3",
        }))
        dialog.clear_end_reference("forward")
        self.assertEqual(dialog.forward_end_reference_edit.text(), "")
        self.assertTrue(dialog.end_reference_pick_active())
        dialog.close()
        self.assertIsNotNone(application)

    def test_up_to_target_collection_is_atomic_and_internal(self):
        application = QApplication.instance() or QApplication([])
        parent = QWidget()
        original = [{"kind": "face", "label": "Face 1"}]
        accepted = []
        dialog = EndTargetCollectionDialog(original, parent)
        dialog.targetsAccepted.connect(accepted.append)

        dialog._remove_target(dialog._targets[0])
        dialog.reject()
        self.assertEqual(accepted, [])
        self.assertEqual(original, [{"kind": "face", "label": "Face 1"}])

        dialog = EndTargetCollectionDialog(original, parent)
        dialog.targetsAccepted.connect(accepted.append)
        dialog._remove_target(dialog._targets[0])
        dialog.accept()

        self.assertTrue(dialog.windowFlags() & Qt.WindowType.SubWindow)
        self.assertEqual(accepted, [[]])
        dialog.deleteLater()
        parent.deleteLater()
        application.processEvents()

    def test_protrusion_persists_up_to_target_as_collection(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )
        dialog.forward_end_condition_combo.setCurrentIndex(
            dialog.forward_end_condition_combo.findData("up_to")
        )
        dialog._set_end_reference_pick_active("forward", True)
        self.assertTrue(dialog.select_end_reference({
            "kind": "face", "label": "Face 1",
        }))

        definition = dialog.protrusion_end_definition()

        self.assertEqual(definition["end_targets_forward"], [{
            "kind": "face", "label": "Face 1",
        }])
        self.assertNotIn("end_reference_forward", definition)
        dialog.close()
        self.assertIsNotNone(application)

    def test_operation_preview_is_restored_after_button_event(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )
        window = MainWindow.__new__(MainWindow)
        window.point_constraint_dialog = dialog
        refreshed = []
        window._preview_protrusion_dialog_frame = refreshed.append
        dialog.show()

        window._queue_protrusion_operation_preview(dialog)
        application.processEvents()

        self.assertEqual(refreshed, [dialog])
        dialog.close()

    def test_protrusion_property_pick_modes_are_mutually_exclusive(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )

        dialog.profile_pick_button.setChecked(True)
        self.assertTrue(dialog.profile_pick_active())
        dialog._set_end_reference_pick_active("forward", True)
        self.assertTrue(dialog.end_reference_pick_active())
        self.assertFalse(dialog.profile_pick_active())

        dialog._activate_position_reference_selection()
        self.assertFalse(dialog.end_reference_pick_active())
        self.assertFalse(dialog.profile_pick_active())

        dialog._set_end_reference_pick_active("forward", True)
        dialog._set_cut_exception_pick_active(True)
        self.assertTrue(dialog.cut_exception_pick_active())
        self.assertFalse(dialog.end_reference_pick_active())

        dialog._activate_container_orientation_row(0)
        application.processEvents()
        self.assertTrue(dialog.container_orientation_selection_active())
        self.assertFalse(dialog.cut_exception_pick_active())
        self.assertFalse(dialog.end_reference_pick_active())
        dialog.close()

    def test_extent_handle_drag_snaps_to_whole_millimetres(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer.world_to_screen = lambda point: QPointF(point[0] * 10.0, 0.0)
        handle = ExtentHandle(
            "length_forward", (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 50.0
        )

        self.assertEqual(
            viewer._extent_handle_value(handle, QPointF(126.0, 4.0)),
            13.0,
        )
        self.assertEqual(
            viewer._extent_handle_value(handle, QPointF(-44.0, 0.0)),
            -4.0,
        )

    def test_extent_handle_survives_transient_empty_preview_rebuild(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        handle = ExtentHandle(
            "length_forward", (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 50.0
        )
        viewer._extent_handles = (handle,)
        viewer._dragged_extent_handle_key = handle.key
        viewer._hovered_extent_handle_key = handle.key
        viewer.update = lambda: None

        viewer.set_extent_handles(())

        self.assertEqual(viewer._extent_handles, (handle,))
        self.assertEqual(viewer._dragged_extent_handle_key, handle.key)

    def test_first_origin_alignment_immediately_shows_remove_crosses(self):
        application = QApplication.instance() or QApplication([])
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "First", ContainerType.COMPONENT
        )
        choices = []
        for plane, normal in (
            ("XZ", (0.0, 1.0, 0.0)),
            ("XY", (0.0, 0.0, 1.0)),
            ("YZ", (1.0, 0.0, 0.0)),
        ):
            choices.extend((
                (
                    f"First / {plane}",
                    f"{component.entity_id}:plane:{plane}",
                    (0.0, 0.0, 0.0),
                    normal,
                ),
                (
                    f"Assembly / {plane}",
                    f"assembly:{plane}",
                    (0.0, 0.0, 0.0),
                    normal,
                ),
            ))
        dialog = AssemblyComponentPropertiesDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            component,
            choices,
            choices,
        )

        self.assertTrue(dialog.accept_assembly_origin())
        self.assertEqual(
            [
                dialog.reference_list.cellWidget(row, 0).text()
                for row in range(3)
            ],
            ["×", "×", "×"],
        )
        dialog.close()
        self.assertIsNotNone(application)

    def test_active_instance_context_uses_part_container_policy(self):
        part = create_empty_part()
        assembly = create_empty_assembly()
        context = ViewerDocumentContext(
            display_document=assembly,
            editing_document=part,
            active_instance_id="second-instance",
            editing_history_boundary=0,
            interaction_scope=ViewerInteractionScope.ACTIVE_PART_CONTAINERS,
        )
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._viewer_document_context = context
        window._selection_controller = SelectionController()
        window.assembly_component_dialog = None
        window.point_constraint_dialog = None
        window.orientation_dialog = None
        window._sketch_reference_mode = False

        policy = window._viewer_selection_policy()

        self.assertEqual(policy.purpose, SelectionPurpose.PART_CONTAINER)

    def test_passive_instance_packet_replaces_assembly_history_source(self):
        history_source = BodyResult.from_mesh(ViewerMesh(
            triangle_positions=(), triangle_normals=(),
            triangle_face_indices=(), triangle_owner_ids=(),
            edges=(), points=(), planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(1.0, 1.0, 1.0),
        ))
        instance_packet = BodyResult.from_mesh(ViewerMesh(
            triangle_positions=(), triangle_normals=(),
            triangle_face_indices=(), triangle_owner_ids=(),
            edges=(), points=(), planes=(),
            bounds_min=(70.0, 0.0, 0.0),
            bounds_max=(71.0, 1.0, 1.0),
        ))

        resolved = MainWindow._passive_instance_display_results(
            {"same-instance-id": history_source},
            {"same-instance-id": instance_packet},
        )

        self.assertIs(resolved["same-instance-id"], instance_packet)

    def test_sketch_external_reference_scope_can_pick_other_instance(self):
        part = create_empty_part()
        assembly = create_empty_assembly()
        other_mesh = ViewerMesh(
            triangle_positions=(), triangle_normals=(),
            triangle_face_indices=(), triangle_owner_ids=(),
            edges=(EdgePolyline(
                4, ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
                owner_id="other-instance",
            ),),
            points=(), planes=(), bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(1.0, 0.0, 0.0),
        )
        context = ViewerDocumentContext(
            display_document=assembly,
            editing_document=part,
            active_instance_id="active-instance",
            editing_history_boundary=0,
            interaction_scope=(
                ViewerInteractionScope.SKETCH_EXTERNAL_REFERENCES
            ),
        )
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._viewer_document_context = context
        window._dimension_inspection_visuals = False
        window.assembly_component_dialog = None
        window._selection_controller = SelectionController()
        window.point_constraint_dialog = None
        window.orientation_dialog = None
        window._sketch_reference_mode = True
        window.native_viewer = SimpleNamespace(
            _pick_axis=lambda _position: None,
            edge_at_mesh=lambda mesh, _position: (
                ("other-instance", 4) if mesh is other_mesh else None
            ),
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(source_bodies={
                "other-instance": SimpleNamespace(mesh=other_mesh),
            })
        )

        candidate = window._viewer_pick_candidate(QPointF(1.0, 1.0))

        self.assertEqual(candidate.owner_id, "other-instance")
        self.assertEqual(candidate.element_index, 4)

    def test_active_part_container_pick_never_falls_back_to_assembly_mesh(self):
        part = create_empty_part()
        container = part.create_container("Box", ContainerType.BOX)
        part.create_primitive(container.entity_id, EntityKind.BOX)
        part_mesh = object()
        assembly_mesh = object()
        context = ViewerDocumentContext(
            display_document=create_empty_assembly(),
            editing_document=part,
            active_instance_id="second-instance",
            editing_history_boundary=1,
            interaction_scope=ViewerInteractionScope.ACTIVE_PART_CONTAINERS,
        )
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._viewer_document_context = context
        window._viewer_interaction_body_result = SimpleNamespace(
            mesh=part_mesh,
            source_bodies={
                container.entity_id: SimpleNamespace(mesh=part_mesh),
            },
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(
                mesh=assembly_mesh,
                source_bodies={},
            )
        )
        window._definition_history_boundary = lambda: 1
        window.native_viewer = SimpleNamespace(
            mesh_is_under_cursor=lambda mesh, _position: mesh is part_mesh
        )

        selected, mesh = window._part_history_container_at_position(
            QPointF(1.0, 1.0)
        )

        self.assertIs(selected, container)
        self.assertIs(mesh, part_mesh)

    def test_active_part_element_uses_selected_assembly_instance_transform(self):
        part = create_empty_part()
        owner = part.create_container("Feature", ContainerType.EMPTY)
        owner.coordinate_system.origin = (5.0, 0.0, 0.0)
        assembly = create_empty_assembly()
        first = assembly.create_container("First", ContainerType.COMPONENT)
        second = assembly.create_container("Second", ContainerType.COMPONENT)
        first.coordinate_system.origin = (100.0, 0.0, 0.0)
        second.coordinate_system.origin = (0.0, 50.0, 0.0)
        second.coordinate_system.rotation = (0.0, 0.0, 90.0)
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._active_component_return_document = assembly
        window._active_component_entity_id = second.entity_id

        transform = window._world_transform_for_object(owner)

        actual = tuple(transform_point(transform, (0.0, 0.0, 0.0)))
        self.assertAlmostEqual(actual[0], 0.0)
        self.assertAlmostEqual(actual[1], 55.0)
        self.assertAlmostEqual(actual[2], 0.0)
        self.assertEqual(window._native_object_origin(owner), actual)

    def test_active_instance_sketch_frame_stays_in_assembly_coordinates(self):
        part = create_empty_part()
        owner = part.create_container("Feature", ContainerType.SKETCH)
        owner.coordinate_system.origin = (5.0, 0.0, 0.0)
        sketch = part.create_sketch(owner.entity_id)
        sketch.parameters["plane"] = "xy"
        assembly = create_empty_assembly()
        instance = assembly.create_container(
            "Second 01", ContainerType.COMPONENT
        )
        instance.coordinate_system.origin = (10.0, 20.0, 30.0)
        instance.coordinate_system.rotation = (0.0, 0.0, 90.0)
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._active_component_return_document = assembly
        window._active_component_entity_id = instance.entity_id
        window._active_component_world_transform = (
            coordinate_system_transform(instance.coordinate_system)
        )
        # Rollback/recalculation may touch the live Assembly graph.  The
        # active Sketcher frame must retain the solved transform captured for
        # the exact selected instance, regardless of chain length.
        instance.coordinate_system = CoordinateSystem()

        origin, x_axis, y_axis = window._sketch_frame(sketch)

        for actual, expected in zip(origin, (10.0, 25.0, 30.0)):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(x_axis, (0.0, 1.0, 0.0)):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(y_axis, (-1.0, 0.0, 0.0)):
            self.assertAlmostEqual(actual, expected)

    def test_active_instance_does_not_enter_part_constraint_solution(self):
        part = create_empty_part()
        origin = next(
            entity
            for entity in part.root.children
            if entity.kind == EntityKind.ORIGIN
        )
        xy_plane = next(
            entity
            for entity in origin.children
            if entity.kind == EntityKind.PLANE
            and entity.parameters.get("plane") == "xy"
        )
        assembly = create_empty_assembly()
        instance = assembly.create_container(
            "Placed part", ContainerType.COMPONENT
        )
        instance.coordinate_system.origin = (0.0, 0.0, 90.0)
        instance.coordinate_system.rotation = (0.0, 0.0, 75.0)
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._active_component_return_document = assembly
        window._active_component_entity_id = instance.entity_id
        window._active_component_world_transform = (
            coordinate_system_transform(instance.coordinate_system)
        )

        solution, _dof, _status, _constrained = (
            window._solve_point_constraints(
                [{"type": "entity", "entity_id": xy_plane.entity_id}],
                (5.0, 6.0, 7.0),
            )
        )

        self.assertEqual(solution, (5.0, 6.0, 0.0))
        self.assertEqual(
            window._reference_origin(xy_plane),
            (0.0, 0.0, 90.0),
        )

    def test_assembly_cut_tool_is_an_object_pick_candidate(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Part", ContainerType.COMPONENT
        )
        cut = assembly.create_container(
            "Cut", ContainerType.PROTRUSION
        )
        cut_mesh = ViewerMesh(
            triangle_positions=(
                0.0, 0.0, 0.0,
                1.0, 0.0, 0.0,
                0.0, 1.0, 0.0,
            ),
            triangle_normals=(0.0, 0.0, 1.0) * 3,
            triangle_face_indices=(1,),
            triangle_owner_ids=(cut.entity_id,),
            edges=(), points=(), planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(1.0, 1.0, 0.0),
        )
        result = BodyResult.from_mesh(cut_mesh)
        calculated = BodyResult(
            mesh=cut_mesh,
            source_bodies={
                component.entity_id: result.with_owner(
                    component.entity_id
                ),
                cut.entity_id: result,
            },
        )
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window.assembly_component_dialog = None
        window.point_constraint_dialog = None
        window._dimension_inspection_visuals = False
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=calculated
        )
        window._viewer_document_context = ViewerDocumentContext(
            display_document=assembly,
            editing_document=assembly,
            active_instance_id=None,
            editing_history_boundary=2,
            interaction_scope=ViewerInteractionScope.ASSEMBLY_INSTANCES,
        )
        window._selection_controller = SelectionController()
        window.orientation_dialog = None
        window._sketch_reference_mode = False
        window.native_viewer = SimpleNamespace(
            mesh_is_under_cursor=lambda mesh, _position: mesh is cut_mesh,
            _pick_object=lambda _position: None,
        )

        candidate = window._viewer_pick_candidate(QPointF(2.0, 2.0))

        self.assertIsNotNone(candidate)
        self.assertEqual(candidate.kind, "object")
        self.assertEqual(candidate.owner_id, cut.entity_id)
        candidates = window._assembly_object_pick_candidates(
            QPointF(2.0, 2.0)
        )
        self.assertEqual(
            [item.owner_id for item in candidates],
            [cut.entity_id, component.entity_id],
        )
        self.assertIs(candidate.hover_mesh, cut_mesh)

    def test_active_part_selection_scope_excludes_other_instances(self):
        assembly = create_empty_assembly()
        first = assembly.create_container("First", ContainerType.COMPONENT)
        second = assembly.create_container("Second", ContainerType.COMPONENT)
        second_origin = next(
            child for child in second.children
            if child.kind == EntityKind.ORIGIN
        )
        generated_owner = f"{second.entity_id}:generated-axis"
        scene = SimpleNamespace(shapes_by_owner_id={
            first.entity_id: object(),
            second.entity_id: object(),
            generated_owner: object(),
        })

        allowed, excluded = MainWindow._active_component_selection_scope(
            assembly, second.entity_id, scene
        )

        self.assertIn(second.entity_id, allowed)
        self.assertIn(second_origin.entity_id, allowed)
        self.assertIn(generated_owner, allowed)
        self.assertNotIn(first.entity_id, allowed)
        self.assertEqual(excluded, {first.entity_id})

    def test_component_activation_distinguishes_part_and_subassembly(self):
        part = create_empty_assembly().create_container(
            "Part", ContainerType.COMPONENT
        )
        part.parameters["source_document_type"] = "part"
        subassembly = create_empty_assembly().create_container(
            "Subassembly", ContainerType.COMPONENT
        )
        subassembly.parameters["source_document_type"] = "assembly"

        self.assertFalse(MainWindow._component_source_is_assembly(part))
        self.assertTrue(MainWindow._component_source_is_assembly(subassembly))

    def test_assembly_tree_distinguishes_part_and_subassembly_icons(self):
        application = QApplication.instance() or QApplication([])
        window = MainWindow.__new__(MainWindow)
        window._active_component_entity_id = None
        assembly = create_empty_assembly()
        part = assembly.create_container("Part", ContainerType.COMPONENT)
        part.parameters.update({
            "source_path": "part.prtz",
            "source_document_type": "part",
        })
        subassembly = assembly.create_container(
            "Subassembly", ContainerType.COMPONENT
        )
        subassembly.parameters.update({
            "source_path": "subassembly.asmz",
            "source_document_type": "assembly",
        })

        part_item = window._create_referenced_part_tree_item(
            part, "root", source_document=assembly
        )
        subassembly_item = window._create_referenced_part_tree_item(
            subassembly, "root", source_document=assembly
        )

        self.assertEqual(
            part_item.icon(0).cacheKey(), resource_icon("part").cacheKey()
        )
        self.assertEqual(
            subassembly_item.icon(0).cacheKey(),
            resource_icon("assembly").cacheKey(),
        )
        self.assertIsNotNone(application)

    def test_active_subassembly_exposes_only_assembly_application(self):
        window = MainWindow.__new__(MainWindow)
        window.document = create_empty_assembly()
        window._active_component_return_document = create_empty_assembly()

        self.assertTrue(
            window._application_mode_available(ApplicationMode.ASSEMBLY)
        )
        self.assertFalse(
            window._application_mode_available(ApplicationMode.MODELING)
        )

    def test_feature_overlay_matches_only_the_activated_instance(self):
        coincident_edge = ((0.0, 0.0, 0.0), (10.0, 0.0, 0.0))
        main = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(
                EdgePolyline(1, coincident_edge, owner_id="first"),
                EdgePolyline(1, coincident_edge, owner_id="second"),
            ),
            points=(),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(10.0, 0.0, 0.0),
        )
        overlay = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(EdgePolyline(
                1, coincident_edge, owner_id="second:protrusion"
            ),),
            points=(),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(10.0, 0.0, 0.0),
        )
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = main
        viewer._object_overlay_mesh = overlay
        viewer._object_overlay_match_owner_id = "second"

        ZimaOpenGLViewer._refresh_object_overlay_edge_matches(viewer)

        self.assertEqual(
            viewer._object_overlay_main_edge_keys,
            frozenset({("second", 1)}),
        )

    def test_assembly_mate_flip_reverses_target_normal(self):
        normal = (0.0, 0.0, 1.0)

        self.assertEqual(
            tuple(AssemblyComponentPropertiesDialog._oriented_target_normal(
                normal, False
            )),
            normal,
        )
        self.assertEqual(
            tuple(AssemblyComponentPropertiesDialog._oriented_target_normal(
                normal, True
            )),
            (0.0, 0.0, -1.0),
        )

    def test_assembly_mate_flip_does_not_reverse_signed_offset_direction(self):
        normal = (0.0, 0.0, 1.0)

        unflipped_orientation, unflipped_position = (
            AssemblyComponentPropertiesDialog._mate_target_normals(
                normal, False
            )
        )
        flipped_orientation, flipped_position = (
            AssemblyComponentPropertiesDialog._mate_target_normals(
                normal, True
            )
        )

        self.assertEqual(tuple(unflipped_orientation), normal)
        self.assertEqual(tuple(flipped_orientation), (0.0, 0.0, -1.0))
        self.assertEqual(tuple(unflipped_position), normal)
        self.assertEqual(tuple(flipped_position), normal)

    def test_plane_flip_controls_side_when_angle_is_between_axis_and_plane(self):
        rows = AssemblyComponentPropertiesDialog._with_orientation_roles([
            {"type": "axis"},
            {"type": "angle"},
            {"type": "plane", "flip": True},
        ])

        self.assertEqual(
            [row["orientation"] for row in rows],
            [True, False, True],
        )
        self.assertEqual(
            AssemblyComponentPropertiesDialog._orientation_mate_indices(rows),
            [2, 0],
        )

    def test_angle_flip_reverses_angle_direction_without_adding_180_degrees(self):
        self.assertAlmostEqual(
            AssemblyComponentPropertiesDialog._signed_mate_angle(11.0, False),
            radians(11.0),
        )
        self.assertAlmostEqual(
            AssemblyComponentPropertiesDialog._signed_mate_angle(11.0, True),
            radians(-11.0),
        )

    def test_three_independent_mate_planes_enable_flip_parity(self):
        self.assertTrue(
            AssemblyComponentPropertiesDialog._three_directions_are_independent(
                ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
            )
        )
        self.assertFalse(
            AssemblyComponentPropertiesDialog._three_directions_are_independent(
                ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (2.0, 0.0, 0.0))
            )
        )

    def test_derived_flip_keeps_only_even_flip_combinations(self):
        for first, second in ((False, False), (True, False), (False, True), (True, True)):
            values = {0: first, 1: second, 2: False}
            values[2] = AssemblyComponentPropertiesDialog._even_flip_value(
                values, 2
            )
            self.assertEqual(sum(values.values()) % 2, 0)

    def test_assembly_object_hover_uses_complete_persisted_component_mesh(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component", ContainerType.COMPONENT
        )
        complete_mesh = object()
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window._dimension_inspection_visuals = False
        window.assembly_component_dialog = None
        window.native_viewer = SimpleNamespace(
            _pick_object=lambda _position: component.entity_id
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(
                source_bodies={
                    component.entity_id: SimpleNamespace(mesh=complete_mesh)
                }
            )
        )
        window._native_object_origin = lambda _component: (1.0, 2.0, 3.0)

        candidate = window._viewer_pick_candidate(QPointF(10.0, 20.0))

        self.assertEqual(candidate.owner_id, component.entity_id)
        self.assertIs(candidate.hover_mesh, complete_mesh)
        self.assertEqual(candidate.anchor, (1.0, 2.0, 3.0))

    def test_assembly_reference_faces_override_dimension_inspection_gate(self):
        assembly = create_empty_assembly()
        candidate = ViewerPickCandidate("face", "component", 7, object())
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window._dimension_inspection_visuals = True
        window.assembly_component_dialog = SimpleNamespace(
            isVisible=lambda: True,
            selection_paused=False,
        )
        window._assembly_viewer_pick_candidates = (
            lambda _dialog, _position: ((candidate, None),)
        )

        selected = window._viewer_pick_candidate(QPointF(10.0, 20.0))

        self.assertIs(selected, candidate)

    def test_assembly_object_hover_always_falls_back_to_all_component_edges(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component", ContainerType.COMPONENT
        )
        complete_edges = (
            EdgePolyline(1, ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
                         owner_id=component.entity_id),
            EdgePolyline(2, ((0.0, 0.0, 1.0), (1.0, 0.0, 1.0)),
                         owner_id=component.entity_id),
        )
        assembly_mesh = ViewerMesh(
            triangle_positions=(), triangle_normals=(),
            triangle_face_indices=(), triangle_owner_ids=(),
            edges=complete_edges, points=(), planes=(),
            bounds_min=(0.0, 0.0, 0.0), bounds_max=(1.0, 0.0, 1.0),
        )
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window._dimension_inspection_visuals = False
        window.assembly_component_dialog = None
        window.native_viewer = SimpleNamespace(
            _pick_object=lambda _position: component.entity_id
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(
                source_bodies={}, mesh=assembly_mesh
            )
        )
        window._native_object_origin = lambda _component: None

        candidate = window._viewer_pick_candidate(QPointF(10.0, 20.0))

        self.assertEqual(candidate.hover_mesh.edges, complete_edges)

    def test_assembly_pick_mesh_follows_changed_component_transform(self):
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component", ContainerType.COMPONENT
        )
        mesh = ViewerMesh(
            triangle_positions=(0.0, 0.0, 0.0) * 3,
            triangle_normals=(0.0, 0.0, 1.0) * 3,
            triangle_face_indices=(1,),
            triangle_owner_ids=("source-solid",),
            edges=(), points=(), planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(0.0, 0.0, 0.0),
        )
        source_result = BodyResult.from_mesh(mesh)
        dialog = SimpleNamespace(_original_component_results={
            component.entity_id: [(source_result, mesh, None)]
        })
        component.coordinate_system.origin = (25.0, 0.0, 0.0)
        window = MainWindow.__new__(MainWindow)
        window.document = assembly

        window._refresh_assembly_original_component_results(dialog)

        transformed = dialog._original_component_results[
            component.entity_id
        ][0][1]
        self.assertEqual(transformed.bounds_min[0], 25.0)
        self.assertEqual(transformed.bounds_max[0], 25.0)

    def test_assembly_placement_invalidates_only_assembly_display_cache(self):
        assembly = create_empty_assembly()
        assembly._shape_history_cache["old"] = object()
        assembly._body_result_cache["old"] = object()
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window._native_viewer_scene = object()

        window._invalidate_assembly_display_cache()

        self.assertEqual(assembly._shape_history_cache, {})
        self.assertEqual(assembly._body_result_cache, {})
        self.assertIsNone(window._native_viewer_scene)

    def test_active_part_regeneration_invalidates_display_assembly_not_part(self):
        part = create_empty_part()
        assembly = create_empty_assembly()
        part._body_result_cache["fresh-part"] = object()
        assembly._shape_history_cache["stale-assembly"] = object()
        assembly._body_result_cache["stale-assembly"] = object()
        window = MainWindow.__new__(MainWindow)
        window.document = part
        window._native_viewer_scene = object()

        window._invalidate_assembly_display_cache(assembly)

        self.assertIn("fresh-part", part._body_result_cache)
        self.assertEqual(assembly._shape_history_cache, {})
        self.assertEqual(assembly._body_result_cache, {})
        self.assertIsNone(window._native_viewer_scene)

    def test_assembly_source_face_precedes_part_self_reference_guard(self):
        window = MainWindow.__new__(MainWindow)
        window._dimension_inspection_visuals = False
        window.document = SimpleNamespace(root=SimpleNamespace(
            entity_id="assembly-root"
        ))
        accepted = []
        window._accept_assembly_face_reference = (
            lambda owner_id, face_index: accepted.append(
                (owner_id, face_index)
            ) or True
        )
        window._current_definition_owns_reference = lambda _owner_id: True

        window._on_native_face_selected("source-component", 7)

        self.assertEqual(accepted, [("source-component", 7)])

    def test_provided_object_click_keeps_source_mesh_for_cyan_confirmation(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._provided_hover_candidate = None
        viewer._pending_model_hover_position = QPointF(1.0, 1.0)
        stopped = []
        viewer._model_hover_timer = SimpleNamespace(
            stop=lambda: stopped.append("apply")
        )
        viewer._model_hover_clear_timer = SimpleNamespace(
            stop=lambda: stopped.append("clear")
        )
        selected = []
        viewer._set_selected_object = selected.append
        overlays = []
        viewer.set_object_overlay = lambda *args, **kwargs: overlays.append(
            (args, kwargs)
        )
        source_mesh = object()
        candidate = ViewerPickCandidate(
            "object", "source-container", 0, source_mesh
        )

        viewer._select_provided_candidate(candidate)

        self.assertIsNone(viewer._provided_hover_candidate)
        self.assertEqual(selected, ["source-container"])
        self.assertEqual(stopped, ["apply", "clear"])
        self.assertIsNone(viewer._pending_model_hover_position)
        self.assertEqual(overlays, [(
            (source_mesh,),
            {
                "selected": True,
                "anchor": None,
                "match_owner_id": "source-container",
            },
        )])

    def test_object_cycle_preview_never_enters_topology_click_dispatch(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._cycled_topology_candidate = ("face", "old", 1)
        viewer.set_feature_hover_edges = lambda _edges: None
        previewed = []
        viewer._apply_provided_hover = previewed.append
        candidate = ViewerPickCandidate(
            "object", "assembly-cut", 0, object()
        )

        viewer.preview_provided_candidate(candidate)

        self.assertIsNone(viewer._cycled_topology_candidate)
        self.assertEqual(previewed, [candidate])

    def test_object_click_confirms_visible_hover_without_repicking_overlap(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        hovered = ViewerPickCandidate("object", "orange-container")
        viewer._provided_hover_candidate = hovered
        viewer._last_model_hover_position = QPointF(100.0, 100.0)
        repicks = []
        viewer._provided_candidate_at = lambda position: repicks.append(
            position
        ) or ViewerPickCandidate("object", "different-container")

        selected = viewer._provided_candidate_for_click(
            QPointF(102.0, 101.0)
        )

        self.assertIs(selected, hovered)
        self.assertEqual(repicks, [])

    def test_late_object_hover_does_not_downgrade_cyan_selection(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._provided_hover_candidate = None
        viewer._object_overlay_persistent = True
        viewer._clear_topology_hover = lambda: None
        viewer.set_source_topology_hover = lambda *_args: None
        overlay_calls = []
        viewer.set_object_overlay = lambda *args, **kwargs: overlay_calls.append(
            (args, kwargs)
        )
        viewer._set_hovered_object = lambda _owner_id: None

        viewer._apply_provided_hover(ViewerPickCandidate(
            "object", "selected-container", 0, object()
        ))

        self.assertEqual(overlay_calls, [])

    def test_confirmed_object_disables_common_hover_provider(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._object_overlay_locks_interaction = True
        calls = []
        viewer._pick_candidate_provider = lambda position: calls.append(
            position
        )

        candidate = viewer._provided_candidate_at(QPointF(10.0, 20.0))

        self.assertIsNone(candidate)
        self.assertEqual(calls, [])

    def test_coincident_is_classified_as_selection_not_placement(self):
        self.assertIn("coincident", SKETCH_ENTITY_SELECTION_TOOLS)
        self.assertIn("coincident", SKETCH_CONSTRAINT_SELECTION_TOOLS)

    def test_history_surface_refresh_updates_nested_orientation_reference(self):
        document = create_empty_part()
        window = MainWindow.__new__(MainWindow)
        window.document = document
        stable_id = "stable-face"
        nested = {
            "type": "face",
            "reference_scope": "history_result",
            "surface_reference_id": stable_id,
            "entity_id": "stale-body",
            "topology_key": "99",
            "key": "face:stale-body:99",
            "equations": [[1.0, 0.0, 0.0, 1.0]],
        }
        references = [{
            "type": "container_orientation",
            "mappings": [{"slot": "primary", "reference": nested}],
        }]
        empty_mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(),
            points=(),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(0.0, 0.0, 0.0),
        )
        body = BodyResult(
            mesh=empty_mesh,
            faces={
                f"{document.root.entity_id}:face:4": SurfaceDescriptor(
                    reference_id=stable_id,
                    kind="plane",
                    origin=(0.0, 0.0, 7.0),
                    normal=(0.0, 0.0, 1.0),
                )
            },
        )

        changed = window._refresh_history_result_surface_references(
            references,
            body,
        )

        self.assertTrue(changed)
        self.assertEqual(nested["entity_id"], document.root.entity_id)
        self.assertEqual(nested["topology_key"], "4")
        self.assertEqual(nested["equations"], [[0.0, 0.0, 1.0, 7.0]])

    def test_rebinding_saved_body_preserves_historical_source_bodies(self):
        empty_mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(),
            points=(),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(0.0, 0.0, 0.0),
        )
        source = BodyResult.from_mesh(empty_mesh)
        saved = BodyResult.from_mesh(empty_mesh)
        saved = BodyResult(
            mesh=saved.mesh,
            faces=saved.faces,
            edges=saved.edges,
            vertices=saved.vertices,
            physical=saved.physical,
            source_bodies={"container-1": source},
        )

        rebound = saved.with_owner("runtime-part")

        self.assertIs(
            rebound.source_bodies["container-1"],
            source,
        )

    def test_loaded_point_vertical_relation_is_reapplied(self):
        entities = [
            {
                "id": "driven",
                "type": "point",
                "x": 12.5,
                "y": 20.0,
                "constraints": [{
                    "type": "vertical",
                    "point_id": "reference",
                    "relation_role": "driven",
                }],
            },
            {
                "id": "reference",
                "type": "point",
                "x": 10.0,
                "y": 0.0,
            },
        ]

        MainWindow._apply_sketch_coincident_constraints(entities)

        self.assertEqual(entities[0]["x"], 10.0)
        self.assertEqual(entities[0]["y"], 20.0)

    def test_dimension_origin_axis_does_not_need_construction_proxy(self):
        window = MainWindow.__new__(MainWindow)
        sketch = SimpleNamespace(entity_id="sketch")
        window.document = SimpleNamespace(
            find_entity=lambda entity_id: sketch if entity_id == "sketch" else None
        )
        window._sketch_edit_entity_id = "sketch"
        stored_reference = {
            "id": "origin-axis-from-view",
            "source_kind": "axis",
            "owner_id": "origin",
        }
        window._stored_sketch_external_references = lambda _sketch: [
            stored_reference
        ]
        window._external_reference_source = lambda _reference: SimpleNamespace(
            kind=EntityKind.ORIGIN,
            locked=True,
        )
        window._resolved_sketch_external_references = lambda _sketch: [{
            "id": "origin-axis-from-view",
            "geometry": {
                "type": "line",
                "point": [0.0, 0.0],
                "direction": [3.0, 0.0],
            },
        }]

        reference = window._canonical_dimension_origin_axis(
            "origin-axis-from-view"
        )

        self.assertEqual(reference, "sketch_axis:x")

    def test_navigation_paints_datum_planes_continuously(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._navigation_active = True
        viewer._sketch_frame = None
        viewer._object_overlay_persistent = False
        painted = []
        viewer._paint_screen_constant_edges = lambda: painted.append("edges")
        viewer._paint_planes = lambda: painted.append("planes")
        viewer._paint_reference_highlights = lambda: None
        viewer._paint_dimensions = lambda: None
        viewer._paint_passive_sketch_overlay = lambda: None
        viewer._paint_source_topology_hover = lambda: None
        viewer._paint_edge_labels = lambda **_kwargs: None

        viewer._paint_navigation_overlays()

        self.assertEqual(painted, ["edges", "planes"])

    def test_source_topology_pick_uses_persisted_viewer_mesh_only(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        mesh = SimpleNamespace(marker="persisted")
        window.document = SimpleNamespace(
            history_objects_at=lambda _boundary: [source],
        )
        window._definition_history_boundary = lambda: 1
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(
                source_bodies={
                    source.entity_id: SimpleNamespace(mesh=mesh),
                }
            )
        )
        window.native_viewer = SimpleNamespace(
            faces_at_mesh=lambda candidate, _position:
                ((1.0, source.entity_id, 7),) if candidate is mesh else (),
            edge_at_mesh=lambda _candidate, _position: None,
            point_at_mesh=lambda _candidate, _position: None,
        )

        self.assertEqual(
            window._source_topology_reference_at_position(
                "face", QPointF(10.0, 20.0)
            ),
            (source.entity_id, 7, mesh),
        )

    def test_standalone_sketch_reference_reuses_final_source_packets(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        downstream = SimpleNamespace(entity_id="downstream")
        source_mesh = SimpleNamespace(marker="persisted-source")
        final_result = SimpleNamespace(source_bodies={
            source.entity_id: SimpleNamespace(mesh=source_mesh),
            downstream.entity_id: SimpleNamespace(mesh=SimpleNamespace()),
        })
        window.document = SimpleNamespace(
            history_objects_at=lambda _boundary: [source],
            history_objects=lambda: [source, downstream],
            cached_body_result_at=lambda _objects: final_result,
        )
        window._definition_history_boundary = lambda: 1
        window._viewer_document_context = SimpleNamespace(
            active_instance_id=None,
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(source_bodies={})
        )
        window.native_viewer = SimpleNamespace(
            faces_at_mesh=lambda candidate, _position:
                ((1.0, source.entity_id, 5),)
                if candidate is source_mesh else (),
            edge_at_mesh=lambda _candidate, _position: None,
            point_at_mesh=lambda _candidate, _position: None,
        )

        self.assertEqual(
            window._source_topology_reference_at_position(
                "face", QPointF(12.0, 24.0)
            ),
            (source.entity_id, 5, source_mesh),
        )

    def test_standalone_sketch_reference_lists_occluded_source_faces(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        source_mesh = SimpleNamespace(marker="persisted-source")
        final_result = SimpleNamespace(source_bodies={
            source.entity_id: SimpleNamespace(mesh=source_mesh),
        })
        window.document = SimpleNamespace(
            history_objects_at=lambda _boundary: [source],
            history_objects=lambda: [source],
            cached_body_result_at=lambda _objects: final_result,
        )
        window._definition_history_boundary = lambda: 1
        window._viewer_document_context = SimpleNamespace(
            active_instance_id=None,
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(source_bodies={})
        )
        window.native_viewer = SimpleNamespace(
            faces_at_mesh=lambda candidate, _position: (
                (2.0, source.entity_id, 5),
                (1.0, source.entity_id, 2),
            ) if candidate is source_mesh else (),
            edge_at_mesh=lambda _candidate, _position: None,
            point_at_mesh=lambda _candidate, _position: None,
        )

        self.assertEqual(
            window._source_topology_candidates_at_position(
                "face", QPointF(12.0, 24.0)
            ),
            (
                (source.entity_id, 5, source_mesh),
                (source.entity_id, 2, source_mesh),
            ),
        )

    def test_standalone_sketch_projection_reads_final_source_packet(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        source_result = SimpleNamespace(marker="persisted-source")
        final_result = SimpleNamespace(source_bodies={
            source.entity_id: source_result,
        })
        window.document = SimpleNamespace(
            history_objects=lambda: [source],
            # The first accepted reference marks the document dirty, after
            # which the normal validated-cache accessor intentionally refuses
            # the old final packet for calculation purposes.
            cached_body_result_at=lambda _objects: None,
        )
        window._viewer_document_context = SimpleNamespace(
            active_instance_id=None,
        )
        window._viewer_interaction_body_result = None
        window._sketch_reference_body_result = final_result
        window._native_viewer_scene = SimpleNamespace(
            source_body_result=lambda _owner_id: None,
        )

        self.assertIs(
            window._persisted_source_body_result(source.entity_id),
            source_result,
        )

    def test_standalone_sketch_offers_persisted_vertex_without_marker(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        source_mesh = SimpleNamespace(points=())
        source_result = SimpleNamespace(
            mesh=source_mesh,
            vertices={
                "source-1:point:7": SimpleNamespace(
                    position=(1.0, 2.0, 3.0)
                ),
            },
        )
        final_result = SimpleNamespace(source_bodies={
            source.entity_id: source_result,
        })
        window.document = SimpleNamespace(
            history_objects_at=lambda _boundary: [source],
            history_objects=lambda: [source],
            cached_body_result_at=lambda _objects: final_result,
        )
        window._definition_history_boundary = lambda: 1
        window._viewer_document_context = SimpleNamespace(
            active_instance_id=None,
        )
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(source_bodies={})
        )
        window.native_viewer = SimpleNamespace(
            devicePixelRatioF=lambda: 1.0,
            world_to_screen=lambda _point: QPointF(10.0, 20.0),
            edge_at_mesh=lambda _mesh, _position: None,
            point_at_mesh=lambda _mesh, _position: None,
        )

        self.assertEqual(
            window._source_topology_candidates_at_position(
                "point", QPointF(12.0, 22.0)
            ),
            ((source.entity_id, 7, source_mesh),),
        )

    def test_viewer_mesh_extracts_persisted_edge_and_point(self):
        mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(EdgePolyline(
                edge_index=3,
                points=((1.0, 2.0, 3.0), (4.0, 5.0, 6.0)),
                owner_id="source-1",
            ),),
            points=(PointMarker(
                point_index=4,
                position=(7.0, 8.0, 9.0),
                owner_id="source-1",
                element_kind="vertex",
            ),),
            planes=(),
            bounds_min=(1.0, 2.0, 3.0),
            bounds_max=(7.0, 8.0, 9.0),
        )

        self.assertEqual(len(mesh.edge_mesh("source-1", 3).edges), 1)
        self.assertEqual(len(mesh.point_mesh("source-1", 4).points), 1)

    def test_external_segment_uses_original_topology_reference_picking(self):
        window = MainWindow.__new__(MainWindow)
        window._sketch_reference_mode = False
        window.point_constraint_dialog = None
        window._selection_controller = SimpleNamespace(
            request=SimpleNamespace(command_id="sketch_external_segment")
        )

        self.assertTrue(window._original_topology_reference_pick_active())

    def test_reverse_one_side_dimension_uses_the_dialog_forward_parameter(
        self,
    ) -> None:
        feature = SimpleNamespace(parameters={
            "extent_mode": "one_side",
            "direction": "reverse",
        })

        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "length_reverse",
            ),
            "length_forward",
        )
        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "angle_reverse",
            ),
            "angle",
        )

    def test_two_side_dimension_keeps_its_own_dialog_parameter(self) -> None:
        feature = SimpleNamespace(parameters={"extent_mode": "two_sides"})

        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "length_reverse",
            ),
            "length_reverse",
        )

    def test_return_from_sketch_drops_hidden_properties_dialog_before_reopen(
        self,
    ) -> None:
        document = create_empty_part()
        protrusion = document.create_container(
            "Protrusion001",
            ContainerType.PROTRUSION,
        )
        reopened = []
        selected = []
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_return_properties_id = protrusion.entity_id
        window.point_constraint_dialog = SimpleNamespace(
            isVisible=lambda: False,
        )
        window._select_tree_object_without_reference_event = selected.append
        window._edit_protrusion = lambda target, rebuild_rollback: reopened.append(
            (target, rebuild_rollback)
        )

        with patch("zima_cad.app.QTimer.singleShot"):
            window._reopen_sketch_properties("unused-sketch")

        self.assertEqual(selected, [protrusion.entity_id])
        self.assertEqual(reopened, [(protrusion, False)])

    def test_properties_dialog_consumes_face_when_general_selection_is_disabled(
        self,
    ) -> None:
        owner = SimpleNamespace(entity_id="Protrusion001")
        root = SimpleNamespace(entity_id="result-body")
        selected: list[tuple[object, object, int | None]] = []

        class VisibleDialog:
            @staticmethod
            def isVisible():
                return True

        class Shape:
            @staticmethod
            def ShapeType():
                return TopAbs_FACE

        class WindowState:
            document = SimpleNamespace(
                root=root,
                find_entity=lambda _owner_id: owner,
            )
            point_constraint_dialog = VisibleDialog()
            view_selection_enabled = False
            _sketch_reference_mode = False
            _active_component_entity_id = None
            _active_component_document = None
            view_selection_mode = ViewSelectionMode.CONTAINER

            @staticmethod
            def _try_pick_protrusion_profile(_obj):
                return False

            @staticmethod
            def _add_point_shape_constraint(
                obj,
                shape,
                *,
                topology_index=None,
            ):
                selected.append((obj, shape, topology_index))

            @staticmethod
            def _select_native_tree_object(_owner_id):
                return None

        shape = Shape()
        MainWindow._apply_native_view_selection(
            WindowState(),
            owner.entity_id,
            shape,
            topology_index=17,
        )
        self.assertEqual(selected, [(owner, shape, 17)])

    def test_non_displayed_history_mesh_picks_edges_and_vertices(self) -> None:
        mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(EdgePolyline(
                edge_index=7,
                points=((0.0, 0.0, 2.0), (10.0, 0.0, 2.0)),
                owner_id="Protrusion001",
            ),),
            points=(PointMarker(
                point_index=3,
                position=(0.0, 0.0, 2.0),
                owner_id="Protrusion001",
                element_kind="vertex",
            ),),
            planes=(),
            bounds_min=(0.0, 0.0, 2.0),
            bounds_max=(10.0, 0.0, 2.0),
        )

        class ViewerState:
            @staticmethod
            def devicePixelRatioF():
                return 1.0

            @staticmethod
            def _camera_point(point):
                return point

            @staticmethod
            def _screen_point(point):
                return QPointF(point[0], point[1])

            @staticmethod
            def _display_edge_points(edge):
                return edge.points

            _point_segment_distance = staticmethod(
                ZimaOpenGLViewer._point_segment_distance
            )

        viewer = ViewerState()
        self.assertEqual(
            ZimaOpenGLViewer.edge_at_mesh(viewer, mesh, QPointF(5.0, 0.0)),
            ("Protrusion001", 7),
        )
        self.assertEqual(
            ZimaOpenGLViewer.point_at_mesh(viewer, mesh, QPointF(0.0, 0.0)),
            ("Protrusion001", 3),
        )

    def test_container_frame_slots_expand_into_position_and_orientation(self):
        primary = {"type": "face", "key": "face", "label": "Face"}
        secondary = {"type": "edge", "key": "edge", "label": "Edge"}
        expanded = MainWindow._expanded_container_frame_references([{
            "type": "container_orientation",
            "work_plane_offset": 12.5,
            "mappings": [
                {
                    "slot": "primary",
                    "reference": primary,
                    "role": "back",
                },
                {
                    "slot": "secondary",
                    "reference": secondary,
                    "role": "right",
                },
            ],
        }])

        self.assertEqual(expanded[0]["orientation_role"], "opposite_normal")
        self.assertEqual(expanded[0]["position_role"], "orientation_only")
        self.assertEqual(expanded[1]["orientation_role"], "right")
        self.assertEqual(expanded[1]["position_role"], "orientation_only")
        self.assertEqual(MainWindow._container_plane_offset([{
            "type": "container_orientation",
            "work_plane_offset": 12.5,
        }]), 12.5)

    def test_container_frame_marks_slots_without_copying_position_references(self):
        position = {
            "type": "entity",
            "key": "position",
            "orientation_role": "up",
            "orientation_drives_rotation": True,
        }
        front = {"type": "face", "key": "front"}
        expanded = MainWindow._expanded_container_frame_references([
            position,
            {
                "type": "container_orientation",
                "mappings": [{
                    "slot": "primary",
                    "reference": front,
                    "role": "front",
                }],
            },
        ])

        self.assertEqual(expanded[0]["orientation_role"], "up")
        self.assertTrue(expanded[0]["orientation_drives_rotation"])
        self.assertEqual(
            expanded[1]["container_orientation_slot"], "primary"
        )
        self.assertEqual(expanded[1]["orientation_role"], "normal")
        self.assertTrue(expanded[1]["preserve_orientation_sign"])

    def test_explicit_frame_drives_rotation_instead_of_position_roles(self):
        class RotationHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
                resolve_shape_references=False,
            ):
                return tuple(descriptor["direction"])

        rotation = MainWindow._plane_reference_rotation(
            RotationHarness(),
            [
                {
                    "type": "fixed_direction",
                    "direction": (1.0, 0.0, 0.0),
                    "orientation_role": "up",
                },
                {
                    "type": "fixed_direction",
                    "direction": (0.0, 1.0, 0.0),
                    "orientation_role": "normal",
                    "container_orientation_slot": "primary",
                },
            ],
        )

        for value in rotation:
            self.assertAlmostEqual(value, 0.0)

    def test_datum_plane_is_parallel_to_explicit_front_reference(self):
        class DatumHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)
            _local_plane_for_normal = staticmethod(
                MainWindow._local_plane_for_normal
            )
            _datum_frame_references = staticmethod(
                MainWindow._datum_frame_references
            )
            _plane_reference_rotation = MainWindow._plane_reference_rotation

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
            ):
                return tuple(descriptor["direction"])

        source_normal = (0.2, 0.4, 0.8944271909999159)
        plane, rotation = MainWindow._datum_plane_frame(
            DatumHarness(),
            [{
                "type": "fixed_direction",
                "direction": source_normal,
                "orientation_role": "normal",
                "container_orientation_slot": "primary",
            }],
            "xy",
        )
        local_normal = {
            "xy": (0.0, 0.0, 1.0),
            "yz": (1.0, 0.0, 0.0),
            "xz": (0.0, 1.0, 0.0),
        }[plane]
        transform = coordinate_system_transform(
            CoordinateSystem(rotation=rotation)
        )
        world_normal = tuple(
            sum(transform[row][column] * local_normal[column]
                for column in range(3))
            for row in range(3)
        )
        agreement = abs(sum(
            world_normal[index] * source_normal[index]
            for index in range(3)
        ))
        self.assertAlmostEqual(agreement, 1.0, places=7)

    def test_face_front_preserves_outward_normal_and_back_reverses_it(self):
        class DatumHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)
            _local_plane_for_normal = staticmethod(
                MainWindow._local_plane_for_normal
            )
            _datum_frame_references = staticmethod(
                MainWindow._datum_frame_references
            )
            _plane_reference_rotation = MainWindow._plane_reference_rotation

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
            ):
                return tuple(descriptor["direction"])

        outward = (-0.2, -0.4, -0.8944271909999159)
        for role, expected_agreement in (("front", 1.0), ("back", -1.0)):
            references = [{
                "type": "container_orientation",
                "mappings": [{
                    "slot": "primary",
                    "reference": {
                        "type": "face",
                        "direction": outward,
                    },
                    "role": role,
                }],
            }]
            plane, rotation = MainWindow._datum_plane_frame(
                DatumHarness(), references, "xz"
            )
            local_normal = {
                "xy": (0.0, 0.0, 1.0),
                "yz": (1.0, 0.0, 0.0),
                "xz": (0.0, 1.0, 0.0),
            }[plane]
            transform = coordinate_system_transform(
                CoordinateSystem(rotation=rotation)
            )
            world_normal = tuple(
                sum(
                    transform[row][column] * local_normal[column]
                    for column in range(3)
                )
                for row in range(3)
            )
            agreement = sum(
                world_normal[index] * outward[index]
                for index in range(3)
            )
            self.assertAlmostEqual(
                agreement, expected_agreement, places=7
            )

    def test_front_offset_does_not_override_locked_container_origin(self):
        class ConstraintHarness:
            document = None

            @staticmethod
            def _resolved_shape_reference_equations(_descriptor):
                return None

        origin = {
            "type": "vertex",
            "equations": [
                [1.0, 0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0, 0.0],
            ],
        }
        front = {
            "type": "face",
            "equations": [[0.0, 0.0, 1.0, 0.0]],
        }
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                ConstraintHarness(),
                [
                    origin,
                    {
                        "type": "container_orientation",
                        "work_plane_offset": 25.0,
                        "mappings": [{
                            "slot": "primary",
                            "reference": front,
                            "role": "front",
                        }],
                    },
                ],
            )
        )

        self.assertEqual(solution, (0.0, 0.0, 0.0))
        self.assertEqual(dof, 0)

    def test_point_anchor_and_offset_plane_resolve_without_conflict(self):
        document = create_empty_part()
        origin = next(
            child for child in document.root.children
            if child.kind == EntityKind.ORIGIN
        )
        point = next(
            child for child in origin.children
            if child.kind == EntityKind.POINT
        )
        xz_plane = next(
            child for child in origin.children
            if child.kind == EntityKind.PLANE
            and child.parameters.get("plane") == "xz"
        )
        window = MainWindow.__new__(MainWindow)
        window.document = document

        solution, dof, _status, _constrained = (
            window._solve_point_constraints([
                {"type": "entity", "entity_id": xz_plane.entity_id,
                 "offset": 29.0},
                {"type": "entity", "entity_id": point.entity_id},
            ])
        )

        self.assertEqual(solution, (0.0, 29.0, 0.0))
        self.assertEqual(dof, 0)

        vertex_solution, vertex_dof, _status, _constrained = (
            window._solve_point_constraints([
                {"type": "entity", "entity_id": xz_plane.entity_id,
                 "offset": 29.0},
                {"type": "vertex", "equations": [
                    [1.0, 0.0, 0.0, 4.0],
                    [0.0, 1.0, 0.0, 5.0],
                    [0.0, 0.0, 1.0, 6.0],
                ]},
            ])
        )

        self.assertEqual(vertex_solution, (4.0, 29.0, 6.0))
        self.assertEqual(vertex_dof, 0)

    def test_stored_shape_equations_can_be_used_without_live_resolution(self):
        class ConstraintHarness:
            document = None

            @staticmethod
            def _resolved_shape_reference_equations(_descriptor):
                raise AssertionError("live OCCT reference resolution was used")

        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                ConstraintHarness(),
                [{
                    "type": "face",
                    "equations": [[0.0, 0.0, 1.0, 12.0]],
                }],
                (3.0, 4.0, 5.0),
                resolve_shape_references=False,
            )
        )

        self.assertEqual(solution, (3.0, 4.0, 12.0))
        self.assertEqual(dof, 2)

    def test_cached_external_sketch_geometry_avoids_live_projection(self):
        document = create_empty_part()
        container = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(container.entity_id)
        cached = {
            "type": "polyline",
            "points": [[1.0, 2.0], [3.0, 4.0]],
        }
        sketch.parameters["external_references"] = json.dumps([{
            "id": "edge:cached",
            "source_kind": "edge",
            "owner_id": document.root.entity_id,
            "element_index": 1,
            "cached_geometry": cached,
        }])
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._active_component_return_document = None
        window._sketch_selected_external_reference_id = None
        window._sync_external_profile_segments = lambda *_args: None
        window._project_sketch_external_reference = lambda *_args: (
            (_ for _ in ()).throw(
                AssertionError("live OCCT projection was used")
            )
        )

        resolved = window._resolved_sketch_external_references(sketch)

        self.assertEqual(resolved[0]["geometry"], cached)

    def test_plane_offset_is_stored_along_its_local_normal(self):
        self.assertEqual(
            MainWindow._plane_local_offset("xy", 6.0),
            (0.0, 0.0, 6.0),
        )
        self.assertEqual(
            MainWindow._plane_local_offset("yz", 6.0),
            (6.0, 0.0, 0.0),
        )
        self.assertEqual(
            MainWindow._plane_local_offset("xz", 6.0),
            (0.0, 6.0, 0.0),
        )

    def test_work_plane_dimension_respects_front_back_role(self):
        document = create_empty_part()
        container = document.create_container(
            "Protrusion",
            ContainerType.PROTRUSION,
        )
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._resolved_shape_reference_equations = lambda _reference: None
        primary = {
            "type": "face",
            "equations": [[0.0, 0.0, 1.0, 0.0]],
        }

        def dimension_for(role):
            container.parameters["constraint_refs"] = json.dumps([{
                "type": "container_orientation",
                "work_plane_offset": 6.0,
                "mappings": [{
                    "slot": "primary",
                    "role": role,
                    "reference": primary,
                }],
            }])
            return next(
                dimension
                for dimension in window._reference_dimensions(container)
                if dimension.key == "work_plane_offset"
            )

        self.assertEqual(
            dimension_for("front").second_point,
            (0.0, 0.0, 6.0),
        )
        self.assertEqual(
            dimension_for("back").second_point,
            (0.0, 0.0, -6.0),
        )

    def test_rotation_offsets_are_composed_in_local_container_frame(self):
        base = (25.0, -35.0, 40.0)
        offset = (15.0, 20.0, -10.0)
        actual = MainWindow._rotation_with_local_offset(base, offset)
        actual_transform = coordinate_system_transform(
            CoordinateSystem(rotation=actual)
        )
        expected_transform = multiply_transforms(
            coordinate_system_transform(CoordinateSystem(rotation=base)),
            coordinate_system_transform(CoordinateSystem(rotation=offset)),
        )
        for row in range(3):
            for column in range(3):
                self.assertAlmostEqual(
                    actual_transform[row][column],
                    expected_transform[row][column],
                )

    def test_fillet_command_resolves_edge_after_command_activation(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        reference = MainWindow._fillet_edge_reference(
            document, document.root.entity_id, 1
        )
        self.assertIsNotNone(reference)
        self.assertEqual(reference.role, "boundary")
        self.assertIsNone(
            MainWindow._fillet_edge_reference(
                document, container.entity_id, 1
            )
        )

    def test_original_solid_reference_resolves_without_result_body(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(container.entity_id, EntityKind.BOX)
        reference = active_face_registry(
            document
        ).edge_reference_for_runtime_index(1)
        self.assertIsNotNone(reference)

        resolution = MainWindow._direct_source_reference_resolution(
            document,
            reference,
            "edge",
        )
        self.assertIsNotNone(resolution)
        self.assertIsNotNone(resolution.shape)

    def test_lazy_assembly_pick_registers_only_selected_frame(self) -> None:
        dialog = SimpleNamespace(
            _reference_frames={},
            _source_frame_keys=set(),
        )
        MainWindow._register_lazy_assembly_frame(
            dialog,
            "selected-face",
            (1.0, 2.0, 3.0),
            (0.0, 0.0, 1.0),
            True,
        )
        self.assertEqual(
            dialog._reference_frames,
            {"selected-face": ((1.0, 2.0, 3.0), (0.0, 0.0, 1.0))},
        )
        self.assertEqual(dialog._source_frame_keys, {"selected-face"})

    def test_assembly_displayed_face_maps_to_original_solid(self) -> None:
        source_document = create_empty_part()
        source = source_document.create_container("Box", ContainerType.BOX)
        source_document.create_primitive(source.entity_id, EntityKind.BOX)
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component",
            ContainerType.COMPONENT,
        )
        component.coordinate_system.origin = (35.0, -12.0, 8.0)
        source_shape = source_document.build_standalone_shape(source)
        instance_shape = transform_shape(
            source_shape,
            coordinate_system_transform(component.coordinate_system),
        )
        explorer = TopExp_Explorer(instance_shape, TopAbs_FACE)
        self.assertTrue(explorer.More())

        window = MainWindow.__new__(MainWindow)
        match = window._component_source_face_from_displayed_face(
            component,
            source_document,
            explorer.Current(),
        )

        self.assertIsNotNone(match)
        self.assertEqual(match[0].entity_id, source.entity_id)
        self.assertGreater(match[1], 0)

    def test_assembly_mate_allows_edited_component_as_source(self) -> None:
        class VisibleDialog:
            @staticmethod
            def isVisible():
                return True

        window = MainWindow.__new__(MainWindow)
        window.assembly_component_dialog = VisibleDialog()
        window._definition_edit_objects = [
            SimpleNamespace(
                entity_id="edited-component",
                kind=EntityKind.CONTAINER,
                children=[],
            )
        ]

        self.assertFalse(
            window._current_definition_owns_reference("edited-component")
        )
        self.assertEqual(window._definition_reference_excluded_ids(), set())

    def test_large_mesh_face_pick_can_be_enabled_for_assembly_mates(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._large_mesh_topology_enabled = False

        viewer.set_large_mesh_topology_enabled(True)

        self.assertTrue(viewer._large_mesh_topology_enabled)

    def test_surface_candidate_cycle_ignores_points_and_edges(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            points=(SimpleNamespace(
                element_kind="point",
                position=(0.0, 0.0, 0.0),
                owner_id="point-owner",
                point_index=1,
            ),),
            edges=(SimpleNamespace(
                element_kind="edge",
                topology_role="sharp",
                owner_id="edge-owner",
                edge_index=1,
            ),),
            planes=(),
        )
        viewer._selection_filter = "surface"
        face_queries = []

        def face_hits(*args, **kwargs):
            face_queries.append((args, kwargs))
            return [
                (2.0, "body", 1),
                (1.0, "body", 2),
            ]

        viewer._face_hits = face_hits
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        candidates = viewer.topology_candidates_at(QPointF())

        self.assertEqual(
            candidates,
            (("face", "body", 1), ("face", "body", 2)),
        )
        self.assertEqual(len(face_queries), 1)
        self.assertEqual(face_queries[0][1]["bounds_tolerance"], 4.0)

    def test_assembly_reference_filter_includes_axes_but_not_body_edges(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            points=(),
            edges=(
                SimpleNamespace(
                    element_kind="edge",
                    topology_role="sharp",
                    owner_id="body",
                    edge_index=1,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                ),
                SimpleNamespace(
                    element_kind="axis",
                    topology_role="datum",
                    owner_id="origin",
                    edge_index=2,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                ),
            ),
            planes=(),
        )
        viewer._selection_filter = "surface_axis"
        viewer._display_mode = "shaded_with_edges"
        viewer._display_edge_points = lambda edge: edge.points
        viewer._camera_point = lambda point: point
        viewer._screen_point = lambda point: QPointF(point[0], point[1])
        viewer._face_hits = lambda *_args, **_kwargs: []
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        candidates = viewer.topology_candidates_at(QPointF(5.0, 0.0))

        self.assertEqual(candidates, (("edge", "origin", 2),))

    def test_assembly_reference_filter_never_offers_result_body_face(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._selection_filter = "reference"
        viewer._face_hits = lambda *_args, **_kwargs: [
            (1.0, "assembly-result-body", 4)
        ]

        self.assertIsNone(viewer._pick_face(QPointF()))

    def test_reference_filter_includes_only_datum_axes_from_scene(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            is_empty=False,
            points=(SimpleNamespace(
                element_kind="vertex",
                position=(5.0, 0.0, 0.0),
                owner_id="point-owner",
                point_index=1,
            ),),
            edges=(
                SimpleNamespace(
                    element_kind="edge",
                    topology_role="sharp",
                    owner_id="solid",
                    edge_index=1,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                ),
                SimpleNamespace(
                    element_kind="axis",
                    topology_role="datum",
                    owner_id="origin",
                    edge_index=2,
                    points=((0.0, 3.0, 0.0), (10.0, 3.0, 0.0)),
                ),
            ),
            planes=(),
        )
        viewer._selection_filter = "reference"
        viewer._display_mode = "shaded_with_edges"
        viewer._display_edge_points = lambda edge: edge.points
        viewer._camera_point = lambda point: point
        viewer._screen_point = lambda point: QPointF(point[0], point[1])
        viewer._face_hits = lambda *_args, **_kwargs: []
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        candidates = viewer.topology_candidates_at(QPointF(5.0, 0.0))
        outside_precise_axis = viewer.topology_candidates_at(
            QPointF(5.0, 7.0)
        )

        self.assertNotIn(("edge", "solid", 1), candidates)
        self.assertIn(("edge", "origin", 2), candidates)
        self.assertNotIn(("edge", "origin", 2), outside_precise_axis)
        self.assertNotIn(("point", "point-owner", 1), candidates)

    def test_assembly_reference_hover_picks_generated_axis(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            is_empty=False,
            edges=(
                EdgePolyline(
                    edge_index=1,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                    owner_id="component:generated-axis",
                    element_kind="centerline",
                ),
            ),
        )
        viewer._selection_filter = "surface_axis"
        viewer._display_mode = "shaded_with_edges"
        viewer._display_edge_points = lambda edge: edge.points
        viewer._camera_point = lambda point: point
        viewer._screen_point = lambda point: QPointF(point[0], point[1])
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        picked = viewer._pick_edge(QPointF(5.0, 0.0))

        self.assertEqual(picked, ("component:generated-axis", 1))

    def test_assembly_datum_axis_is_delivered_to_properties_dialog(self) -> None:
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component", ContainerType.COMPONENT
        )
        origin = next(
            child for child in component.children
            if child.kind == EntityKind.ORIGIN
        )
        accepted = []
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window.assembly_component_dialog = SimpleNamespace(
            isVisible=lambda: True,
            selection_paused=False,
            accept_axis=lambda descriptor: accepted.append(descriptor),
        )

        consumed = window._accept_assembly_edge_reference(
            origin.entity_id, 1
        )

        self.assertTrue(consumed)
        self.assertEqual(
            accepted, [f"{component.entity_id}:axis:X"]
        )

    def test_reference_picker_prefers_axis_over_coincident_body_edge(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            is_empty=False,
            edges=(
                EdgePolyline(
                    edge_index=1,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                    owner_id="body",
                    element_kind="edge",
                ),
                EdgePolyline(
                    edge_index=2,
                    points=((0.0, 0.0, 0.0), (10.0, 0.0, 0.0)),
                    owner_id="component:datum-axis",
                    element_kind="axis",
                ),
            ),
        )
        viewer._selection_filter = "reference"
        viewer._display_mode = "shaded_with_edges"
        viewer._display_edge_points = lambda edge: edge.points
        viewer._camera_point = lambda point: point
        viewer._screen_point = lambda point: QPointF(point[0], point[1])
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        self.assertEqual(
            viewer._pick_edge(QPointF(5.0, 0.0)),
            ("component:datum-axis", 2),
        )

    def test_plane_picker_accepts_plane_interior(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        plane = SimpleNamespace(
            owner_id="origin-plane",
            plane_index=1,
            corners=(
                (0.0, 0.0, 0.0),
                (10.0, 0.0, 0.0),
                (10.0, 10.0, 0.0),
                (0.0, 10.0, 0.0),
            ),
        )
        viewer._mesh = SimpleNamespace(planes=(plane,))
        viewer._selection_filter = "all"
        viewer._display_plane_corners = lambda item: item.corners
        viewer._camera_point = lambda point: point
        viewer._screen_point = lambda point: QPointF(point[0], point[1])
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        self.assertEqual(
            viewer._pick_plane(QPointF(5.0, 5.0)),
            ("origin-plane", 1),
        )

    def test_stable_reference_hover_picks_non_scene_source_face(self) -> None:
        document = create_empty_part()
        source = document.create_container("Source", ContainerType.BOX)
        source_mesh = SimpleNamespace(
            face_mesh=lambda owner_id, face_index: (
                "face-mesh", owner_id, face_index
            ),
            edge_mesh=lambda *_args: None,
            point_mesh=lambda *_args: None,
        )
        viewer = SimpleNamespace(
            _pick_plane=lambda _position: None,
            _pick_axis=lambda _position: None,
        )
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window.native_viewer = viewer
        window.assembly_component_dialog = None
        window._dimension_inspection_visuals = ()
        window._viewer_selection_policy = lambda: ViewerSelectionPolicy(
            SelectionPurpose.STABLE_REFERENCE,
            TopologySource.ORIGINAL_SOLIDS,
            frozenset({SelectionKind.FACE}),
            "topology",
            "all",
        )
        window._source_topology_reference_at_position = (
            lambda kind, _position: (
                (source.entity_id, 3, source_mesh)
                if kind == "face" else None
            )
        )
        candidate = window._viewer_pick_candidate(QPointF(25.0, 30.0))

        self.assertEqual(candidate.kind, "face")
        self.assertEqual(candidate.owner_id, source.entity_id)
        self.assertEqual(candidate.element_index, 3)
        self.assertEqual(
            candidate.hover_mesh, ("face-mesh", source.entity_id, 3)
        )

    def test_cycled_hidden_face_preview_is_not_replaced_by_cursor_hit(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._dimension_inspection_visuals = ()
        cleared = []
        hovered = []
        window.native_viewer = SimpleNamespace(
            _cycled_topology_candidate=("face", "body", 6),
            set_source_topology_hover=lambda mesh: cleared.append(mesh),
            set_feature_hover_edges=lambda edges: cleared.append(edges),
        )
        window._set_view_hover = lambda *value: hovered.append(value)
        window._viewer_selection_policy = lambda: ViewerSelectionPolicy(
            SelectionPurpose.VIEW_ORIENTATION,
            TopologySource.DISPLAYED_MODEL,
            frozenset({SelectionKind.FACE}),
            "topology",
            "face",
        )
        window._on_native_source_topology_hovered = lambda *_args: self.fail(
            "cycled preview must not be replaced by a cursor ray hit"
        )

        window._on_native_face_hovered("body", 6)

        self.assertEqual(cleared, [None, set()])
        self.assertEqual(hovered, [("face", "body", 6)])

    def test_result_body_can_be_excluded_from_object_picking(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._excluded_object_owner_ids = frozenset({"result-body"})
        viewer._pick_face = lambda _position: ("result-body", 1)
        viewer._pick_edge = lambda _position: ("result-body", 1)

        self.assertIsNone(viewer._pick_object(QPointF()))

        viewer._excluded_object_owner_ids = frozenset()
        self.assertEqual(viewer._pick_object(QPointF()), "result-body")

    def test_part_hover_resolves_latest_historical_container(self) -> None:
        document = create_empty_part()
        first = document.create_container("First", ContainerType.BOX)
        document.create_primitive(first.entity_id, EntityKind.BOX)
        second = document.create_container("Second", ContainerType.BOX)
        document.create_primitive(second.entity_id, EntityKind.BOX)
        window = MainWindow.__new__(MainWindow)
        window.document = document
        source_mesh = SimpleNamespace()
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(source_bodies={
                first.entity_id: SimpleNamespace(mesh=source_mesh),
                second.entity_id: SimpleNamespace(mesh=source_mesh),
            }),
        )
        window._cached_source_model_shapes = []
        window._cached_source_model_meshes = {}
        window._definition_history_boundary = lambda: 2
        window.native_viewer = SimpleNamespace(
            mesh_is_under_cursor=lambda _mesh, _position: True,
        )

        hit = window._part_history_container_at_position(QPointF())

        self.assertIsNotNone(hit)
        self.assertIs(hit[0], second)

    def test_new_container_origin_uses_fallback_before_references(self) -> None:
        class Edit:
            def __init__(self, value):
                self._value = value

            def value(self):
                return self._value

        dialog = SimpleNamespace(
            point_object=None,
            solution=None,
            coordinate_edits=(Edit(4.0), Edit(5.0), Edit(6.0)),
            rotation_edits=None,
            point_rotation=lambda: (0.0, 0.0, 0.0),
            _solution_references=lambda: [],
        )
        window = MainWindow.__new__(MainWindow)
        window.point_constraint_dialog = dialog
        window._plane_reference_rotation = lambda _references: (0.0, 0.0, 0.0)
        window._rotation_with_local_offset = lambda base, _offset: base

        coordinate_system = window._definition_preview_coordinate_system()

        self.assertEqual(coordinate_system.origin, (4.0, 5.0, 6.0))

    def test_middle_dismiss_invalidates_the_stored_part_hover(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._sketch_edit_entity_id = None
        window._sketch_show_all_dimensions = False
        window._dismiss_view_selection_requested = False
        window._dimension_overlays = {}
        window._dimension_object_id = None
        window._dimension_bindings = {}
        window._dimension_owner_ids = {}
        window._dimension_inspection_visuals = False
        window._dimension_selection_suspended = False
        window.native_viewer = SimpleNamespace(
            set_dimensions=lambda _dimensions: None,
            set_dimension_inspection_active=lambda _active: None,
        )

        window._dismiss_dimension_overlays()

        self.assertTrue(window._dismiss_view_selection_requested)

    def test_dimension_dismiss_clears_selection_and_resumes_hover(self) -> None:
        calls = []

        class Signal:
            def __init__(self, name):
                self.name = name

            def emit(self, *args):
                calls.append((self.name, args))

        viewer = SimpleNamespace(
            dimensionsDismissRequested=Signal("dimensions"),
            selectedObjectChanged=Signal("selection"),
            _clear_topology_selection=lambda: calls.append(
                ("topology", ())
            ),
        )

        ZimaOpenGLViewer._dismiss_dimensions_and_object_selection(viewer)

        self.assertEqual(
            calls,
            [
                ("dimensions", ()),
                ("topology", ()),
                ("selection", ("",)),
            ],
        )

    def test_application_selection_restores_general_viewer_mode(self) -> None:
        calls = []

        class Action:
            def __init__(self, checked=False):
                self.checked = checked

            def blockSignals(self, blocked):
                calls.append(("block", blocked))
                return False

            def setChecked(self, checked):
                self.checked = checked
                calls.append(("application_action", checked))

            def isChecked(self):
                return self.checked

        window = MainWindow.__new__(MainWindow)
        window.application_selection_action = Action(False)
        window.view_selection_action = Action(False)
        window.native_viewer = SimpleNamespace(
            _dismiss_dimensions_and_object_selection=lambda: calls.append(
                ("dismiss", True)
            )
        )
        window.rebuild_view = lambda **kwargs: calls.append(
            ("rebuild", kwargs)
        )

        window._activate_application_selection()

        self.assertTrue(window.application_selection_action.isChecked())
        self.assertTrue(window.view_selection_action.isChecked())
        self.assertIn(("dismiss", True), calls)
        self.assertIn(
            (
                "rebuild",
                {"fit": False, "rebuild_geometry": False},
            ),
            calls,
        )

    def test_feature_dimensions_suspend_selection_after_profile_cleanup(self):
        window = MainWindow.__new__(MainWindow)
        feature = SimpleNamespace(
            kind=EntityKind.PROTRUSION,
            parameters={"sketch_id": "sketch"},
        )
        container = SimpleNamespace(
            kind=EntityKind.CONTAINER,
            container_type=ContainerType.PROTRUSION,
            children=[feature],
        )
        window.document = SimpleNamespace(
            find_owning_object=lambda _entity_id: container
        )
        window._first_editable_dimension_entity = lambda _obj: feature
        window._dimension_overlays = {"length": object()}
        calls = []
        window._show_protrusion_profile_overlay = (
            lambda _obj: calls.append("profile")
        )
        window._begin_dimension_inspection = (
            lambda: calls.append("suspend")
        )

        shown = window._show_edit_overlays(container, QPoint())

        self.assertTrue(shown)
        self.assertEqual(calls, ["profile", "suspend"])

    def test_dimension_inspection_suspends_and_restores_hover_selection(self):
        calls = []

        class Viewer:
            def set_selection_enabled(self, enabled):
                calls.append(("selection", enabled))

            def set_dimension_inspection_active(self, active):
                calls.append(("inspection", active))

            def _clear_topology_hover(self):
                calls.append(("clear_hover", None))

            def _clear_topology_selection(self):
                calls.append(("clear_topology", None))

            def _set_hovered_object(self, owner_id):
                calls.append(("hovered_object", owner_id))

            def set_dimensions(self, _dimensions):
                pass

        window = MainWindow.__new__(MainWindow)
        window.native_viewer = Viewer()
        window.selection_filter_combo = SimpleNamespace(
            setEnabled=lambda enabled: calls.append(("combo", enabled))
        )
        window.view_selection_enabled = True
        window._dimension_selection_suspended = False
        window._dimension_inspection_visuals = False
        window._dimension_overlays = {}
        window._dimension_object_id = None
        window._dimension_bindings = {}
        window._dimension_owner_ids = {}

        window._begin_dimension_inspection()
        window._clear_dimension_overlays()

        self.assertIn(("inspection", True), calls)
        self.assertIn(("selection", False), calls)
        self.assertEqual(calls[-2:], [("selection", True), ("combo", True)])
        self.assertFalse(window._dimension_selection_suspended)

    def test_left_click_does_not_dismiss_dimension_inspection(self) -> None:
        accepted = []
        viewer = SimpleNamespace(
            _dimension_inspection_active=True,
            _dimensions=(object(),),
            _dismiss_dimensions_and_object_selection=lambda: self.fail(
                "LMB must not finish dimension inspection"
            ),
        )
        event = SimpleNamespace(
            button=lambda: Qt.MouseButton.LeftButton,
            accept=lambda: accepted.append(True),
        )

        ZimaOpenGLViewer.mousePressEvent(viewer, event)

        self.assertEqual(accepted, [True])

    def test_imported_face_reference_is_created_lazily(self) -> None:
        imported = SimpleNamespace(
            entity_id="imported-step",
            kind=EntityKind.IMPORTED_STEP,
            locked=False,
        )
        source = SimpleNamespace(children=[imported])

        reference = MainWindow._lazy_imported_face_reference(source, 12001)

        self.assertEqual(
            reference,
            FaceRef("imported-step", "imported", "12001"),
        )

    def test_assembly_properties_forces_viewport_reference_mode(self) -> None:
        calls = []

        class Viewer:
            def __getattr__(self, name):
                return lambda value=None: calls.append((name, value))

        window = MainWindow.__new__(MainWindow)
        window.native_viewer = Viewer()

        window._configure_assembly_reference_picking()

        self.assertIn(("set_selection_enabled", True), calls)
        self.assertIn(("set_selection_filter", "reference"), calls)
        self.assertIn(("set_interaction_mode", "topology"), calls)
        self.assertIn(("set_excluded_topology_owners", set()), calls)
        self.assertIn(("set_large_mesh_topology_enabled", True), calls)
        self.assertIn(("set_object_overlay", None), calls)
        self.assertIn(("_set_selected_object", None), calls)
        self.assertIn(("_set_hovered_object", None), calls)
        self.assertFalse(window.native_viewer._object_overlay_locks_interaction)
        self.assertFalse(window.native_viewer._object_overlay_persistent)

    def test_viewer_policy_distinguishes_normal_and_feature_selection(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._selection_controller = SelectionController()
        window._sketch_reference_mode = False
        window.assembly_component_dialog = None
        window.point_constraint_dialog = None
        window.orientation_dialog = None

        window.document = create_empty_part()
        policy = window._viewer_selection_policy()
        self.assertEqual(policy.purpose, SelectionPurpose.PART_CONTAINER)
        self.assertEqual(policy.topology_source, TopologySource.NONE)
        self.assertEqual(policy.allowed_kinds, {SelectionKind.OBJECT})

        window.document = create_empty_assembly()
        policy = window._viewer_selection_policy()
        self.assertEqual(policy.purpose, SelectionPurpose.ASSEMBLY_COMPONENT)
        self.assertEqual(policy.interaction_mode, "object")

        window._selection_controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(candidate),
            on_complete=lambda _values: None,
        ))
        policy = window._viewer_selection_policy()
        self.assertEqual(policy.purpose, SelectionPurpose.BODY_EDGE_OPERATION)
        self.assertEqual(policy.topology_source, TopologySource.INPUT_BODY)
        self.assertEqual(policy.selection_filter, "edge")

    def test_viewer_policy_uses_original_topology_for_container_references(self):
        window = MainWindow.__new__(MainWindow)
        window._selection_controller = SelectionController()
        window._sketch_reference_mode = False
        window.assembly_component_dialog = None
        window.orientation_dialog = None
        window.document = create_empty_part()
        window.point_constraint_dialog = SimpleNamespace(
            isVisible=lambda: True,
        )
        window._container_orientation_selection_is_active = lambda: False

        policy = window._viewer_selection_policy()

        self.assertEqual(policy.purpose, SelectionPurpose.STABLE_REFERENCE)
        self.assertTrue(policy.uses_original_topology)
        self.assertIn(SelectionKind.FACE, policy.allowed_kinds)
        self.assertIn(SelectionKind.EDGE, policy.allowed_kinds)
        self.assertIn(SelectionKind.POINT, policy.allowed_kinds)

    def test_up_to_filters_the_existing_reference_contract_only_while_active(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )
        dialog.show()
        application.processEvents()
        window = MainWindow.__new__(MainWindow)
        window._selection_controller = SelectionController()
        window._sketch_reference_mode = False
        window.assembly_component_dialog = None
        window.orientation_dialog = None
        window.document = create_empty_part()
        window.point_constraint_dialog = dialog
        window._container_orientation_selection_is_active = lambda: False

        placement_policy = window._viewer_selection_policy()
        self.assertIn(SelectionKind.EDGE, placement_policy.allowed_kinds)
        self.assertIn(SelectionKind.AXIS, placement_policy.allowed_kinds)

        dialog.forward_end_condition_combo.setCurrentIndex(
            dialog.forward_end_condition_combo.findData("up_to")
        )
        self.assertTrue(dialog.end_reference_pick_active())
        up_to_policy = window._viewer_selection_policy()
        self.assertEqual(
            up_to_policy.allowed_kinds,
            {SelectionKind.FACE, SelectionKind.POINT, SelectionKind.PLANE},
        )

        dialog._set_end_reference_pick_active("forward", False)
        restored_policy = window._viewer_selection_policy()
        self.assertEqual(
            restored_policy.allowed_kinds, placement_policy.allowed_kinds
        )
        dialog.close()

    def test_up_to_contract_change_discards_candidates_from_placement(self):
        application = QApplication.instance() or QApplication([])
        dialog = ProtrusionConstraintDialog(
            lambda _references, fallback: (
                fallback, 3, "", (False, False, False)
            ),
            [],
        )
        dialog.show()
        application.processEvents()
        viewer = SimpleNamespace(
            invalidated=False,
            blockSignals=lambda _blocked: False,
            invalidate_pick_candidates=lambda: setattr(
                viewer, "invalidated", True
            ),
            set_selection_filter=lambda value: setattr(
                viewer, "selection_filter", value
            ),
            set_interaction_mode=lambda value: setattr(
                viewer, "interaction_mode", value
            ),
            set_reference_picking_active=lambda value: setattr(
                viewer, "reference_picking", value
            ),
            set_selection_enabled=lambda value: setattr(
                viewer, "selection_enabled", value
            ),
            update=lambda: None,
        )
        window = MainWindow.__new__(MainWindow)
        window.native_viewer = viewer
        window._selection_controller = SelectionController()
        window._sketch_reference_mode = False
        window.assembly_component_dialog = None
        window.orientation_dialog = None
        window.document = create_empty_part()
        window.point_constraint_dialog = dialog
        window._container_orientation_selection_is_active = lambda: False
        window._view_candidate_cycle_ids = ("edge:old:1",)
        window._view_candidate_cycle_index = 0
        window._history_source_cycle_ids = ("edge:old:1",)
        window._history_source_cycle_index = 0
        window._history_source_cycle_active = True

        dialog._set_end_reference_pick_active("forward", True)
        window._protrusion_end_reference_contract_changed(dialog)

        self.assertTrue(viewer.invalidated)
        self.assertEqual(window._view_candidate_cycle_ids, ())
        self.assertEqual(window._history_source_cycle_ids, ())
        self.assertEqual(viewer.interaction_mode, "topology")
        self.assertTrue(viewer.reference_picking)
        dialog.close()

    def test_assembly_choices_sync_generated_solid_axes_before_picking(self) -> None:
        source_document = create_empty_part()
        source_container = source_document.create_container(
            "Cylinder001", ContainerType.CYLINDER
        )
        cylinder = source_document.create_primitive(
            source_container.entity_id, EntityKind.CYLINDER
        )
        cylinder.children.clear()

        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component001", ContainerType.COMPONENT
        )
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        window._component_source_document = (
            lambda candidate: source_document
            if candidate.entity_id == component.entity_id
            else None
        )

        choices = window._assembly_plane_choices(component, True)

        generated_axis = next(
            child for child in cylinder.children
            if child.parameters.get("generated_axis") == "true"
        )
        descriptor = (
            f"{component.entity_id}:datum_axis:{generated_axis.entity_id}"
        )
        self.assertIn(descriptor, {choice[1] for choice in choices})

    def test_lazy_assembly_pick_filter_contains_real_components(self) -> None:
        assembly = create_empty_assembly()
        source = assembly.create_container("Source", ContainerType.COMPONENT)
        target = assembly.create_container("Target", ContainerType.COMPONENT)
        window = MainWindow.__new__(MainWindow)
        window.document = assembly

        self.assertEqual(
            window._assembly_reference_pick_owner_ids(source, "source"),
            {source.entity_id},
        )
        self.assertEqual(
            window._assembly_reference_pick_owner_ids(source, "target"),
            {target.entity_id},
        )

    def test_parent_transform_propagates_through_assembly_mate_chain(self) -> None:
        assembly = create_empty_assembly()
        parent = assembly.create_container("Parent", ContainerType.COMPONENT)
        child = assembly.create_container("Child", ContainerType.COMPONENT)
        grandchild = assembly.create_container(
            "Grandchild",
            ContainerType.COMPONENT,
        )
        child.coordinate_system.origin = (10.0, 0.0, 0.0)
        grandchild.coordinate_system.origin = (20.0, 0.0, 0.0)
        child.parameters["assembly_mates"] = json.dumps([{
            "target": f"{parent.entity_id}:plane:XY",
        }])
        grandchild.parameters["assembly_mates"] = json.dumps([{
            "target": f"{child.entity_id}:plane:XY",
        }])
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        previous = window._homogeneous_transform(parent.coordinate_system)

        parent.coordinate_system.origin = (5.0, 3.0, 0.0)
        window._propagate_assembly_component_transform(parent, previous)

        self.assertEqual(child.coordinate_system.origin, (15.0, 3.0, 0.0))
        self.assertEqual(
            grandchild.coordinate_system.origin,
            (25.0, 3.0, 0.0),
        )

    def test_plane_mate_drag_removes_normal_translation(self) -> None:
        delta = MainWindow._project_assembly_drag_delta(
            (3.0, 4.0, 5.0),
            [(0.0, 0.0, 1.0)],
        )

        self.assertAlmostEqual(delta[0], 3.0)
        self.assertAlmostEqual(delta[1], 4.0)
        self.assertAlmostEqual(delta[2], 0.0)

    def test_axis_mate_drag_allows_only_axis_translation(self) -> None:
        delta = MainWindow._project_assembly_drag_delta(
            (3.0, 4.0, 5.0),
            [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
        )

        self.assertAlmostEqual(delta[0], 0.0)
        self.assertAlmostEqual(delta[1], 0.0)
        self.assertAlmostEqual(delta[2], 5.0)

    def test_fully_constrained_mate_drag_blocks_translation(self) -> None:
        delta = MainWindow._project_assembly_drag_delta(
            (3.0, 4.0, 5.0),
            [
                (1.0, 0.0, 0.0),
                (0.0, 1.0, 0.0),
                (0.0, 0.0, 1.0),
            ],
        )

        for value in delta:
            self.assertAlmostEqual(value, 0.0)

    def test_standalone_sketch_edit_uses_its_own_history_boundary(self) -> None:
        document = create_empty_part()
        before = document.create_container("Before", ContainerType.BOX)
        document.create_primitive(before.entity_id, EntityKind.BOX)
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id)
        after = document.create_container("After", ContainerType.BOX)
        document.create_primitive(after.entity_id, EntityKind.BOX)

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_edit_entity_id = sketch.entity_id

        self.assertEqual(window._definition_history_boundary(), 1)

    def test_sketch_projects_face_from_original_history_solid(self) -> None:
        document = create_empty_part()
        source = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(source.entity_id, EntityKind.BOX)
        sketch_owner = document.create_container(
            "Sketch",
            ContainerType.SKETCH,
        )
        sketch = document.create_sketch(sketch_owner.entity_id)

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._native_viewer_scene = None
        window._active_component_return_document = None
        window._active_component_entity_id = None

        projection = window._project_sketch_external_reference(
            sketch,
            {
                "source_kind": "face",
                "owner_id": source.entity_id,
                "element_index": 1,
            },
        )

        self.assertIsNotNone(projection)
        self.assertEqual(projection["type"], "polylines")

    def test_view_double_click_does_not_open_container_in_sketcher(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._sketch_edit_entity_id = "active-sketch"
        window._selected_object = lambda: self.fail(
            "Sketcher double-click reached container editing"
        )

        window._on_native_object_double_clicked("owning-protrusion")

    def test_view_double_click_uses_confirmed_container_identity(self) -> None:
        document = create_empty_part()
        box = document.create_container("Box", ContainerType.BOX)
        activated = []
        shown = []

        class Viewer:
            @staticmethod
            def blockSignals(_blocked):
                return False

            @staticmethod
            def _clear_topology_selection():
                return None

            @staticmethod
            def set_selected_container_contents(_ids):
                return None

            @staticmethod
            def set_selected_container_origin(_owner_id):
                return None

            @staticmethod
            def width():
                return 800

            @staticmethod
            def height():
                return 600

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_edit_entity_id = None
        window._selected_object = lambda: box
        window._edge_treatment_at_last_view_click = lambda _owner_id: None
        window._activate_object_for_editing = (
            lambda target: activated.append(target) or target
        )
        window._show_edit_overlays = (
            lambda target, _position: shown.append(target)
        )
        window.native_viewer = Viewer()

        window._on_native_object_double_clicked(box.entity_id)

        self.assertEqual(activated, [box])
        self.assertEqual(shown, [box])

    def test_sketch_placement_picks_original_face_directly(self) -> None:
        original = ("source-solid", 4, object())
        window = MainWindow.__new__(MainWindow)
        window._source_topology_reference_at_cursor = (
            lambda kind: original if kind == "face" else None
        )

        self.assertIs(
            window._source_face_reference_for_pick(7),
            original,
        )

    def test_stored_external_sketch_points_are_always_visible(self) -> None:
        reference = {"id": "external-point"}
        visible = ZimaOpenGLViewer._external_point_marker_visible
        self.assertTrue(visible(reference, None, True))
        self.assertTrue(visible(reference, "external-point", True))
        self.assertTrue(visible({**reference, "selected": True}, None, True))
        self.assertTrue(visible(reference, None, False))

    def test_solid_vertices_are_selectable_only_in_topology_mode(self) -> None:
        class ViewerState:
            _sketch_reference_selection_mode = False
            _interaction_mode = "object"

        state = ViewerState()
        selectable = ZimaOpenGLViewer._point_marker_is_selectable
        self.assertFalse(selectable(state, "vertex"))
        state._interaction_mode = "topology"
        self.assertTrue(selectable(state, "vertex"))
        state._interaction_mode = "object"
        self.assertTrue(selectable(state, "datum_point"))

    def test_solid_vertex_pick_maps_back_to_occt_vertex(self) -> None:
        shape = BRepPrimAPI_MakeBox(20.0, 30.0, 40.0).Shape()
        mesh = triangulate_shape(shape, owner_id="result")
        self.assertEqual(len(mesh.points), 8)
        for marker in mesh.points:
            self.assertIsNotNone(topology_subshape(
                shape,
                element_kind="point",
                element_index=marker.point_index,
            ))

    def test_stable_vertex_reference_constrains_point_and_reports_loss(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        registry = active_face_registry(document)
        reference = registry.vertex_references[0]
        descriptor = {
            "type": "vertex",
            "entity_id": document.root.entity_id,
            "reference_scope": "history_result",
            "history_cursor": 1,
            "point_ref": reference.to_dict(),
            "equations": [],
        }

        class Harness:
            def __init__(self, part):
                self.document = part

            def _shape_for_reference_descriptor(self, _descriptor, _reference):
                return self.document.build_shape_at(1)

            def _resolved_shape_reference_equations(self, item):
                return MainWindow._resolved_shape_reference_equations(self, item)

        harness = Harness(document)
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                harness, [descriptor], (9.0, 9.0, 9.0)
            )
        )
        self.assertEqual(dof, 0)
        self.assertIsNotNone(solution)

        box.suppressed = True
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                harness, [descriptor], (9.0, 9.0, 9.0)
            )
        )
        self.assertEqual(dof, 3)
        self.assertEqual(solution, (9.0, 9.0, 9.0))
        self.assertEqual(descriptor["resolution_state"], "missing")

    def test_model_display_modes_use_distinct_surface_passes(self) -> None:
        self.assertEqual(_surface_pass_for_display_mode("wire"), "none")
        self.assertEqual(_surface_pass_for_display_mode("hidden_edges"), "depth")
        self.assertEqual(_surface_pass_for_display_mode("no_hidden"), "depth")
        self.assertEqual(
            _surface_pass_for_display_mode("shaded_with_edges"), "color"
        )
        self.assertEqual(_surface_pass_for_display_mode("shaded"), "color")

    def test_drawing_styles_map_to_shared_renderer_modes(self) -> None:
        mode_for = DrawingCanvas._renderer_display_mode
        self.assertEqual(mode_for({"display_style": "wireframe"}), "wire")
        self.assertEqual(
            mode_for({"display_style": "hidden_line"}), "hidden_edges"
        )
        self.assertEqual(
            mode_for({"display_style": "no_hidden"}), "no_hidden"
        )
        self.assertEqual(
            mode_for({"display_style": "shaded_edges"}),
            "shaded_with_edges",
        )
        self.assertEqual(mode_for({"display_style": "shaded"}), "shaded")

    def test_family_table_data_keeps_instances_and_optional_columns(self) -> None:
        document = create_empty_part()
        document.document_settings["family_table"] = json.dumps({
            "columns": ["LENGTH", "MATERIAL"],
            "instances": [{
                "name": "LONG",
                "values": {"LENGTH": "100", "MATERIAL": "STEEL"},
            }],
        })
        self.assertEqual(
            FamilyTableDialog.document_data(document, "generic.prtz"),
            {
                "columns": ["LENGTH", "MATERIAL"],
                "instances": [{
                    "name": "LONG",
                    "values": {
                        "LENGTH": "100",
                        "MATERIAL": "STEEL",
                    },
                }],
                "generic_name": "generic.prtz",
            },
        )

    def test_sketch_mirror_reflects_point_across_shifted_axis(self) -> None:
        self.assertEqual(
            MainWindow._mirrored_sketch_position(
                (5.0, 3.0),
                (2.0, 0.0),
                (0.0, 1.0),
            ),
            (-1.0, 3.0),
        )

    def test_working_directory_cleanup_only_collects_cad_versions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in (
                "part.prtz",
                "part.prtz.1",
                "part.prtz.3",
                "machine.asmz.2",
                "sheet.drwz.4",
                "notes.txt.1",
                "part.prtz.tmp",
            ):
                (root / name).write_text(name, encoding="utf-8")

            groups = MainWindow._working_directory_archive_groups(root)

            self.assertEqual(
                {path.name for path in groups},
                {"part.prtz", "machine.asmz", "sheet.drwz"},
            )
            self.assertEqual(
                [path.name for path in groups[root / "part.prtz"]],
                ["part.prtz.1", "part.prtz.3"],
            )

    def test_curved_external_sketch_edge_remains_bounded(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "polyline",
            "points": [[1.0, 0.0], [0.7, 0.7], [0.0, 1.0]],
        })

        self.assertIsNotNone(geometry)
        self.assertEqual(geometry["type"], "polyline")
        segment = MainWindow._sketch_reference_constraint_line(
            geometry,
            {},
            (0.7, 0.7),
        )
        self.assertIsNotNone(segment)
        self.assertTrue(segment["bounded"])

    def test_straight_external_sketch_edge_remains_infinite(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "polyline",
            "points": [[0.0, 0.0], [1.0, 0.0], [2.0, 0.0]],
        })

        self.assertIsNotNone(geometry)
        self.assertEqual(geometry["type"], "line")

    def test_model_edge_reference_remains_a_finite_segment(self) -> None:
        projected_edge = {
            "type": "polyline",
            "points": [[0.0, 0.0], [2.0, 0.0]],
        }

        edge = MainWindow._normalized_sketch_reference_geometry(
            "edge", projected_edge
        )
        plane = MainWindow._normalized_sketch_reference_geometry(
            "plane", projected_edge
        )

        self.assertEqual(edge, projected_edge)
        self.assertEqual(plane["type"], "line")

    def test_axis_crossing_sketch_is_kept_as_axis_point(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "axis_point",
            "point": [4.0, -3.0],
        })

        self.assertEqual(geometry, {
            "type": "axis_point",
            "point": [4.0, -3.0],
        })

    def test_orientation_references_leave_rotation_offsets_editable(self) -> None:
        dof_for = AxisConstraintDialog._rotation_degrees_of_freedom

        self.assertEqual(dof_for([]), 3)
        self.assertEqual(dof_for([{"orientation_role": "normal"}]), 1)
        self.assertEqual(dof_for([
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]), 0)
        editable_for = AxisConstraintDialog._editable_rotation_axes
        self.assertEqual(editable_for([]), {"x", "y", "z"})
        self.assertEqual(
            editable_for([{"orientation_role": "normal"}]),
            {"x", "y", "z"},
        )
        self.assertEqual(editable_for([
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]), {"x", "y", "z"})
        drives = AxisConstraintDialog._reference_drives_rotation_kind
        legacy_plane = {
            "type": "entity",
            "orientation_role": "normal",
        }
        axis = {
            "type": "entity",
            "orientation_role": "normal",
        }
        self.assertFalse(drives(legacy_plane, EntityKind.PLANE))
        self.assertTrue(drives(axis, EntityKind.AXIS))
        self.assertTrue(drives(
            {**legacy_plane, "orientation_drives_rotation": True},
            EntityKind.PLANE,
        ))

    def test_parallel_direction_does_not_complete_container_frame(self) -> None:
        class OrientationHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
                resolve_shape_references=False,
            ):
                return tuple(descriptor["direction"])

        independent = MainWindow._orientation_references_are_independent
        harness = OrientationHarness()
        primary = [{"direction": (1.0, 0.0, 0.0)}]

        self.assertFalse(independent(
            harness,
            primary,
            {"direction": (2.0, 0.0, 0.0)},
        ))
        self.assertFalse(independent(
            harness,
            primary,
            {"direction": (-1.0, 0.0, 0.0)},
        ))
        self.assertTrue(independent(
            harness,
            primary,
            {"direction": (0.0, 1.0, 0.0)},
        ))

    def test_edge_direction_falls_back_to_constraint_equations(self) -> None:
        class EdgeHarness:
            document = None
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)

        direction = MainWindow._orientation_reference_vector(
            EdgeHarness(),
            {
                "type": "edge",
                "topology_key": "semantic:edge:key",
                "equations": [
                    [0.0, 1.0, 0.0, 2.0],
                    [0.0, 0.0, 1.0, 3.0],
                ],
            },
            allow_frame_fallback=False,
        )

        self.assertEqual(direction, (1.0, 0.0, 0.0))

    def test_inclined_sketch_view_uses_complete_local_frame(self) -> None:
        root_half = 2.0 ** -0.5
        base_x = (root_half, root_half, 0.0)
        base_y = (-0.5, 0.5, root_half)
        twist = radians(30.0)
        x_axis = tuple(
            cos(twist) * base_x[index]
            + sin(twist) * base_y[index]
            for index in range(3)
        )
        y_axis = tuple(
            -sin(twist) * base_x[index]
            + cos(twist) * base_y[index]
            for index in range(3)
        )
        direction, roll = MainWindow._sketch_view_orientation((
            (10.0, 20.0, 30.0),
            x_axis,
            y_axis,
        ))

        expected_normal = MainWindow._normalized_vector(
            MainWindow._cross_product(x_axis, y_axis)
        )
        for actual, expected in zip(direction, expected_normal):
            self.assertAlmostEqual(actual, -expected)
        self.assertNotAlmostEqual(roll, 0.0)

    def test_inclined_view_direction_maps_to_camera_depth(self) -> None:
        direction = MainWindow._normalized_vector((1.0, -2.0, 3.0))
        yaw, pitch = camera_angles_for_view_direction(direction)
        rotation = _camera_rotation_matrix(yaw, pitch, 0.0)
        camera_direction = tuple(
            sum(rotation[row][column] * direction[column]
                for column in range(3))
            for row in range(3)
        )

        self.assertAlmostEqual(camera_direction[0], 0.0)
        self.assertAlmostEqual(camera_direction[1], 0.0)
        self.assertAlmostEqual(camera_direction[2], -1.0)

    def test_live_bounds_change_preserves_camera_projection(self) -> None:
        camera = CameraState(
            yaw_degrees=37.0,
            pitch_degrees=-52.0,
            roll_degrees=11.0,
            pan_x=23.0,
            pan_y=-17.0,
            zoom=1.7,
        )
        previous_center = (1.0, 2.0, 3.0)
        previous_radius = 12.0
        new_center = (8.0, -4.0, 9.0)
        new_radius = 31.0
        point = (5.0, 7.0, -2.0)
        viewport_height = 600.0

        def projected(center, radius):
            rotation = _camera_rotation_matrix(
                camera.yaw_degrees,
                camera.pitch_degrees,
                camera.roll_degrees,
            )
            relative = tuple(
                point[axis] - center[axis] for axis in range(3)
            )
            rotated = tuple(
                sum(rotation[row][column] * relative[column]
                    for column in range(3))
                for row in range(3)
            )
            scale = viewport_height * 0.5 / radius * camera.zoom
            return (
                camera.pan_x + rotated[0] * scale,
                camera.pan_y - rotated[1] * scale,
                scale,
            )

        before = projected(previous_center, previous_radius)
        preserve_camera_for_scene_bounds(
            camera,
            previous_center,
            previous_radius,
            new_center,
            new_radius,
            viewport_height,
        )
        after = projected(new_center, new_radius)

        for actual, expected in zip(after, before):
            self.assertAlmostEqual(actual, expected)

    def test_three_independent_assembly_mates_lock_full_transform(self) -> None:
        rows = [
            {"target": "x", "orientation": True},
            {"target": "y", "orientation": True},
            {"target": "z", "orientation": False},
        ]
        normals = {
            "x": (1.0, 0.0, 0.0),
            "y": (0.0, 1.0, 0.0),
            "z": (0.0, 0.0, 1.0),
        }

        self.assertEqual(
            AssemblyComponentPropertiesDialog._mate_transform_locks(
                rows,
                normals,
            ),
            (True, True),
        )

    def test_partial_assembly_mates_leave_transform_editable(self) -> None:
        rows = [{"target": "z", "orientation": True}]

        self.assertEqual(
            AssemblyComponentPropertiesDialog._mate_transform_locks(
                rows,
                {"z": (0.0, 0.0, 1.0)},
            ),
            (False, False),
        )

    def test_unresolved_assembly_mate_rows_are_retained(self) -> None:
        unresolved = {
            "source": "assembly-face-ref:source",
            "target": "assembly-face-ref:target",
            "type": "plane",
        }
        self.assertEqual(
            AssemblyComponentPropertiesDialog._retained_mate_rows([
                unresolved,
                {"source": "incomplete"},
                "invalid",
            ]),
            [unresolved],
        )

    def test_assembly_edge_descriptor_is_an_axis_mate_reference(self) -> None:
        descriptor = "assembly-edge-ref:stable-edge"
        self.assertTrue(
            AssemblyComponentPropertiesDialog._is_axis_reference(descriptor)
        )
        self.assertTrue(
            AssemblyComponentPropertiesDialog._reference_matches_mate_type(
                descriptor, "axis"
            )
        )
        self.assertFalse(
            AssemblyComponentPropertiesDialog._reference_matches_mate_type(
                descriptor, "plane"
            )
        )

    def test_shaded_projection_contains_model_surface_triangles(self) -> None:
        mesh = triangulate_shape(
            BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        )

        triangles = shaded_projection(
            [(mesh, "#B9C2CC")],
            "isometric",
        )

        self.assertEqual(len(triangles), mesh.triangle_count)
        self.assertTrue(all(len(item["points"]) == 3 for item in triangles))
        self.assertTrue(all(item["color"] == "#B9C2CC" for item in triangles))
        self.assertTrue(all(
            len(item["vertex_brightness"]) == 3 for item in triangles
        ))
        self.assertTrue(all(
            len(item["vertex_depths"]) == 3 for item in triangles
        ))
        self.assertTrue(all(
            0.42 <= brightness <= 1.0
            for item in triangles
            for brightness in item["vertex_brightness"]
        ))

        colored = shaded_projection(
            [(mesh, {"": "#C04020"})],
            "isometric",
        )
        self.assertTrue(all(item["color"] == "#C04020" for item in colored))

    def test_model_edges_are_classified_by_topology(self) -> None:
        box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        self.assertEqual(
            {edge.topology_role for edge in triangulate_shape(box).edges},
            {"sharp"},
        )

        face_explorer = TopExp_Explorer(box, TopAbs_FACE)
        face_mesh = triangulate_shape(face_explorer.Current())
        self.assertEqual(
            {edge.topology_role for edge in face_mesh.edges},
            {"boundary"},
        )

        cylinder = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        cylinder_roles = {
            edge.topology_role for edge in triangulate_shape(cylinder).edges
        }
        self.assertIn("seam", cylinder_roles)
        self.assertIn("sharp", cylinder_roles)

    def test_edge_visibility_matches_display_mode(self) -> None:
        def edge(role: str) -> EdgePolyline:
            return EdgePolyline(
                edge_index=1,
                points=((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
                topology_role=role,
            )

        self.assertTrue(edge_visible_in_display(edge("boundary"), "wire"))
        self.assertTrue(edge_visible_in_display(edge("tangent"), "wire"))
        self.assertTrue(edge_visible_in_display(edge("seam"), "wire"))
        self.assertTrue(
            edge_visible_in_display(edge("seam"), "shaded_with_edges")
        )
        self.assertFalse(
            edge_visible_in_display(edge("periodic_tangent"), "wire")
        )
        self.assertFalse(
            edge_visible_in_display(edge("tangent"), "shaded_with_edges")
        )
        self.assertTrue(
            edge_visible_in_display(edge("sharp"), "shaded_with_edges")
        )
        self.assertFalse(edge_visible_in_display(edge("sharp"), "shaded"))

    def test_exact_tangent_facet_remains_a_silhouette(self) -> None:
        tangent = SilhouetteEdge(
            first=(0.0, 0.0, 0.0),
            second=(1.0, 0.0, 0.0),
            adjacent_normals=((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
        )
        self.assertEqual(
            silhouette_segments_from_edges((tangent,), (0.0, 1.0, 0.0)),
            ((tangent.first, tangent.second),),
        )

    def test_curved_surface_produces_view_dependent_silhouette(self) -> None:
        cylinder = triangulate_shape(
            BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        )

        self.assertTrue(any(
            len({
                tuple(cylinder.triangle_normals[
                    offset + vertex * 3:offset + vertex * 3 + 3
                ])
                for vertex in range(3)
            }) > 1
            for offset in range(0, len(cylinder.triangle_normals), 9)
        ))

        self.assertGreater(
            len(silhouette_segments(cylinder, (1.0, 0.0, 0.0))),
            0,
        )
        self.assertEqual(
            silhouette_segments(cylinder, (0.0, 0.0, 1.0)),
            (),
        )
        oblique_segments = silhouette_segments(
            cylinder,
            (1.0, 0.7, 0.3),
        )
        self.assertEqual(len(oblique_segments), 2)
        self.assertTrue(all(
            abs(first[2] - second[2]) > 19.0
            for first, second in oblique_segments
        ))

    def test_topological_edges_are_not_duplicated_as_silhouettes(self) -> None:
        box = triangulate_shape(
            BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        )

        self.assertEqual(
            silhouette_segments(box, (1.0, 1.0, 1.0)),
            (),
        )

    def test_workspace_pen_is_one_pixel_and_cosmetic(self) -> None:
        pen = cosmetic_pen(QColor("#808080"))
        self.assertTrue(pen.isCosmetic())
        self.assertEqual(pen.widthF(), 1.0)

    def test_parallel_edge_dimension_uses_placement_coordinate(self) -> None:
        geometry = parallel_dimension_geometry(
            ((0.0, 0.0), (0.0, 10.0)),
            ((10.0, 0.0), (10.0, 10.0)),
            (4.0, 20.0),
        )
        self.assertIsNotNone(geometry)
        self.assertAlmostEqual(geometry["distance"], 10.0)
        self.assertEqual(geometry["first_point"], (0.0, 20.0))
        self.assertEqual(geometry["second_point"], (10.0, 20.0))

    def test_non_parallel_edges_cannot_form_basic_dimension(self) -> None:
        self.assertIsNone(parallel_dimension_geometry(
            ((0.0, 0.0), (10.0, 0.0)),
            ((0.0, 0.0), (0.0, 10.0)),
            (20.0, 20.0),
        ))

    def test_legacy_sheet_without_scale_defaults_to_one_to_one(self) -> None:
        document = create_empty_drawing()
        document.document_settings["drawing_sheets"] = json.dumps([
            {
                "id": "legacy-sheet",
                "name": "List 1",
                "format": "A4",
                "views": [],
            }
        ])

        sheet = drawing_sheets(document)[0]

        self.assertEqual(sheet["default_scale_numerator"], 1.0)
        self.assertEqual(sheet["default_scale"], 1.0)

    def test_deleting_drawing_view_removes_children_and_dimensions(self) -> None:
        sheet = {
            "views": [
                {"id": "parent"},
                {"id": "child", "parent_view_id": "parent"},
                {"id": "grandchild", "parent_view_id": "child"},
                {"id": "unrelated"},
            ],
            "dimensions": [
                {
                    "id": "dependent",
                    "first": {"view_id": "child"},
                    "second": {"view_id": "child"},
                },
                {
                    "id": "kept",
                    "first": {"view_id": "unrelated"},
                    "second": {"view_id": "unrelated"},
                },
            ],
        }

        removed = delete_drawing_view(sheet, "parent")

        self.assertEqual(removed, {"parent", "child", "grandchild"})
        self.assertEqual([view["id"] for view in sheet["views"]], ["unrelated"])
        self.assertEqual(
            [dimension["id"] for dimension in sheet["dimensions"]],
            ["kept"],
        )

    def test_moving_drawing_view_updates_position_and_bounds(self) -> None:
        sheet = {
            "views": [{
                "id": "view",
                "x": 10.0,
                "y": 20.0,
                "scale": 2.0,
                "model_extent": [2.0, 4.0],
                "caption_position": [12.0, 30.0],
            }],
            "dimensions": [{
                "first": {"view_id": "view"},
                "second": {"view_id": "view"},
                "placement": [15.0, 25.0],
            }],
        }

        self.assertTrue(move_drawing_view(sheet, "view", 50.0, 60.0))

        view = sheet["views"][0]
        self.assertEqual((view["x"], view["y"]), (50.0, 60.0))
        self.assertEqual(
            view["bounds"],
            {"left": 47.0, "right": 53.0, "bottom": 55.0, "top": 65.0},
        )
        self.assertEqual(view["caption_position"], [52.0, 70.0])
        self.assertEqual(
            sheet["dimensions"][0]["placement"], [55.0, 65.0]
        )

    def test_moving_parent_view_carries_projected_descendants(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 10.0, "y": 20.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "right",
                    "x": 40.0,
                    "y": 20.0,
                },
                {
                    "id": "grandchild",
                    "parent_view_id": "child",
                    "projection_direction": "top",
                    "x": 40.0,
                    "y": 50.0,
                },
                {"id": "unrelated", "x": 5.0, "y": 5.0},
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "parent", 20.0, 35.0))

        positions = {
            view["id"]: (view["x"], view["y"])
            for view in sheet["views"]
        }
        self.assertEqual(positions["parent"], (20.0, 35.0))
        self.assertEqual(positions["child"], (50.0, 35.0))
        self.assertEqual(positions["grandchild"], (50.0, 65.0))
        self.assertEqual(positions["unrelated"], (5.0, 5.0))

    def test_moving_projected_child_uses_only_its_free_axis(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 10.0, "y": 20.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "right",
                    "x": 40.0,
                    "y": 20.0,
                },
                {
                    "id": "grandchild",
                    "parent_view_id": "child",
                    "projection_direction": "top",
                    "x": 40.0,
                    "y": 50.0,
                },
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "child", 70.0, 90.0))

        positions = {
            view["id"]: (view["x"], view["y"])
            for view in sheet["views"]
        }
        self.assertEqual(positions["parent"], (10.0, 20.0))
        self.assertEqual(positions["child"], (70.0, 20.0))
        self.assertEqual(positions["grandchild"], (70.0, 50.0))

    def test_moving_diagonal_projection_stays_on_its_45_degree_ray(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 50.0, "y": 50.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "top_right",
                    "x": 30.0,
                    "y": 70.0,
                },
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "child", 10.0, 80.0))

        child = sheet["views"][1]
        self.assertAlmostEqual(50.0 - child["x"], child["y"] - 50.0)

    @staticmethod
    def projected_axis(
        orientation: str, endpoint: tuple[float, float, float]
    ) -> tuple[float, float]:
        line = project_polylines([[(0.0, 0.0, 0.0), endpoint]], orientation)[0]
        return line[1][0] - line[0][0], line[1][1] - line[0][1]

    def test_front_has_positive_x_left_and_positive_z_up(self) -> None:
        self.assertLess(self.projected_axis("front", (1.0, 0.0, 0.0))[0], 0.0)
        self.assertGreater(self.projected_axis("front", (0.0, 0.0, 1.0))[1], 0.0)

    def test_top_has_positive_x_left_and_positive_y_down(self) -> None:
        self.assertLess(self.projected_axis("top", (1.0, 0.0, 0.0))[0], 0.0)
        self.assertLess(self.projected_axis("top", (0.0, 1.0, 0.0))[1], 0.0)

    def test_right_has_positive_y_left_and_positive_z_up(self) -> None:
        self.assertLess(self.projected_axis("right", (0.0, 1.0, 0.0))[0], 0.0)
        self.assertGreater(self.projected_axis("right", (0.0, 0.0, 1.0))[1], 0.0)

    def test_default_camera_uses_reversed_isometric_side(self) -> None:
        self.assertEqual(
            (CameraState().yaw_degrees, CameraState().pitch_degrees),
            STANDARD_VIEW_ORIENTATIONS["default"],
        )

    def test_standard_orthographic_camera_convention(self) -> None:
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["front"], (180.0, -90.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["back"], (0.0, -90.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["top"], (180.0, 0.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["bottom"], (180.0, 180.0))

    def test_projected_view_uses_sheet_projection_method(self) -> None:
        expectations = {
            ("third_angle", "right"): "right",
            ("third_angle", "left"): "left",
            ("third_angle", "top"): "top",
            ("third_angle", "bottom"): "bottom",
            ("first_angle", "right"): "left",
            ("first_angle", "left"): "right",
            ("first_angle", "top"): "bottom",
            ("first_angle", "bottom"): "top",
        }
        for (method, direction), expected_name in expectations.items():
            actual = projection_axes(projected_view_orientation(
                "front", direction, method
            ))
            expected = projection_axes(expected_name)
            for actual_axis, expected_axis in zip(actual, expected):
                for value, expected_value in zip(actual_axis, expected_axis):
                    self.assertAlmostEqual(value, expected_value, places=6)

    def test_projected_view_rotates_relative_to_parent(self) -> None:
        parent = projection_axes("isometric")
        actual = projection_axes(projected_view_orientation(
            "isometric", "right", "third_angle"
        ))
        expected = (
            tuple(-value for value in parent[2]),
            parent[1],
            parent[0],
        )
        for actual_axis, expected_axis in zip(actual, expected):
            for value, expected_value in zip(actual_axis, expected_axis):
                self.assertAlmostEqual(value, expected_value, places=6)

    def test_diagonal_projected_view_folds_parent_by_ninety_degrees(self) -> None:
        parent = projection_axes("front")
        actual = projection_axes(projected_view_orientation(
            "front", "top_right", "third_angle"
        ))
        diagonal = 2.0 ** -0.5
        expected_depth = tuple(
            diagonal * parent[0][axis] + diagonal * parent[1][axis]
            for axis in range(3)
        )
        for value, expected in zip(actual[2], expected_depth):
            self.assertAlmostEqual(value, expected, places=6)
        for axis in actual:
            self.assertAlmostEqual(
                sum(value * value for value in axis), 1.0, places=6
            )
        self.assertAlmostEqual(
            sum(actual[0][i] * actual[1][i] for i in range(3)),
            0.0,
            places=6,
        )

    def test_orbit_camera_remains_continuous_when_upside_down(self) -> None:
        camera = CameraState(
            yaw_degrees=25.0,
            pitch_degrees=180.0,
            roll_degrees=0.0,
        )
        before = _camera_rotation_matrix(
            camera.yaw_degrees,
            camera.pitch_degrees,
            camera.roll_degrees,
        )
        orbit_camera_state(camera, 5.0, 0.0)
        after = _camera_rotation_matrix(
            camera.yaw_degrees,
            camera.pitch_degrees,
            camera.roll_degrees,
        )

        # A horizontal drag is always a rotation around the screen Y axis,
        # even after the camera has crossed 90° and is upside down.
        angle = radians(5.0)
        expected = _multiply_rotation_matrices(
            (
                (cos(angle), 0.0, sin(angle)),
                (0.0, 1.0, 0.0),
                (-sin(angle), 0.0, cos(angle)),
            ),
            before,
        )
        for actual_row, expected_row in zip(after, expected):
            for actual, expected_value in zip(actual_row, expected_row):
                self.assertAlmostEqual(actual, expected_value, places=6)

    def test_orbit_camera_preserves_rotation_through_euler_conversion(self) -> None:
        camera = CameraState(
            yaw_degrees=215.264,
            pitch_degrees=-89.9,
            roll_degrees=0.0,
        )
        for _step in range(20):
            orbit_camera_state(camera, 3.0, 2.0)
            matrix = _camera_rotation_matrix(
                camera.yaw_degrees,
                camera.pitch_degrees,
                camera.roll_degrees,
            )
            for row in matrix:
                self.assertAlmostEqual(
                    sum(value * value for value in row), 1.0, places=6
                )

    def test_view_bounds_follow_position_and_scale(self) -> None:
        view = {
            "x": 100.0,
            "y": 80.0,
            "scale": 2.0,
            "model_extent": [10.0, 6.0],
        }
        self.assertEqual(
            update_view_bounds(view),
            {"left": 89.0, "right": 111.0, "bottom": 73.0, "top": 87.0},
        )

    def test_drawing_scale_caption_uses_ratio_notation(self) -> None:
        self.assertEqual(drawing_scale_text(1.0), "M1:1")
        self.assertEqual(drawing_scale_text(0.5), "M1:2")
        self.assertEqual(drawing_scale_text(2.0), "M2:1")

    def test_named_camera_orientation_projects_with_roll(self) -> None:
        projected = project_polylines(
            [[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)]],
            {"yaw_degrees": 0.0, "pitch_degrees": 0.0, "roll_degrees": 90.0},
        )[0]
        self.assertAlmostEqual(projected[1][0] - projected[0][0], 0.0, places=6)
        self.assertGreater(projected[1][1] - projected[0][1], 0.0)

    def test_technical_projection_classifies_box_edges(self) -> None:
        shape = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        wire_polylines = [
            list(edge.points) for edge in triangulate_shape(shape).edges
        ]

        projection = technical_projection([shape], wire_polylines, "front")

        self.assertTrue(projection["wireframe_polylines"])
        self.assertEqual(len(projection["polylines"]), 1)
        for first, last in zip(
            projection["polylines"][0][0],
            projection["polylines"][0][-1],
        ):
            self.assertAlmostEqual(first, last)
        # Rear box edges coincide exactly with the visible front outline and
        # must not leave a dashed copy underneath it.
        self.assertFalse(projection["hidden_polylines"])

    def test_drawing_wireframe_uses_filtered_cylinder_edges_and_outlines(
        self,
    ) -> None:
        shape = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        mesh = triangulate_shape(shape)
        wire_polylines = [
            list(edge.points)
            for edge in mesh.edges
            if edge_visible_in_display(edge, "wire")
        ]

        projection = technical_projection(
            [shape],
            wire_polylines,
            "isometric",
        )

        # Two circular topology edges plus view-dependent silhouette outlines.
        self.assertGreater(len(projection["wireframe_polylines"]), 2)
        self.assertGreaterEqual(
            sum(len(line) for line in projection["wireframe_polylines"]),
            sum(len(line) for line in wire_polylines),
        )
        self.assertTrue(projection["polylines"])
        self.assertTrue(projection["hidden_polylines"])
        self.assertLessEqual(len(projection["polylines"]), 5)
        self.assertLessEqual(len(projection["hidden_polylines"]), 2)

    def test_cylinder_edges_share_surface_triangulation_nodes(self) -> None:
        mesh = triangulate_shape(BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape())
        surface_nodes = {
            tuple(round(mesh.triangle_positions[offset + axis], 7)
                  for axis in range(3))
            for offset in range(0, len(mesh.triangle_positions), 3)
        }
        circular_edges = [
            edge for edge in mesh.edges
            if len(edge.points) > 2 and edge.topology_role == "sharp"
        ]

        self.assertEqual(len(circular_edges), 2)
        for edge in circular_edges:
            self.assertTrue(all(
                tuple(round(value, 7) for value in point) in surface_nodes
                for point in edge.points
            ))

    def test_new_direct_sketch_defaults_to_front_xz_plane(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id)
        self.assertIsNotNone(sketch)
        self.assertEqual(sketch.parameters["plane"], "xz")

    def test_plane_normal_selects_nearest_global_local_plane(self) -> None:
        plane_for = MainWindow._local_plane_for_normal
        self.assertEqual(
            plane_for((1.0, 0.0, 0.0), "xz"),
            ("yz", "left"),
        )
        self.assertEqual(
            plane_for((0.0, -1.0, 0.0), "yz"),
            ("xz", "normal"),
        )
        self.assertEqual(
            plane_for((0.0, 0.0, -1.0), "yz"),
            ("xy", "up"),
        )

    def test_xy_front_and_xz_up_keep_distinct_datum_axes(self) -> None:
        references = [
            {"entity_id": "xy", "orientation_role": "normal"},
            {"entity_id": "xz", "orientation_role": "up"},
        ]

        remapped = MainWindow._datum_frame_references(
            references, primary_index=0, normal_role="up"
        )

        self.assertEqual(remapped[0]["orientation_role"], "up")
        self.assertEqual(remapped[1]["orientation_role"], "normal")
        self.assertEqual(references[0]["orientation_role"], "normal")
        self.assertEqual(references[1]["orientation_role"], "up")

    def test_xy_back_and_xz_down_preserve_secondary_sign(self) -> None:
        references = [
            {"entity_id": "xy", "orientation_role": "opposite_normal"},
            {"entity_id": "xz", "orientation_role": "down"},
        ]

        remapped = MainWindow._datum_frame_references(
            references, primary_index=0, normal_role="up"
        )

        self.assertEqual(remapped[0]["orientation_role"], "down")
        self.assertEqual(
            remapped[1]["orientation_role"], "opposite_normal"
        )

    def test_xy_protrusion_dimension_follows_global_z(self) -> None:
        extrusion, offset = MainWindow._protrusion_dimension_axes("xy")
        self.assertEqual(extrusion, (0.0, 0.0, 1.0))
        self.assertEqual(offset, (1.0, 0.0, 0.0))

    def test_equal_circle_group_copies_driver_radius(self) -> None:
        entities = [
            {
                "id": "g1",
                "type": "circle",
                "radius": 10.0,
                "equal_radius_group": "equal-circle:g1",
            },
            {
                "id": "g2",
                "type": "circle",
                "radius": 12.5,
                "equal_radius_group": "equal-circle:g1",
                "equal_radius_reference": True,
            },
        ]

        MainWindow._apply_sketch_equal_circle_radii(entities)

        self.assertEqual(entities[0]["radius"], 10.0)
        self.assertEqual(entities[1]["radius"], 10.0)

    def test_distance_seed_moves_only_the_freer_endpoint(self) -> None:
        window = MainWindow.__new__(MainWindow)
        fixed = {
            "type": "point",
            "id": "fixed",
            "x": 0.0,
            "y": 0.0,
            "constraints": [{
                "type": "point_on_reference",
                "reference_id": "sketch_origin",
            }],
        }
        free = {
            "type": "point",
            "id": "free",
            "x": 3.0,
            "y": 4.0,
        }
        dimension = {
            "type": "distance",
            "point_ids": ["fixed", "free"],
            "value": 10.0,
            "driving": True,
        }

        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 6.0)
        self.assertAlmostEqual(free["y"], 8.0)

        dimension["point_ids"] = ["free", "fixed"]
        dimension["value"] = 5.0
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 3.0)
        self.assertAlmostEqual(free["y"], 4.0)

        free["dimension_locks"] = ["x", "y"]
        dimension["value"] = 20.0
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 3.0)
        self.assertAlmostEqual(free["y"], 4.0)

    def test_axis_distance_seed_respects_measured_coordinate_lock(self) -> None:
        window = MainWindow.__new__(MainWindow)
        first = {
            "type": "point",
            "id": "first",
            "x": 2.0,
            "y": 3.0,
        }
        second = {
            "type": "point",
            "id": "second",
            "x": 7.0,
            "y": 9.0,
            "dimension_locks": ["x"],
        }

        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [first, second],
            [{
                "type": "distance_x",
                "point_ids": ["first", "second"],
                "value": 12.0,
                "driving": True,
            }],
        )

        self.assertEqual((second["x"], second["y"]), (7.0, 9.0))
        self.assertEqual((first["x"], first["y"]), (-5.0, 3.0))

        second.pop("dimension_locks")
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [first, second],
            [{
                "type": "distance_x",
                "point_ids": ["first", "second"],
                "value": -4.0,
                "driving": True,
            }],
        )

        self.assertEqual((first["x"], first["y"]), (-5.0, 3.0))
        self.assertEqual((second["x"], second["y"]), (-9.0, 9.0))

    def test_negative_ordinary_distance_seed_flips_free_side(self) -> None:
        window = MainWindow.__new__(MainWindow)
        anchored = {
            "type": "point",
            "id": "anchored",
            "x": 0.0,
            "y": 0.0,
            "dimension_locks": ["x", "y"],
        }
        free = {
            "type": "point",
            "id": "free",
            "x": 60.0,
            "y": 80.0,
        }

        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [anchored, free],
            [{
                "type": "distance",
                "point_ids": ["anchored", "free"],
                "value": -100.0,
                "driving": True,
            }],
        )

        self.assertEqual((anchored["x"], anchored["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], -60.0)
        self.assertAlmostEqual(free["y"], -80.0)

    def test_dimension_cleanup_keeps_properties_reference_picking_enabled(
        self,
    ) -> None:
        enabled = []
        window = MainWindow.__new__(MainWindow)
        window._dimension_overlays = {}
        window._dimension_bindings = {}
        window._dimension_owner_ids = {}
        window._dimension_object_id = None
        window._dimension_inspection_visuals = False
        window._dimension_selection_suspended = True
        window.view_selection_enabled = False
        window.application_selection_action = None
        window.point_constraint_dialog = SimpleNamespace(
            isVisible=lambda: True
        )
        window.selection_filter_combo = SimpleNamespace(
            setEnabled=lambda value: enabled.append(("filter", value))
        )
        window.native_viewer = SimpleNamespace(
            set_dimensions=lambda _values: None,
            set_extent_handles=lambda _values: None,
            set_dimension_inspection_active=lambda _active: None,
            set_selection_enabled=lambda value: enabled.append(
                ("viewer", value)
            ),
            set_sketch_overlay=lambda _mesh: None,
            set_passive_sketch_overlay=lambda _mesh: None,
            set_object_overlay=lambda _mesh: None,
        )

        window._clear_dimension_overlays()

        self.assertIn(("viewer", True), enabled)
        self.assertIn(("filter", True), enabled)

    def test_new_command_ends_body_dimension_inspection(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._dimension_inspection_visuals = True
        window._dimension_selection_suspended = True
        window._dimension_overlays = {"length": object()}
        dismissed = []
        window._dismiss_dimension_overlays = lambda: dismissed.append(True)

        window._end_dimension_inspection_for_command()

        self.assertEqual(dismissed, [True])

    def test_coordinate_dependencies_are_evaluated_per_direction(self) -> None:
        entities = [
            {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
            {"type": "point", "id": "p2", "x": 5.0, "y": 0.0},
            {
                "type": "segment",
                "id": "g1",
                "point_ids": ["p1", "p2"],
                "constraints": [{"type": "horizontal"}],
            },
        ]

        self.assertFalse(MainWindow._sketch_coordinate_has_dependencies(
            entities, [], "p1", "x"
        ))
        self.assertTrue(MainWindow._sketch_coordinate_has_dependencies(
            entities, [], "p1", "y"
        ))
        self.assertFalse(MainWindow._sketch_coordinate_has_dependencies(
            entities,
            [{
                "type": "distance_y",
                "point_ids": ["p1", "p2"],
            }],
            "p1",
            "x",
        ))
        self.assertTrue(MainWindow._sketch_coordinate_has_dependencies(
            entities,
            [{
                "type": "distance_x",
                "point_ids": ["p1", "p2"],
            }],
            "p1",
            "x",
        ))

    def test_point_line_dimension_placement_slides_along_line(self) -> None:
        first_dimension, second_dimension = (
            MainWindow._point_line_dimension_placement(
                point=(5.0, 10.0),
                projection=(5.0, 0.0),
                line_first=(0.0, 0.0),
                line_second=(10.0, 0.0),
                placement=(8.0, 20.0),
            )
        )

        self.assertEqual(first_dimension, (8.0, 0.0))
        self.assertEqual(second_dimension, (8.0, 10.0))

    def test_xz_sketch_geometry_is_mapped_without_owner_rotation(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id, plane="xz")
        model = SketchModel.from_editor_data(
            [
                {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
                {"type": "point", "id": "p2", "x": 10.0, "y": 20.0},
                {
                    "type": "segment",
                    "id": "g1",
                    "point_ids": ["p1", "p2"],
                },
            ],
            [],
        )
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        shape = make_sketch_shape(owner, sketch)
        self.assertIsNotNone(shape)
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        xmin, ymin, zmin, xmax, ymax, zmax = bounds.Get()
        self.assertAlmostEqual(xmin, 0.0, places=6)
        self.assertAlmostEqual(xmax, 10.0, places=6)
        self.assertAlmostEqual(ymin, 0.0, places=6)
        self.assertAlmostEqual(ymax, 0.0, places=6)
        self.assertAlmostEqual(zmin, 0.0, places=6)
        self.assertAlmostEqual(zmax, 20.0, places=6)

    def test_persisted_sketch_profile_mesh_needs_no_occt_shape(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id, plane="xz")
        model = SketchModel.from_editor_data(
            [
                {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
                {"type": "point", "id": "p2", "x": 10.0, "y": 20.0},
                {
                    "type": "segment",
                    "id": "g1",
                    "point_ids": ["p1", "p2"],
                },
            ],
            [],
        )
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        window = MainWindow.__new__(MainWindow)

        with patch(
            "zima_cad.model.make_sketch_shape",
            side_effect=AssertionError("OCCT sketch construction is forbidden"),
        ), patch(
            "zima_cad.app.triangulate_shape",
            side_effect=AssertionError("OCCT triangulation is forbidden"),
        ):
            mesh = window._persisted_sketch_profile_mesh(
                sketch,
                coordinate_system_transform(owner.coordinate_system),
            )

        self.assertIsNotNone(mesh)
        self.assertEqual(len(mesh.edges), 1)
        self.assertEqual(mesh.edges[0].points[0], (0.0, 0.0, 0.0))
        self.assertEqual(mesh.edges[0].points[1], (10.0, 0.0, 20.0))
        self.assertEqual(mesh.triangle_positions, ())

    def test_thin_profile_preview_uses_offset_boundaries_and_end_caps(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id, plane="xz")
        model = SketchModel.from_editor_data([
            {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
            {"type": "point", "id": "p2", "x": 20.0, "y": 0.0},
            {"type": "point", "id": "p3", "x": 20.0, "y": -10.0},
            {
                "type": "segment", "id": "g1",
                "point_ids": ["p1", "p2"],
            },
            {
                "type": "segment", "id": "g2",
                "point_ids": ["p2", "p3"],
            },
        ], [])
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        window = MainWindow.__new__(MainWindow)

        mesh = window._persisted_thin_profile_mesh(
            sketch,
            coordinate_system_transform(owner.coordinate_system),
            2.0,
            "symmetric",
        )

        self.assertIsNotNone(mesh)
        self.assertEqual(len(mesh.edges), 6)
        positions = {
            tuple(round(value, 6) for value in point)
            for edge in mesh.edges for point in edge.points
        }
        self.assertEqual(
            positions,
            {
                (0.0, 0.0, -1.0), (19.0, 0.0, -1.0),
                (19.0, 0.0, -10.0),
                (0.0, 0.0, 1.0), (21.0, 0.0, 1.0),
                (21.0, 0.0, -10.0),
            },
        )

    def test_closed_thin_preview_contains_inner_and_outer_loops(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id, plane="xz")
        model = SketchModel.from_editor_data([
            {"type": "point", "id": "a", "x": 0.0, "y": 0.0},
            {"type": "point", "id": "b", "x": 20.0, "y": 0.0},
            {"type": "point", "id": "c", "x": 20.0, "y": 10.0},
            {"type": "point", "id": "d", "x": 0.0, "y": 10.0},
            {"type": "segment", "id": "ab", "point_ids": ["a", "b"]},
            {"type": "segment", "id": "bc", "point_ids": ["b", "c"]},
            {"type": "segment", "id": "cd", "point_ids": ["c", "d"]},
            {"type": "segment", "id": "da", "point_ids": ["d", "a"]},
        ], [])
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        window = MainWindow.__new__(MainWindow)

        mesh = window._persisted_thin_profile_mesh(
            sketch,
            coordinate_system_transform(owner.coordinate_system),
            2.0,
            "symmetric",
        )

        self.assertIsNotNone(mesh)
        self.assertEqual(len(mesh.edges), 8)
        self.assertEqual(len({point for edge in mesh.edges for point in edge.points}), 8)
        window.document = document
        for mode in ("one_side", "other_side", "symmetric"):
            dialog = SimpleNamespace(
                _profile_sketch_id="",
                _profile_source="internal",
                point_object=owner,
                result_type_combo=SimpleNamespace(currentData=lambda: "thin"),
                thin_thickness_spin=SimpleNamespace(value=lambda: 2.0),
                thin_mode_combo=SimpleNamespace(
                    currentData=lambda selected=mode: selected
                ),
            )
            refreshed = window._new_protrusion_profile_mesh(
                dialog, CoordinateSystem()
            )
            self.assertIsNotNone(refreshed, mode)
            self.assertEqual(len(refreshed.edges), 8, mode)

    def test_straight_chamfer_dimension_uses_the_real_short_chord(self) -> None:
        curves = (
            CurveDescriptor(
                "long-a", "other", ((0.0, 0.0, 0.0), (0.0, 100.0, 0.0))
            ),
            CurveDescriptor(
                "short", "other", ((0.0, 0.0, 0.0), (20.0, 0.0, 20.0))
            ),
            CurveDescriptor(
                "long-b", "other", ((20.0, 0.0, 20.0), (20.0, 100.0, 20.0))
            ),
        )
        window = MainWindow.__new__(MainWindow)

        witnesses = window._edge_treatment_rim_points(
            None, curves, 20.0 * 2.0 ** 0.5
        )

        self.assertIsNotNone(witnesses)
        self.assertAlmostEqual(
            float(((witnesses[1] - witnesses[0]) ** 2).sum()) ** 0.5,
            20.0 * 2.0 ** 0.5,
            places=7,
        )

    def test_straight_chamfer_extension_follows_an_adjacent_face(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._world_transform_for_object = lambda _container: (
            (1.0, 0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0, 0.0),
            (0.0, 0.0, 1.0, 0.0),
            (0.0, 0.0, 0.0, 1.0),
        )

        directions = window._straight_chamfer_dimension_directions(
            SimpleNamespace(),
            (
                np.asarray((0.0, 0.0, 0.0)),
                np.asarray((20.0, 0.0, 20.0)),
            ),
            np.asarray((0.0, 1.0, 0.0)),
        )

        self.assertIsNotNone(directions)
        chamfer, extension = directions
        self.assertAlmostEqual(abs(float(chamfer @ extension)), 2.0 ** -0.5)
        self.assertAlmostEqual(abs(float(extension[0] * extension[2])), 0.0)


if __name__ == "__main__":
    unittest.main()
