import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class LinuxLauncherTests(unittest.TestCase):
    def run_launcher(self, environment):
        repository = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copy2(repository / "zima-cad", root / "zima-cad")
            python = root / "runtime" / "linux" / "python" / "bin" / "python"
            python.parent.mkdir(parents=True)
            python.symlink_to(sys.executable)
            (root / "main.py").write_text(
                "import os\nprint(os.environ.get('QT_QPA_PLATFORM', '<auto>'))\n",
                encoding="utf-8",
            )
            process_environment = os.environ.copy()
            for name in ("QT_QPA_PLATFORM", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "DISPLAY"):
                process_environment.pop(name, None)
            process_environment.update(environment)
            process_environment["ZIMA_CAD_GPU"] = "system"
            return subprocess.run(
                [str(root / "zima-cad")],
                check=True,
                capture_output=True,
                text=True,
                env=process_environment,
            ).stdout.strip()

    def test_wayland_session_uses_native_wayland_backend(self):
        self.assertEqual(
            self.run_launcher({
                "XDG_SESSION_TYPE": "wayland",
                "WAYLAND_DISPLAY": "wayland-0",
                "DISPLAY": ":0",
            }),
            "wayland",
        )

    def test_x11_session_uses_xcb_backend(self):
        self.assertEqual(
            self.run_launcher({"XDG_SESSION_TYPE": "x11", "DISPLAY": ":0"}),
            "xcb",
        )

    def test_explicit_qt_backend_override_is_preserved(self):
        self.assertEqual(
            self.run_launcher({
                "QT_QPA_PLATFORM": "offscreen",
                "XDG_SESSION_TYPE": "wayland",
                "WAYLAND_DISPLAY": "wayland-0",
            }),
            "offscreen",
        )

    def test_headless_environment_leaves_backend_to_qt(self):
        self.assertEqual(self.run_launcher({}), "<auto>")


if __name__ == "__main__":
    unittest.main()
