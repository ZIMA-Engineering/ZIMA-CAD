import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon, QPixmap
from PySide6.QtWidgets import QApplication, QLabel, QMessageBox

from zima_cad.paths import app_path
from zima_cad.settings import resolve_startup_context


class PersistentSplashScreen(QLabel):
    """Top-level startup image unaffected by native splash auto-dismissal."""

    def __init__(self, pixmap: QPixmap) -> None:
        super().__init__()
        self.setWindowFlags(
            Qt.WindowType.Window
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
        )
        self.setPixmap(pixmap)
        self.setFixedSize(pixmap.size())
        screen = QApplication.primaryScreen()
        if screen is not None:
            self.move(screen.availableGeometry().center() - self.rect().center())

    def mousePressEvent(self, event) -> None:
        event.accept()


def bootstrap() -> int:
    try:
        startup_context, qt_arguments = resolve_startup_context(sys.argv[1:])
    except ValueError as exc:
        application = QApplication(sys.argv)
        QMessageBox.critical(None, "ZIMA-CAD", str(exc))
        return 2

    application = QApplication([sys.argv[0], *qt_arguments])
    application_icon = QIcon(
        str(app_path("resources", "branding", "app-icon.svg"))
    )
    application.setWindowIcon(application_icon)
    splash_pixmap = QPixmap(
        str(app_path("resources", "branding", "splash.svg"))
    )
    splash = None
    if not splash_pixmap.isNull():
        splash = PersistentSplashScreen(splash_pixmap)
        splash.setWindowIcon(application_icon)
        splash.show()
        splash.raise_()
        application.processEvents()

    # Import the CAD kernel and the complete UI only after the splash is
    # already mapped. These are the expensive parts of application startup.
    from zima_cad.app import main

    return main(
        application=application,
        startup_context=startup_context,
        splash=splash,
    )


if __name__ == "__main__":
    raise SystemExit(bootstrap())
