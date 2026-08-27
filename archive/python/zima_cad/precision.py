from __future__ import annotations

import math
from typing import Any


# OCCT's ordinary geometric confusion is approximately 1e-7 mm.  A document
# may deliberately use a coarser engineering tolerance, but calculations must
# never be made less stable by requesting a smaller effective kernel value.
KERNEL_LINEAR_TOLERANCE = 1.0e-7
DEFAULT_LINEAR_TOLERANCE = 1.0e-3
DEFAULT_ANGULAR_TOLERANCE = 1.0e-3


def format_model_float(value: Any) -> str:
    """Serialize one binary64 value without losing information.

    Seventeen significant decimal digits are sufficient for an IEEE-754
    double to make an exact binary round trip.  This is storage precision,
    deliberately independent of the number of decimals shown in the UI.
    """

    # Python's float repr is the shortest decimal spelling that round-trips
    # to the same binary64 value (using at most 17 significant digits).  The
    # trailing ``.0`` is a Python type marker rather than required model data;
    # our readers already parse the field as float, so omit it for clean INI
    # output without losing the value (including the sign of ``-0``).
    text = repr(float(value))
    return text[:-2] if text.endswith(".0") else text


def _document_precision_value(
    document: Any,
    key: str,
    default: float,
) -> float:
    try:
        value = float(document.document_precision.get(key, str(default)))
    except (AttributeError, TypeError, ValueError):
        value = default
    return value if math.isfinite(value) and value >= 0.0 else default


def model_linear_tolerance(document: Any = None) -> float:
    """Return the document's Boolean/model resolution in length units."""

    return max(
        KERNEL_LINEAR_TOLERANCE,
        _document_precision_value(
            document,
            "linear_tolerance",
            DEFAULT_LINEAR_TOLERANCE,
        ),
    )


def model_angular_tolerance(document: Any = None) -> float:
    """Return the document's angular resolution in document angle units."""

    return max(
        0.0,
        _document_precision_value(
            document,
            "angular_tolerance",
            DEFAULT_ANGULAR_TOLERANCE,
        ),
    )
