"""Representation-independent rules for constrained dimension values."""

from __future__ import annotations

import math
from typing import Any, Mapping


def dimension_range_bounds(
    lower_limit: float,
    upper_limit: float,
) -> tuple[float, float]:
    """Validate and return absolute inclusive dimension limits."""
    lower = float(lower_limit)
    upper = float(upper_limit)
    if not math.isfinite(lower) or not math.isfinite(upper):
        raise ValueError("dimension limits must be finite")
    if lower > upper:
        raise ValueError("lower dimension limit exceeds upper limit")
    return lower, upper


def dimension_value_in_range(
    values: Mapping[str, Any],
    value: float,
) -> bool:
    """Check a current value against optional absolute limits."""
    try:
        current = float(value)
    except (TypeError, ValueError):
        return False
    if not math.isfinite(current):
        return False
    if not bool(values.get("range_enabled", False)):
        return True
    try:
        lower, upper = dimension_range_bounds(
            float(values["lower_limit"]),
            float(values["upper_limit"]),
        )
    except (KeyError, TypeError, ValueError):
        return False
    return lower - 1.0e-12 <= current <= upper + 1.0e-12


def validate_dimension_range(values: Mapping[str, Any]) -> None:
    """Validate nominal/current values and their optional absolute limits."""
    nominal = float(values["nominal_value"])
    current = float(values["current_value"])
    if not math.isfinite(nominal) or not math.isfinite(current):
        raise ValueError("dimension values must be finite")
    if not bool(values.get("range_enabled", False)):
        return
    lower, upper = dimension_range_bounds(
        float(values["lower_limit"]),
        float(values["upper_limit"]),
    )
    if not lower <= nominal <= upper:
        raise ValueError("nominal dimension value is outside its limits")
    if not lower <= current <= upper:
        raise ValueError("current dimension value is outside its limits")
