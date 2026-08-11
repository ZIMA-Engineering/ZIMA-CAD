from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable


class SelectionKind(str, Enum):
    OBJECT = "object"
    FACE = "face"
    EDGE = "edge"
    POINT = "point"
    AXIS = "axis"
    PLANE = "plane"


class SelectionPurpose(str, Enum):
    ASSEMBLY_COMPONENT = "assembly_component"
    PART_CONTAINER = "part_container"
    STABLE_REFERENCE = "stable_reference"
    BODY_EDGE_OPERATION = "body_edge_operation"
    SKETCH_REFERENCE = "sketch_reference"
    VIEW_ORIENTATION = "view_orientation"


class TopologySource(str, Enum):
    NONE = "none"
    ORIGINAL_SOLIDS = "original_solids"
    INPUT_BODY = "input_body"
    DISPLAYED_MODEL = "displayed_model"


class ViewerInteractionScope(str, Enum):
    """Objects the shared viewer is allowed to expose in one context."""

    ASSEMBLY_INSTANCES = "assembly_instances"
    ACTIVE_PART_CONTAINERS = "active_part_containers"
    ACTIVE_PART_REFERENCES = "active_part_references"
    SKETCH_EXTERNAL_REFERENCES = "sketch_external_references"
    ASSEMBLY_MATE_REFERENCES = "assembly_mate_references"


@dataclass(frozen=True)
class ViewerDocumentContext:
    """Keep display, editing and Assembly-instance identity independent."""

    display_document: Any
    editing_document: Any
    active_instance_id: str | None
    editing_history_boundary: int
    interaction_scope: ViewerInteractionScope

    @property
    def displays_active_instance(self) -> bool:
        return self.active_instance_id is not None


@dataclass(frozen=True)
class ViewerSelectionPolicy:
    """Authoritative viewer contract for one active interaction."""

    purpose: SelectionPurpose
    topology_source: TopologySource
    allowed_kinds: frozenset[SelectionKind]
    interaction_mode: str
    selection_filter: str

    @property
    def uses_original_topology(self) -> bool:
        return self.topology_source == TopologySource.ORIGINAL_SOLIDS


@dataclass(frozen=True)
class SelectionCandidate:
    kind: SelectionKind
    owner_id: str
    element_index: int = 0
    shape: Any | None = None

    @property
    def key(self) -> tuple[str, str, int]:
        return self.kind.value, self.owner_id, self.element_index


@dataclass(frozen=True)
class SelectionResolution:
    value: Any | None = None
    error: str | None = None

    @property
    def accepted(self) -> bool:
        return self.value is not None and self.error is None


SelectionResolver = Callable[[SelectionCandidate], SelectionResolution]
SelectionCallback = Callable[[tuple[Any, ...]], None]
CancelCallback = Callable[[], None]


@dataclass
class SelectionRequest:
    command_id: str
    allowed_kinds: frozenset[SelectionKind]
    resolver: SelectionResolver
    on_complete: SelectionCallback
    minimum_count: int = 1
    maximum_count: int = 1
    prompt: str = ""
    wrong_kind_message: str = ""
    on_cancel: CancelCallback | None = None

    def __post_init__(self) -> None:
        if not self.allowed_kinds:
            raise ValueError("SelectionRequest requires an allowed kind")
        if self.minimum_count < 1:
            raise ValueError("SelectionRequest minimum_count must be positive")
        if self.maximum_count < self.minimum_count:
            raise ValueError("SelectionRequest maximum_count is too small")


@dataclass(frozen=True)
class SelectionUpdate:
    consumed: bool
    accepted: bool = False
    completed: bool = False
    message: str = ""


@dataclass
class SelectionController:
    request: SelectionRequest | None = None
    _values: list[Any] = field(default_factory=list)
    _candidate_keys: list[tuple[str, str, int]] = field(default_factory=list)

    @property
    def active(self) -> bool:
        return self.request is not None

    @property
    def prompt(self) -> str:
        return self.request.prompt if self.request is not None else ""

    @property
    def values(self) -> tuple[Any, ...]:
        return tuple(self._values)

    @property
    def candidate_keys(self) -> tuple[tuple[str, str, int], ...]:
        return tuple(self._candidate_keys)

    def begin(self, request: SelectionRequest) -> None:
        self.cancel()
        self.request = request
        self._values.clear()
        self._candidate_keys.clear()

    def cancel(self) -> bool:
        request = self.request
        self.request = None
        self._values.clear()
        self._candidate_keys.clear()
        if request is not None and request.on_cancel is not None:
            request.on_cancel()
        return request is not None

    def toggle(self, candidate: SelectionCandidate) -> SelectionUpdate:
        """Toggle one resolved candidate without completing the request."""
        request = self.request
        if request is None:
            return SelectionUpdate(consumed=False)
        if candidate.kind not in request.allowed_kinds:
            return SelectionUpdate(consumed=True, message=request.wrong_kind_message)
        if candidate.key in self._candidate_keys:
            index = self._candidate_keys.index(candidate.key)
            self._candidate_keys.remove(candidate.key)
            self._values.pop(index)
            return SelectionUpdate(consumed=True, accepted=True, message=request.prompt)
        resolution = request.resolver(candidate)
        if not resolution.accepted:
            return SelectionUpdate(
                consumed=True,
                message=resolution.error or request.prompt,
            )
        self._candidate_keys.append(candidate.key)
        self._values.append(resolution.value)
        return SelectionUpdate(consumed=True, accepted=True, message=request.prompt)

    def complete(self) -> bool:
        request = self.request
        if request is None or len(self._values) < request.minimum_count:
            return False
        values = tuple(self._values)
        self.request = None
        self._values.clear()
        self._candidate_keys.clear()
        request.on_complete(values)
        return True

    def remove_key(self, key: tuple[str, str, int]) -> bool:
        if key not in self._candidate_keys:
            return False
        index = self._candidate_keys.index(key)
        self._candidate_keys.pop(index)
        self._values.pop(index)
        return True

    def submit(self, candidate: SelectionCandidate) -> SelectionUpdate:
        request = self.request
        if request is None:
            return SelectionUpdate(consumed=False)
        if candidate.kind not in request.allowed_kinds:
            return SelectionUpdate(
                consumed=True,
                message=request.wrong_kind_message or request.prompt,
            )
        if candidate.key in self._candidate_keys:
            return SelectionUpdate(consumed=True, message=request.prompt)
        resolution = request.resolver(candidate)
        if not resolution.accepted:
            return SelectionUpdate(
                consumed=True,
                message=resolution.error or request.prompt,
            )
        self._candidate_keys.append(candidate.key)
        self._values.append(resolution.value)
        completed = len(self._values) >= request.maximum_count
        if completed:
            values = tuple(self._values)
            self.request = None
            self._values.clear()
            self._candidate_keys.clear()
            request.on_complete(values)
        return SelectionUpdate(
            consumed=True,
            accepted=True,
            completed=completed,
            message="" if completed else request.prompt,
        )
