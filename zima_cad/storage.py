from __future__ import annotations

import configparser
import base64
import gzip
import hashlib
import io
import json
from pathlib import Path

from zima_cad.body_result import BodyResult
from zima_cad.precision import format_model_float
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


ASSEMBLY_BODY_CACHE_REVISION = "5"


def create_cross_language_fixtures(directory: Path) -> None:
    """Write deterministic INI fixtures consumed by the C++ contract tests."""
    directory.mkdir(parents=True, exist_ok=True)
    body = {
        "volume": 1.0, "surface_area": 6.0, "source_fingerprint": "",
        "kernel_shape": "", "vertices": [], "triangles": [],
        "triangle_references": [], "edges": [], "points": [], "axes": [],
        "dimensions": [],
        "original_references": {
            "vertices": [], "triangles": [], "triangle_references": [],
            "edges": [], "points": [], "axes": [],
        },
    }
    part = configparser.ConfigParser(interpolation=None)
    part.optionxform = str
    part["Document"] = {
        "document_id": "part-fixture-001", "type": "part",
        "format_version": DOCUMENT_FORMAT_VERSION, "name": "Fixture Part",
    }
    part["DocumentUnits"] = {"Length": "mm"}
    part["DocumentPrecision"] = {"mesh_deflection": "0.1"}
    part["Material"] = {"Name": "Steel"}
    part["Containers"] = {"items": ""}
    part["CachedBodies"] = {
        "document_signature": "", "encoding": "zima-history-bodies-json-gzip-base64",
        "data": "",
    }
    _write_fixture_ini(directory / "part.prtz", part)

    assembly_payload = {
        "format": "zima-cad-cpp", "type": "assembly",
        "document_id": "assembly-fixture-001", "name": "Fixture Assembly",
        "user_parameters": {"clearance": "0.15 mm"},
        "user_parameter_order": ["clearance"],
        "user_parameter_labels": {"clearance": {"en": "Clearance"}},
        "user_parameter_values": {"clearance": {"": "0.15 mm"}},
        "relations": [], "document_units": {"Length": "mm"},
        "document_precision": {"mesh_deflection": "0.1"},
        "physical_parameters": {}, "physical_parameter_units": {},
        "material_parameter_descriptions": {}, "family_table": "{\"columns\":[],\"instances\":[]}",
        "sketches": [], "cuts": [], "constructions": [], "dependencies": [], "mates": [],
        "components": [{
            "occurrence_id": "subassembly-occurrence", "name": "Nested Assembly",
            "source_document_id": "nested-assembly-fixture", "source_path": "nested.asmz",
            "source_kind": "assembly", "suppressed": False, "visible": True,
            "grounded": False, "placement": {"x": 0, "y": 0, "z": 0,
                "rotation_x": 0, "rotation_y": 0, "rotation_z": 0},
            "calculated_source": body,
            "nested_snapshot": [{
                "occurrence_id": "part-occurrence", "name": "Nested Part",
                "source_document_id": "part-fixture-001", "source_kind": "part",
                "manually_suppressed": False, "dependency_suppressed": False,
                "visible": True, "grounded": False,
                "placement": {"x": 2, "y": 0, "z": 0,
                    "rotation_x": 0, "rotation_y": 0, "rotation_z": 0},
                "children": [],
            }],
        }],
    }
    assembly = configparser.ConfigParser(interpolation=None)
    assembly.optionxform = str
    assembly["Document"] = {
        "document_id": "assembly-fixture-001", "type": "assembly",
        "format_version": DOCUMENT_FORMAT_VERSION,
    }
    assembly["DocumentUnits"] = {"Length": "mm"}
    assembly["DocumentPrecision"] = {"mesh_deflection": "0.1"}
    assembly["Containers"] = {"items": "assembly-fixture-001"}
    assembly["Container.assembly-fixture-001"] = {
        "param.cpp_assembly": json.dumps(assembly_payload, ensure_ascii=True,
                                          separators=(",", ":")),
    }
    _write_fixture_ini(directory / "nested.asmz", assembly)

    drawing_payload = {
        "format": "zima-cad-drawing", "version": 2,
        "document_id": "drawing-fixture-001", "name": "Fixture Drawing",
        "sheets": [{
            "id": "sheet-fixture-001", "name": "Sheet 1", "format": "A4",
            "projection_method": "first_angle", "default_scale": 1.0,
            "frame_lines": [], "frame_texts": [], "title_block_lines": [],
            "title_block_texts": [], "title_block_fields": [], "bom_rows": [],
            "dimensions": [], "views": [{
                "id": "view-fixture-001", "name": "Front",
                "source_document_id": "part-fixture-001", "source_path": "part.prtz",
                "parent_view_id": "", "orientation": "front",
                "projection_direction": "none",
                "camera": {"horizontal": [1, 0, 0], "vertical": [0, 0, 1],
                           "depth": [0, -1, 0]},
                "display_style": "visible_edges", "x": 0, "y": 0, "scale": 1,
                "projected_edges": [], "projected_triangles": [],
            }],
        }],
    }
    drawing = configparser.ConfigParser(interpolation=None)
    drawing.optionxform = str
    drawing["Document"] = {
        "document_id": "drawing-fixture-001", "type": "drawing",
        "format_version": DOCUMENT_FORMAT_VERSION,
        "param.cpp_drawing": json.dumps(drawing_payload, ensure_ascii=True,
                                         separators=(",", ":")),
    }
    _write_fixture_ini(directory / "drawing.drwz", drawing)


def _write_fixture_ini(path: Path, config: configparser.ConfigParser) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        config.write(stream, space_around_delimiters=False)


class ContainerEntityLimitError(ValueError):
    def __init__(self, container_name: str, entity_names: list[str]) -> None:
        self.container_name = container_name
        self.entity_names = entity_names
        super().__init__(
            f"Container {container_name!r} contains an invalid entity combination: "
            + ", ".join(entity_names)
        )


def _canonical_json(value) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _parse_cpp_object(raw: str, label: str) -> dict:
    try:
        value = json.loads(str(raw))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid {label} payload") from error
    if not isinstance(value, dict):
        raise ValueError(f"Invalid {label} payload: expected an object")
    return value


def _require_cpp_keys(value: dict, keys, label: str) -> None:
    missing = [key for key in keys if key not in value]
    if missing:
        raise ValueError(f"Invalid {label} payload: missing {', '.join(missing)}")


def _load_cpp_assembly_parameters(document: PartDocument, raw: str) -> None:
    payload = _parse_cpp_object(raw, "C++ Assembly")
    if payload.get("format") != "zima-cad-cpp" or payload.get("type") != "assembly":
        raise ValueError("Invalid C++ Assembly payload")
    _require_cpp_keys(payload, ("components", "mates", "cuts"), "C++ Assembly")
    for key in ("components", "mates", "cuts"):
        if not isinstance(payload[key], list):
            raise ValueError(f"Invalid C++ Assembly payload: {key} must be a list")
    for index, mate in enumerate(payload["mates"]):
        if not isinstance(mate, dict):
            raise ValueError(f"Invalid C++ Assembly mate at index {index}")
        _require_cpp_keys(
            mate,
            ("mate_id", "name", "kind", "dependent", "prerequisite", "offset",
             "angle_degrees", "flipped", "status", "suppressed"),
            "C++ Assembly mate",
        )
        for reference_name in ("dependent", "prerequisite"):
            reference = mate[reference_name]
            if not isinstance(reference, dict):
                raise ValueError(
                    f"Invalid C++ Assembly mate: {reference_name} must be an object"
                )
            _require_cpp_keys(
                reference, ("kind", "instance_path", "owner_id", "semantic_key"),
                "C++ Assembly mate reference",
            )
    for index, cut in enumerate(payload["cuts"]):
        if not isinstance(cut, dict):
            raise ValueError(f"Invalid C++ Assembly cut at index {index}")
        _require_cpp_keys(
            cut,
            ("id", "feature_id", "feature_parent_id", "name", "kind",
             "suppressed", "targets"),
            "C++ Assembly cut",
        )
        if not isinstance(cut["targets"], list):
            raise ValueError("Invalid C++ Assembly cut payload: targets must be a list")
    document.document_settings["assembly_mates"] = _canonical_json(payload["mates"])
    document.document_settings["assembly_cuts"] = _canonical_json(payload["cuts"])
    document.document_settings["cpp_assembly"] = _canonical_json(payload)


def _attach_cpp_assembly_mates(document: PartDocument) -> None:
    raw = document.document_settings.get("assembly_mates")
    if not raw:
        return
    mates = json.loads(str(raw))
    components = {
        str(obj.parameters.get("occurrence_id", obj.entity_id)): obj
        for obj in document.history_objects()
        if obj.container_type == ContainerType.COMPONENT
    }
    rows: dict[str, list[dict]] = {}
    for mate in mates:
        dependent = mate["dependent"]
        occurrence_id = str(dependent["instance_path"])
        if occurrence_id not in components:
            continue
        row = {
            "mate_id": mate["mate_id"],
            "name": mate["name"],
            "type": mate["kind"],
            "source": dependent["semantic_key"],
            "target": mate["prerequisite"]["semantic_key"],
            "dependent": dependent,
            "prerequisite": mate["prerequisite"],
            "dependent_occurrence_id": dependent["instance_path"],
            "target_occurrence_id": mate["prerequisite"]["instance_path"],
            "offset": mate["offset"],
            "angle_degrees": mate["angle_degrees"],
            "flipped": mate["flipped"],
            "status": mate["status"],
            "suppressed": mate["suppressed"],
        }
        for key in ("lower_limit", "upper_limit"):
            if key in mate:
                row[key] = mate[key]
        rows.setdefault(occurrence_id, []).append(row)
    for occurrence_id, component in components.items():
        component.parameters["assembly_mates"] = _canonical_json(
            rows.get(occurrence_id, [])
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
    if document.relations:
        config["Relations"] = {
            "Data": json.dumps(
                document.relations,
                ensure_ascii=False,
                separators=(",", ":"),
            )
        }

    containers = document.visible_objects()
    config["Containers"] = {
        "items": ",".join(container.entity_id for container in containers)
    }

    for container in containers:
        write_entity(config, container)

    # Persist every calculated history boundary. Editing an earlier feature
    # must consume its real input body without substituting the final body or
    # invoking OCCT from a viewer/property path.
    history = document.history_objects()
    document.cached_body_result_at(history)
    cached_results = {}
    for boundary in range(1, len(history) + 1):
        keys = document._shape_history_cache_keys(history[:boundary])
        result = document._body_result_cache.get(keys[-1]) if keys else None
        if result is not None:
            cached_results[str(boundary)] = result.to_dict()
    if cached_results:
        result_text = json.dumps(
            cached_results,
            ensure_ascii=True,
            separators=(",", ":"),
        )
        config["CachedBodies"] = {
            "document_signature": _config_model_signature(config),
            "encoding": "zima-history-bodies-json-gzip-base64",
            "assembly_calculation_revision": (
                ASSEMBLY_BODY_CACHE_REVISION
                if document.document_settings.get("type") == "assembly"
                else ""
            ),
            "data": base64.b64encode(
                gzip.compress(result_text.encode("utf-8"), compresslevel=6)
            ).decode("ascii"),
        }

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
    if document_type == "assembly":
        root_id = document.document_settings.get("document_id", "")
        root_section = f"Container.{root_id}"
        root_cpp = config.get(root_section, "param.cpp_assembly", fallback="")
        if root_cpp:
            document.document_settings["param.cpp_assembly"] = root_cpp
    cpp_assembly = document.document_settings.get("param.cpp_assembly", "")
    if document_type == "assembly" and cpp_assembly:
        _load_cpp_assembly_parameters(document, cpp_assembly)
    if document_type == "drawing":
        cpp_drawing = document.document_settings.get("param.cpp_drawing", "")
        if cpp_drawing:
            try:
                drawing_payload = json.loads(cpp_drawing)
                sheets = drawing_payload.get("sheets")
                if not isinstance(sheets, list):
                    raise ValueError("C++ Drawing payload has no sheet list")
                document.document_settings["drawing_sheets"] = json.dumps(
                    sheets, ensure_ascii=False, separators=(",", ":")
                )
            except (TypeError, ValueError, json.JSONDecodeError) as error:
                raise ValueError("Invalid C++ Drawing payload") from error
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
        document.physical_parameters = material_parameters
    if config.has_section("MaterialUnits"):
        document.physical_parameter_units = dict(config["MaterialUnits"])
    if config.has_section("MaterialDescriptions"):
        document.material_parameter_descriptions = read_language_map(
            config["MaterialDescriptions"]
        )
    if config.has_section("UserParameters"):
        load_user_parameters(config, document)
    if config.has_section("Relations"):
        try:
            relations = json.loads(config.get("Relations", "Data", fallback="[]"))
        except json.JSONDecodeError as exc:
            raise ValueError("Invalid model relations") from exc
        if not isinstance(relations, list):
            raise ValueError("Invalid model relations")
        document.relations = [
            {
                "target": str(item.get("target", "")),
                "expression": str(item.get("expression", "")),
            }
            for item in relations
            if isinstance(item, dict)
        ]

    container_ids = [
        item.strip()
        for item in config.get("Containers", "items", fallback="").split(",")
        if item.strip()
    ]

    for container_id in container_ids:
        section = f"Container.{container_id}"
        if not config.has_section(section):
            continue
        if document_type == "assembly" and container_id == document.document_settings.get(
            "document_id", ""
        ):
            continue

        obj = read_entity(config, section, container_id)
        document.root.add_child(obj)

    if document_type == "assembly":
        _attach_cpp_assembly_mates(document)

    # Format 6 uses one automatic internal solid result. Explicit format-5 Body
    # snapshots are redundant because their source objects remain in the history.
    document.root.children = [
        obj for obj in document.root.children
        if obj.kind != EntityKind.BODY
    ]
    reconnect_history_result_references(document)
    validate_container_entities(document)
    validate_sketch_data(document)
    _load_cached_body(config, document)
    return document


def _load_cached_body(
    config: configparser.ConfigParser,
    document: PartDocument,
) -> None:
    if not config.has_section("CachedBodies"):
        return
    if (
        document.document_settings.get("type") == "assembly"
        and config.get(
            "CachedBodies",
            "assembly_calculation_revision",
            fallback="",
        )
        != ASSEMBLY_BODY_CACHE_REVISION
    ):
        # Viewer packets created before headless mate regeneration could hold
        # a post-solver intermediate scene (notably an uncut Assembly) under
        # an otherwise valid model signature. They are calculation caches,
        # not document data; reject them and let the normal regeneration
        # boundary create one authoritative current packet.
        return
    history = document.history_objects()
    if not history or config.get(
        "CachedBodies", "document_signature", fallback=""
    ) != _config_model_signature(config):
        return
    if config.get("CachedBodies", "encoding", fallback="") != (
        "zima-history-bodies-json-gzip-base64"
    ):
        return
    try:
        payload = base64.b64decode(
            config.get("CachedBodies", "data", fallback=""),
            validate=True,
        )
        result_data = json.loads(gzip.decompress(payload).decode("utf-8"))
        if not isinstance(result_data, dict):
            return
    except (KeyError, OSError, TypeError, UnicodeError, ValueError, RuntimeError, json.JSONDecodeError):
        return
    for raw_boundary, value in result_data.items():
        try:
            boundary = int(raw_boundary)
            result = BodyResult.from_dict(value)
        except (TypeError, ValueError, RuntimeError):
            continue
        if boundary < 1 or boundary > len(history):
            continue
        cache_keys = document._shape_history_cache_keys(history[:boundary])
        if not cache_keys:
            continue
        if document.document_settings.get("type", "part") == "part":
            result = result.with_owner(document.root.entity_id)
        document._body_result_cache[cache_keys[-1]] = result


def _config_model_signature(
    config: configparser.ConfigParser,
) -> str:
    """Hash the persisted parametric document, excluding its BREP cache."""
    canonical = tuple(
        (
            section,
            tuple(sorted(config.items(section))),
        )
        for section in sorted(config.sections())
        if section != "CachedBodies"
    )
    return hashlib.sha256(
        repr(canonical).encode("utf-8")
    ).hexdigest()


def reconnect_history_result_references(document: PartDocument) -> None:
    """Bind persisted automatic-Body references to the new runtime Part ID."""
    def reconnect_descriptor(value) -> bool:
        changed = False
        if isinstance(value, dict):
            if value.get("reference_scope") == "history_result":
                reference_type = str(value.get("type", ""))
                topology_key = str(value.get("topology_key", "0"))
                value["entity_id"] = document.root.entity_id
                value["key"] = (
                    f"{reference_type}:{document.root.entity_id}:"
                    f"{topology_key}"
                )
                changed = True
            # Container orientation stores complete reference descriptors
            # below mappings[].reference.  Walk the current data model
            # recursively so top-level placement and FRONT/TOP always bind
            # to the same runtime Body owner.
            for nested in value.values():
                changed = reconnect_descriptor(nested) or changed
        elif isinstance(value, list):
            for nested in value:
                changed = reconnect_descriptor(nested) or changed
        return changed

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
        changed = reconnect_descriptor(references)
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
            is_history_result = (
                reference.get("reference_scope") == "history_result"
            )
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
                # Profile geometry created from an external edge owns the
                # same persisted reference id on both its endpoint records
                # and segment record.  Keep those links in step with the
                # runtime Part-id remap performed above.
                for section_name in ("points", "geometry"):
                    records = sketch_data.get(section_name, {})
                    if not isinstance(records, dict):
                        continue
                    for record in records.values():
                        if not isinstance(record, dict):
                            continue
                        reference_id = str(
                            record.get("external_profile_reference_id", "")
                        )
                        if reference_id in reference_id_remap:
                            record["external_profile_reference_id"] = (
                                reference_id_remap[reference_id]
                            )
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
        container_type = config.get(
            section,
            "TYPE",
            fallback=obj.parameters.get("container_type", ContainerType.EMPTY.value),
        )
        if container_type == "OCCURRENCE":
            container_type = ContainerType.COMPONENT.value
        elif container_type in {"ASSEMBLY", "FEATURE"}:
            container_type = ContainerType.EMPTY.value
        obj.parameters["container_type"] = container_type
        cpp_data = obj.parameters.get("cpp_data")
        if cpp_data and container_type == ContainerType.COMPONENT.value:
            try:
                occurrence = _parse_cpp_object(cpp_data, "Assembly occurrence")
                _require_cpp_keys(
                    occurrence,
                    ("occurrence_id", "name", "source_document_id", "source_path",
                     "source_kind", "suppressed", "visible", "grounded",
                     "placement"),
                    "Assembly occurrence",
                )
                obj.parameters["source_path"] = str(
                    occurrence["source_path"]
                )
                obj.parameters["source_document_id"] = str(
                    occurrence["source_document_id"]
                )
                obj.parameters["source_kind"] = str(
                    occurrence["source_kind"]
                )
                obj.parameters["instance_path"] = str(
                    occurrence["occurrence_id"]
                )
                obj.parameters["occurrence_id"] = str(occurrence["occurrence_id"])
                obj.parameters["suppressed"] = str(
                    bool(occurrence["suppressed"])
                ).lower()
                obj.parameters["visible"] = str(bool(occurrence["visible"])).lower()
                obj.parameters["grounded"] = str(bool(occurrence["grounded"])).lower()
                obj.suppressed = bool(occurrence["suppressed"])
                obj.user_visible = bool(occurrence["visible"])
                obj.parameters["assembly_occurrence"] = _canonical_json(occurrence)
            except (TypeError, ValueError, KeyError) as error:
                raise ValueError(f"Invalid C++ Assembly occurrence {obj.name!r}") from error
        elif cpp_data and container_type == ContainerType.EMPTY.value:
            # C++ assembly cut containers are represented by normal Python
            # feature containers.  Keep their complete native definition in
            # the feature parameters instead of reducing it to a generic
            # container.
            cpp_kind = str(obj.parameters.get("cpp_kind", "")).lower()
            if cpp_kind == "feature":
                try:
                    cut = _parse_cpp_object(cpp_data, "Assembly cut")
                    _require_cpp_keys(
                        cut,
                        ("id", "feature_id", "feature_parent_id", "name", "kind",
                         "suppressed", "targets"),
                        "Assembly cut",
                    )
                    obj.parameters["assembly_cut"] = _canonical_json(cut)
                    obj.parameters["assembly_cut_targets"] = _canonical_json(
                        cut["targets"]
                    )
                except (TypeError, ValueError, KeyError) as error:
                    raise ValueError(f"Invalid C++ Assembly cut {obj.name!r}") from error
    if kind in (EntityKind.PROTRUSION, EntityKind.REVOLVE):
        cpp_data = obj.parameters.get("cpp_data")
        if cpp_data:
            try:
                cut = _parse_cpp_object(cpp_data, "Assembly cut feature")
                _require_cpp_keys(
                    cut, ("id", "feature_id", "feature_parent_id", "targets"),
                    "Assembly cut feature",
                )
                obj.parameters["assembly_cut"] = _canonical_json(cut)
                obj.parameters["assembly_cut_targets"] = _canonical_json(
                    cut["targets"]
                )
            except (TypeError, ValueError, KeyError) as error:
                raise ValueError(
                    f"Invalid C++ Assembly cut feature {obj.name!r}"
                ) from error
    if kind == EntityKind.SKETCH and "role" not in obj.parameters:
        obj.parameters["role"] = "PROFILE"
    # C++ PartDocument exports its native sketch beside the INI entity.  Fold
    # it into the Python sketch_data contract during load so subsequent edits
    # and saves remain entirely Python-side and kernel-free.
    if kind == EntityKind.SKETCH:
        cpp_sketch = obj.parameters.get("cpp_sketch")
        if cpp_sketch:
            try:
                sketch_data = SketchModel.from_dict(json.loads(str(cpp_sketch))).to_dict()
                obj.parameters["sketch_data"] = json.dumps(
                    sketch_data, ensure_ascii=False, separators=(",", ":")
                )
                obj.parameters["plane"] = sketch_data.get(
                    "plane", obj.parameters.get("plane", "xy")
                )
                if "plane_offset" in sketch_data:
                    obj.parameters["plane_offset"] = str(sketch_data["plane_offset"])
            except (TypeError, ValueError, json.JSONDecodeError, SketchModelError) as error:
                raise SketchModelError(
                    f"sketch {obj.name!r} contains invalid native data"
                ) from error

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
    return format_model_float(value)


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
