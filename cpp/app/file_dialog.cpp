#include "file_dialog.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>

namespace zima::app {
namespace {

class FileProxyModel final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const auto* files = qobject_cast<const QFileSystemModel*>(sourceModel());
        if (files != nullptr) {
            const QFileInfo info = files->fileInfo(files->index(row, 0, parent));
            if (info.isDir() && info.fileName().compare(
                    QStringLiteral("0000-index"), Qt::CaseInsensitive) == 0) return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        const auto* files = qobject_cast<const QFileSystemModel*>(sourceModel());
        if (files != nullptr) {
            const bool left_dir = files->fileInfo(left).isDir();
            const bool right_dir = files->fileInfo(right).isDir();
            if (left_dir != right_dir) {
                return sortOrder() == Qt::AscendingOrder ? left_dir : right_dir;
            }
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

QString choose_file(QWidget* parent, const QString& caption,
                    const QString& initial_path, const QString& name_filter,
                    QFileDialog::AcceptMode mode, const QString& suffix) {
    QFileDialog dialog(parent, caption, initial_path, name_filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::HideNameFilterDetails, false);
    dialog.setAcceptMode(mode);
    dialog.setFileMode(mode == QFileDialog::AcceptOpen
                           ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    dialog.setViewMode(QFileDialog::Detail);
    if (!suffix.isEmpty()) dialog.setDefaultSuffix(suffix);
    if (auto* files = dialog.findChild<QFileSystemModel*>()) {
        files->setNameFilterDisables(false);
    }
    auto* proxy = new FileProxyModel(&dialog);
    proxy->setDynamicSortFilter(true);
    dialog.setProxyModel(proxy);
    proxy->sort(0, Qt::AscendingOrder);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().front();
}

}  // namespace

QString open_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter) {
    return choose_file(parent, caption, initial_path, name_filter,
                       QFileDialog::AcceptOpen, {});
}

QString save_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QString& default_suffix) {
    return choose_file(parent, caption, initial_path, name_filter,
                       QFileDialog::AcceptSave, default_suffix);
}

}  // namespace zima::app
