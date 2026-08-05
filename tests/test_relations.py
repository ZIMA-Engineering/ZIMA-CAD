import tempfile
import unittest
from pathlib import Path

from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox

from zima_cad.model import create_empty_part
from zima_cad.relations import RelationError, evaluate_document_relations
from zima_cad.storage import load_part_document, save_part_document


class RelationsTests(unittest.TestCase):
    def test_mass_relation_writes_plain_user_parameter_value(self):
        document = create_empty_part()
        document.document_precision["decimal_places"] = "6"
        document.physical_parameters["MASS_DENSITY"] = "7.85e-6"
        document.physical_parameter_units["MASS_DENSITY"] = "kg/mm^3"
        document.relations = [{
            "target": "hmotnost",
            "expression": "model.mass",
        }]
        document.build_active_shape = lambda: BRepPrimAPI_MakeBox(10, 10, 10).Shape()

        result = evaluate_document_relations(document)

        self.assertEqual(result["hmotnost"], "0.007850")
        self.assertEqual(document.user_parameters["hmotnost"], "0.007850")
        self.assertEqual(
            document.user_parameter_values["hmotnost"][""], "0.007850"
        )

    def test_relations_can_use_previous_result_and_safe_functions(self):
        document = create_empty_part()
        document.relations = [
            {"target": "a", "expression": "max(2, 3)"},
            {"target": "b", "expression": "a * 4"},
        ]

        result = evaluate_document_relations(document)

        self.assertEqual(result, {"a": "3", "b": "12"})

    def test_arbitrary_python_is_rejected(self):
        document = create_empty_part()
        document.relations = [{
            "target": "hmotnost",
            "expression": "__import__('os').system('true')",
        }]

        with self.assertRaises(RelationError):
            evaluate_document_relations(document)

    def test_relations_round_trip_in_model_file(self):
        document = create_empty_part()
        document.relations = [{
            "target": "hmotnost",
            "expression": "model.mass",
        }]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "relations.prtz"
            save_part_document(document, path)
            restored = load_part_document(path)

        self.assertEqual(restored.relations, document.relations)


if __name__ == "__main__":
    unittest.main()
