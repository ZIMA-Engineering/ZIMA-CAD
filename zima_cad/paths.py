from __future__ import annotations

import sys
from enum import Enum
from pathlib import Path


class RuntimePlatform(str, Enum):
    WINDOWS = "windows"
    LINUX = "linux"


def application_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent.parent


def app_path(*parts: str) -> Path:
    return application_root().joinpath(*parts)


def current_runtime_platform() -> RuntimePlatform:
    if sys.platform.startswith("win"):
        return RuntimePlatform.WINDOWS
    if sys.platform.startswith("linux"):
        return RuntimePlatform.LINUX
    raise RuntimeError(f"Unsupported platform for ZIMA-CAD runtime: {sys.platform}")


def runtime_path(platform_name: RuntimePlatform | str | None = None) -> Path:
    platform_dir = platform_name or current_runtime_platform()
    if isinstance(platform_dir, RuntimePlatform):
        platform_dir = platform_dir.value
    return app_path("runtime", platform_dir)


def ensure_application_directories() -> None:
    directories = (
        ("Projects",),
        ("config",),
        ("config", "materials"),
        ("config", "templates"),
        ("config", "formats"),
        ("resources",),
        ("resources", "icons"),
        ("resources", "templates"),
        ("resources", "materials"),
        ("runtime",),
        ("runtime", RuntimePlatform.WINDOWS.value),
        ("runtime", RuntimePlatform.LINUX.value),
        ("app",),
    )
    for directory_parts in directories:
        app_path(*directory_parts).mkdir(parents=True, exist_ok=True)
