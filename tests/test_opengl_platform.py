import unittest

from zima_cad.opengl_platform import opengl_platform_config


class OpenGLPlatformTests(unittest.TestCase):
    def test_linux_keeps_wayland_opengl_es_configuration(self):
        config = opengl_platform_config("linux")
        self.assertEqual(config.renderable_type, "gles")
        self.assertEqual(config.version, (3, 0))
        self.assertEqual(config.shader_version, "300 es")
        self.assertTrue(config.shader_precision)

    def test_windows_uses_desktop_opengl_configuration(self):
        config = opengl_platform_config("win32")
        self.assertEqual(config.renderable_type, "desktop")
        self.assertEqual(config.version, (3, 3))
        self.assertEqual(config.shader_version, "330 core")
        self.assertFalse(config.shader_precision)


if __name__ == "__main__":
    unittest.main()
