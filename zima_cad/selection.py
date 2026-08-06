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
