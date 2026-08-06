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
    make_chamfer_shape,
    make_fillet_shape,
    _unique_subshapes,
    protrusion_face_registry,
    revolve_face_registry,
    semantic_face_registry,
    ZimaEntity,
)
from OCC.Core.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_SOLID
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve
from OCC.Core.GeomAbs import GeomAbs_Ellipse
from zima_cad.sketch_model import SketchModel
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.topology import (
    AssemblyFaceRef,
    AssemblyEdgeRef,
    assembly_edge_descriptor,
    assembly_face_descriptor,
    EdgeRef,
    FaceRef,
    TopologyRegistry,
    TopologyResolutionState,
    VertexRef,
    decode_assembly_face_reference,
    decode_assembly_edge_reference,
    decode_edge_reference,
    decode_face_reference,
    decode_vertex_reference,
    encode_edge_reference,
    encode_face_reference,
    encode_vertex_reference,
    encode_assembly_face_reference,
    encode_assembly_edge_reference,
    parse_edge_reference,
    parse_face_reference,
    parse_vertex_reference,
    parse_assembly_face_reference,
    parse_assembly_face_descriptor,
    parse_assembly_edge_descriptor,
    resolve_assembly_face,
    resolve_assembly_edge,
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

    def test_topology_registry_is_reused_until_geometry_changes(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        solid = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(solid)

        first = active_face_registry(document)
        second = active_face_registry(document)
        self.assertIs(first, second)

        solid.parameters["length"] = "125"
        changed = active_face_registry(document)
        self.assertIsNot(first, changed)

    def test_format_9_is_deliberately_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.prtz"
            save_part_document(create_empty_part(), path)
            contents = path.read_text(encoding="utf-8")
            path.write_text(
                contents.replace("format_version = 10", "format_version = 9"),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "expected format 10"):
                load_part_document(path)

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
        self.assertEqual(
            parse_assembly_face_reference(first.to_dict()), first
        )
        self.assertEqual(
            decode_assembly_face_reference(
                encode_assembly_face_reference(first)
            ),
            first,
        )
        descriptor = assembly_face_descriptor(first)
        self.assertEqual(parse_assembly_face_descriptor(descriptor), first)
        self.assertIsNone(parse_assembly_face_descriptor(
            f"{first.instance_id}:face-ref:17"
        ))
        first_registry = TopologyRegistry()
        second_registry = TopologyRegistry()
        first_registry.register_face(face, "first-instance-face")
        second_registry.register_face(face, "second-instance-face")
        registries = {
            first.instance_id: first_registry,
            second.instance_id: second_registry,
        }
        self.assertEqual(
            resolve_assembly_face(first, registries).shape,
            "first-instance-face",
        )
        self.assertEqual(
            resolve_assembly_face(second, registries).shape,
            "second-instance-face",
        )
        second_registry.register_face(face, "ambiguous-second-face")
        self.assertEqual(
            resolve_assembly_face(second, registries).state,
            TopologyResolutionState.AMBIGUOUS,
        )
        self.assertEqual(
            resolve_assembly_face(
                AssemblyFaceRef("missing-component", face), registries
            ).state,
            TopologyResolutionState.MISSING,
        )

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

    def test_assembly_edge_reference_keeps_instance_separate(self) -> None:
        edge = EdgeRef("extrusion-1", "start", "circle-1")
        first = AssemblyEdgeRef("component-a", edge)
        second = AssemblyEdgeRef("component-b", edge)
        self.assertNotEqual(first, second)
        self.assertEqual(
            decode_assembly_edge_reference(
                encode_assembly_edge_reference(first)
            ),
            first,
        )
        descriptor = assembly_edge_descriptor(first)
        self.assertEqual(parse_assembly_edge_descriptor(descriptor), first)
        self.assertIsNone(parse_assembly_edge_descriptor(
            "component-a:edge:1"
        ))
        first_registry = TopologyRegistry()
        second_registry = TopologyRegistry()
        first_registry.register_edge(edge, "first-instance-edge")
        second_registry.register_edge(edge, "second-instance-edge")
        registries = {
            first.instance_id: first_registry,
            second.instance_id: second_registry,
        }
        self.assertEqual(
            resolve_assembly_edge(first, registries).shape,
            "first-instance-edge",
        )
        self.assertEqual(
            resolve_assembly_edge(second, registries).shape,
            "second-instance-edge",
        )
        second_registry.register_edge(edge, "ambiguous-edge")
        self.assertEqual(
            resolve_assembly_edge(second, registries).state,
            TopologyResolutionState.AMBIGUOUS,
        )

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

    def test_box_edge_and_vertex_roles_survive_dimension_changes(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        shape = document.build_standalone_shape(container)
        first = semantic_face_registry(document, box, shape)
        expected_edges = set(first.edge_references)
        expected_vertices = set(first.vertex_references)
        self.assertEqual(len(expected_edges), 12)
        self.assertEqual(len(expected_vertices), 8)

        box.parameters.update({"length": "240", "width": "35", "height": "81"})
        shape = document.build_standalone_shape(container)
        second = semantic_face_registry(document, box, shape)
        self.assertEqual(set(second.edge_references), expected_edges)
        self.assertEqual(set(second.vertex_references), expected_vertices)
        for reference in expected_edges:
            self.assertEqual(
                second.resolve_edge(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        for reference in expected_vertices:
            self.assertEqual(
                second.resolve_vertex(reference).state,
                TopologyResolutionState.RESOLVED,
            )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "box.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.edge_references), expected_edges)
            self.assertEqual(
                set(loaded_registry.vertex_references), expected_vertices
            )

    def test_fillet_uses_stable_box_edge_after_dimension_change(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)

        def build():
            shape = document.build_standalone_shape(container)
            registry = semantic_face_registry(document, box, shape)
            edge = next(reference for reference in registry.edge_references)
            return edge, make_fillet_shape(
                shape, registry, edge, 4.0, "fillet-1"
            )

        edge, (first_shape, first_registry) = build()
        generated = FaceRef(
            "fillet-1", "generated", semantic_provenance_id(edge)
        )
        self.assertEqual(
            first_registry.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertEqual(self._subshape_count(first_shape, TopAbs_SOLID), 1)

        box.parameters.update({"length": "140", "width": "65", "height": "45"})
        edited_edge, (edited_shape, edited_registry) = build()
        self.assertEqual(edited_edge, edge)
        self.assertEqual(
            edited_registry.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertEqual(self._subshape_count(edited_shape, TopAbs_SOLID), 1)

    def test_fillet_history_regenerates_from_stable_edge_reference(self) -> None:
        document = create_empty_part()
        box_container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(box_container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        source_shape = document.build_active_shape()
        source_registry = active_face_registry(document)
        edge = source_registry.edge_references[0]

        fillet_container = document.create_container(
            "Fillet", ContainerType.FILLET
        )
        fillet = ZimaEntity(
            name="Fillet",
            kind=EntityKind.FILLET,
            parameters={"edge_ref": edge.serialize(), "radius": "4"},
        )
        fillet_container.add_child(fillet)
        result = document.build_active_shape()
        self.assertEqual(self._subshape_count(result, TopAbs_SOLID), 1)
        self.assertNotEqual(
            self._subshape_count(result, TopAbs_FACE),
            self._subshape_count(source_shape, TopAbs_FACE),
        )
        generated = FaceRef(
            fillet.entity_id, "generated", semantic_provenance_id(edge)
        )
        self.assertEqual(
            active_face_registry(document).resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )

        box.parameters.update({"length": "140", "width": "65", "height": "45"})
        edited = document.build_active_shape()
        self.assertEqual(self._subshape_count(edited, TopAbs_SOLID), 1)
        self.assertNotIn("build_status", fillet.parameters)
        self.assertEqual(
            active_face_registry(document).resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fillet.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            self.assertEqual(
                self._subshape_count(loaded.build_active_shape(), TopAbs_SOLID),
                1,
            )
            self.assertEqual(
                active_face_registry(loaded).resolve(generated).state,
                TopologyResolutionState.RESOLVED,
            )

    def test_chamfer_history_regenerates_and_reloads(self) -> None:
        document = create_empty_part()
        box_container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(box_container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        source_shape = document.build_active_shape()
        source_registry = active_face_registry(document)
        edge = source_registry.edge_references[0]

        direct_shape, direct_registry = make_chamfer_shape(
            source_shape, source_registry, edge, 3.0, "direct-chamfer"
        )
        self.assertEqual(self._subshape_count(direct_shape, TopAbs_SOLID), 1)
        direct_generated = FaceRef(
            "direct-chamfer", "generated", semantic_provenance_id(edge)
        )
        self.assertEqual(
            direct_registry.resolve(direct_generated).state,
            TopologyResolutionState.RESOLVED,
        )

        chamfer_container = document.create_container(
            "Chamfer", ContainerType.CHAMFER
        )
        chamfer = ZimaEntity(
            name="Chamfer",
            kind=EntityKind.CHAMFER,
            parameters={"edge_ref": edge.serialize(), "distance": "3"},
        )
        chamfer_container.add_child(chamfer)
        result = document.build_active_shape()
        self.assertEqual(self._subshape_count(result, TopAbs_SOLID), 1)
        self.assertNotIn("build_status", chamfer.parameters)

        chamfer.parameters["distance"] = "5"
        box.parameters["length"] = "140"
        edited = document.build_active_shape()
        self.assertEqual(self._subshape_count(edited, TopAbs_SOLID), 1)
        self.assertNotIn("build_status", chamfer.parameters)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "chamfer.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_chamfer = next(
                child
                for obj in loaded.history_objects()
                if obj.container_type == ContainerType.CHAMFER
                for child in obj.children
                if child.kind == EntityKind.CHAMFER
            )
            self.assertEqual(loaded_chamfer.parameters["distance"], "5")
            self.assertEqual(
                self._subshape_count(loaded.build_active_shape(), TopAbs_SOLID),
                1,
            )
            self.assertNotIn("build_status", loaded_chamfer.parameters)

    def test_fillet_names_every_result_edge_for_following_features(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        source_shape = document.build_standalone_shape(container)
        source_registry = semantic_face_registry(document, box, source_shape)

        first_shape, first_registry = make_fillet_shape(
            source_shape,
            source_registry,
            source_registry.edge_references[0],
            4.0,
            "fillet-1",
        )
        edges = _unique_subshapes(first_shape, TopAbs_EDGE)
        self.assertTrue(edges)
        self.assertTrue(all(
            first_registry.edge_reference_for_runtime_index(index) is not None
            for index in range(1, len(edges) + 1)
        ))

        following_reference = next(
            reference
            for reference in first_registry.edge_references
            if reference.feature_id == "fillet-1"
        )
        second_shape, _second_registry = make_fillet_shape(
            first_shape,
            first_registry,
            following_reference,
            1.0,
            "fillet-2",
        )
        self.assertEqual(self._subshape_count(second_shape, TopAbs_SOLID), 1)

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
        self.assertEqual(
            {
                reference for reference in registry.edge_references
                if reference.role == "intersection"
            },
            expected_edges,
        )
        self.assertEqual(
            {
                reference for reference in registry.vertex_references
                if reference.role == "intersection"
            },
            expected_vertices,
        )
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
        self.assertEqual(
            {ref for ref in edited.edge_references if ref.role == "intersection"},
            expected_edges,
        )
        self.assertEqual(
            {ref for ref in edited.vertex_references if ref.role == "intersection"},
            expected_vertices,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "intersection.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(
            {
                ref for ref in loaded_registry.edge_references
                if ref.role == "intersection"
            },
            expected_edges,
        )
        self.assertEqual(
            {
                ref for ref in loaded_registry.vertex_references
                if ref.role == "intersection"
            },
            expected_vertices,
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

    def test_additive_bridge_joins_two_solid_ancestry_before_cut(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "BridgeSource",
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
            "BridgeSourceSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        source = ZimaEntity(
            "BridgeSourceFeature",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "12"},
        )
        container.add_child(sketch)
        container.add_child(source)
        document.root.add_child(container)

        bridge_container = document.create_container("Bridge", ContainerType.BOX)
        bridge = document.create_primitive(
            bridge_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(bridge)
        bridge.parameters.update({
            "length": "60", "width": "16", "height": "10"
        })
        bridge.combine_mode = CombineMode.ADD
        bridge_container.coordinate_system.origin = (15.0, -2.0, 5.0)

        cut_container = document.create_container("BridgeCut", ContainerType.BOX)
        cutter = document.create_primitive(
            cut_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(cutter)
        cutter.parameters.update({
            "length": "10", "width": "12", "height": "4"
        })
        cutter.combine_mode = CombineMode.SUBTRACT
        cut_container.coordinate_system.origin = (5.0, 0.0, 8.0)

        shape = document.build_active_shape()
        self.assertEqual(
            self._subshape_count(shape, TopAbs_SOLID),
            1,
            (bridge.parameters.get("build_status"), cutter.parameters.get("build_status")),
        )
        self.assertNotIn("build_status", bridge.parameters)
        self.assertNotIn("build_status", cutter.parameters)
        registry = active_face_registry(document)
        remembered_faces = set(registry.references)
        remembered_edges = set(registry.edge_references)
        remembered_vertices = set(registry.vertex_references)
        self.assertTrue(any(
            reference.feature_id == source.entity_id
            for reference in remembered_faces
        ))
        self.assertTrue(any(
            reference.feature_id == bridge.entity_id
            for reference in remembered_faces
        ))
        cut_intersections = {
            reference for reference in remembered_edges
            if reference.feature_id == cutter.entity_id
            and reference.role == "intersection"
        }
        self.assertTrue(
            cut_intersections
            or any(
                reference.feature_id == cutter.entity_id
                for reference in remembered_faces
            )
        )

        bridge.parameters["length"] = "62"
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), remembered_faces)
        self.assertEqual(set(edited_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), remembered_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "additive-bridge-chain.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_shape = loaded.build_active_shape()
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(self._subshape_count(loaded_shape, TopAbs_SOLID), 1)
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

    def test_ellipse_protrusion_uses_stable_curve_provenance(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "EllipseProtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 10.0},
            {
                "id": "ellipse-profile",
                "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        sketch = ZimaEntity(
            "EllipseSketch",
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
            "EllipseExtrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "12",
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
            FaceRef(feature.entity_id, "generated", "ellipse-profile"),
        }
        expected_edges = {
            EdgeRef(feature.entity_id, role, "ellipse-profile")
            for role in ("start", "end")
        }

        first = active_face_registry(document)
        self.assertEqual(set(first.references), expected_faces)
        self.assertEqual(set(first.edge_references), expected_edges)
        self.assertEqual(set(first.vertex_references), set())
        shape = make_protrusion_shape(document, container)
        explorer = TopExp_Explorer(shape, TopAbs_EDGE)
        exact_ellipse_edges = 0
        while explorer.More():
            if BRepAdaptor_Curve(explorer.Current()).GetType() == GeomAbs_Ellipse:
                exact_ellipse_edges += 1
            explorer.Next()
        self.assertGreaterEqual(exact_ellipse_edges, 2)
        shape_registry = protrusion_face_registry(
            document, container, shape
        )
        fillet_edge = EdgeRef(
            feature.entity_id, "start", "ellipse-profile"
        )
        filleted_shape, filleted_registry = make_fillet_shape(
            shape,
            shape_registry,
            fillet_edge,
            0.2,
            "ellipse-fillet",
        )
        fillet_face = FaceRef(
            "ellipse-fillet",
            "generated",
            semantic_provenance_id(fillet_edge),
        )
        self.assertEqual(
            filleted_registry.resolve(fillet_face).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertEqual(self._subshape_count(filleted_shape, TopAbs_SOLID), 1)

        edited, dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") == "major":
                entity["x"], entity["y"] = 24.0, 8.0
            elif entity.get("id") == "minor":
                entity["x"], entity["y"] = -4.0, 12.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        second = active_face_registry(document)
        self.assertEqual(set(second.references), expected_faces)
        self.assertEqual(set(second.edge_references), expected_edges)
        edited_shape = make_protrusion_shape(document, container)
        edited_shape_registry = protrusion_face_registry(
            document, container, edited_shape
        )
        edited_fillet_shape, edited_fillet_registry = make_fillet_shape(
            edited_shape,
            edited_shape_registry,
            fillet_edge,
            0.2,
            "ellipse-fillet",
        )
        self.assertEqual(
            edited_fillet_registry.resolve(fillet_face).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertEqual(
            self._subshape_count(edited_fillet_shape, TopAbs_SOLID),
            1,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ellipse-extrusion.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            restored = active_face_registry(loaded)
            self.assertEqual(set(restored.references), expected_faces)
            self.assertEqual(set(restored.edge_references), expected_edges)

    def test_elliptical_arc_protrusion_names_endpoints(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "EllipticalArcProtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "start", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "end", "type": "point", "x": -20.0, "y": 0.0},
            {
                "id": "ellipse-arc",
                "type": "elliptical_arc",
                "point_ids": ["center", "major", "minor", "start", "end"],
            },
            {
                "id": "closure",
                "type": "segment",
                "point_ids": ["end", "start"],
            },
        ]
        sketch = ZimaEntity(
            "EllipticalArcSketch",
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
            "EllipticalArcExtrusion",
            EntityKind.PROTRUSION,
            parameters={"sketch_id": sketch.entity_id, "length": "15"},
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected_faces = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            FaceRef(feature.entity_id, "generated", "ellipse-arc"),
            FaceRef(feature.entity_id, "generated", "closure"),
        }
        expected_vertices = {
            VertexRef(feature.entity_id, role, point_id)
            for role in ("start", "end")
            for point_id in ("start", "end")
        }

        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.vertex_references), expected_vertices)

    def test_ellipse_cut_keeps_provenance_after_axis_change_and_reload(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Base", ContainerType.BOX)
        base = document.create_primitive(base_container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100",
        })

        container = ZimaEntity(
            "EllipseCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        container.coordinate_system.origin = (-50.0, 0.0, 0.0)
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 18.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 9.0},
            {
                "id": "ellipse-profile",
                "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        sketch = ZimaEntity(
            "EllipseSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "yz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        feature = ZimaEntity(
            "EllipseCutter",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "100",
                "extent_mode": "one_side",
                "direction": "forward",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        feature.combine_mode = CombineMode.SUBTRACT
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        document.set_history_cursor(len(document.history_objects()))

        generated = FaceRef(
            feature.entity_id, "generated", "ellipse-profile"
        )
        first = active_face_registry(document)
        self.assertEqual(
            first.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )
        remembered_faces = set(first.references)
        remembered_edges = set(first.edge_references)
        self.assertTrue(any(
            reference.feature_id == feature.entity_id
            for reference in remembered_edges
        ))

        edited, dimensions = SketchModel.from_dict(json.loads(
            str(sketch.parameters["sketch_data"])
        )).to_editor_data()
        for entity in edited:
            if entity.get("id") == "major":
                entity["x"], entity["y"] = 22.0, 4.0
            elif entity.get("id") == "minor":
                entity["x"], entity["y"] = -2.0, 11.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        second = active_face_registry(document)
        self.assertEqual(set(second.references), remembered_faces)
        self.assertEqual(set(second.edge_references), remembered_edges)
        self.assertEqual(
            second.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ellipse-cut.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            restored = active_face_registry(loaded)
        self.assertEqual(set(restored.references), remembered_faces)
        self.assertEqual(set(restored.edge_references), remembered_edges)
        self.assertEqual(
            restored.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )

    def test_ellipse_fillet_history_survives_axis_change_and_reload(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "EllipseProtrusion",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 10.0},
            {
                "id": "ellipse-profile",
                "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        sketch = ZimaEntity(
            "EllipseSketch",
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
            "EllipseExtrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "20",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        source_edge = EdgeRef(
            feature.entity_id, "start", "ellipse-profile"
        )
        fillet_container = document.create_container(
            "EllipseFillet", ContainerType.FILLET
        )
        fillet = ZimaEntity(
            "EllipseFillet",
            EntityKind.FILLET,
            parameters={
                "edge_ref": source_edge.serialize(),
                "radius": "0.5",
            },
        )
        fillet_container.add_child(fillet)
        generated = FaceRef(
            fillet.entity_id,
            "generated",
            semantic_provenance_id(source_edge),
        )

        first_shape = document.build_active_shape()
        first = active_face_registry(document)
        self.assertEqual(self._subshape_count(first_shape, TopAbs_SOLID), 1)
        self.assertEqual(
            first.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertNotIn("build_status", fillet.parameters)

        edited, dimensions = SketchModel.from_dict(json.loads(
            str(sketch.parameters["sketch_data"])
        )).to_editor_data()
        for entity in edited:
            if entity.get("id") == "major":
                entity["x"], entity["y"] = 26.0, 5.0
            elif entity.get("id") == "minor":
                entity["x"], entity["y"] = -2.5, 13.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_shape = document.build_active_shape()
        edited_registry = active_face_registry(document)
        self.assertEqual(self._subshape_count(edited_shape, TopAbs_SOLID), 1)
        self.assertEqual(
            edited_registry.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )
        self.assertNotIn("build_status", fillet.parameters)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ellipse-fillet-history.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_shape = loaded.build_active_shape()
            loaded_registry = active_face_registry(loaded)
        self.assertEqual(self._subshape_count(loaded_shape, TopAbs_SOLID), 1)
        self.assertEqual(
            loaded_registry.resolve(generated).state,
            TopologyResolutionState.RESOLVED,
        )

    def test_partial_ellipse_revolve_has_stable_topology(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "EllipseRevolve",
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
            {"id": "center", "type": "point", "x": 30.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 40.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 30.0, "y": 5.0},
            {
                "id": "ellipse-profile",
                "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        sketch = ZimaEntity(
            "EllipseRevolveSketch",
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
            "EllipseRevolveFeature",
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
            FaceRef(feature.entity_id, "generated", "ellipse-profile"),
        }
        expected_edges = {
            EdgeRef(feature.entity_id, role, "ellipse-profile")
            for role in ("start", "end")
        }

        registry = active_face_registry(document)
        self.assertEqual(set(registry.references), expected_faces)
        self.assertEqual(set(registry.edge_references), expected_edges)
        self.assertEqual(set(registry.vertex_references), set())

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

    def test_crossing_cylinder_and_torus_cuts_keep_curved_ancestry(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Box", ContainerType.BOX)
        base = document.create_primitive(
            base_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })

        cylinder_container = ZimaEntity(
            "CylinderCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        cylinder_container.coordinate_system.origin = (45.0, 0.0, 0.0)
        cylinder_sketch = ZimaEntity(
            "CylinderSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "yz",
                "profile": "circle",
                "diameter": "20",
                "sketch_data": json.dumps(SketchModel().to_dict()),
            },
        )
        cylinder = ZimaEntity(
            "CylinderFeature",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": cylinder_sketch.entity_id,
                "length_forward": "30",
                "extent_mode": "one_side",
                "direction": "forward",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        cylinder_container.add_child(cylinder_sketch)
        cylinder_container.add_child(cylinder)
        document.root.add_child(cylinder_container)

        torus_container = ZimaEntity(
            "TorusCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.REVOLVE.value},
        )
        torus_entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 10.0},
            {
                "id": "axis", "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
            {"id": "center", "type": "point", "x": 48.0, "y": 0.0},
            {"id": "start", "type": "point", "x": 48.0, "y": -5.0},
            {"id": "end", "type": "point", "x": 48.0, "y": 5.0},
            {
                "id": "arc", "type": "arc", "arc_mode": "center",
                "clockwise": False,
                "point_ids": ["center", "start", "end"],
            },
            {
                "id": "closure", "type": "segment",
                "point_ids": ["end", "start"],
            },
        ]
        torus_sketch = ZimaEntity(
            "TorusSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(torus_entities).to_dict()
                ),
            },
        )
        torus = ZimaEntity(
            "TorusFeature",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": torus_sketch.entity_id,
                "angle": "360",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        torus_container.add_child(torus_sketch)
        torus_container.add_child(torus)
        document.root.add_child(torus_container)
        document.set_history_cursor(len(document.history_objects()))

        registry = active_face_registry(document)
        remembered_faces = set(registry.references)
        remembered_edges = set(registry.edge_references)
        remembered_vertices = set(registry.vertex_references)
        for feature in (cylinder, torus):
            curved_intersections = {
                reference for reference in remembered_edges
                if reference.feature_id == feature.entity_id
                and reference.role == "intersection"
            }
            self.assertTrue(curved_intersections)
            self.assertTrue(any(
                registry.resolve_edge(reference).state
                == TopologyResolutionState.RESOLVED
                for reference in curved_intersections
            ))
        self.assertTrue(any(
            reference.feature_id == torus.entity_id
            and reference.role == "intersection"
            and cylinder.entity_id in str(reference.source_id)
            for reference in remembered_edges
        ))

        edited, dimensions = SketchModel.from_dict(
            json.loads(str(torus_sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") in ("center", "start", "end"):
                entity["x"] = 47.0
        torus_sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        cylinder_sketch.parameters["diameter"] = "19"
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), remembered_faces)
        self.assertEqual(set(edited_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), remembered_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "crossing-curved-cuts.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(set(loaded_registry.references), remembered_faces)
        self.assertEqual(set(loaded_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), remembered_vertices
        )

    def test_spline_cut_keeps_ancestry_across_cylindrical_cut(self) -> None:
        document = create_empty_part()
        base_container = document.create_container("Box", ContainerType.BOX)
        base = document.create_primitive(
            base_container.entity_id, EntityKind.BOX
        )
        self.assertIsNotNone(base)
        base.parameters.update({
            "length": "100", "width": "100", "height": "100"
        })

        cylinder_container = ZimaEntity(
            "CylinderCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        cylinder_container.coordinate_system.origin = (45.0, 0.0, 0.0)
        cylinder_sketch = ZimaEntity(
            "CylinderSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "yz", "profile": "circle", "diameter": "20",
                "sketch_data": json.dumps(SketchModel().to_dict()),
            },
        )
        cylinder = ZimaEntity(
            "CylinderFeature",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": cylinder_sketch.entity_id,
                "length_forward": "30",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        cylinder_container.add_child(cylinder_sketch)
        cylinder_container.add_child(cylinder)
        document.root.add_child(cylinder_container)

        spline_container = ZimaEntity(
            "SplineCut",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        spline_container.coordinate_system.origin = (45.0, 0.0, 0.0)
        spline_entities = [
            {"id": "a", "type": "point", "x": -5.0, "y": -5.0},
            {"id": "b", "type": "point", "x": 15.0, "y": -5.0},
            {"id": "c", "type": "point", "x": 15.0, "y": 15.0},
            {"id": "d", "type": "point", "x": -5.0, "y": 15.0},
            {
                "id": "spline", "type": "spline",
                "point_ids": ["a", "b", "c", "d", "a"],
            },
        ]
        spline_sketch = ZimaEntity(
            "SplineCutSketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "yz", "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(spline_entities).to_dict()
                ),
            },
        )
        spline = ZimaEntity(
            "SplineFeature",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": spline_sketch.entity_id,
                "length_forward": "30",
                "operation": CombineMode.SUBTRACT.value,
            },
        )
        spline_container.add_child(spline_sketch)
        spline_container.add_child(spline)
        document.root.add_child(spline_container)
        document.set_history_cursor(len(document.history_objects()))

        registry = active_face_registry(document)
        remembered_faces = set(registry.references)
        remembered_edges = set(registry.edge_references)
        remembered_vertices = set(registry.vertex_references)
        spline_face = FaceRef(spline.entity_id, "generated", "spline")
        self.assertIn(spline_face, remembered_faces)
        crossing_references = {
            reference for reference in remembered_edges
            if reference.feature_id == spline.entity_id
            and reference.role == "intersection"
            and cylinder.entity_id in str(reference.source_id)
        }
        self.assertTrue(crossing_references)
        self.assertTrue(any(
            registry.resolve_edge(reference).state
            == TopologyResolutionState.RESOLVED
            for reference in crossing_references
        ))

        edited, dimensions = SketchModel.from_dict(
            json.loads(str(spline_sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited:
            if entity.get("id") == "c":
                entity["x"] = 14.0
        spline_sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(edited, dimensions).to_dict()
        )
        edited_registry = active_face_registry(document)
        self.assertEqual(set(edited_registry.references), remembered_faces)
        self.assertEqual(set(edited_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(edited_registry.vertex_references), remembered_vertices
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "spline-cylinder-cuts.prtz"
            save_part_document(document, path)
            loaded_registry = active_face_registry(load_part_document(path))
        self.assertEqual(set(loaded_registry.references), remembered_faces)
        self.assertEqual(set(loaded_registry.edge_references), remembered_edges)
        self.assertEqual(
            set(loaded_registry.vertex_references), remembered_vertices
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
