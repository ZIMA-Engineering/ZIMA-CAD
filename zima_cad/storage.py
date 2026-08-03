from __future__ import annotations

import configparser
import io
import json
import math
from pathlib import Path

from zima_cad.model import (
    CombineMode,
    ContainerType,
    CoordinateSystem,
    DOCUMENT_FORMAT_VERSION,
    EntityKind,
    OriginScope,
    PlaneOnFaceAttachment,
    PartDocument,
    TreeExposure,
    ZimaEntity,
    add_coordinate_system_children,
    create_empty_assembly,
    create_empty_drawing,
    create_empty_part,
    default_user_parameter_labels,
)
from zima_cad.sketch_model import SketchModel, SketchModelError
from zima_cad.versioned_io import write_text_versioned


class ContainerEntityLimitError(ValueError):
    def __init__(self, container_name: str, entity_names: list[str]) -> None:
        self.container_name = container_name
        self.entity_names = entity_names
        super().__init__(
            f"Container {container_name!r} contains an invalid entity combination: "
            + ", ".join(entity_names)
        )


def save_part_document(document: PartDocument, file_path: Path) -> None:
    document.source_file_path = file_path.resolve()
    validate_container_entities(document)
    validate_sketch_data(document)
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

    containers = document.visible_objects()
    config["Containers"] = {
        "items": ",".join(container.entity_id for container in containers)
    }

    for container in containers:
        write_entity(config, container)

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
    if (
        config.get("Document", "format_version", fallback="")
        != DOCUMENT_FORMAT_VERSION
    ):
        raise ValueError(
            "Unsupported document format; expected format "
            f"{DOCUMENT_FORMAT_VERSION}."
        )

    document_type = config.get("Document", "type", fallback="part")
    document = (
        create_empty_assembly()
        if document_type == "assembly"
        else create_empty_drawing()
        if document_type == "drawing"
        else create_empty_part()
    )
    document.source_file_path = file_path.resolve()
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
    document.document_settings["format_version"] = DOCUMENT_FORMAT_VERSION
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

    container_ids = [
        item.strip()
        for item in config.get("Containers", "items", fallback="").split(",")
        if item.strip()
    ]

    for container_id in container_ids:
        section = f"Container.{container_id}"
        if not config.has_section(section):
            continue

        obj = read_entity(config, section, container_id)
        document.root.add_child(obj)

    # Format 6 uses one automatic internal solid result. Explicit format-5 Body
    # snapshots are redundant because their source objects remain in the history.
    document.root.children = [
        obj for obj in document.root.children
        if obj.kind != EntityKind.BODY
    ]
    reconnect_history_result_references(document)
    migrate_missing_system_references(document)
    validate_container_entities(document)
    validate_sketch_data(document)
    return document


def reconnect_history_result_references(document: PartDocument) -> None:
    """Bind persisted automatic-Body references to the new runtime Part ID."""
    for obj in walk_entities(document.root):
        raw_references = obj.parameters.get("constraint_refs")
        if raw_references is None:
            continue
        try:
            references = json.loads(str(raw_references))
        except (TypeError, ValueError, json.JSONDecodeError):
            continue
        if not isinstance(references, list):
            continue
        changed = False
        key_remap: dict[str, str] = {}
        for reference in references:
            if (
                not isinstance(reference, dict)
                or reference.get("reference_scope") != "history_result"
            ):
                continue
            reference_type = str(reference.get("type", ""))
            topology_key = str(reference.get("topology_key", "0"))
            old_key = str(reference.get("key", ""))
            reference["entity_id"] = document.root.entity_id
            reference["key"] = (
                f"{reference_type}:{document.root.entity_id}:{topology_key}"
            )
            if old_key:
                key_remap[old_key] = str(reference["key"])
            changed = True
        for reference in references:
            if not isinstance(reference, dict):
                continue
            mappings = reference.get("mappings")
            if not isinstance(mappings, list):
                continue
            for mapping in mappings:
                if not isinstance(mapping, dict):
                    continue
                old_key = str(mapping.get("reference_key", ""))
                if old_key in key_remap:
                    mapping["reference_key"] = key_remap[old_key]
                    changed = True
        if changed:
            obj.parameters["constraint_refs"] = json.dumps(
                references,
                ensure_ascii=False,
            )

    for obj in walk_entities(document.root):
        raw_external_references = obj.parameters.get("external_references")
        if raw_external_references is None:
            continue
        try:
            external_references = json.loads(str(raw_external_references))
        except (TypeError, ValueError, json.JSONDecodeError):
            continue
        if not isinstance(external_references, list):
            continue
        external_changed = False
        reference_id_remap: dict[str, str] = {}
        for reference in external_references:
            if not isinstance(reference, dict):
                continue
            owner_id = str(reference.get("owner_id", ""))
            is_history_result = (
                reference.get("reference_scope") == "history_result"
            )
            # Early external-reference builds stored the transient Part result
            # ID without a scope marker. It is the only referenced owner absent
            # immediately after loading; retain compatibility with those files.
            if (
                not is_history_result
                and owner_id
                and document.find_entity(owner_id) is None
                and reference.get("source_kind") in ("face", "edge", "point")
            ):
                reference["reference_scope"] = "history_result"
                is_history_result = True
            if not is_history_result:
                continue
            old_reference_id = str(reference.get("id", ""))
            reference["owner_id"] = document.root.entity_id
            source_kind = str(reference.get("source_kind", "reference"))
            try:
                element_index = int(reference.get("element_index", 0))
            except (TypeError, ValueError):
                element_index = 0
            reference["id"] = (
                f"{source_kind}:{document.root.entity_id}:{element_index}"
            )
            if old_reference_id and old_reference_id != reference["id"]:
                reference_id_remap[old_reference_id] = reference["id"]
            external_changed = True
        if external_changed:
            obj.parameters["external_references"] = json.dumps(
                external_references,
                ensure_ascii=False,
            )
        if reference_id_remap and obj.kind == EntityKind.SKETCH:
            try:
                sketch_data = json.loads(
                    str(obj.parameters.get("sketch_data", "{}"))
                )
            except (TypeError, ValueError, json.JSONDecodeError):
                sketch_data = None
            if isinstance(sketch_data, dict):
                constraints = sketch_data.get("constraints", {})
                if isinstance(constraints, dict):
                    for constraint in constraints.values():
                        if not isinstance(constraint, dict):
                            continue
                        references = constraint.get("references")
                        if isinstance(references, list):
                            constraint["references"] = [
                                reference_id_remap.get(
                                    str(reference_id),
                                    reference_id,
                                )
                                for reference_id in references
                            ]
                        reference_id = str(
                            constraint.get("reference_id", "")
                        )
                        if reference_id in reference_id_remap:
                            constraint["reference_id"] = (
                                reference_id_remap[reference_id]
                            )
                obj.parameters["sketch_data"] = json.dumps(
                    sketch_data,
                    ensure_ascii=False,
                )


def migrate_missing_system_references(document: PartDocument) -> None:
    """Reconnect legacy references to randomly identified system geometry."""
    origin = next(
        (
            child
            for child in document.root.children
            if child.kind == EntityKind.ORIGIN
        ),
        None,
    )
    if origin is None:
        return
    system_entities = {child.name: child for child in origin.children}
    for obj in walk_entities(document.root):
        if obj.kind not in (EntityKind.POINT, EntityKind.AXIS):
            continue
        raw_references = obj.parameters.get("constraint_refs")
        if raw_references is None:
            continue
        try:
            references = json.loads(str(raw_references))
        except (TypeError, ValueError, json.JSONDecodeError):
            continue
        if not isinstance(references, list):
            continue
        changed = False
        for reference in references:
            if (
                not isinstance(reference, dict)
                or reference.get("type") != "entity"
            ):
                continue
            entity_id = str(reference.get("entity_id", ""))
            if entity_id and document.find_entity(entity_id) is not None:
                continue
            replacement = system_entities.get(str(reference.get("label", "")))
            if replacement is None:
                continue
            reference["entity_id"] = replacement.entity_id
            reference["key"] = f"entity:{replacement.entity_id}"
            changed = True
        if changed:
            obj.parameters["constraint_refs"] = json.dumps(
                references,
                ensure_ascii=False,
            )


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


def validate_container_entities(document: PartDocument) -> None:
    for obj in walk_entities(document.root):
        if obj.kind != EntityKind.CONTAINER:
            continue
        entities = obj.entity_children()
        if not obj.has_valid_entity_combination():
            raise ContainerEntityLimitError(obj.name, [entity.name for entity in entities])


def validate_sketch_data(document: PartDocument) -> None:
    for sketch in walk_entities(document.root):
        if sketch.kind != EntityKind.SKETCH:
            continue
        raw_data = sketch.parameters.get("sketch_data")
        if raw_data is None:
            raise SketchModelError(
                f"sketch {sketch.name!r} has no versioned sketch data"
            )
        try:
            data = json.loads(str(raw_data))
            SketchModel.from_dict(data)
        except (
            TypeError,
            ValueError,
            json.JSONDecodeError,
            SketchModelError,
        ) as error:
            raise SketchModelError(
                f"sketch {sketch.name!r} contains invalid data"
            ) from error


def walk_entities(obj: ZimaEntity):
    yield obj
    for child in obj.children:
        yield from walk_entities(child)


def write_entity(config: configparser.ConfigParser, obj: ZimaEntity) -> None:
    x, y, z = obj.coordinate_system.origin
    rx, ry, rz = obj.coordinate_system.rotation
    section_prefix = (
        "Container" if obj.kind == EntityKind.CONTAINER else "Entity"
    )
    section = f"{section_prefix}.{obj.entity_id}"
    config[section] = {
        "id": obj.entity_id,
        "name": obj.name,
        "kind": obj.kind.value,
        "combine_mode": obj.combine_mode.value,
        "x": format_float(x),
        "y": format_float(y),
        "z": format_float(z),
        "rx": format_float(rx),
        "ry": format_float(ry),
        "rz": format_float(rz),
        "user_visible": str(obj.user_visible).lower(),
        "suppressed": str(obj.suppressed).lower(),
        "tree_exposure": obj.tree_exposure.value,
        "show_auxiliary_geometry": str(obj.show_auxiliary_geometry).lower(),
    }
    if obj.kind == EntityKind.ORIGIN and obj.origin_scope is not None:
        config[section]["origin_scope"] = obj.origin_scope.value
    if obj.kind == EntityKind.CONTAINER:
        config[section]["TYPE"] = obj.container_type.value

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
        config[f"Children.{obj.entity_id}"] = {
            "items": ",".join(child.entity_id for child in user_children)
        }
        for child in user_children:
            write_entity(config, child)


def read_entity(
    config: configparser.ConfigParser,
    section: str,
    entity_id: str,
) -> ZimaEntity:
    kind = EntityKind(
        config.get(section, "kind", fallback=EntityKind.CONTAINER.value)
    )
    obj = ZimaEntity(
        name=config.get(section, "name", fallback=entity_id),
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
        entity_id=config.get(section, "id", fallback=entity_id),
        user_visible=config.getboolean(section, "user_visible", fallback=True),
        suppressed=config.getboolean(section, "suppressed", fallback=False),
        show_auxiliary_geometry=config.getboolean(
            section,
            "show_auxiliary_geometry",
            fallback=False,
        ),
        tree_exposure=TreeExposure(
            config.get(
                section,
                "tree_exposure",
                fallback=TreeExposure.PUBLIC.value,
            )
        ),
        origin_scope=(
            OriginScope(
                config.get(
                    section,
                    "origin_scope",
                    fallback=OriginScope.LOCAL.value,
                )
            )
            if kind == EntityKind.ORIGIN
            else None
        ),
    )

    for key, value in config[section].items():
        if key.startswith("param."):
            obj.parameters[key.removeprefix("param.")] = value
    if kind == EntityKind.CONTAINER:
        obj.parameters["container_type"] = config.get(
            section,
            "TYPE",
            fallback=obj.parameters.get("container_type", ContainerType.EMPTY.value),
        )
    if kind == EntityKind.SKETCH and "role" not in obj.parameters:
        obj.parameters["role"] = "PROFILE"

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

    if kind == EntityKind.CONTAINER:
        add_coordinate_system_children(obj)

    child_ids = [
        item.strip()
        for item in config.get(f"Children.{obj.entity_id}", "items", fallback="").split(",")
        if item.strip()
    ]
    for child_id in child_ids:
        child_section = f"Entity.{child_id}"
        if config.has_section(child_section):
            child = read_entity(config, child_section, child_id)
            if (
                obj.kind == EntityKind.CONTAINER
                and child.kind in (EntityKind.POINT, EntityKind.AXIS)
            ):
                # Point and axis entities are the geometric representation of
                # their owning Container, not a second public tree item.
                child.tree_exposure = TreeExposure.INTERNAL
            obj.add_child(child)

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
