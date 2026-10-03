#include "document_tools_dialogs.hpp"
#include "relation_text_editor.hpp"
#include "numeric_expression_edit.hpp"
#include <zima/document/relation_program.hpp>
#include <zima/document/exact_unit_conversion.hpp>
#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>
#include "file_dialog.hpp"
#include "table_entry.hpp"
#include "resource_icon.hpp"

#include <zima/document/relations.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/document/material_library.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QMenu>
#include <QToolButton>
#include <QTimer>

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <nlohmann/json.hpp>

#include <set>
#include <algorithm>
#include <utility>

namespace zima::app {
namespace {



class NoWheelComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

QString value_or(const std::map<std::string, std::string>& values,
                 const char* key, const char* fallback) {
    const auto found = values.find(key);
    return QString::fromStdString(found == values.end() ? fallback : found->second);
}

// Keep the original native string for untouched cells: displaying inches must
// not round a stored millimetre value through a second floating-point conversion.
constexpr int family_native_role=Qt::UserRole;
constexpr int family_display_role=Qt::UserRole+1;
QString family_display_value(const QString& native,const zima::workspace::FamilyReference& reference) {
    bool valid{};const double value=native.toDouble(&valid);
    if(!valid||!std::isfinite(value)||reference.binding.kind!="dimension")return native;
    const auto source_unit=reference.unit=="deg"||reference.unit=="rad"?"deg":"mm";
    if(const auto exact=document::exact_decimal_unit_conversion(native.toStdString(),source_unit,reference.unit))
        return QString::fromStdString(*exact);
    // Ordinary Family input can be approximate in the selected display unit;
    // unchanged cells retain their exact native strings independently.
    auto result=QString::number(value/reference.native_scale,'g',17);
    if(value==0&&std::signbit(value)&&!result.startsWith('-'))result.prepend('-');
    return result;
}
ui::InputQuantity family_quantity(const zima::workspace::FamilyReference& reference) {
    if(reference.unit=="deg"||reference.unit=="rad")return ui::InputQuantity::Angle;
    return reference.unit.empty()?ui::InputQuantity::Scalar:ui::InputQuantity::Length;
}

}  // namespace

UserParametersDialog::UserParametersDialog(
    UserParameterData data, QString language,
    std::function<void(UserParameterData)> accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.parameters.title", "Parametry"), parent),
      data_(std::move(data)), language_(std::move(language)),
      accepted_(std::move(accepted)) {
    setObjectName("documentParametersDialog");
    setProperty("expandBottomTable", true);
    setMinimumSize(600, 420);
    set_initial_size(QSize(690, 560));
    auto* language_form = new QFormLayout;
    language_combo_ = new NoWheelComboBox(this);
    language_combo_->setObjectName("parameterLanguage");
    language_combo_->setEditable(true);
    std::set<QString> languages{"cs", "de", "en", "fr", "ru", language_};
    for (const auto& [key, localized] : data_.labels)
        for (const auto& [item_language, value] : localized)
            if (!item_language.empty()) languages.insert(QString::fromStdString(item_language));
    for (const auto& [key, localized] : data_.values)
        for (const auto& [item_language, value] : localized)
            if (!item_language.empty()) languages.insert(QString::fromStdString(item_language));
    for (const auto& item : languages) language_combo_->addItem(item);
    language_combo_->setCurrentText(language_);
    language_form->addRow(settings.text("label.language", "Jazyk"), language_combo_);
    content_layout()->addLayout(language_form);
    table_ = new QTableWidget(0, 6, this);
    table_->setObjectName("documentParametersTable");
    table_->setHorizontalHeaderLabels({QString{},settings.text("column.key", "Klíč"),
        settings.text("column.shared", "Sdílená"),
        settings.text("column.label", "Popisek"),
        settings.text("column.value", "Hodnota"),QString{}});
    table_->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Fixed);
    table_->setColumnWidth(5,34);
    table_->horizontalHeader()->moveSection(5,0);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->setColumnWidth(3, 160);
    table_->setItemDelegate(new EnterDownDelegate(table_));
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    content_layout()->addWidget(table_, 1);
    auto* ordering_buttons=new QHBoxLayout;
    move_up_=new QPushButton(zima::ui::reference_arrow_icon(Qt::UpArrow),tr("Nahoru"),this);
    move_down_=new QPushButton(zima::ui::reference_arrow_icon(Qt::DownArrow),tr("Dolů"),this);
    move_up_->setObjectName("parameterMoveUp");move_down_->setObjectName("parameterMoveDown");
    for(auto* button:{move_up_,move_down_}) {
        button->setIconSize({20,20});button->setAutoDefault(false);
        ordering_buttons->addWidget(button);
    }
    ordering_buttons->addStretch();content_layout()->addLayout(ordering_buttons);
    connect(move_up_,&QPushButton::clicked,this,[this]{move_row(-1);});
    connect(move_down_,&QPushButton::clicked,this,[this]{move_row(1);});
    connect(table_,&QTableWidget::itemChanged,this,[this]{update_order_controls();});
    connect(table_->model(),&QAbstractItemModel::rowsRemoved,this,[this]{update_order_controls();});
    const auto change_language = [this] {
            const QString next_language = language_combo_->currentText();
            if (next_language.trimmed().isEmpty() || next_language == language_) return;
            if (!read_table()) return;
            language_ = next_language.trimmed();
            populate();
        };
    connect(language_combo_, &QComboBox::activated, this,
        [change_language](int) { change_language(); });
    if (language_combo_->lineEdit() != nullptr)
        connect(language_combo_->lineEdit(), &QLineEdit::editingFinished,
            this, change_language);
    populate();
    new TableEntryRows(table_,[this]{add_row();});
}

void UserParametersDialog::populate() {
    const auto selected_key=ordering_index_.data().toString().trimmed();
    const QSignalBlocker block(table_);
    table_->setRowCount(0);
    for (const auto& key : data_.order) {
        const int row = table_->rowCount(); table_->insertRow(row);
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(key)));
        auto* check = new QCheckBox(table_);
        check->setChecked(data_.values[key].contains(""));
        auto* cell = new QWidget(table_); auto* layout = new QHBoxLayout(cell);
        layout->setContentsMargins(0, 0, 0, 0); layout->setAlignment(Qt::AlignCenter);
        layout->addWidget(check); table_->setCellWidget(row, 2, cell);
        const auto label = data_.labels[key].find(language_.toStdString());
        table_->setItem(row, 3, new QTableWidgetItem(label == data_.labels[key].end()
            ? QString{} : QString::fromStdString(label->second)));
        const auto& values = data_.values[key];
        const auto shared = values.find("");
        const auto localized = values.find(language_.toStdString());
        table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(
            shared != values.end() ? shared->second :
            localized != values.end() ? localized->second : std::string{})));
        add_order_control(row);
        if(!selected_key.isEmpty()&&selected_key==QString::fromStdString(key))
            ordering_index_=table_->model()->index(row,1);
    }
    update_order_controls();
}

void UserParametersDialog::add_order_control(int row) {
    auto* check=new QCheckBox(table_);
    check->setObjectName("parameterOrder");
    check->setToolTip(tr("Vybrat parametr pro přesun nahoru nebo dolů"));
    check->setEnabled(table_->item(row,1)&&!table_->item(row,1)->text().trimmed().isEmpty());
    const QPersistentModelIndex index(table_->model()->index(row,1));
    connect(check,&QCheckBox::toggled,this,[this,index](bool checked) {
        if(checked)ordering_index_=index;
        else if(ordering_index_==index)ordering_index_=QPersistentModelIndex{};
        update_order_controls();
    });
    table_->setCellWidget(row,5,zima::ui::centered_cell_widget(check));
}

void UserParametersDialog::update_order_controls() {
    const bool selected=ordering_index_.isValid()&&!ordering_index_.data().toString().trimmed().isEmpty();
    if(!selected)ordering_index_=QPersistentModelIndex{};
    bool before=false,after=false;
    for(int row=0;row<table_->rowCount();++row) {
        const bool populated=table_->item(row,1)&&!table_->item(row,1)->text().trimmed().isEmpty();
        if(auto* cell=table_->cellWidget(row,5))if(auto* check=cell->findChild<QCheckBox*>()) {
            const QSignalBlocker block(check);check->setEnabled(populated);
            check->setChecked(selected&&ordering_index_.row()==row);
        }
        if(populated&&selected) {
            before|=row<ordering_index_.row();after|=row>ordering_index_.row();
        }
    }
    move_up_->setEnabled(before);move_down_->setEnabled(after);
}

void UserParametersDialog::move_row(int direction) {
    if(!ordering_index_.isValid()||!read_table())return;
    const auto key=ordering_index_.data().toString().trimmed().toStdString();
    const auto selected=std::ranges::find(data_.order,key);
    if(selected==data_.order.end())return;
    const auto index=std::distance(data_.order.begin(),selected);
    const auto target=index+direction;
    if(target<0||target>=static_cast<std::ptrdiff_t>(data_.order.size()))return;
    std::swap(data_.order[index],data_.order[target]);
    populate();
    table_->scrollTo(ordering_index_);
}

bool UserParametersDialog::read_table() {
    table_->clearFocus();
    std::vector<std::string> order;
    std::set<std::string> seen;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (!table_row_has_text(table_,row)) continue;
        const QString key_text = table_->item(row, 1) == nullptr ? QString{} :
            table_->item(row, 1)->text().trimmed();
        const std::string key = key_text.toStdString();
        if (key.empty() || !seen.insert(key).second) {
            QMessageBox::information(this, windowTitle(),
                key.empty() ? tr("Klíč parametru je povinný.") :
                tr("Klíč parametru musí být jedinečný."));
            return false;
        }
        order.push_back(key);
        const QString label = table_->item(row, 3) == nullptr ? QString{} :
            table_->item(row, 3)->text();
        const QString value = table_->item(row, 4) == nullptr ? QString{} :
            table_->item(row, 4)->text();
        if (label.isEmpty()) data_.labels[key].erase(language_.toStdString());
        else data_.labels[key][language_.toStdString()] = label.toStdString();
        auto* check = table_->cellWidget(row, 2) == nullptr ? nullptr :
            table_->cellWidget(row, 2)->findChild<QCheckBox*>();
        if (check != nullptr && check->isChecked()) {
            data_.values[key][""] = value.toStdString();
            data_.values[key].erase(language_.toStdString());
        } else {
            data_.values[key][language_.toStdString()] = value.toStdString();
            data_.values[key].erase("");
        }
    }
    data_.order = std::move(order);
    for (auto it = data_.labels.begin(); it != data_.labels.end();) {
        if (!seen.contains(it->first)) it = data_.labels.erase(it); else ++it;
    }
    for (auto it = data_.values.begin(); it != data_.values.end();) {
        if (!seen.contains(it->first)) it = data_.values.erase(it); else ++it;
    }
    return true;
}

void UserParametersDialog::add_row() {
    const int row=table_->rowCount();table_->insertRow(row);
    for(int column:{1,3,4}) table_->setItem(row,column,new QTableWidgetItem);
    auto* check=new QCheckBox(table_);check->setChecked(true);
    table_->setCellWidget(row,2,zima::ui::centered_cell_widget(check));
    add_order_control(row);
}

bool UserParametersDialog::submit() {
    if (!read_table()) return false;
    try {
        zima::document::normalize_user_parameters(data_);
        accepted_(data_);
    } catch(const std::exception& error) {
        throw std::runtime_error(tr(error.what()).toStdString());
    }
    return true;
}

FileSettingsDialog::FileSettingsDialog(
    DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.file_settings.title", "Nastavení souboru"), parent),
      data_(std::move(data)), accepted_(std::move(accepted)) {
    setObjectName("fileSettingsDialog");
    auto* form = new QFormLayout;
    for (const auto& [key, values] : zima::document::file_unit_choices()) {
        auto* combo = new NoWheelComboBox(this);
        combo->setObjectName(QStringLiteral("fileUnit") + QString::fromStdString(key));
        for(const auto& value:values)combo->addItem(QString::fromStdString(value));
        combo->setCurrentText(value_or(data_.units, key.c_str(), values.front().c_str()));
        units_[key] = combo;
        form->addRow(settings.text(QStringLiteral("document.unit.") +
            QString::fromStdString(key).toLower(), QString::fromStdString(key)), combo);
    }
    auto* annotation_note=new QLabel(tr("Manufacturing limits remain unchanged. ≈ marks an approximate secondary value when exact conversion is not possible."),this);
    annotation_note->setObjectName("fileAnnotationUnitsNote");annotation_note->setWordWrap(true);form->addRow(annotation_note);
    const auto precision = [&](const char* key, const char* fallback) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QStringLiteral("filePrecision")+QString::fromLatin1(key));
        spin->setDecimals(9); spin->setRange(0.0, 1000000.0);
        spin->setValue(value_or(data_.precision, key, fallback).toDouble());
        return spin;
    };
    linear_ = precision("linear_tolerance", "0.001");
    angular_ = precision("angular_tolerance", "0.001");
    mesh_ = precision("mesh_deflection", "0.1");
    mesh_->setMinimum(0.000000001);
    decimals_ = new QSpinBox(this); decimals_->setObjectName("filePrecisiondecimal_places");decimals_->setRange(0, 12);
    decimals_->setValue(value_or(data_.precision, "decimal_places", "3").toInt());
    form->addRow(settings.text("document.precision.linear_tolerance", "Lineární tolerance"), linear_);
    form->addRow(settings.text("document.precision.angular_tolerance", "Úhlová tolerance"), angular_);
    form->addRow(settings.text("document.precision.mesh_deflection", "Odchylka triangulace"), mesh_);
    form->addRow(settings.text("document.precision.decimal_places", "Počet desetinných míst"), decimals_);
    if(data_.sheet_metal) {
        pages_=new QTabWidget(this);pages_->setObjectName("fileSettingsPages");
        auto* general=new QWidget(pages_);general->setLayout(form);pages_->addTab(general,tr("Obecné"));
        auto* sheet=new QWidget(pages_);sheet->setObjectName("sheetMetalSettingsPage");
        auto* sheet_form=new QFormLayout(sheet);
        thickness_=new QDoubleSpinBox(sheet);thickness_->setObjectName("sheetMetalThickness");
        thickness_->setDecimals(6);thickness_->setRange(.000001,1000000);thickness_->setSuffix(" mm");
        thickness_->setValue(data_.sheet_metal->thickness_mm.value_or(1));
        sheet_form->addRow(tr("Výchozí tloušťka materiálu"),thickness_);
        k_factor_=new QDoubleSpinBox(sheet);k_factor_->setObjectName("sheetMetalKFactor");
        k_factor_->setDecimals(6);k_factor_->setRange(0,1);k_factor_->setSingleStep(.01);k_factor_->setValue(data_.sheet_metal->k_factor);
        sheet_form->addRow(tr("Výchozí K faktor"),k_factor_);
        sheet_cut_tolerance_=new QDoubleSpinBox(sheet);sheet_cut_tolerance_->setObjectName("sheetCutTolerance");
        sheet_cut_tolerance_->setDecimals(6);sheet_cut_tolerance_->setRange(.000001,1.);
        sheet_cut_tolerance_->setSingleStep(.01);sheet_cut_tolerance_->setSuffix(" mm");
        sheet_cut_tolerance_->setValue(value_or(data_.precision,"sheet_cut_tolerance","0.05").toDouble());
        sheet_form->addRow(tr("Tolerance řezu plechem"),sheet_cut_tolerance_);
        auto* note=new QLabel(tr("Výchozí hodnoty jsou uložené v tomto dílu."),sheet);note->setWordWrap(true);sheet_form->addRow(note);
        pages_->addTab(sheet,tr("Plechy"));content_layout()->addWidget(pages_);
    } else content_layout()->addLayout(form);
}

void FileSettingsDialog::show_sheet_metal_page() {if(pages_)pages_->setCurrentIndex(1);}

bool FileSettingsDialog::submit() {
    for (const auto& [key, combo] : units_) data_.units[key] = combo->currentText().trimmed().toStdString();
    data_.precision["linear_tolerance"] = QString::number(linear_->value(), 'g', 15).toStdString();
    data_.precision["angular_tolerance"] = QString::number(angular_->value(), 'g', 15).toStdString();
    data_.precision["mesh_deflection"] = QString::number(mesh_->value(), 'g', 15).toStdString();
    data_.precision["decimal_places"] = QString::number(decimals_->value()).toStdString();
    try {
        if(data_.sheet_metal) {
            data_.sheet_metal->thickness_mm=thickness_->value();
            data_.sheet_metal->k_factor=k_factor_->value();
            data_.precision["sheet_cut_tolerance"]=QString::number(sheet_cut_tolerance_->value(),'g',15).toStdString();
        }
        zima::document::validate_file_settings({data_.units,data_.precision,data_.sheet_metal});
        accepted_(data_);
    } catch(const zima::document::RelationError& error) {
        throw std::runtime_error(tr("Line %1: %2 %3").arg(error.line).arg(tr(error.message.c_str()),QString::fromStdString(error.detail)).toStdString());
    } catch(const std::exception& error) {
        throw std::runtime_error(tr(error.what()).toStdString());
    }
    return true;
}

RelationsDialog::RelationsDialog(
    std::map<std::string, std::string> parameters,
    std::string relations,
    std::function<void(std::string)> accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.relations.title", "Relace"), parent),
      accepted_(std::move(accepted)) {
    setObjectName("relationsDialog"); resize(900, 680);
    setProperty("expandBottomTable", true);
    auto* explanation = new QLabel(tr("One assignment per line. Text uses quotes; & joins text. OK saves the source; Regenerate calculates it."), this);
    explanation->setWordWrap(true); content_layout()->addWidget(explanation);
    auto* actions=new QHBoxLayout;
    auto* import_button=new QPushButton(tr("Import text…"),this);import_button->setObjectName("relationsImport");
    auto* export_button=new QPushButton(tr("Export text…"),this);export_button->setObjectName("relationsExport");
    auto* insert_button=new QPushButton(tr("Insert text…"),this);insert_button->setObjectName("relationsInsertText");
    actions->addWidget(import_button);actions->addWidget(export_button);actions->addWidget(insert_button);actions->addStretch();content_layout()->addLayout(actions);
    editor_=new RelationTextEditor(this);editor_->setPlainText(QString::fromStdString(relations));content_layout()->addWidget(editor_,3);
    pick_dimension_=new QPushButton(zima::ui::reference_arrow_icon(Qt::RightArrow),tr("Insert dimension from View"),this);
    pick_dimension_->setObjectName("relationsPickDimension");pick_dimension_->setCheckable(true);actions->insertWidget(0,pick_dimension_);
    picked_dimension_=new QLabel(this);picked_dimension_->setObjectName("relationsPickedDimension");content_layout()->addWidget(picked_dimension_);
    connect(pick_dimension_,&QPushButton::toggled,this,[this](bool enabled){if(enabled)insertion_cursor_=editor_->textCursor();if(entry_changed)entry_changed();});
    connect(import_button,&QPushButton::clicked,this,[this]{
        const auto path=open_file(this,tr("Import relations"),{},tr("UTF-8 text (*.txt)"));if(path.isEmpty())return;
        QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024){QMessageBox::warning(this,windowTitle(),tr("Cannot read the relation text file (maximum 1 MiB)."));return;}
        QStringDecoder decoder(QStringDecoder::Utf8);const QString source=decoder(file.readAll());
        if(decoder.hasError()){QMessageBox::warning(this,windowTitle(),tr("The file is not valid UTF-8 text."));return;}
        editor_->selectAll();editor_->insertPlainText(source);
    });
    connect(export_button,&QPushButton::clicked,this,[this]{
        const auto path=save_file(this,tr("Export relations"),{},tr("UTF-8 text (*.txt)"),"txt");if(path.isEmpty())return;
        QSaveFile file(path);const auto bytes=editor_->toPlainText().toUtf8();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(this,windowTitle(),tr("Cannot save the relation text file."));
    });
    connect(insert_button,&QPushButton::clicked,this,[this]{
        class TextDialog final:public zima::ui::PropertiesSubWindow {
            QPlainTextEdit* text_;std::function<void(QString)> accepted_;
            bool submit() override {accepted_(text_->toPlainText());return true;}
        public:
            TextDialog(QWidget* parent,std::function<void(QString)> accepted):PropertiesSubWindow(QObject::tr("Insert text"),parent),accepted_(std::move(accepted)){
                setObjectName("relationInsertTextDialog");resize(480,260);text_=new QPlainTextEdit(this);text_->setObjectName("relationLiteralText");content_layout()->addWidget(text_,1);
            }
        };
        const QPointer<RelationsDialog> owner(this);
        auto* dialog=new TextDialog(parentWidget(),[owner](const QString& text){if(owner)owner->editor_->insertPlainText(QString::fromStdString(zima::document::quote_relation_text(text.toStdString())));});
        connect(this,&QObject::destroyed,dialog,&QWidget::close);
        dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();
    });
}

void RelationsDialog::set_dimension_catalog(
    std::vector<zima::document::DimensionParameter> parameters,
    const zima::document::DimensionIdentifiers& identifiers,
    const std::map<std::string,std::string>& values) {
    std::sort(parameters.begin(), parameters.end(), [&](const auto& a, const auto& b) {
        const auto first = identifiers.identifier(a.owner_id, a.semantic_key);
        const auto second = identifiers.identifier(b.owner_id, b.semantic_key);
        return first.size() != second.size() ? first.size() < second.size() : first < second;
    });
    auto* title = new QLabel(tr("Document dimensions — double-click to insert an identifier"), this);
    content_layout()->addWidget(title);
    auto* catalog = new QTableWidget(static_cast<int>(parameters.size()), 4, this);
    catalog->setObjectName("documentDimensionIdentifiers");
    catalog->setHorizontalHeaderLabels({tr("Identifikace"), tr("Objekt"), tr("Kóta / parametr"),tr("Hodnota")});
    catalog->setEditTriggers(QAbstractItemView::NoEditTriggers);
    catalog->horizontalHeader()->setStretchLastSection(true);
    const auto parameter_label = [&](const std::string& semantic) -> QString {
        if (semantic.starts_with("dimension:")) return tr("Kóta skici");
        if (semantic.starts_with("corner_dimension:")) return tr("Poloměr rohu");
        if (semantic.starts_with("parameter:pattern:spacing:")) return tr("Rozteč");
        if (semantic=="parameter:pattern:angle") return tr("Úhel");
        if (semantic.starts_with("parameter:pattern:count")) return tr("Počet");
        if (semantic.starts_with("parameter:pattern:reverse_count:")) return tr("Počet vzad");
        auto key = QString::fromStdString(semantic);
        const bool placement = key.startsWith("parameter:placement:");
        if (key.startsWith("placement-reference:") || key.contains("reference_offset:")) {
            const auto parts = key.split(':');
            const bool lower = parts.back() == "lower_limit";
            const bool upper = parts.back() == "upper_limit";
            const int index = parts[parts.size() - (lower || upper ? 2 : 1)].toInt() + 1;
            return lower ? tr("Vazba %1 – dolní mez").arg(index)
                : upper ? tr("Vazba %1 – horní mez").arg(index)
                : tr("Vazba %1 – odsazení / úhel").arg(index);
        }
        if (placement) key.remove(0, 20);
        else if (key.startsWith("parameter:")) key.remove(0, 10);
        const std::map<QString, QString> labels{
            {"x",tr("Poloha X")}, {"y",tr("Poloha Y")}, {"z",tr("Poloha Z")},
            {"rotation_x",tr("Natočení X")}, {"rotation_y",tr("Natočení Y")}, {"rotation_z",tr("Natočení Z")},
            {"length",tr("Délka")}, {"width",tr("Šířka")}, {"height",tr("Výška")},
            {"radius",tr("Poloměr")}, {"bottom_radius",tr("Dolní poloměr")}, {"top_radius",tr("Horní poloměr")},
            {"top_offset",tr("Horní posun")}, {"profile_offset",tr("Odsazení profilu")},
            {"length_forward",tr("Rozsah vpřed")}, {"length_reverse",tr("Rozsah vzad")},
            {"thin_thickness",tr("Tloušťka stěny")}, {"thickness",tr("Tloušťka")},
            {"angle",tr("Úhel")}, {"primary",tr("První rozměr")}, {"secondary",tr("Druhý rozměr")},
            {"treatment_angle",tr("Úhel sražení")}, {"diameter",tr("Průměr")},
            {"bore_diameter",tr("Průměr otvoru")}, {"bore_length",tr("Hloubka otvoru")},
            {"entrance_chamfer",tr("Vstupní sražení")}, {"exit_chamfer",tr("Výstupní sražení")},
            {"drill_point_angle",tr("Úhel hrotu")}, {"thread_diameter",tr("Průměr závitu")},
            {"thread_pitch",tr("Stoupání závitu")}, {"thread_length",tr("Délka závitu")},
            {"thread_designation",tr("Jmenovitý průměr závitu")}, {"pitch",tr("Stoupání")},
            {"chamfer_depth",tr("Hloubka sražení")}, {"chamfer_angle",tr("Úhel sražení")},
            {"runout_pitch_factor",tr("Součinitel výběhu")}, {"nominal_diameter",tr("Jmenovitý průměr")},
            {"root_diameter",tr("Průměr jádra")}, {"offset",tr("Odsazení")}};
        const auto found = labels.find(key);
        return found == labels.end() ? key : found->second;
    };
    int row = 0;
    for (const auto& parameter : parameters) {
        const auto name=identifiers.identifier(parameter.owner_id,parameter.semantic_key);
        const QStringList cells{QString::fromStdString(name),
            QString::fromStdString(parameter.owner_name), parameter_label(parameter.semantic_key),
            values.contains(name)?QString::fromStdString(values.at(name)):QString{}};
        for (int column=0; column<cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells[column]);
            item->setToolTip(QString::fromStdString(parameter.owner_id + " / " + parameter.semantic_key));
            catalog->setItem(row, column, item);
        }
        ++row;
    }
    catalog->resizeColumnsToContents();
    catalog->setMaximumHeight(170);
    connect(catalog,&QTableWidget::cellDoubleClicked,this,[this,catalog](int row,int){if(const auto* item=catalog->item(row,0))editor_->insertPlainText(item->text());});
    content_layout()->addWidget(catalog, 1);
    resize(820, 620);
}

bool RelationsDialog::submit() {
    try { const auto source=editor_->toPlainText().toStdString();static_cast<void>(zima::document::RelationProgram(source));accepted_(source); }
    catch(const zima::document::RelationError& error){editor_->show_error(error.line);throw std::runtime_error(tr("Line %1: %2 %3").arg(error.line).arg(tr(error.message.c_str()),QString::fromStdString(error.detail)).toStdString());}
    catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
    return true;
}
bool RelationsDialog::entering_dimension() const{return pick_dimension_->isChecked();}
void RelationsDialog::end_entry(){pick_dimension_->setChecked(false);}
void RelationsDialog::insert_dimension(const QString& identifier,const QString& value) {
    if(insertion_cursor_.isNull())insertion_cursor_=editor_->textCursor();
    insertion_cursor_.insertText(identifier);editor_->setTextCursor(insertion_cursor_);
    picked_dimension_->setText(tr("Selected dimension: %1 = %2").arg(identifier,value));
    end_entry();editor_->setFocus();
}

FamilyInstanceDialog::FamilyInstanceDialog(QString generic_name,const zima::document::FamilyTable& model,
    const std::string& selected_row,bool replacing,std::function<void(const std::string&)> accepted,QWidget* parent)
    : PropertiesSubWindow(replacing?tr("Replace — vybrat variantu"):tr("Vložit — vybrat variantu"),parent),accepted_(std::move(accepted)) {
    setObjectName("componentFamilyDialog");setProperty("expandBottomTable",true);
    setMinimumSize(430,280);set_initial_size(QSize(560,360));
    auto* heading=new QLabel(tr("Vyberte výchozí model nebo variantu Family Table."),this);
    heading->setWordWrap(true);content_layout()->addWidget(heading);
    table_=new QTableWidget(static_cast<int>(model.instances.size()+1),2,this);
    table_->setObjectName("componentFamilyTable");
    table_->setHorizontalHeaderLabels({tr("Název"),tr("Typ")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->hide();table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(std::max(28,table_->fontMetrics().height()+10));
    table_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    table_->setItem(0,0,new QTableWidgetItem(std::move(generic_name)));
    table_->item(0,0)->setData(Qt::UserRole,QString{});
    table_->setItem(0,1,new QTableWidgetItem(tr("Výchozí (nativní)")));
    int selected=0;
    for(std::size_t i=0;i<model.instances.size();++i) {
        const auto& row=model.instances[i];const auto index=static_cast<int>(i+1);
        table_->setItem(index,0,new QTableWidgetItem(QString::fromStdString(row.name)));
        table_->item(index,0)->setData(Qt::UserRole,QString::fromStdString(row.id));
        table_->setItem(index,1,new QTableWidgetItem(tr("Varianta")));
        if(row.id==selected_row)selected=index;
    }
    table_->selectRow(selected);content_layout()->addWidget(table_,1);
    connect(table_,&QTableWidget::cellDoubleClicked,this,[this]{buttons()->button(QDialogButtonBox::Ok)->click();});
}
bool FamilyInstanceDialog::submit() {
    if(table_->currentRow()<0)return false;
    accepted_(table_->item(table_->currentRow(),0)->data(Qt::UserRole).toString().toStdString());return true;
}

FamilyTableDialog::FamilyTableDialog(
    QString generic_name, DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.family_table.title", "Family Table"), parent),
      generic_name_(std::move(generic_name)), data_(std::move(data)),
      accepted_(std::move(accepted)), settings_(settings) {
    setObjectName("familyTableDialog");setMinimumSize(600,280);
    setProperty("expandBottomTable", true);
    set_initial_size(QSize(2280,440));setSizeGripEnabled(true);
    const auto model=data_.family_table.empty()?zima::document::FamilyTable{}:zima::document::parse_family_table(data_.family_table);
    auto* language_form=new QFormLayout;
    language_combo_=new NoWheelComboBox(this);language_combo_->setObjectName("familyLabelLanguage");
    language_combo_->addItems({"cs","en","de","fr","ru"});
    language_combo_->setFixedWidth(90);
    language_combo_->setCurrentText(settings.language);
    label_language_=language_combo_->currentText();
    language_form->addRow(tr("Jazyk popisků"),language_combo_);content_layout()->addLayout(language_form);
    table_=new QTableWidget(1,4,this);table_->setObjectName("familyTableTable");
    table_->setHorizontalHeaderItem(0,new QTableWidgetItem);
    // The shared reference delegate uses the established light reference text.
    auto palette=table_->palette();palette.setColor(QPalette::Base,QColor("#20252b"));
    palette.setColor(QPalette::Text,QColor("#e6edf3"));table_->setPalette(palette);
    table_->setHorizontalHeaderItem(1,new QTableWidgetItem(settings.text("dialog.family_table.instance","Variant")));
    table_->setHorizontalHeaderItem(2,new QTableWidgetItem(tr("Sdílený")));
    table_->setHorizontalHeaderItem(3,new QTableWidgetItem(tr("Lokalizace")));
    table_->setColumnWidth(2,80);table_->setColumnWidth(3,220);
    for(const int column:{2,3}) {
        auto* item=new QTableWidgetItem;item->setFlags(Qt::NoItemFlags);table_->setItem(0,column,item);
    }
    table_->setColumnWidth(1,200);table_->setItem(0,1,new QTableWidgetItem(generic_name_));
    table_->item(0,1)->setFlags(Qt::ItemIsEnabled);zima::ui::install_reference_cell_delegate(table_);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    for(const auto& name:model.columns) {
        const auto& binding=model.bindings.at(name);
        columns_.push_back(zima::workspace::FamilyReference{binding,name,name,{}});
        table_->setColumnCount(4+2*static_cast<int>(columns_.size()));
    }
    for(const auto& row:model.instances) {
        add_instance();const int r=table_->rowCount()-1;table_->item(r,1)->setText(QString::fromStdString(row.name));
        table_->item(r,1)->setData(Qt::UserRole,QString::fromStdString(row.id));
        QVariantMap labels;for(const auto& [language,label]:row.labels)labels[QString::fromStdString(language)]=QString::fromStdString(label);
        table_->item(r,1)->setData(Qt::UserRole+1,labels);
        table_->item(r,2)->setCheckState(row.shared_name?Qt::Checked:Qt::Unchecked);
        for(int i=0;i<static_cast<int>(model.columns.size());++i)if(auto found=row.values.find(model.columns[i]);found!=row.values.end()) {
            auto* cell=table_->item(r,4+2*i);const auto native=QString::fromStdString(found->second);
            cell->setText(native);cell->setData(family_native_role,native);
        }
    }
    refresh_labels();
    connect(language_combo_,&QComboBox::currentTextChanged,this,[this](const QString& language){
        store_labels();label_language_=language;refresh_labels();
    });
    connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if(item->row()>0 && item->column()==2){store_labels();refresh_labels();}
    });
    content_layout()->addWidget(new QLabel(settings.text("dialog.family_table.hint",
        "Click a base cell, then pick a solid in View. Double-click the solid to show its dimensions. Use the row header icon to open a variant."),this));
    content_layout()->addWidget(table_, 1);
    auto* actions=new QHBoxLayout;
    auto* add=new QPushButton(settings.text("dialog.family_table.add_column","Add column"),this);add->setObjectName("familyAddColumn");
    auto* remove=new QPushButton(settings.text("dialog.family_table.delete_column","Delete column"),this);remove->setObjectName("familyDeleteColumn");
    connect(add,&QPushButton::clicked,this,&FamilyTableDialog::add_column);
    connect(remove,&QPushButton::clicked,this,[this]{
        const int i=active_column_>=0?active_column_:(table_->currentColumn()-4)/2;
        if(i<0||i>=static_cast<int>(columns_.size())||(active_column_<0&&table_->currentColumn()<4))return;
        end_entry();table_->removeColumn(4+2*i);table_->removeColumn(4+2*i);columns_.erase(columns_.begin()+i);
        inspected_.clear();refresh_references();
    });
    actions->addWidget(add);actions->addWidget(remove);actions->addStretch();content_layout()->addLayout(actions);
    connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(row==0&&column>=4&&(column%2)==0)arm_column((column-4)/2);});
    const auto open_row=[this](int row){
        if(row<=0||!table_->item(row,1)||table_->item(row,1)->text().trimmed().isEmpty())return;
        requested_instance_=table_->item(row,1)->text().trimmed().toStdString();buttons()->button(QDialogButtonBox::Ok)->click();
    };
    connect(table_,&QWidget::customContextMenuRequested,this,[this,open_row](const QPoint& point){
        const auto item=table_->itemAt(point);if(!item||item->row()<=0)return;
        QMenu menu(this);auto* action=menu.addAction(settings_.text("dialog.family_table.open","Open variant"));
        if(menu.exec(table_->viewport()->mapToGlobal(point))==action)open_row(item->row());
    });
    if(columns_.empty())add_column();else refresh_references();
    new TableEntryRows(table_,[this]{add_instance();},1,open_row,resource_icon("open"));
}
void FamilyTableDialog::set_references(std::vector<zima::workspace::FamilyReference> references) {
    references_=std::move(references);
    for(auto& column:columns_)if(column) {
        const auto match=std::ranges::find_if(references_,[&](const auto& r){return r.binding==column->binding;});
        if(match!=references_.end()) {const auto saved_name=column->name;column=*match;column->name=saved_name;}
    }
    refresh_references();
}
void FamilyTableDialog::refresh_references() {
    const QSignalBlocker blocker(table_);table_->clearSpans();
    for(int i=0;i<static_cast<int>(columns_.size());++i) {
        const int c=4+2*i;const auto& column=columns_[i];
        table_->setColumnWidth(c,150);table_->setColumnWidth(c+1,30);
        auto header=column?QString::fromStdString(column->name):QStringLiteral("+");
        if(column&&!column->unit.empty())header+=QStringLiteral(" [%1]").arg(QString::fromStdString(column->unit));
        table_->setHorizontalHeaderItem(c,new QTableWidgetItem(header));
        table_->setHorizontalHeaderItem(c+1,new QTableWidgetItem);
        auto* ref=new zima::ui::ReferenceCellItem(column?(column->binding.kind=="dimension"
            ?family_display_value(QString::fromStdString(column->value),*column):QString::fromStdString(column->owner_name))
            :settings_.text("dialog.family_table.pick","Pick a solid or dimension"));
        if(column) {
            ref->set_reference(QString::fromStdString(column->binding.owner_id+":"+column->binding.semantic_key));
            ref->setToolTip(QString::fromStdString(column->owner_name+" / "+column->binding.semantic_key));
            ref->set_missing(std::ranges::none_of(references_,[&](const auto& r){return r.binding==column->binding;}));
        }
        ref->set_active_input(active_column_==i);ref->set_inspected(inspected_.contains(i));table_->setItem(0,c,ref);
        auto* eye=zima::ui::build_reference_inspection_button(column.has_value(),inspected_.contains(i),[this,i](bool checked){
            if(checked)inspected_.insert(i);else inspected_.erase(i);refresh_references();if(entry_changed)entry_changed();
        });
        if(auto* old=table_->cellWidget(0,c+1))old->hide();
        table_->setCellWidget(0,c+1,zima::ui::centered_cell_widget(eye));
        for(int row=1;row<table_->rowCount();++row) {
            if(!table_->item(row,c))table_->setItem(row,c,new QTableWidgetItem);
            auto* cell=table_->item(row,c);
            // A reference refresh must never overwrite an edited cell. Missing
            // references retain their native text until their unit is known.
            if(column&&!column->unit.empty()&&cell->data(family_native_role).isValid()&&
                !cell->data(family_display_role).isValid()) {
                const auto display=family_display_value(cell->data(family_native_role).toString(),*column);
                cell->setText(display);cell->setData(family_display_role,display);
            }
            const auto value=cell->text();
            table_->setSpan(row,c,1,2);
            if(column&&column->binding.kind!="dimension") {
                if(auto* old=table_->cellWidget(row,c))old->hide();
                auto* combo=new QComboBox(table_);combo->addItem(QString(),QString());
                // The item retains the canonical yes/no value for persistence.
                // Cover its text before painting the localized cell editor.
                combo->setBackgroundRole(QPalette::Base);
                combo->setAutoFillBackground(true);
                combo->addItem(settings_.text("dialog.family_table.yes","Yes"),"yes");combo->addItem(settings_.text("dialog.family_table.no","No"),"no");
                combo->setToolTip(settings_.text("dialog.family_table.inherit","Empty = use the base value"));
                combo->setCurrentIndex(std::max(0,combo->findData(value)));table_->setCellWidget(row,c,combo);
                const QPersistentModelIndex index(table_->model()->index(row,c));
                connect(combo,&QComboBox::currentIndexChanged,this,[this,combo,index]{if(index.isValid())table_->item(index.row(),index.column())->setText(combo->currentData().toString());});
            } else {if(auto* old=table_->cellWidget(row,c))old->hide();table_->removeCellWidget(row,c);}
        }
    }
}
void FamilyTableDialog::arm_column(int column) {
    active_column_=column;refresh_references();if(entry_changed)entry_changed();
}
void FamilyTableDialog::choose_reference(const zima::workspace::FamilyReference& reference) {
    if(active_column_<0||active_column_>=static_cast<int>(columns_.size()))return;
    for(int i=0;i<static_cast<int>(columns_.size());++i)if(i!=active_column_&&columns_[i]&&columns_[i]->binding==reference.binding)return;
    auto selected=reference;const auto original=selected.name;int suffix=2;
    const auto used=[&](const std::string& name){for(int i=0;i<static_cast<int>(columns_.size());++i)if(i!=active_column_&&columns_[i]&&columns_[i]->name==name)return true;return false;};
    while(used(selected.name))selected.name=original+" ("+std::to_string(suffix++)+")";
    const int column=4+2*active_column_;
    if(!columns_[active_column_]||columns_[active_column_]->binding!=reference.binding)
        for(int row=1;row<table_->rowCount();++row)if(auto* cell=table_->item(row,column)) {
            cell->setText({});cell->setData(family_native_role,QVariant{});cell->setData(family_display_role,QVariant{});
        }
    columns_[active_column_]=std::move(selected);refresh_references();if(entry_changed)entry_changed();
}
void FamilyTableDialog::end_entry(){active_column_=-1;inspected_.clear();refresh_references();if(entry_changed)entry_changed();}
std::vector<zima::document::FamilyColumn> FamilyTableDialog::inspected_references() const {
    std::vector<zima::document::FamilyColumn> result;for(const auto i:inspected_)if(columns_[i])result.push_back(columns_[i]->binding);return result;
}
void FamilyTableDialog::add_instance() {
    const QSignalBlocker blocker(table_);
    const int row=table_->rowCount();table_->insertRow(row);
    for(int column=1;column<table_->columnCount();++column)table_->setItem(row,column,new QTableWidgetItem);
    table_->item(row,2)->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);
    table_->item(row,2)->setCheckState(Qt::Checked);
    table_->item(row,3)->setFlags(Qt::ItemIsSelectable);
    refresh_references();
}
void FamilyTableDialog::add_column() {
    const int i=static_cast<int>(columns_.size());columns_.push_back(std::nullopt);table_->setColumnCount(4+2*static_cast<int>(columns_.size()));arm_column(i);
}
void FamilyTableDialog::store_labels() {
    const QSignalBlocker blocker(table_);
    for(int row=1;row<table_->rowCount();++row) {
        auto* name=table_->item(row,1);auto* label=table_->item(row,3);
        if(!name || !label || !label->flags().testFlag(Qt::ItemIsEditable))continue;
        auto labels=name->data(Qt::UserRole+1).toMap();const auto value=label->text().trimmed();
        if(value.isEmpty())labels.remove(label_language_);else labels[label_language_]=value;
        name->setData(Qt::UserRole+1,labels);
    }
}
void FamilyTableDialog::refresh_labels() {
    const QSignalBlocker blocker(table_);
    for(int row=1;row<table_->rowCount();++row) {
        const auto* name=table_->item(row,1);auto* label=table_->item(row,3);
        const bool shared=table_->item(row,2)->checkState()==Qt::Checked;
        label->setText(name->data(Qt::UserRole+1).toMap().value(label_language_).toString());
        label->setFlags(shared?Qt::ItemFlags(Qt::ItemIsSelectable):Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsEditable);
        label->setToolTip(tr("Prázdná lokalizace použije název varianty."));
    }
}
zima::document::FamilyTable FamilyTableDialog::read_table() const {
    zima::document::FamilyTable result;
    for(const auto& column:columns_)if(column){result.columns.push_back(column->name);result.bindings[column->name]=column->binding;}
    for(int row=1;row<table_->rowCount();++row) {
        if(!table_row_has_text(table_,row)&&table_->item(row,1)->data(Qt::UserRole+1).toMap().isEmpty())continue;
        zima::document::FamilyInstance instance;instance.name=table_->item(row,1)->text().trimmed().toStdString();
        instance.id=table_->item(row,1)->data(Qt::UserRole).toString().toStdString();
        instance.shared_name=table_->item(row,2)->checkState()==Qt::Checked;
        const auto labels=table_->item(row,1)->data(Qt::UserRole+1).toMap();
        for(auto it=labels.begin();it!=labels.end();++it)instance.labels[it.key().toStdString()]=it.value().toString().toStdString();
        for(int i=0;i<static_cast<int>(columns_.size());++i)if(columns_[i]) {
            const auto* cell=table_->item(row,4+2*i);auto value=cell->text().trimmed();
            if(columns_[i]->binding.kind=="dimension"&&!value.isEmpty()) {
                const auto original=cell->data(family_native_role);
                const auto display=cell->data(family_display_role);
                if(original.isValid()&&value==(display.isValid()?display.toString():original.toString()))value=original.toString();
                else try {
                    const double native=quantity_expression_value(value,family_quantity(*columns_[i]),columns_[i]->native_scale);
                    if(!std::isfinite(native))throw std::invalid_argument("Family dimensions must be finite numbers.");
                    value=QString::number(native,'g',17);
                    if(native==0&&std::signbit(native)&&!value.startsWith('-'))value.prepend('-');
                } catch(const std::exception&) {
                    throw std::invalid_argument(tr("Invalid value in row %1, column %2.")
                        .arg(row).arg(QString::fromStdString(columns_[i]->name)).toStdString());
                }
            }
            if(!value.isEmpty()||cell->data(family_native_role).isValid())
                instance.values[columns_[i]->name]=value.toStdString();
        }
        result.instances.push_back(std::move(instance));
    }
    zima::document::validate_family_table(result,generic_name_.toStdString());return result;
}
bool FamilyTableDialog::submit() {
    store_labels();
    data_.family_table=zima::document::serialize_family_table(read_table());accepted_(data_);
    if(!requested_instance_.empty()&&open_instance)open_instance(requested_instance_);
    return true;
}

MaterialDialog::MaterialDialog(DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.material.title", "Materiál"), parent),
      data_(std::move(data)), accepted_(std::move(accepted)), settings_(settings) {
    setObjectName("materialDialog");
    setProperty("expandBottomTable", true);
    setMinimumSize(620, 380);
    set_initial_size(QSize(1100, 700));
    setSizeGripEnabled(true);
    auto* top = new QHBoxLayout; top->addWidget(new QLabel(settings.text("dialog.material.current_data", "Data materiálu uložená v dokumentu")));
    top->addStretch(); auto* load = new QPushButton(settings.text("dialog.material.load", "Načíst z knihovny..."));
    load->setObjectName("loadMaterialLibrary");
    connect(load, &QPushButton::clicked, this, &MaterialDialog::load_library); top->addWidget(load); content_layout()->addLayout(top);
    table_ = new QTableWidget(0, 5, this); table_->setObjectName("materialTable");
    table_->setHorizontalHeaderLabels({QString{},settings.text("column.parameter", "Parametr"), settings.text("column.value", "Hodnota"), settings.text("column.unit", "Jednotka"), settings.text("column.description", "Popis")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); content_layout()->addWidget(table_, 1);
    populate();
    new TableEntryRows(table_,[this]{add_row();});
}

void MaterialDialog::populate() {
    table_->setRowCount(0);
    for (const auto& [key, value] : data_.physical_parameters) {
        const auto unit = data_.physical_parameter_units.find(key);
        const auto description = data_.descriptions.find(key);
        QString description_text;
        if (description != data_.descriptions.end()) {
            auto localized = description->second.find(settings_.language.toStdString());
            if (localized != description->second.end()) description_text = QString::fromStdString(localized->second);
        }
        add_row(QString::fromStdString(key), QString::fromStdString(value), unit == data_.physical_parameter_units.end() ? QString{} : QString::fromStdString(unit->second), description_text);
    }
}

void MaterialDialog::add_row(const QString& name, const QString& value, const QString& unit, const QString& description) {
    const int row = table_->rowCount(); table_->insertRow(row);
    table_->setItem(row, 1, new QTableWidgetItem(name)); table_->setItem(row, 2, new QTableWidgetItem(value));
    auto* combo = new NoWheelComboBox(table_); combo->setEditable(false);
    for(const auto& choice:zima::document::material_unit_choices(name.toStdString()))combo->addItem(QString::fromStdString(choice));
    if(combo->count()==1 && combo->itemText(0).isEmpty())combo->setEnabled(false);
    combo->setCurrentText(unit); table_->setCellWidget(row, 3, combo);
    auto* description_item = new QTableWidgetItem(description);
    description_item->setFlags(description_item->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, 4, description_item);
}

void MaterialDialog::load_library() {
    const QString file = open_file(this, settings_.text("file.select_material", "Vybrat materiál"), settings_.resolved_paths.value("Materials"), settings_.text("file.filter.material", "Materiál ZIMA-CAD (*.matz)"), settings_.translations);
    if (file.isEmpty()) return;
    try {
        auto material=zima::document::load_material_library(std::filesystem::u8path(file.toStdString()));
        data_.physical_parameters=std::move(material.properties);data_.physical_parameter_units=std::move(material.units);data_.descriptions=std::move(material.descriptions);
        populate();
    }catch(const std::exception& error){QMessageBox::warning(this,windowTitle(),tr(error.what()));}
}

bool MaterialDialog::submit() {
    auto next=data_;next.physical_parameters.clear();next.physical_parameter_units.clear();
    try {
        for(int row=0;row<table_->rowCount();++row) {
            if(!table_row_has_text(table_,row))continue;
            const auto key=table_->item(row,1)?table_->item(row,1)->text().trimmed().toStdString():"";
            const auto value=table_->item(row,2)?table_->item(row,2)->text().toStdString():"";
            if(!next.physical_parameters.emplace(key,value).second)throw std::invalid_argument("Material property names must be unique.");
            if(auto* combo=qobject_cast<QComboBox*>(table_->cellWidget(row,3));combo && !combo->currentText().isEmpty())next.physical_parameter_units[key]=combo->currentText().toStdString();
            if(table_->item(row,4) && !table_->item(row,4)->text().isEmpty())next.descriptions[key][settings_.language.toStdString()]=table_->item(row,4)->text().toStdString();
        }
        std::erase_if(next.descriptions,[&](const auto& entry){return !next.physical_parameters.contains(entry.first);});
        zima::document::validate_material({next.physical_parameters,next.physical_parameter_units,next.descriptions});
        accepted_(next);data_=std::move(next);return true;
    }catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
}

}  // namespace zima::app
