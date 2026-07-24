from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Any
from uuid import uuid4

from OCC.Core.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCC.Core.BRepBuilderAPI import (
    BRepBuilderAPI_MakeEdge,
    BRepBuilderAPI_MakePolygon,
    BRepBuilderAPI_Transform,
)
from OCC.Core.BRepPrimAPI import (
    BRepPrimAPI_MakeBox,
    BRepPrimAPI_MakeCylinder,
    BRepPrimAPI_MakeSphere,
    BRepPrimAPI_MakeWedge,
)
from OCC.Core.gp import gp_Ax2, gp_Circ, gp_Dir, gp_Pnt, gp_Trsf


ORIGIN_WIDGET_SIZE = 320.0


def default_document_settings() -> dict[str, str]:
    return {
        "type": "part",
        "format_version": "3",
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


class ObjectKind(str, Enum):
    PART = "part"
    OBJECT = "object"
    ORIGIN = "origin"
    POINT = "point"
    AXIS = "axis"
    PLANE = "plane"
    SKETCH = "sketch"
    BOX = "box"
    CYLINDER = "cylinder"
    WEDGE = "wedge"


ENTITY_KINDS = frozenset(
    {
        ObjectKind.POINT,
        ObjectKind.AXIS,
        ObjectKind.SKETCH,
        ObjectKind.BOX,
        ObjectKind.CYLINDER,
        ObjectKind.WEDGE,
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
class ZimaObject:
    name: str
    kind: ObjectKind
    combine_mode: CombineMode = CombineMode.NONE
    coordinate_system: CoordinateSystem = field(default_factory=CoordinateSystem)
    parameters: dict[str, Any] = field(default_factory=dict)
    children: list["ZimaObject"] = field(default_factory=list)
    object_id: str = field(default_factory=lambda: uuid4().hex)
    locked: bool = False
    attachment: PlaneOnFaceAttachment | None = None

    def add_child(self, child: "ZimaObject") -> None:
        self.children.append(child)

    def entity_children(self) -> list["ZimaObject"]:
        return [
            child
            for child in self.children
            if not child.locked and child.kind in ENTITY_KINDS
        ]

    def can_accept_entity(self) -> bool:
        return self.kind == ObjectKind.OBJECT and not self.entity_children()


@dataclass
class PartDocument:
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
    root: ZimaObject = field(
        default_factory=lambda: ZimaObject(
            name="Part001",
            kind=ObjectKind.PART,
            combine_mode=CombineMode.NONE,
        )
    )

    def __post_init__(self) -> None:
        if not any(child.kind == ObjectKind.ORIGIN for child in self.root.children):
            self.root.children.insert(0, create_origin_object())

    def visible_objects(self) -> list[ZimaObject]:
        return [obj for obj in self.root.children if obj.kind != ObjectKind.ORIGIN]

    def find_object(self, object_id: str) -> ZimaObject | None:
        return find_child_object(self.root, object_id)

    def next_object_name(self) -> str:
        existing = {child.name for child in self.root.children}
        index = 1
        while True:
            name = f"Object{index:03}"
            if name not in existing:
                return name
            index += 1

    def create_object(self) -> ZimaObject:
        obj = ZimaObject(
            name=self.next_object_name(),
            kind=ObjectKind.OBJECT,
            combine_mode=CombineMode.NONE,
        )
        add_coordinate_system_children(obj)
        self.root.add_child(obj)
        return obj

    def delete_object(self, object_id: str) -> bool:
        return delete_child_object(self.root, object_id)

    def find_parent(self, object_id: str) -> ZimaObject | None:
        return find_parent_object(self.root, object_id)

    def find_owning_object(self, object_id: str) -> ZimaObject | None:
        parent = self.find_parent(object_id)
        while parent is not None and parent.kind != ObjectKind.OBJECT:
            parent = self.find_parent(parent.object_id)
        return parent

    def create_sketch_on_plane(self, plane_id: str) -> ZimaObject | None:
        parent = self.find_owning_object(plane_id)
        plane = self.find_object(plane_id)
        if parent is None or plane is None:
            return None
        if (
            parent.kind != ObjectKind.OBJECT
            or plane.kind != ObjectKind.PLANE
            or not parent.can_accept_entity()
        ):
            return None

        return self.create_sketch(parent.object_id, str(plane.parameters.get("plane", "")))

    def create_sketch(self, parent_id: str, plane: str = "xy") -> ZimaObject | None:
        parent = self.find_object(parent_id)
        if parent is None or not parent.can_accept_entity():
            return None
        if plane not in {"xy", "yz", "xz"}:
            return None

        sketch = ZimaObject(
            name=next_child_name(parent, "Sketch"),
            kind=ObjectKind.SKETCH,
            combine_mode=CombineMode.NONE,
            parameters={
                "plane": plane,
                "profile": "circle",
                "diameter": "10",
                "unit": "mm",
            },
        )
        parent.add_child(sketch)
        return sketch

    def create_datum_point(self, parent_id: str) -> ZimaObject | None:
        parent = self.find_object(parent_id)
        if parent is None or not parent.can_accept_entity():
            return None
        point = ZimaObject(
            name=next_child_name(parent, "Point"),
            kind=ObjectKind.POINT,
        )
        parent.add_child(point)
        return point

    def create_datum_axis(self, parent_id: str) -> ZimaObject | None:
        parent = self.find_object(parent_id)
        if parent is None or not parent.can_accept_entity():
            return None
        axis = ZimaObject(
            name=next_child_name(parent, "Axis"),
            kind=ObjectKind.AXIS,
            parameters={
                "display_style": "centerline",
                "axis": "z",
                "length": "100",
                "unit": "mm",
            },
        )
        parent.add_child(axis)
        return axis

    def create_cube(self, source_id: str) -> ZimaObject | None:
        source = self.find_object(source_id)
        if source is None:
            return None

        parent = source
        if source.kind == ObjectKind.POINT:
            source_parent = self.find_owning_object(source.object_id)
            if source_parent is None:
                return None
            parent = source_parent

        if not parent.can_accept_entity():
            return None

        cube = ZimaObject(
            name=next_child_name(parent, "Cube"),
            kind=ObjectKind.BOX,
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

    def create_wedge(self, source_id: str) -> ZimaObject | None:
        source = self.find_object(source_id)
        if source is None:
            return None

        parent = source
        if source.kind == ObjectKind.POINT:
            source_parent = self.find_owning_object(source.object_id)
            if source_parent is None:
                return None
            parent = source_parent

        if not parent.can_accept_entity():
            return None

        wedge = ZimaObject(
            name=next_child_name(parent, "Wedge"),
            kind=ObjectKind.WEDGE,
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

    def rebuild_shape(self):
        self.resolve_attachments()
        result_shape = None

        for obj in self.visible_objects():
            result_shape = apply_object_to_shape(result_shape, obj, identity_transform())

        return result_shape

    def resolve_attachments(self) -> None:
        for obj in self.visible_objects():
            resolve_object_attachments(self, obj)


def apply_object_to_shape(
    result_shape,
    obj: ZimaObject,
    parent_transform: tuple[tuple[float, float, float, float], ...],
):
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    shape = make_shape(obj)

    if shape is not None:
        shape = transform_shape(shape, world_transform)
        if obj.combine_mode == CombineMode.ADD:
            if result_shape is None:
                result_shape = shape
            else:
                result_shape = BRepAlgoAPI_Fuse(result_shape, shape).Shape()
        elif obj.combine_mode == CombineMode.SUBTRACT and result_shape is not None:
            result_shape = BRepAlgoAPI_Cut(result_shape, shape).Shape()

    for child in obj.children:
        if child.locked or child.kind == ObjectKind.SKETCH:
            continue
        result_shape = apply_object_to_shape(result_shape, child, world_transform)

    return result_shape


def make_shape(obj: ZimaObject):
    x, y, z = (0.0, 0.0, 0.0)

    if obj.kind == ObjectKind.BOX:
        length = float(obj.parameters.get("length", 100.0))
        width = float(obj.parameters.get("width", 60.0))
        height = float(obj.parameters.get("height", 20.0))
        return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), length, width, height).Shape()

    if obj.kind == ObjectKind.CYLINDER:
        diameter = float(obj.parameters.get("diameter", 20.0))
        height = float(obj.parameters.get("height", 40.0))
        axis = gp_Ax2(gp_Pnt(x, y, z), gp_Dir(0.0, 0.0, 1.0))
        return BRepPrimAPI_MakeCylinder(axis, diameter / 2.0, height).Shape()

    if obj.kind == ObjectKind.WEDGE:
        length = float(obj.parameters.get("length", 100.0))
        width = float(obj.parameters.get("width", 60.0))
        height = float(obj.parameters.get("height", 50.0))
        top_offset = float(obj.parameters.get("top_offset", 50.0))
        top_offset = max(0.0, min(length, top_offset))
        return BRepPrimAPI_MakeWedge(length, width, height, top_offset).Shape()

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


def object_world_transform(
    document: PartDocument,
    object_id: str,
) -> tuple[tuple[float, float, float, float], ...] | None:
    def visit(obj: ZimaObject, parent_transform):
        world_transform = multiply_transforms(
            parent_transform,
            coordinate_system_transform(obj.coordinate_system),
        )
        if obj.object_id == object_id:
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
    box: ZimaObject,
    role: str,
) -> tuple[tuple[float, float, float], tuple[float, float, float]] | None:
    if box.kind != ObjectKind.BOX:
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
    world_transform = object_world_transform(document, box.object_id)
    if local_frame is None or world_transform is None:
        return None
    point = transform_point(world_transform, local_frame[0])
    normal = normalized(transform_vector(world_transform, local_frame[1]))
    return (point, normal) if normal is not None else None


def wedge_face_frame(
    document: PartDocument,
    wedge: ZimaObject,
    role: str,
) -> tuple[tuple[float, float, float], tuple[float, float, float]] | None:
    if wedge.kind != ObjectKind.WEDGE:
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
    world_transform = object_world_transform(document, wedge.object_id)
    if local_frame is None or world_transform is None:
        return None
    point = transform_point(world_transform, local_frame[0])
    normal = normalized(transform_vector(world_transform, local_frame[1]))
    return (point, normal) if normal is not None else None


def solid_face_frames(
    document: PartDocument,
    solid: ZimaObject,
) -> dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]]:
    roles = (
        ("x_min", "x_max", "y_min", "y_max", "z_min", "z_max")
        if solid.kind == ObjectKind.BOX
        else ("x_min", "slope", "y_min", "y_max", "z_min", "z_max")
        if solid.kind == ObjectKind.WEDGE
        else ()
    )
    frame_function = box_face_frame if solid.kind == ObjectKind.BOX else wedge_face_frame
    frames = {}
    for role in roles:
        frame = frame_function(document, solid, role)
        if frame is not None:
            frames[role] = frame
    return frames


def resolve_object_attachments(document: PartDocument, obj: ZimaObject) -> None:
    if obj.attachment is not None:
        resolve_plane_on_face_attachment(document, obj)
    for child in obj.children:
        if not child.locked:
            resolve_object_attachments(document, child)


def resolve_plane_on_face_attachment(document: PartDocument, obj: ZimaObject) -> bool:
    attachment = obj.attachment
    if attachment is None:
        return True
    target = document.find_object(attachment.target_object_id)
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
    parent: ZimaObject,
    sketch: ZimaObject,
    parent_transform: tuple[tuple[float, float, float, float], ...] | None = None,
):
    if sketch.kind != ObjectKind.SKETCH:
        return None
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
    axis: ZimaObject,
    parent_transform: tuple[tuple[float, float, float, float], ...],
):
    if axis.kind != ObjectKind.AXIS or axis.parameters.get("display_style") != "centerline":
        return None
    length = max(0.001, float(axis.parameters.get("length", 100.0)))
    direction = {
        "x": (1.0, 0.0, 0.0),
        "y": (0.0, 1.0, 0.0),
        "z": (0.0, 0.0, 1.0),
    }.get(str(axis.parameters.get("axis", "z")), (0.0, 0.0, 1.0))
    half = length / 2.0
    start = gp_Pnt(*(-half * value for value in direction))
    end = gp_Pnt(*(half * value for value in direction))
    return transform_shape(BRepBuilderAPI_MakeEdge(start, end).Edge(), parent_transform)


def find_child_object(parent: ZimaObject, object_id: str) -> ZimaObject | None:
    if parent.object_id == object_id:
        return parent
    for child in parent.children:
        found = find_child_object(child, object_id)
        if found is not None:
            return found
    return None


def find_parent_object(parent: ZimaObject, object_id: str) -> ZimaObject | None:
    for child in parent.children:
        if child.object_id == object_id:
            return parent
        found = find_parent_object(child, object_id)
        if found is not None:
            return found
    return None


def next_child_name(parent: ZimaObject, prefix: str) -> str:
    existing = {child.name for child in parent.children}
    index = 1
    while True:
        name = f"{prefix}{index:03}"
        if name not in existing:
            return name
        index += 1


def delete_child_object(parent: ZimaObject, object_id: str) -> bool:
    for index, child in enumerate(parent.children):
        if child.object_id == object_id:
            if child.locked:
                return False
            del parent.children[index]
            return True
        if delete_child_object(child, object_id):
            return True
    return False


def create_origin_object() -> ZimaObject:
    origin = ZimaObject(
        name="Origin",
        kind=ObjectKind.ORIGIN,
        combine_mode=CombineMode.NONE,
        locked=True,
    )
    add_origin_children(origin)
    return origin


def add_coordinate_system_children(parent: ZimaObject) -> None:
    if parent.kind != ObjectKind.OBJECT:
        return
    if not any(child.kind == ObjectKind.ORIGIN for child in parent.children):
        parent.add_child(create_origin_object())


def add_origin_children(parent: ZimaObject) -> None:
    parent.add_child(
        ZimaObject(name="Point 0,0,0", kind=ObjectKind.POINT, locked=True)
    )
    parent.add_child(
        ZimaObject(
            name="X Axis",
            kind=ObjectKind.AXIS,
            parameters={"axis": "x"},
            locked=True,
        )
    )
    parent.add_child(
        ZimaObject(
            name="Y Axis",
            kind=ObjectKind.AXIS,
            parameters={"axis": "y"},
            locked=True,
        )
    )
    parent.add_child(
        ZimaObject(
            name="Z Axis",
            kind=ObjectKind.AXIS,
            parameters={"axis": "z"},
            locked=True,
        )
    )
    parent.add_child(
        ZimaObject(
            name="XY Plane",
            kind=ObjectKind.PLANE,
            parameters={"plane": "xy"},
            locked=True,
        )
    )
    parent.add_child(
        ZimaObject(
            name="YZ Plane",
            kind=ObjectKind.PLANE,
            parameters={"plane": "yz"},
            locked=True,
        )
    )
    parent.add_child(
        ZimaObject(
            name="XZ Plane",
            kind=ObjectKind.PLANE,
            parameters={"plane": "xz"},
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
