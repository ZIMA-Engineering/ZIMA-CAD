from __future__ import annotations

import unittest

from zima_cad.selection import (
    SelectionCandidate,
    SelectionController,
    SelectionKind,
    SelectionRequest,
    SelectionResolution,
)
from zima_cad.model import (
    ContainerType,
    EntityKind,
    ZimaEntity,
    create_empty_part,
)


class SelectionControllerTests(unittest.TestCase):
    def test_empty_history_never_falls_back_to_final_body_result(self):
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(container.entity_id, EntityKind.BOX)
        key = document._shape_history_cache_keys(
            document.history_objects()
        )[-1]
        document._body_result_cache[key] = object()

        self.assertIsNone(document.cached_body_result_at([]))

    def test_generated_viewer_axis_does_not_change_body_cache_key(self):
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        primitive = document.create_primitive(
            container.entity_id, EntityKind.BOX
        )
        history = document.history_objects()
        original_key = document._shape_history_cache_keys(history)[-1]
        validated_result = object()
        document._body_result_cache[original_key] = validated_result

        primitive.add_child(ZimaEntity(
            name="Generated Axis",
            kind=EntityKind.AXIS,
            parameters={"generated_axis": "true"},
        ))
        current_key = document._shape_history_cache_keys(history)[-1]
        self.assertEqual(current_key, original_key)

        self.assertIs(
            document.cached_body_result_at(history), validated_result
        )

    def test_evaluated_length_output_does_not_change_body_cache_key(self):
        document = create_empty_part()
        container = document.create_container(
            "Protrusion", ContainerType.PROTRUSION
        )
        feature = ZimaEntity(
            name="Protrusion",
            kind=EntityKind.PROTRUSION,
            parameters={
                "length_forward": "50",
                "evaluated_length_forward": "88.4742331905",
            },
        )
        container.add_child(feature)
        history = document.history_objects()
        original_key = document._shape_history_cache_keys(history)[-1]
        validated_result = object()
        document._body_result_cache[original_key] = validated_result

        # OCCT evaluation stores the measured result at full binary64
        # precision.  This output must not make its own input signature stale.
        feature.parameters["evaluated_length_forward"] = (
            "88.47423319047367"
        )

        self.assertEqual(
            document._shape_history_cache_keys(history)[-1],
            original_key,
        )
        self.assertIs(
            document.cached_body_result_at(history), validated_result
        )

        # The user-controlled extent remains a real geometric input.
        feature.parameters["length_forward"] = "51"
        self.assertNotEqual(
            document._shape_history_cache_keys(history)[-1],
            original_key,
        )

    def test_request_filters_resolves_and_completes(self) -> None:
        completed = []
        controller = SelectionController()
        controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(
                value=f"edge:{candidate.element_index}"
                if candidate.owner_id == "result" else None,
                error=None
                if candidate.owner_id == "result" else "unsupported",
            ),
            on_complete=completed.append,
            prompt="select edge",
            wrong_kind_message="edge required",
        ))

        wrong = controller.submit(SelectionCandidate(
            SelectionKind.FACE, "result", 1
        ))
        self.assertTrue(wrong.consumed)
        self.assertFalse(wrong.accepted)
        self.assertEqual(wrong.message, "edge required")
        rejected = controller.submit(SelectionCandidate(
            SelectionKind.EDGE, "source", 1
        ))
        self.assertEqual(rejected.message, "unsupported")
        accepted = controller.submit(SelectionCandidate(
            SelectionKind.EDGE, "result", 3
        ))
        self.assertTrue(accepted.completed)
        self.assertFalse(controller.active)
        self.assertEqual(completed, [("edge:3",)])

    def test_selected_candidate_can_be_removed_by_key(self) -> None:
        controller = SelectionController()
        controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(candidate.element_index),
            on_complete=lambda _values: None,
            maximum_count=10,
        ))
        candidate = SelectionCandidate(SelectionKind.EDGE, "body", 4)
        controller.toggle(candidate)

        self.assertTrue(controller.remove_key(candidate.key))
        self.assertEqual(controller.values, ())
        self.assertEqual(controller.candidate_keys, ())
        self.assertFalse(controller.remove_key(candidate.key))

    def test_multi_selection_rejects_duplicates_and_can_cancel(self) -> None:
        cancelled = []
        completed = []
        controller = SelectionController()
        controller.begin(SelectionRequest(
            command_id="multi-edge",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(candidate.key),
            on_complete=completed.append,
            maximum_count=2,
            on_cancel=lambda: cancelled.append(True),
        ))
        first = SelectionCandidate(SelectionKind.EDGE, "result", 1)
        self.assertTrue(controller.submit(first).accepted)
        self.assertFalse(controller.submit(first).accepted)
        self.assertTrue(controller.cancel())
        self.assertEqual(cancelled, [True])
        self.assertEqual(completed, [])

    def test_multi_selection_can_toggle_and_complete_explicitly(self) -> None:
        completed = []
        controller = SelectionController()
        controller.begin(SelectionRequest(
            command_id="fillet",
            allowed_kinds=frozenset({SelectionKind.EDGE}),
            resolver=lambda candidate: SelectionResolution(candidate.element_index),
            on_complete=completed.append,
            maximum_count=10,
        ))
        first = SelectionCandidate(SelectionKind.EDGE, "result", 2)
        second = SelectionCandidate(SelectionKind.EDGE, "result", 7)

        controller.toggle(first)
        controller.toggle(second)
        controller.toggle(first)

        self.assertEqual(controller.values, (7,))
        self.assertEqual(
            controller.candidate_keys,
            ((SelectionKind.EDGE.value, "result", 7),),
        )
        self.assertTrue(controller.complete())
        self.assertEqual(completed, [(7,)])
        self.assertFalse(controller.active)


if __name__ == "__main__":
    unittest.main()
