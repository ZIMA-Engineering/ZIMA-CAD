import json
import unittest

from zima_cad.model import (
    ContainerType,
    EntityKind,
    SketchRole,
    ZimaEntity,
    create_empty_part,
    delete_child_entity,
)
from zima_cad.sketch_model import GeometryType, SketchGeometry, SketchModel, SketchPoint
from zima_cad.viewer_scene import build_document_viewer_scene_data


class GeneratedAxisTests(unittest.TestCase):
    def test_edited_container_origin_is_visible_without_global_toggle(self):
        document = create_empty_part()
        container = document.create_container("Point001", ContainerType.POINT)
        origin = next(
            child for child in container.children
            if child.kind == EntityKind.ORIGIN
        )

        normal = build_document_viewer_scene_data(
            document,
            show_object_origins=False,
        )
        editing = build_document_viewer_scene_data(
            document,
            show_object_origins=False,
            editing_object_id=container.entity_id,
        )

        self.assertFalse(any(
            edge.owner_id == origin.entity_id for edge in normal.mesh.edges
        ))
        self.assertEqual(
            sum(edge.owner_id == origin.entity_id for edge in editing.mesh.edges),
            3,
        )

    def test_cylinder_axis_is_locked_and_stable(self):
        document = create_empty_part()
        container = document.create_container("Cylinder001", ContainerType.CYLINDER)
        cylinder = document.create_primitive(container.entity_id, EntityKind.CYLINDER)

        axis = cylinder.children[0]
        axis_id = axis.entity_id
        self.assertTrue(axis.locked)
        self.assertEqual(axis.parameters["axis"], "z")
        self.assertFalse(delete_child_entity(document.root, axis_id))

        document.sync_generated_axes()
        self.assertEqual([child.entity_id for child in cylinder.children], [axis_id])

    def test_sphere_has_three_principal_axes(self):
        document = create_empty_part()
        container = document.create_container("Sphere001", ContainerType.SPHERE)
        sphere = document.create_primitive(container.entity_id, EntityKind.SPHERE)

        self.assertEqual(
            {axis.parameters["axis"] for axis in sphere.children},
            {"x", "y", "z"},
        )
        self.assertTrue(all(axis.locked for axis in sphere.children))

    def test_circle_protrusion_axis_uses_circle_center_and_stable_id(self):
        document = create_empty_part()
        container = document.create_container("Protrusion001", ContainerType.PROTRUSION)
        sketch = document.create_sketch(container.entity_id, "xz", SketchRole.PROFILE)
        model = SketchModel()
        model.add_point(SketchPoint("center", 12.0, 7.0))
        model.add_geometry(SketchGeometry(
            "circle001", GeometryType.CIRCLE, ("center",), {"radius": 3.0}
        ))
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        feature = ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "20",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        )
        container.add_child(feature)

        document.sync_generated_axes()
        axis = feature.children[0]
        self.assertEqual(axis.entity_id, f"{feature.entity_id}:axis:circle:circle001")
        self.assertEqual(axis.parameters["axis"], "y")
        self.assertEqual(
            tuple(float(axis.parameters[f"origin_{key}"]) for key in "xyz"),
            (12.0, 10.0, 7.0),
        )

    def test_axes_follow_axes_visibility_switch(self):
        document = create_empty_part()
        container = document.create_container("Cylinder001", ContainerType.CYLINDER)
        cylinder = document.create_primitive(container.entity_id, EntityKind.CYLINDER)
        axis_id = cylinder.children[0].entity_id

        hidden = build_document_viewer_scene_data(document, show_user_axes=False)
        visible = build_document_viewer_scene_data(document, show_user_axes=True)
        self.assertNotIn(axis_id, hidden.shapes_by_owner_id)
        self.assertIn(axis_id, visible.shapes_by_owner_id)


if __name__ == "__main__":
    unittest.main()
