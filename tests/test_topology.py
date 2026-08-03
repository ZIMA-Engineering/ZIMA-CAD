from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from zima_cad.model import (
    CombineMode,
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
from OCC.Core.TopAbs import TopAbs_SOLID
from OCC.Core.TopExp import TopExp_Explorer
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
    semantic_provenance_id,
)
from zima_cad.viewer_mesh import triangulate_shape


class StableTopologyTests(unittest.TestCase):
    @staticmethod
    def _subshape_count(shape, shape_type) -> int:
        explorer = TopExp_Explorer(shape, shape_type)
        count = 0
        while explorer.More():
            count += 1
            explorer.Next()
        return count

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

    def test_semantic_provenance_is_order_independent_and_kernel_free(self) -> None:
        first = FaceRef("base", "x_max")
        second = FaceRef("cut", "generated", "sketch-edge")
        provenance = semantic_provenance_id(first, second)
        self.assertEqual(
            provenance, semantic_provenance_id(second, first)
        )
        self.assertEqual(
            json.loads(provenance),
            [
                {"feature_id": "base", "kind": "face", "role": "x_max"},
                {
                    "feature_id": "cut",
                    "kind": "face",
                    "role": "generated",
                    "source_id": "sketch-edge",
                },
            ],
        )

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

    def test_disconnected_add_preserves_last_valid_body(self) -> None:
        document = create_empty_part()
        first_container = document.create_container("Box", ContainerType.BOX)
        first = document.create_primitive(first_container.entity_id, EntityKind.BOX)
        second_container = document.create_container("Box", ContainerType.BOX)
        second = document.create_primitive(second_container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        first.combine_mode = CombineMode.ADD
        second.combine_mode = CombineMode.ADD
        second_container.coordinate_system.origin = (1000.0, 0.0, 0.0)

        disconnected = document.build_active_shape()
        self.assertEqual(
            self._subshape_count(disconnected, TopAbs_SOLID), 1
        )
        self.assertEqual(second.parameters.get("build_status"), "disconnected")

        second_container.coordinate_system.origin = (20.0, 0.0, 0.0)
        connected = document.build_active_shape()
        self.assertEqual(self._subshape_count(connected, TopAbs_SOLID), 1)
        self.assertNotIn("build_status", second.parameters)

    def test_boolean_cut_propagates_both_feature_face_identities(self) -> None:
        document = create_empty_part()
        outer_container = document.create_container("Box", ContainerType.BOX)
        outer = document.create_primitive(
            outer_container.entity_id, EntityKind.BOX
        )
        tool_container = document.create_container("Box", ContainerType.BOX)
        tool = document.create_primitive(
            tool_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(outer)
        self.assertIsNotNone(tool)
        outer.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })
        tool.parameters.update({
            "length": "20", "width": "20", "height": "20"
        })
        tool.combine_mode = CombineMode.SUBTRACT
        expected = {
            FaceRef(feature.entity_id, role)
            for feature in (outer, tool)
            for role in (
                "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"
            )
        }

        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected)
        for reference in expected:
            self.assertEqual(
                registry.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cut.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(set(loaded_registry.references), expected)
        for reference in expected:
            self.assertEqual(
                loaded_registry.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )

    def test_boolean_split_exposes_fragments_without_silent_rebinding(self) -> None:
        document = create_empty_part()
        first_container = document.create_container("Box", ContainerType.BOX)
        first = document.create_primitive(
            first_container.entity_id, EntityKind.BOX
        )
        second_container = document.create_container("Box", ContainerType.BOX)
        second = document.create_primitive(
            second_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        second_container.coordinate_system.origin = (20.0, 0.0, 0.0)
        original = FaceRef(first.entity_id, "y_max")

        first_registry = active_face_registry(document)
        self.assertEqual(
            first_registry.resolve(original).state,
            TopologyResolutionState.AMBIGUOUS,
        )
        fragments = {
            reference
            for reference in first_registry.references
            if reference.feature_id == first.entity_id
            and reference.role == "y_max"
            and reference.fragment is not None
        }
        self.assertEqual(
            fragments,
            {
                FaceRef(first.entity_id, "y_max", fragment=1),
                FaceRef(first.entity_id, "y_max", fragment=2),
            },
        )

        second_container.coordinate_system.origin = (25.0, 0.0, 0.0)
        edited_registry = active_face_registry(document)
        self.assertEqual(
            edited_registry.resolve(original).state,
            TopologyResolutionState.AMBIGUOUS,
        )
        self.assertTrue(fragments.issubset(set(edited_registry.references)))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fuse.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(
            loaded_registry.resolve(original).state,
            TopologyResolutionState.AMBIGUOUS,
        )
        self.assertTrue(fragments.issubset(set(loaded_registry.references)))

    def test_boolean_intersections_have_zima_owned_edge_and_vertex_ids(self) -> None:
        document = create_empty_part()
        outer_container = document.create_container("Box", ContainerType.BOX)
        outer = document.create_primitive(
            outer_container.entity_id, EntityKind.BOX
        )
        tool_container = document.create_container("Box", ContainerType.BOX)
        tool = document.create_primitive(
            tool_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(outer)
        self.assertIsNotNone(tool)
        outer.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })
        tool.parameters.update({
            "length": "30", "width": "20", "height": "20"
        })
        tool.combine_mode = CombineMode.SUBTRACT
        tool_container.coordinate_system.origin = (45.0, 0.0, 0.0)
        outer_face = FaceRef(outer.entity_id, "x_max")
        expected_edges = {
            EdgeRef(
                tool.entity_id,
                "intersection",
                semantic_provenance_id(
                    outer_face, FaceRef(tool.entity_id, role)
                ),
            )
            for role in ("y_min", "y_max", "z_min", "z_max")
        }
        expected_vertices = {
            VertexRef(
                tool.entity_id,
                "intersection",
                semantic_provenance_id(
                    outer_face,
                    FaceRef(tool.entity_id, y_role),
                    FaceRef(tool.entity_id, z_role),
                ),
            )
            for y_role in ("y_min", "y_max")
            for z_role in ("z_min", "z_max")
        }

        registry = active_face_registry(document)
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), expected_vertices)
        for reference in expected_edges:
            self.assertEqual(
                registry.resolve_edge(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        for reference in expected_vertices:
            self.assertEqual(
                registry.resolve_vertex(reference).state,
                TopologyResolutionState.RESOLVED,
            )

        tool.parameters["length"] = "40"
        tool_container.coordinate_system.origin = (42.0, 0.0, 0.0)
        edited = active_face_registry(document)
        self.assertEqual(set(edited.edge_references), expected_edges)
        self.assertEqual(set(edited.vertex_references), expected_vertices)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "intersection.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), expected_vertices
        )

        remembered_edge = next(iter(expected_edges))
        tool_container.coordinate_system.origin = (200.0, 0.0, 0.0)
        document.build_active_shape()
        missing_registry = active_face_registry(document)
        self.assertEqual(
            missing_registry.resolve_edge(remembered_edge).state,
            TopologyResolutionState.MISSING,
        )
        self.assertEqual(
            tool.parameters.get("build_status"), "no_intersection"
        )
        tool_container.coordinate_system.origin = (42.0, 0.0, 0.0)
        document.build_active_shape()
        restored_registry = active_face_registry(document)
        self.assertEqual(
            restored_registry.resolve_edge(remembered_edge).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertNotIn("build_status", tool.parameters)

    def test_boolean_intersections_survive_three_feature_chain(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Box", ContainerType.BOX)
        base = document.create_primitive(
            base_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })
        for origin, dimensions in (
            ((45.0, 0.0, 0.0), (30.0, 20.0, 20.0)),
            ((0.0, 45.0, 0.0), (20.0, 30.0, 20.0)),
        ):
            container = document.create_container("Cut", ContainerType.BOX)
            tool = document.create_primitive(container.entity_id, EntityKind.BOX)
            self.assertIsNotNone(tool)
            tool.parameters.update({
                "length": str(dimensions[0]),
                "width": str(dimensions[1]),
                "height": str(dimensions[2]),
            })
            tool.combine_mode = CombineMode.SUBTRACT
            container.coordinate_system.origin = origin

        registry = active_face_registry(document)
        intersection_edges = {
            reference for reference in registry.edge_references
            if reference.role == "intersection"
        }
        intersection_vertices = {
            reference for reference in registry.vertex_references
            if reference.role == "intersection"
        }
        self.assertEqual(len(intersection_edges), 8)
        self.assertEqual(len(intersection_vertices), 8)
        for reference in intersection_edges:
            self.assertEqual(
                registry.resolve_edge(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        for reference in intersection_vertices:
            self.assertEqual(
                registry.resolve_vertex(reference).state,
                TopologyResolutionState.RESOLVED,
            )

    def test_boolean_chain_propagates_two_solid_feature_ancestry(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "TwoSolidExtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = []
        for prefix, x_offset in (("left", 0.0), ("right", 40.0)):
            coordinates = (
                (x_offset, 0.0), (x_offset + 20.0, 0.0),
                (x_offset + 20.0, 20.0), (x_offset, 20.0),
            )
            point_ids = tuple(f"{prefix}-p{index}" for index in range(4))
            entities.extend(
                {"id": point_id, "type": "point", "x": x, "y": y}
                for point_id, (x, y) in zip(point_ids, coordinates)
            )
            entities.extend(
                {
                    "id": f"{prefix}-e{index}",
                    "type": "segment",
                    "point_ids": [
                        point_ids[index], point_ids[(index + 1) % 4]
                    ],
                }
                for index in range(4)
            )
        sketch = ZimaEntity(
            "TwoSolidSketch",
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
            "TwoSolidFeature",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "12"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        cutters = []
        for name, origin, dimensions in (
            ("FirstCrossCut", (8.0, 0.0, 0.0), (44.0, 6.0, 20.0)),
            ("SecondCrossCut", (-5.0, 7.0, 7.0), (70.0, 3.0, 6.0)),
        ):
            cut_container = document.create_container(name, ContainerType.BOX)
            cutter = document.create_primitive(
                cut_container.entity_id, EntityKind.BOX
            )
            self.assertIsNotNone(cutter)
            cutter.parameters.update({
                "length": str(dimensions[0]),
                "width": str(dimensions[1]),
                "height": str(dimensions[2]),
            })
            cutter.combine_mode = CombineMode.SUBTRACT
            cut_container.coordinate_system.origin = origin
            cutters.append((cut_container, cutter))

        registry = active_face_registry(document)
        self.assertGreaterEqual(
            self._subshape_count(document.build_active_shape(), TopAbs_SOLID),
            2,
        )
        remembered_faces = set(registry.references)
        remembered_edges = set(registry.edge_references)
        remembered_vertices = set(registry.vertex_references)
        for _container, cutter in cutters:
            intersections = {
                reference for reference in registry.edge_references
                if reference.feature_id == cutter.entity_id
                and reference.role == "intersection"
            }
            self.assertTrue(intersections)
            self.assertTrue(any(
                registry.resolve_edge(reference).state
                == TopologyResolutionState.RESOLVED
                for reference in intersections
            ))

        cutters[1][0].coordinate_system.origin = (-5.0, 7.0, 8.0)
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), remembered_faces)
        self.assertEqual(set(edited_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), remembered_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "two-solid-boolean-chain.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(set(loaded_registry.references), remembered_faces)
        self.assertEqual(set(loaded_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), remembered_vertices
        )

    def test_circular_protrusion_cut_keeps_curved_provenance(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Box", ContainerType.BOX)
        base = document.create_primitive(
            base_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })
        container = ZimaEntity(
            "CircularCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        container.coordinate_system.origin = (45.0, 0.0, 0.0)
        sketch = ZimaEntity(
            "CircleSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "yz",
                "profile": "circle",
                "diameter": "20",
                "sketch_data": json.dumps(SketchModel().to_dict()),
            },
        )
        feature = ZimaEntity(
            "CircularProtrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "30",
                "extent_mode": "one_side",
                "direction": "forward",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        document.set_history_cursor(len(document.history_objects()))
        generated_face = FaceRef(
            feature.entity_id, "generated", sketch.entity_id
        )
        intersection = EdgeRef(
            feature.entity_id,
            "intersection",
            semantic_provenance_id(
                FaceRef(base.entity_id, "x_max"), generated_face
            ),
        )

        registry = active_face_registry(document)
        self.assertEqual(
            registry.resolve(generated_face).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertEqual(
            registry.resolve_edge(intersection).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertIn(
            EdgeRef(feature.entity_id, "start", sketch.entity_id),
            registry.edge_references,
        )
        self.assertEqual(
            {
                reference for reference in registry.vertex_references
                if reference.role == "intersection"
            },
            set(),
        )
        sketch.parameters["diameter"] = "28"
        edited = active_face_registry(document)
        self.assertEqual(
            edited.resolve_edge(intersection).state,
            TopologyResolutionState.RESOLVED,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "circular-cut.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(
            loaded_registry.resolve_edge(intersection).state,
            TopologyResolutionState.RESOLVED,
        )

    def test_history_orientation_mapping_tracks_runtime_part_id(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(container.entity_id, EntityKind.BOX)
        old_root_id = document.root.entity_id
        face_key = f"face:{old_root_id}:1"
        container.parameters["constraint_refs"] = json.dumps([
            {
                "type": "face",
                "entity_id": old_root_id,
                "key": face_key,
                "topology_key": "1",
                "reference_scope": "history_result",
                "history_cursor": 1,
                "equations": [[1.0, 0.0, 0.0, 20.0]],
            },
            {
                "type": "container_orientation",
                "mappings": [{
                    "slot": "front",
                    "flip": False,
                    "reference_key": face_key,
                }],
            },
        ])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mapping.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
        loaded_container = loaded.history_objects()[0]
        references = json.loads(
            str(loaded_container.parameters["constraint_refs"])
        )
        expected_key = f"face:{loaded.root.entity_id}:1"
        self.assertEqual(references[0]["key"], expected_key)
        self.assertEqual(
            references[1]["mappings"][0]["reference_key"],
            expected_key,
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

    def test_revolve_arc_provenance_survives_edit_and_reload(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "ArcRevolve",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 20.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
            {"id": "center", "type": "point", "x": 15.0, "y": 5.0},
            {"id": "start", "type": "point", "x": 15.0, "y": 0.0},
            {"id": "end", "type": "point", "x": 15.0, "y": 10.0},
            {
                "id": "arc",
                "type": "arc",
                "arc_mode": "center",
                "clockwise": False,
                "point_ids": ["center", "start", "end"],
            },
            {
                "id": "closure",
                "type": "segment",
                "point_ids": ["end", "start"],
            },
        ]
        sketch = ZimaEntity(
            "ArcSketch",
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
            "ArcRevolveFeature",
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
            FaceRef(feature.entity_id, "generated", "arc"),
            FaceRef(feature.entity_id, "generated", "closure"),
        }
        expected_edges = {
            EdgeRef(feature.entity_id, role, source_id)
            for role in ("start", "end")
            for source_id in ("arc", "closure")
        } | {
            EdgeRef(feature.entity_id, "generated", point_id)
            for point_id in ("start", "end")
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, point_id)
            for role in ("start", "end")
            for point_id in ("start", "end")
        }

        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), expected_vertices)
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") == "center":
                entity["x"] = 18.0
            elif entity.get("id") in ("start", "end"):
                entity["x"] = 18.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), expected_faces)
        self.assertEqual(set(edited_registry.edge_references), expected_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), expected_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arc-revolve.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(set(loaded_registry.references), expected_faces)
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), expected_vertices
        )

    def test_closed_spline_extrusion_provenance_survives_edit_and_reload(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "SplineExtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 20.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 20.0},
            {
                "id": "spline",
                "type": "spline",
                "point_ids": ["a", "b", "c", "d", "a"],
            },
        ]
        sketch = ZimaEntity(
            "SplineSketch",
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
            "SplineFeature",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "15"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected_faces = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            FaceRef(feature.entity_id, "generated", "spline"),
        }
        expected_edges = {
            EdgeRef(feature.entity_id, role, "spline")
            for role in ("start", "end")
        } | {EdgeRef(feature.entity_id, "generated", "a")}

        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.edge_references), expected_edges)
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") == "c":
                entity["x"] = 24.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), expected_faces)
        self.assertEqual(set(edited_registry.edge_references), expected_edges)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closed-spline.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(set(loaded_registry.references), expected_faces)
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)

    def test_nested_mixed_extrusion_keeps_inner_wall_provenance(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "NestedExtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "outer-center", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "oa", "type": "point", "x": 0.0, "y": 40.0},
            {"id": "ob", "type": "point", "x": 50.0, "y": 0.0},
            {"id": "oc", "type": "point", "x": 50.0, "y": 40.0},
            {"id": "od", "type": "point", "x": 0.0, "y": 0.0},
            {
                "id": "outer-arc",
                "type": "arc",
                "arc_mode": "center",
                "clockwise": False,
                "point_ids": ["outer-center", "oa", "od"],
            },
            {"id": "outer-a", "type": "segment", "point_ids": ["od", "ob"]},
            {"id": "outer-b", "type": "segment", "point_ids": ["ob", "oc"]},
            {"id": "outer-c", "type": "segment", "point_ids": ["oc", "oa"]},
            {"id": "ia", "type": "point", "x": 15.0, "y": 12.0},
            {"id": "ib", "type": "point", "x": 35.0, "y": 12.0},
            {"id": "ic", "type": "point", "x": 35.0, "y": 28.0},
            {"id": "id", "type": "point", "x": 15.0, "y": 28.0},
            {
                "id": "inner-spline",
                "type": "spline",
                "point_ids": ["ia", "ib", "ic", "id", "ia"],
            },
        ]
        sketch = ZimaEntity(
            "NestedSketch",
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
            "NestedFeature",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "20"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected_faces = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            *(FaceRef(feature.entity_id, "generated", source_id) for source_id in (
                "outer-arc", "outer-a", "outer-b", "outer-c", "inner-spline"
            )),
        }
        source_ids = (
            "outer-arc", "outer-a", "outer-b", "outer-c", "inner-spline"
        )
        expected_edges = {
            EdgeRef(feature.entity_id, role, source_id)
            for role in ("start", "end")
            for source_id in source_ids
        } | {
            EdgeRef(feature.entity_id, "generated", point_id)
            for point_id in ("oa", "ob", "oc", "od", "ia")
        }

        shape = make_protrusion_shape(document, container)
        self.assertEqual(self._subshape_count(shape, TopAbs_SOLID), 1)
        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(
            registry.resolve(
                FaceRef(feature.entity_id, "generated", "inner-spline")
            ).state,
            TopologyResolutionState.RESOLVED,
        )
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") == "ic":
                entity["y"] = 30.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), expected_faces)
        self.assertEqual(set(edited_registry.edge_references), expected_edges)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested-mixed.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(set(loaded_registry.references), expected_faces)
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)

    def test_three_level_extrusion_keeps_cap_and_island_provenance(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "IslandExtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = []
        loop_sources: dict[str, tuple[str, ...]] = {}
        for prefix, coordinates in (
            ("outer", ((0, 0), (60, 0), (60, 50), (0, 50))),
            ("hole", ((10, 10), (50, 10), (50, 40), (10, 40))),
            ("island", ((20, 18), (40, 18), (40, 32), (20, 32))),
        ):
            point_ids = tuple(f"{prefix}-p{index}" for index in range(4))
            source_ids = tuple(f"{prefix}-e{index}" for index in range(4))
            loop_sources[prefix] = source_ids
            entities.extend(
                {"id": point_id, "type": "point", "x": x, "y": y}
                for point_id, (x, y) in zip(point_ids, coordinates)
            )
            entities.extend(
                {
                    "id": source_ids[index],
                    "type": "segment",
                    "point_ids": [
                        point_ids[index], point_ids[(index + 1) % 4]
                    ],
                }
                for index in range(4)
            )
        sketch = ZimaEntity(
            "IslandSketch",
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
            "IslandFeature",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "12"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        cap_references = {
            FaceRef(feature.entity_id, role, fragment=fragment)
            for role in ("start", "end")
            for fragment in (1, 2)
        }
        generated_references = {
            FaceRef(feature.entity_id, "generated", source_id)
            for source_ids in loop_sources.values()
            for source_id in source_ids
        }
        source_ids = tuple(
            source_id
            for loop in loop_sources.values()
            for source_id in loop
        )
        profile_point_ids = tuple(
            f"{prefix}-p{index}"
            for prefix in ("outer", "hole", "island")
            for index in range(4)
        )
        expected_edges = {
            EdgeRef(feature.entity_id, role, source_id)
            for role in ("start", "end")
            for source_id in source_ids
        } | {
            EdgeRef(feature.entity_id, "generated", point_id)
            for point_id in profile_point_ids
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, point_id)
            for role in ("start", "end")
            for point_id in profile_point_ids
        }
        shape = make_protrusion_shape(document, container)
        self.assertEqual(self._subshape_count(shape, TopAbs_SOLID), 2)
        registry = active_face_registry(document)
        self.assertEqual(
            set(registry.references),
            cap_references | generated_references,
        )
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), expected_vertices)
        for reference in cap_references:
            self.assertEqual(
                registry.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if str(entity.get("id", "")).startswith("island-p"):
                entity["x"] = float(entity["x"]) + 2.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(
            set(edited_registry.references), cap_references | generated_references
        )
        self.assertEqual(set(edited_registry.edge_references), expected_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), expected_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "three-level-island.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(
            set(loaded_registry.references),
            cap_references | generated_references,
        )
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), expected_vertices
        )

    def test_full_arc_revolve_cut_names_repeated_curved_intersections(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Box", ContainerType.BOX)
        base = document.create_primitive(
            base_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })
        container = ZimaEntity(
            "TorusCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 10.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
            {"id": "center", "type": "point", "x": 48.0, "y": 0.0},
            {"id": "start", "type": "point", "x": 48.0, "y": -5.0},
            {"id": "end", "type": "point", "x": 48.0, "y": 5.0},
            {
                "id": "arc",
                "type": "arc",
                "arc_mode": "center",
                "clockwise": False,
                "point_ids": ["center", "start", "end"],
            },
            {
                "id": "closure",
                "type": "segment",
                "point_ids": ["end", "start"],
            },
        ]
        sketch = ZimaEntity(
            "TorusSketch",
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
            "TorusRevolve",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": sketch.entity_id,
                "angle": "360",
                "extent_mode": "one_side",
                "direction": "forward",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        document.set_history_cursor(len(document.history_objects()))

        registry = active_face_registry(document)
        curved_intersections = {
            reference for reference in registry.edge_references
            if reference.feature_id == feature.entity_id
            and reference.role == "intersection"
        }
        self.assertTrue(curved_intersections)
        self.assertTrue(any(
            reference.fragment is not None
            for reference in curved_intersections
        ))
        for reference in curved_intersections:
            if reference.fragment is not None:
                self.assertEqual(
                    registry.resolve_edge(reference).state,
                    TopologyResolutionState.RESOLVED,
                )
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") in ("center", "start", "end"):
                entity["x"] = 47.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(
            set(edited_registry.edge_references),
            set(registry.edge_references),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "torus-cut.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(
            set(loaded_registry.edge_references),
            set(edited_registry.edge_references),
        )

    def test_closed_spline_full_revolve_keeps_generated_provenance(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "SplineRevolve",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -20.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 20.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
            {"id": "a", "type": "point", "x": 10.0, "y": -5.0},
            {"id": "b", "type": "point", "x": 20.0, "y": -5.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 5.0},
            {"id": "d", "type": "point", "x": 10.0, "y": 5.0},
            {
                "id": "spline",
                "type": "spline",
                "point_ids": ["a", "b", "c", "d", "a"],
            },
        ]
        sketch = ZimaEntity(
            "SplineRevolveSketch",
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
            "SplineRevolveFeature",
            EntityKind.REVOLVE,
            parameters={"sketch_id": sketch.entity_id, "angle": "360"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        registry = active_face_registry(document)
        self.assertEqual(
            set(registry.references),
            {FaceRef(feature.entity_id, "generated", "spline")},
        )
        self.assertIn(
            EdgeRef(feature.entity_id, "generated", "a"),
            registry.edge_references,
        )

    def test_three_level_partial_revolve_keeps_cap_fragments(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "NestedRevolve",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -40.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 40.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
        ]
        loop_sources: dict[str, tuple[str, ...]] = {}
        for prefix, coordinates in (
            ("outer", ((10, -25), (60, -25), (60, 25), (10, 25))),
            ("hole", ((20, -15), (50, -15), (50, 15), (20, 15))),
            ("island", ((30, -7), (40, -7), (40, 7), (30, 7))),
        ):
            point_ids = tuple(f"{prefix}-p{index}" for index in range(4))
            source_ids = tuple(f"{prefix}-e{index}" for index in range(4))
            loop_sources[prefix] = source_ids
            entities.extend(
                {"id": point_id, "type": "point", "x": x, "y": y}
                for point_id, (x, y) in zip(point_ids, coordinates)
            )
            entities.extend(
                {
                    "id": source_ids[index],
                    "type": "segment",
                    "point_ids": [
                        point_ids[index], point_ids[(index + 1) % 4]
                    ],
                }
                for index in range(4)
            )
        sketch = ZimaEntity(
            "NestedRevolveSketch",
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
            "NestedRevolveFeature",
            EntityKind.REVOLVE,
            parameters={"sketch_id": sketch.entity_id, "angle": "120"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        cap_references = {
            FaceRef(feature.entity_id, role, fragment=fragment)
            for role in ("start", "end")
            for fragment in (1, 2)
        }
        generated_references = {
            FaceRef(feature.entity_id, "generated", source_id)
            for source_ids in loop_sources.values()
            for source_id in source_ids
        }

        source_ids = tuple(
            source_id
            for loop in loop_sources.values()
            for source_id in loop
        )
        profile_point_ids = tuple(
            f"{prefix}-p{index}"
            for prefix in ("outer", "hole", "island")
            for index in range(4)
        )
        expected_edges = {
            EdgeRef(feature.entity_id, role, source_id)
            for role in ("start", "end")
            for source_id in source_ids
        } | {
            EdgeRef(feature.entity_id, "generated", point_id)
            for point_id in profile_point_ids
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, point_id)
            for role in ("start", "end")
            for point_id in profile_point_ids
        }
        shape = make_revolve_shape(document, container)
        self.assertEqual(self._subshape_count(shape, TopAbs_SOLID), 2)
        registry = active_face_registry(document)
        self.assertEqual(
            set(registry.references),
            cap_references | generated_references,
        )
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), expected_vertices)
        for reference in cap_references:
            self.assertEqual(
                registry.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if str(entity.get("id", "")).startswith("island-p"):
                entity["x"] = float(entity["x"]) + 1.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(
            set(edited_registry.references), cap_references | generated_references
        )
        self.assertEqual(set(edited_registry.edge_references), expected_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), expected_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "three-level-revolve.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(
            set(loaded_registry.references),
            cap_references | generated_references,
        )
        self.assertEqual(set(loaded_registry.edge_references), expected_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), expected_vertices
        )


if __name__ == "__main__":
    unittest.main()
