import unittest
from pathlib import Path

from zima_cad.drawing import drawing_sheets, store_drawing_sheets
from zima_cad.drawing_format import load_drawing_format
from zima_cad.model import create_empty_drawing
from zima_cad.title_block import load_title_block


class EmbeddedDrawingTemplateTests(unittest.TestCase):
    def test_each_sheet_keeps_independent_embedded_definitions(self) -> None:
        document = create_empty_drawing()
        frame = load_drawing_format(Path("config/formats/ZE-A4.frmz"))
        title_block = load_title_block(Path("config/formats/ZE-RAZITKO.tblz"))
        sheets = drawing_sheets(document)
        sheets[0]["format_definition"] = frame
        sheets[0]["title_block_definition"] = title_block
        second = dict(sheets[0])
        second["id"] = "second-sheet"
        second["name"] = "List 2"
        second["title_block_definition"] = dict(title_block, name="Second")
        sheets.append(second)

        store_drawing_sheets(document, sheets)
        restored = drawing_sheets(document)

        self.assertEqual(restored[0]["format_definition"]["name"], frame["name"])
        self.assertEqual(
            restored[0]["title_block_definition"]["name"],
            title_block["name"],
        )
        self.assertEqual(restored[1]["title_block_definition"]["name"], "Second")
        restored[1]["title_block_definition"]["name"] = "Changed"
        self.assertEqual(
            restored[0]["title_block_definition"]["name"],
            title_block["name"],
        )
