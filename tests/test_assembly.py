import tempfile
import unittest
import json
from pathlib import Path

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.GProp import GProp_GProps

from zima_cad.model import (
    CombineMode,
    ContainerType,
    EntityKind,
    ZimaEntity,
    active_face_registry,
    create_empty_assembly,
    create_empty_part,
)
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.sketch_model import SketchModel
from zima_cad.topology import (
    AssemblyEdgeRef,
    AssemblyFaceRef,
    EdgeRef,
    FaceRef,
    TopologyResolutionState,
    assembly_face_descriptor,
    assembly_edge_descriptor,
    parse_assembly_face_descriptor,
    parse_assembly_edge_descriptor,
    resolve_assembly_face,
    resolve_assembly_edge,
)
from zima_cad.viewer_scene import build_document_viewer_scene_data


class AssemblyDocumentTests(unittest.TestCase):
    @staticmethod
    def _volume(shape):
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        return abs(float(properties.Mass()))

    def test_component_inherits_part_color_until_overridden(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "colored.prtz"
            assembly_path = root / "colors.asmz"
            part = create_empty_part()
            part.document_settings["body_color"] = "#228844"
            box = part.create_container("Box", ContainerType.BOX)
            part.create_primitive(box.entity_id, EntityKind.BOX)
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            component = assembly.create_container(
                "colored", ContainerType.COMPONENT
            )
            component.parameters.update({
                "source_path": "colored.prtz",
                "body_color": "#B9C2CC",
            })
            scene = build_document_viewer_scene_data(
                assembly,
                component_documents={component.entity_id: part},
            )
            self.assertEqual(
                scene.surface_colors_by_owner_id[component.entity_id],
                "#228844",
            )

            component.parameters["body_color"] = "#AA2233"
            component.parameters["body_color_override"] = "true"
            scene = build_document_viewer_scene_data(
                assembly,
                component_documents={component.entity_id: part},
            )
            self.assertEqual(
                scene.surface_colors_by_owner_id[component.entity_id],
                "#AA2233",
            )

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
            assembly.document_settings["named_views"] = json.dumps([{
                "name": "Detail A",
                "yaw_degrees": 12.5,
                "pitch_degrees": -30.0,
                "pan_x": 18.0,
                "pan_y": -4.0,
                "zoom": 2.25,
            }])
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
            self.assertEqual(
                json.loads(loaded.document_settings["named_views"])[0]["name"],
                "Detail A",
            )
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
                0,
            )
            orientation_scene = build_document_viewer_scene_data(
                loaded,
                show_object_planes=True,
                show_component_origins=True,
            )
            self.assertEqual(
                len([
                    edge for edge in orientation_scene.mesh.edges
                    if edge.owner_id == component_origin.entity_id
                    and edge.element_kind == "axis"
                ]),
                3,
            )
            self.assertEqual(
                {
                    plane.plane_index
                    for plane in orientation_scene.mesh.planes
                    if plane.owner_id == component_origin.entity_id
                },
                {1, 2, 3},
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

    def test_mate_faces_survive_source_boolean_regeneration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "boolean-part.prtz"
            assembly_path = root / "boolean-assembly.asmz"
            part = create_empty_part()
            base_container = part.create_container("Base", ContainerType.BOX)
            base = part.create_primitive(
                base_container.entity_id, EntityKind.BOX
            )
            tool_container = part.create_container("Cut", ContainerType.BOX)
            tool = part.create_primitive(
                tool_container.entity_id, EntityKind.BOX
            )
            self.assertIsNotNone(base)
            self.assertIsNotNone(tool)
            base.parameters.update({
                "length": "100", "width": "100", "height": "100"
            })
            tool.parameters.update({
                "length": "30", "width": "20", "height": "20"
            })
            tool.combine_mode = CombineMode.SUBTRACT
            tool_container.coordinate_system.origin = (45.0, 0.0, 0.0)
            face = FaceRef(tool.entity_id, "y_max")
            self.assertEqual(
                active_face_registry(part).resolve(face).state,
                TopologyResolutionState.RESOLVED,
            )
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            components = []
            for index, x in enumerate((0.0, 140.0)):
                component = assembly.create_container(
                    f"boolean-part-{index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = part_path.name
                component.coordinate_system.origin = (x, 0.0, 0.0)
                components.append(component)
            source_reference = AssemblyFaceRef(
                components[0].entity_id, face
            )
            target_reference = AssemblyFaceRef(
                components[1].entity_id, face
            )
            components[1].parameters["assembly_mates"] = json.dumps([{
                "source": assembly_face_descriptor(source_reference),
                "target": assembly_face_descriptor(target_reference),
                "type": "planar",
                "offset": 0.0,
            }])
            save_part_document(assembly, assembly_path)

            tool.parameters["width"] = "25"
            save_part_document(part, part_path)
            loaded_part = load_part_document(part_path)
            loaded_assembly = load_part_document(assembly_path)
            loaded_components = loaded_assembly.history_objects()
            loaded_registry = active_face_registry(loaded_part)
            registries = {
                component.entity_id: loaded_registry
                for component in loaded_components
            }
            mate = json.loads(str(
                loaded_components[1].parameters["assembly_mates"]
            ))[0]
            loaded_source = parse_assembly_face_descriptor(mate["source"])
            loaded_target = parse_assembly_face_descriptor(mate["target"])
            self.assertEqual(loaded_source, source_reference)
            self.assertEqual(loaded_target, target_reference)
            self.assertEqual(
                resolve_assembly_face(loaded_source, registries).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                resolve_assembly_face(loaded_target, registries).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                loaded_components[1].coordinate_system.origin,
                components[1].coordinate_system.origin,
            )

            loaded_tool = loaded_part.find_entity(tool.entity_id)
            self.assertIsNotNone(loaded_tool)
            loaded_tool.locked = True
            missing_registry = active_face_registry(loaded_part)
            missing_registries = {
                component.entity_id: missing_registry
                for component in loaded_components
            }
            self.assertEqual(
                resolve_assembly_face(
                    loaded_target, missing_registries
                ).state,
                TopologyResolutionState.MISSING,
            )

    def test_mate_face_recovers_after_temporary_boolean_split(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "split-source.prtz"
            assembly_path = root / "split-assembly.asmz"
            part = create_empty_part()
            base_container = part.create_container("Base", ContainerType.BOX)
            base = part.create_primitive(
                base_container.entity_id, EntityKind.BOX
            )
            self.assertIsNotNone(base)
            face = FaceRef(base.entity_id, "y_max")
            self.assertEqual(
                active_face_registry(part).resolve(face).state,
                TopologyResolutionState.RESOLVED,
            )
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            components = []
            for index in range(2):
                component = assembly.create_container(
                    f"split-source-{index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = part_path.name
                components.append(component)
            source_reference = AssemblyFaceRef(
                components[0].entity_id, face
            )
            target_reference = AssemblyFaceRef(
                components[1].entity_id, face
            )
            components[1].parameters["assembly_mates"] = json.dumps([{
                "source": assembly_face_descriptor(source_reference),
                "target": assembly_face_descriptor(target_reference),
                "type": "planar",
            }])
            save_part_document(assembly, assembly_path)

            splitter_container = part.create_container(
                "Splitter", ContainerType.BOX
            )
            splitter = part.create_primitive(
                splitter_container.entity_id, EntityKind.BOX
            )
            self.assertIsNotNone(splitter)
            splitter.combine_mode = CombineMode.ADD
            splitter_container.coordinate_system.origin = (20.0, 0.0, 0.0)
            save_part_document(part, part_path)

            loaded_part = load_part_document(part_path)
            loaded_assembly = load_part_document(assembly_path)
            loaded_components = loaded_assembly.history_objects()
            ambiguous_registry = active_face_registry(loaded_part)
            registries = {
                component.entity_id: ambiguous_registry
                for component in loaded_components
            }
            self.assertEqual(
                resolve_assembly_face(source_reference, registries).state,
                TopologyResolutionState.AMBIGUOUS,
            )
            self.assertEqual(
                resolve_assembly_face(target_reference, registries).state,
                TopologyResolutionState.AMBIGUOUS,
            )
            mate_before_recovery = str(
                loaded_components[1].parameters["assembly_mates"]
            )

            loaded_splitter = loaded_part.find_entity(splitter.entity_id)
            self.assertIsNotNone(loaded_splitter)
            loaded_splitter.locked = True
            recovered_registry = active_face_registry(loaded_part)
            recovered_registries = {
                component.entity_id: recovered_registry
                for component in loaded_components
            }
            self.assertEqual(
                resolve_assembly_face(source_reference, recovered_registries).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                resolve_assembly_face(target_reference, recovered_registries).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                str(loaded_components[1].parameters["assembly_mates"]),
                mate_before_recovery,
            )

    def test_concentric_mate_edge_survives_source_regeneration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            part_path = root / "shaft.prtz"
            assembly_path = root / "shafts.asmz"
            part = create_empty_part()
            container = ZimaEntity(
                "Shaft",
                EntityKind.CONTAINER,
                parameters={"container_type": ContainerType.PROTRUSION.value},
            )
            sketch = ZimaEntity(
                "ShaftSketch",
                EntityKind.SKETCH,
                parameters={
                    "plane": "xz",
                    "profile": "circle",
                    "diameter": "20",
                    "sketch_data": json.dumps(SketchModel().to_dict()),
                },
            )
            feature = ZimaEntity(
                "ShaftFeature",
                EntityKind.PROTRUSION,
                parameters={
                    "sketch_id": sketch.entity_id,
                    "length_forward": "50",
                },
            )
            container.add_child(sketch)
            container.add_child(feature)
            part.root.add_child(container)
            edge = EdgeRef(feature.entity_id, "start", sketch.entity_id)
            self.assertEqual(
                active_face_registry(part).resolve_edge(edge).state,
                TopologyResolutionState.RESOLVED,
            )
            save_part_document(part, part_path)

            assembly = create_empty_assembly()
            assembly.source_file_path = assembly_path
            components = []
            for index, x in enumerate((0.0, 80.0)):
                component = assembly.create_container(
                    f"shaft-{index}", ContainerType.COMPONENT
                )
                component.parameters["source_path"] = part_path.name
                component.coordinate_system.origin = (x, 0.0, 0.0)
                components.append(component)
            source_reference = AssemblyEdgeRef(
                components[0].entity_id, edge
            )
            target_reference = AssemblyEdgeRef(
                components[1].entity_id, edge
            )
            components[1].parameters["assembly_mates"] = json.dumps([{
                "source": assembly_edge_descriptor(source_reference),
                "target": assembly_edge_descriptor(target_reference),
                "type": "axis",
                "offset": 0.0,
            }])
            save_part_document(assembly, assembly_path)

            sketch.parameters["diameter"] = "28"
            save_part_document(part, part_path)
            loaded_part = load_part_document(part_path)
            loaded_assembly = load_part_document(assembly_path)
            loaded_components = loaded_assembly.history_objects()
            loaded_registry = active_face_registry(loaded_part)
            registries = {
                component.entity_id: loaded_registry
                for component in loaded_components
            }
            mate = json.loads(str(
                loaded_components[1].parameters["assembly_mates"]
            ))[0]
            loaded_source = parse_assembly_edge_descriptor(mate["source"])
            loaded_target = parse_assembly_edge_descriptor(mate["target"])
            self.assertEqual(loaded_source, source_reference)
            self.assertEqual(loaded_target, target_reference)
            self.assertEqual(
                resolve_assembly_edge(loaded_source, registries).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                resolve_assembly_edge(loaded_target, registries).state,
                TopologyResolutionState.RESOLVED,
            )
            original_placement = tuple(
                loaded_components[1].coordinate_system.origin
            )
            mate_payload = str(
                loaded_components[1].parameters["assembly_mates"]
            )

            loaded_feature = loaded_part.find_entity(feature.entity_id)
            self.assertIsNotNone(loaded_feature)
            loaded_feature.locked = True
            missing_registry = active_face_registry(loaded_part)
            missing_registries = {
                component.entity_id: missing_registry
                for component in loaded_components
            }
            self.assertEqual(
                resolve_assembly_edge(
                    loaded_target, missing_registries
                ).state,
                TopologyResolutionState.MISSING,
            )

            loaded_feature.locked = False
            recovered_registry = active_face_registry(loaded_part)
            recovered_registries = {
                component.entity_id: recovered_registry
                for component in loaded_components
            }
            self.assertEqual(
                resolve_assembly_edge(
                    loaded_target, recovered_registries
                ).state,
                TopologyResolutionState.RESOLVED,
            )
            self.assertEqual(
                str(loaded_components[1].parameters["assembly_mates"]),
                mate_payload,
            )
            self.assertEqual(
                tuple(loaded_components[1].coordinate_system.origin),
                original_placement,
            )


if __name__ == "__main__":
    unittest.main()
