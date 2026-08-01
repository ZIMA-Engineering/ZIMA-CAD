import tempfile
import unittest
from pathlib import Path

from zima_cad.model import (
    ContainerType,
    EntityKind,
    create_empty_assembly,
    create_empty_part,
)
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.viewer_scene import build_document_viewer_scene_data


class AssemblyDocumentTests(unittest.TestCase):
    def test_assembly_round_trip_and_component_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "block.prtz"
            assembly_path = root / "machine.asmz"

            part = create_empty_part()
            box_container = part.create_container("Box", ContainerType.BOX)
            self.assertIsNotNone(
                part.create_primitive(box_container.entity_id, EntityKind.BOX)
            )
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            component = assembly.create_container(
                "block",
                ContainerType.COMPONENT,
            )
            component.parameters.update(
                {
                    "source_path": "block.prtz",
                    "fixed": "true",
                }
            )
            save_part_document(assembly, assembly_path)

            loaded = load_part_document(assembly_path)
            self.assertEqual(loaded.document_settings["type"], "assembly")
            self.assertEqual(loaded.root.origin_scope, None)
            loaded_component = loaded.history_objects()[0]
            self.assertEqual(loaded_component.container_type, ContainerType.COMPONENT)
            self.assertEqual(loaded_component.parameters["source_path"], "block.prtz")
            self.assertIsNotNone(loaded.build_standalone_shape(loaded_component))

            scene = build_document_viewer_scene_data(loaded)
            self.assertIn(loaded_component.entity_id, scene.shapes_by_owner_id)


if __name__ == "__main__":
    unittest.main()
