#include "export_options_dialog.hpp"

#include <QCheckBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>

namespace zima::app {

ExportOptionsDialog::ExportOptionsDialog(const QString& path, QWidget* parent)
    : PropertiesSubWindow(tr("Exportovat"), parent) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setObjectName("exportOptionsDialog");
    setMinimumWidth(460);

    const QFileInfo file(path);
    auto* form = new QFormLayout;
    auto* name = new QLabel(file.fileName(), this);
    name->setObjectName("exportFileName");
    name->setTextFormat(Qt::PlainText);
    name->setToolTip(file.absoluteFilePath());
    form->addRow(tr("Soubor"), name);

    auto* format = new QLabel(file.suffix().toUpper(), this);
    format->setObjectName("exportFormat");
    format->setTextFormat(Qt::PlainText);
    form->addRow(tr("Formát"), format);

    skeletons_ = new QCheckBox(tr("Skeleton"), this);
    skeletons_->setObjectName("exportIncludeSkeleton");
    skeletons_->setChecked(false);
    form->addRow(QString{}, skeletons_);
    content_layout()->addLayout(form);
}

bool ExportOptionsDialog::include_skeletons() const { return skeletons_->isChecked(); }
bool ExportOptionsDialog::submit() { return true; }

}
