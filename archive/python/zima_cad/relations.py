"""Safe, deterministic model relations.

Relations deliberately use a small Python-like expression syntax.  They are
never executed as Python code; the parsed AST is evaluated through an explicit
allow-list so model files cannot access the filesystem or application runtime.
"""

from __future__ import annotations

import ast
import math
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Mapping

from OCC.Core.BRepGProp import brepgprop
from OCC.Core.GProp import GProp_GProps


class RelationError(ValueError):
    pass


_FUNCTIONS = {
    "abs": abs,
    "min": min,
    "max": max,
    "round": round,
    "sqrt": math.sqrt,
    "sin": math.sin,
    "cos": math.cos,
    "tan": math.tan,
}

_BINARY = {
    ast.Add: lambda a, b: a + b,
    ast.Sub: lambda a, b: a - b,
    ast.Mult: lambda a, b: a * b,
    ast.Div: lambda a, b: a / b,
    ast.Pow: lambda a, b: a**b,
    ast.Mod: lambda a, b: a % b,
}

_COMPARE = {
    ast.Eq: lambda a, b: a == b,
    ast.NotEq: lambda a, b: a != b,
    ast.Lt: lambda a, b: a < b,
    ast.LtE: lambda a, b: a <= b,
    ast.Gt: lambda a, b: a > b,
    ast.GtE: lambda a, b: a >= b,
}


def _number(value: object, name: str) -> float:
    try:
        return float(str(value).strip().replace(",", "."))
    except (TypeError, ValueError) as exc:
        raise RelationError(f"Parameter {name!r} is not numeric") from exc


def _density_kg_per_mm3(document) -> float:
    value = _number(
        document.physical_parameters.get("MASS_DENSITY", "0"),
        "MASS_DENSITY",
    )
    unit = str(
        document.physical_parameter_units.get("MASS_DENSITY", "kg/mm^3")
    ).strip().lower().replace("³", "^3")
    factors = {
        "kg/mm^3": 1.0,
        "kg/m^3": 1.0e-9,
        "g/cm^3": 1.0e-6,
        "lb/in^3": 0.45359237 / (25.4**3),
    }
    if unit not in factors:
        raise RelationError(f"Unsupported density unit {unit!r}")
    return value * factors[unit]


def model_values(document) -> tuple[SimpleNamespace, SimpleNamespace]:
    volume = 0.0
    area = 0.0
    mass = 0.0
    if document.document_settings.get("type") == "assembly":
        from zima_cad.model import ContainerType
        from zima_cad.storage import load_part_document

        objects = document.history_objects_at(document.history_cursor())
        document_cache = document.__dict__.setdefault(
            "_assembly_component_document_cache", {}
        )
        for component in objects:
            if component.container_type != ContainerType.COMPONENT:
                continue
            raw_path = str(component.parameters.get("source_path", "")).strip()
            if not raw_path:
                raise RelationError(
                    f"Assembly component {component.name!r} has no source file"
                )
            source_path = Path(raw_path)
            if not source_path.is_absolute():
                assembly_path = document.source_file_path
                if assembly_path is None:
                    raise RelationError(
                        f"Cannot resolve component {component.name!r} before "
                        "the assembly is saved"
                    )
                source_path = assembly_path.parent / source_path
            source_path = source_path.resolve()
            source_document = document_cache.get(source_path)
            if source_document is None:
                try:
                    source_document = load_part_document(source_path)
                except (OSError, ValueError) as exc:
                    raise RelationError(
                        f"Cannot load assembly component {source_path}"
                    ) from exc
                document_cache[source_path] = source_document
            shape = document.build_assembly_component_shape(
                component,
                objects,
                source_document=source_document,
            )
            if shape is None or shape.IsNull():
                continue
            properties = GProp_GProps()
            brepgprop.VolumeProperties(shape, properties)
            component_volume = abs(float(properties.Mass()))
            volume += component_volume
            mass += component_volume * _density_kg_per_mm3(source_document)
            properties = GProp_GProps()
            brepgprop.SurfaceProperties(shape, properties)
            area += abs(float(properties.Mass()))
    else:
        shape = document.build_active_shape()
        if shape is not None and not shape.IsNull():
            properties = GProp_GProps()
            brepgprop.VolumeProperties(shape, properties)
            volume = abs(float(properties.Mass()))
            properties = GProp_GProps()
            brepgprop.SurfaceProperties(shape, properties)
            area = abs(float(properties.Mass()))
        mass = volume * _density_kg_per_mm3(document)
    material = SimpleNamespace(density=_density_kg_per_mm3(document))
    model = SimpleNamespace(
        volume=volume,
        area=area,
        mass=mass,
    )
    return model, material


@dataclass
class _Evaluator:
    values: Mapping[str, Any]

    def evaluate(self, expression: str) -> Any:
        try:
            tree = ast.parse(expression, mode="eval")
        except SyntaxError as exc:
            raise RelationError(str(exc)) from exc
        return self._node(tree.body)

    def _node(self, node: ast.AST) -> Any:
        if isinstance(node, ast.Constant) and isinstance(
            node.value, (int, float, str, bool)
        ):
            return node.value
        if isinstance(node, ast.Name):
            if node.id in self.values:
                return self.values[node.id]
            raise RelationError(f"Unknown name {node.id!r}")
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
            owner = self.values.get(node.value.id)
            if isinstance(owner, SimpleNamespace) and hasattr(owner, node.attr):
                return getattr(owner, node.attr)
            raise RelationError(f"Unknown value {node.value.id}.{node.attr}")
        if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
            return _BINARY[type(node.op)](
                self._node(node.left), self._node(node.right)
            )
        if isinstance(node, ast.UnaryOp) and isinstance(
            node.op, (ast.UAdd, ast.USub, ast.Not)
        ):
            value = self._node(node.operand)
            if isinstance(node.op, ast.UAdd):
                return +value
            if isinstance(node.op, ast.USub):
                return -value
            return not value
        if isinstance(node, ast.IfExp):
            return self._node(node.body if self._node(node.test) else node.orelse)
        if isinstance(node, ast.Compare):
            left = self._node(node.left)
            for operator, comparator in zip(node.ops, node.comparators):
                operation = _COMPARE.get(type(operator))
                right = self._node(comparator)
                if operation is None or not operation(left, right):
                    return False
                left = right
            return True
        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id in _FUNCTIONS
            and not node.keywords
        ):
            return _FUNCTIONS[node.func.id](*[self._node(arg) for arg in node.args])
        raise RelationError(f"Unsupported expression element {type(node).__name__}")


def evaluate_document_relations(document) -> dict[str, str]:
    """Evaluate relations in order and write plain results to user parameters."""

    model, material = model_values(document)
    values: dict[str, Any] = {"model": model, "material": material}
    for key, raw in document.user_parameters.items():
        try:
            values[key] = _number(raw, key)
        except RelationError:
            values[key] = raw
    results: dict[str, str] = {}
    decimals = max(0, int(document.document_precision.get("decimal_places", "3")))
    for relation in document.relations:
        target = str(relation.get("target", "")).strip()
        expression = str(relation.get("expression", "")).strip()
        if not target or not expression:
            continue
        result = _Evaluator(values).evaluate(expression)
        if isinstance(result, float):
            rendered = f"{result:.{decimals}f}"
        elif isinstance(result, bool):
            rendered = "true" if result else "false"
        else:
            rendered = str(result)
        values[target] = result
        results[target] = rendered
        document.user_parameters[target] = rendered
        document.user_parameter_values.setdefault(target, {})[""] = rendered
        if target not in document.user_parameter_order:
            document.user_parameter_order.append(target)
        document.user_parameter_labels.setdefault(target, {})
    return results
