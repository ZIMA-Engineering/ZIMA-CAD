import unittest
import json

from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib

from zima_cad.app import MainWindow
from zima_cad.drawing import drawing_sheets, project_polylines
from zima_cad.model import (
    ContainerType,
    create_empty_drawing,
    create_empty_part,
    make_sketch_shape,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.viewer import CameraState, STANDARD_VIEW_ORIENTATIONS


class DrawingViewConventionTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
