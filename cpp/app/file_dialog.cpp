#include "file_dialog.hpp"
#include "resource_icon.hpp"
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QIcon>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListView>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTreeView>

#include <utility>

namespace zima::app {
namespace {

class FileProxyModel final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void set_name_header(QString value) { name_header_ = std::move(value); }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override {
        if (section == 0 && orientation == Qt::Horizontal &&
            role == Qt::DisplayRole && !name_header_.isEmpty()) {
            return name_header_;
        }
        return QSortFilterProxyModel::headerData(section, orientation, role);
    }

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

private:
    QString name_header_;
};

class ZimaDocumentFileIconProvider final : public QFileIconProvider {
public:
    QIcon icon(const QFileInfo& info) const override {
        const QString suffix = info.suffix().toLower();
        QString icon_name;
        if (suffix == QStringLiteral("prtz")) icon_name = QStringLiteral("part");
        else if (suffix == QStringLiteral("asmz")) icon_name = QStringLiteral("assembly");
        else if (suffix == QStringLiteral("drwz")) icon_name = QStringLiteral("drawing");
        else if (suffix == QStringLiteral("frmz")) icon_name = QStringLiteral("drawing-format");
        else if (suffix == QStringLiteral("tblz")) icon_name = QStringLiteral("title-block");
        if (!icon_name.isEmpty()) return resource_icon(icon_name);
        return QFileIconProvider::icon(info);
    }
};

class Utf8FileNameDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QString displayText(const QVariant& value,
                        const QLocale& locale) const override {
        const QString text = QStyledItemDelegate::displayText(value, locale);
        // Some Qt builds decode the UTF-8 XDG user-directory names through
        // Latin-1 in QFileDialog's places sidebar (for example StaÅ¾enÃ©).
        // Repair only strings carrying the characteristic mojibake markers.
        if (!text.contains(QChar(0x00c3)) && !text.contains(QChar(0x00c5)))
            return text;
        const QByteArray latin1 = text.toLatin1();
        if (latin1.contains('?')) return text;
        const QString repaired = QString::fromUtf8(latin1);
        return repaired.contains(QChar::ReplacementCharacter) ? text : repaired;
    }
};

void localize_file_views(QFileDialog& dialog,
                         const QMap<QString, QString>& translations) {
    for (auto* view : dialog.findChildren<QListView*>()) {
        view->setIconSize(QSize(20, 20));
        view->setItemDelegate(new Utf8FileNameDelegate(view));
    }
    for (auto* view : dialog.findChildren<QTreeView*>()) {
        view->setIconSize(QSize(20, 20));
        view->setItemDelegate(new Utf8FileNameDelegate(view));
        view->sortByColumn(0, Qt::AscendingOrder);
    }
}

QString choose_file(QWidget* parent, const QString& caption,
                    const QString& initial_path, const QString& name_filter,
                    QFileDialog::AcceptMode mode, const QString& suffix,
                    const QMap<QString, QString>& translations) {
    QFileDialog dialog(parent);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowTitle(caption);
    const QStringList filters = name_filter.split(
        QStringLiteral(";;"), Qt::SkipEmptyParts);
    if (!filters.isEmpty()) dialog.setNameFilters(filters);
    dialog.setOption(QFileDialog::HideNameFilterDetails, false);
    dialog.setAcceptMode(mode);
    dialog.setFileMode(mode == QFileDialog::AcceptOpen
                           ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    dialog.setViewMode(QFileDialog::Detail);
    if (!suffix.isEmpty()) dialog.setDefaultSuffix(suffix);
    const QFileInfo initial(initial_path);
    if (initial.isDir()) {
        dialog.setDirectory(initial.absoluteFilePath());
    } else {
        dialog.setDirectory(initial.absolutePath());
        dialog.selectFile(initial.fileName());
    }
    if (auto* label = dialog.findChild<QLabel*>("lookInLabel"))
        label->setText(translations.value("file.dialog.look_in", label->text()));
    if (auto* label = dialog.findChild<QLabel*>("fileNameLabel"))
        label->setText(translations.value("file.dialog.file_name", label->text()));
    if (auto* label = dialog.findChild<QLabel*>("fileTypeLabel"))
        label->setText(translations.value("file.dialog.file_type", label->text()));
    if (auto* buttons = dialog.findChild<QDialogButtonBox*>()) {
        if (auto* accept = buttons->button(mode == QFileDialog::AcceptOpen
                ? QDialogButtonBox::Open : QDialogButtonBox::Save)) {
            accept->setText(translations.value(
                mode == QFileDialog::AcceptOpen ? "button.open" : "button.save",
                accept->text()));
        }
        if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
            cancel->setText(translations.value("button.cancel", cancel->text()));
    }
    if (auto* files = dialog.findChild<QFileSystemModel*>()) {
        files->setNameFilterDisables(false);
    }
    ZimaDocumentFileIconProvider icon_provider;
    dialog.setIconProvider(&icon_provider);
    auto* proxy = new FileProxyModel(&dialog);
    proxy->set_name_header(
        translations.value("file.dialog.name", QStringLiteral("Name")));
    proxy->setDynamicSortFilter(true);
    dialog.setProxyModel(proxy);
    localize_file_views(dialog, translations);
    proxy->sort(0, Qt::AscendingOrder);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().front();
}

}  // namespace

QString open_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QMap<QString, QString>& translations) {
    return choose_file(parent, caption, initial_path, name_filter,
                       QFileDialog::AcceptOpen, {}, translations);
}

QString save_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QString& default_suffix,
                  const QMap<QString, QString>& translations) {
    return choose_file(parent, caption, initial_path, name_filter,
                       QFileDialog::AcceptSave, default_suffix, translations);
}

QString choose_directory(QWidget* parent, const QString& caption,
                         const QString& initial_path,
                         const QMap<QString, QString>& translations) {
    QFileDialog dialog(parent);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowTitle(caption);
    if (!initial_path.trimmed().isEmpty()) dialog.setDirectory(initial_path);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setViewMode(QFileDialog::Detail);
    if (auto* label = dialog.findChild<QLabel*>("lookInLabel"))
        label->setText(translations.value("file.dialog.look_in", label->text()));
    if (auto* label = dialog.findChild<QLabel*>("fileNameLabel"))
        label->setText(translations.value("file.dialog.directory", label->text()));
    if (auto* buttons = dialog.findChild<QDialogButtonBox*>()) {
        if (auto* accept = buttons->button(QDialogButtonBox::Open))
            accept->setText(translations.value("button.select", accept->text()));
        if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
            cancel->setText(translations.value("button.cancel", cancel->text()));
    }
    auto* proxy = new FileProxyModel(&dialog);
    proxy->set_name_header(
        translations.value("file.dialog.name", QStringLiteral("Name")));
    proxy->setDynamicSortFilter(true);
    dialog.setProxyModel(proxy);
    localize_file_views(dialog, translations);
    proxy->sort(0, Qt::AscendingOrder);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().front();
}

}  // namespace zima::app
