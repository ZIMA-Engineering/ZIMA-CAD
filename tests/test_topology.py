from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from zima_cad.model import (
    ContainerType,
    EntityKind,
    active_face_registry,
    create_empty_part,
    make_protrusion_shape,
    make_revolve_shape,
    protrusion_face_registry,
    revolve_face_registry,
    semantic_face_registry,
    ZimaEntity,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.topology import (
    AssemblyFaceRef,
    EdgeRef,
    FaceRef,
    TopologyRegistry,
    TopologyResolutionState,
    VertexRef,
    decode_edge_reference,
    decode_face_reference,
    decode_vertex_reference,
    encode_edge_reference,
    encode_face_reference,
    encode_vertex_reference,
    parse_edge_reference,
    parse_face_reference,
    parse_vertex_reference,
)
from zima_cad.viewer_mesh import triangulate_shape


class StableTopologyTests(unittest.TestCase):
    def test_face_reference_round_trip(self) -> None:
        reference = FaceRef(
            feature_id="extrusion-1",
            role="generated",
            source_id="sketch-edge-7",
            fragment=2,
        )
        self.assertEqual(FaceRef.deserialize(reference.serialize()), reference)
        self.assertEqual(parse_face_reference(reference.to_dict()), reference)
        self.assertEqual(
            decode_face_reference(encode_face_reference(reference)),
            reference,
        )
        self.assertIsNone(parse_face_reference("17"))

    def test_assembly_reference_keeps_instance_separate(self) -> None:
        face = FaceRef("extrusion-1", "end")
        first = AssemblyFaceRef.from_dict({
            "instance_id": "component-a",
            "face": face.to_dict(),
        })
        second = AssemblyFaceRef("component-b", face)
        self.assertNotEqual(first, second)
        self.assertEqual(first.face, second.face)

    def test_edge_and_vertex_reference_round_trip(self) -> None:
        edge = EdgeRef("extrusion-1", "start", "sketch-edge-7", 2)
        vertex = VertexRef("extrusion-1", "end", "sketch-point-3")
        self.assertEqual(parse_edge_reference(edge.to_dict()), edge)
        self.assertEqual(
            decode_edge_reference(encode_edge_reference(edge)), edge
        )
        self.assertEqual(parse_vertex_reference(vertex.to_dict()), vertex)
        self.assertEqual(
            decode_vertex_reference(encode_vertex_reference(vertex)), vertex
        )
        self.assertIsNone(parse_edge_reference("3"))
        self.assertIsNone(parse_vertex_reference("9"))

    def test_registry_keeps_topology_kinds_separate(self) -> None:
        face = FaceRef("extrusion-1", "end")
        edge = EdgeRef("extrusion-1", "end", "sketch-edge-1")
        vertex = VertexRef("extrusion-1", "end", "sketch-point-1")
        registry = TopologyRegistry()
        registry.register_face(face, "face", runtime_index=1)
        registry.register_edge(edge, "edge", runtime_index=1)
        registry.register_vertex(vertex, "vertex", runtime_index=1)
        self.assertEqual(registry.reference_for_runtime_index(1), face)
        self.assertEqual(registry.edge_reference_for_runtime_index(1), edge)
        self.assertEqual(
            registry.vertex_reference_for_runtime_index(1), vertex
        )
        self.assertEqual(registry.resolve_edge(edge).shape, "edge")
        self.assertEqual(registry.resolve_vertex(vertex).shape, "vertex")

    def test_registry_never_silently_resolves_ambiguous_face(self) -> None:
        reference = FaceRef("extrusion-1", "generated", "edge-1")
        registry = TopologyRegistry()
        registry.register_face(reference, "face-a", runtime_index=3)
        registry.register_face(reference, "face-b", runtime_index=4)
        result = registry.resolve(reference)
        self.assertEqual(result.state, TopologyResolutionState.AMBIGUOUS)
        self.assertIsNone(result.shape)
        self.assertEqual(result.candidates, ("face-a", "face-b"))
        self.assertEqual(registry.reference_for_runtime_index(3), reference)

    def test_registry_distinguishes_missing_and_incompatible(self) -> None:
        registry = TopologyRegistry()
        registry.register_face(FaceRef("box-1", "x_min"), "face")
        self.assertEqual(
            registry.resolve(FaceRef("box-1", "x_max")).state,
            TopologyResolutionState.INCOMPATIBLE,
        )
        self.assertEqual(
            registry.resolve(FaceRef("box-2", "x_min")).state,
            TopologyResolutionState.MISSING,
        )

    def test_box_face_roles_survive_dimension_changes(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        first_shape = document.build_standalone_shape(container)
        first = semantic_face_registry(document, box, first_shape)
        expected = {
            FaceRef(box.entity_id, role)
            for role in (
                "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"
            )
        }
        self.assertEqual(set(first.references), expected)

        box.parameters.update({"length": "240", "width": "35", "height": "81"})
        second_shape = document.build_standalone_shape(container)
        second = semantic_face_registry(document, box, second_shape)
        self.assertEqual(set(second.references), expected)
        for reference in expected:
            self.assertEqual(
                second.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "box.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.references), expected)
            for reference in expected:
                self.assertEqual(
                    loaded_registry.resolve(reference).state,
                    TopologyResolutionState.RESOLVED,
                )

    def test_protrusion_caps_survive_length_change(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "Protrusion001",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 10.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        feature = ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "10",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            *(
                FaceRef(feature.entity_id, "generated", source_id)
                for source_id in ("ab", "bc", "cd", "da")
            ),
        }
        expected_edges = {
            *(
                EdgeRef(feature.entity_id, role, source_id)
                for role in ("start", "end")
                for source_id in ("ab", "bc", "cd", "da")
            ),
            *(
                EdgeRef(feature.entity_id, "generated", source_id)
                for source_id in ("a", "b", "c", "d")
            ),
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, source_id)
            for role in ("start", "end")
            for source_id in ("a", "b", "c", "d")
        }

        first_shape = make_protrusion_shape(document, container)
        first = protrusion_face_registry(document, container, first_shape)
        mesh = triangulate_shape(first_shape, owner_id=document.root.entity_id)
        self.assertEqual(
            len([
                point
                for point in mesh.points
                if point.element_kind == "vertex"
            ]),
            8,
        )
        self.assertEqual(set(first.references), expected)
        self.assertEqual(set(first.edge_references), expected_edges)
        self.assertEqual(set(first.vertex_references), expected_vertices)
        feature.parameters["length_forward"] = "125"
        second_shape = make_protrusion_shape(document, container)
        second = protrusion_face_registry(document, container, second_shape)
        self.assertEqual(set(second.references), expected)
        self.assertEqual(set(second.edge_references), expected_edges)
        self.assertEqual(set(second.vertex_references), expected_vertices)
        for reference in expected:
            self.assertEqual(
                second.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        edited_entities, edited_dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited_entities:
            if entity.get("id") in {"b", "c"}:
                entity["x"] = 35.0
            if entity.get("id") in {"c", "d"}:
                entity["y"] = 18.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(
                edited_entities,
                edited_dimensions,
            ).to_dict()
        )
        reshaped = active_face_registry(document)
        self.assertEqual(set(reshaped.references), expected)
        self.assertEqual(set(reshaped.edge_references), expected_edges)
        self.assertEqual(set(reshaped.vertex_references), expected_vertices)
        for reference in expected:
            self.assertEqual(
                reshaped.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extrusion.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.references), expected)
            self.assertEqual(
                set(loaded_registry.edge_references), expected_edges
            )
            self.assertEqual(
                set(loaded_registry.vertex_references), expected_vertices
            )
            for reference in expected:
                self.assertEqual(
                    loaded_registry.resolve(reference).state,
                    TopologyResolutionState.RESOLVED,
                )

    def test_revolve_topology_survives_angle_and_profile_changes(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "Revolve001",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 30.0},
            {"id": "axis", "type": "construction", "point_ids": ["axis-a", "axis-b"]},
            {"id": "a", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 12.0},
            {"id": "d", "type": "point", "x": 10.0, "y": 12.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(SketchModel.from_editor_data(entities).to_dict()),
            },
        )
        feature = ZimaEntity(
            "Revolve",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": sketch.entity_id,
                "angle": "120",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected_faces = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            *(FaceRef(feature.entity_id, "generated", source_id)
              for source_id in ("ab", "bc", "cd", "da")),
        }
        expected_edges = {
            *(EdgeRef(feature.entity_id, role, source_id)
              for role in ("start", "end")
              for source_id in ("ab", "bc", "cd", "da")),
            *(EdgeRef(feature.entity_id, "generated", point_id)
              for point_id in ("a", "b", "c", "d")),
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, point_id)
            for role in ("start", "end")
            for point_id in ("a", "b", "c", "d")
        }

        for angle in (120, 210):
            feature.parameters["angle"] = str(angle)
            shape = make_revolve_shape(document, container)
            registry = revolve_face_registry(document, container, shape)
            self.assertEqual(set(registry.references), expected_faces)
            self.assertEqual(set(registry.edge_references), expected_edges)
            self.assertEqual(set(registry.vertex_references), expected_vertices)

        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") in {"b", "c"}:
                entity["x"] = 24.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), expected_vertices)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "revolve.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.references), expected_faces)
            self.assertEqual(set(loaded_registry.edge_references), expected_edges)
            self.assertEqual(set(loaded_registry.vertex_references), expected_vertices)

        feature.parameters["angle"] = "360"
        full_registry = active_face_registry(document)
        self.assertEqual(
            set(full_registry.references),
            {
                FaceRef(feature.entity_id, "generated", source_id)
                for source_id in ("ab", "bc", "cd", "da")
            },
        )
        self.assertEqual(
            set(full_registry.edge_references),
            {
                EdgeRef(feature.entity_id, "generated", point_id)
                for point_id in ("a", "b", "c", "d")
            },
        )
        self.assertEqual(full_registry.vertex_references, ())


if __name__ == "__main__":
    unittest.main()
