from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ConstraintCapability:
    translation_dof: int
    rotation_dof: int
    accepts_orientation_references: bool
    maximum_orientation_references: int = 0

    @property
    def total_dof(self) -> int:
        return self.translation_dof + self.rotation_dof


CONTAINER_CONSTRAINT_CAPABILITIES = {
    "POINT": ConstraintCapability(3, 0, False),
    # An unoriented spatial line has three position and two direction DOF;
    # rotation around itself does not change the axis geometry.
    "AXIS": ConstraintCapability(3, 2, True, 2),
    "PLANE": ConstraintCapability(3, 2, True, 2),
}

DEFAULT_CONSTRAINT_CAPABILITY = ConstraintCapability(3, 3, True, 2)


def constraint_capability(container_type: str) -> ConstraintCapability:
    return CONTAINER_CONSTRAINT_CAPABILITIES.get(
        str(container_type).upper(),
        DEFAULT_CONSTRAINT_CAPABILITY,
    )


def reference_admission(
    capability: ConstraintCapability,
    *,
    current_translation_dof: int,
    trial_translation_dof: int,
    trial_is_valid: bool,
    orientation_candidate: bool,
    orientation_reference_count: int,
) -> str:
    """Return `position`, `orientation`, or `reject` for one reference."""

    if (
        trial_is_valid
        and current_translation_dof > 0
        and trial_translation_dof < current_translation_dof
    ):
        return "position"
    if (
        orientation_candidate
        and capability.accepts_orientation_references
        and orientation_reference_count
        < capability.maximum_orientation_references
    ):
        return "orientation"
    return "reject"
