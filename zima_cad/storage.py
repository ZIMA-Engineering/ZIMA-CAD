from __future__ import annotations

import configparser
import io
import math
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
    default_user_parameter_labels,
)
from zima_cad.versioned_io import write_text_versioned


class ObjectEntityLimitError(ValueError):
    def __init__(self, object_name: str, entity_names: list[str]) -> None:
        self.object_name = object_name
        self.entity_names = entity_names
        super().__init__(
            f"Object {object_name!r} contains an invalid entity combination: "
            + ", ".join(entity_names)
        )


def save_part_document(document: PartDocument, file_path: Path) -> None:
    validate_object_entities(document)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str

    config["Document"] = document.document_settings
    config["DocumentUnits"] = document.document_units
    config["DocumentPrecision"] = document.document_precision
    material_name = document.physical_parameters.get("MATERIAL_NAME", "")
    config["Material"] = {"Name": material_name}
    config["MaterialProperties"] = {
        key: value
        for key, value in document.physical_parameters.items()
        if key != "MATERIAL_NAME"
    }
    if document.physical_parameter_units:
        config["MaterialUnits"] = document.physical_parameter_units
    material_descriptions = flatten_language_map(
        document.material_parameter_descriptions
    )
    if material_descriptions:
        config["MaterialDescriptions"] = material_descriptions
    config["UserParameters"] = {"Order": ", ".join(document.user_parameter_order)}
    config["UserParameterLabels"] = flatten_language_map(document.user_parameter_labels)
    config["UserParameterValues"] = flatten_language_map(document.user_parameter_values)

    objects = document.visible_objects()
    config["Objects"] = {"items": ",".join(obj.object_id for obj in objects)}

    for obj in objects:
        write_object(config, obj)

    buffer = io.StringIO()
    config.write(buffer)
    write_text_versioned(
        file_path,
        buffer.getvalue().rstrip() + "\n",
        validator=load_part_document,
    )


def load_part_document(file_path: Path) -> PartDocument:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(file_path, encoding="utf-8")

    document = create_empty_part()
    if config.has_section("Document"):
        document.document_settings.update(dict(config["Document"]))
        legacy_unit_names = {
            "units": "Length",
            "angle_units": "Angle",
        }
        for legacy_name, unit_name in legacy_unit_names.items():
            if legacy_name in document.document_settings:
                document.document_units[unit_name] = document.document_settings.pop(
                    legacy_name
                )
        for precision_name in (
            "linear_tolerance",
            "angular_tolerance",
            "mesh_deflection",
        ):
            if precision_name in document.document_settings:
                document.document_precision[precision_name] = (
                    document.document_settings.pop(precision_name)
                )
    if config.has_section("DocumentUnits"):
        document.document_units.update(dict(config["DocumentUnits"]))
    if config.has_section("DocumentPrecision"):
        document.document_precision.update(dict(config["DocumentPrecision"]))
    document.document_settings["format_version"] = "3"
    if config.has_section("Material") or config.has_section("MaterialProperties"):
        material_parameters = {
            "MATERIAL_NAME": config.get("Material", "Name", fallback="")
        }
        if config.has_section("MaterialProperties"):
            material_parameters.update(dict(config["MaterialProperties"]))
        document.physical_parameters = normalize_physical_parameters(
            material_parameters
        )
    elif config.has_section("Physical"):
        document.physical_parameters = normalize_physical_parameters(
            dict(config["Physical"])
        )
    if config.has_section("MaterialUnits"):
        document.physical_parameter_units = normalize_material_units(
            dict(config["MaterialUnits"])
        )
    elif config.has_section("PhysicalUnits"):
        document.physical_parameter_units = normalize_material_units(
            dict(config["PhysicalUnits"])
        )
    if config.has_section("MaterialDescriptions"):
        document.material_parameter_descriptions = normalize_material_descriptions(
            read_language_map(config["MaterialDescriptions"])
        )
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

    validate_object_entities(document)
    return document


def normalize_physical_parameters(parameters: dict[str, str]) -> dict[str, str]:
    legacy_names = {
        "material_name": "MATERIAL_NAME",
        "density": "MASS_DENSITY",
        "poisson_ratio": "POISSON_RATIO",
        "youngs_modulus": "YOUNG_MODULUS",
        "thermal_expansion": "THERMAL_EXPANSION_COEFFICIENT",
        "specific_heat_capacity": "SPECIFIC_HEAT",
        "thermal_conductivity": "THERMAL_CONDUCTIVITY",
        "sheet_k_factor": "SHEETMETAL_K_FACTOR",
    }
    normalized = dict(parameters)
    for legacy_name, canonical_name in legacy_names.items():
        legacy_value = normalized.pop(legacy_name, "")
        if legacy_value and not normalized.get(canonical_name):
            normalized[canonical_name] = legacy_value
    y_factor = normalized.pop("INITIAL_BEND_Y_FACTOR", "")
    if y_factor and not normalized.get("SHEETMETAL_K_FACTOR"):
        try:
            normalized["SHEETMETAL_K_FACTOR"] = (
                f"{float(y_factor) * 2.0 / math.pi:.12g}"
            )
        except ValueError:
            pass
    normalized.pop("BEND_TABLE", None)
    normalized.pop("material_source", None)
    return normalized


def normalize_material_units(units: dict[str, str]) -> dict[str, str]:
    normalized = dict(units)
    if "INITIAL_BEND_Y_FACTOR" in normalized:
        normalized.setdefault(
            "SHEETMETAL_K_FACTOR",
            normalized.pop("INITIAL_BEND_Y_FACTOR"),
        )
    normalized.pop("BEND_TABLE", None)
    normalized.pop("material_source", None)
    normalized.pop("MATERIAL_NAME", None)
    normalized.pop("material_name", None)
    return normalized


def normalize_material_descriptions(
    descriptions: dict[str, dict[str, str]],
) -> dict[str, dict[str, str]]:
    normalized = dict(descriptions)
    if "material_name" in normalized:
        normalized.setdefault("MATERIAL_NAME", normalized.pop("material_name"))
    if "INITIAL_BEND_Y_FACTOR" in normalized:
        normalized.setdefault(
            "SHEETMETAL_K_FACTOR",
            normalized.pop("INITIAL_BEND_Y_FACTOR"),
        )
    normalized.pop("BEND_TABLE", None)
    normalized.pop("material_source", None)
    return normalized


def validate_object_entities(document: PartDocument) -> None:
    for obj in walk_objects(document.root):
        if obj.kind != ObjectKind.OBJECT:
            continue
        entities = obj.entity_children()
        if not obj.has_valid_entity_combination():
            raise ObjectEntityLimitError(obj.name, [entity.name for entity in entities])


def walk_objects(obj: ZimaObject):
    yield obj
    for child in obj.children:
        yield from walk_objects(child)


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
        for language, label in default_user_parameter_labels().get(key, {}).items():
            document.user_parameter_labels[key].setdefault(language, label)
