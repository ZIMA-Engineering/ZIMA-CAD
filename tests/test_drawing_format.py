from pathlib import Path
import tempfile
import unittest

from zima_cad.drawing_format import load_drawing_format
from zima_cad.title_block import (
    load_title_block,
    resolve_title_block_text,
    title_block_token_scope,
    title_block_tokens,
)


class DrawingFormatTests(unittest.TestCase):
    def test_all_native_sheet_frames_load_without_title_blocks(self) -> None:
        for sheet_format in ("A0", "A1", "A2", "A3", "A4"):
            with self.subTest(sheet_format=sheet_format):
                definition = load_drawing_format(
                    Path(f"config/formats/ZE-{sheet_format}.frmz")
                )
                self.assertEqual(definition["sheet_format"], sheet_format)
                self.assertEqual(definition["document_type"], "any")
                self.assertFalse(definition["title_block"]["enabled"])
                geometry = definition["frame"]["geometry"]
                self.assertGreaterEqual(len(geometry), 32)
                self.assertFalse(any(
                    entity.get("text") == "ZIMA-Engineering"
                    for entity in geometry
                ))

    def test_loads_native_frame_geometry_without_title_block(self) -> None:
        source = Path("config/formats/ZE-A4.frmz")
        definition = load_drawing_format(source)

        self.assertEqual(definition["sheet_format"], "A4")
        self.assertEqual(definition["document_type"], "any")
        self.assertFalse(definition["title_block"]["enabled"])
        geometry = definition["frame"]["geometry"]
        self.assertEqual(len(geometry), 44)
        self.assertEqual(definition["coordinate_system"], "bottom_right")
        self.assertTrue(any(entity["pen"] == "GREEN" for entity in geometry))
        self.assertTrue(any(entity["pen"] == "WHITE" for entity in geometry))
        self.assertFalse(any(
            entity.get("text") == "ZIMA-Engineering"
            for entity in geometry
        ))

    def test_rejects_unknown_geometry_pen(self) -> None:
        content = """[Format]
SheetFormat = A4
Orientation = portrait
DocumentType = part
[Frame]
[FrameGeometry]
Line001 = 0, 0, 1, 1, BLUE
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.frmz"
            path.write_text(content, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsupported drawing pen"):
                load_drawing_format(path)

    def test_loads_static_reference_title_block(self) -> None:
        definition = load_title_block(
            Path("config/formats/ZE-RAZITKO.tblz")
        )
        self.assertEqual(definition["anchor"], "bottom-right")
        self.assertEqual(definition["width"], 180.0)
        self.assertEqual(definition["height"], 60.0)
        self.assertEqual(definition["coordinate_system"], "bottom_right")
        self.assertEqual(definition["content_origin"], (10.0, 10.0))
        self.assertTrue(any(
            entity.get("text") == "ZIMA-Engineering"
            for entity in definition["geometry"]
        ))
        self.assertEqual(
            sum(entity["kind"] == "circle" for entity in definition["geometry"]),
            2,
        )
        fields = {field["id"]: field for field in definition["fields"]}
        self.assertEqual(set(fields), {
            "DRAWN_BY", "APPROVED_BY", "DATE", "DOCUMENT_NUMBER",
            "SHEET_NUMBER", "NAME", "SCALE", "SHEET_FORMAT",
            "ACCURACY", "TOLERANCING",
            "ASSEMBLY_WEIGHT", "ASSEMBLY_QUANTITY",
        })
        self.assertEqual(fields["DRAWN_BY"]["text"], "&kreslil")
        self.assertEqual(fields["DRAWN_BY"]["default"], "-")
        self.assertEqual(fields["DRAWN_BY"]["align"], "right")
        self.assertEqual(fields["DRAWN_BY"]["offset_y"], -0.7)
        self.assertEqual(fields["APPROVED_BY"]["text"], "&schvalil")
        self.assertEqual(fields["APPROVED_BY"]["align"], "right")
        self.assertEqual(fields["APPROVED_BY"]["offset_y"], -0.5)
        self.assertEqual(fields["DATE"]["text"], "&datum")
        self.assertEqual(
            fields["DOCUMENT_NUMBER"]["text"],
            "&document.file_stem.&verze",
        )
        self.assertEqual(fields["SHEET_NUMBER"]["text"], "&sheet.position")
        self.assertEqual(fields["SHEET_NUMBER"]["format"], "{index}/{count}")
        self.assertEqual(fields["SHEET_NUMBER"]["align"], "left")
        self.assertEqual(fields["SHEET_NUMBER"]["vertical_align"], "bottom")
        self.assertFalse(fields["SHEET_NUMBER"]["editable"])
        self.assertFalse(fields["SHEET_NUMBER"]["write_back"])
        self.assertEqual(fields["NAME"]["text"], "&nazev")
        self.assertEqual(fields["NAME"]["pen"], "WHITE")
        self.assertEqual(fields["DOCUMENT_NUMBER"]["pen"], "WHITE")
        self.assertEqual(fields["NAME"]["height"], 5.0)
        self.assertEqual(fields["DOCUMENT_NUMBER"]["height"], 5.0)
        self.assertTrue(all(
            entity["height"] == 2.5
            for entity in definition["geometry"]
            if entity["kind"] == "text" and entity["pen"] != "WHITE"
        ))
        company_heading = next(
            entity for entity in definition["geometry"]
            if entity.get("text") == "ZIMA-Engineering"
        )
        self.assertEqual(company_heading["pen"], "WHITE")
        self.assertEqual(company_heading["height"], 5.0)
        self.assertTrue(all(
            field["height"] == 2.5
            for field in fields.values()
            if field["pen"] != "WHITE"
        ))
        self.assertTrue(fields["NAME"]["editable"])
        self.assertTrue(fields["NAME"]["write_back"])
        self.assertEqual(fields["DATE"]["align"], "right")
        self.assertEqual(fields["DATE"]["offset_y"], -0.3)
        self.assertEqual(fields["DATE"]["box_height"], 5.0)
        self.assertEqual(fields["DATE"]["x"], 91.5)
        self.assertEqual(fields["DATE"]["x"] + fields["DATE"]["box_width"], 137.5)
        self.assertEqual(fields["SCALE"]["text"], "&sheet.scale")
        self.assertEqual(fields["SCALE"]["pen"], "WHITE")
        self.assertEqual(fields["SCALE"]["format"], "M{numerator}:{denominator}")
        self.assertEqual(fields["SHEET_FORMAT"]["align"], "right")
        self.assertEqual(fields["ACCURACY"]["text"], "&presnost")
        self.assertEqual(fields["TOLERANCING"]["text"], "&tolerovani")
        self.assertEqual(
            fields["ASSEMBLY_WEIGHT"]["text"], "&hmotnost_sestavy"
        )
        self.assertEqual(
            fields["ASSEMBLY_QUANTITY"]["text"], "&mnozstvi_sestav"
        )
        static_texts = {
            entity.get("text") for entity in definition["geometry"]
        }
        self.assertIn("Schválil:", static_texts)
        self.assertIn("Chráněno podle ISO 16016", static_texts)
        self.assertIn("Název:", static_texts)
        self.assertIn("NÁZEV – OZNAČENÍ", static_texts)
        self.assertNotIn("TR51x5-40", static_texts)
        self.assertNotIn("CSN 42 5715", static_texts)
        white_lines = {
            (entity["x1"], entity["y1"], entity["x2"], entity["y2"])
            for entity in definition["geometry"]
            if entity["kind"] == "line" and entity["pen"] == "WHITE"
        }
        self.assertTrue({
            (91.0, 10.0, 91.0, 15.0),
            (91.0, 15.0, 91.0, 20.0),
            (91.0, 20.0, 91.0, 25.0),
            (91.0, 25.0, 91.0, 30.0),
        }.issubset(white_lines))
        self.assertTrue({
            (10.0, 10.0, 10.0, 20.0),
            (10.0, 20.0, 10.0, 30.0),
        }.issubset(white_lines))
        self.assertIn((10.0, 20.0, 91.0, 20.0), white_lines)
        self.assertIn(
            "e-mail: vladimir.zima@zima-engineering.cz", static_texts
        )

    def test_one_field_resolves_multiple_model_system_and_local_tokens(self) -> None:
        field = {
            "text": "Číslo &document.file_stem.&model.verze / &drawing.edice",
            "default": "-",
        }
        self.assertEqual(
            title_block_tokens(field["text"]),
            ("document.file_stem", "model.verze", "drawing.edice"),
        )
        self.assertEqual(title_block_token_scope("model.verze"), "model")
        self.assertEqual(title_block_token_scope("drawing.edice"), "drawing")
        self.assertEqual(title_block_token_scope("sheet.scale"), "system")
        self.assertEqual(
            resolve_title_block_text(
                field,
                context={
                    "file_stem": "ZE0019-0200-0001",
                    "parameters": {"verze": "03"},
                    "local_parameters": {"edice": "A"},
                },
                sheet={},
            ),
            "Číslo ZE0019-0200-0001.03 / A",
        )

    def test_czech_parameter_label_resolves_to_stable_model_key(self) -> None:
        text = "&Název / &Materiál / &Číslo_položky"
        self.assertEqual(
            title_block_tokens(text),
            ("Název", "Materiál", "Číslo_položky"),
        )
        self.assertEqual(
            resolve_title_block_text(
                {"text": "&Název / &Materiál"},
                context={
                    "parameters": {"nazev": "OBJÍMKA", "material": "S235JR"},
                    "parameter_aliases": {
                        "Název": "nazev",
                        "Materiál": "material",
                    },
                },
                sheet={},
            ),
            "OBJÍMKA / S235JR",
        )

    def test_bom_row_tokens_are_system_values(self) -> None:
        self.assertEqual(title_block_token_scope("bom.item_number"), "system")
        self.assertEqual(title_block_token_scope("bom.quantity"), "system")
        self.assertEqual(
            resolve_title_block_text(
                {"text": "&bom.item_number / &bom.quantity"},
                context={"bom_row": {"item": "3", "quantity": "7"}},
                sheet={},
            ),
            "3 / 7",
        )


if __name__ == "__main__":
    unittest.main()
