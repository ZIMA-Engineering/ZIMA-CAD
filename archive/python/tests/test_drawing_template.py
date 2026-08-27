import json
import tempfile
import unittest
from pathlib import Path

from zima_cad.drawing_template import (
    create_empty_drawing_template,
    load_drawing_template,
    save_drawing_template,
    template_sketch,
)
from zima_cad.sketch_model import SketchModel


class DrawingTemplateTests(unittest.TestCase):
    def test_native_formats_open_as_editable_sketches(self) -> None:
        for name, expected_type in (
            ("ZE-A4.frmz", "drawing_format"),
            ("ZE-RAZITKO.tblz", "title_block"),
        ):
            with self.subTest(name=name):
                document = load_drawing_template(Path("config/formats") / name)
                self.assertEqual(document.document_settings["type"], expected_type)
                sketch = template_sketch(document)
                self.assertEqual(sketch.parameters["template_editor"], "true")
                model = SketchModel.from_dict(json.loads(sketch.parameters["sketch_data"]))
                self.assertTrue(model.geometry or model.points)
                self.assertFalse(any(
                    point.attributes.get("text_role") == "outline_point"
                    for point in model.points.values()
                ))
                self.assertFalse(any(
                    geometry.attributes.get("text_role") == "outline"
                    for geometry in model.geometry.values()
                ))

    def test_creates_blank_frame_and_title_block_templates(self) -> None:
        for template_type, expected_width in (
            ("drawing_format", "297"),
            ("title_block", "180"),
        ):
            with self.subTest(template_type=template_type):
                document = create_empty_drawing_template(
                    template_type,
                    "NEW",
                )
                self.assertEqual(
                    document.document_settings["type"],
                    template_type,
                )
                self.assertEqual(
                    document.document_settings["template_coordinate_width"],
                    expected_width,
                )
                model = SketchModel.from_dict(json.loads(
                    template_sketch(document).parameters["sketch_data"]
                ))
                self.assertFalse(model.points)
                self.assertFalse(model.geometry)

    def test_save_is_versioned_and_round_trips_sketch_data(self) -> None:
        document = load_drawing_template(Path("config/formats/ZE-A4.frmz"))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "frame.frmz"
            save_drawing_template(document, target)
            save_drawing_template(document, target)
            self.assertTrue(target.is_file())
            self.assertTrue(target.with_name("frame.frmz.1").is_file())
            reloaded = load_drawing_template(target)
            self.assertEqual(
                template_sketch(reloaded).parameters["sketch_data"],
                template_sketch(document).parameters["sketch_data"],
            )

    def test_title_block_save_is_versioned(self) -> None:
        document = load_drawing_template(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "block.tblz"
            save_drawing_template(document, target)
            save_drawing_template(document, target)
            self.assertTrue(target.with_name("block.tblz.1").is_file())

    def test_title_block_fields_are_editable_reference_codes(self) -> None:
        document = load_drawing_template(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        model = SketchModel.from_dict(json.loads(
            template_sketch(document).parameters["sketch_data"]
        ))
        fields = {
            point.attributes.get("template_field_id"): point
            for point in model.points.values()
            if point.attributes.get("template_field_id")
        }
        self.assertEqual(fields["DRAWN_BY"].attributes["text_value"], "&Kreslil")
        self.assertEqual(
            fields["DOCUMENT_NUMBER"].attributes["text_value"],
            "&document.file_stem.&Verze",
        )
        self.assertTrue(model.constraints)
        self.assertTrue(all(
            constraint.constraint_type in ("horizontal", "vertical")
            for constraint in model.constraints.values()
        ))
        self.assertIn("coordinate:p92:x", model.dimensions)
        self.assertIn("coordinate:p92:y", model.dimensions)

    def test_title_block_field_with_multiple_tokens_round_trips_as_text(self) -> None:
        document = load_drawing_template(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        model = SketchModel.from_dict(json.loads(
            template_sketch(document).parameters["sketch_data"]
        ))
        field = next(
            point for point in model.points.values()
            if point.attributes.get("template_field_id") == "DOCUMENT_NUMBER"
        )
        field.attributes["text_value"] = (
            "&document.file_stem / &model.revision / &drawing.edice"
        )
        template_sketch(document).parameters["sketch_data"] = json.dumps(
            model.to_dict(), ensure_ascii=False
        )
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "block.tblz"
            save_drawing_template(document, target)
            text = target.read_text(encoding="utf-8")
            self.assertIn(
                "Text = &document.file_stem / &model.revision / &drawing.edice",
                text,
            )
            reloaded = load_drawing_template(target)
            reloaded_model = SketchModel.from_dict(json.loads(
                template_sketch(reloaded).parameters["sketch_data"]
            ))
            reloaded_field = next(
                point for point in reloaded_model.points.values()
                if point.attributes.get("template_field_id")
                == "DOCUMENT_NUMBER"
            )
            self.assertEqual(
                reloaded_field.attributes["text_value"],
                "&document.file_stem / &model.revision / &drawing.edice",
            )

    def test_deleted_title_block_field_does_not_return_after_reopen(self) -> None:
        document = load_drawing_template(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        sketch = template_sketch(document)
        model = SketchModel.from_dict(json.loads(sketch.parameters["sketch_data"]))
        deleted_ids = [
            point_id
            for point_id, point in model.points.items()
            if point.attributes.get("template_field_id") == "DATE"
        ]
        self.assertEqual(len(deleted_ids), 1)
        model.points.pop(deleted_ids[0])
        sketch.parameters["sketch_data"] = json.dumps(
            model.to_dict(), ensure_ascii=False
        )

        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "block.tblz"
            save_drawing_template(document, target)
            self.assertNotIn("[Field.DATE]", target.read_text(encoding="utf-8"))
            reopened = load_drawing_template(target)
            reopened_model = SketchModel.from_dict(json.loads(
                template_sketch(reopened).parameters["sketch_data"]
            ))
            self.assertFalse(any(
                point.attributes.get("template_field_id") == "DATE"
                for point in reopened_model.points.values()
            ))

    def test_field_box_coordinates_survive_sketch_round_trip(self) -> None:
        document = load_drawing_template(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "block.tblz"
            save_drawing_template(document, target)
            text = target.read_text(encoding="utf-8")
            # The field includes the title block's own 10 mm insertion offset.
            self.assertIn("X = 91.5", text)
            self.assertIn("BoxWidth = 32.5", text)

    def test_frame_labels_use_centered_anchors(self) -> None:
        document = load_drawing_template(Path("config/formats/ZE-A4.frmz"))
        model = SketchModel.from_dict(json.loads(
            template_sketch(document).parameters["sketch_data"]
        ))
        labels = [
            point for point in model.points.values()
            if point.attributes.get("text_role") == "anchor"
        ]
        self.assertTrue(labels)
        self.assertTrue(all(
            point.attributes.get("text_horizontal") == "center"
            for point in labels
        ))

    def test_legacy_frame_coordinates_become_bottom_right_coordinates(self) -> None:
        document = load_drawing_template(Path("config/formats/ZE-A4.frmz"))
        model = SketchModel.from_dict(json.loads(
            template_sketch(document).parameters["sketch_data"]
        ))
        segment = next(iter(model.geometry.values()))
        first, second = (model.points[point_id] for point_id in segment.point_ids)
        self.assertAlmostEqual(first.x, 195.0)
        self.assertAlmostEqual(second.x, 5.15)
        self.assertEqual(
            document.document_settings["template_coordinate_system"],
            "bottom_right",
        )


if __name__ == "__main__":
    unittest.main()
