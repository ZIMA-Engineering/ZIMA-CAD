from __future__ import annotations

import math
import json
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any
from uuid import uuid4

from OCC.Core.BRepAlgoAPI import (
    BRepAlgoAPI_Common,
    BRepAlgoAPI_Cut,
    BRepAlgoAPI_Fuse,
)
from OCC.Core.BRepGProp import brepgprop
from OCC.Core.BRepExtrema import BRepExtrema_DistShapeShape
from OCC.Core.BRepFilletAPI import BRepFilletAPI_MakeFillet
from OCC.Core.BRepBuilderAPI import (
    BRepBuilderAPI_MakeEdge,
    BRepBuilderAPI_MakeFace,
    BRepBuilderAPI_MakePolygon,
    BRepBuilderAPI_MakeVertex,
    BRepBuilderAPI_MakeWire,
    BRepBuilderAPI_Transform,
)
from OCC.Core.BRep import BRep_Builder, BRep_Tool
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCC.Core.GC import GC_MakeArcOfCircle, GC_MakeArcOfEllipse
from OCC.Core.GeomAbs import (
    GeomAbs_Circle,
    GeomAbs_Cylinder,
    GeomAbs_Ellipse,
    GeomAbs_Plane,
)
from OCC.Core.GeomAPI import GeomAPI_Interpolate
from OCC.Core.GProp import GProp_GProps
from OCC.Core.BRepOffsetAPI import BRepOffsetAPI_ThruSections
from OCC.Core.BRepPrimAPI import (
    BRepPrimAPI_MakeBox,
    BRepPrimAPI_MakeCone,
    BRepPrimAPI_MakeCylinder,
    BRepPrimAPI_MakeRevol,
    BRepPrimAPI_MakeSphere,
    BRepPrimAPI_MakeWedge,
    BRepPrimAPI_MakePrism,
)
from OCC.Core.gp import (
    gp_Ax1,
    gp_Ax2,
    gp_Circ,
    gp_Dir,
    gp_Elips,
    gp_Pnt,
    gp_Trsf,
    gp_Vec,
)
from OCC.Core.TColgp import TColgp_HArray1OfPnt
from OCC.Core.TopoDS import TopoDS_Compound
from OCC.Core.TopAbs import (
    TopAbs_EDGE,
    TopAbs_FACE,
    TopAbs_REVERSED,
    TopAbs_SOLID,
    TopAbs_VERTEX,
)
from OCC.Core.TopExp import TopExp_Explorer

from zima_cad.sketch_model import SketchModel, SketchModelError
from zima_cad.sketch_geometry import (
    center_arc_points,
    ellipse_points,
    elliptical_arc_points,
    evaluate_corner_radius,
)
from zima_cad.topology import (
    EdgeRef,
    FaceRef,
    parse_edge_reference,
    TopologyRegistry,
    VertexRef,
    semantic_provenance_id,
)


ORIGIN_WIDGET_SIZE = 320.0
DOCUMENT_FORMAT_VERSION = "9"


def default_document_settings() -> dict[str, str]:
    return {
        "document_id": str(uuid4()),
        "type": "part",
        "format_version": DOCUMENT_FORMAT_VERSION,
        "body_visible": "true",
        "body_suppressed": "false",
    }


def default_document_units() -> dict[str, str]:
    return {
        "Length": "mm",
        "Angle": "deg",
        "Mass": "kg",
        "Time": "s",
        "Temperature": "C",
        "Stress": "MPa",
    }


def default_document_precision() -> dict[str, str]:
    return {
        "linear_tolerance": "0.001",
        "angular_tolerance": "0.001",
        "mesh_deflection": "0.1",
        "decimal_places": "3",
    }


def default_physical_parameters() -> dict[str, str]:
    return {"MATERIAL_NAME": ""}


def default_user_parameters() -> dict[str, str]:
    return {
        "nazev": "",
        "norma": "",
        "polotovar": "",
        "material": "",
        "mnozstvi": "1",
        "kreslil": "",
        "schvalil": "",
        "verze": "00",
        "datum": "",
        "presnost": "ISO 2768-mK",
        "tolerovani": "ISO 8015",
        "hmotnost_sestavy": "",
        "mnozstvi_sestav": "",
        "hmotnost": "",
    }


def default_user_parameter_order() -> list[str]:
    return [
        "nazev",
        "norma",
        "polotovar",
        "material",
        "mnozstvi",
        "kreslil",
        "schvalil",
        "verze",
        "datum",
        "presnost",
        "tolerovani",
        "hmotnost_sestavy",
        "mnozstvi_sestav",
        "hmotnost",
    ]


def default_user_parameter_labels() -> dict[str, dict[str, str]]:
    return {
        "datum": {"cs": "Datum", "de": "Datum", "en": "Date", "fr": "Date"},
        "kreslil": {
            "cs": "Kreslil",
            "de": "Gezeichnet von",
            "en": "Drew",
            "fr": "Dessiné par",
        },
        "hmotnost_sestavy": {
            "cs": "Hmotnost sestavy",
            "de": "Baugruppengewicht",
            "en": "Assembly weight",
            "fr": "Masse de l'assemblage",
        },
        "hmotnost": {
            "cs": "Hmotnost",
            "de": "Gewicht",
            "en": "Weight",
            "fr": "Masse",
        },
        "material": {
            "cs": "Materi\u00e1l",
            "de": "Material",
            "en": "Material",
            "fr": "Matériau",
        },
        "mnozstvi": {
            "cs": "Mno\u017estv\u00ed",
            "de": "Menge",
            "en": "Quantity",
            "fr": "Quantité",
        },
        "mnozstvi_sestav": {
            "cs": "Mno\u017estv\u00ed sestav",
            "de": "Baugruppenmenge",
            "en": "Assembly quantity",
            "fr": "Quantit\u00e9 d'assemblages",
        },
        "nazev": {"cs": "N\u00e1zev", "de": "Name", "en": "Name", "fr": "Nom"},
        "presnost": {
            "cs": "P\u0159esnost",
            "de": "Genauigkeit",
            "en": "Accuracy",
            "fr": "Pr\u00e9cision",
        },
        "norma": {"cs": "Norma", "de": "Norm", "en": "Standard", "fr": "Norme"},
        "polotovar": {
            "cs": "Polotovar",
            "de": "Rohteil",
            "en": "Stock",
            "fr": "Ébauche",
        },
        "schvalil": {
            "cs": "Schvalil",
            "de": "Genehmigt von",
            "en": "Approved",
            "fr": "Approuvé par",
        },
        "verze": {"cs": "Verze", "de": "Version", "en": "Version", "fr": "Version"},
        "tolerovani": {
            "cs": "Tolerov\u00e1n\u00ed",
            "de": "Tolerierung",
            "en": "Tolerancing",
            "fr": "Tol\u00e9rancement",
        },
    }


def default_user_parameter_values() -> dict[str, dict[str, str]]:
    return {key: {"": value} for key, value in default_user_parameters().items()}


class CombineMode(str, Enum):
    NONE = "0"
    ADD = "+"
    SUBTRACT = "-"


class EntityKind(str, Enum):
    PART = "part"
    CONTAINER = "container"
    BODY = "body"
    ORIGIN = "origin"
    POINT = "point"
    AXIS = "axis"
    PLANE = "plane"
    SKETCH = "sketch"
    PROTRUSION = "protrusion"
    REVOLVE = "revolve"
    FILLET = "fillet"
    BOX = "box"
    SPHERE = "sphere"
    CYLINDER = "cylinder"
    CONE = "cone"
    PYRAMID = "pyramid"
    WEDGE = "wedge"


class ContainerType(str, Enum):
    EMPTY = "EMPTY"
    POINT = "POINT"
    AXIS = "AXIS"
    PLANE = "PLANE"
    SKETCH = "SKETCH"
    BOX = "BOX"
    SPHERE = "SPHERE"
    CYLINDER = "CYLINDER"
    CONE = "CONE"
    PYRAMID = "PYRAMID"
    WEDGE = "WEDGE"
    PROTRUSION = "PROTRUSION"
    REVOLVE = "REVOLVE"
    FILLET = "FILLET"
    COMPONENT = "COMPONENT"


class TreeExposure(str, Enum):
    PUBLIC = "public"
    INTERNAL = "internal"
    HIDDEN = "hidden"


class OriginScope(str, Enum):
    PART = "part"
    ASSEMBLY = "assembly"
    CONTAINER = "container"
    LOCAL = "local"


class SketchRole(str, Enum):
    PROFILE = "PROFILE"
    PATH = "PATH"
    GUIDE = "GUIDE"
    SECTION = "SECTION"


ENTITY_KINDS = frozenset(
    {
        EntityKind.POINT,
        EntityKind.AXIS,
        EntityKind.PLANE,
        EntityKind.SKETCH,
        EntityKind.PROTRUSION,
        EntityKind.REVOLVE,
        EntityKind.FILLET,
        EntityKind.BOX,
        EntityKind.SPHERE,
        EntityKind.CYLINDER,
        EntityKind.CONE,
        EntityKind.PYRAMID,
        EntityKind.WEDGE,
    }
)

SOLID_KINDS = frozenset(
    {
        EntityKind.BOX,
        EntityKind.SPHERE,
        EntityKind.CYLINDER,
        EntityKind.CONE,
        EntityKind.PYRAMID,
        EntityKind.WEDGE,
    }
)


@dataclass
class CoordinateSystem:
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0)
    x_axis: tuple[float, float, float] = (1.0, 0.0, 0.0)
    y_axis: tuple[float, float, float] = (0.0, 1.0, 0.0)
    z_axis: tuple[float, float, float] = (0.0, 0.0, 1.0)


@dataclass
class PlaneOnFaceAttachment:
    source_plane: str
    target_object_id: str
    target_face_role: str
    primary_axis: str = "x"
    secondary_axis: str = "y"
    active_axis: str = "x"
    switch_angle: float = 45.0
    flip_normal: bool = False
    status: str = "resolved"


@dataclass
class ZimaEntity:
    name: str
    kind: EntityKind
    combine_mode: CombineMode = CombineMode.NONE
    coordinate_system: CoordinateSystem = field(default_factory=CoordinateSystem)
    parameters: dict[str, Any] = field(default_factory=dict)
    children: list["ZimaEntity"] = field(default_factory=list)
    entity_id: str = field(default_factory=lambda: uuid4().hex)
    locked: bool = False
    attachment: PlaneOnFaceAttachment | None = None
    user_visible: bool = True
    suppressed: bool = False
    tree_exposure: TreeExposure = TreeExposure.PUBLIC
    show_internal_entities: bool = True
    show_auxiliary_geometry: bool = False
    origin_scope: OriginScope | None = None

    def add_child(self, child: "ZimaEntity") -> None:
        self.children.append(child)

    def entity_children(self) -> list["ZimaEntity"]:
        return [
            child
            for child in self.children
            if not child.locked and child.kind in ENTITY_KINDS
        ]

    def body_children(self) -> list["ZimaEntity"]:
        return [child for child in self.children if child.kind == EntityKind.BODY]

    def sketch_role(self) -> SketchRole | None:
        if self.kind != EntityKind.SKETCH:
            return None
        try:
            return SketchRole(
                str(self.parameters.get("role", SketchRole.PROFILE.value)).upper()
            )
        except ValueError:
            return None

    @property
    def container_type(self) -> ContainerType:
        configured_type = self.parameters.get("container_type")
        if configured_type is not None:
            return ContainerType(str(configured_type).upper())
        entities = self.entity_children()
        solid = next(
            (entity for entity in entities if entity.kind in SOLID_KINDS),
            None,
        )
        if solid is not None:
            return ContainerType(solid.kind.value.upper())
        if any(entity.kind == EntityKind.SKETCH for entity in entities):
            return ContainerType.SKETCH
        if any(entity.kind == EntityKind.AXIS for entity in entities):
            return ContainerType.AXIS
        if any(entity.kind == EntityKind.PLANE for entity in entities):
            return ContainerType.PLANE
        return ContainerType.POINT

    def has_valid_entity_combination(self) -> bool:
        entities = self.entity_children()
        if self.parameters.get("experimental_container") == "true":
            return all(
                entity.kind
                in (
                    EntityKind.POINT,
                    EntityKind.AXIS,
                    EntityKind.PLANE,
                    EntityKind.SKETCH,
                )
                for entity in entities
            )
        points = [entity for entity in entities if entity.kind == EntityKind.POINT]
        axes = [entity for entity in entities if entity.kind == EntityKind.AXIS]
        planes = [entity for entity in entities if entity.kind == EntityKind.PLANE]
        sketches = [entity for entity in entities if entity.kind == EntityKind.SKETCH]
        features = [
            entity for entity in entities
            if entity.kind in (
                EntityKind.PROTRUSION, EntityKind.REVOLVE, EntityKind.FILLET
            )
        ]
        solids = [entity for entity in entities if entity.kind in SOLID_KINDS]
        if (
            len(points) > 1
            or len(axes) > 1
            or len(planes) > 1
            or len(features) > 1
            or len(solids) > 1
        ):
            return False
        if (
            len(points)
            + len(axes)
            + len(planes)
            + len(sketches)
            + len(features)
            + len(solids)
            != len(entities)
        ):
            return False
        if points and len(entities) != 1:
            return False
        if axes and len(entities) != 1:
            return False
        if planes and len(entities) != 1:
            return False
        if features and any((points, axes, planes, solids)):
            return False
        roles = [sketch.sketch_role() for sketch in sketches]
        if any(role is None for role in roles):
            return False
        if roles.count(SketchRole.PROFILE) > 1:
            return False
        if roles.count(SketchRole.PATH) > 1:
            return False
        return True

    def can_accept_entity(
        self,
        kind: EntityKind | None = None,
        sketch_role: SketchRole = SketchRole.PROFILE,
    ) -> bool:
        if self.kind != EntityKind.CONTAINER:
            return False
        if kind is None:
            candidates = (
                EntityKind.POINT,
                EntityKind.AXIS,
                EntityKind.SKETCH,
                EntityKind.PROTRUSION,
                EntityKind.REVOLVE,
                EntityKind.FILLET,
                EntityKind.BOX,
                EntityKind.SPHERE,
                EntityKind.CYLINDER,
                EntityKind.CONE,
                EntityKind.PYRAMID,
                EntityKind.WEDGE,
            )
            return any(self.can_accept_entity(candidate) for candidate in candidates)
        if kind not in ENTITY_KINDS:
            return False
        parameters = (
            {"role": sketch_role.value}
            if kind == EntityKind.SKETCH
            else {}
        )
        candidate = ZimaEntity(name="", kind=kind, parameters=parameters)
        self.children.append(candidate)
        try:
            return self.has_valid_entity_combination()
        finally:
            self.children.pop()


@dataclass
class PartDocument:
    regeneration_required: bool = False
    source_file_path: Path | None = field(default=None, repr=False, compare=False)
    document_settings: dict[str, str] = field(default_factory=default_document_settings)
    document_units: dict[str, str] = field(default_factory=default_document_units)
    document_precision: dict[str, str] = field(default_factory=default_document_precision)
    physical_parameters: dict[str, str] = field(default_factory=default_physical_parameters)
    physical_parameter_units: dict[str, str] = field(default_factory=dict)
    material_parameter_descriptions: dict[str, dict[str, str]] = field(
        default_factory=dict
    )
    user_parameters: dict[str, str] = field(default_factory=default_user_parameters)
    user_parameter_order: list[str] = field(default_factory=default_user_parameter_order)
    user_parameter_labels: dict[str, dict[str, str]] = field(
        default_factory=default_user_parameter_labels
    )
    user_parameter_values: dict[str, dict[str, str]] = field(
        default_factory=default_user_parameter_values
    )
    root: ZimaEntity = field(
        default_factory=lambda: ZimaEntity(
            name="Part001",
            kind=EntityKind.PART,
            combine_mode=CombineMode.NONE,
        )
    )

    def __post_init__(self) -> None:
        if not any(child.kind == EntityKind.ORIGIN for child in self.root.children):
            scope = (
                OriginScope.ASSEMBLY
                if self.document_settings.get("type") == "assembly"
                else OriginScope.PART
            )
            self.root.children.insert(
                0,
                create_origin_object("document", scope),
            )

    def visible_objects(self) -> list[ZimaEntity]:
        return [obj for obj in self.root.children if obj.kind != EntityKind.ORIGIN]

    def history_objects(self) -> list[ZimaEntity]:
        return [
            obj for obj in self.root.children
            if obj.kind == EntityKind.CONTAINER
        ]

    def history_cursor(self) -> int:
        try:
            cursor = int(
                self.document_settings.get(
                    "history_cursor",
                    len(self.history_objects()),
                )
            )
        except (TypeError, ValueError):
            cursor = len(self.history_objects())
        return max(0, min(cursor, len(self.history_objects())))

    def set_history_cursor(self, cursor: int) -> None:
        self.document_settings["history_cursor"] = str(
            max(0, min(cursor, len(self.history_objects())))
        )

    def body_is_visible(self) -> bool:
        return self.document_settings.get("body_visible", "true").lower() == "true"

    def set_body_visible(self, visible: bool) -> None:
        self.document_settings["body_visible"] = str(visible).lower()

    def body_is_suppressed(self) -> bool:
        return self.document_settings.get("body_suppressed", "false").lower() == "true"

    def set_body_suppressed(self, suppressed: bool) -> None:
        self.document_settings["body_suppressed"] = str(suppressed).lower()

    def move_history_object(self, entity_id: str, target_index: int) -> bool:
        history = self.history_objects()
        moving = next(
            (obj for obj in history if obj.entity_id == entity_id),
            None,
        )
        if moving is None:
            return False
        remaining = [obj for obj in history if obj is not moving]
        target_index = max(0, min(target_index, len(remaining)))
        if history.index(moving) == target_index:
            return False

        self.root.children.remove(moving)
        if target_index < len(remaining):
            insertion_index = self.root.children.index(remaining[target_index])
            self.root.children.insert(insertion_index, moving)
        elif remaining:
            insertion_index = self.root.children.index(remaining[-1]) + 1
            self.root.children.insert(insertion_index, moving)
        else:
            self.root.add_child(moving)
        return True

    def active_history_objects(self) -> list[ZimaEntity]:
        return self.history_objects()[:self.history_cursor()]

    def history_index(self, entity_id: str) -> int | None:
        return next(
            (
                index
                for index, obj in enumerate(self.history_objects())
                if obj.entity_id == entity_id
            ),
            None,
        )

    def history_objects_at(self, cursor: int) -> list[ZimaEntity]:
        history = self.history_objects()
        return history[:max(0, min(cursor, len(history)))]

    def history_objects_before(self, entity_id: str) -> list[ZimaEntity]:
        index = self.history_index(entity_id)
        return self.history_objects_at(
            len(self.history_objects()) if index is None else index
        )

    def find_entity(self, entity_id: str) -> ZimaEntity | None:
        return find_child_entity(self.root, entity_id)

    def next_container_name(self, prefix: str = "Container") -> str:
        existing = {child.name for child in self.root.children}
        index = 1
        while True:
            name = f"{prefix}{index:03}"
            if name not in existing:
                return name
            index += 1

    def create_container(
        self,
        name_prefix: str = "Container",
        container_type: ContainerType = ContainerType.EMPTY,
    ) -> ZimaEntity:
        obj = ZimaEntity(
            name=self.next_container_name(name_prefix),
            kind=EntityKind.CONTAINER,
            combine_mode=CombineMode.NONE,
            parameters={"container_type": container_type.value},
        )
        add_coordinate_system_children(obj)
        history = self.history_objects()
        cursor = self.history_cursor()
        if cursor < len(history):
            insertion_index = self.root.children.index(history[cursor])
            self.root.children.insert(insertion_index, obj)
        else:
            self.root.add_child(obj)
        self.set_history_cursor(cursor + 1)
        return obj

    def delete_container(self, entity_id: str) -> bool:
        history = self.history_objects()
        cursor = self.history_cursor()
        deleted_index = next(
            (index for index, obj in enumerate(history) if obj.entity_id == entity_id),
            None,
        )
        deleted = delete_child_entity(self.root, entity_id)
        if deleted and deleted_index is not None and deleted_index < cursor:
            self.set_history_cursor(cursor - 1)
        return deleted

    def find_parent(self, entity_id: str) -> ZimaEntity | None:
        return find_parent_entity(self.root, entity_id)

    def find_owning_object(self, entity_id: str) -> ZimaEntity | None:
        parent = self.find_parent(entity_id)
        while parent is not None and parent.kind != EntityKind.CONTAINER:
            parent = self.find_parent(parent.entity_id)
        return parent

    def is_effectively_visible(self, entity_id: str) -> bool:
        # Per-object visibility was removed when the automatic Body became
        # authoritative.  Keep reading the legacy field for file
        # compatibility, but it must no longer leave old documents hidden
        # without any corresponding control in the UI.
        return not self.is_effectively_suppressed(entity_id)

    def is_effectively_suppressed(self, entity_id: str) -> bool:
        obj = self.find_entity(entity_id)
        owning_history_object = obj
        while (
            owning_history_object is not None
            and owning_history_object.kind != EntityKind.CONTAINER
        ):
            owning_history_object = self.find_parent(owning_history_object.entity_id)
        if owning_history_object is not None:
            history = self.history_objects()
            try:
                if history.index(owning_history_object) >= self.history_cursor():
                    return True
            except ValueError:
                pass
        while obj is not None:
            if obj.suppressed:
                return True
            obj = self.find_parent(obj.entity_id)
        return False

    def create_sketch_on_plane(
        self,
        plane_id: str,
        role: SketchRole = SketchRole.PROFILE,
    ) -> ZimaEntity | None:
        parent = self.find_owning_object(plane_id)
        plane = self.find_entity(plane_id)
        if parent is None or plane is None:
            return None
        if (
            parent.kind != EntityKind.CONTAINER
            or plane.kind != EntityKind.PLANE
            or not parent.can_accept_entity(EntityKind.SKETCH, role)
        ):
            return None

        return self.create_sketch(
            parent.entity_id,
            str(plane.parameters.get("plane", "")),
            role,
        )

    def create_sketch(
        self,
        parent_id: str,
        plane: str = "xz",
        role: SketchRole = SketchRole.PROFILE,
        name_prefix: str = "Sketch",
    ) -> ZimaEntity | None:
        parent = self.find_entity(parent_id)
        if parent is None or not parent.can_accept_entity(EntityKind.SKETCH, role):
            return None
        if plane not in {"xy", "yz", "xz"}:
            return None

        sketch = ZimaEntity(
            name=next_child_name(parent, name_prefix),
            kind=EntityKind.SKETCH,
            combine_mode=CombineMode.NONE,
            parameters={
                "plane": plane,
                "profile": "entities",
                "sketch_data": json.dumps(SketchModel().to_dict()),
                "external_references": "[]",
                "unit": "mm",
                "role": role.value,
            },
        )
        parent.add_child(sketch)
        return sketch

    def create_datum_axis(self, parent_id: str) -> ZimaEntity | None:
        parent = self.find_entity(parent_id)
        if parent is None or not parent.can_accept_entity(EntityKind.AXIS):
            return None
        axis = ZimaEntity(
            name=next_child_name(parent, "Axis"),
            kind=EntityKind.AXIS,
            parameters={
                "display_style": "centerline",
                "axis": "z",
                "length": "50",
                "unit": "mm",
            },
            tree_exposure=TreeExposure.INTERNAL,
        )
        parent.add_child(axis)
        return axis

    def create_datum_plane(self, parent_id: str) -> ZimaEntity | None:
        parent = self.find_entity(parent_id)
        if parent is None or not parent.can_accept_entity(EntityKind.PLANE):
            return None
        plane = ZimaEntity(
            name=next_child_name(parent, "Plane"),
            kind=EntityKind.PLANE,
            parameters={
                "display_style": "datum",
                "plane": "xy",
                "size": "50",
                "unit": "mm",
            },
            tree_exposure=TreeExposure.INTERNAL,
        )
        parent.add_child(plane)
        return plane

    def create_point(self, parent_id: str) -> ZimaEntity | None:
        parent = self.find_entity(parent_id)
        if parent is None or not parent.can_accept_entity(EntityKind.POINT):
            return None
        point = ZimaEntity(
            name=next_child_name(parent, "Point"),
            kind=EntityKind.POINT,
            parameters={"unit": "mm"},
            tree_exposure=TreeExposure.INTERNAL,
        )
        parent.add_child(point)
        return point

    def create_body(self, parent_id: str) -> ZimaEntity | None:
        parent = self.find_entity(parent_id)
        if parent is None:
            return None
        if parent.kind != EntityKind.PART:
            parent = self.root
        previous = [
            child
            for child in self.root.children
            if child.kind != EntityKind.ORIGIN and not child.suppressed
        ]
        if not previous:
            return None
        body = ZimaEntity(
            name=next_child_name(self.root, "Body"),
            kind=EntityKind.BODY,
            parameters={
                "source_ids": ",".join(child.entity_id for child in previous),
            },
        )
        self.root.add_child(body)
        return body

    def _solid_feature_parent(self, source: ZimaEntity) -> ZimaEntity | None:
        parent = source
        if source.kind == EntityKind.POINT:
            source_parent = self.find_owning_object(source.entity_id)
            if source_parent is None:
                return None
            parent = source_parent
        if parent.kind == EntityKind.CONTAINER:
            return parent
        return None

    def create_cube(self, source_id: str) -> ZimaEntity | None:
        source = self.find_entity(source_id)
        if source is None:
            return None

        parent = self._solid_feature_parent(source)

        if parent is None or not parent.can_accept_entity(EntityKind.BOX):
            return None

        cube = ZimaEntity(
            name=next_child_name(parent, "Cube"),
            kind=EntityKind.BOX,
            combine_mode=CombineMode.ADD,
            parameters={
                "length": "10",
                "width": "10",
                "height": "10",
                "unit": "mm",
            },
        )
        parent.add_child(cube)
        return cube

    def create_wedge(self, source_id: str) -> ZimaEntity | None:
        source = self.find_entity(source_id)
        if source is None:
            return None

        parent = self._solid_feature_parent(source)

        if parent is None or not parent.can_accept_entity(EntityKind.WEDGE):
            return None

        wedge = ZimaEntity(
            name=next_child_name(parent, "Wedge"),
            kind=EntityKind.WEDGE,
            combine_mode=CombineMode.ADD,
            parameters={
                "length": "100",
                "width": "60",
                "height": "50",
                "top_offset": "50",
                "unit": "mm",
            },
        )
        parent.add_child(wedge)
        return wedge

    def create_primitive(
        self,
        parent_id: str,
        kind: EntityKind,
    ) -> ZimaEntity | None:
        source = self.find_entity(parent_id)
        parent = self._solid_feature_parent(source) if source is not None else None
        definitions = {
            EntityKind.BOX: (
                "Cube",
                {"length": "40", "width": "30", "height": "20", "unit": "mm"},
            ),
            EntityKind.SPHERE: (
                "Sphere",
                {"diameter": "30", "unit": "mm"},
            ),
            EntityKind.CYLINDER: (
                "Cylinder",
                {"diameter": "30", "height": "50", "unit": "mm"},
            ),
            EntityKind.CONE: (
                "Cone",
                {
                    "bottom_diameter": "40",
                    "top_diameter": "0",
                    "height": "50",
                    "unit": "mm",
                },
            ),
            EntityKind.PYRAMID: (
                "Pyramid",
                {"length": "40", "width": "40", "height": "50", "unit": "mm"},
            ),
            EntityKind.WEDGE: (
                "Wedge",
                {
                    "length": "60",
                    "width": "40",
                    "height": "40",
                    "top_offset": "30",
                    "unit": "mm",
                },
            ),
        }
        if (
            parent is None
            or kind not in definitions
            or not parent.can_accept_entity(kind)
        ):
            return None
        name_prefix, parameters = definitions[kind]
        primitive = ZimaEntity(
            name=next_child_name(parent, name_prefix),
            kind=kind,
            combine_mode=CombineMode.ADD,
            parameters=parameters,
        )
        parent.add_child(primitive)
        self.sync_generated_axes_for_object(parent)
        return primitive

    def sync_generated_axes(self) -> None:
        """Keep locked, feature-owned axes in step with their source geometry."""
        for obj in self.history_objects():
            self.sync_generated_axes_for_object(obj)

    def sync_generated_axes_for_object(self, obj: ZimaEntity) -> None:
        desired: dict[str, dict[str, Any]] = {}
        feature = next(
            (
                child for child in obj.children
                if child.kind == EntityKind.PROTRUSION and not child.locked
            ),
            None,
        )
        if feature is not None:
            sketch = self.find_entity(str(feature.parameters.get("sketch_id", "")))
            if sketch is not None and sketch.kind == EntityKind.SKETCH:
                try:
                    sketch_model = SketchModel.from_dict(json.loads(
                        str(sketch.parameters.get("sketch_data", "{}"))
                    ))
                except (SketchModelError, TypeError, ValueError, json.JSONDecodeError):
                    sketch_model = None
                if sketch_model is not None:
                    plane = str(sketch.parameters.get("plane", "xz"))
                    direction = {"xy": "z", "xz": "y", "yz": "x"}.get(plane, "y")
                    forward = max(0.0, float(feature.parameters.get(
                        "length_forward", feature.parameters.get("length", 10.0)
                    )))
                    reverse = max(0.0, float(feature.parameters.get("length_reverse", 0.0)))
                    mode = str(feature.parameters.get("extent_mode", "one_side"))
                    if mode == "symmetric":
                        reverse = forward
                    elif mode == "one_side":
                        if str(feature.parameters.get("direction", "forward")) == "reverse":
                            reverse, forward = forward, 0.0
                        else:
                            reverse = 0.0
                    span = max(forward + reverse, 1.0)
                    axial_center = (forward - reverse) / 2.0
                    for geometry_id, geometry in sketch_model.geometry.items():
                        if geometry.geometry_type.value != "circle" or not geometry.point_ids:
                            continue
                        center = sketch_model.points.get(geometry.point_ids[0])
                        if center is None:
                            continue
                        origin = {
                            "xy": (center.x, center.y, axial_center),
                            "xz": (center.x, axial_center, center.y),
                            "yz": (axial_center, center.x, center.y),
                        }.get(plane, (center.x, axial_center, center.y))
                        axis_id = f"{feature.entity_id}:axis:circle:{geometry_id}"
                        desired[axis_id] = {
                            "owner": feature,
                            "name": f"Axis {geometry_id}",
                            "axis": direction,
                            "origin": origin,
                            "length": max(span * 1.2, span + 10.0),
                            "source_geometry_id": geometry_id,
                        }

        for primitive in (
            child for child in obj.children
            if not child.locked and child.kind in (
                EntityKind.CYLINDER, EntityKind.CONE, EntityKind.SPHERE
            )
        ):
            axes = ("x", "y", "z") if primitive.kind == EntityKind.SPHERE else ("z",)
            size = (
                float(primitive.parameters.get("diameter", 30.0))
                if primitive.kind == EntityKind.SPHERE
                else float(primitive.parameters.get("height", 50.0))
            )
            for direction in axes:
                axis_id = f"{primitive.entity_id}:axis:{direction}"
                desired[axis_id] = {
                    "owner": primitive,
                    "name": f"{direction.upper()} Axis",
                    "axis": direction,
                    "origin": (0.0, 0.0, 0.0),
                    "length": max(size * 1.2, size + 10.0),
                    "source_geometry_id": "",
                }

        generated = {
            child.entity_id: child
            for owner in obj.children
            for child in owner.children
            if child.kind == EntityKind.AXIS
            and child.parameters.get("generated_axis") == "true"
        }
        for axis_id, definition in desired.items():
            axis = generated.pop(axis_id, None)
            owner = definition["owner"]
            if axis is None:
                axis = ZimaEntity(
                    name=definition["name"],
                    kind=EntityKind.AXIS,
                    entity_id=axis_id,
                    locked=True,
                    tree_exposure=TreeExposure.INTERNAL,
                )
                owner.add_child(axis)
            origin = definition["origin"]
            axis.parameters.update({
                "generated_axis": "true",
                "display_style": "centerline",
                "axis": definition["axis"],
                "origin_x": f"{origin[0]:.12g}",
                "origin_y": f"{origin[1]:.12g}",
                "origin_z": f"{origin[2]:.12g}",
                "length": f"{definition['length']:.12g}",
                "source_geometry_id": definition["source_geometry_id"],
                "unit": "mm",
            })
        for stale in generated.values():
            parent = self.find_parent(stale.entity_id)
            if parent is not None:
                parent.children.remove(stale)

    def rebuild_shape(self):
        self.resolve_attachments()
        return self.build_active_shape()

    def build_active_shape(self):
        """Build the automatic solid result up to the history cursor."""
        return self.build_shape_at(self.history_cursor())

    def build_shape_before(self, entity_id: str):
        """Build the automatic solid result immediately before an object."""
        index = self.history_index(entity_id)
        return self.build_shape_at(
            len(self.history_objects()) if index is None else index
        )

    def build_shape_at(self, cursor: int):
        """Build the automatic solid result at an explicit history boundary."""
        return self.build_shape_for_objects(self.history_objects_at(cursor))

    def build_shape_for_object_ids(self, object_ids: list[str]):
        """Build a history snapshot from stable source object IDs."""
        objects = []
        for entity_id in object_ids:
            obj = self.find_entity(entity_id)
            if obj is not None and obj.kind == EntityKind.CONTAINER:
                objects.append(obj)
        return self.build_shape_for_objects(objects)

    def build_shape_for_objects(self, objects: list[ZimaEntity]):
        """Build an automatic solid result from an explicit object sequence."""
        if self.body_is_suppressed():
            return None
        if self.document_settings.get("type") == "assembly":
            result_shape = None
            for obj in objects:
                if obj.container_type != ContainerType.COMPONENT:
                    continue
                shape = self.build_assembly_component_shape(obj, objects)
                if shape is None:
                    continue
                result_shape = (
                    shape if result_shape is None
                    else BRepAlgoAPI_Fuse(result_shape, shape).Shape()
                )
            return result_shape
        result_shape = None
        for obj in objects:
            result_shape = apply_object_to_shape(
                result_shape,
                obj,
                identity_transform(),
                document=self,
            )

        return result_shape

    def build_assembly_component_shape(
        self,
        component: ZimaEntity,
        objects: list[ZimaEntity] | None = None,
    ):
        """Evaluate one component with later assembly-only cuts applied."""
        shape = self.build_standalone_shape(component)
        if shape is None:
            return None
        history = self.history_objects() if objects is None else objects
        component_index = next(
            (index for index, obj in enumerate(history) if obj is component),
            -1,
        )
        if component_index < 0:
            return shape
        for feature in history[component_index + 1:]:
            if feature.container_type not in (
                ContainerType.PROTRUSION,
                ContainerType.REVOLVE,
            ):
                continue
            try:
                target_ids = json.loads(
                    str(feature.parameters.get("assembly_target_ids", "[]"))
                )
            except (TypeError, ValueError, json.JSONDecodeError):
                target_ids = []
            if component.entity_id not in target_ids:
                continue
            tool = self.build_standalone_shape(feature)
            if tool is not None:
                shape = BRepAlgoAPI_Cut(shape, tool).Shape()
        return shape

    def build_standalone_shape(self, obj: ZimaEntity):
        """Build one history object for source inspection, ignoring its first sign."""
        return apply_object_to_shape(
            None,
            obj,
            identity_transform(),
            accept_first_shape=True,
            document=self,
        )

    def source_highlight_shapes(self, obj: ZimaEntity) -> list[Any]:
        shape = self.build_standalone_shape(obj)
        shapes = [shape] if shape is not None else []
        sphere = next(
            (
                child for child in obj.children
                if not child.locked and child.kind == EntityKind.SPHERE
            ),
            None,
        )
        cylinder = next(
            (
                child for child in obj.children
                if not child.locked and child.kind == EntityKind.CYLINDER
            ),
            None,
        )
        if sphere is None and cylinder is None:
            return shapes
        shapes = []
        entity = sphere if sphere is not None else cylinder
        world_transform = multiply_transforms(
            coordinate_system_transform(obj.coordinate_system),
            coordinate_system_transform(entity.coordinate_system),
        )
        radius = max(
            0.001,
            float(entity.parameters.get("diameter", 30.0)) / 2.0,
        )
        if sphere is not None:
            circle_frames = (
                (gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                (gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0)),
            )
        else:
            half_height = max(
                0.001,
                float(cylinder.parameters.get("height", 40.0)),
            ) / 2.0
            circle_frames = (
                (gp_Pnt(0.0, 0.0, -half_height), gp_Dir(0.0, 0.0, 1.0)),
                (gp_Pnt(0.0, 0.0, half_height), gp_Dir(0.0, 0.0, 1.0)),
            )
        for center, normal in circle_frames:
            circle = gp_Circ(gp_Ax2(center, normal), radius)
            edge = BRepBuilderAPI_MakeEdge(circle).Edge()
            shapes.append(transform_shape(edge, world_transform))
        if cylinder is not None:
            for x in (-radius, radius):
                edge = BRepBuilderAPI_MakeEdge(
                    gp_Pnt(x, 0.0, -half_height),
                    gp_Pnt(x, 0.0, half_height),
                ).Edge()
                shapes.append(transform_shape(edge, world_transform))
        return shapes

    def build_body_shape(self, body: ZimaEntity):
        """Build the recorded history snapshot represented by a root Body."""
        if body.kind != EntityKind.BODY:
            return None
        source_ids = {
            value
            for value in str(body.parameters.get("source_ids", "")).split(",")
            if value
        }
        result_shape = None
        for item in self.root.children:
            if item.entity_id == body.entity_id:
                break
            if item.kind == EntityKind.ORIGIN or item.entity_id not in source_ids:
                continue
            if item.kind == EntityKind.BODY:
                nested_shape = self.build_body_shape(item)
                if nested_shape is not None:
                    result_shape = (
                        nested_shape
                        if result_shape is None
                        else BRepAlgoAPI_Fuse(result_shape, nested_shape).Shape()
                    )
            else:
                result_shape = apply_object_to_shape(
                    result_shape, item, identity_transform(), document=self
                )
        return result_shape

    def resolve_attachments(self) -> None:
        for obj in self.visible_objects():
            resolve_entity_attachments(self, obj)


def make_component_shape(
    document: PartDocument | None,
    component: ZimaEntity,
):
    """Load the referenced part and return its evaluated body shape."""
    raw_path = str(component.parameters.get("source_path", "")).strip()
    if not raw_path:
        return None
    source_path = Path(raw_path)
    if not source_path.is_absolute():
        base_path = document.source_file_path if document is not None else None
        if base_path is None:
            return None
        source_path = base_path.parent / source_path
    try:
        # Local import avoids coupling the document model to its serializer at
        # module import time.
        from zima_cad.storage import load_part_document

        source_document = load_part_document(source_path.resolve())
        if source_document.document_settings.get("type", "part") != "part":
            return None
        return source_document.build_active_shape()
    except (OSError, ValueError):
        return None


def apply_object_to_shape(
    result_shape,
    obj: ZimaEntity,
    parent_transform: tuple[tuple[float, float, float, float], ...],
    accept_first_shape: bool = False,
    document: PartDocument | None = None,
):
    if obj.suppressed:
        return result_shape
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    feature_type = str(obj.parameters.get("container_type", ""))
    is_protrusion = (
        obj.kind == EntityKind.CONTAINER
        and feature_type == ContainerType.PROTRUSION.value
    )
    is_revolve = (
        obj.kind == EntityKind.CONTAINER
        and feature_type == ContainerType.REVOLVE.value
    )
    is_fillet = (
        obj.kind == EntityKind.CONTAINER
        and feature_type == ContainerType.FILLET.value
    )
    is_component = (
        obj.kind == EntityKind.CONTAINER
        and feature_type == ContainerType.COMPONENT.value
    )
    if is_fillet:
        feature = next((
            child for child in obj.children
            if child.kind == EntityKind.FILLET and not child.locked
        ), None)
        if result_shape is None or feature is None or document is None:
            if feature is not None and not accept_first_shape:
                feature.parameters["build_status"] = "missing_input"
            return result_shape
        reference = parse_edge_reference(feature.parameters.get("edge_ref"))
        history_index = document.history_index(obj.entity_id)
        registry = (
            face_registry_at(document, history_index)
            if history_index is not None else TopologyRegistry()
        )
        registry = _rebind_registry_to_shape(registry, result_shape)
        try:
            radius = float(feature.parameters.get("radius", 1.0))
            if reference is None:
                raise ValueError("Fillet requires a stable edge reference")
            result_shape, _registry = make_fillet_shape(
                result_shape,
                registry,
                reference,
                radius,
                feature.entity_id,
            )
        except (RuntimeError, TypeError, ValueError) as error:
            if not accept_first_shape:
                feature.parameters["build_status"] = str(error)
            return result_shape
        if not accept_first_shape:
            feature.parameters.pop("build_status", None)
        return result_shape
    shape = (
        make_component_shape(document, obj)
        if is_component
        else make_protrusion_shape(document, obj)
        if is_protrusion
        else make_revolve_shape(document, obj)
        if is_revolve
        else make_shape(obj)
    )

    if shape is not None:
        if not is_protrusion and not is_revolve:
            shape = transform_shape(shape, world_transform)
        solid_feature = (
            next(
                (
                    child for child in obj.children
                    if child.kind in (EntityKind.PROTRUSION, EntityKind.REVOLVE)
                    and not child.locked
                ),
                None,
            )
            if is_protrusion or is_revolve else None
        )
        operation = (
            CombineMode(
                str(
                    (
                        solid_feature.parameters
                        if solid_feature is not None
                        else obj.parameters
                    ).get("operation", CombineMode.ADD.value)
                )
            )
            if is_protrusion or is_revolve
            else obj.combine_mode
        )
        status_owner = solid_feature or obj
        record_build_status = not accept_first_shape

        def solid_count(candidate) -> int:
            explorer = TopExp_Explorer(candidate, TopAbs_SOLID)
            solids = []
            while explorer.More():
                solid = explorer.Current()
                if not any(solid.IsSame(existing) for existing in solids):
                    solids.append(solid)
                explorer.Next()
            return len(solids)

        shape_solids = solid_count(shape)
        if operation in (CombineMode.ADD, CombineMode.SUBTRACT) and not shape_solids:
            if record_build_status:
                status_owner.parameters["build_status"] = "not_solid"
            shape = None
        elif record_build_status:
            status_owner.parameters.pop("build_status", None)
        if shape is not None and (operation == CombineMode.ADD or (
            accept_first_shape and result_shape is None
        )):
            if result_shape is None:
                result_shape = shape
            else:
                fused = BRepAlgoAPI_Fuse(result_shape, shape).Shape()
                # An additive Part feature must join the existing body. OCCT
                # otherwise returns a compound of disconnected solids, which
                # looks deceptively like a successful or even surface result.
                if solid_count(fused) == 1:
                    result_shape = fused
                elif record_build_status:
                    status_owner.parameters["build_status"] = "disconnected"
        elif (
            shape is not None
            and operation == CombineMode.SUBTRACT
            and result_shape is not None
        ):
            common = BRepAlgoAPI_Common(result_shape, shape).Shape()
            common_properties = GProp_GProps()
            brepgprop.VolumeProperties(common, common_properties)
            common_volume = abs(float(common_properties.Mass()))
            if common_volume <= 1.0e-9:
                if record_build_status:
                    status_owner.parameters["build_status"] = "no_intersection"
            else:
                cut = BRepAlgoAPI_Cut(result_shape, shape).Shape()
                if solid_count(cut) >= 1:
                    result_shape = cut
                elif record_build_status:
                    status_owner.parameters["build_status"] = "empty_result"

    for child in obj.children:
        if child.locked or child.kind == EntityKind.SKETCH:
            continue
        result_shape = apply_object_to_shape(
            result_shape,
            child,
            world_transform,
            accept_first_shape=accept_first_shape,
            document=document,
        )

    return result_shape


def _occt_ellipse(
    center: tuple[float, float],
    first_axis: tuple[float, float],
    second_axis: tuple[float, float],
) -> tuple[gp_Elips, bool]:
    """Build an exact planar OCCT ellipse and report a reversed Y basis."""
    vectors = (
        (first_axis[0] - center[0], first_axis[1] - center[1]),
        (second_axis[0] - center[0], second_axis[1] - center[1]),
    )
    lengths = tuple(math.hypot(*vector) for vector in vectors)
    if min(lengths) <= 1.0e-12:
        raise ValueError("ellipse axes must be non-zero")
    major_index = 0 if lengths[0] >= lengths[1] else 1
    minor_index = 1 - major_index
    x_vector = vectors[major_index]
    y_vector = vectors[minor_index]
    cross = x_vector[0] * y_vector[1] - x_vector[1] * y_vector[0]
    reversed_y = cross < 0.0
    ellipse = gp_Elips(
        gp_Ax2(
            gp_Pnt(center[0], center[1], 0.0),
            gp_Dir(0.0, 0.0, 1.0),
            gp_Dir(x_vector[0], x_vector[1], 0.0),
        ),
        lengths[major_index],
        lengths[minor_index],
    )
    return ellipse, reversed_y


def _occt_ellipse_parameter(
    ellipse: gp_Elips,
    point: tuple[float, float],
) -> float:
    center = ellipse.Location()
    x_direction = ellipse.XAxis().Direction()
    y_direction = ellipse.YAxis().Direction()
    dx, dy = point[0] - center.X(), point[1] - center.Y()
    cosine = (
        dx * x_direction.X() + dy * x_direction.Y()
    ) / ellipse.MajorRadius()
    sine = (
        dx * y_direction.X() + dy * y_direction.Y()
    ) / ellipse.MinorRadius()
    return math.atan2(sine, cosine)


def _make_exact_ellipse_edge(
    points: list[tuple[float, float]] | tuple[tuple[float, float], ...],
    *,
    arc: bool = False,
    clockwise: bool = False,
):
    ellipse, reversed_y = _occt_ellipse(points[0], points[1], points[2])
    if not arc:
        return BRepBuilderAPI_MakeEdge(ellipse).Edge()
    first = _occt_ellipse_parameter(ellipse, points[3])
    last = _occt_ellipse_parameter(ellipse, points[4])
    sense = not clockwise
    if reversed_y:
        sense = not sense
    curve = GC_MakeArcOfEllipse(ellipse, first, last, sense).Value()
    return BRepBuilderAPI_MakeEdge(curve).Edge()


def _make_sketch_profile_faces(sketch: ZimaEntity):
    """Build planar faces for every closed non-construction sketch loop."""
    wires = []
    if sketch.parameters.get("profile") == "circle":
        radius = max(1.0e-9, float(sketch.parameters.get("diameter", 10.0)) / 2.0)
        edge = BRepBuilderAPI_MakeEdge(
            gp_Circ(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), radius)
        ).Edge()
        wires.append(BRepBuilderAPI_MakeWire(edge).Wire())
    elif sketch.parameters.get("profile") == "entities":
        try:
            model = SketchModel.from_dict(
                json.loads(str(sketch.parameters.get("sketch_data", "{}")))
            )
            entities, _dimensions = model.to_editor_data()
        except (TypeError, ValueError, json.JSONDecodeError, SketchModelError):
            return None
        points = {
            str(item.get("id")): (
                float(item.get("x", 0.0)),
                float(item.get("y", 0.0)),
            )
            for item in entities
            if isinstance(item, dict) and item.get("type") == "point"
        }
        for item in entities:
            if not isinstance(item, dict) or item.get("role") == "construction":
                continue
            if item.get("type") == "circle":
                center_ids = item.get("point_ids", ())
                if len(center_ids) == 1 and str(center_ids[0]) in points:
                    cx, cy = points[str(center_ids[0])]
                    radius = float(item.get("radius", 0.0))
                    if radius > 1.0e-9:
                        edge = BRepBuilderAPI_MakeEdge(
                            gp_Circ(
                                gp_Ax2(gp_Pnt(cx, cy, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                                radius,
                            )
                        ).Edge()
                        wires.append(BRepBuilderAPI_MakeWire(edge).Wire())
            elif item.get("type") == "ellipse":
                point_ids = tuple(map(str, item.get("point_ids", ())))
                if len(point_ids) == 3 and all(pid in points for pid in point_ids):
                    try:
                        edge = _make_exact_ellipse_edge(
                            tuple(points[pid] for pid in point_ids)
                        )
                        wires.append(BRepBuilderAPI_MakeWire(edge).Wire())
                    except (RuntimeError, ValueError):
                        continue
        segment_entities = {
            str(item.get("id", "")): item
            for item in entities
            if isinstance(item, dict)
            and item.get("type") == "segment"
            and item.get("role") != "construction"
            and len(item.get("point_ids", ())) == 2
        }
        trim_points: dict[tuple[str, str], tuple[float, float]] = {}
        corner_arcs: dict[
            tuple[str, str, str],
            tuple[tuple[float, float], ...],
        ] = {}
        for first_id, first_geometry in segment_entities.items():
            first_points = tuple(map(str, first_geometry.get("point_ids", ())))
            for record in first_geometry.get("corner_radii", ()):
                if not isinstance(record, dict):
                    continue
                second_id = str(record.get("other_geometry_id", ""))
                vertex_id = str(record.get("vertex_id", ""))
                second_geometry = segment_entities.get(second_id)
                if second_geometry is None:
                    continue
                second_points = tuple(
                    map(str, second_geometry.get("point_ids", ()))
                )
                if (
                    vertex_id not in first_points
                    or vertex_id not in second_points
                    or vertex_id not in points
                ):
                    continue
                first_outer_id = next(
                    point_id for point_id in first_points
                    if point_id != vertex_id
                )
                second_outer_id = next(
                    point_id for point_id in second_points
                    if point_id != vertex_id
                )
                evaluated = evaluate_corner_radius(
                    points[vertex_id],
                    points[first_outer_id],
                    points[second_outer_id],
                    float(record.get("radius", 0.0)),
                )
                if evaluated is None:
                    continue
                trim_points[(first_id, vertex_id)] = evaluated.first_tangent
                trim_points[(second_id, vertex_id)] = evaluated.second_tangent
                corner_arcs[(vertex_id, first_id, second_id)] = (
                    evaluated.arc_points
                )

        profile_entities: dict[
            str,
            tuple[dict[str, Any], str, str],
        ] = {}
        for item in entities:
            if (
                not isinstance(item, dict)
                or item.get("role") == "construction"
            ):
                continue
            geometry_id = str(item.get("id", ""))
            point_ids = tuple(map(str, item.get("point_ids", ())))
            entity_type = str(item.get("type", ""))
            endpoints: tuple[str, str] | None = None
            if entity_type == "segment" and len(point_ids) == 2:
                endpoints = point_ids
            elif entity_type == "spline" and len(point_ids) >= 2:
                endpoints = (point_ids[0], point_ids[-1])
            elif entity_type == "arc" and len(point_ids) >= 3:
                endpoints = (
                    (point_ids[1], point_ids[2])
                    if item.get("arc_mode") == "center"
                    else (point_ids[0], point_ids[-1])
                )
            elif entity_type == "elliptical_arc" and len(point_ids) >= 5:
                endpoints = (point_ids[3], point_ids[4])
            if geometry_id and endpoints is not None:
                profile_entities[geometry_id] = (
                    item,
                    endpoints[0],
                    endpoints[1],
                )

        unused = [
            (geometry_id, start_id, end_id)
            for geometry_id, (_geometry, start_id, end_id)
            in profile_entities.items()
        ]
        while unused:
            first_id, first_point, second_point = unused.pop(0)
            chain_points = [first_point, second_point]
            chain_geometry = [first_id]
            while chain_points[-1] != chain_points[0]:
                match_index = next(
                    (
                        index for index, (_segment_id, start_id, end_id)
                        in enumerate(unused)
                        if chain_points[-1] in (start_id, end_id)
                    ),
                    None,
                )
                if match_index is None:
                    break
                geometry_id, start_id, end_id = unused.pop(match_index)
                chain_geometry.append(geometry_id)
                chain_points.append(
                    end_id if start_id == chain_points[-1] else start_id
                )
            if len(chain_points) < 2 or chain_points[-1] != chain_points[0]:
                continue
            wire_builder = BRepBuilderAPI_MakeWire()
            valid = True
            geometry_count = len(chain_geometry)
            for index, geometry_id in enumerate(chain_geometry):
                start_id = chain_points[index]
                end_id = chain_points[index + 1]
                geometry = profile_entities[geometry_id][0]
                entity_type = str(geometry.get("type", ""))
                point_ids = tuple(
                    map(str, geometry.get("point_ids", ()))
                )
                start = trim_points.get(
                    (geometry_id, start_id),
                    points.get(start_id),
                )
                end = trim_points.get(
                    (geometry_id, end_id),
                    points.get(end_id),
                )
                if start is None or end is None:
                    valid = False
                    break
                try:
                    if entity_type == "segment":
                        edge = BRepBuilderAPI_MakeEdge(
                            gp_Pnt(*start, 0.0),
                            gp_Pnt(*end, 0.0),
                        ).Edge()
                    elif entity_type == "arc":
                        if geometry.get("arc_mode") == "center":
                            sampled = center_arc_points(
                                points[point_ids[0]],
                                points[point_ids[1]],
                                points[point_ids[2]],
                                segments=96,
                                clockwise=bool(
                                    geometry.get("clockwise", False)
                                ),
                            )
                            if start_id != point_ids[1]:
                                sampled = tuple(reversed(sampled))
                        else:
                            sampled = tuple(
                                points[point_id] for point_id in point_ids
                            )
                            if start_id != point_ids[0]:
                                sampled = tuple(reversed(sampled))
                        if len(sampled) < 3:
                            valid = False
                            break
                        edge = BRepBuilderAPI_MakeEdge(
                            GC_MakeArcOfCircle(
                                gp_Pnt(*sampled[0], 0.0),
                                gp_Pnt(
                                    *sampled[len(sampled) // 2],
                                    0.0,
                                ),
                                gp_Pnt(*sampled[-1], 0.0),
                            ).Value()
                        ).Edge()
                    elif entity_type == "spline":
                        ordered_ids = point_ids
                        if start_id != point_ids[0]:
                            ordered_ids = tuple(reversed(ordered_ids))
                        periodic = (
                            len(ordered_ids) >= 4
                            and ordered_ids[0] == ordered_ids[-1]
                        )
                        if periodic:
                            ordered_ids = ordered_ids[:-1]
                        poles = TColgp_HArray1OfPnt(1, len(ordered_ids))
                        for pole_index, point_id in enumerate(
                            ordered_ids,
                            1,
                        ):
                            poles.SetValue(
                                pole_index,
                                gp_Pnt(*points[point_id], 0.0),
                            )
                        interpolation = GeomAPI_Interpolate(
                            poles,
                            periodic,
                            1.0e-7,
                        )
                        interpolation.Perform()
                        if not interpolation.IsDone():
                            valid = False
                            break
                        edge = BRepBuilderAPI_MakeEdge(
                            interpolation.Curve()
                        ).Edge()
                    elif entity_type == "elliptical_arc":
                        edge = _make_exact_ellipse_edge(
                            tuple(points[pid] for pid in point_ids[:5]),
                            arc=True,
                            clockwise=(
                                bool(geometry.get("clockwise", False))
                                if start_id == point_ids[3]
                                else not bool(geometry.get("clockwise", False))
                            ),
                        )
                    else:
                        valid = False
                        break
                    wire_builder.Add(edge)
                except (RuntimeError, TypeError, ValueError, KeyError):
                    valid = False
                    break
                outgoing_id = chain_geometry[(index + 1) % geometry_count]
                arc_points = (
                    corner_arcs.get((end_id, geometry_id, outgoing_id))
                    or corner_arcs.get((end_id, outgoing_id, geometry_id))
                )
                if arc_points:
                    outgoing_start = trim_points.get(
                        (outgoing_id, end_id)
                    )
                    if outgoing_start is None:
                        continue
                    ordered = arc_points
                    distance_to_end = math.dist(ordered[0], end)
                    distance_to_outgoing = math.dist(ordered[0], outgoing_start)
                    if distance_to_outgoing < distance_to_end:
                        ordered = tuple(reversed(ordered))
                    wire_builder.Add(
                        BRepBuilderAPI_MakeEdge(
                            GC_MakeArcOfCircle(
                                gp_Pnt(*ordered[0], 0.0),
                                gp_Pnt(*ordered[len(ordered) // 2], 0.0),
                                gp_Pnt(*ordered[-1], 0.0),
                            ).Value()
                        ).Edge()
                    )
            if valid and wire_builder.IsDone():
                wires.append(wire_builder.Wire())
    if not wires:
        return None

    faces = []
    for wire in wires:
        try:
            face = BRepBuilderAPI_MakeFace(wire).Face()
        except (RuntimeError, ValueError):
            # A zero-length edge can survive in a damaged/temporarily
            # under-constrained sketch.  Keep the document and Sketcher
            # usable so the profile can be repaired instead of aborting the
            # entire view rebuild in OpenCASCADE.
            continue
        if not face.IsNull():
            faces.append(face)
    return faces


def make_protrusion_shape(document: PartDocument | None, obj: ZimaEntity):
    """Build a straight extrusion from the closed profile of a referenced sketch."""
    if document is None:
        return None
    feature = next(
        (
            child for child in obj.children
            if child.kind == EntityKind.PROTRUSION and not child.locked
        ),
        None,
    )
    parameters = feature.parameters if feature is not None else obj.parameters
    sketch = document.find_entity(str(parameters.get("sketch_id", "")))
    if sketch is None or sketch.kind != EntityKind.SKETCH:
        return None
    faces = _make_sketch_profile_faces(sketch)
    if not faces:
        return None

    forward = max(0.0, float(parameters.get("length_forward", parameters.get("length", 10.0))))
    reverse = max(0.0, float(parameters.get("length_reverse", 0.0)))
    extent_mode = str(parameters.get("extent_mode", "one_side"))
    if extent_mode == "symmetric":
        reverse = forward
    elif extent_mode == "one_side":
        if str(parameters.get("direction", "forward")) == "reverse":
            reverse, forward = forward, 0.0
        else:
            reverse = 0.0
    length = forward + reverse
    if length <= 1.0e-9:
        return None
    start = -reverse
    # An external sketch lends its 2D geometry, not its placement.  The
    # Protrusion container's plane/reference frame owns the resulting feature.
    plane = str(sketch.parameters.get("plane", "xz"))
    profile_offset = float(parameters.get("profile_offset", 0.0))
    plane_transform = multiply_transforms(
        sketch_plane_offset_transform(plane, profile_offset),
        sketch_plane_transform(plane),
    )
    extrusion_direction = {
        "xy": (0.0, 0.0, 1.0),
        "xz": (0.0, 1.0, 0.0),
        "yz": (1.0, 0.0, 0.0),
    }.get(plane, (0.0, 1.0, 0.0))
    profile_transform = coordinate_system_transform(obj.coordinate_system)
    translated = multiply_transforms(
        profile_transform,
        (
            (1.0, 0.0, 0.0, extrusion_direction[0] * start),
            (0.0, 1.0, 0.0, extrusion_direction[1] * start),
            (0.0, 0.0, 1.0, extrusion_direction[2] * start),
            (0.0, 0.0, 0.0, 1.0),
        ),
    )
    solids = []
    for face in faces:
        embedded_face = transform_shape(face, plane_transform)
        local = BRepPrimAPI_MakePrism(
            embedded_face,
            gp_Vec(*(value * length for value in extrusion_direction)),
        ).Shape()
        solids.append(transform_shape(local, translated))
    def solid_volume(shape) -> float:
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        return abs(float(properties.Mass()))

    profiled_solids = sorted(
        ((solid_volume(solid), solid) for solid in solids),
        key=lambda item: item[0],
        reverse=True,
    )
    result = None
    for index, (volume, solid) in enumerate(profiled_solids):
        nesting_depth = 0
        if volume > 1.0e-12:
            for outer_volume, outer_solid in profiled_solids[:index]:
                if outer_volume <= volume:
                    continue
                common = BRepAlgoAPI_Common(outer_solid, solid).Shape()
                common_volume = solid_volume(common)
                if common_volume >= volume - max(volume * 1.0e-7, 1.0e-9):
                    nesting_depth += 1
        if result is None:
            result = solid
        elif nesting_depth % 2:
            result = BRepAlgoAPI_Cut(result, solid).Shape()
        else:
            result = BRepAlgoAPI_Fuse(result, solid).Shape()
    return result


def make_revolve_shape(document: PartDocument | None, obj: ZimaEntity):
    """Revolve a closed sketch profile around its first construction line."""
    if document is None:
        return None
    feature = next(
        (
            child for child in obj.children
            if child.kind == EntityKind.REVOLVE and not child.locked
        ),
        None,
    )
    parameters = feature.parameters if feature is not None else obj.parameters
    sketch = document.find_entity(str(parameters.get("sketch_id", "")))
    if sketch is None or sketch.kind != EntityKind.SKETCH:
        return None
    faces = _make_sketch_profile_faces(sketch)
    if not faces:
        return None
    try:
        sketch_model = SketchModel.from_dict(
            json.loads(str(sketch.parameters.get("sketch_data", "{}")))
        )
        entities, _dimensions = sketch_model.to_editor_data()
    except (TypeError, ValueError, json.JSONDecodeError, SketchModelError):
        return None
    points = {
        str(entity.get("id", "")): (
            float(entity.get("x", 0.0)),
            float(entity.get("y", 0.0)),
        )
        for entity in entities
        if isinstance(entity, dict) and entity.get("type") == "point"
    }
    axis_geometry = next(
        (
            entity for entity in entities
            if isinstance(entity, dict)
            and entity.get("type") == "construction"
            and len(entity.get("point_ids", ())) == 2
        ),
        None,
    )
    if axis_geometry is None:
        return None
    axis_ids = tuple(map(str, axis_geometry.get("point_ids", ())))
    if any(point_id not in points for point_id in axis_ids):
        return None
    first = points[axis_ids[0]]
    second = points[axis_ids[1]]
    direction = (second[0] - first[0], second[1] - first[1])
    if math.hypot(*direction) <= 1.0e-9:
        return None
    angle = max(1.0e-6, min(360.0, float(parameters.get("angle", 360.0))))
    reverse_angle = max(
        1.0e-6,
        min(360.0, float(parameters.get("angle_reverse", angle))),
    )
    extent_mode = str(parameters.get("extent_mode", "one_side"))
    rotation_direction = str(parameters.get("direction", "forward"))
    if extent_mode == "symmetric":
        half_angle = min(angle, 180.0)
        signed_angles = (half_angle, -half_angle)
    elif extent_mode == "two_sides":
        reverse_angle = min(reverse_angle, max(0.0, 360.0 - angle))
        signed_angles = (
            (angle, -reverse_angle) if reverse_angle > 1.0e-6 else (angle,)
        )
    else:
        signed_angles = (
            (-angle,) if rotation_direction == "reverse" else (angle,)
        )
    plane = str(sketch.parameters.get("plane", "xz"))
    profile_offset = float(parameters.get("profile_offset", 0.0))
    plane_transform = multiply_transforms(
        sketch_plane_offset_transform(plane, profile_offset),
        sketch_plane_transform(plane),
    )
    axis_point = transform_point(
        plane_transform,
        (first[0], first[1], 0.0),
    )
    axis_end = transform_point(
        plane_transform,
        (first[0] + direction[0], first[1] + direction[1], 0.0),
    )
    axis_direction = tuple(
        axis_end[index] - axis_point[index]
        for index in range(3)
    )
    axis = gp_Ax1(gp_Pnt(*axis_point), gp_Dir(*axis_direction))
    local_solids = []
    try:
        for signed_angle in signed_angles:
            for face in faces:
                embedded_face = transform_shape(face, plane_transform)
                local_solids.append(
                    BRepPrimAPI_MakeRevol(
                        embedded_face,
                        axis,
                        math.radians(signed_angle),
                        True,
                    ).Shape()
                )
    except (RuntimeError, TypeError, ValueError):
        return None

    def solid_volume(shape) -> float:
        properties = GProp_GProps()
        brepgprop.VolumeProperties(shape, properties)
        return abs(float(properties.Mass()))

    profiled_solids = sorted(
        ((solid_volume(solid), solid) for solid in local_solids),
        key=lambda item: item[0],
        reverse=True,
    )
    result = None
    for index, (volume, solid) in enumerate(profiled_solids):
        nesting_depth = 0
        if volume > 1.0e-12:
            for outer_volume, outer_solid in profiled_solids[:index]:
                if outer_volume <= volume:
                    continue
                common = BRepAlgoAPI_Common(outer_solid, solid).Shape()
                common_volume = solid_volume(common)
                if common_volume >= volume - max(
                    volume * 1.0e-7,
                    1.0e-9,
                ):
                    nesting_depth += 1
        if result is None:
            result = solid
        elif nesting_depth % 2:
            result = BRepAlgoAPI_Cut(result, solid).Shape()
        else:
            result = BRepAlgoAPI_Fuse(result, solid).Shape()
    return (
        transform_shape(result, coordinate_system_transform(obj.coordinate_system))
        if result is not None
        else None
    )


def make_shape(obj: ZimaEntity):
    x, y, z = (0.0, 0.0, 0.0)

    if obj.kind == EntityKind.BOX:
        length = float(obj.parameters.get("length", 100.0))
        width = float(obj.parameters.get("width", 60.0))
        height = float(obj.parameters.get("height", 20.0))
        return BRepPrimAPI_MakeBox(
            gp_Pnt(
                x - length / 2.0,
                y - width / 2.0,
                z - height / 2.0,
            ),
            length,
            width,
            height,
        ).Shape()

    if obj.kind == EntityKind.SPHERE:
        diameter = float(obj.parameters.get("diameter", 30.0))
        return BRepPrimAPI_MakeSphere(
            gp_Pnt(x, y, z),
            max(0.001, diameter / 2.0),
        ).Shape()

    if obj.kind == EntityKind.CYLINDER:
        diameter = float(obj.parameters.get("diameter", 20.0))
        height = float(obj.parameters.get("height", 40.0))
        axis = gp_Ax2(
            gp_Pnt(x, y, z - height / 2.0),
            gp_Dir(0.0, 0.0, 1.0),
        )
        return BRepPrimAPI_MakeCylinder(axis, diameter / 2.0, height).Shape()

    if obj.kind == EntityKind.CONE:
        bottom_diameter = float(obj.parameters.get("bottom_diameter", 40.0))
        top_diameter = float(obj.parameters.get("top_diameter", 0.0))
        height = float(obj.parameters.get("height", 50.0))
        bottom_radius = max(0.0, bottom_diameter / 2.0)
        top_radius = max(0.0, top_diameter / 2.0)
        if abs(bottom_radius - top_radius) <= 1e-9:
            return BRepPrimAPI_MakeCylinder(
                bottom_radius,
                max(0.001, height),
            ).Shape()
        return BRepPrimAPI_MakeCone(
            bottom_radius,
            top_radius,
            max(0.001, height),
        ).Shape()

    if obj.kind == EntityKind.PYRAMID:
        length = max(0.001, float(obj.parameters.get("length", 40.0)))
        width = max(0.001, float(obj.parameters.get("width", 40.0)))
        height = max(0.001, float(obj.parameters.get("height", 50.0)))
        base = BRepBuilderAPI_MakePolygon()
        for point in (
            gp_Pnt(x - length / 2.0, y - width / 2.0, z),
            gp_Pnt(x + length / 2.0, y - width / 2.0, z),
            gp_Pnt(x + length / 2.0, y + width / 2.0, z),
            gp_Pnt(x - length / 2.0, y + width / 2.0, z),
        ):
            base.Add(point)
        base.Close()
        builder = BRepOffsetAPI_ThruSections(True, True)
        builder.AddWire(base.Wire())
        builder.AddVertex(
            BRepBuilderAPI_MakeVertex(
                gp_Pnt(x, y, z + height)
            ).Vertex()
        )
        builder.Build()
        return builder.Shape()

    if obj.kind == EntityKind.WEDGE:
        length = float(obj.parameters.get("length", 100.0))
        width = float(obj.parameters.get("width", 60.0))
        height = float(obj.parameters.get("height", 50.0))
        top_offset = float(obj.parameters.get("top_offset", 50.0))
        top_offset = max(0.0, min(length, top_offset))
        shape = BRepPrimAPI_MakeWedge(
            length,
            width,
            height,
            top_offset,
        ).Shape()
        translation = gp_Trsf()
        translation.SetTranslation(
            gp_Vec(-length / 2.0, -width / 2.0, 0.0)
        )
        return BRepBuilderAPI_Transform(shape, translation, True).Shape()

    return None


def identity_transform() -> tuple[tuple[float, float, float, float], ...]:
    return (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
    )


def sketch_plane_transform(
    plane: str,
) -> tuple[tuple[float, float, float, float], ...]:
    """Map 2D sketch coordinates (u, v, 0) into its container plane."""
    return {
        "xy": identity_transform(),
        "xz": (
            (1.0, 0.0, 0.0, 0.0),
            (0.0, 0.0, 1.0, 0.0),
            (0.0, 1.0, 0.0, 0.0),
        ),
        "yz": (
            (0.0, 0.0, 1.0, 0.0),
            (1.0, 0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0, 0.0),
        ),
    }.get(plane, identity_transform())


def sketch_plane_offset_transform(
    plane: str,
    offset: float,
) -> tuple[tuple[float, float, float, float], ...]:
    origin = {
        "xy": (0.0, 0.0, offset),
        "yz": (offset, 0.0, 0.0),
        "xz": (0.0, offset, 0.0),
    }.get(plane, (0.0, 0.0, offset))
    return coordinate_system_transform(CoordinateSystem(origin=origin))


def coordinate_system_transform(
    coordinate_system: CoordinateSystem,
) -> tuple[tuple[float, float, float, float], ...]:
    rx, ry, rz = (math.radians(value) for value in coordinate_system.rotation)
    sx, cx = math.sin(rx), math.cos(rx)
    sy, cy = math.sin(ry), math.cos(ry)
    sz, cz = math.sin(rz), math.cos(rz)

    rx_matrix = (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, cx, -sx, 0.0),
        (0.0, sx, cx, 0.0),
    )
    ry_matrix = (
        (cy, 0.0, sy, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (-sy, 0.0, cy, 0.0),
    )
    rz_matrix = (
        (cz, -sz, 0.0, 0.0),
        (sz, cz, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
    )
    rotation = multiply_transforms(rz_matrix, multiply_transforms(ry_matrix, rx_matrix))
    x, y, z = coordinate_system.origin
    return (
        (rotation[0][0], rotation[0][1], rotation[0][2], x),
        (rotation[1][0], rotation[1][1], rotation[1][2], y),
        (rotation[2][0], rotation[2][1], rotation[2][2], z),
    )


def multiply_transforms(
    first: tuple[tuple[float, float, float, float], ...],
    second: tuple[tuple[float, float, float, float], ...],
) -> tuple[tuple[float, float, float, float], ...]:
    rows = []
    for row in range(3):
        values = []
        for column in range(3):
            values.append(
                sum(first[row][index] * second[index][column] for index in range(3))
            )
        values.append(
            first[row][3]
            + sum(first[row][index] * second[index][3] for index in range(3))
        )
        rows.append(tuple(values))
    return tuple(rows)


def transform_point(
    transform: tuple[tuple[float, float, float, float], ...],
    point: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (
        transform[0][0] * point[0]
        + transform[0][1] * point[1]
        + transform[0][2] * point[2]
        + transform[0][3],
        transform[1][0] * point[0]
        + transform[1][1] * point[1]
        + transform[1][2] * point[2]
        + transform[1][3],
        transform[2][0] * point[0]
        + transform[2][1] * point[1]
        + transform[2][2] * point[2]
        + transform[2][3],
    )


def transform_vector(
    transform: tuple[tuple[float, float, float, float], ...],
    vector: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (
        sum(transform[0][index] * vector[index] for index in range(3)),
        sum(transform[1][index] * vector[index] for index in range(3)),
        sum(transform[2][index] * vector[index] for index in range(3)),
    )


def vector_dot(first, second) -> float:
    return sum(first[index] * second[index] for index in range(3))


def vector_cross(first, second) -> tuple[float, float, float]:
    return (
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0],
    )


def normalized(vector) -> tuple[float, float, float] | None:
    length = math.sqrt(vector_dot(vector, vector))
    if length <= 1.0e-12:
        return None
    return tuple(value / length for value in vector)


def entity_world_transform(
    document: PartDocument,
    entity_id: str,
) -> tuple[tuple[float, float, float, float], ...] | None:
    def visit(obj: ZimaEntity, parent_transform):
        world_transform = multiply_transforms(
            parent_transform,
            coordinate_system_transform(obj.coordinate_system),
        )
        if obj.entity_id == entity_id:
            return world_transform
        for child in obj.children:
            found = visit(child, world_transform)
            if found is not None:
                return found
        return None

    for obj in document.visible_objects():
        found = visit(obj, identity_transform())
        if found is not None:
            return found
    return None


def box_face_frame(
    document: PartDocument,
    box: ZimaEntity,
    role: str,
) -> tuple[tuple[float, float, float], tuple[float, float, float]] | None:
    if box.kind != EntityKind.BOX:
        return None
    length = float(box.parameters.get("length", 100.0))
    width = float(box.parameters.get("width", 60.0))
    height = float(box.parameters.get("height", 20.0))
    local_frames = {
        "x_min": ((0.0, width / 2.0, height / 2.0), (-1.0, 0.0, 0.0)),
        "x_max": ((length, width / 2.0, height / 2.0), (1.0, 0.0, 0.0)),
        "y_min": ((length / 2.0, 0.0, height / 2.0), (0.0, -1.0, 0.0)),
        "y_max": ((length / 2.0, width, height / 2.0), (0.0, 1.0, 0.0)),
        "z_min": ((length / 2.0, width / 2.0, 0.0), (0.0, 0.0, -1.0)),
        "z_max": ((length / 2.0, width / 2.0, height), (0.0, 0.0, 1.0)),
    }
    local_frame = local_frames.get(role)
    world_transform = entity_world_transform(document, box.entity_id)
    if local_frame is None or world_transform is None:
        return None
    point = transform_point(world_transform, local_frame[0])
    normal = normalized(transform_vector(world_transform, local_frame[1]))
    return (point, normal) if normal is not None else None


def wedge_face_frame(
    document: PartDocument,
    wedge: ZimaEntity,
    role: str,
) -> tuple[tuple[float, float, float], tuple[float, float, float]] | None:
    if wedge.kind != EntityKind.WEDGE:
        return None
    length = float(wedge.parameters.get("length", 100.0))
    width = float(wedge.parameters.get("width", 60.0))
    height = float(wedge.parameters.get("height", 50.0))
    top_offset = max(
        0.0,
        min(length, float(wedge.parameters.get("top_offset", 50.0))),
    )
    slope_normal = normalized((width, length - top_offset, 0.0))
    if slope_normal is None:
        return None
    local_frames = {
        "x_min": ((0.0, width / 2.0, height / 2.0), (-1.0, 0.0, 0.0)),
        "slope": ((length, 0.0, height / 2.0), slope_normal),
        "y_min": ((length / 2.0, 0.0, height / 2.0), (0.0, -1.0, 0.0)),
        "y_max": ((top_offset / 2.0, width, height / 2.0), (0.0, 1.0, 0.0)),
        "z_min": ((0.0, 0.0, 0.0), (0.0, 0.0, -1.0)),
        "z_max": ((0.0, 0.0, height), (0.0, 0.0, 1.0)),
    }
    local_frame = local_frames.get(role)
    world_transform = entity_world_transform(document, wedge.entity_id)
    if local_frame is None or world_transform is None:
        return None
    point = transform_point(world_transform, local_frame[0])
    normal = normalized(transform_vector(world_transform, local_frame[1]))
    return (point, normal) if normal is not None else None


def solid_face_frames(
    document: PartDocument,
    solid: ZimaEntity,
) -> dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]]:
    roles = (
        ("x_min", "x_max", "y_min", "y_max", "z_min", "z_max")
        if solid.kind == EntityKind.BOX
        else ("x_min", "slope", "y_min", "y_max", "z_min", "z_max")
        if solid.kind == EntityKind.WEDGE
        else ()
    )
    frame_function = box_face_frame if solid.kind == EntityKind.BOX else wedge_face_frame
    frames = {}
    for role in roles:
        frame = frame_function(document, solid, role)
        if frame is not None:
            frames[role] = frame
    return frames


def semantic_face_registry(
    document: PartDocument,
    solid: ZimaEntity,
    shape,
) -> TopologyRegistry:
    """Name supported primitive faces by their modeling role, never by order."""

    registry = TopologyRegistry()
    frames = solid_face_frames(document, solid)
    if not frames or shape is None:
        return registry
    explorer = TopExp_Explorer(shape, TopAbs_FACE)
    runtime_index = 0
    while explorer.More():
        runtime_index += 1
        face = explorer.Current()
        adaptor = BRepAdaptor_Surface(face)
        if adaptor.GetType() != GeomAbs_Plane:
            explorer.Next()
            continue
        direction = adaptor.Plane().Axis().Direction()
        sign = -1.0 if face.Orientation() == TopAbs_REVERSED else 1.0
        normal = (
            sign * direction.X(),
            sign * direction.Y(),
            sign * direction.Z(),
        )
        ranked = sorted(
            (
                vector_dot(normal, frame_normal),
                role,
            )
            for role, (_point, frame_normal) in frames.items()
        )
        if not ranked or ranked[-1][0] < 1.0 - 1.0e-7:
            explorer.Next()
            continue
        role = ranked[-1][1]
        registry.register_face(
            FaceRef(feature_id=solid.entity_id, role=role),
            face,
            runtime_index=runtime_index,
        )
        explorer.Next()
    # Primitive edges and vertices are defined by the semantic faces meeting
    # there.  This avoids inheriting OCCT's traversal order as persistent ID.
    faces = _unique_subshapes(shape, TopAbs_FACE)
    edges = _unique_subshapes(shape, TopAbs_EDGE)
    vertices = _unique_subshapes(shape, TopAbs_VERTEX)
    face_refs = {
        index: reference
        for index, face in enumerate(faces)
        if (
            reference := next((
                candidate
                for candidate, registered in registry.face_entries
                if any(face.IsSame(item) for item in registered)
            ), None)
        ) is not None
    }
    adjacent_faces: dict[int, set[FaceRef]] = {}
    incident_faces: dict[int, set[FaceRef]] = {}
    for face_index, face in enumerate(faces):
        reference = face_refs.get(face_index)
        if reference is None:
            continue
        edge_explorer = TopExp_Explorer(face, TopAbs_EDGE)
        while edge_explorer.More():
            edge = edge_explorer.Current()
            for edge_index, candidate in enumerate(edges):
                if edge.IsSame(candidate):
                    adjacent_faces.setdefault(edge_index, set()).add(reference)
                    break
            edge_explorer.Next()
        vertex_explorer = TopExp_Explorer(face, TopAbs_VERTEX)
        while vertex_explorer.More():
            vertex = vertex_explorer.Current()
            for vertex_index, candidate in enumerate(vertices):
                if vertex.IsSame(candidate):
                    incident_faces.setdefault(vertex_index, set()).add(reference)
                    break
            vertex_explorer.Next()
    for edge_index, references in adjacent_faces.items():
        if len(references) == 2:
            registry.register_edge(
                EdgeRef(
                    solid.entity_id,
                    "boundary",
                    semantic_provenance_id(*references),
                ),
                edges[edge_index],
                runtime_index=edge_index + 1,
            )
    for vertex_index, references in incident_faces.items():
        if len(references) == 3:
            registry.register_vertex(
                VertexRef(
                    solid.entity_id,
                    "boundary",
                    semantic_provenance_id(*references),
                ),
                vertices[vertex_index],
                runtime_index=vertex_index + 1,
            )
    return registry


def protrusion_face_registry(
    document: PartDocument,
    container: ZimaEntity,
    shape,
) -> TopologyRegistry:
    """Name the two cap faces of a straight extrusion by feature provenance."""

    registry = TopologyRegistry()
    if container.container_type != ContainerType.PROTRUSION or shape is None:
        return registry
    feature = next(
        (
            child
            for child in container.children
            if child.kind == EntityKind.PROTRUSION and not child.locked
        ),
        None,
    )
    if feature is None:
        return registry
    sketch = document.find_entity(str(feature.parameters.get("sketch_id", "")))
    if sketch is None or sketch.kind != EntityKind.SKETCH:
        return registry
    local_direction = {
        "xy": (0.0, 0.0, 1.0),
        "xz": (0.0, 1.0, 0.0),
        "yz": (1.0, 0.0, 0.0),
    }.get(str(sketch.parameters.get("plane", "xz")), (0.0, 1.0, 0.0))
    world_transform = entity_world_transform(document, container.entity_id)
    if world_transform is None:
        return registry
    extrusion_direction = normalized(
        transform_vector(world_transform, local_direction)
    )
    if extrusion_direction is None:
        return registry
    try:
        sketch_model = SketchModel.from_dict(
            json.loads(str(sketch.parameters.get("sketch_data", "{}")))
        )
        sketch_entities, _dimensions = sketch_model.to_editor_data()
    except (TypeError, ValueError, json.JSONDecodeError, SketchModelError):
        sketch_entities = []
    circular_source_id = (
        sketch.entity_id
        if sketch.parameters.get("profile") == "circle"
        else None
    )
    sketch_points = {
        str(entity.get("id", "")): (
            float(entity.get("x", 0.0)),
            float(entity.get("y", 0.0)),
        )
        for entity in sketch_entities
        if isinstance(entity, dict) and entity.get("type") == "point"
    }
    plane = str(sketch.parameters.get("plane", "xz"))
    plane_transform = multiply_transforms(
        sketch_plane_offset_transform(
            plane,
            float(feature.parameters.get("profile_offset", 0.0)),
        ),
        sketch_plane_transform(plane),
    )
    forward = max(
        0.0,
        float(feature.parameters.get(
            "length_forward",
            feature.parameters.get("length", 10.0),
        )),
    )
    reverse = max(0.0, float(feature.parameters.get("length_reverse", 0.0)))
    extent_mode = str(feature.parameters.get("extent_mode", "one_side"))
    if extent_mode == "symmetric":
        reverse = forward
    elif extent_mode == "one_side":
        if str(feature.parameters.get("direction", "forward")) == "reverse":
            reverse, forward = forward, 0.0
        else:
            reverse = 0.0
    start = -reverse
    end = forward
    point_positions: dict[str, dict[str, tuple[float, float, float]]] = {}
    profile_origin = transform_point(
        world_transform,
        transform_point(plane_transform, (0.0, 0.0, 0.0)),
    )
    for point_id, point_2d in sketch_points.items():
        base = transform_point(
            world_transform,
            transform_point(plane_transform, (*point_2d, 0.0)),
        )
        point_positions[point_id] = {
            role: tuple(
                base[index] + extrusion_direction[index] * distance
                for index in range(3)
            )
            for role, distance in (("start", start), ("end", end))
        }
    curve_sources: list[tuple[str, tuple[float, float, float]]] = []
    curve_point_ids: dict[str, tuple[str, str]] = {}
    curve_midpoints: dict[str, tuple[float, float, float]] = {}
    closed_curve_seam_ids: set[str] = set()
    for entity in sketch_entities:
        if not isinstance(entity, dict) or entity.get("type") not in (
            "segment", "arc", "spline", "ellipse", "elliptical_arc"
        ):
            continue
        if entity.get("role") == "construction":
            continue
        point_ids = tuple(map(str, entity.get("point_ids", ())))
        if len(point_ids) < 2 or any(
            point_id not in sketch_points for point_id in point_ids
        ):
            continue
        source_id = str(entity.get("id", ""))
        if entity.get("type") == "segment" and len(point_ids) == 2:
            first_2d, second_2d = (
                sketch_points[point_id] for point_id in point_ids
            )
            midpoint_2d = (
                (first_2d[0] + second_2d[0]) * 0.5,
                (first_2d[1] + second_2d[1]) * 0.5,
            )
            endpoint_ids = (point_ids[0], point_ids[1])
        elif entity.get("type") == "arc" and len(point_ids) >= 3:
            if entity.get("arc_mode") == "center":
                endpoint_ids = (point_ids[1], point_ids[2])
                sampled = center_arc_points(
                    sketch_points[point_ids[0]],
                    sketch_points[point_ids[1]],
                    sketch_points[point_ids[2]],
                    segments=32,
                    clockwise=bool(entity.get("clockwise", False)),
                )
                if not sampled:
                    continue
                midpoint_2d = sampled[len(sampled) // 2]
            else:
                endpoint_ids = (point_ids[0], point_ids[-1])
                midpoint_2d = sketch_points[point_ids[len(point_ids) // 2]]
        elif entity.get("type") == "spline":
            endpoint_ids = (point_ids[0], point_ids[-1])
            interpolation_ids = point_ids[:-1] if (
                len(point_ids) >= 4 and point_ids[0] == point_ids[-1]
            ) else point_ids
            poles = TColgp_HArray1OfPnt(1, len(interpolation_ids))
            for pole_index, point_id in enumerate(interpolation_ids, 1):
                poles.SetValue(
                    pole_index, gp_Pnt(*sketch_points[point_id], 0.0)
                )
            interpolation = GeomAPI_Interpolate(
                poles, len(interpolation_ids) != len(point_ids), 1.0e-7
            )
            interpolation.Perform()
            if not interpolation.IsDone():
                continue
            curve = interpolation.Curve()
            point = curve.Value(
                (curve.FirstParameter() + curve.LastParameter()) * 0.5
            )
            midpoint_2d = (point.X(), point.Y())
        elif entity.get("type") == "ellipse" and len(point_ids) == 3:
            sampled = ellipse_points(
                sketch_points[point_ids[0]],
                sketch_points[point_ids[1]],
                sketch_points[point_ids[2]],
            )
            endpoint_ids = (point_ids[1], point_ids[1])
            closed_curve_seam_ids.add(point_ids[1])
            midpoint_2d = sampled[len(sampled) // 2]
        elif entity.get("type") == "elliptical_arc" and len(point_ids) == 5:
            sampled = elliptical_arc_points(
                sketch_points[point_ids[0]],
                sketch_points[point_ids[1]],
                sketch_points[point_ids[2]],
                sketch_points[point_ids[3]],
                sketch_points[point_ids[4]],
                clockwise=bool(entity.get("clockwise", False)),
            )
            if not sampled:
                continue
            endpoint_ids = (point_ids[3], point_ids[4])
            midpoint_2d = sampled[len(sampled) // 2]
        else:
            continue
        curve_point_ids[source_id] = endpoint_ids
        midpoint = transform_point(
            world_transform,
            transform_point(plane_transform, (*midpoint_2d, 0.0)),
        )
        curve_midpoints[source_id] = midpoint
        generated_midpoint = tuple(
            midpoint[index]
            + extrusion_direction[index] * ((start + end) * 0.5)
            for index in range(3)
        )
        curve_sources.append((source_id, generated_midpoint))

    profile_endpoint_ids = {
        point_id
        for endpoints in curve_point_ids.values()
        for point_id in endpoints
        if point_id not in closed_curve_seam_ids
    }

    explorer = TopExp_Explorer(shape, TopAbs_FACE)
    runtime_index = 0
    cap_faces: dict[str, list[tuple[Any, int]]] = {"start": [], "end": []}
    while explorer.More():
        runtime_index += 1
        face = explorer.Current()
        adaptor = BRepAdaptor_Surface(face)
        if (
            adaptor.GetType() == GeomAbs_Cylinder
            and circular_source_id is not None
        ):
            registry.register_face(
                FaceRef(
                    feature.entity_id,
                    "generated",
                    circular_source_id,
                ),
                face,
                runtime_index=runtime_index,
            )
            explorer.Next()
            continue
        matching_curves = []
        for source_id, midpoint in curve_sources:
            distance = BRepExtrema_DistShapeShape(
                BRepBuilderAPI_MakeVertex(gp_Pnt(*midpoint)).Vertex(), face
            )
            distance.Perform()
            if distance.IsDone() and distance.Value() <= 1.0e-6:
                matching_curves.append(source_id)
        if len(matching_curves) == 1:
            registry.register_face(
                FaceRef(
                    feature_id=feature.entity_id,
                    role="generated",
                    source_id=matching_curves[0],
                ),
                face,
                runtime_index=runtime_index,
            )
            explorer.Next()
            continue
        if adaptor.GetType() != GeomAbs_Plane:
            explorer.Next()
            continue
        direction = adaptor.Plane().Axis().Direction()
        sign = -1.0 if face.Orientation() == TopAbs_REVERSED else 1.0
        normal = (
            sign * direction.X(),
            sign * direction.Y(),
            sign * direction.Z(),
        )
        agreement = vector_dot(normal, extrusion_direction)
        role = (
            "end"
            if agreement > 1.0 - 1.0e-7
            else "start"
            if agreement < -1.0 + 1.0e-7
            else None
        )
        if role is not None:
            cap_faces[role].append((face, runtime_index))
        else:
            plane = adaptor.Plane()
            location = plane.Location()
            plane_point = (location.X(), location.Y(), location.Z())
            matching_sources = [
                source_id
                for source_id, midpoint in curve_sources
                if abs(vector_dot(
                    normal,
                    tuple(
                        midpoint[index] - plane_point[index]
                        for index in range(3)
                    ),
                )) <= 1.0e-6
            ]
            if len(matching_sources) == 1:
                registry.register_face(
                    FaceRef(
                        feature_id=feature.entity_id,
                        role="generated",
                        source_id=matching_sources[0],
                    ),
                    face,
                    runtime_index=runtime_index,
                )
        explorer.Next()

    for role, candidates in cap_faces.items():
        if len(candidates) == 1:
            face, face_index = candidates[0]
            registry.register_face(
                FaceRef(feature_id=feature.entity_id, role=role),
                face,
                runtime_index=face_index,
            )
            continue
        ordered_candidates = sorted(
            candidates,
            key=lambda candidate: _topology_fragment_key(
                candidate[0], TopAbs_FACE
            ),
        )
        for fragment, (face, face_index) in enumerate(
            ordered_candidates, 1
        ):
            boundary_sources: list[str] = []
            face_edges = TopExp_Explorer(face, TopAbs_EDGE)
            edges = []
            while face_edges.More():
                edges.append(face_edges.Current())
                face_edges.Next()
            for source_id, midpoint in curve_midpoints.items():
                source_points = curve_point_ids[source_id]
                expected = tuple(
                    point_positions[point_id][role]
                    for point_id in source_points
                )
                sample = tuple(
                    midpoint[index]
                    + extrusion_direction[index]
                    * (start if role == "start" else end)
                    for index in range(3)
                )
                vertex = BRepBuilderAPI_MakeVertex(gp_Pnt(*sample)).Vertex()
                touches_boundary = False
                for edge in edges:
                    try:
                        adaptor = BRepAdaptor_Curve(edge)
                        edge_endpoints = (
                            adaptor.Value(adaptor.FirstParameter()),
                            adaptor.Value(adaptor.LastParameter()),
                        )
                        endpoint_positions = tuple(
                            (point.X(), point.Y(), point.Z())
                            for point in edge_endpoints
                        )
                    except (AttributeError, RuntimeError):
                        continue
                    endpoints_match = (
                        sum(
                            (endpoint_positions[0][index] - expected[0][index]) ** 2
                            for index in range(3)
                        ) <= 1.0e-12
                        and sum(
                            (endpoint_positions[1][index] - expected[1][index]) ** 2
                            for index in range(3)
                        ) <= 1.0e-12
                    ) or (
                        sum(
                            (endpoint_positions[0][index] - expected[1][index]) ** 2
                            for index in range(3)
                        ) <= 1.0e-12
                        and sum(
                            (endpoint_positions[1][index] - expected[0][index]) ** 2
                            for index in range(3)
                        ) <= 1.0e-12
                    )
                    if not endpoints_match:
                        continue
                    distance = BRepExtrema_DistShapeShape(vertex, edge)
                    distance.Perform()
                    if distance.IsDone() and distance.Value() <= 1.0e-6:
                        touches_boundary = True
                        break
                if touches_boundary:
                    boundary_sources.append(source_id)
            source_id = semantic_provenance_id(*(
                EdgeRef(feature.entity_id, role, boundary_source)
                for boundary_source in boundary_sources
            )) if boundary_sources else None
            registry.register_face(
                FaceRef(
                    feature.entity_id,
                    role,
                    source_id,
                    fragment if source_id is None else None,
                ),
                face,
                runtime_index=face_index,
            )

    def point_tuple(point) -> tuple[float, float, float]:
        return (point.X(), point.Y(), point.Z())

    def points_match(
        first: tuple[float, float, float],
        second: tuple[float, float, float],
    ) -> bool:
        return sum(
            (first[index] - second[index]) ** 2 for index in range(3)
        ) <= 1.0e-12

    edge_explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    runtime_edge_index = 0
    seen_edges: list[Any] = []
    while edge_explorer.More():
        edge = edge_explorer.Current()
        if any(edge.IsSame(existing) for existing in seen_edges):
            edge_explorer.Next()
            continue
        seen_edges.append(edge)
        runtime_edge_index += 1
        try:
            adaptor = BRepAdaptor_Curve(edge)
            endpoints = (
                point_tuple(adaptor.Value(adaptor.FirstParameter())),
                point_tuple(adaptor.Value(adaptor.LastParameter())),
            )
        except (AttributeError, RuntimeError):
            edge_explorer.Next()
            continue
        matched_reference = None
        if (
            adaptor.GetType() == GeomAbs_Circle
            and circular_source_id is not None
        ):
            circle = adaptor.Circle()
            center = circle.Location()
            center_position = point_tuple(center)
            axial_distance = vector_dot(
                extrusion_direction,
                tuple(
                    center_position[index] - profile_origin[index]
                    for index in range(3)
                ),
            )
            role = (
                "start" if abs(axial_distance - start) <= 1.0e-6
                else "end" if abs(axial_distance - end) <= 1.0e-6
                else None
            )
            if role is not None:
                matched_reference = EdgeRef(
                    feature.entity_id, role, circular_source_id
                )
        for curve_id, point_ids in curve_point_ids.items():
            if matched_reference is not None:
                break
            for role in ("start", "end"):
                expected = tuple(
                    point_positions[point_id][role] for point_id in point_ids
                )
                if (
                    points_match(endpoints[0], expected[0])
                    and points_match(endpoints[1], expected[1])
                ) or (
                    points_match(endpoints[0], expected[1])
                    and points_match(endpoints[1], expected[0])
                ):
                    sample = tuple(
                        curve_midpoints[curve_id][index]
                        + extrusion_direction[index]
                        * (start if role == "start" else end)
                        for index in range(3)
                    )
                    distance = BRepExtrema_DistShapeShape(
                        BRepBuilderAPI_MakeVertex(gp_Pnt(*sample)).Vertex(),
                        edge,
                    )
                    distance.Perform()
                    if distance.IsDone() and distance.Value() <= 1.0e-6:
                        matched_reference = EdgeRef(
                            feature.entity_id, role, curve_id
                        )
                        break
            if matched_reference is not None:
                break
        if matched_reference is None:
            for point_id in profile_endpoint_ids:
                positions = point_positions[point_id]
                expected = (positions["start"], positions["end"])
                if (
                    points_match(endpoints[0], expected[0])
                    and points_match(endpoints[1], expected[1])
                ) or (
                    points_match(endpoints[0], expected[1])
                    and points_match(endpoints[1], expected[0])
                ):
                    matched_reference = EdgeRef(
                        feature.entity_id, "generated", point_id
                    )
                    break
        if matched_reference is not None:
            registry.register_edge(
                matched_reference,
                edge,
                runtime_index=runtime_edge_index,
            )
        edge_explorer.Next()

    vertex_explorer = TopExp_Explorer(shape, TopAbs_VERTEX)
    runtime_vertex_index = 0
    seen_vertices: list[Any] = []
    while vertex_explorer.More():
        vertex = vertex_explorer.Current()
        if any(vertex.IsSame(existing) for existing in seen_vertices):
            vertex_explorer.Next()
            continue
        seen_vertices.append(vertex)
        runtime_vertex_index += 1
        try:
            position = point_tuple(BRep_Tool.Pnt(vertex))
        except (TypeError, RuntimeError):
            vertex_explorer.Next()
            continue
        matched_reference = next(
            (
                VertexRef(feature.entity_id, role, point_id)
                for point_id in profile_endpoint_ids
                for positions in (point_positions[point_id],)
                for role in ("start", "end")
                if points_match(position, positions[role])
            ),
            None,
        )
        if matched_reference is not None:
            registry.register_vertex(
                matched_reference,
                vertex,
                runtime_index=runtime_vertex_index,
            )
        vertex_explorer.Next()
    return registry


def revolve_face_registry(
    document: PartDocument,
    container: ZimaEntity,
    shape,
) -> TopologyRegistry:
    """Name Revolve topology from persistent Sketch entity provenance."""

    registry = TopologyRegistry()
    if container.container_type != ContainerType.REVOLVE or shape is None:
        return registry
    feature = next(
        (
            child for child in container.children
            if child.kind == EntityKind.REVOLVE and not child.locked
        ),
        None,
    )
    if feature is None:
        return registry
    sketch = document.find_entity(str(feature.parameters.get("sketch_id", "")))
    if sketch is None or sketch.kind != EntityKind.SKETCH:
        return registry
    try:
        sketch_model = SketchModel.from_dict(
            json.loads(str(sketch.parameters.get("sketch_data", "{}")))
        )
        entities, _dimensions = sketch_model.to_editor_data()
    except (TypeError, ValueError, json.JSONDecodeError, SketchModelError):
        return registry
    points = {
        str(entity.get("id", "")): (
            float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
        )
        for entity in entities
        if isinstance(entity, dict) and entity.get("type") == "point"
    }
    axis_geometry = next(
        (
            entity for entity in entities
            if isinstance(entity, dict)
            and entity.get("type") == "construction"
            and len(entity.get("point_ids", ())) == 2
        ),
        None,
    )
    if axis_geometry is None:
        return registry
    axis_ids = tuple(map(str, axis_geometry.get("point_ids", ())))
    if len(axis_ids) != 2 or any(point_id not in points for point_id in axis_ids):
        return registry
    plane = str(sketch.parameters.get("plane", "xz"))
    plane_transform = multiply_transforms(
        sketch_plane_offset_transform(
            plane,
            float(feature.parameters.get("profile_offset", 0.0)),
        ),
        sketch_plane_transform(plane),
    )
    axis_start = transform_point(plane_transform, (*points[axis_ids[0]], 0.0))
    axis_end = transform_point(plane_transform, (*points[axis_ids[1]], 0.0))
    axis_direction = normalized(tuple(axis_end[i] - axis_start[i] for i in range(3)))
    world_transform = entity_world_transform(document, container.entity_id)
    if axis_direction is None or world_transform is None:
        return registry

    angle = max(1.0e-6, min(360.0, float(feature.parameters.get("angle", 360.0))))
    reverse_angle = max(1.0e-6, min(360.0, float(feature.parameters.get("angle_reverse", angle))))
    extent_mode = str(feature.parameters.get("extent_mode", "one_side"))
    if extent_mode == "symmetric":
        half = min(angle, 180.0)
        start_angle, end_angle = -half, half
    elif extent_mode == "two_sides":
        reverse_angle = min(reverse_angle, max(0.0, 360.0 - angle))
        start_angle, end_angle = -reverse_angle, angle
    elif str(feature.parameters.get("direction", "forward")) == "reverse":
        start_angle, end_angle = -angle, 0.0
    else:
        start_angle, end_angle = 0.0, angle
    is_full = end_angle - start_angle >= 360.0 - 1.0e-7

    def rotate(point, degrees: float) -> tuple[float, float, float]:
        vector = tuple(point[i] - axis_start[i] for i in range(3))
        radians = math.radians(degrees)
        cosine, sine = math.cos(radians), math.sin(radians)
        cross = (
            axis_direction[1] * vector[2] - axis_direction[2] * vector[1],
            axis_direction[2] * vector[0] - axis_direction[0] * vector[2],
            axis_direction[0] * vector[1] - axis_direction[1] * vector[0],
        )
        projection = vector_dot(axis_direction, vector)
        local = tuple(
            axis_start[i] + vector[i] * cosine + cross[i] * sine
            + axis_direction[i] * projection * (1.0 - cosine)
            for i in range(3)
        )
        return transform_point(world_transform, local)

    local_points = {
        point_id: transform_point(plane_transform, (*point, 0.0))
        for point_id, point in points.items()
    }
    positions = {
        point_id: {
            "start": rotate(point, start_angle),
            "end": rotate(point, end_angle),
        }
        for point_id, point in local_points.items()
    }
    curve_endpoints: dict[str, tuple[str, str]] = {}
    curve_midpoints: dict[str, tuple[float, float, float]] = {}
    generated_samples: dict[str, tuple[float, float, float]] = {}
    closed_curve_seam_ids: set[str] = set()
    middle_angle = (start_angle + end_angle) * 0.5
    for entity in entities:
        if not isinstance(entity, dict) or entity.get("type") not in (
            "segment", "arc", "spline", "ellipse", "elliptical_arc"
        ):
            continue
        if entity.get("role") == "construction":
            continue
        point_ids = tuple(map(str, entity.get("point_ids", ())))
        if any(point_id not in local_points for point_id in point_ids):
            continue
        source_id = str(entity.get("id", ""))
        if entity.get("type") == "segment" and len(point_ids) == 2:
            endpoint_ids = (point_ids[0], point_ids[1])
            midpoint_2d = tuple(
                (points[endpoint_ids[0]][i] + points[endpoint_ids[1]][i])
                * 0.5
                for i in range(2)
            )
        elif entity.get("type") == "arc" and len(point_ids) >= 3:
            if entity.get("arc_mode") == "center":
                endpoint_ids = (point_ids[1], point_ids[2])
                sampled = center_arc_points(
                    points[point_ids[0]],
                    points[point_ids[1]],
                    points[point_ids[2]],
                    segments=32,
                    clockwise=bool(entity.get("clockwise", False)),
                )
                if not sampled:
                    continue
                midpoint_2d = sampled[len(sampled) // 2]
            else:
                endpoint_ids = (point_ids[0], point_ids[-1])
                midpoint_2d = points[point_ids[len(point_ids) // 2]]
        elif entity.get("type") == "spline" and len(point_ids) >= 2:
            endpoint_ids = (point_ids[0], point_ids[-1])
            interpolation_ids = point_ids[:-1] if (
                len(point_ids) >= 4 and point_ids[0] == point_ids[-1]
            ) else point_ids
            poles = TColgp_HArray1OfPnt(1, len(interpolation_ids))
            for pole_index, point_id in enumerate(interpolation_ids, 1):
                poles.SetValue(
                    pole_index, gp_Pnt(*points[point_id], 0.0)
                )
            interpolation = GeomAPI_Interpolate(
                poles, len(interpolation_ids) != len(point_ids), 1.0e-7
            )
            interpolation.Perform()
            if not interpolation.IsDone():
                continue
            curve = interpolation.Curve()
            point = curve.Value(
                (curve.FirstParameter() + curve.LastParameter()) * 0.5
            )
            midpoint_2d = (point.X(), point.Y())
        elif entity.get("type") == "ellipse" and len(point_ids) == 3:
            sampled = ellipse_points(
                points[point_ids[0]],
                points[point_ids[1]],
                points[point_ids[2]],
            )
            endpoint_ids = (point_ids[1], point_ids[1])
            closed_curve_seam_ids.add(point_ids[1])
            midpoint_2d = sampled[len(sampled) // 2]
        elif entity.get("type") == "elliptical_arc" and len(point_ids) == 5:
            sampled = elliptical_arc_points(
                points[point_ids[0]],
                points[point_ids[1]],
                points[point_ids[2]],
                points[point_ids[3]],
                points[point_ids[4]],
                clockwise=bool(entity.get("clockwise", False)),
            )
            if not sampled:
                continue
            endpoint_ids = (point_ids[3], point_ids[4])
            midpoint_2d = sampled[len(sampled) // 2]
        else:
            continue
        curve_endpoints[source_id] = endpoint_ids
        midpoint = transform_point(
            plane_transform, (*midpoint_2d, 0.0)
        )
        curve_midpoints[source_id] = midpoint
        generated_samples[source_id] = rotate(midpoint, middle_angle)

    profile_endpoint_ids = {
        point_id
        for endpoints in curve_endpoints.values()
        for point_id in endpoints
        if point_id not in closed_curve_seam_ids
    }

    def point_tuple(point) -> tuple[float, float, float]:
        return (point.X(), point.Y(), point.Z())

    def points_match(first, second) -> bool:
        return sum((first[i] - second[i]) ** 2 for i in range(3)) <= 1.0e-12

    face_explorer = TopExp_Explorer(shape, TopAbs_FACE)
    face_index = 0
    cap_faces: dict[str, list[tuple[Any, int]]] = {"start": [], "end": []}
    while face_explorer.More():
        face_index += 1
        face = face_explorer.Current()
        face_vertices = []
        vertex_explorer = TopExp_Explorer(face, TopAbs_VERTEX)
        while vertex_explorer.More():
            position = point_tuple(BRep_Tool.Pnt(vertex_explorer.Current()))
            if not any(points_match(position, existing) for existing in face_vertices):
                face_vertices.append(position)
            vertex_explorer.Next()
        matched = None
        cap_role = None
        is_planar_face = BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane
        if not is_full and is_planar_face:
            for role in ("start", "end"):
                role_angle = start_angle if role == "start" else end_angle
                boundary_samples = [
                    rotate(midpoint, role_angle)
                    for midpoint in curve_midpoints.values()
                ]
                if boundary_samples:
                    on_face = []
                    for sample in boundary_samples:
                        distance = BRepExtrema_DistShapeShape(
                            BRepBuilderAPI_MakeVertex(
                                gp_Pnt(*sample)
                            ).Vertex(),
                            face,
                        )
                        distance.Perform()
                        on_face.append(
                            distance.IsDone() and distance.Value() <= 1.0e-6
                        )
                    if all(on_face):
                        cap_role = role
                        break
                expected = [positions[point_id][role] for point_id in local_points]
                if len(face_vertices) >= 2 and all(
                    any(points_match(position, candidate) for candidate in expected)
                    for position in face_vertices
                ):
                    cap_role = role
                    break
        if cap_role is None:
            candidates = []
            for source_id, sample in generated_samples.items():
                distance = BRepExtrema_DistShapeShape(
                    BRepBuilderAPI_MakeVertex(gp_Pnt(*sample)).Vertex(), face
                )
                distance.Perform()
                if distance.IsDone() and distance.Value() <= 1.0e-6:
                    candidates.append(source_id)
            if len(candidates) == 1:
                matched = FaceRef(feature.entity_id, "generated", candidates[0])
        if matched is not None:
            registry.register_face(matched, face, runtime_index=face_index)
        elif cap_role is not None:
            cap_faces[cap_role].append((face, face_index))
        face_explorer.Next()

    for role, candidates in cap_faces.items():
        if len(candidates) == 1:
            face, runtime_index = candidates[0]
            registry.register_face(
                FaceRef(feature.entity_id, role),
                face,
                runtime_index=runtime_index,
            )
            continue
        for fragment, (face, runtime_index) in enumerate(
            sorted(
                candidates,
                key=lambda candidate: _topology_fragment_key(
                    candidate[0], TopAbs_FACE
                ),
            ),
            1,
        ):
            registry.register_face(
                FaceRef(feature.entity_id, role, fragment=fragment),
                face,
                runtime_index=runtime_index,
            )

    edge_explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    edge_index = 0
    seen_edges: list[Any] = []
    while edge_explorer.More():
        edge = edge_explorer.Current()
        if any(edge.IsSame(existing) for existing in seen_edges):
            edge_explorer.Next()
            continue
        seen_edges.append(edge)
        edge_index += 1
        adaptor = BRepAdaptor_Curve(edge)
        endpoints = (
            point_tuple(adaptor.Value(adaptor.FirstParameter())),
            point_tuple(adaptor.Value(adaptor.LastParameter())),
        )
        matched = None
        if not is_full:
            for source_id, point_ids in curve_endpoints.items():
                for role in ("start", "end"):
                    expected = tuple(positions[point_id][role] for point_id in point_ids)
                    if ((points_match(endpoints[0], expected[0]) and points_match(endpoints[1], expected[1])) or
                            (points_match(endpoints[0], expected[1]) and points_match(endpoints[1], expected[0]))):
                        sample = rotate(
                            curve_midpoints[source_id],
                            start_angle if role == "start" else end_angle,
                        )
                        distance = BRepExtrema_DistShapeShape(
                            BRepBuilderAPI_MakeVertex(
                                gp_Pnt(*sample)
                            ).Vertex(),
                            edge,
                        )
                        distance.Perform()
                        if distance.IsDone() and distance.Value() <= 1.0e-6:
                            matched = EdgeRef(
                                feature.entity_id, role, source_id
                            )
                            break
                if matched is not None:
                    break
        if matched is None:
            for point_id in profile_endpoint_ids:
                point_positions = positions[point_id]
                expected = (point_positions["start"], point_positions["end"])
                if ((points_match(endpoints[0], expected[0]) and points_match(endpoints[1], expected[1])) or
                        (points_match(endpoints[0], expected[1]) and points_match(endpoints[1], expected[0]))):
                    matched = EdgeRef(feature.entity_id, "generated", point_id)
                    break
        if matched is not None:
            registry.register_edge(matched, edge, runtime_index=edge_index)
        edge_explorer.Next()

    if not is_full:
        vertex_explorer = TopExp_Explorer(shape, TopAbs_VERTEX)
        vertex_index = 0
        seen_vertices: list[Any] = []
        while vertex_explorer.More():
            vertex = vertex_explorer.Current()
            if any(vertex.IsSame(existing) for existing in seen_vertices):
                vertex_explorer.Next()
                continue
            seen_vertices.append(vertex)
            vertex_index += 1
            position = point_tuple(BRep_Tool.Pnt(vertex))
            matched = next((
                VertexRef(feature.entity_id, role, point_id)
                for point_id in profile_endpoint_ids
                for point_positions in (positions[point_id],)
                for role in ("start", "end")
                if points_match(position, point_positions[role])
            ), None)
            if matched is not None:
                registry.register_vertex(matched, vertex, runtime_index=vertex_index)
            vertex_explorer.Next()
    return registry


def _standalone_topology_registry(
    document: PartDocument,
    container: ZimaEntity,
    shape,
) -> TopologyRegistry:
    if container.container_type == ContainerType.PROTRUSION:
        return protrusion_face_registry(document, container, shape)
    if container.container_type == ContainerType.REVOLVE:
        return revolve_face_registry(document, container, shape)
    solid = next((
        child for child in container.children
        if child.kind in (EntityKind.BOX, EntityKind.WEDGE)
        and not child.locked
    ), None)
    return (
        semantic_face_registry(document, solid, shape)
        if solid is not None
        else TopologyRegistry()
    )


def _container_boolean_operation(container: ZimaEntity) -> CombineMode:
    feature = next((
        child for child in container.children
        if child.kind in (EntityKind.PROTRUSION, EntityKind.REVOLVE)
        and not child.locked
    ), None)
    if feature is not None:
        try:
            return CombineMode(str(feature.parameters.get(
                "operation", CombineMode.ADD.value
            )))
        except ValueError:
            return CombineMode.ADD
    solid = next((
        child for child in container.children
        if child.kind in SOLID_KINDS and not child.locked
    ), None)
    return solid.combine_mode if solid is not None else CombineMode.NONE


def _container_boolean_feature_id(container: ZimaEntity) -> str:
    feature = next((
        child for child in container.children
        if child.kind in (
            EntityKind.PROTRUSION,
            EntityKind.REVOLVE,
            *SOLID_KINDS,
        )
        and not child.locked
    ), None)
    return feature.entity_id if feature is not None else container.entity_id


def _unique_subshapes(shape, shape_type: int) -> list[Any]:
    explorer = TopExp_Explorer(shape, shape_type)
    result = []
    while explorer.More():
        candidate = explorer.Current()
        if not any(candidate.IsSame(existing) for existing in result):
            result.append(candidate)
        explorer.Next()
    return result


def _rebind_registry_to_shape(
    registry: TopologyRegistry,
    shape,
) -> TopologyRegistry:
    """Bind one evaluation's semantic refs to an equivalent live shape."""

    rebound = TopologyRegistry()
    kinds = (
        (
            _unique_subshapes(shape, TopAbs_FACE),
            registry.references,
            registry.runtime_index_for_reference,
            rebound.register_face,
        ),
        (
            _unique_subshapes(shape, TopAbs_EDGE),
            registry.edge_references,
            registry.edge_runtime_index_for_reference,
            rebound.register_edge,
        ),
        (
            _unique_subshapes(shape, TopAbs_VERTEX),
            registry.vertex_references,
            registry.vertex_runtime_index_for_reference,
            rebound.register_vertex,
        ),
    )
    for shapes, references, runtime_index, register in kinds:
        for reference in references:
            index = runtime_index(reference)
            if index is not None and 0 < index <= len(shapes):
                register(reference, shapes[index - 1], runtime_index=index)
    return rebound


def _boolean_history_shapes(builder, source_shape) -> list[Any]:
    candidates = []
    for method_name in ("Modified", "Generated"):
        try:
            history = getattr(builder, method_name)(source_shape)
        except (AttributeError, RuntimeError, TypeError):
            continue
        try:
            iterator = iter(history)
        except TypeError:
            continue
        for candidate in iterator:
            if not any(candidate.IsSame(existing) for existing in candidates):
                candidates.append(candidate)
    return candidates


def _topology_fragment_key(shape, shape_type: int) -> tuple[float, ...]:
    if shape_type == TopAbs_VERTEX:
        point = BRep_Tool.Pnt(shape)
        return (point.X(), point.Y(), point.Z())
    properties = GProp_GProps()
    if shape_type == TopAbs_FACE:
        brepgprop.SurfaceProperties(shape, properties)
    else:
        brepgprop.LinearProperties(shape, properties)
    center = properties.CentreOfMass()
    return (
        center.X(), center.Y(), center.Z(), abs(float(properties.Mass()))
    )


def _reference_with_fragment(reference, fragment: int):
    return type(reference)(
        reference.feature_id,
        reference.role,
        reference.source_id,
        fragment,
    )


def _propagate_boolean_registry(
    builder,
    result_shape,
    left: TopologyRegistry,
    right: TopologyRegistry,
) -> TopologyRegistry:
    """Propagate semantic ancestry through one OCCT Boolean operation."""

    result = TopologyRegistry()
    kinds = (
        (
            TopAbs_FACE,
            (*left.face_entries, *right.face_entries),
            result.register_face,
        ),
        (
            TopAbs_EDGE,
            (*left.edge_entries, *right.edge_entries),
            result.register_edge,
        ),
        (
            TopAbs_VERTEX,
            (*left.vertex_entries, *right.vertex_entries),
            result.register_vertex,
        ),
    )
    for shape_type, entries, register in kinds:
        final_shapes = _unique_subshapes(result_shape, shape_type)
        assignments: list[tuple[Any, list[int], Any, bool]] = []
        for reference, source_shapes in entries:
            candidate_indices = []
            for source_shape in source_shapes:
                history_shapes = _boolean_history_shapes(builder, source_shape)
                if not history_shapes:
                    try:
                        deleted = bool(builder.IsDeleted(source_shape))
                    except (AttributeError, RuntimeError, TypeError):
                        deleted = True
                    if not deleted:
                        history_shapes = [source_shape]
                for candidate in history_shapes:
                    index = next((
                        item_index
                        for item_index, final_shape in enumerate(final_shapes)
                        if candidate.IsSame(final_shape)
                    ), None)
                    if index is not None and index not in candidate_indices:
                        candidate_indices.append(index)
            candidate_indices.sort(
                key=lambda index: _topology_fragment_key(
                    final_shapes[index], shape_type
                )
            )
            if not candidate_indices:
                continue
            if len(candidate_indices) == 1:
                assignments.append((
                    reference, candidate_indices, reference, True
                ))
                continue
            # Keep the pre-split identity explicitly ambiguous and expose
            # deterministic fragment references for subsequent selections.
            assignments.append((
                reference, candidate_indices, reference, False
            ))
            for fragment, index in enumerate(candidate_indices, 1):
                assignments.append((
                    _reference_with_fragment(reference, fragment),
                    [index],
                    reference,
                    True,
                ))

        origins_by_index: dict[int, set[Any]] = {}
        for _reference, indices, origin, _runtime in assignments:
            for index in indices:
                origins_by_index.setdefault(index, set()).add(origin)
        for reference, indices, _origin, expose_runtime in assignments:
            merged = any(
                len(origins_by_index.get(index, ())) > 1
                for index in indices
            )
            for index in indices:
                shape = final_shapes[index]
                register(
                    reference,
                    shape,
                    runtime_index=(index + 1)
                    if expose_runtime and not merged
                    else None,
                )
                if merged:
                    # Duplicate registration deliberately yields AMBIGUOUS;
                    # a merge must never silently pick one ancestry.
                    register(reference, shape)
    return result


def make_fillet_shape(
    shape,
    registry: TopologyRegistry,
    edge_reference: EdgeRef,
    radius: float,
    feature_id: str,
) -> tuple[Any, TopologyRegistry]:
    """Fillet one persistently named edge and propagate its topology."""

    if shape is None:
        raise ValueError("Fillet requires an input shape")
    if radius <= 0.0 or not math.isfinite(radius):
        raise ValueError("Fillet radius must be a positive finite number")
    resolution = registry.resolve_edge(edge_reference)
    if resolution.state.value != "resolved" or resolution.shape is None:
        raise ValueError(
            f"Fillet edge is {resolution.state.value}: "
            f"{edge_reference.serialize()}"
        )
    builder = BRepFilletAPI_MakeFillet(shape)
    builder.Add(float(radius), resolution.shape)
    builder.Build()
    if not builder.IsDone():
        raise ValueError("Fillet could not be built with the requested radius")
    result_shape = builder.Shape()
    if result_shape.IsNull() or not _unique_subshapes(result_shape, TopAbs_SOLID):
        raise ValueError("Fillet did not produce a solid")

    result_registry = _propagate_boolean_registry(
        builder, result_shape, registry, TopologyRegistry()
    )
    final_faces = _unique_subshapes(result_shape, TopAbs_FACE)
    generated_indices = {
        index
        for generated in _boolean_history_shapes(builder, resolution.shape)
        for index, candidate in enumerate(final_faces)
        if generated.ShapeType() == TopAbs_FACE and generated.IsSame(candidate)
    }
    generated_reference = FaceRef(
        feature_id,
        "generated",
        semantic_provenance_id(edge_reference),
    )
    _register_derived_references(
        result_registry,
        {generated_reference: list(generated_indices)},
        final_faces,
        result_registry.register_face,
    )
    _register_feature_incidence_topology(
        result_registry,
        result_shape,
        feature_id,
    )
    return result_shape, result_registry


def _register_feature_incidence_topology(
    registry: TopologyRegistry,
    shape,
    feature_id: str,
) -> None:
    """Name still-unmapped feature edges/vertices by semantic incidence."""

    faces = _unique_subshapes(shape, TopAbs_FACE)
    edges = _unique_subshapes(shape, TopAbs_EDGE)
    vertices = _unique_subshapes(shape, TopAbs_VERTEX)
    face_refs = {
        index: references[0]
        for index, face in enumerate(faces)
        if len(references := _resolved_face_refs_for_shape(registry, face)) == 1
    }
    adjacent_faces: dict[int, set[FaceRef]] = {}
    incident_faces: dict[int, set[FaceRef]] = {}
    for face_index, face in enumerate(faces):
        reference = face_refs.get(face_index)
        if reference is None:
            continue
        edge_explorer = TopExp_Explorer(face, TopAbs_EDGE)
        while edge_explorer.More():
            edge = edge_explorer.Current()
            edge_index = next((
                index for index, candidate in enumerate(edges)
                if edge.IsSame(candidate)
            ), None)
            if edge_index is not None:
                adjacent_faces.setdefault(edge_index, set()).add(reference)
            edge_explorer.Next()
        vertex_explorer = TopExp_Explorer(face, TopAbs_VERTEX)
        while vertex_explorer.More():
            vertex = vertex_explorer.Current()
            vertex_index = next((
                index for index, candidate in enumerate(vertices)
                if vertex.IsSame(candidate)
            ), None)
            if vertex_index is not None:
                incident_faces.setdefault(vertex_index, set()).add(reference)
            vertex_explorer.Next()

    edge_references: dict[EdgeRef, list[int]] = {}
    for edge_index, references in adjacent_faces.items():
        if (
            registry.edge_reference_for_runtime_index(edge_index + 1) is None
            and len(references) == 2
        ):
            reference = EdgeRef(
                feature_id,
                "generated-edge",
                semantic_provenance_id(*references),
            )
            edge_references.setdefault(reference, []).append(edge_index)
    _register_derived_references(
        registry,
        edge_references,
        edges,
        registry.register_edge,
    )

    vertex_references: dict[VertexRef, list[int]] = {}
    for vertex_index, references in incident_faces.items():
        if (
            registry.vertex_reference_for_runtime_index(vertex_index + 1)
            is None
            and len(references) >= 3
        ):
            reference = VertexRef(
                feature_id,
                "generated-vertex",
                semantic_provenance_id(*references),
            )
            vertex_references.setdefault(reference, []).append(vertex_index)
    _register_derived_references(
        registry,
        vertex_references,
        vertices,
        registry.register_vertex,
    )


def _resolved_face_refs_for_shape(
    registry: TopologyRegistry,
    face,
) -> tuple[FaceRef, ...]:
    references = []
    for reference, shapes in registry.face_entries:
        resolution = registry.resolve(reference)
        if resolution.state.value != "resolved":
            continue
        if any(face.IsSame(candidate) for candidate in shapes):
            references.append(reference)
    return tuple(references)


def _register_derived_references(
    registry: TopologyRegistry,
    references_by_index: dict[Any, list[int]],
    final_shapes: list[Any],
    register,
) -> None:
    for reference, raw_indices in references_by_index.items():
        indices = sorted(
            set(raw_indices),
            key=lambda index: _topology_fragment_key(
                final_shapes[index], final_shapes[index].ShapeType()
            ),
        )
        if len(indices) == 1:
            index = indices[0]
            register(reference, final_shapes[index], runtime_index=index + 1)
            continue
        for index in indices:
            register(reference, final_shapes[index])
        for fragment, index in enumerate(indices, 1):
            register(
                _reference_with_fragment(reference, fragment),
                final_shapes[index],
                runtime_index=index + 1,
            )


def _register_boolean_intersections(
    builder,
    result_shape,
    registry: TopologyRegistry,
    feature_id: str,
) -> None:
    """Assign ZIMA-owned identities to new Boolean section topology."""

    try:
        section_edges = list(builder.SectionEdges())
    except (AttributeError, RuntimeError, TypeError):
        return
    if not section_edges:
        return
    final_faces = _unique_subshapes(result_shape, TopAbs_FACE)
    final_edges = _unique_subshapes(result_shape, TopAbs_EDGE)
    final_vertices = _unique_subshapes(result_shape, TopAbs_VERTEX)

    face_refs: dict[int, FaceRef] = {}
    for face_index, face in enumerate(final_faces):
        references = _resolved_face_refs_for_shape(registry, face)
        if len(references) == 1:
            face_refs[face_index] = references[0]

    adjacent_faces_by_edge: dict[int, list[int]] = {}
    incident_faces_by_vertex: dict[int, list[int]] = {}
    for face_index, face in enumerate(final_faces):
        edge_explorer = TopExp_Explorer(face, TopAbs_EDGE)
        while edge_explorer.More():
            edge = edge_explorer.Current()
            edge_index = next((
                index for index, candidate in enumerate(final_edges)
                if edge.IsSame(candidate)
            ), None)
            if edge_index is not None:
                adjacent_faces_by_edge.setdefault(edge_index, []).append(
                    face_index
                )
            edge_explorer.Next()
        vertex_explorer = TopExp_Explorer(face, TopAbs_VERTEX)
        while vertex_explorer.More():
            vertex = vertex_explorer.Current()
            vertex_index = next((
                index for index, candidate in enumerate(final_vertices)
                if vertex.IsSame(candidate)
            ), None)
            if vertex_index is not None:
                incident_faces_by_vertex.setdefault(vertex_index, []).append(
                    face_index
                )
            vertex_explorer.Next()

    section_indices = {
        index
        for section in section_edges
        for index, edge in enumerate(final_edges)
        if section.IsSame(edge)
    }
    edge_references: dict[EdgeRef, list[int]] = {}
    section_vertex_indices: set[int] = set()
    for edge_index in section_indices:
        if registry.edge_reference_for_runtime_index(edge_index + 1) is not None:
            continue
        references = {
            face_refs[face_index]
            for face_index in adjacent_faces_by_edge.get(edge_index, ())
            if face_index in face_refs
        }
        if len(references) != 2:
            continue
        reference = EdgeRef(
            feature_id,
            "intersection",
            semantic_provenance_id(*references),
        )
        edge_references.setdefault(reference, []).append(edge_index)
        vertex_explorer = TopExp_Explorer(
            final_edges[edge_index], TopAbs_VERTEX
        )
        while vertex_explorer.More():
            vertex = vertex_explorer.Current()
            section_vertex_indices.update(
                index
                for index, candidate in enumerate(final_vertices)
                if vertex.IsSame(candidate)
            )
            vertex_explorer.Next()
    _register_derived_references(
        registry, edge_references, final_edges, registry.register_edge
    )

    vertex_references: dict[VertexRef, list[int]] = {}
    for vertex_index in section_vertex_indices:
        if registry.vertex_reference_for_runtime_index(vertex_index + 1) is not None:
            continue
        references = {
            face_refs[face_index]
            for face_index in incident_faces_by_vertex.get(vertex_index, ())
            if face_index in face_refs
        }
        if len(references) < 3:
            continue
        reference = VertexRef(
            feature_id,
            "intersection",
            semantic_provenance_id(*references),
        )
        vertex_references.setdefault(reference, []).append(vertex_index)
    _register_derived_references(
        registry,
        vertex_references,
        final_vertices,
        registry.register_vertex,
    )


def boolean_topology_registry_at(
    document: PartDocument,
    cursor: int,
) -> TopologyRegistry:
    """Evaluate supported feature ancestry through the Part Boolean history."""

    result_shape = None
    result_registry = TopologyRegistry()
    for container in document.history_objects_at(cursor):
        if container.container_type == ContainerType.FILLET:
            feature = next((
                child for child in container.children
                if child.kind == EntityKind.FILLET and not child.locked
            ), None)
            reference = (
                parse_edge_reference(feature.parameters.get("edge_ref"))
                if feature is not None else None
            )
            if result_shape is None or feature is None or reference is None:
                continue
            try:
                result_shape, result_registry = make_fillet_shape(
                    result_shape,
                    result_registry,
                    reference,
                    float(feature.parameters.get("radius", 1.0)),
                    feature.entity_id,
                )
            except (RuntimeError, TypeError, ValueError):
                pass
            continue
        tool_shape = document.build_standalone_shape(container)
        if tool_shape is None:
            continue
        tool_registry = _standalone_topology_registry(
            document, container, tool_shape
        )
        operation = _container_boolean_operation(container)
        if result_shape is None:
            result_shape = tool_shape
            result_registry = tool_registry
            continue
        builder = (
            BRepAlgoAPI_Cut(result_shape, tool_shape)
            if operation == CombineMode.SUBTRACT
            else BRepAlgoAPI_Fuse(result_shape, tool_shape)
        )
        combined = builder.Shape()
        if operation == CombineMode.ADD and len(
            _unique_subshapes(combined, TopAbs_SOLID)
        ) != 1:
            continue
        if operation == CombineMode.SUBTRACT and not _unique_subshapes(
            combined, TopAbs_SOLID
        ):
            continue
        result_registry = _propagate_boolean_registry(
            builder, combined, result_registry, tool_registry
        )
        _register_boolean_intersections(
            builder,
            combined,
            result_registry,
            _container_boolean_feature_id(container),
        )
        result_shape = combined
    return result_registry


def face_registry_at(
    document: PartDocument,
    cursor: int,
) -> TopologyRegistry:
    """Return semantic faces for a single-feature history snapshot."""

    objects = document.history_objects_at(cursor)
    if len(objects) > 1:
        return boolean_topology_registry_at(document, cursor)
    if len(objects) != 1:
        return TopologyRegistry()
    container = objects[0]
    shape = document.build_shape_at(cursor)
    if container.container_type == ContainerType.PROTRUSION:
        return protrusion_face_registry(document, container, shape)
    if container.container_type == ContainerType.REVOLVE:
        return revolve_face_registry(document, container, shape)
    solid = next(
        (
            child
            for child in container.children
            if child.kind in (EntityKind.BOX, EntityKind.WEDGE)
            and not child.locked
        ),
        None,
    )
    return (
        semantic_face_registry(document, solid, shape)
        if solid is not None
        else TopologyRegistry()
    )


def active_face_registry(document: PartDocument) -> TopologyRegistry:
    """Return semantic faces when the active result has unambiguous provenance."""

    return face_registry_at(document, document.history_cursor())


def resolve_entity_attachments(document: PartDocument, obj: ZimaEntity) -> None:
    if obj.attachment is not None:
        resolve_plane_on_face_attachment(document, obj)
    for child in obj.children:
        if not child.locked:
            resolve_entity_attachments(document, child)


def resolve_plane_on_face_attachment(document: PartDocument, obj: ZimaEntity) -> bool:
    attachment = obj.attachment
    if attachment is None:
        return True
    target = document.find_entity(attachment.target_object_id)
    if target is None:
        attachment.status = "broken_face"
        return False
    frame = solid_face_frames(document, target).get(attachment.target_face_role)
    if frame is None:
        attachment.status = "broken_face"
        return False
    point, normal = frame
    if attachment.flip_normal:
        normal = tuple(-value for value in normal)

    axes = {
        "x": (1.0, 0.0, 0.0),
        "y": (0.0, 1.0, 0.0),
        "z": (0.0, 0.0, 1.0),
    }
    primary = axes.get(attachment.primary_axis)
    secondary = axes.get(attachment.secondary_axis)
    if primary is None or secondary is None or abs(vector_dot(primary, secondary)) > 1e-9:
        attachment.status = "needs_reference"
        return False

    threshold = math.cos(math.radians(attachment.switch_angle))
    reference = primary if abs(vector_dot(normal, primary)) <= threshold else secondary
    attachment.active_axis = (
        attachment.primary_axis if reference is primary else attachment.secondary_axis
    )
    tangent = normalized(
        tuple(reference[index] - normal[index] * vector_dot(normal, reference) for index in range(3))
    )
    if tangent is None:
        attachment.status = "needs_reference"
        return False

    current_transform = coordinate_system_transform(obj.coordinate_system)
    source_tangent_axis = {
        "xy": (1.0, 0.0, 0.0),
        "yz": (0.0, 1.0, 0.0),
        "xz": (1.0, 0.0, 0.0),
    }.get(attachment.source_plane)
    if source_tangent_axis is None:
        attachment.status = "needs_reference"
        return False
    current_tangent = transform_vector(current_transform, source_tangent_axis)
    if vector_dot(tangent, current_tangent) < 0.0:
        tangent = tuple(-value for value in tangent)

    if attachment.source_plane == "xy":
        x_axis = tangent
        z_axis = normal
        y_axis = normalized(vector_cross(z_axis, x_axis))
    elif attachment.source_plane == "yz":
        x_axis = normal
        y_axis = tangent
        z_axis = normalized(vector_cross(x_axis, y_axis))
    else:
        x_axis = tangent
        y_axis = normal
        z_axis = normalized(vector_cross(x_axis, y_axis))
    if y_axis is None or z_axis is None:
        attachment.status = "needs_reference"
        return False

    rotation = (
        (x_axis[0], y_axis[0], z_axis[0]),
        (x_axis[1], y_axis[1], z_axis[1]),
        (x_axis[2], y_axis[2], z_axis[2]),
    )
    ry = math.asin(max(-1.0, min(1.0, -rotation[2][0])))
    if abs(math.cos(ry)) > 1.0e-9:
        rx = math.atan2(rotation[2][1], rotation[2][2])
        rz = math.atan2(rotation[1][0], rotation[0][0])
    else:
        rx = 0.0
        rz = math.atan2(-rotation[0][1], rotation[1][1])

    distance = vector_dot(normal, point)
    obj.coordinate_system.origin = tuple(normal[index] * distance for index in range(3))
    obj.coordinate_system.rotation = tuple(math.degrees(value) for value in (rx, ry, rz))
    attachment.status = "resolved" if reference is primary else "fallback_axis"
    return True


def gp_transform(
    transform: tuple[tuple[float, float, float, float], ...],
) -> gp_Trsf:
    trsf = gp_Trsf()
    trsf.SetValues(
        transform[0][0],
        transform[0][1],
        transform[0][2],
        transform[0][3],
        transform[1][0],
        transform[1][1],
        transform[1][2],
        transform[1][3],
        transform[2][0],
        transform[2][1],
        transform[2][2],
        transform[2][3],
    )
    return trsf


def transform_shape(
    shape,
    transform: tuple[tuple[float, float, float, float], ...],
):
    return BRepBuilderAPI_Transform(shape, gp_transform(transform), True).Shape()


def make_sketch_shape(
    parent: ZimaEntity,
    sketch: ZimaEntity,
    parent_transform: tuple[tuple[float, float, float, float], ...] | None = None,
    plane_offset: float | None = None,
):
    if sketch.kind != EntityKind.SKETCH:
        return None
    if sketch.parameters.get("profile") == "entities":
        try:
            sketch_model = SketchModel.from_dict(
                json.loads(str(sketch.parameters.get("sketch_data", "{}")))
            )
            entities, _dimensions = sketch_model.to_editor_data()
        except (
            TypeError,
            ValueError,
            json.JSONDecodeError,
            SketchModelError,
        ):
            return None
        compound = TopoDS_Compound()
        builder = BRep_Builder()
        builder.MakeCompound(compound)
        edge_count = 0
        point_positions = {
            str(entity.get("id", "")): [
                float(entity.get("x", 0.0)),
                float(entity.get("y", 0.0)),
            ]
            for entity in entities
            if isinstance(entity, dict)
            and entity.get("type") == "point"
            and str(entity.get("id", ""))
        }
        geometry_by_id = {
            str(entity.get("id", "")): entity
            for entity in entities
            if isinstance(entity, dict)
            and entity.get("type") == "segment"
            and entity.get("role") != "construction"
            and str(entity.get("id", ""))
        }
        corner_trim_points: dict[tuple[str, str], tuple[float, float]] = {}
        corner_arcs: list[tuple[tuple[float, float], ...]] = []
        for first_id, first_geometry in geometry_by_id.items():
            records = first_geometry.get("corner_radii", ())
            if not isinstance(records, list):
                continue
            first_ids = tuple(map(str, first_geometry.get("point_ids", ())))
            if len(first_ids) != 2:
                continue
            for record in records:
                if not isinstance(record, dict):
                    continue
                second_id = str(record.get("other_geometry_id", ""))
                vertex_id = str(record.get("vertex_id", ""))
                second_geometry = geometry_by_id.get(second_id)
                second_ids = (
                    tuple(map(str, second_geometry.get("point_ids", ())))
                    if second_geometry is not None
                    else ()
                )
                if (
                    len(second_ids) != 2
                    or vertex_id not in first_ids
                    or vertex_id not in second_ids
                ):
                    continue
                vertex = point_positions.get(vertex_id)
                first_outer = point_positions.get(
                    next(item for item in first_ids if item != vertex_id)
                )
                second_outer = point_positions.get(
                    next(item for item in second_ids if item != vertex_id)
                )
                if (
                    vertex is None
                    or first_outer is None
                    or second_outer is None
                ):
                    continue
                evaluated = evaluate_corner_radius(
                    tuple(vertex),
                    tuple(first_outer),
                    tuple(second_outer),
                    float(record.get("radius", 0.0)),
                )
                if evaluated is None:
                    continue
                corner_trim_points[(first_id, vertex_id)] = (
                    evaluated.first_tangent
                )
                corner_trim_points[(second_id, vertex_id)] = (
                    evaluated.second_tangent
                )
                corner_arcs.append(evaluated.arc_points)
        for entity in entities:
            if (
                not isinstance(entity, dict)
                or entity.get("type") in ("point", "construction")
                or entity.get("role") == "construction"
            ):
                continue
            points = entity.get("points", ())
            if isinstance(entity.get("point_ids"), list):
                points = [
                    list(
                        corner_trim_points.get(
                            (
                                str(entity.get("id", "")),
                                point_id,
                            ),
                            tuple(point_positions[point_id]),
                        )
                    )
                    for point_id in map(str, entity["point_ids"])
                    if point_id in point_positions
                ]
            if not isinstance(points, list):
                continue
            curve = None
            try:
                if entity.get("type") == "circle" and len(points) == 1:
                    center = points[0]
                    radius = float(entity.get("radius", 0.0))
                    if radius > 1.0e-12:
                        circle = gp_Circ(
                            gp_Ax2(
                                gp_Pnt(
                                    float(center[0]),
                                    float(center[1]),
                                    0.0,
                                ),
                                gp_Dir(0.0, 0.0, 1.0),
                            ),
                            radius,
                        )
                        builder.Add(
                            compound,
                            BRepBuilderAPI_MakeEdge(circle).Edge(),
                        )
                        edge_count += 1
                        continue
                elif entity.get("type") == "arc" and len(points) >= 3:
                    if entity.get("arc_mode") == "center":
                        sampled = center_arc_points(
                            tuple(points[0]),
                            tuple(points[1]),
                            tuple(points[2]),
                            clockwise=bool(
                                entity.get("clockwise", False)
                            ),
                        )
                        if len(sampled) < 3:
                            continue
                        points = [
                            sampled[0],
                            sampled[len(sampled) // 2],
                            sampled[-1],
                        ]
                    curve = GC_MakeArcOfCircle(
                        gp_Pnt(float(points[0][0]), float(points[0][1]), 0.0),
                        gp_Pnt(float(points[1][0]), float(points[1][1]), 0.0),
                        gp_Pnt(float(points[2][0]), float(points[2][1]), 0.0),
                    ).Value()
                elif entity.get("type") == "spline" and len(points) >= 2:
                    periodic = len(points) >= 4 and points[0] == points[-1]
                    interpolation_points = points[:-1] if periodic else points
                    poles = TColgp_HArray1OfPnt(1, len(interpolation_points))
                    for point_index, point in enumerate(
                        interpolation_points, 1
                    ):
                        poles.SetValue(
                            point_index,
                            gp_Pnt(
                                float(point[0]),
                                float(point[1]),
                                0.0,
                            ),
                        )
                    interpolation = GeomAPI_Interpolate(
                        poles,
                        periodic,
                        1.0e-7,
                    )
                    interpolation.Perform()
                    if interpolation.IsDone():
                        curve = interpolation.Curve()
                elif entity.get("type") in ("ellipse", "elliptical_arc"):
                    required = 3 if entity.get("type") == "ellipse" else 5
                    if len(points) >= required:
                        edge = _make_exact_ellipse_edge(
                            tuple(tuple(point) for point in points[:required]),
                            arc=entity.get("type") == "elliptical_arc",
                            clockwise=bool(entity.get("clockwise", False)),
                        )
                        builder.Add(compound, edge)
                        edge_count += 1
                        continue
            except (RuntimeError, ValueError, TypeError):
                curve = None
            if curve is not None:
                builder.Add(
                    compound,
                    BRepBuilderAPI_MakeEdge(curve).Edge(),
                )
                edge_count += 1
                continue
            for first, second in zip(points, points[1:]):
                if (
                    not isinstance(first, list)
                    or not isinstance(second, list)
                    or len(first) < 2
                    or len(second) < 2
                ):
                    continue
                edge = BRepBuilderAPI_MakeEdge(
                    gp_Pnt(float(first[0]), float(first[1]), 0.0),
                    gp_Pnt(float(second[0]), float(second[1]), 0.0),
                ).Edge()
                builder.Add(compound, edge)
                edge_count += 1
        for arc_points in corner_arcs:
            try:
                curve = GC_MakeArcOfCircle(
                    gp_Pnt(*arc_points[0], 0.0),
                    gp_Pnt(*arc_points[len(arc_points) // 2], 0.0),
                    gp_Pnt(*arc_points[-1], 0.0),
                ).Value()
                builder.Add(
                    compound,
                    BRepBuilderAPI_MakeEdge(curve).Edge(),
                )
                edge_count += 1
            except (RuntimeError, ValueError, TypeError):
                continue
        if edge_count == 0:
            return None
        transform = (
            parent_transform
            or coordinate_system_transform(parent.coordinate_system)
        )
        plane = str(sketch.parameters.get("plane", "xy"))
        effective_offset = float(
            sketch.parameters.get("profile_offset", 0.0)
            if plane_offset is None
            else plane_offset
        )
        plane_transform = multiply_transforms(
            sketch_plane_offset_transform(plane, effective_offset),
            sketch_plane_transform(plane),
        )
        return transform_shape(
            compound,
            multiply_transforms(transform, plane_transform),
        )
    if sketch.parameters.get("profile") != "circle":
        return None

    plane = str(sketch.parameters.get("plane", "xy"))
    diameter = float(sketch.parameters.get("diameter", 10.0))
    normal = {
        "xy": gp_Dir(0.0, 0.0, 1.0),
        "yz": gp_Dir(1.0, 0.0, 0.0),
        "xz": gp_Dir(0.0, 1.0, 0.0),
    }.get(plane, gp_Dir(0.0, 0.0, 1.0))

    circle = gp_Circ(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), normal), diameter / 2.0)
    shape = BRepBuilderAPI_MakeEdge(circle).Edge()
    effective_offset = float(
        sketch.parameters.get("profile_offset", 0.0)
        if plane_offset is None
        else plane_offset
    )
    transform = multiply_transforms(
        parent_transform
        or coordinate_system_transform(parent.coordinate_system),
        sketch_plane_offset_transform(plane, effective_offset),
    )
    return transform_shape(shape, transform)


def make_datum_axis_shape(
    axis: ZimaEntity,
    parent_transform: tuple[tuple[float, float, float, float], ...],
):
    if axis.kind != EntityKind.AXIS or axis.parameters.get("display_style") != "centerline":
        return None
    length = max(0.001, float(axis.parameters.get("length", 50.0)))
    direction = {
        "x": (1.0, 0.0, 0.0),
        "y": (0.0, 1.0, 0.0),
        "z": (0.0, 0.0, 1.0),
    }.get(str(axis.parameters.get("axis", "z")), (0.0, 0.0, 1.0))
    half = length / 2.0
    origin = tuple(
        float(axis.parameters.get(f"origin_{coordinate}", 0.0))
        for coordinate in ("x", "y", "z")
    )
    start = gp_Pnt(*(origin[index] - half * direction[index] for index in range(3)))
    end = gp_Pnt(*(origin[index] + half * direction[index] for index in range(3)))
    return transform_shape(BRepBuilderAPI_MakeEdge(start, end).Edge(), parent_transform)


def find_child_entity(parent: ZimaEntity, entity_id: str) -> ZimaEntity | None:
    if parent.entity_id == entity_id:
        return parent
    for child in parent.children:
        found = find_child_entity(child, entity_id)
        if found is not None:
            return found
    return None


def find_parent_entity(parent: ZimaEntity, entity_id: str) -> ZimaEntity | None:
    for child in parent.children:
        if child.entity_id == entity_id:
            return parent
        found = find_parent_entity(child, entity_id)
        if found is not None:
            return found
    return None


def next_child_name(parent: ZimaEntity, prefix: str) -> str:
    existing = {child.name for child in parent.children}
    index = 1
    while True:
        name = f"{prefix}{index:03}"
        if name not in existing:
            return name
        index += 1


def delete_child_entity(parent: ZimaEntity, entity_id: str) -> bool:
    for index, child in enumerate(parent.children):
        if child.entity_id == entity_id:
            if child.locked:
                return False
            del parent.children[index]
            return True
        if delete_child_entity(child, entity_id):
            return True
    return False


def create_origin_object(
    owner_id: str | None = None,
    scope: OriginScope = OriginScope.LOCAL,
) -> ZimaEntity:
    origin = ZimaEntity(
        name={
            OriginScope.PART: "Part Origin",
            OriginScope.ASSEMBLY: "Assembly Origin",
            OriginScope.CONTAINER: "Container Origin",
            OriginScope.LOCAL: "Local Origin",
        }[scope],
        kind=EntityKind.ORIGIN,
        combine_mode=CombineMode.NONE,
        entity_id=(
            f"{owner_id}:origin"
            if owner_id is not None
            else uuid4().hex
        ),
        locked=True,
        tree_exposure=TreeExposure.INTERNAL,
        origin_scope=scope,
    )
    add_origin_children(origin)
    return origin


def add_coordinate_system_children(parent: ZimaEntity) -> None:
    if parent.kind != EntityKind.CONTAINER:
        return
    if not any(child.kind == EntityKind.ORIGIN for child in parent.children):
        parent.add_child(
            create_origin_object(
                parent.entity_id,
                OriginScope.CONTAINER,
            )
        )


def add_origin_children(parent: ZimaEntity) -> None:
    parent.add_child(
        ZimaEntity(
            name="Point 0,0,0",
            kind=EntityKind.POINT,
            entity_id=f"{parent.entity_id}:point",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="X Axis",
            kind=EntityKind.AXIS,
            parameters={"axis": "x"},
            entity_id=f"{parent.entity_id}:axis:x",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="Y Axis",
            kind=EntityKind.AXIS,
            parameters={"axis": "y"},
            entity_id=f"{parent.entity_id}:axis:y",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="Z Axis",
            kind=EntityKind.AXIS,
            parameters={"axis": "z"},
            entity_id=f"{parent.entity_id}:axis:z",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="XY Plane",
            kind=EntityKind.PLANE,
            parameters={"plane": "xy"},
            entity_id=f"{parent.entity_id}:plane:xy",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="YZ Plane",
            kind=EntityKind.PLANE,
            parameters={"plane": "yz"},
            entity_id=f"{parent.entity_id}:plane:yz",
            locked=True,
        )
    )
    parent.add_child(
        ZimaEntity(
            name="XZ Plane",
            kind=EntityKind.PLANE,
            parameters={"plane": "xz"},
            entity_id=f"{parent.entity_id}:plane:xz",
            locked=True,
        )
    )


def make_origin_shapes(
    size: float = ORIGIN_WIDGET_SIZE,
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0),
    plane_scale: float = 1.0,
):
    ox, oy, oz = origin
    point = BRepPrimAPI_MakeSphere(gp_Pnt(ox, oy, oz), 11.0).Shape()
    axis_radius = 4.2
    end_sphere_radius = 7.0
    x_axis = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(ox, oy, oz), gp_Dir(1.0, 0.0, 0.0)),
        axis_radius,
        size,
    ).Shape()
    x_end = BRepPrimAPI_MakeSphere(gp_Pnt(ox + size, oy, oz), end_sphere_radius).Shape()
    y_axis = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(ox, oy, oz), gp_Dir(0.0, 1.0, 0.0)),
        axis_radius,
        size,
    ).Shape()
    y_end = BRepPrimAPI_MakeSphere(gp_Pnt(ox, oy + size, oz), end_sphere_radius).Shape()
    z_axis = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(ox, oy, oz), gp_Dir(0.0, 0.0, 1.0)),
        axis_radius,
        size,
    ).Shape()
    z_end = BRepPrimAPI_MakeSphere(gp_Pnt(ox, oy, oz + size), end_sphere_radius).Shape()
    half = size / 3.0 * plane_scale

    return {
        "point": point,
        "x_axis": [x_axis, x_end],
        "y_axis": [y_axis, y_end],
        "z_axis": [z_axis, z_end],
        "xy_plane": make_plane_wire(
            [
                gp_Pnt(ox - half, oy - half, oz),
                gp_Pnt(ox + half, oy - half, oz),
                gp_Pnt(ox + half, oy + half, oz),
                gp_Pnt(ox - half, oy + half, oz),
            ]
        ),
        "yz_plane": make_plane_wire(
            [
                gp_Pnt(ox, oy - half, oz - half),
                gp_Pnt(ox, oy + half, oz - half),
                gp_Pnt(ox, oy + half, oz + half),
                gp_Pnt(ox, oy - half, oz + half),
            ]
        ),
        "xz_plane": make_plane_wire(
            [
                gp_Pnt(ox - half, oy, oz - half),
                gp_Pnt(ox + half, oy, oz - half),
                gp_Pnt(ox + half, oy, oz + half),
                gp_Pnt(ox - half, oy, oz + half),
            ]
        ),
    }


def make_plane_label_points(
    size: float = ORIGIN_WIDGET_SIZE,
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0),
    plane_scale: float = 1.0,
):
    ox, oy, oz = origin
    half = size / 3.0 * plane_scale
    return {
        "xy_plane": gp_Pnt(ox + half, oy + half, oz),
        "yz_plane": gp_Pnt(ox, oy + half, oz + half),
        "xz_plane": gp_Pnt(ox + half, oy, oz + half),
    }


def make_axis_label_points(
    size: float = ORIGIN_WIDGET_SIZE,
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0),
):
    ox, oy, oz = origin
    label_distance = size * 1.08
    return {
        "x_axis": gp_Pnt(ox + label_distance, oy, oz),
        "y_axis": gp_Pnt(ox, oy + label_distance, oz),
        "z_axis": gp_Pnt(ox, oy, oz + label_distance),
    }


def make_plane_wire(points: list[gp_Pnt]):
    polygon = BRepBuilderAPI_MakePolygon()
    for point in points:
        polygon.Add(point)
    polygon.Close()
    return polygon.Wire()


def create_empty_part() -> PartDocument:
    return PartDocument()


def create_empty_assembly() -> PartDocument:
    settings = default_document_settings()
    settings["type"] = "assembly"
    settings["body_visible"] = "true"
    return PartDocument(
        document_settings=settings,
        root=ZimaEntity(
            name="Assembly001",
            kind=EntityKind.PART,
            combine_mode=CombineMode.NONE,
        ),
    )


def create_empty_drawing() -> PartDocument:
    settings = default_document_settings()
    settings["type"] = "drawing"
    settings["drawing_sheets"] = json.dumps([{
        "id": str(uuid4()),
        "name": "List 1",
        "format": "A4",
        "orientation": "portrait",
        "views": [],
    }], separators=(",", ":"))
    document = PartDocument(
        document_settings=settings,
        root=ZimaEntity(
            name="Drawing001",
            kind=EntityKind.PART,
            combine_mode=CombineMode.NONE,
        ),
    )
    document.root.children = []
    return document
