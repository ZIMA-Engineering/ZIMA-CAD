from __future__ import annotations

import unittest

from zima_cad.constraint_policy import (
    constraint_capability,
    editable_rotation_axes,
    reference_admission,
    rotation_degrees_of_freedom,
)


class ConstraintCapabilityTests(unittest.TestCase):
    def test_every_container_has_six_placement_dof(self) -> None:
        for container_type in (
            "POINT", "AXIS", "PLANE", "SKETCH", "BOX", "PROTRUSION"
        ):
            capability = constraint_capability(container_type)
            self.assertEqual(capability.translation_dof, 3)
            self.assertEqual(capability.rotation_dof, 3)
            self.assertEqual(capability.total_dof, 6)
            self.assertTrue(capability.accepts_orientation_references)

    def test_point_accepts_bounded_orientation_after_position(self) -> None:
        point = constraint_capability("POINT")
        self.assertEqual(reference_admission(
            point,
            current_translation_dof=3,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=False,
            orientation_reference_count=0,
        ), "position")
        self.assertEqual(reference_admission(
            point,
            current_translation_dof=0,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=True,
            orientation_reference_count=0,
        ), "orientation")
        self.assertEqual(reference_admission(
            point,
            current_translation_dof=0,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=True,
            orientation_reference_count=2,
        ), "reject")

    def test_container_accepts_at_most_three_references(self) -> None:
        capability = constraint_capability("PROTRUSION")

        self.assertEqual(reference_admission(
            capability,
            current_translation_dof=1,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=False,
            orientation_reference_count=0,
            current_reference_count=3,
        ), "reject")

    def test_parallel_orientation_reference_is_rejected(self) -> None:
        capability = constraint_capability("PLANE")

        self.assertEqual(reference_admission(
            capability,
            current_translation_dof=0,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=True,
            orientation_reference_count=1,
            current_reference_count=2,
            orientation_independent=False,
        ), "reject")

    def test_axis_can_accept_bounded_orientation_references(self) -> None:
        axis = constraint_capability("AXIS")
        self.assertEqual(axis.total_dof, 6)
        self.assertEqual(reference_admission(
            axis,
            current_translation_dof=0,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=True,
            orientation_reference_count=0,
        ), "orientation")
        self.assertEqual(reference_admission(
            axis,
            current_translation_dof=0,
            trial_translation_dof=0,
            trial_is_valid=True,
            orientation_candidate=True,
            orientation_reference_count=2,
        ), "reject")

    def test_rotation_corrections_stay_editable_for_defined_frame(self) -> None:
        none = []
        first = [{"orientation_role": "normal"}]
        complete = [
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]
        self.assertEqual(rotation_degrees_of_freedom(none), 3)
        self.assertEqual(editable_rotation_axes(none), {"x", "y", "z"})
        self.assertEqual(rotation_degrees_of_freedom(first), 1)
        self.assertEqual(editable_rotation_axes(first), {"x", "y", "z"})
        self.assertEqual(rotation_degrees_of_freedom(complete), 0)
        self.assertEqual(
            editable_rotation_axes(complete),
            {"x", "y", "z"},
        )


if __name__ == "__main__":
    unittest.main()
