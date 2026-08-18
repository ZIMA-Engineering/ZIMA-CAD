from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QFileInfo, QSize
from PySide6.QtWidgets import (
    QDialog,
    QFileDialog,
    QFileIconProvider,
    QListView,
    QTreeView,
    QWidget,
)

from zima_cad.icons import document_file_icon_name, resource_icon


class ZimaDocumentFileIconProvider(QFileIconProvider):
    """Decorate current ZIMA document files with their application icons."""

    def icon(self, file_info_or_type):
        if isinstance(file_info_or_type, QFileInfo):
            icon_name = document_file_icon_name(file_info_or_type.fileName())
            if icon_name is not None:
                return resource_icon(icon_name)
        return super().icon(file_info_or_type)


def create_zima_file_dialog(
    parent: QWidget | None,
    caption: str,
    initial_path: str | Path,
    name_filter: str,
    *,
    accept_mode: QFileDialog.AcceptMode,
    default_suffix: str = "",
) -> QFileDialog:
    """Build the shared non-native chooser used for ZIMA document files."""
    dialog = QFileDialog(parent)
    # Native OS dialogs ignore QFileIconProvider. The Qt dialog is required
    # for identical Part/Assembly/Drawing/Frame/Title-block icons on Linux and
    # Windows without installing host-wide MIME/file associations.
    dialog.setOption(QFileDialog.Option.DontUseNativeDialog, True)
    dialog.setWindowTitle(caption)
    dialog.setAcceptMode(accept_mode)
    dialog.setFileMode(
        QFileDialog.FileMode.ExistingFile
        if accept_mode == QFileDialog.AcceptMode.AcceptOpen
        else QFileDialog.FileMode.AnyFile
    )
    dialog.setViewMode(QFileDialog.ViewMode.Detail)
    filters = [part.strip() for part in name_filter.split(";;") if part.strip()]
    if filters:
        dialog.setNameFilters(filters)
    if default_suffix:
        dialog.setDefaultSuffix(default_suffix.lstrip("."))

    path = Path(initial_path)
    if path.is_dir():
        dialog.setDirectory(str(path))
    else:
        dialog.setDirectory(str(path.parent))
        dialog.selectFile(path.name)

    provider = ZimaDocumentFileIconProvider()
    dialog.setIconProvider(provider)
    # QFileDialog does not own custom providers consistently across Qt
    # bindings. Retain it for the complete dialog lifetime.
    dialog._zima_document_icon_provider = provider
    for view_type in (QListView, QTreeView):
        for view in dialog.findChildren(view_type):
            view.setIconSize(QSize(20, 20))
    return dialog


def get_zima_open_file_name(
    parent: QWidget | None,
    caption: str,
    initial_path: str | Path,
    name_filter: str,
) -> tuple[str, str]:
    dialog = create_zima_file_dialog(
        parent,
        caption,
        initial_path,
        name_filter,
        accept_mode=QFileDialog.AcceptMode.AcceptOpen,
    )
    if dialog.exec() != QDialog.DialogCode.Accepted:
        return "", ""
    selected_files = dialog.selectedFiles()
    return (
        selected_files[0] if selected_files else "",
        dialog.selectedNameFilter(),
    )


def get_zima_save_file_name(
    parent: QWidget | None,
    caption: str,
    initial_path: str | Path,
    name_filter: str,
    *,
    default_suffix: str = "",
) -> tuple[str, str]:
    dialog = create_zima_file_dialog(
        parent,
        caption,
        initial_path,
        name_filter,
        accept_mode=QFileDialog.AcceptMode.AcceptSave,
        default_suffix=default_suffix,
    )
    if dialog.exec() != QDialog.DialogCode.Accepted:
        return "", ""
    selected_files = dialog.selectedFiles()
    return (
        selected_files[0] if selected_files else "",
        dialog.selectedNameFilter(),
    )
