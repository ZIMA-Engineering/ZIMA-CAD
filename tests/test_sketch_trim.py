import unittest
import math

from zima_cad.sketch_trim import (
    apply_trim_pieces,
    nearest_trim_piece,
    pieces_crossed_by_path,
    trim_topology,
)
from zima_cad.sketch_model import SketchModel


def point(point_id, x, y):
    return {"id": point_id, "type": "point", "x": x, "y": y}


class SketchTrimTests(unittest.TestCase):
    def test_base_sketch_axes_create_trim_points(self):
        entities = [
            point("p1", -10, 3), point("p2", 10, 3),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
        ]
        pieces = trim_topology(entities, include_base_axes=True)
        self.assertEqual(sum(piece.entity_id == "g1" for piece in pieces), 2)
        self.assertFalse(any(piece.entity_id.startswith("__sketch_axis") for piece in pieces))

    def test_crossing_segments_are_split_into_clickable_pieces(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            point("p3", 0, -10), point("p4", 0, 10),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
            {"id": "g2", "type": "segment", "point_ids": ["p3", "p4"]},
        ]
        pieces = trim_topology(entities)
        self.assertEqual(sum(piece.entity_id == "g1" for piece in pieces), 2)
        self.assertEqual(sum(piece.entity_id == "g2" for piece in pieces), 2)
        selected = nearest_trim_piece(pieces, (5, 0), 0.5)
        self.assertIsNotNone(selected)
        revised, mapping = apply_trim_pieces(entities, (selected,))
        self.assertEqual(mapping["g1"], ["g1"])
        segment = next(item for item in revised if item.get("id") == "g1")
        points = {item["id"]: item for item in revised if item.get("type") == "point"}
        positions = [(points[pid]["x"], points[pid]["y"]) for pid in segment["point_ids"]]
        self.assertIn((-10.0, 0.0), positions)
        self.assertIn((0.0, 0.0), positions)
        SketchModel.from_editor_data(revised, []).validate()

    def test_construction_line_splits_but_cannot_be_trimmed(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            point("p3", 0, -10), point("p4", 0, 10),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
            {"id": "axis", "type": "construction", "point_ids": ["p3", "p4"]},
        ]
        pieces = trim_topology(entities)
        self.assertEqual(sum(piece.entity_id == "g1" for piece in pieces), 2)
        self.assertFalse(any(piece.entity_id == "axis" for piece in pieces))

    def test_auxiliary_geometry_does_not_create_trim_points(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            point("p3", 0, -10), point("p4", 0, 10),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
            {
                "id": "helper", "type": "segment",
                "role": "construction", "point_ids": ["p3", "p4"],
            },
        ]
        pieces = trim_topology(entities)
        self.assertEqual(sum(piece.entity_id == "g1" for piece in pieces), 1)
        self.assertFalse(any(piece.entity_id == "helper" for piece in pieces))

    def test_entity_without_trim_points_is_deleted_whole(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
        ]
        piece = nearest_trim_piece(trim_topology(entities), (0, 0), 0.5)
        self.assertIsNotNone(piece)
        revised, mapping = apply_trim_pieces(entities, (piece,))
        self.assertEqual(mapping["g1"], [])
        self.assertEqual(revised, [])

    def test_orphan_point_referenced_by_dimension_is_preserved(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
        ]
        piece = nearest_trim_piece(trim_topology(entities), (0, 0), 0.5)
        revised, _mapping = apply_trim_pieces(
            entities, (piece,), referenced_point_ids=("p1",),
        )
        self.assertEqual(
            [item["id"] for item in revised if item.get("type") == "point"],
            ["p1"],
        )

    def test_points_linked_by_constraint_survive_geometry_removal(self):
        entities = [
            {
                **point("p1", -10, 0),
                "constraints": [{"type": "coincident", "point_ids": ["p1", "p2"]}],
            },
            point("p2", 10, 0),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
        ]
        piece = nearest_trim_piece(trim_topology(entities), (0, 0), 0.5)
        revised, _mapping = apply_trim_pieces(entities, (piece,))
        self.assertEqual(
            {item["id"] for item in revised if item.get("type") == "point"},
            {"p1", "p2"},
        )

    def test_circle_trim_creates_an_arc(self):
        entities = [
            point("center", 0, 0), point("a", -10, 0), point("b", 10, 0),
            {"id": "circle", "type": "circle", "point_ids": ["center"], "radius": 5},
            {"id": "line", "type": "segment", "point_ids": ["a", "b"]},
        ]
        pieces = trim_topology(entities)
        circle_pieces = [piece for piece in pieces if piece.entity_id == "circle"]
        self.assertEqual(len(circle_pieces), 2)
        upper = nearest_trim_piece(circle_pieces, (0, 5), 0.5)
        revised, _mapping = apply_trim_pieces(entities, (upper,))
        result = next(item for item in revised if item.get("id") == "circle")
        self.assertEqual(result["type"], "arc")
        self.assertEqual(len(result["point_ids"]), 3)
        SketchModel.from_editor_data(revised, []).validate()

    def test_two_slanted_tangents_split_each_circle_into_arcs(self):
        radius = 5.0
        centre_offset = (20.0, 5.0)
        length = math.hypot(*centre_offset)
        normal = (-centre_offset[1] / length, centre_offset[0] / length)
        entities = [
            point("center1", 0.0, 0.0),
            point("center2", *centre_offset),
        ]
        for index, side in enumerate((-1.0, 1.0), start=1):
            first_id = f"t{index}a"
            second_id = f"t{index}b"
            first = (
                side * radius * normal[0],
                side * radius * normal[1],
            )
            second = (
                centre_offset[0] + first[0],
                centre_offset[1] + first[1],
            )
            entities.extend((
                {
                    **point(first_id, *first),
                    "curve_attachment": {
                        "type": "circle",
                        "geometry_id": "circle1",
                    },
                },
                {
                    **point(second_id, *second),
                    "curve_attachment": {
                        "type": "circle",
                        "geometry_id": "circle2",
                    },
                },
                {
                    "id": f"line{index}",
                    "type": "segment",
                    "point_ids": [first_id, second_id],
                    "constraints": [
                        {
                            "type": "tangent",
                            "geometry_id": "circle1",
                            "contact_point_id": first_id,
                        },
                        {
                            "type": "tangent",
                            "geometry_id": "circle2",
                            "contact_point_id": second_id,
                        },
                    ],
                },
            ))
        entities.extend((
            {
                "id": "circle1", "type": "circle",
                "point_ids": ["center1"], "radius": radius,
            },
            {
                "id": "circle2", "type": "circle",
                "point_ids": ["center2"], "radius": radius,
            },
        ))

        pieces = trim_topology(entities)

        self.assertEqual(
            sum(piece.entity_id == "circle1" for piece in pieces), 2
        )
        self.assertEqual(
            sum(piece.entity_id == "circle2" for piece in pieces), 2
        )
        self.assertFalse(
            any(
                piece.entity_id == "circle1"
                and piece.start == 0.0
                and piece.end == 1.0
                for piece in pieces
            )
        )
        selected = next(
            piece for piece in pieces if piece.entity_id == "circle1"
        )
        revised, _mapping = apply_trim_pieces(entities, (selected,))
        result = next(
            entity for entity in revised if entity.get("id") == "circle1"
        )
        self.assertEqual(result["type"], "arc")
        self.assertEqual(set(result["point_ids"][1:]), {"t1a", "t2a"})
        SketchModel.from_editor_data(revised, []).validate()

    def test_drag_path_selects_every_crossed_piece(self):
        entities = [
            point("p1", -10, -3), point("p2", 10, -3),
            point("p3", -10, 3), point("p4", 10, 3),
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
            {"id": "g2", "type": "segment", "point_ids": ["p3", "p4"]},
        ]
        selected = pieces_crossed_by_path(
            trim_topology(entities), ((0, -5), (0, 5)), 0.1
        )
        self.assertEqual({piece.entity_id for piece in selected}, {"g1", "g2"})

    def test_split_survivors_never_reuse_original_geometry_id(self):
        entities = [
            point("p1", -10, 0), point("p2", 10, 0),
            point("p3", -3, -5), point("p4", -3, 5),
            point("p5", 3, -5), point("p6", 3, 5),
            {"id": "g1", "type": "segment", "point_ids": ["p3", "p4"]},
            {"id": "g2", "type": "segment", "point_ids": ["p1", "p2"]},
            {"id": "g3", "type": "segment", "point_ids": ["p5", "p6"]},
        ]
        middle = nearest_trim_piece(
            tuple(
                piece for piece in trim_topology(entities)
                if piece.entity_id == "g2"
            ),
            (0, 0),
            0.5,
        )

        revised, mapping = apply_trim_pieces(entities, (middle,))

        identifiers = [str(entity.get("id", "")) for entity in revised]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertEqual(mapping["g2"][0], "g2")
        self.assertNotEqual(mapping["g2"][1], "g2")
        SketchModel.from_editor_data(revised, []).validate()

    def test_ellipse_trim_creates_elliptical_arc(self):
        entities = [
            point("c", 0, 0), point("major", 8, 0), point("minor", 0, 4),
            point("a", -12, 0), point("b", 12, 0),
            {"id": "ellipse", "type": "ellipse", "point_ids": ["c", "major", "minor"]},
            {"id": "line", "type": "segment", "point_ids": ["a", "b"]},
        ]
        pieces = trim_topology(entities)
        ellipse_pieces = [piece for piece in pieces if piece.entity_id == "ellipse"]
        self.assertEqual(len(ellipse_pieces), 2)
        selected = nearest_trim_piece(ellipse_pieces, (0, 4), 0.5)
        revised, _mapping = apply_trim_pieces(entities, (selected,))
        result = next(item for item in revised if item.get("id") == "ellipse")
        self.assertEqual(result["type"], "elliptical_arc")
        SketchModel.from_editor_data(revised, []).validate()

    def test_spline_is_reconstructed_after_trim(self):
        entities = [
            point("s1", -10, -2), point("s2", -3, 5),
            point("s3", 3, -5), point("s4", 10, 2),
            point("a", 0, -10), point("b", 0, 10),
            {"id": "spline", "type": "spline", "point_ids": ["s1", "s2", "s3", "s4"]},
            {"id": "axis", "type": "construction", "point_ids": ["a", "b"]},
        ]
        pieces = trim_topology(entities)
        spline_pieces = [piece for piece in pieces if piece.entity_id == "spline"]
        self.assertGreaterEqual(len(spline_pieces), 2)
        revised, _mapping = apply_trim_pieces(entities, (spline_pieces[0],))
        result = next(item for item in revised if item.get("id") == "spline")
        self.assertEqual(result["type"], "spline")
        self.assertGreaterEqual(len(result["point_ids"]), 2)
        SketchModel.from_editor_data(revised, []).validate()


if __name__ == "__main__":
    unittest.main()
