from __future__ import annotations

import unittest
import tempfile
from pathlib import Path
from types import SimpleNamespace

from PySide6.QtCore import QFileInfo, QSize, Qt
from PySide6.QtWidgets import QApplication, QFileDialog, QListView, QTreeView

from zima_cad.file_dialogs import (
    ZimaDocumentFileIconProvider,
    ZimaDocumentFileProxyModel,
    create_zima_file_dialog,
)
from zima_cad.icons import (
    document_file_icon_name,
    document_type_icon_name,
    resource_icon,
)
from zima_cad.app import MainWindow, NewDocumentDialog


class DocumentFileIconTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QApplication.instance() or QApplication([])

    def test_current_document_extensions_map_to_distinct_icons(self):
        expected = {
            ".prtz": "part",
            ".asmz": "assembly",
            ".drwz": "drawing",
            ".frmz": "drawing-format",
            ".tblz": "title-block",
        }

        for suffix, icon_name in expected.items():
            with self.subTest(suffix=suffix):
                self.assertEqual(
                    document_file_icon_name(f"DOCUMENT{suffix.upper()}"),
                    icon_name,
                )
        self.assertIsNone(document_file_icon_name("legacy.prt"))
        self.assertIsNone(document_file_icon_name("notes.txt"))

    def test_document_types_share_the_file_icon_names(self):
        self.assertEqual(document_type_icon_name("part"), "part")
        self.assertEqual(document_type_icon_name("assembly"), "assembly")
        self.assertEqual(document_type_icon_name("drawing"), "drawing")
        self.assertEqual(
            document_type_icon_name("drawing_format"), "drawing-format"
        )
        self.assertEqual(
            document_type_icon_name("title_block"), "title-block"
        )

    def test_document_tabs_use_every_dedicated_document_icon(self):
        window = MainWindow.__new__(MainWindow)
        for document_type, icon_name in {
            "part": "part",
            "assembly": "assembly",
            "drawing": "drawing",
            "drawing_format": "drawing-format",
            "title_block": "title-block",
        }.items():
            with self.subTest(document_type=document_type):
                document = SimpleNamespace(
                    document_settings={"type": document_type}
                )
                self.assertEqual(
                    window._document_tab_icon(document).cacheKey(),
                    resource_icon(icon_name).cacheKey(),
                )

    def test_new_document_dialog_uses_the_five_document_icons(self):
        dialog = NewDocumentDialog()
        self.addCleanup(dialog.close)

        for radio, icon_name in (
            (dialog.part_radio, "part"),
            (dialog.assembly_radio, "assembly"),
            (dialog.drawing_radio, "drawing"),
            (dialog.drawing_format_radio, "drawing-format"),
            (dialog.title_block_radio, "title-block"),
        ):
            with self.subTest(icon_name=icon_name):
                self.assertEqual(
                    radio.icon().cacheKey(), resource_icon(icon_name).cacheKey()
                )

    def test_file_icon_provider_returns_the_shared_resource_icons(self):
        provider = ZimaDocumentFileIconProvider()
        for suffix, icon_name in {
            ".prtz": "part",
            ".asmz": "assembly",
            ".drwz": "drawing",
            ".frmz": "drawing-format",
            ".tblz": "title-block",
        }.items():
            with self.subTest(suffix=suffix):
                provided = provider.icon(QFileInfo(f"document{suffix}"))
                self.assertEqual(
                    provided.cacheKey(),
                    resource_icon(icon_name).cacheKey(),
                )
                self.assertFalse(provided.pixmap(18, 18).isNull())

    def test_shared_dialog_uses_qt_detail_view_and_custom_provider(self):
        dialog = create_zima_file_dialog(
            None,
            "Open document",
            Path("Projects"),
            "Documents (*.prtz *.asmz *.drwz *.frmz *.tblz)",
            accept_mode=QFileDialog.AcceptMode.AcceptOpen,
        )
        self.addCleanup(dialog.close)

        self.assertTrue(
            dialog.testOption(QFileDialog.Option.DontUseNativeDialog)
        )
        self.assertEqual(dialog.viewMode(), QFileDialog.ViewMode.Detail)
        self.assertEqual(dialog.fileMode(), QFileDialog.FileMode.ExistingFile)
        self.assertIsInstance(
            dialog.iconProvider(), ZimaDocumentFileIconProvider
        )
        views = [
            *dialog.findChildren(QListView),
            *dialog.findChildren(QTreeView),
        ]
        self.assertTrue(views)
        self.assertTrue(all(view.iconSize() == QSize(20, 20) for view in views))

    def test_shared_save_dialog_keeps_default_document_suffix(self):
        dialog = create_zima_file_dialog(
            None,
            "Save assembly",
            Path("Projects/assembly.asmz"),
            "Assemblies (*.asmz)",
            accept_mode=QFileDialog.AcceptMode.AcceptSave,
            default_suffix=".asmz",
        )
        self.addCleanup(dialog.close)

        self.assertEqual(dialog.fileMode(), QFileDialog.FileMode.AnyFile)
        self.assertEqual(dialog.acceptMode(), QFileDialog.AcceptMode.AcceptSave)
        self.assertEqual(dialog.defaultSuffix(), "asmz")

    def test_shared_dialog_hides_only_the_managed_index_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "0000-index").mkdir()
            (root / "0000-INDEX").mkdir()
            (root / "ordinary-folder").mkdir()
            (root / "part.prtz").touch()
            (root / "notes.txt").touch()
            dialog = create_zima_file_dialog(
                None,
                "Open document",
                root,
                "Parts (*.prtz)",
                accept_mode=QFileDialog.AcceptMode.AcceptOpen,
            )
            self.addCleanup(dialog.close)
            proxy = dialog.proxyModel()

            self.assertIsInstance(proxy, ZimaDocumentFileProxyModel)
            source = proxy.sourceModel()
            root_index = source.index(str(root))
            # QFileSystemModel populates directory rows asynchronously.
            deadline = 100
            while source.rowCount(root_index) < 5 and deadline:
                self.application.processEvents()
                deadline -= 1
            visible_names = {
                proxy.data(proxy.index(row, 0, proxy.mapFromSource(root_index)))
                for row in range(proxy.rowCount(proxy.mapFromSource(root_index)))
            }
            self.assertNotIn("0000-index", visible_names)
            self.assertNotIn("0000-INDEX", visible_names)
            self.assertIn("ordinary-folder", visible_names)

            proxy_root = proxy.mapFromSource(root_index)
            proxy.sort(0, Qt.SortOrder.AscendingOrder)
            self.application.processEvents()
            ordered_entries = [
                (
                    proxy.data(proxy.index(row, 0, proxy_root)),
                    source.fileInfo(proxy.mapToSource(
                        proxy.index(row, 0, proxy_root)
                    )).isDir(),
                )
                for row in range(proxy.rowCount(proxy_root))
            ]
            first_file = next(
                (
                    index
                    for index, (_name, is_directory)
                    in enumerate(ordered_entries)
                    if not is_directory
                ),
                len(ordered_entries),
            )
            self.assertTrue(all(
                is_directory
                for _name, is_directory in ordered_entries[:first_file]
            ))
            self.assertTrue(all(
                not is_directory
                for _name, is_directory in ordered_entries[first_file:]
            ))


if __name__ == "__main__":
    unittest.main()
