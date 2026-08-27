from __future__ import annotations

from dataclasses import dataclass
import sys


@dataclass(frozen=True)
class OpenGLPlatformConfig:
    renderable_type: str
    version: tuple[int, int]
    profile: str
    shader_version: str
    shader_precision: bool


def opengl_platform_config(
    platform_name: str | None = None,
) -> OpenGLPlatformConfig:
    platform = sys.platform if platform_name is None else platform_name
    if platform.startswith("linux"):
        return OpenGLPlatformConfig(
            renderable_type="gles",
            version=(3, 0),
            profile="none",
            shader_version="300 es",
            shader_precision=True,
        )
    if platform.startswith("win"):
        return OpenGLPlatformConfig(
            renderable_type="desktop",
            version=(3, 3),
            profile="core",
            shader_version="330 core",
            shader_precision=False,
        )
    raise RuntimeError(
        f"Unsupported platform for ZIMA-CAD OpenGL: {platform}"
    )


OPENGL_CONFIG = opengl_platform_config()


def platform_shader(source: str) -> str:
    shader = source.replace(
        "#version 300 es",
        f"#version {OPENGL_CONFIG.shader_version}",
        1,
    )
    if not OPENGL_CONFIG.shader_precision:
        shader = shader.replace("precision highp float;\n", "")
    return shader
