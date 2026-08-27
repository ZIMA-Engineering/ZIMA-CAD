from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import (
    QFileInfo,
    QModelIndex,
    QSize,
    QSortFilterProxyModel,
    Qt,
)
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


class ZimaDocumentFileProxyModel(QSortFilterProxyModel):
    """Hide application-managed directories from ZIMA document choosers."""

    HIDDEN_DIRECTORY_NAMES = frozenset({"0000-index"})

    def filterAcceptsRow(
        self,
        source_row: int,
        source_parent: QModelIndex,
    ) -> bool:
        source = self.sourceModel()
        if source is None:
            return True
        index = source.index(source_row, 0, source_parent)
        file_info = source.fileInfo(index)
        if (
            file_info.isDir()
            and file_info.fileName().casefold()
            in self.HIDDEN_DIRECTORY_NAMES
        ):
            return False
        return super().filterAcceptsRow(source_row, source_parent)

    def lessThan(self, left: QModelIndex, right: QModelIndex) -> bool:
        source = self.sourceModel()
        if source is not None:
            left_is_directory = source.fileInfo(left).isDir()
            right_is_directory = source.fileInfo(right).isDir()
            if left_is_directory != right_is_directory:
                # QSortFilterProxyModel reverses comparisons for descending
                # order. Compensate so directories remain in the first group.
                return (
                    left_is_directory
                    if self.sortOrder() == Qt.SortOrder.AscendingOrder
                    else right_is_directory
                )
        return super().lessThan(left, right)


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
        # False means non-matching files are hidden rather than merely
        # disabled. Directories remain available for navigation, except for
        # the application-managed entries removed by the proxy below.
        dialog.setOption(QFileDialog.Option.HideNameFilterDetails, False)
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
    proxy = ZimaDocumentFileProxyModel(dialog)
    proxy.setDynamicSortFilter(True)
    dialog.setProxyModel(proxy)
    source_model = proxy.sourceModel()
    if hasattr(source_model, "setNameFilterDisables"):
        source_model.setNameFilterDisables(False)
    # QFileDialog does not own custom providers consistently across Qt
    # bindings. Retain it for the complete dialog lifetime.
    dialog._zima_document_icon_provider = provider
    dialog._zima_document_proxy_model = proxy
    for view_type in (QListView, QTreeView):
        for view in dialog.findChildren(view_type):
            view.setIconSize(QSize(20, 20))
            if isinstance(view, QTreeView):
                view.sortByColumn(0, Qt.SortOrder.AscendingOrder)
    proxy.sort(0, Qt.SortOrder.AscendingOrder)
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
