from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from zima_cad.model import (
    ContainerType,
    CoordinateSystem,
    EntityKind,
    create_empty_assembly,
    create_empty_part,
)
from zima_cad.step_export import export_step_shape
from zima_cad.step_import import import_step_file
from zima_cad.storage import save_part_document


class StepExportTests(unittest.TestCase):
    def test_part_result_exports_and_imports_again(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(container.entity_id, EntityKind.BOX)

        with tempfile.TemporaryDirectory() as directory:
            result = export_step_shape(
                document.build_active_shape(),
                Path(directory) / "part-without-extension",
            )
            self.assertEqual(result.path.suffix, ".step")
            self.assertEqual(result.solid_count, 1)
            imported = import_step_file(result.path)
            self.assertEqual(imported.solid_count, 1)

    def test_assembly_exports_all_transformed_component_solids(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "block.prtz"
            part = create_empty_part()
            box = part.create_container("Box", ContainerType.BOX)
            part.create_primitive(box.entity_id, EntityKind.BOX)
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = root / "machine.asmz"
            for index, x_position in enumerate((0.0, 100.0), 1):
                component = assembly.create_container(
                    f"Block{index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = part_path.name
                component.coordinate_system = CoordinateSystem(
                    origin=(x_position, 0.0, 0.0)
                )

            result = export_step_shape(
                assembly.build_active_shape(),
                root / "assembly.stp",
            )
            self.assertEqual(result.solid_count, 2)
            imported = import_step_file(result.path)
            self.assertEqual(imported.solid_count, 2)

    def test_empty_model_is_rejected_without_creating_a_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "empty.step"
            with self.assertRaises(ValueError):
                export_step_shape(None, target)
            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
