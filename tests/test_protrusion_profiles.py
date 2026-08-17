import json
import unittest

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.BRep import BRep_Tool
from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib
from OCC.Core.BRepCheck import BRepCheck_Analyzer
from OCC.Core.GProp import GProp_GProps
from OCC.Core.TopAbs import TopAbs_FACE, TopAbs_SOLID, TopAbs_VERTEX
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox
from OCC.Core.BRepBuilderAPI import BRepBuilderAPI_MakeFace
from OCC.Core.GeomAPI import GeomAPI_PointsToBSplineSurface
from OCC.Core.TColgp import TColgp_Array2OfPnt
from OCC.Core.gp import gp_Dir, gp_Pln, gp_Pnt

from zima_cad.app import MainWindow
from zima_cad.model import (
    CombineMode,
    CoordinateSystem,
    ContainerType,
    EntityKind,
    ZimaEntity,
    create_empty_part,
    make_protrusion_shape,
    make_revolve_shape,
    make_sketch_shape,
    protrusion_face_registry,
    sketch_profile_status,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.topology import EdgeRef, FaceRef, TopologyRegistry


class ProtrusionProfileTests(unittest.TestCase):
    @staticmethod
    def _build_profile(
        entities,
        profile_offset=0.0,
        result_type="solid",
        through_history=False,
        end_condition="length",
        extent_mode="one_side",
        end_reference=None,
        input_shape=None,
        input_registry=None,
        return_feature=False,
        thin_thickness=1.0,
        thin_mode="one_side",
        plane="xz",
    ):
        document = create_empty_part()
        container = ZimaEntity(
            "Protrusion001",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={
                "plane": plane,
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
                "extent_mode": extent_mode,
                "direction": "forward",
                "profile_offset": f"{profile_offset:.12g}",
                "result_type": result_type,
                "thin_thickness": f"{thin_thickness:.12g}",
                "thin_mode": thin_mode,
                "end_condition_forward": end_condition,
                "end_targets_forward": json.dumps(
                    [end_reference] if isinstance(end_reference, dict) else []
                ),
                "operation": (
                    CombineMode.SUBTRACT.value
                    if end_condition == "through_all"
                    else CombineMode.ADD.value
                ),
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        shape = (
            document.build_shape_for_objects([container])
            if through_history
            else make_protrusion_shape(
                document, container, input_shape=input_shape,
                input_registry=input_registry,
            )
        )
        return (shape, feature) if return_feature else shape

    def test_symmetric_up_to_uses_one_face_as_an_absolute_half_length(self):
        entities = [
            {"id": "a", "type": "point", "x": 1.0, "y": 1.0},
            {"id": "b", "type": "point", "x": 9.0, "y": 1.0},
            {"id": "c", "type": "point", "x": 9.0, "y": 9.0},
            {"id": "d", "type": "point", "x": 1.0, "y": 9.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        shape, feature = self._build_profile(
            entities,
            end_condition="up_to",
            extent_mode="symmetric",
            end_reference={
                "kind": "plane",
                "fallback_origin": [0.0, -20.0, 0.0],
                "fallback_normal": [0.0, 1.0, 0.0],
            },
            return_feature=True,
        )

        self.assertIsNotNone(shape)
        self.assertAlmostEqual(
            float(feature.parameters["evaluated_length_forward"]), 20.0
        )
        self.assertAlmostEqual(
            float(feature.parameters["evaluated_length_reverse"]), 20.0
        )

    def test_inclined_up_to_uses_supporting_plane_not_bounded_face(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 10.0, "y": 10.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        reference = FaceRef("target", "generated", "edge")
        registry = TopologyRegistry()
        bounded_face = BRepBuilderAPI_MakeFace(
            gp_Pln(gp_Pnt(0.0, 20.0, 0.0), gp_Dir(1.0, -1.0, 0.0)),
            -5.0, 5.0, -5.0, 5.0,
        ).Face()
        registry.register_face(reference, bounded_face)
        shape = self._build_profile(
            entities,
            end_condition="up_to",
            end_reference={
                "kind": "face",
                "fallback_origin": [0.0, 20.0, 0.0],
                "fallback_normal": [1.0, -1.0, 0.0],
                "reference": reference.to_dict(),
            },
            input_registry=registry,
        )

        vertices = []
        explorer = TopExp_Explorer(shape, TopAbs_VERTEX)
        while explorer.More():
            point = BRep_Tool.Pnt(explorer.Current())
            position = (point.X(), point.Y(), point.Z())
            if not any(
                sum((position[i] - old[i]) ** 2 for i in range(3)) < 1.0e-12
                for old in vertices
            ):
                vertices.append(position)
            explorer.Next()
        self.assertEqual(len(vertices), 8)
        self.assertEqual(
            sum(abs(point[0] - point[1] + 20.0) < 1.0e-6 for point in vertices),
            4,
        )

    def test_up_to_accepts_a_general_spline_surface(self):
        entities = [
            {"id": "a", "type": "point", "x": 1.0, "y": 1.0},
            {"id": "b", "type": "point", "x": 9.0, "y": 1.0},
            {"id": "c", "type": "point", "x": 9.0, "y": 9.0},
            {"id": "d", "type": "point", "x": 1.0, "y": 9.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        poles = TColgp_Array2OfPnt(1, 4, 1, 4)
        for row in range(1, 5):
            for column in range(1, 5):
                poles.SetValue(
                    row,
                    column,
                    gp_Pnt(
                        (row - 1) * 10.0 / 3.0,
                        10.0 + (1.0 if (row + column) % 2 else 0.0),
                        (column - 1) * 10.0 / 3.0,
                    ),
                )
        target_face = BRepBuilderAPI_MakeFace(
            GeomAPI_PointsToBSplineSurface(poles).Surface(), 1.0e-7
        ).Face()
        reference = FaceRef("target", "generated", "spline-surface")
        registry = TopologyRegistry()
        registry.register_face(reference, target_face)

        shape = self._build_profile(
            entities,
            end_condition="up_to",
            end_reference={
                "kind": "face",
                "surface_kind": "other",
                "reference": reference.to_dict(),
            },
            input_registry=registry,
        )

        self.assertHasVolume(shape)
        end_heights = []
        explorer = TopExp_Explorer(shape, TopAbs_VERTEX)
        while explorer.More():
            point = BRep_Tool.Pnt(explorer.Current())
            if point.Y() > 1.0:
                end_heights.append(round(point.Y(), 6))
            explorer.Next()
        self.assertGreaterEqual(len(set(end_heights)), 2)

        ellipse = [
            {"id": "center", "type": "point", "x": 5.0, "y": 5.0},
            {"id": "major", "type": "point", "x": 8.0, "y": 5.0},
            {"id": "minor", "type": "point", "x": 5.0, "y": 7.0},
            {
                "id": "ellipse", "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        self.assertHasVolume(self._build_profile(
            ellipse,
            result_type="thin",
            thin_thickness=0.5,
            end_condition="up_to",
            end_reference={
                "kind": "face",
                "surface_kind": "other",
                "reference": reference.to_dict(),
            },
            input_registry=registry,
        ))

    def test_dependent_multi_profile_source_does_not_mark_valid_up_to_red(self):
        document = create_empty_part()
        target_container = document.create_container(
            "Target",
            ContainerType.BOX,
        )
        target_box = document.create_primitive(
            target_container.entity_id,
            EntityKind.BOX,
        )
        self.assertIsNotNone(target_box)

        entities = [
            {"id": "a", "type": "point", "x": -10.0, "y": -8.0},
            {"id": "b", "type": "point", "x": -6.0, "y": -8.0},
            {"id": "c", "type": "point", "x": -6.0, "y": -4.0},
            {"id": "d", "type": "point", "x": -10.0, "y": -4.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
            {"id": "e", "type": "point", "x": 6.0, "y": 4.0},
            {"id": "f", "type": "point", "x": 10.0, "y": 4.0},
            {"id": "g", "type": "point", "x": 10.0, "y": 8.0},
            {"id": "h", "type": "point", "x": 6.0, "y": 8.0},
            {"id": "ef", "type": "segment", "point_ids": ["e", "f"]},
            {"id": "fg", "type": "segment", "point_ids": ["f", "g"]},
            {"id": "gh", "type": "segment", "point_ids": ["g", "h"]},
            {"id": "he", "type": "segment", "point_ids": ["h", "e"]},
            {"id": "o", "type": "point", "x": 0.0, "y": 0.0},
            {
                "id": "circle",
                "type": "circle",
                "point_ids": ["o"],
                "radius": 2.0,
            },
        ]
        source_container = document.create_container(
            "Profiles",
            ContainerType.PROTRUSION,
        )
        source_container.coordinate_system = CoordinateSystem(
            origin=(30.0, 0.0, 0.0)
        )
        sketch = document.create_sketch(source_container.entity_id, plane="yz")
        self.assertIsNotNone(sketch)
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(entities).to_dict()
        )
        feature = ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "50",
                "length_reverse": "0",
                "extent_mode": "one_side",
                "direction": "reverse",
                "result_type": "solid",
                "operation": CombineMode.ADD.value,
                "end_condition_forward": "up_to",
                "end_targets_forward": json.dumps([{
                    "kind": "face",
                    # Exercise the same general supporting-surface path as
                    # an extruded ellipse side in Projects/01.prtz.
                    "surface_kind": "other",
                    "reference": FaceRef(
                        target_box.entity_id,
                        "x_max",
                    ).to_dict(),
                }]),
                "end_condition_reverse": "length",
                "end_targets_reverse": "[]",
            },
        )
        source_container.add_child(feature)

        # Simulate the diagnostic left by an earlier failed edit.  A
        # successful authoritative calculation must clear it before the UI
        # reads the feature status for the Properties label and tree row.
        feature.parameters["build_status"] = "up_to_reference_unresolved"
        body = document.build_active_shape()
        self.assertHasVolume(body)
        self.assertNotIn("build_status", feature.parameters)

        # Regeneration builds this auxiliary packet after the authoritative
        # Body. It needs the preceding Body only to resolve the target face;
        # the packet itself must contain the three uncombined profile solids.
        source = document.build_standalone_shape(source_container)
        self.assertHasVolume(source)
        source_solids = TopExp_Explorer(source, TopAbs_SOLID)
        count = 0
        while source_solids.More():
            count += 1
            source_solids.Next()
        self.assertEqual(count, 3)
        self.assertNotIn("build_status", feature.parameters)

    def test_through_all_uses_a_calculated_length_without_replacing_manual_length(self):
        entities = [
            {"id": "a", "type": "point", "x": 1.0, "y": 1.0},
            {"id": "b", "type": "point", "x": 9.0, "y": 1.0},
            {"id": "c", "type": "point", "x": 9.0, "y": 9.0},
            {"id": "d", "type": "point", "x": 1.0, "y": 9.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        shape, feature = self._build_profile(
            entities,
            end_condition="through_all",
            input_shape=BRepPrimAPI_MakeBox(10.0, 20.0, 10.0).Shape(),
            return_feature=True,
        )

        self.assertIsNotNone(shape)
        self.assertEqual(feature.parameters["length_forward"], "10")
        self.assertGreater(
            float(feature.parameters["evaluated_length_forward"]), 20.0
        )

    def test_open_profile_requires_thin_and_creates_a_solid(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
        ]
        self.assertIsNone(self._build_profile(entities))
        thin = self._build_profile(entities, result_type="thin")
        self.assertIsNotNone(thin)
        self.assertTrue(TopExp_Explorer(thin, TopAbs_SOLID).More())
        history_thin = self._build_profile(
            entities,
            result_type="thin",
            through_history=True,
        )
        self.assertIsNotNone(history_thin)
        self.assertTrue(TopExp_Explorer(history_thin, TopAbs_SOLID).More())
        properties = GProp_GProps()
        brepgprop.VolumeProperties(thin, properties)
        self.assertGreater(abs(float(properties.Mass())), 1.0e-6)

    def test_closed_rectangle_can_create_either_solid_or_thin(self):
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
        solid = self._build_profile(entities, result_type="solid")
        thin = self._build_profile(entities, result_type="thin")

        self.assertIsNotNone(solid)
        self.assertIsNotNone(thin)
        self.assertTrue(TopExp_Explorer(solid, TopAbs_SOLID).More())
        self.assertTrue(TopExp_Explorer(thin, TopAbs_SOLID).More())
        solid_properties = GProp_GProps()
        thin_properties = GProp_GProps()
        brepgprop.VolumeProperties(solid, solid_properties)
        brepgprop.VolumeProperties(thin, thin_properties)
        self.assertGreater(float(solid_properties.Mass()), 0.0)
        self.assertGreater(float(thin_properties.Mass()), 0.0)
        self.assertLess(
            float(thin_properties.Mass()), float(solid_properties.Mass())
        )

    def test_open_curves_create_valid_thin_solids_in_every_side_mode(self):
        profiles = {
            "arc": [
                {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "start", "type": "point", "x": 10.0, "y": 0.0},
                {"id": "end", "type": "point", "x": 0.0, "y": 10.0},
                {
                    "id": "arc", "type": "arc", "arc_mode": "center",
                    "radius": 10.0,
                    "point_ids": ["center", "start", "end"],
                },
            ],
            "elliptical_arc": [
                {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "major", "type": "point", "x": 10.0, "y": 0.0},
                {"id": "minor", "type": "point", "x": 0.0, "y": 5.0},
                {"id": "start", "type": "point", "x": 10.0, "y": 0.0},
                {"id": "end", "type": "point", "x": 0.0, "y": 5.0},
                {
                    "id": "elliptical-arc", "type": "elliptical_arc",
                    "point_ids": [
                        "center", "major", "minor", "start", "end",
                    ],
                },
            ],
            "spline": [
                {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "m1", "type": "point", "x": 8.0, "y": 5.0},
                {"id": "m2", "type": "point", "x": 15.0, "y": -3.0},
                {"id": "b", "type": "point", "x": 22.0, "y": 2.0},
                {
                    "id": "spline", "type": "spline",
                    "point_ids": ["a", "m1", "m2", "b"],
                },
            ],
        }
        for name, entities in profiles.items():
            for mode in ("one_side", "other_side", "symmetric"):
                with self.subTest(profile=name, mode=mode):
                    shape = self._build_profile(
                        entities,
                        result_type="thin",
                        thin_thickness=0.75,
                        thin_mode=mode,
                    )
                    self.assertHasVolume(shape)

    def test_closed_ellipse_creates_both_solid_and_thin(self):
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 5.0},
            {
                "id": "ellipse", "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        self.assertHasVolume(self._build_profile(entities))
        for plane in ("xy", "xz", "yz"):
            for mode in ("one_side", "other_side", "symmetric"):
                with self.subTest(plane=plane, mode=mode):
                    self.assertHasVolume(self._build_profile(
                        entities,
                        result_type="thin",
                        thin_thickness=0.75,
                        thin_mode=mode,
                        plane=plane,
                    ))
        self.assertIsNone(self._build_profile(
            entities,
            result_type="thin",
            thin_thickness=7.0,
            thin_mode="one_side",
        ))

    def test_circle_entity_creates_both_solid_and_thin(self):
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {
                "id": "circle", "type": "circle",
                "point_ids": ["center"], "radius": 6.0,
            },
        ]
        self.assertHasVolume(self._build_profile(entities))
        for mode in ("one_side", "other_side", "symmetric"):
            with self.subTest(mode=mode):
                self.assertHasVolume(self._build_profile(
                    entities,
                    result_type="thin",
                    thin_thickness=0.75,
                    thin_mode=mode,
                ))

    def test_ellipse_thin_keeps_named_inner_and_outer_topology(self):
        document = create_empty_part()
        container = ZimaEntity(
            "ThinEllipse",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 5.0},
            {
                "id": "ellipse", "type": "ellipse",
                "point_ids": ["center", "major", "minor"],
            },
        ]
        sketch = ZimaEntity(
            "Sketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz", "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        feature = ZimaEntity(
            "Thin",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "10",
                "extent_mode": "one_side",
                "direction": "forward",
                "result_type": "thin",
                "thin_thickness": "0.75",
                "thin_mode": "one_side",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        expected_faces = {
            FaceRef(feature.entity_id, "inside", "ellipse"),
            FaceRef(feature.entity_id, "outside", "ellipse"),
        }
        expected_edges = {
            EdgeRef(feature.entity_id, f"{cap}_{side}", "ellipse")
            for cap in ("start", "end")
            for side in ("inside", "outside")
        }
        for thickness in ("0.75", "0.5"):
            feature.parameters["thin_thickness"] = thickness
            shape = make_protrusion_shape(document, container)
            self.assertHasVolume(shape)
            registry = protrusion_face_registry(document, container, shape)
            self.assertTrue(expected_faces.issubset(registry.references))
            self.assertTrue(
                expected_edges.issubset(registry.edge_references)
            )

    def test_corner_radius_is_used_by_solid_and_thin_profiles(self):
        closed = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 10.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 10.0},
            {
                "id": "ab", "type": "segment", "point_ids": ["a", "b"],
                "corner_radii": [{
                    "id": "round-b",
                    "other_geometry_id": "bc",
                    "vertex_id": "b",
                    "radius": 2.0,
                }],
            },
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        solid = self._build_profile(closed)
        thin = self._build_profile(closed, result_type="thin")
        self.assertHasVolume(solid)
        self.assertHasVolume(thin)
        properties = GProp_GProps()
        brepgprop.VolumeProperties(solid, properties)
        expected_volume = (200.0 - 4.0 + 3.141592653589793) * 10.0
        self.assertAlmostEqual(float(properties.Mass()), expected_volume, places=5)

        open_profile = closed[:3] + closed[4:6]
        self.assertHasVolume(self._build_profile(
            open_profile,
            result_type="thin",
        ))

    def test_profile_status_distinguishes_open_and_closed(self):
        def sketch(entities):
            return ZimaEntity(
                "Sketch",
                EntityKind.SKETCH,
                parameters={
                    "profile": "entities",
                    "sketch_data": json.dumps(
                        SketchModel.from_editor_data(entities).to_dict()
                    ),
                },
            )

        open_entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
        ]
        closed_entities = open_entities + [
            {"id": "c", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "ca", "type": "segment", "point_ids": ["c", "a"]},
        ]
        self.assertEqual(sketch_profile_status(sketch(open_entities)), "open")
        self.assertEqual(sketch_profile_status(sketch(closed_entities)), "closed")

    def test_open_profile_can_create_revolved_thin_solid(self):
        entities = [
            {"id": "axis_a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis_b", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "a", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 15.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis_a", "axis_b"],
            },
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
        ]
        document = create_empty_part()
        container = ZimaEntity("RevolveSurface", EntityKind.CONTAINER)
        sketch = ZimaEntity(
            "Sketch",
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
            "Revolve",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": sketch.entity_id,
                "angle": "180",
                "extent_mode": "one_side",
                "direction": "forward",
                "result_type": "thin",
                "thin_thickness": "1",
                "thin_mode": "one_side",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        thin = make_revolve_shape(document, container)
        self.assertIsNotNone(thin)
        self.assertTrue(TopExp_Explorer(thin, TopAbs_SOLID).More())
        properties = GProp_GProps()
        brepgprop.VolumeProperties(thin, properties)
        self.assertGreater(abs(float(properties.Mass())), 1.0e-6)

    def test_elliptical_arc_can_create_revolved_thin_solid(self):
        entities = [
            {"id": "axis-a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis-b", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "center", "type": "point", "x": 12.0, "y": 5.0},
            {"id": "major", "type": "point", "x": 18.0, "y": 5.0},
            {"id": "minor", "type": "point", "x": 12.0, "y": 9.0},
            {"id": "start", "type": "point", "x": 18.0, "y": 5.0},
            {"id": "end", "type": "point", "x": 12.0, "y": 9.0},
            {
                "id": "axis", "type": "construction",
                "point_ids": ["axis-a", "axis-b"],
            },
            {
                "id": "elliptical-arc", "type": "elliptical_arc",
                "point_ids": [
                    "center", "major", "minor", "start", "end",
                ],
            },
        ]
        document = create_empty_part()
        container = ZimaEntity("RevolveEllipse", EntityKind.CONTAINER)
        sketch = ZimaEntity(
            "Sketch",
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
            "Revolve",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": sketch.entity_id,
                "angle": "180",
                "extent_mode": "one_side",
                "direction": "forward",
                "result_type": "thin",
                "thin_thickness": "1",
                "thin_mode": "symmetric",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        self.assertHasVolume(make_revolve_shape(document, container))

    def assertHasVolume(self, shape):
        self.assertIsNotNone(shape)
        self.assertTrue(BRepCheck_Analyzer(shape).IsValid())
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        self.assertGreater(abs(float(properties.Mass())), 1.0e-6)

    def test_sketch_profile_offset_is_local_to_its_plane(self):
        parent = ZimaEntity(
            "SketchContainer",
            EntityKind.CONTAINER,
            coordinate_system=CoordinateSystem(origin=(100.0, 200.0, 300.0)),
        )
        sketch = ZimaEntity(
            "Sketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "profile_offset": "7",
                "sketch_data": json.dumps(SketchModel.from_editor_data([
                    {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
                    {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
                    {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
                ]).to_dict()),
            },
        )
        parent.add_child(sketch)

        shape = make_sketch_shape(parent, sketch)
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        _xmin, ymin, _zmin, _xmax, ymax, _zmax = bounds.Get()
        self.assertAlmostEqual(ymin, 207.0, places=6)
        self.assertAlmostEqual(ymax, 207.0, places=6)

    def test_closed_profile_can_include_center_arc(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "top", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "bottom", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "rt", "type": "point", "x": 20.0, "y": 10.0},
            {"id": "rb", "type": "point", "x": 20.0, "y": -10.0},
            {"id": "top_line", "type": "segment", "point_ids": ["rt", "top"]},
            {
                "id": "arc",
                "type": "arc",
                "arc_mode": "center",
                "clockwise": False,
                "radius": 10.0,
                "point_ids": ["c", "top", "bottom"],
            },
            {"id": "bottom_line", "type": "segment", "point_ids": ["bottom", "rb"]},
            {"id": "right_line", "type": "segment", "point_ids": ["rb", "rt"]},
        ]
        self.assertHasVolume(self._build_profile(entities))
        self.assertHasVolume(self._build_profile(
            entities,
            result_type="thin",
            thin_thickness=0.75,
            thin_mode="symmetric",
        ))

    def test_closed_profile_can_include_elliptical_arc_for_solid_and_thin(self):
        entities = [
            {"id": "center", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "major", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "minor", "type": "point", "x": 0.0, "y": 5.0},
            {"id": "start", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "end", "type": "point", "x": 0.0, "y": 5.0},
            {
                "id": "elliptical-arc", "type": "elliptical_arc",
                "point_ids": [
                    "center", "major", "minor", "start", "end",
                ],
            },
            {
                "id": "end-center", "type": "segment",
                "point_ids": ["end", "center"],
            },
            {
                "id": "center-start", "type": "segment",
                "point_ids": ["center", "start"],
            },
        ]
        self.assertHasVolume(self._build_profile(entities))
        self.assertHasVolume(self._build_profile(
            entities,
            result_type="thin",
            thin_thickness=0.5,
            thin_mode="symmetric",
        ))

    def test_closed_profile_can_include_spline(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "s1", "type": "point", "x": 10.0, "y": 5.0},
            {"id": "s2", "type": "point", "x": 20.0, "y": -5.0},
            {"id": "b", "type": "point", "x": 30.0, "y": 0.0},
            {"id": "rt", "type": "point", "x": 30.0, "y": 20.0},
            {"id": "lt", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "spline", "type": "spline", "point_ids": ["a", "s1", "s2", "b"]},
            {"id": "right", "type": "segment", "point_ids": ["b", "rt"]},
            {"id": "top", "type": "segment", "point_ids": ["rt", "lt"]},
            {"id": "left", "type": "segment", "point_ids": ["lt", "a"]},
        ]
        self.assertHasVolume(self._build_profile(entities))
        self.assertHasVolume(self._build_profile(
            entities,
            result_type="thin",
            thin_thickness=0.75,
            thin_mode="symmetric",
        ))

    def test_single_spline_can_close_on_its_first_point(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 20.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 20.0},
            {
                "id": "closed-spline",
                "type": "spline",
                "point_ids": ["a", "b", "c", "d", "a"],
            },
        ]
        self.assertHasVolume(self._build_profile(entities))
        self.assertHasVolume(self._build_profile(
            entities,
            result_type="thin",
            thin_thickness=0.75,
            thin_mode="symmetric",
        ))

    def test_front_xz_profile_extrudes_from_profile_offset(self):
        shape = self._build_profile([
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 10.0, "y": 20.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ], profile_offset=25.0)
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        xmin, ymin, zmin, xmax, ymax, zmax = bounds.Get()
        self.assertAlmostEqual(xmin, 0.0, places=6)
        self.assertAlmostEqual(xmax, 10.0, places=6)
        self.assertAlmostEqual(ymin, 25.0, places=6)
        self.assertAlmostEqual(ymax, 35.0, places=6)
        self.assertAlmostEqual(zmin, 0.0, places=6)
        self.assertAlmostEqual(zmax, 20.0, places=6)

    def test_protrusion_dimension_starts_on_offset_profile_plane(self):
        document = create_empty_part()
        container = ZimaEntity(
            "Protrusion001",
            EntityKind.CONTAINER,
            coordinate_system=CoordinateSystem(origin=(100.0, 200.0, 300.0)),
        )
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={"plane": "xz"},
        )
        feature = ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "10",
                "extent_mode": "one_side",
                "direction": "forward",
                "profile_offset": "25",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        class DimensionContext:
            _protrusion_dimension_axes = staticmethod(
                MainWindow._protrusion_dimension_axes
            )

            def __init__(self, active_document):
                self.document = active_document

        dimensions = MainWindow._primitive_dimensions(
            DimensionContext(document),
            feature,
        )

        self.assertEqual(len(dimensions), 1)
        dimension = dimensions[0]
        self.assertEqual(dimension.key, "length_forward")
        self.assertEqual(dimension.first_point, (100.0, 225.0, 300.0))
        self.assertEqual(dimension.second_point, (100.0, 235.0, 300.0))

    def test_external_sketch_lends_geometry_not_placement(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 10.0, "y": 20.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        document = create_empty_part()
        source = ZimaEntity(
            "SourceSketch",
            EntityKind.CONTAINER,
            coordinate_system=CoordinateSystem(
                origin=(-500.0, 80.0, 40.0),
                rotation=(20.0, 30.0, 40.0),
            ),
        )
        sketch = ZimaEntity(
            "Sketch",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        source.add_child(sketch)
        target = ZimaEntity(
            "TargetProtrusion",
            EntityKind.CONTAINER,
            coordinate_system=CoordinateSystem(origin=(100.0, 200.0, 300.0)),
        )
        target.add_child(ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "profile_source": "external",
                "sketch_id": sketch.entity_id,
                "length_forward": "10",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        ))
        document.root.add_child(source)
        document.root.add_child(target)

        shape = make_protrusion_shape(document, target)
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        xmin, ymin, zmin, xmax, ymax, zmax = bounds.Get()
        self.assertAlmostEqual(xmin, 100.0, places=6)
        self.assertAlmostEqual(xmax, 110.0, places=6)
        self.assertAlmostEqual(ymin, 200.0, places=6)
        self.assertAlmostEqual(ymax, 210.0, places=6)
        self.assertAlmostEqual(zmin, 300.0, places=6)
        self.assertAlmostEqual(zmax, 320.0, places=6)

    def test_closed_profile_revolves_around_first_construction_line(self):
        entities = [
            {"id": "axis_a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "axis_b", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "axis2_a", "type": "point", "x": -5.0, "y": 30.0},
            {"id": "axis2_b", "type": "point", "x": 25.0, "y": 30.0},
            {"id": "a", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 10.0},
            {"id": "d", "type": "point", "x": 10.0, "y": 10.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["axis_a", "axis_b"],
            },
            {
                "id": "ignored_axis",
                "type": "construction",
                "point_ids": ["axis2_a", "axis2_b"],
            },
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        document = create_empty_part()
        container = ZimaEntity("Revolve001", EntityKind.CONTAINER)
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
            "Revolve",
            EntityKind.REVOLVE,
            parameters={
                "sketch_id": sketch.entity_id,
                "angle": "360",
                "direction": "forward",
                "profile_offset": "15",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)

        shape = make_revolve_shape(document, container)
        self.assertHasVolume(shape)
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        self.assertAlmostEqual(
            abs(float(properties.Mass())),
            3000.0 * 3.141592653589793,
            places=5,
        )
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        _xmin, ymin, _zmin, _xmax, ymax, _zmax = bounds.Get()
        self.assertAlmostEqual((ymin + ymax) * 0.5, 15.0, places=6)

        feature.parameters.update({
            "angle": "180",
            "angle_reverse": "180",
            "extent_mode": "symmetric",
        })
        symmetric_shape = make_revolve_shape(document, container)
        self.assertHasVolume(symmetric_shape)
        symmetric_properties = GProp_GProps()
        brepgprop.VolumeProperties(symmetric_shape, symmetric_properties)
        self.assertAlmostEqual(
            abs(float(symmetric_properties.Mass())),
            3000.0 * 3.141592653589793,
            places=5,
        )


if __name__ == "__main__":
    unittest.main()
