from __future__ import annotations

import math
import json
from dataclasses import dataclass, field
from enum import Enum
from typing import Any
from uuid import uuid4

from OCC.Core.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCC.Core.BRepBuilderAPI import (
    BRepBuilderAPI_MakeEdge,
    BRepBuilderAPI_MakePolygon,
    BRepBuilderAPI_MakeVertex,
    BRepBuilderAPI_Transform,
)
from OCC.Core.BRep import BRep_Builder
from OCC.Core.GC import GC_MakeArcOfCircle
from OCC.Core.GeomAPI import GeomAPI_Interpolate
from OCC.Core.BRepOffsetAPI import BRepOffsetAPI_ThruSections
from OCC.Core.BRepPrimAPI import (
    BRepPrimAPI_MakeBox,
    BRepPrimAPI_MakeCone,
    BRepPrimAPI_MakeCylinder,
    BRepPrimAPI_MakeSphere,
    BRepPrimAPI_MakeWedge,
)
from OCC.Core.gp import gp_Ax2, gp_Circ, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec
from OCC.Core.TColgp import TColgp_HArray1OfPnt
from OCC.Core.TopoDS import TopoDS_Compound

from zima_cad.sketch_model import SketchModel, SketchModelError


ORIGIN_WIDGET_SIZE = 320.0
DOCUMENT_FORMAT_VERSION = "8"


def default_document_settings() -> dict[str, str]:
    return {
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
        "nazev": {"cs": "N\u00e1zev", "de": "Name", "en": "Name", "fr": "Nom"},
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
        solids = [entity for entity in entities if entity.kind in SOLID_KINDS]
        if len(points) > 1 or len(axes) > 1 or len(planes) > 1 or len(solids) > 1:
            return False
        if (
            len(points)
            + len(axes)
            + len(planes)
            + len(sketches)
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
            self.root.children.insert(
                0,
                create_origin_object("document", OriginScope.PART),
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
        plane: str = "xy",
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
        return primitive

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
        result_shape = None
        for obj in objects:
            result_shape = apply_object_to_shape(
                result_shape,
                obj,
                identity_transform(),
            )

        return result_shape

    def build_standalone_shape(self, obj: ZimaEntity):
        """Build one history object for source inspection, ignoring its first sign."""
        return apply_object_to_shape(
            None,
            obj,
            identity_transform(),
            accept_first_shape=True,
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
                    result_shape, item, identity_transform()
                )
        return result_shape

    def resolve_attachments(self) -> None:
        for obj in self.visible_objects():
            resolve_entity_attachments(self, obj)


def apply_object_to_shape(
    result_shape,
    obj: ZimaEntity,
    parent_transform: tuple[tuple[float, float, float, float], ...],
    accept_first_shape: bool = False,
):
    if obj.suppressed:
        return result_shape
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    shape = make_shape(obj)

    if shape is not None:
        shape = transform_shape(shape, world_transform)
        if obj.combine_mode == CombineMode.ADD or (
            accept_first_shape and result_shape is None
        ):
            if result_shape is None:
                result_shape = shape
            else:
                result_shape = BRepAlgoAPI_Fuse(result_shape, shape).Shape()
        elif obj.combine_mode == CombineMode.SUBTRACT and result_shape is not None:
            result_shape = BRepAlgoAPI_Cut(result_shape, shape).Shape()

    for child in obj.children:
        if child.locked or child.kind == EntityKind.SKETCH:
            continue
        result_shape = apply_object_to_shape(
            result_shape,
            child,
            world_transform,
            accept_first_shape=accept_first_shape,
        )

    return result_shape


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
        for entity in entities:
            if (
                not isinstance(entity, dict)
                or entity.get("type") in ("point", "construction")
            ):
                continue
            points = entity.get("points", ())
            if isinstance(entity.get("point_ids"), list):
                points = [
                    point_positions[point_id]
                    for point_id in map(str, entity["point_ids"])
                    if point_id in point_positions
                ]
            if not isinstance(points, list):
                continue
            curve = None
            try:
                if entity.get("type") == "circle" and len(points) == 2:
                    center = points[0]
                    circumference = points[1]
                    radius = math.hypot(
                        float(circumference[0]) - float(center[0]),
                        float(circumference[1]) - float(center[1]),
                    )
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
                    curve = GC_MakeArcOfCircle(
                        gp_Pnt(float(points[0][0]), float(points[0][1]), 0.0),
                        gp_Pnt(float(points[1][0]), float(points[1][1]), 0.0),
                        gp_Pnt(float(points[2][0]), float(points[2][1]), 0.0),
                    ).Value()
                elif entity.get("type") == "spline" and len(points) >= 2:
                    poles = TColgp_HArray1OfPnt(1, len(points))
                    for point_index, point in enumerate(points, 1):
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
                        False,
                        1.0e-7,
                    )
                    interpolation.Perform()
                    if interpolation.IsDone():
                        curve = interpolation.Curve()
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
        if edge_count == 0:
            return None
        transform = (
            parent_transform
            or coordinate_system_transform(parent.coordinate_system)
        )
        return transform_shape(compound, transform)
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
    transform = parent_transform or coordinate_system_transform(parent.coordinate_system)
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
    start = gp_Pnt(*(-half * value for value in direction))
    end = gp_Pnt(*(half * value for value in direction))
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
