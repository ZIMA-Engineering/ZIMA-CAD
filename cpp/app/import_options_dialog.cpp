#include "import_options_dialog.hpp"
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>
namespace zima::app {
ImportOptionsDialog::ImportOptionsDialog(const QString& path,double default_deflection,QWidget* parent)
    : PropertiesSubWindow(tr("Nastavení importu"),parent) {
    if (!std::isfinite(default_deflection) || default_deflection<=0)
        throw std::invalid_argument("Invalid default import mesh deflection");
    setAttribute(Qt::WA_DeleteOnClose,true);
    setMinimumWidth(460);
    const QFileInfo file(path);
    auto* form=new QFormLayout;
    const auto row=[&](const QString& title,const QString& value,const char* name) {
        auto* label=new QLabel(this);label->setTextFormat(Qt::PlainText);
        label->setText(value);label->setWordWrap(true);label->setObjectName(name);
        form->addRow(title,label);return label;
    };
    row(tr("Soubor"),file.fileName(),"importFileName")->setToolTip(file.absoluteFilePath());
    row(tr("Velikost"),QLocale().formattedDataSize(file.size())+tr(" (%1 B)").arg(QLocale().toString(file.size())),"importFileSize");
    const auto suffix=file.suffix().toLower();
    const bool step=suffix=="stp" || suffix=="step";
    row(tr("Formát"),step?QStringLiteral("STEP"):QStringLiteral("IGES"),"importFormat");
    if(step) {
        QFile input(path);
        if(input.open(QIODevice::ReadOnly)) {
            // Header only: no geometry transfer or unbounded pre-import scan.
            const auto header=QString::fromLatin1(input.read(65536));
            const QRegularExpression schema(QStringLiteral("FILE_SCHEMA\\s*\\(\\s*\\((.*?)\\)"),
                QRegularExpression::CaseInsensitiveOption|QRegularExpression::DotMatchesEverythingOption);
            const auto match=schema.match(header);
            if(match.hasMatch()) {
                auto value=match.captured(1).simplified();value.remove(QChar(39));
                row(tr("Schéma STEP"),value,"importSchema");
            }
        }
    }
    deflection_=new QDoubleSpinBox(this);deflection_->setObjectName("importMeshDeflection");
    deflection_->setDecimals(9);deflection_->setRange(1e-9,std::max(1e6,default_deflection));
    deflection_->setValue(default_deflection);deflection_->setSingleStep(0.05);deflection_->setSuffix(tr(" mm"));
    form->addRow(tr("Jemnost zobrazení — odchylka"),deflection_);
    content_layout()->addLayout(form);
    auto* help=new QLabel(tr("Menší odchylka vytváří jemnější síť a více dat. Přesná geometrie zůstává zachovaná. Volba platí pro tento import."),this);
    help->setWordWrap(true);content_layout()->addWidget(help);
}
double ImportOptionsDialog::mesh_deflection() const { return deflection_->value(); }
bool ImportOptionsDialog::submit() { return std::isfinite(mesh_deflection()) && mesh_deflection()>0; }
}
