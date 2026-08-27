#!/usr/bin/env python3
"""Measure the archived Python Part path against the native C++ fixture.

This deliberately reports shape-only and complete viewer work separately.
Comparing Python ``build_active_shape`` with C++ ``evaluate_history`` is not a
valid parity measurement because the latter also materializes persisted ZIMA
viewer packets for every history boundary.
"""

from __future__ import annotations

from pathlib import Path
import statistics
import sys
from time import perf_counter


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "archive" / "python"))

from zima_cad.model import (  # noqa: E402
    CombineMode,
    ContainerType,
    EntityKind,
    create_empty_part,
)
from zima_cad.viewer_scene import build_document_viewer_scene_data  # noqa: E402


FEATURE_COUNT = 24


def fixture():
    document = create_empty_part()
    for index in range(FEATURE_COUNT):
        container = document.create_container(
            "benchmark-box-", ContainerType.BOX
        )
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        if box is None:
            raise RuntimeError("Python benchmark failed to create a Box")
        box.parameters.update({
            "length": "20",
            "width": "20",
            "height": "20",
        })
        container.coordinate_system.origin = (
            float(index % 8) * 12.0,
            float(index // 8) * 12.0,
            0.0,
        )
        container.combine_mode = (
            CombineMode.NONE if index == 0 else CombineMode.ADD
        )
    return document


def measure(function, repetitions: int = 3) -> float:
    samples = []
    for _ in range(repetitions):
        started = perf_counter()
        function()
        samples.append((perf_counter() - started) * 1000.0)
    return statistics.mean(samples)


def main() -> None:
    shape_only_ms = measure(lambda: fixture().build_active_shape())

    def final_viewer() -> None:
        build_document_viewer_scene_data(fixture())

    final_viewer_ms = measure(final_viewer)

    def all_boundaries() -> None:
        document = fixture()
        for boundary in range(1, FEATURE_COUNT + 1):
            build_document_viewer_scene_data(
                document, history_boundary=boundary
            )

    all_boundaries_ms = measure(all_boundaries)

    # Time only the edit/rebuild, not construction of a fresh fixture.
    incremental_samples = []
    for _ in range(5):
        document = fixture()
        build_document_viewer_scene_data(document)
        container = document.history_objects()[-1]
        box = next(
            child
            for child in container.entity_children()
            if child.kind == EntityKind.BOX
        )
        box.parameters["height"] = "24"
        started = perf_counter()
        build_document_viewer_scene_data(document)
        incremental_samples.append((perf_counter() - started) * 1000.0)

    print("ZIMA_PYTHON_PERF_V1")
    print(f"part_shape_only features={FEATURE_COUNT} mean_ms={shape_only_ms:.3f}")
    print(f"part_final_viewer features={FEATURE_COUNT} mean_ms={final_viewer_ms:.3f}")
    print(
        f"part_all_boundaries features={FEATURE_COUNT} "
        f"mean_ms={all_boundaries_ms:.3f}"
    )
    print(
        f"part_incremental_viewer changed_index={FEATURE_COUNT - 1} "
        f"mean_ms={statistics.mean(incremental_samples):.3f}"
    )


if __name__ == "__main__":
    main()
