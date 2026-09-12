#include "document_tools_dialogs.hpp"
#include "file_dialog.hpp"
#include "table_entry.hpp"

#include <zima/document/relations.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/document/material_library.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <nlohmann/json.hpp>

#include <set>
#include <utility>

namespace zima::app {
namespace {

class ColumnNameDialog final : public zima::ui::PropertiesSubWindow {
public:
    ColumnNameDialog(std::function<bool(QString)> accepted,
                     const ApplicationSettings& settings, QWidget* parent)
        : PropertiesSubWindow(settings.text(
              "dialog.family_table.add_column", "Přidat sloupec"), parent),
          accepted_(std::move(accepted)) {
        setObjectName("familyTableColumnDialog");
        auto* form = new QFormLayout;
        name_ = new QLineEdit(this);
        name_->setObjectName("familyTableColumnName");
        form->addRow(settings.text(
            "dialog.family_table.column_name", "Název sloupce:"), name_);
        content_layout()->addLayout(form);
    }
protected:
    bool submit() override { return accepted_(name_->text().trimmed()); }
private:
    QLineEdit* name_{};
    std::function<bool(QString)> accepted_;
};

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


}  // namespace

UserParametersDialog::UserParametersDialog(
    UserParameterData data, QString language,
    std::function<void(UserParameterData)> accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.parameters.title", "Parametry"), parent),
      data_(std::move(data)), language_(std::move(language)),
      accepted_(std::move(accepted)) {
    setObjectName("documentParametersDialog");
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
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("documentParametersTable");
    table_->setHorizontalHeaderLabels({settings.text("column.key", "Klíč"),
        settings.text("column.shared", "Sdílená"),
        settings.text("column.label", "Popisek"),
        settings.text("column.value", "Hodnota")});
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->setColumnWidth(2, 160);
    table_->setItemDelegate(new EnterDownDelegate(table_));
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    content_layout()->addWidget(table_);
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
    table_->setRowCount(0);
    for (const auto& key : data_.order) {
        const int row = table_->rowCount(); table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(key)));
        auto* check = new QCheckBox(table_);
        check->setChecked(data_.values[key].contains(""));
        auto* cell = new QWidget(table_); auto* layout = new QHBoxLayout(cell);
        layout->setContentsMargins(0, 0, 0, 0); layout->setAlignment(Qt::AlignCenter);
        layout->addWidget(check); table_->setCellWidget(row, 1, cell);
        const auto label = data_.labels[key].find(language_.toStdString());
        table_->setItem(row, 2, new QTableWidgetItem(label == data_.labels[key].end()
            ? QString{} : QString::fromStdString(label->second)));
        const auto& values = data_.values[key];
        const auto shared = values.find("");
        const auto localized = values.find(language_.toStdString());
        table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(
            shared != values.end() ? shared->second :
            localized != values.end() ? localized->second : std::string{})));
    }
}

bool UserParametersDialog::read_table() {
    table_->clearFocus();
    std::vector<std::string> order;
    std::set<std::string> seen;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (!table_row_has_text(table_,row)) continue;
        const QString key_text = table_->item(row, 0) == nullptr ? QString{} :
            table_->item(row, 0)->text().trimmed();
        const std::string key = key_text.toStdString();
        if (key.empty() || !seen.insert(key).second) {
            QMessageBox::information(this, windowTitle(),
                key.empty() ? tr("Klíč parametru je povinný.") :
                tr("Klíč parametru musí být jedinečný."));
            return false;
        }
        order.push_back(key);
        const QString label = table_->item(row, 2) == nullptr ? QString{} :
            table_->item(row, 2)->text();
        const QString value = table_->item(row, 3) == nullptr ? QString{} :
            table_->item(row, 3)->text();
        if (label.isEmpty()) data_.labels[key].erase(language_.toStdString());
        else data_.labels[key][language_.toStdString()] = label.toStdString();
        auto* check = table_->cellWidget(row, 1) == nullptr ? nullptr :
            table_->cellWidget(row, 1)->findChild<QCheckBox*>();
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
    for(int column:{0,2,3}) table_->setItem(row,column,new QTableWidgetItem);
    auto* check=new QCheckBox(table_);check->setChecked(true);
    table_->setCellWidget(row,1,zima::ui::centered_cell_widget(check));
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
    content_layout()->addLayout(form);
}

bool FileSettingsDialog::submit() {
    for (const auto& [key, combo] : units_) data_.units[key] = combo->currentText().trimmed().toStdString();
    data_.precision["linear_tolerance"] = QString::number(linear_->value(), 'g', 15).toStdString();
    data_.precision["angular_tolerance"] = QString::number(angular_->value(), 'g', 15).toStdString();
    data_.precision["mesh_deflection"] = QString::number(mesh_->value(), 'g', 15).toStdString();
    data_.precision["decimal_places"] = QString::number(decimals_->value()).toStdString();
    try {
        zima::document::validate_file_settings({data_.units,data_.precision});
        accepted_(data_);
    } catch(const std::exception& error) {
        throw std::runtime_error(tr(error.what()).toStdString());
    }
    return true;
}

RelationsDialog::RelationsDialog(
    std::map<std::string, std::string> parameters,
    std::vector<zima::document::ModelRelation> relations,
    std::function<void(std::vector<zima::document::ModelRelation>)> accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.relations.title", "Relace"), parent),
      parameters_(std::move(parameters)), accepted_(std::move(accepted)) {
    setObjectName("relationsDialog"); resize(820, 440);
    auto* explanation = new QLabel(settings.text("dialog.relations.explanation",
        "Relace zapisují vypočítanou hodnotu do cílového parametru."), this);
    explanation->setWordWrap(true); content_layout()->addWidget(explanation);
    table_ = new QTableWidget(0, 2, this); table_->setObjectName("relationsTable");
    table_->setHorizontalHeaderLabels({settings.text("column.relation.target", "Cílový parametr"),
        settings.text("column.relation.expression", "Výraz")});
    table_->horizontalHeader()->setStretchLastSection(true);
    content_layout()->addWidget(table_);
    if (relations.empty()) add_row();
    else for (const auto& relation : relations) add_row(relation.target, relation.expression);
    new TableEntryRows(table_,[this]{add_row();});
}

void RelationsDialog::set_dimension_catalog(
    std::vector<zima::document::DimensionParameter> parameters,
    const zima::document::DimensionIdentifiers& identifiers) {
    std::sort(parameters.begin(), parameters.end(), [&](const auto& a, const auto& b) {
        const auto first = identifiers.identifier(a.owner_id, a.semantic_key);
        const auto second = identifiers.identifier(b.owner_id, b.semantic_key);
        return first.size() != second.size() ? first.size() < second.size() : first < second;
    });
    auto* title = new QLabel(tr("Identifikace kót v dokumentu (pro budoucí vzorce)"), this);
    content_layout()->addWidget(title);
    auto* catalog = new QTableWidget(static_cast<int>(parameters.size()), 3, this);
    catalog->setObjectName("documentDimensionIdentifiers");
    catalog->setHorizontalHeaderLabels({tr("Identifikace"), tr("Objekt"), tr("Kóta / parametr")});
    catalog->setEditTriggers(QAbstractItemView::NoEditTriggers);
    catalog->horizontalHeader()->setStretchLastSection(true);
    const auto parameter_label = [&](const std::string& semantic) -> QString {
        if (semantic.starts_with("dimension:")) return tr("Kóta skici");
        if (semantic.starts_with("corner_dimension:")) return tr("Poloměr rohu");
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
        const QStringList cells{QString::fromStdString(identifiers.identifier(parameter.owner_id, parameter.semantic_key)),
            QString::fromStdString(parameter.owner_name), parameter_label(parameter.semantic_key)};
        for (int column=0; column<cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells[column]);
            item->setToolTip(QString::fromStdString(parameter.owner_id + " / " + parameter.semantic_key));
            catalog->setItem(row, column, item);
        }
        ++row;
    }
    catalog->resizeColumnsToContents();
    content_layout()->addWidget(catalog);
    resize(820, 620);
}

void RelationsDialog::add_row(const std::string& target, const std::string& expression) {
    const int row = table_->rowCount(); table_->insertRow(row);
    auto* combo = new QComboBox(table_); combo->setEditable(true);
    for (const auto& [key, value] : parameters_) combo->addItem(QString::fromStdString(key));
    combo->setCurrentText(QString::fromStdString(target)); table_->setCellWidget(row, 0, combo);
    table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(expression)));
}

bool RelationsDialog::submit() {
    std::vector<zima::document::ModelRelation> relations;
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, 0));
        const QString target = combo == nullptr ? QString{} : combo->currentText().trimmed();
        const QString expression = table_->item(row, 1) == nullptr ? QString{} : table_->item(row, 1)->text().trimmed();
        if (target.isEmpty() && expression.isEmpty()) continue;
        relations.push_back({target.toStdString(),expression.toStdString()});
    }
    try { zima::document::validate_model_relations(relations); accepted_(std::move(relations)); }
    catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
    return true;
}

FamilyTableDialog::FamilyTableDialog(
    QString generic_name, DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.family_table.title", "Family Table"), parent),
      generic_name_(std::move(generic_name)), data_(std::move(data)),
      accepted_(std::move(accepted)), settings_(settings) {
    setObjectName("familyTableDialog"); resize(760, 440);
    nlohmann::json json;
    try { json = nlohmann::json::parse(data_.family_table); }
    catch (...) { json = {{"columns", nlohmann::json::array()}, {"instances", nlohmann::json::array()}}; }
    const auto columns = json.value("columns", std::vector<std::string>{});
    table_ = new QTableWidget(1, 1 + static_cast<int>(columns.size()), this);
    table_->setObjectName("familyTableTable");
    QStringList headers{settings.text("dialog.family_table.instance", "Instance")};
    for (const auto& column : columns) headers.push_back(QString::fromStdString(column));
    table_->setHorizontalHeaderLabels(headers); table_->setItem(0, 0, new QTableWidgetItem(generic_name_));
    table_->item(0, 0)->setFlags(table_->item(0, 0)->flags() & ~Qt::ItemIsEditable);
    for (int column = 1; column < table_->columnCount(); ++column) {
        table_->setItem(0, column, new QTableWidgetItem); table_->item(0, column)->setFlags(Qt::ItemIsEnabled);
    }
    for (const auto& instance : json.value("instances", nlohmann::json::array())) {
        const int row = table_->rowCount(); table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(instance.value("name", ""))));
        const auto values = instance.value("values", nlohmann::json::object());
        for (int column = 1; column < table_->columnCount(); ++column) {
            const auto key = table_->horizontalHeaderItem(column)->text().toStdString();
            table_->setItem(row, column, new QTableWidgetItem(QString::fromStdString(values.value(key, ""))));
        }
    }
    content_layout()->addWidget(table_);
    auto* actions = new QHBoxLayout;
    auto* add_column_button = new QPushButton(settings.text("dialog.family_table.add_column", "Přidat sloupec"));
    auto* delete_column = new QPushButton(settings.text("dialog.family_table.delete_column", "Smazat sloupec"));
    connect(add_column_button, &QPushButton::clicked, this, &FamilyTableDialog::add_column);
    connect(delete_column, &QPushButton::clicked, this, [this] {
        std::set<int, std::greater<>> columns;
        for (const auto& index : table_->selectionModel()->selectedIndexes())
            if (index.column() > 0) columns.insert(index.column());
        for (const int column : columns) table_->removeColumn(column);
    });
    for (auto* button : {add_column_button, delete_column}) actions->addWidget(button);
    actions->addStretch(); content_layout()->addLayout(actions);
    new TableEntryRows(table_,[this]{add_instance();},1);
}

void FamilyTableDialog::add_instance() {
    const int row=table_->rowCount();table_->insertRow(row);
    for(int column=0;column<table_->columnCount();++column) table_->setItem(row,column,new QTableWidgetItem);
}

void FamilyTableDialog::add_column() {
    if (findChild<QDialog*>("familyTableColumnDialog") != nullptr) return;
    auto* dialog = new ColumnNameDialog([this](const QString& name) {
        if (name.isEmpty()) return false;
        for (int column = 1; column < table_->columnCount(); ++column) {
            if (table_->horizontalHeaderItem(column)->text().trimmed() == name) return false;
        }
        const int column = table_->columnCount(); table_->insertColumn(column);
        table_->setHorizontalHeaderItem(column, new QTableWidgetItem(name));
        for (int row = 0; row < table_->rowCount(); ++row)
            table_->setItem(row, column, new QTableWidgetItem);
        return true;
    }, settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

bool FamilyTableDialog::submit() {
    zima::document::FamilyTable table;
    for(int column=1;column<table_->columnCount();++column)table.columns.push_back(table_->horizontalHeaderItem(column)->text().trimmed().toStdString());
    for(int row=1;row<table_->rowCount();++row) {
        if(!table_row_has_text(table_,row))continue;
        zima::document::FamilyInstance instance;
        instance.name=table_->item(row,0)?table_->item(row,0)->text().trimmed().toStdString():"";
        for(int column=1;column<table_->columnCount();++column)instance.values[table.columns[column-1]]=table_->item(row,column)?table_->item(row,column)->text().toStdString():"";
        table.instances.push_back(std::move(instance));
    }
    try {
        zima::document::validate_family_table(table,generic_name_.toStdString());
        data_.family_table=zima::document::serialize_family_table(table);accepted_(data_);return true;
    }catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
}

MaterialDialog::MaterialDialog(DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.material.title", "Materiál"), parent),
      data_(std::move(data)), accepted_(std::move(accepted)), settings_(settings) {
    setObjectName("materialDialog");
    setMinimumSize(620, 380);
    set_initial_size(QSize(1100, 700));
    setSizeGripEnabled(true);
    auto* top = new QHBoxLayout; top->addWidget(new QLabel(settings.text("dialog.material.current_data", "Data materiálu uložená v dokumentu")));
    top->addStretch(); auto* load = new QPushButton(settings.text("dialog.material.load", "Načíst z knihovny..."));
    load->setObjectName("loadMaterialLibrary");
    connect(load, &QPushButton::clicked, this, &MaterialDialog::load_library); top->addWidget(load); content_layout()->addLayout(top);
    table_ = new QTableWidget(0, 4, this); table_->setObjectName("materialTable");
    table_->setHorizontalHeaderLabels({settings.text("column.parameter", "Parametr"), settings.text("column.value", "Hodnota"), settings.text("column.unit", "Jednotka"), settings.text("column.description", "Popis")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); content_layout()->addWidget(table_);
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
    table_->setItem(row, 0, new QTableWidgetItem(name)); table_->setItem(row, 1, new QTableWidgetItem(value));
    auto* combo = new NoWheelComboBox(table_); combo->setEditable(false);
    for(const auto& choice:zima::document::material_unit_choices(name.toStdString()))combo->addItem(QString::fromStdString(choice));
    if(combo->count()==1 && combo->itemText(0).isEmpty())combo->setEnabled(false);
    combo->setCurrentText(unit); table_->setCellWidget(row, 2, combo);
    auto* description_item = new QTableWidgetItem(description);
    description_item->setFlags(description_item->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, 3, description_item);
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
            const auto key=table_->item(row,0)?table_->item(row,0)->text().trimmed().toStdString():"";
            const auto value=table_->item(row,1)?table_->item(row,1)->text().toStdString():"";
            if(!next.physical_parameters.emplace(key,value).second)throw std::invalid_argument("Material property names must be unique.");
            if(auto* combo=qobject_cast<QComboBox*>(table_->cellWidget(row,2));combo && !combo->currentText().isEmpty())next.physical_parameter_units[key]=combo->currentText().toStdString();
            if(table_->item(row,3) && !table_->item(row,3)->text().isEmpty())next.descriptions[key][settings_.language.toStdString()]=table_->item(row,3)->text().toStdString();
        }
        std::erase_if(next.descriptions,[&](const auto& entry){return !next.physical_parameters.contains(entry.first);});
        zima::document::validate_material({next.physical_parameters,next.physical_parameter_units,next.descriptions});
        accepted_(next);data_=std::move(next);return true;
    }catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
}

}  // namespace zima::app
