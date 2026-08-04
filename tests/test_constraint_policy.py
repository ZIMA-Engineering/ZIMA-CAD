from __future__ import annotations

import unittest

from zima_cad.constraint_policy import (
    constraint_capability,
    reference_admission,
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


if __name__ == "__main__":
    unittest.main()
