from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ConstraintCapability:
    translation_dof: int
    rotation_dof: int
    accepts_orientation_references: bool
    maximum_orientation_references: int = 0
    maximum_references: int = 3

    @property
    def total_dof(self) -> int:
        return self.translation_dof + self.rotation_dof


DEFAULT_CONSTRAINT_CAPABILITY = ConstraintCapability(3, 3, True, 2)


def constraint_capability(container_type: str) -> ConstraintCapability:
    # Every container owns a complete local coordinate system. Geometry inside
    # it may be rotationally symmetric, but container placement still exposes
    # X/Y/Z and RX/RY/RZ consistently.
    return DEFAULT_CONSTRAINT_CAPABILITY


def reference_admission(
    capability: ConstraintCapability,
    *,
    current_translation_dof: int,
    trial_translation_dof: int,
    trial_is_valid: bool,
    orientation_candidate: bool,
    orientation_reference_count: int,
    current_reference_count: int = 0,
    orientation_independent: bool = True,
) -> str:
    """Return `position`, `orientation`, or `reject` for one reference."""

    if current_reference_count >= capability.maximum_references:
        return "reject"

    if (
        trial_is_valid
        and current_translation_dof > 0
        and trial_translation_dof < current_translation_dof
    ):
        return "position"
    if (
        orientation_candidate
        and orientation_independent
        and capability.accepts_orientation_references
        and orientation_reference_count
        < capability.maximum_orientation_references
    ):
        return "orientation"
    return "reject"


def rotation_degrees_of_freedom(references) -> int:
    orientation_count = min(2, sum(
        str(reference.get("orientation_role", "none")) != "none"
        for reference in references
    ))
    return (3, 1, 0)[orientation_count]


def editable_rotation_axes(references) -> set[str]:
    # RX/RY/RZ are corrections applied after the references establish the
    # base frame. They remain editable even when that frame is fully defined.
    return {"x", "y", "z"}
