import tempfile
import unittest
import json
from pathlib import Path

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.GProp import GProp_GProps

from zima_cad.model import (
    ContainerType,
    EntityKind,
    ZimaEntity,
    create_empty_assembly,
    create_empty_part,
)
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.viewer_scene import build_document_viewer_scene_data


class AssemblyDocumentTests(unittest.TestCase):
    @staticmethod
    def _volume(shape):
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        return abs(float(properties.Mass()))

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
            self.assertEqual(
                scene.surface_colors_by_owner_id[loaded_component.entity_id],
                "#B9C2CC",
            )
            component_origin = next(
                child
                for child in loaded_component.children
                if child.kind == EntityKind.ORIGIN
            )
            self.assertEqual(
                len([
                    edge for edge in scene.mesh.edges
                    if edge.owner_id == component_origin.entity_id
                    and edge.element_kind == "axis"
                ]),
                3,
            )

    def test_assembly_cut_only_changes_target_component(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "block.prtz"
            assembly_path = root / "machine.asmz"
            part = create_empty_part()
            box = part.create_container("Box", ContainerType.BOX)
            part.create_primitive(box.entity_id, EntityKind.BOX)
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            targets = []
            for index, x in enumerate((0.0, 100.0)):
                component = assembly.create_container(
                    f"block-{index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = "block.prtz"
                component.coordinate_system.origin = (x, 0.0, 0.0)
                targets.append(component)

            cut = assembly.create_container(
                "Extruded cut", ContainerType.PROTRUSION
            )
            sketch = ZimaEntity(
                "Sketch", EntityKind.SKETCH,
                parameters={"profile": "circle", "diameter": "10"},
            )
            feature = ZimaEntity(
                "Cut", EntityKind.PROTRUSION,
                parameters={
                    "sketch_id": sketch.entity_id,
                    "length_forward": "20",
                    "extent_mode": "symmetric",
                    "operation": "-",
                },
            )
            cut.parameters["assembly_target_ids"] = json.dumps(
                [targets[0].entity_id]
            )
            cut.add_child(sketch)
            cut.add_child(feature)

            original_first = assembly.build_standalone_shape(targets[0])
            original_second = assembly.build_standalone_shape(targets[1])
            result_first = assembly.build_assembly_component_shape(targets[0])
            result_second = assembly.build_assembly_component_shape(targets[1])
            self.assertLess(self._volume(result_first), self._volume(original_first))
            self.assertAlmostEqual(
                self._volume(result_second), self._volume(original_second), places=5
            )
            active_scene = build_document_viewer_scene_data(
                assembly,
                uncut_component_id=targets[0].entity_id,
                uncut_component_shape=part.build_active_shape(),
            )
            self.assertAlmostEqual(
                self._volume(
                    active_scene.shapes_by_owner_id[targets[0].entity_id]
                ),
                self._volume(original_first),
                places=5,
            )


if __name__ == "__main__":
    unittest.main()
