import json
import unittest

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib
from OCC.Core.GProp import GProp_GProps

from zima_cad.app import MainWindow
from zima_cad.model import (
    CoordinateSystem,
    EntityKind,
    ZimaEntity,
    create_empty_part,
    make_protrusion_shape,
    make_revolve_shape,
    make_sketch_shape,
)
from zima_cad.sketch_model import SketchModel


class ProtrusionProfileTests(unittest.TestCase):
    @staticmethod
    def _build_profile(entities, profile_offset=0.0):
        document = create_empty_part()
        container = ZimaEntity("Protrusion001", EntityKind.CONTAINER)
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
                "profile_offset": f"{profile_offset:.12g}",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        return make_protrusion_shape(document, container)

    def assertHasVolume(self, shape):
        self.assertIsNotNone(shape)
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
        self.assertHasVolume(self._build_profile([
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
        ]))

    def test_closed_profile_can_include_spline(self):
        self.assertHasVolume(self._build_profile([
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
        ]))

    def test_single_spline_can_close_on_its_first_point(self):
        self.assertHasVolume(self._build_profile([
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 20.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 20.0},
            {
                "id": "closed-spline",
                "type": "spline",
                "point_ids": ["a", "b", "c", "d", "a"],
            },
        ]))

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
