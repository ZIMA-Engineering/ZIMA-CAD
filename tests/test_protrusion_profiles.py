import json
import unittest

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.GProp import GProp_GProps

from zima_cad.model import (
    EntityKind,
    ZimaEntity,
    create_empty_part,
    make_protrusion_shape,
)
from zima_cad.sketch_model import SketchModel


class ProtrusionProfileTests(unittest.TestCase):
    @staticmethod
    def _build_profile(entities):
        document = create_empty_part()
        container = ZimaEntity("Protrusion001", EntityKind.CONTAINER)
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={
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
        return make_protrusion_shape(document, container)

    def assertHasVolume(self, shape):
        self.assertIsNotNone(shape)
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        self.assertGreater(abs(float(properties.Mass())), 1.0e-6)

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


if __name__ == "__main__":
    unittest.main()
