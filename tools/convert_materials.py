from __future__ import annotations

import configparser
import io
import re
from pathlib import Path

from zima_cad.versioned_io import validate_ini_file, write_text_versioned


PROPERTY_ORDER = [
    "YOUNG_MODULUS",
    "POISSON_RATIO",
    "SHEAR_MODULUS",
    "MASS_DENSITY",
    "THERMAL_EXPANSION_COEFFICIENT",
    "THERM_EXPANSION_REF_TEMPERATURE",
    "STRUCTURAL_DAMPING_COEFFICIENT",
    "STRESS_LIMIT_FOR_TENSION",
    "STRESS_LIMIT_FOR_COMPRESSION",
    "STRESS_LIMIT_FOR_SHEAR",
    "THERMAL_CONDUCTIVITY",
    "EMISSIVITY",
    "SPECIFIC_HEAT",
    "HARDNESS",
    "CONDITION",
    "INITIAL_BEND_Y_FACTOR",
    "BEND_TABLE",
]

DESCRIPTIONS = {
    "material_name": ("Materiál", "Material"),
    "YOUNG_MODULUS": ("Youngův modul", "Young's modulus"),
    "POISSON_RATIO": ("Poissonovo číslo", "Poisson's ratio"),
    "SHEAR_MODULUS": ("Modul pružnosti ve smyku", "Shear modulus"),
    "MASS_DENSITY": ("Hustota", "Mass density"),
    "THERMAL_EXPANSION_COEFFICIENT": (
        "Součinitel teplotní roztažnosti",
        "Thermal expansion coefficient",
    ),
    "THERM_EXPANSION_REF_TEMPERATURE": (
        "Referenční teplota roztažnosti",
        "Thermal expansion reference temperature",
    ),
    "STRUCTURAL_DAMPING_COEFFICIENT": (
        "Součinitel konstrukčního tlumení",
        "Structural damping coefficient",
    ),
    "STRESS_LIMIT_FOR_TENSION": ("Mez napětí v tahu", "Tensile stress limit"),
    "STRESS_LIMIT_FOR_COMPRESSION": (
        "Mez napětí v tlaku",
        "Compressive stress limit",
    ),
    "STRESS_LIMIT_FOR_SHEAR": ("Mez smykového napětí", "Shear stress limit"),
    "THERMAL_CONDUCTIVITY": ("Tepelná vodivost", "Thermal conductivity"),
    "EMISSIVITY": ("Emisivita", "Emissivity"),
    "SPECIFIC_HEAT": ("Měrná tepelná kapacita", "Specific heat"),
    "HARDNESS": ("Tvrdost", "Hardness"),
    "CONDITION": ("Stav materiálu", "Material condition"),
    "INITIAL_BEND_Y_FACTOR": ("Počáteční Y-faktor ohybu", "Initial bend Y-factor"),
    "BEND_TABLE": ("Tabulka ohybů", "Bend table"),
    "material_source": ("Zdroj materiálu", "Material source"),
}

PTC_NAME_MAP = {
    "PTC_YOUNG_MODULUS": "YOUNG_MODULUS",
    "PTC_POISSON_RATIO": "POISSON_RATIO",
    "PTC_SHEAR_MODULUS": "SHEAR_MODULUS",
    "PTC_MASS_DENSITY": "MASS_DENSITY",
    "PTC_THERMAL_EXPANSION_COEF": "THERMAL_EXPANSION_COEFFICIENT",
    "PTC_THERMAL_CONDUCTIVITY": "THERMAL_CONDUCTIVITY",
    "PTC_SPECIFIC_HEAT": "SPECIFIC_HEAT",
    "PTC_INITIAL_BEND_Y_FACTOR": "INITIAL_BEND_Y_FACTOR",
    "PTC_BEND_TABLE": "BEND_TABLE",
}


def parse_legacy(text: str, fallback_name: str) -> tuple[str, dict[str, str]]:
    material_match = re.search(r"^\s*MATERIAL\s+(.+?)\s*$", text, re.MULTILINE)
    name = material_match.group(1).strip() if material_match else fallback_name
    properties = {}
    for key in PROPERTY_ORDER:
        match = re.search(
            rf"^[ \t]*{re.escape(key)}[ \t]*=[ \t]*([^\r\n]*)$",
            text,
            re.MULTILINE,
        )
        properties[key] = match.group(1).strip() if match else ""
    return name, properties


def parse_ptc(text: str, fallback_name: str) -> tuple[str, dict[str, str]]:
    name_match = re.search(r"^Name\s*=\s*(.+?)\s*$", text, re.MULTILINE)
    name = name_match.group(1).strip() if name_match else fallback_name
    properties = {key: "" for key in PROPERTY_ORDER}
    blocks = re.findall(r"\{\s*Name\s*=\s*(\S+)(.*?)\}", text, re.DOTALL)
    for ptc_name, body in blocks:
        key = PTC_NAME_MAP.get(ptc_name)
        if key is None:
            continue
        default_match = re.search(r"^\s*Default\s*=\s*(.*?)\s*$", body, re.MULTILINE)
        if default_match:
            properties[key] = default_match.group(1).strip().strip("'")
    return name, properties


def convert_file(path: Path) -> None:
    text = path.read_text(encoding="utf-8-sig")
    if text.lstrip().startswith("["):
        existing = configparser.ConfigParser()
        existing.optionxform = str
        existing.read_string(text)
        name = existing.get("Material", "Name", fallback=path.stem.upper())
        properties = {
            key: existing.get("Properties", key, fallback="")
            for key in PROPERTY_ORDER
        }
        properties = {
            key: "" if "=" in value else value
            for key, value in properties.items()
        }
    else:
        fallback_name = path.stem.upper()
        if "ND_RelParSet" in text:
            name, properties = parse_ptc(text, fallback_name)
        else:
            name, properties = parse_legacy(text, fallback_name)

    config = configparser.ConfigParser()
    config.optionxform = str
    config["Material"] = {"Name": name}
    config["Properties"] = properties
    config["ParameterDescriptions"] = {
        f"{key}\\cs": labels[0]
        for key, labels in DESCRIPTIONS.items()
    }
    config["ParameterDescriptions"].update(
        {f"{key}\\en": labels[1] for key, labels in DESCRIPTIONS.items()}
    )
    buffer = io.StringIO()
    config.write(buffer)
    write_text_versioned(
        path,
        buffer.getvalue().rstrip() + "\n",
        validator=validate_ini_file,
    )


def main() -> None:
    materials_path = Path(__file__).resolve().parents[1] / "config" / "materials"
    for path in sorted(materials_path.glob("*.matz")):
        convert_file(path)


if __name__ == "__main__":
    main()
