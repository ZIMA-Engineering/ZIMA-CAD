"""Run the native Sketch intersection mouse matrix and audit explicit work.

Development verification only; portable release acceptance uses the distribution
pipeline and its packaged-resource checks. All outputs stay under build/.
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/sketch-intersection-ui"))
    parser.add_argument("--language", choices=("cs", "en", "de", "fr", "ru"), default="cs")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--platform", default=None)
    parser.add_argument("--single", action="store_true", help="Run the circular Part contour case only.")
    parser.add_argument("--spline", action="store_true", help="Run the genuine spline Part contour mouse case.")
    parser.add_argument("--trimmed", action="store_true", help="Run the trimmed cylinder Part contour mouse case.")
    args = parser.parse_args()
    if args.spline and args.trimmed:
        parser.error("Choose either --spline or --trimmed.")
    args.single = args.single or args.spline or args.trimmed
    executable = args.exe.resolve(strict=True)
    output = args.output.resolve()
    try:
        output.relative_to((root / "build").resolve())
    except ValueError:
        parser.error("Verification output must be inside this repository's build/ directory.")
    output.mkdir(parents=True, exist_ok=True)
    configuration = configparser.ConfigParser(interpolation=None)
    configuration.optionxform = str
    configuration["Application"] = {"Language": args.language, "Theme": args.theme}
    configuration["Paths"] = {
        key: (root / path).as_posix() for key, path in {
            "Materials": "config/materials", "Templates": "config/templates",
            "Formats": "config/formats", "Symbols": "config/symbols",
            "Forms": "config/lib/01-SHEETMETAL/01-FORM",
            "Localization": "config/localization",
        }.items()
    }
    configuration["Paths"]["WorkingDirectory"] = output.as_posix()
    configuration["Templates"] = {"Part": "START_PART_mm.prtz", "Assembly": "START_ASSEMBLY_mm.asmz"}
    with (output / "config.ini").open("w", encoding="utf-8") as stream:
        configuration.write(stream)
    environment = {key: value for key, value in os.environ.items() if not key.startswith("ZIMA_VERIFY_")}
    environment.update(
        QT_STYLE_OVERRIDE="Fusion", ZIMA_VERIFY_FACE_PLANE_ONLY="1",
        ZIMA_CPP_PROFILE_COMMAND="1",
    )
    if args.platform:
        environment["QT_QPA_PLATFORM"] = args.platform
    elif os.name == "nt":
        environment["QT_QPA_PLATFORM"] = "windows"
    if args.trimmed:
        environment["ZIMA_VERIFY_FACE_PLANE_TRIMMED"] = "1"
    if args.spline:
        environment["ZIMA_VERIFY_FACE_PLANE_SPLINE"] = "1"
    if args.single:
        environment["ZIMA_VERIFY_FACE_PLANE_SINGLE"] = "1"
    temporary = root / "build/pre-release-test-temp"
    temporary.mkdir(exist_ok=True)
    environment.update(TEMP=str(temporary), TMP=str(temporary))
    start = time.perf_counter()
    result = subprocess.run(
        [str(executable), "--verify-startup", "--working-directory", str(output)],
        cwd=root, env=environment, capture_output=True, timeout=150,
    )
    elapsed = time.perf_counter() - start
    (output / "stdout.log").write_bytes(result.stdout)
    (output / "stderr.log").write_bytes(result.stderr)
    times = [float(value) for value in re.findall(rb"Sketch face-plane query: ([\d.]+) ms", result.stderr)]
    stages = re.findall(
        rb"Face-plane (command|source|branch|middle) begin(.*?)Face-plane \1 end", result.stderr, re.S,
    )
    scene_counts = [block.count(b"Workspace scene publication") for _, block in stages]
    expected_queries, expected_transitions = (1, 2) if args.single else (12, 32)
    counts_match = len(times) == expected_queries and len(stages) == expected_transitions
    scenes_match = bool(scene_counts) and all(value == 1 for value in scene_counts)
    digest = hashlib.sha256()
    with executable.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    report = {
        "ok": result.returncode == 0 and counts_match and scenes_match,
        "returncode": result.returncode, "seconds": round(elapsed, 3),
        "language": args.language, "theme": args.theme, "single": args.single, "spline": args.spline, "trimmed": args.trimmed,
        "executable_sha256": digest.hexdigest(), "face_queries": len(times),
        "query_milliseconds": times, "command_transitions": len(stages),
        "one_scene_per_transition": scenes_match, "scene_counts": scene_counts,
    }
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=True))
    if not report["ok"]:
        sys.stderr.write(result.stdout.decode("utf-8", errors="replace")[-2000:])
        sys.stderr.write(result.stderr.decode("utf-8", errors="replace")[-4000:])
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
