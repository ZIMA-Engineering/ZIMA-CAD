from __future__ import annotations

import configparser
from pathlib import Path

from zima_cad.model import (
    CombineMode,
    CoordinateSystem,
    ObjectKind,
    PlaneOnFaceAttachment,
    PartDocument,
    ZimaObject,
    add_coordinate_system_children,
    create_empty_part,
)


def save_part_document(document: PartDocument, file_path: Path) -> None:
    config = configparser.ConfigParser()
    config.optionxform = str

    config["Document"] = document.document_settings
    config["Physical"] = document.physical_parameters
    config["UserParameters"] = {"Order": ", ".join(document.user_parameter_order)}
    config["UserParameterLabels"] = flatten_language_map(document.user_parameter_labels)
    config["UserParameterValues"] = flatten_language_map(document.user_parameter_values)

    objects = document.visible_objects()
    config["Objects"] = {"items": ",".join(obj.object_id for obj in objects)}

    for obj in objects:
        write_object(config, obj)

    file_path.parent.mkdir(parents=True, exist_ok=True)
    with file_path.open("w", encoding="utf-8") as stream:
        config.write(stream)


def load_part_document(file_path: Path) -> PartDocument:
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(file_path, encoding="utf-8")

    document = create_empty_part()
    if config.has_section("Document"):
        document.document_settings.update(dict(config["Document"]))
    if config.has_section("Physical"):
        document.physical_parameters.update(dict(config["Physical"]))
    if config.has_section("UserParameters"):
        load_user_parameters(config, document)

    object_ids = [
        item.strip()
        for item in config.get("Objects", "items", fallback="").split(",")
        if item.strip()
    ]

    for object_id in object_ids:
        section = f"Object.{object_id}"
        if not config.has_section(section):
            continue

        obj = read_object(config, section, object_id)
        document.root.add_child(obj)

    return document


def write_object(config: configparser.ConfigParser, obj: ZimaObject) -> None:
    x, y, z = obj.coordinate_system.origin
    rx, ry, rz = obj.coordinate_system.rotation
    section = f"Object.{obj.object_id}"
    config[section] = {
        "id": obj.object_id,
        "name": obj.name,
        "kind": obj.kind.value,
        "combine_mode": obj.combine_mode.value,
        "x": format_float(x),
        "y": format_float(y),
        "z": format_float(z),
        "rx": format_float(rx),
        "ry": format_float(ry),
        "rz": format_float(rz),
    }

    for key, value in obj.parameters.items():
        config[section][f"param.{key}"] = str(value)

    if obj.attachment is not None:
        attachment = obj.attachment
        config[section].update(
            {
                "attachment.type": "plane_on_face",
                "attachment.source_plane": attachment.source_plane,
                "attachment.target_object": attachment.target_object_id,
                "attachment.target_face_role": attachment.target_face_role,
                "attachment.primary_axis": attachment.primary_axis,
                "attachment.secondary_axis": attachment.secondary_axis,
                "attachment.active_axis": attachment.active_axis,
                "attachment.switch_angle": format_float(attachment.switch_angle),
                "attachment.flip_normal": str(attachment.flip_normal).lower(),
                "attachment.status": attachment.status,
            }
        )

    user_children = [child for child in obj.children if not child.locked]
    if user_children:
        config[f"Children.{obj.object_id}"] = {
            "items": ",".join(child.object_id for child in user_children)
        }
        for child in user_children:
            write_object(config, child)


def read_object(
    config: configparser.ConfigParser,
    section: str,
    object_id: str,
) -> ZimaObject:
    kind = ObjectKind(config.get(section, "kind", fallback=ObjectKind.OBJECT.value))
    obj = ZimaObject(
        name=config.get(section, "name", fallback=object_id),
        kind=kind,
        combine_mode=CombineMode(
            config.get(section, "combine_mode", fallback=CombineMode.NONE.value)
        ),
        coordinate_system=CoordinateSystem(
            origin=(
                config.getfloat(section, "x", fallback=0.0),
                config.getfloat(section, "y", fallback=0.0),
                config.getfloat(section, "z", fallback=0.0),
            ),
            rotation=(
                config.getfloat(section, "rx", fallback=0.0),
                config.getfloat(section, "ry", fallback=0.0),
                config.getfloat(section, "rz", fallback=0.0),
            ),
        ),
        object_id=config.get(section, "id", fallback=object_id),
    )

    for key, value in config[section].items():
        if key.startswith("param."):
            obj.parameters[key.removeprefix("param.")] = value

    if config.get(section, "attachment.type", fallback="") == "plane_on_face":
        obj.attachment = PlaneOnFaceAttachment(
            source_plane=config.get(section, "attachment.source_plane", fallback="xy"),
            target_object_id=config.get(section, "attachment.target_object", fallback=""),
            target_face_role=config.get(
                section, "attachment.target_face_role", fallback=""
            ),
            primary_axis=config.get(section, "attachment.primary_axis", fallback="x"),
            secondary_axis=config.get(section, "attachment.secondary_axis", fallback="y"),
            active_axis=config.get(section, "attachment.active_axis", fallback="x"),
            switch_angle=config.getfloat(
                section, "attachment.switch_angle", fallback=45.0
            ),
            flip_normal=config.getboolean(
                section, "attachment.flip_normal", fallback=False
            ),
            status=config.get(section, "attachment.status", fallback="resolved"),
        )

    if kind == ObjectKind.OBJECT:
        add_coordinate_system_children(obj)

    child_ids = [
        item.strip()
        for item in config.get(f"Children.{obj.object_id}", "items", fallback="").split(",")
        if item.strip()
    ]
    for child_id in child_ids:
        child_section = f"Object.{child_id}"
        if config.has_section(child_section):
            obj.add_child(read_object(config, child_section, child_id))

    return obj


def format_float(value: float) -> str:
    return f"{float(value):.12g}"


def flatten_language_map(values: dict[str, dict[str, str]]) -> dict[str, str]:
    flattened = {}
    for key, language_values in values.items():
        for language, value in language_values.items():
            flattened[language_key(key, language)] = str(value)
    return flattened


def language_key(key: str, language: str) -> str:
    if not language:
        return key
    return f"{key}\\{language}"


def split_language_key(key: str) -> tuple[str, str]:
    if "\\" not in key:
        return key, ""
    parameter_key, language = key.rsplit("\\", 1)
    return parameter_key, language


def load_user_parameters(
    config: configparser.ConfigParser,
    document: PartDocument,
) -> None:
    order_text = config.get("UserParameters", "Order", fallback="")
    if order_text:
        document.user_parameter_order = [
            item.strip() for item in order_text.split(",") if item.strip()
        ]
    else:
        legacy_values = dict(config["UserParameters"])
        document.user_parameter_order = list(legacy_values.keys())
        document.user_parameter_values = {
            key: {"": value} for key, value in legacy_values.items()
        }
        document.user_parameters = legacy_values

    if config.has_section("UserParameterLabels"):
        document.user_parameter_labels = read_language_map(config["UserParameterLabels"])

    if config.has_section("UserParameterValues"):
        document.user_parameter_values = read_language_map(config["UserParameterValues"])
        document.user_parameters = {
            key: values.get("", "")
            for key, values in document.user_parameter_values.items()
            if "" in values
        }

    ensure_user_parameter_keys(document)


def read_language_map(section) -> dict[str, dict[str, str]]:
    values: dict[str, dict[str, str]] = {}
    for raw_key, value in section.items():
        key, language = split_language_key(raw_key)
        values.setdefault(key, {})[language] = value
    return values


def ensure_user_parameter_keys(document: PartDocument) -> None:
    known_keys = set(document.user_parameter_order)
    known_keys.update(document.user_parameter_values)

    for key in known_keys:
        if key not in document.user_parameter_order:
            document.user_parameter_order.append(key)
        document.user_parameter_labels.setdefault(key, {})
        document.user_parameter_values.setdefault(key, {"": ""})
