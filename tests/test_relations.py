import tempfile
import unittest
from pathlib import Path

from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox

from zima_cad.model import ContainerType, EntityKind, create_empty_assembly, create_empty_part
from zima_cad.relations import RelationError, evaluate_document_relations
from zima_cad.storage import load_part_document, save_part_document


class RelationsTests(unittest.TestCase):
    def test_assembly_mass_uses_each_component_material_density(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assembly = create_empty_assembly()
            assembly.source_file_path = root / "assembly.asmz"
            document_cache = assembly.__dict__.setdefault(
                "_assembly_component_document_cache", {}
            )

            expected_mass = 0.0
            for index, density in enumerate((1.0e-6, 8.0e-6), start=1):
                source_path = (root / f"part-{index}.prtz").resolve()
                part = create_empty_part()
                part.physical_parameters["MASS_DENSITY"] = str(density)
                part.physical_parameter_units["MASS_DENSITY"] = "kg/mm^3"
                container = part.create_container("Box", ContainerType.BOX)
                part.create_primitive(container.entity_id, EntityKind.BOX)
                document_cache[source_path] = part
                component = assembly.create_container(
                    f"Part {index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = str(source_path)
                expected_mass += 40.0 * 30.0 * 20.0 * density

            result = evaluate_document_relations(assembly)

        self.assertEqual(result["hmotnost"], f"{expected_mass:.3f}")

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
