from __future__ import annotations

import unittest
from pathlib import Path

from zima_cad.drawing_template import create_empty_drawing_template
from zima_cad.model import (
    create_empty_assembly,
    create_empty_drawing,
    create_empty_part,
)
from zima_cad.storage import load_part_document
from zima_cad.title_block import load_title_block, resolve_title_block_text


DEFAULT_PARAMETER_KEYS = (
    "name",
    "standard",
    "stock",
    "material",
    "quantity",
    "drawn_by",
    "approved_by",
    "revision",
    "date",
    "general_tolerance",
    "tolerancing",
    "mass",
)


class UserParameterDefaultsTests(unittest.TestCase):
    def test_new_documents_use_english_parameter_keys(self) -> None:
        documents = (
            create_empty_part(),
            create_empty_assembly(),
            create_empty_drawing(),
            create_empty_drawing_template("drawing_format", "Frame"),
            create_empty_drawing_template("title_block", "Title block"),
        )
        for document in documents:
            with self.subTest(document_type=document.document_settings["type"]):
                self.assertEqual(
                    tuple(document.user_parameter_order),
                    DEFAULT_PARAMETER_KEYS,
                )
                self.assertEqual(
                    tuple(document.user_parameters),
                    DEFAULT_PARAMETER_KEYS,
                )

        self.assertEqual(
            create_empty_assembly().relations,
            [{"target": "mass", "expression": "model.mass"}],
        )

    def test_start_part_uses_the_same_english_parameter_contract(self) -> None:
        document = load_part_document(Path("config/templates/start_part.prtz"))

        self.assertEqual(
            tuple(document.user_parameter_order),
            DEFAULT_PARAMETER_KEYS,
        )
        self.assertEqual(tuple(document.user_parameters), DEFAULT_PARAMETER_KEYS)
        self.assertEqual(
            document.relations,
            [{"target": "mass", "expression": "model.mass"}],
        )

    def test_localized_title_block_name_selects_localized_value(self) -> None:
        document = create_empty_part()
        document.user_parameter_values["name"] = {
            "cs": "PŘÍRUBA",
            "de": "FLANSCH",
            "en": "FLANGE",
        }
        context = {
            "parameters": document.user_parameters,
            "parameter_values": document.user_parameter_values,
            "parameter_labels": document.user_parameter_labels,
        }

        self.assertEqual(
            resolve_title_block_text(
                {"text": "&Název", "locale": "en"},
                context=context,
                sheet={},
            ),
            "PŘÍRUBA",
        )
        self.assertEqual(
            resolve_title_block_text(
                {"text": "&Name", "locale": "en"},
                context=context,
                sheet={},
            ),
            "FLANGE",
        )
        self.assertEqual(
            resolve_title_block_text(
                {"text": "&model.name", "locale": "de"},
                context=context,
                sheet={},
            ),
            "FLANSCH",
        )

    def test_shared_value_overrides_localized_title_block_value(self) -> None:
        document = create_empty_part()
        document.user_parameter_values["name"] = {"": "SHARED NAME"}
        context = {
            "parameters": document.user_parameters,
            "parameter_values": document.user_parameter_values,
            "parameter_labels": document.user_parameter_labels,
        }

        for token, locale in (("&Název", "cs"), ("&Name", "en")):
            with self.subTest(token=token):
                self.assertEqual(
                    resolve_title_block_text(
                        {"text": token, "locale": locale},
                        context=context,
                        sheet={},
                    ),
                    "SHARED NAME",
                )

    def test_supplied_title_blocks_use_localized_parameter_names(self) -> None:
        czech = load_title_block(Path("config/formats/ZE-RAZITKO.tblz"))
        english = load_title_block(Path("config/formats/ZE-TITLE-BLOCK.tblz"))
        czech_fields = {field["id"]: field for field in czech["fields"]}
        english_fields = {field["id"]: field for field in english["fields"]}

        self.assertEqual(czech_fields["NAME"]["text"], "&Název")
        self.assertEqual(czech_fields["DRAWN_BY"]["text"], "&Kreslil")
        self.assertEqual(
            czech_fields["DOCUMENT_NUMBER"]["text"],
            "&document.file_stem.&Verze",
        )
        self.assertEqual(english_fields["NAME"]["text"], "&Name")
        self.assertEqual(english_fields["DRAWN_BY"]["text"], "&Drew")
        self.assertEqual(
            english_fields["DOCUMENT_NUMBER"]["text"],
            "&document.file_stem.&Version",
        )

        document = create_empty_part()
        document.user_parameter_values["name"] = {
            "cs": "PŘÍRUBA",
            "en": "FLANGE",
        }
        context = {
            "parameters": document.user_parameters,
            "parameter_values": document.user_parameter_values,
            "parameter_labels": document.user_parameter_labels,
        }
        self.assertEqual(
            resolve_title_block_text(
                czech_fields["NAME"], context=context, sheet={}
            ),
            "PŘÍRUBA",
        )
        self.assertEqual(
            resolve_title_block_text(
                english_fields["NAME"], context=context, sheet={}
            ),
            "FLANGE",
        )


if __name__ == "__main__":
    unittest.main()
