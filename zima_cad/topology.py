from __future__ import annotations

import json
import base64
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Iterable


class TopologyResolutionState(str, Enum):
    RESOLVED = "resolved"
    MISSING = "missing"
    AMBIGUOUS = "ambiguous"
    INCOMPATIBLE = "incompatible"


@dataclass(frozen=True, order=True)
class FaceRef:
    """Persistent identity of one logical face produced by a feature."""

    feature_id: str
    role: str
    source_id: str | None = None
    fragment: int | None = None

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "feature_id": self.feature_id,
            "role": self.role,
        }
        if self.source_id is not None:
            result["source_id"] = self.source_id
        if self.fragment is not None:
            result["fragment"] = self.fragment
        return result

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> FaceRef:
        feature_id = str(value.get("feature_id", "")).strip()
        role = str(value.get("role", "")).strip()
        if not feature_id or not role:
            raise ValueError("FaceRef requires feature_id and role")
        fragment_value = value.get("fragment")
        return cls(
            feature_id=feature_id,
            role=role,
            source_id=(
                str(value["source_id"])
                if value.get("source_id") is not None
                else None
            ),
            fragment=(
                int(fragment_value) if fragment_value is not None else None
            ),
        )

    def serialize(self) -> str:
        return json.dumps(
            self.to_dict(),
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )

    @classmethod
    def deserialize(cls, value: str) -> FaceRef:
        decoded = json.loads(value)
        if not isinstance(decoded, dict):
            raise ValueError("FaceRef payload must be an object")
        return cls.from_dict(decoded)


@dataclass(frozen=True)
class AssemblyFaceRef:
    instance_id: str
    face: FaceRef

    def to_dict(self) -> dict[str, Any]:
        return {"instance_id": self.instance_id, "face": self.face.to_dict()}

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> AssemblyFaceRef:
        face = value.get("face")
        if not isinstance(face, dict):
            raise ValueError("AssemblyFaceRef requires a face object")
        instance_id = str(value.get("instance_id", "")).strip()
        if not instance_id:
            raise ValueError("AssemblyFaceRef requires instance_id")
        return cls(instance_id=instance_id, face=FaceRef.from_dict(face))


@dataclass(frozen=True)
class TopologyResolution:
    state: TopologyResolutionState
    shape: Any | None = None
    candidates: tuple[Any, ...] = ()


@dataclass
class TopologyRegistry:
    """Runtime bidirectional mapping between OCCT faces and persistent refs."""

    _faces_by_ref: dict[FaceRef, list[Any]] = field(default_factory=dict)
    _refs_by_runtime_index: dict[int, FaceRef] = field(default_factory=dict)
    _runtime_indices_by_ref: dict[FaceRef, list[int]] = field(
        default_factory=dict
    )

    def register_face(
        self,
        reference: FaceRef,
        face: Any,
        *,
        runtime_index: int | None = None,
    ) -> None:
        self._faces_by_ref.setdefault(reference, []).append(face)
        if runtime_index is not None:
            index = int(runtime_index)
            self._refs_by_runtime_index[index] = reference
            self._runtime_indices_by_ref.setdefault(reference, []).append(index)

    def register_faces(
        self,
        reference: FaceRef,
        faces: Iterable[Any],
    ) -> None:
        for face in faces:
            self.register_face(reference, face)

    def reference_for_runtime_index(self, index: int) -> FaceRef | None:
        return self._refs_by_runtime_index.get(int(index))

    def runtime_index_for_reference(self, reference: FaceRef) -> int | None:
        indices = self._runtime_indices_by_ref.get(reference, ())
        return indices[0] if len(indices) == 1 else None

    def resolve(self, reference: FaceRef) -> TopologyResolution:
        exact = tuple(self._faces_by_ref.get(reference, ()))
        if len(exact) == 1:
            return TopologyResolution(
                TopologyResolutionState.RESOLVED,
                shape=exact[0],
                candidates=exact,
            )
        if len(exact) > 1:
            return TopologyResolution(
                TopologyResolutionState.AMBIGUOUS,
                candidates=exact,
            )
        same_feature = tuple(
            face
            for candidate, faces in self._faces_by_ref.items()
            if candidate.feature_id == reference.feature_id
            for face in faces
        )
        return TopologyResolution(
            TopologyResolutionState.INCOMPATIBLE
            if same_feature
            else TopologyResolutionState.MISSING,
            candidates=same_feature,
        )

    @property
    def references(self) -> tuple[FaceRef, ...]:
        return tuple(sorted(self._faces_by_ref))


def parse_face_reference(value: Any) -> FaceRef | None:
    """Read the new object/string representation; reject legacy indices."""

    if isinstance(value, FaceRef):
        return value
    if isinstance(value, dict):
        try:
            return FaceRef.from_dict(value)
        except (TypeError, ValueError):
            return None
    if not isinstance(value, str) or not value.lstrip().startswith("{"):
        return None
    try:
        return FaceRef.deserialize(value)
    except (TypeError, ValueError, json.JSONDecodeError):
        return None


def encode_face_reference(reference: FaceRef) -> str:
    payload = reference.serialize().encode("utf-8")
    return base64.urlsafe_b64encode(payload).decode("ascii").rstrip("=")


def decode_face_reference(token: str) -> FaceRef | None:
    try:
        padding = "=" * (-len(token) % 4)
        payload = base64.urlsafe_b64decode((token + padding).encode("ascii"))
        return FaceRef.deserialize(payload.decode("utf-8"))
    except (ValueError, UnicodeError, json.JSONDecodeError):
        return None
