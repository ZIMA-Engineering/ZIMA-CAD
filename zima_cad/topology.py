from __future__ import annotations

import json
import base64
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Iterable, Mapping


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


@dataclass(frozen=True, order=True)
class EdgeRef:
    """Persistent identity of one logical edge produced by a feature."""

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
    def from_dict(cls, value: dict[str, Any]) -> EdgeRef:
        feature_id = str(value.get("feature_id", "")).strip()
        role = str(value.get("role", "")).strip()
        if not feature_id or not role:
            raise ValueError("EdgeRef requires feature_id and role")
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
    def deserialize(cls, value: str) -> EdgeRef:
        decoded = json.loads(value)
        if not isinstance(decoded, dict):
            raise ValueError("EdgeRef payload must be an object")
        return cls.from_dict(decoded)


@dataclass(frozen=True, order=True)
class VertexRef:
    """Persistent identity of one logical vertex produced by a feature."""

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
    def from_dict(cls, value: dict[str, Any]) -> VertexRef:
        feature_id = str(value.get("feature_id", "")).strip()
        role = str(value.get("role", "")).strip()
        if not feature_id or not role:
            raise ValueError("VertexRef requires feature_id and role")
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
    def deserialize(cls, value: str) -> VertexRef:
        decoded = json.loads(value)
        if not isinstance(decoded, dict):
            raise ValueError("VertexRef payload must be an object")
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

    def serialize(self) -> str:
        return json.dumps(
            self.to_dict(),
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )

    @classmethod
    def deserialize(cls, value: str) -> AssemblyFaceRef:
        decoded = json.loads(value)
        if not isinstance(decoded, dict):
            raise ValueError("AssemblyFaceRef payload must be an object")
        return cls.from_dict(decoded)


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
    _edges_by_ref: dict[EdgeRef, list[Any]] = field(default_factory=dict)
    _edge_refs_by_runtime_index: dict[int, EdgeRef] = field(default_factory=dict)
    _edge_runtime_indices_by_ref: dict[EdgeRef, list[int]] = field(
        default_factory=dict
    )
    _vertices_by_ref: dict[VertexRef, list[Any]] = field(default_factory=dict)
    _vertex_refs_by_runtime_index: dict[int, VertexRef] = field(
        default_factory=dict
    )
    _vertex_runtime_indices_by_ref: dict[VertexRef, list[int]] = field(
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

    def register_edge(
        self,
        reference: EdgeRef,
        edge: Any,
        *,
        runtime_index: int | None = None,
    ) -> None:
        self._edges_by_ref.setdefault(reference, []).append(edge)
        if runtime_index is not None:
            index = int(runtime_index)
            self._edge_refs_by_runtime_index[index] = reference
            self._edge_runtime_indices_by_ref.setdefault(reference, []).append(index)

    def register_vertex(
        self,
        reference: VertexRef,
        vertex: Any,
        *,
        runtime_index: int | None = None,
    ) -> None:
        self._vertices_by_ref.setdefault(reference, []).append(vertex)
        if runtime_index is not None:
            index = int(runtime_index)
            self._vertex_refs_by_runtime_index[index] = reference
            self._vertex_runtime_indices_by_ref.setdefault(reference, []).append(index)

    def reference_for_runtime_index(self, index: int) -> FaceRef | None:
        return self._refs_by_runtime_index.get(int(index))

    def runtime_index_for_reference(self, reference: FaceRef) -> int | None:
        indices = self._runtime_indices_by_ref.get(reference, ())
        return indices[0] if len(indices) == 1 else None

    def edge_reference_for_runtime_index(self, index: int) -> EdgeRef | None:
        return self._edge_refs_by_runtime_index.get(int(index))

    def edge_runtime_index_for_reference(self, reference: EdgeRef) -> int | None:
        indices = self._edge_runtime_indices_by_ref.get(reference, ())
        return indices[0] if len(indices) == 1 else None

    def vertex_reference_for_runtime_index(self, index: int) -> VertexRef | None:
        return self._vertex_refs_by_runtime_index.get(int(index))

    def vertex_runtime_index_for_reference(
        self, reference: VertexRef
    ) -> int | None:
        indices = self._vertex_runtime_indices_by_ref.get(reference, ())
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

    @staticmethod
    def _resolve_reference(
        reference: EdgeRef | VertexRef,
        shapes_by_ref: dict[Any, list[Any]],
    ) -> TopologyResolution:
        exact = tuple(shapes_by_ref.get(reference, ()))
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
            shape
            for candidate, shapes in shapes_by_ref.items()
            if candidate.feature_id == reference.feature_id
            for shape in shapes
        )
        return TopologyResolution(
            TopologyResolutionState.INCOMPATIBLE
            if same_feature
            else TopologyResolutionState.MISSING,
            candidates=same_feature,
        )

    def resolve_edge(self, reference: EdgeRef) -> TopologyResolution:
        return self._resolve_reference(reference, self._edges_by_ref)

    def resolve_vertex(self, reference: VertexRef) -> TopologyResolution:
        return self._resolve_reference(reference, self._vertices_by_ref)

    @property
    def references(self) -> tuple[FaceRef, ...]:
        return tuple(sorted(self._faces_by_ref, key=self._reference_sort_key))

    @property
    def edge_references(self) -> tuple[EdgeRef, ...]:
        return tuple(sorted(
            self._edges_by_ref, key=self._reference_sort_key
        ))

    @property
    def vertex_references(self) -> tuple[VertexRef, ...]:
        return tuple(sorted(
            self._vertices_by_ref, key=self._reference_sort_key
        ))

    @staticmethod
    def _reference_sort_key(reference) -> tuple[Any, ...]:
        return (
            reference.feature_id,
            reference.role,
            reference.source_id or "",
            -1 if reference.fragment is None else reference.fragment,
        )

    @property
    def face_entries(self) -> tuple[tuple[FaceRef, tuple[Any, ...]], ...]:
        return tuple(
            (reference, tuple(self._faces_by_ref[reference]))
            for reference in sorted(
                self._faces_by_ref, key=self._reference_sort_key
            )
        )

    @property
    def edge_entries(self) -> tuple[tuple[EdgeRef, tuple[Any, ...]], ...]:
        return tuple(
            (reference, tuple(self._edges_by_ref[reference]))
            for reference in sorted(
                self._edges_by_ref, key=self._reference_sort_key
            )
        )

    @property
    def vertex_entries(self) -> tuple[tuple[VertexRef, tuple[Any, ...]], ...]:
        return tuple(
            (reference, tuple(self._vertices_by_ref[reference]))
            for reference in sorted(
                self._vertices_by_ref, key=self._reference_sort_key
            )
        )


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


def parse_assembly_face_reference(value: Any) -> AssemblyFaceRef | None:
    if isinstance(value, AssemblyFaceRef):
        return value
    if isinstance(value, dict):
        try:
            return AssemblyFaceRef.from_dict(value)
        except (TypeError, ValueError):
            return None
    if not isinstance(value, str) or not value.lstrip().startswith("{"):
        return None
    try:
        return AssemblyFaceRef.deserialize(value)
    except (TypeError, ValueError, json.JSONDecodeError):
        return None


def encode_assembly_face_reference(reference: AssemblyFaceRef) -> str:
    payload = reference.serialize().encode("utf-8")
    return base64.urlsafe_b64encode(payload).decode("ascii").rstrip("=")


def decode_assembly_face_reference(token: str) -> AssemblyFaceRef | None:
    try:
        padding = "=" * (-len(token) % 4)
        payload = base64.urlsafe_b64decode((token + padding).encode("ascii"))
        return AssemblyFaceRef.deserialize(payload.decode("utf-8"))
    except (ValueError, UnicodeError, json.JSONDecodeError):
        return None


ASSEMBLY_FACE_DESCRIPTOR_PREFIX = "assembly-face-ref:"


def assembly_face_descriptor(reference: AssemblyFaceRef) -> str:
    return ASSEMBLY_FACE_DESCRIPTOR_PREFIX + encode_assembly_face_reference(
        reference
    )


def parse_assembly_face_descriptor(value: str) -> AssemblyFaceRef | None:
    if not value.startswith(ASSEMBLY_FACE_DESCRIPTOR_PREFIX):
        return None
    return decode_assembly_face_reference(
        value[len(ASSEMBLY_FACE_DESCRIPTOR_PREFIX):]
    )


def resolve_assembly_face(
    reference: AssemblyFaceRef,
    registries_by_instance: Mapping[str, TopologyRegistry],
) -> TopologyResolution:
    registry = registries_by_instance.get(reference.instance_id)
    if registry is None:
        return TopologyResolution(TopologyResolutionState.MISSING)
    return registry.resolve(reference.face)


def parse_edge_reference(value: Any) -> EdgeRef | None:
    if isinstance(value, EdgeRef):
        return value
    if isinstance(value, dict):
        try:
            return EdgeRef.from_dict(value)
        except (TypeError, ValueError):
            return None
    if not isinstance(value, str) or not value.lstrip().startswith("{"):
        return None
    try:
        return EdgeRef.deserialize(value)
    except (TypeError, ValueError, json.JSONDecodeError):
        return None


def encode_edge_reference(reference: EdgeRef) -> str:
    payload = reference.serialize().encode("utf-8")
    return base64.urlsafe_b64encode(payload).decode("ascii").rstrip("=")


def decode_edge_reference(token: str) -> EdgeRef | None:
    try:
        padding = "=" * (-len(token) % 4)
        payload = base64.urlsafe_b64decode((token + padding).encode("ascii"))
        return EdgeRef.deserialize(payload.decode("utf-8"))
    except (ValueError, UnicodeError, json.JSONDecodeError):
        return None


def parse_vertex_reference(value: Any) -> VertexRef | None:
    if isinstance(value, VertexRef):
        return value
    if isinstance(value, dict):
        try:
            return VertexRef.from_dict(value)
        except (TypeError, ValueError):
            return None
    if not isinstance(value, str) or not value.lstrip().startswith("{"):
        return None
    try:
        return VertexRef.deserialize(value)
    except (TypeError, ValueError, json.JSONDecodeError):
        return None


def encode_vertex_reference(reference: VertexRef) -> str:
    payload = reference.serialize().encode("utf-8")
    return base64.urlsafe_b64encode(payload).decode("ascii").rstrip("=")


def decode_vertex_reference(token: str) -> VertexRef | None:
    try:
        padding = "=" * (-len(token) % 4)
        payload = base64.urlsafe_b64decode((token + padding).encode("ascii"))
        return VertexRef.deserialize(payload.decode("utf-8"))
    except (ValueError, UnicodeError, json.JSONDecodeError):
        return None


def semantic_provenance_id(*references: FaceRef | EdgeRef | VertexRef) -> str:
    """Canonical, kernel-independent identity of a derived topology source."""

    payload = [
        {
            "kind": (
                "face" if isinstance(reference, FaceRef)
                else "edge" if isinstance(reference, EdgeRef)
                else "vertex"
            ),
            **reference.to_dict(),
        }
        for reference in references
    ]
    payload.sort(key=lambda item: json.dumps(
        item, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ))
    return json.dumps(
        payload,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    )
