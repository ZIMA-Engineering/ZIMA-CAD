#include "document_tools_dialogs.hpp"
#include "file_dialog.hpp"

#include <zima/document/relations.hpp>

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
#include <QSettings>
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

void add_delete_row_buttons(QVBoxLayout* layout, QTableWidget* table,
                            const ApplicationSettings& settings,
                            const std::function<void()>& add) {
    auto* row = new QHBoxLayout;
    auto* add_button = new QPushButton(settings.text("button.add", "Přidat"));
    auto* delete_button = new QPushButton(settings.text("button.delete", "Odstranit"));
    QObject::connect(add_button, &QPushButton::clicked, add_button, add);
    QObject::connect(delete_button, &QPushButton::clicked, table, [table] {
        std::set<int, std::greater<>> rows;
        for (const auto& index : table->selectionModel()->selectedIndexes())
            rows.insert(index.row());
        for (const int selected : rows) table->removeRow(selected);
    });
    row->addWidget(add_button);
    row->addWidget(delete_button);
    row->addStretch();
    layout->addLayout(row);
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
    setMinimumSize(760, 420);
    set_initial_size(QSize(920, 560));
    auto* language_form = new QFormLayout;
    language_combo_ = new NoWheelComboBox(this);
    language_combo_->setObjectName("parameterLanguage");
    language_combo_->setEditable(true);
    std::set<QString> languages{"cs", "de", "en", "fr", language_};
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
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    content_layout()->addWidget(table_);
    auto* actions = new QHBoxLayout;
    auto* add = new QPushButton(settings.text("button.add", "Přidat"));
    auto* remove = new QPushButton(settings.text("button.delete", "Odstranit"));
    connect(add, &QPushButton::clicked, this, &UserParametersDialog::add_row);
    connect(remove, &QPushButton::clicked, this, &UserParametersDialog::delete_rows);
    actions->addWidget(add); actions->addWidget(remove); actions->addStretch();
    content_layout()->addLayout(actions);
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
    if (!read_table()) return;
    int index = 1; std::string key;
    do { key = QStringLiteral("param%1").arg(index++, 3, 10, QLatin1Char('0')).toStdString(); }
    while (std::find(data_.order.begin(), data_.order.end(), key) != data_.order.end());
    data_.order.push_back(key); data_.labels[key][language_.toStdString()] = key;
    data_.values[key][""] = ""; populate();
}

void UserParametersDialog::delete_rows() {
    std::set<int, std::greater<>> rows;
    for (const auto& index : table_->selectionModel()->selectedIndexes()) rows.insert(index.row());
    for (const int row : rows) table_->removeRow(row);
    static_cast<void>(read_table());
}

bool UserParametersDialog::submit() {
    if (!read_table()) return false;
    data_.flat.clear();
    for (const auto& key : data_.order) {
        const auto values = data_.values.find(key);
        if (values == data_.values.end()) continue;
        const auto shared = values->second.find("");
        if (shared != values->second.end()) data_.flat[key] = shared->second;
    }
    accepted_(std::move(data_));
    return true;
}

FileSettingsDialog::FileSettingsDialog(
    DocumentToolData data, ToolDataAccepted accepted,
    const ApplicationSettings& settings, QWidget* parent)
    : PropertiesSubWindow(settings.text("dialog.file_settings.title", "Nastavení souboru"), parent),
      data_(std::move(data)), accepted_(std::move(accepted)) {
    setObjectName("fileSettingsDialog");
    auto* form = new QFormLayout;
    const std::map<std::string, QStringList> choices{
        {"Length", {"mm", "cm", "m", "in"}}, {"Angle", {"deg", "rad"}},
        {"Mass", {"kg", "g", "t", "lb"}}, {"Time", {"s", "min"}},
        {"Temperature", {"C", "K", "F"}},
        {"Stress", {"Pa", "kPa", "MPa", "GPa", "psi"}}};
    for (const auto& [key, values] : choices) {
        auto* combo = new NoWheelComboBox(this);
        combo->setObjectName(QStringLiteral("fileUnit") + QString::fromStdString(key));
        combo->setEditable(true);
        combo->addItems(values);
        combo->setCurrentText(value_or(data_.units, key.c_str(), values.front().toUtf8()));
        units_[key] = combo;
        form->addRow(settings.text(QStringLiteral("document.unit.") +
            QString::fromStdString(key).toLower(), QString::fromStdString(key)), combo);
    }
    const auto precision = [&](const char* key, const char* fallback) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setDecimals(9); spin->setRange(0.0, 1000000.0);
        spin->setValue(value_or(data_.precision, key, fallback).toDouble());
        return spin;
    };
    linear_ = precision("linear_tolerance", "0.001");
    angular_ = precision("angular_tolerance", "0.001");
    mesh_ = precision("mesh_deflection", "0.1");
    mesh_->setMinimum(0.000000001);
    decimals_ = new QSpinBox(this); decimals_->setRange(0, 12);
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
    accepted_(std::move(data_));
    return true;
}

RelationsDialog::RelationsDialog(
    std::map<std::string, std::string> parameters,
    std::vector<zima::document::ModelRelation> relations,
    std::function<void(std::map<std::string, std::string>,
        std::vector<zima::document::ModelRelation>)> accepted,
    const ApplicationSettings& settings, QWidget* parent,
    std::map<std::string, double> model_values, int decimal_places)
    : PropertiesSubWindow(settings.text("dialog.relations.title", "Relace"), parent),
      parameters_(std::move(parameters)), accepted_(std::move(accepted)),
      model_values_(std::move(model_values)), decimal_places_(decimal_places) {
    setObjectName("relationsDialog"); resize(820, 440);
    auto* explanation = new QLabel(settings.text("dialog.relations.explanation",
        "Relace zapisují vypočítanou hodnotu do cílového parametru."), this);
    explanation->setWordWrap(true); content_layout()->addWidget(explanation);
    table_ = new QTableWidget(0, 2, this); table_->setObjectName("relationsTable");
    table_->setHorizontalHeaderLabels({settings.text("column.relation.target", "Cílový parametr"),
        settings.text("column.relation.expression", "Výraz")});
    table_->horizontalHeader()->setStretchLastSection(true);
    content_layout()->addWidget(table_);
    if (relations.empty()) add_row("mass", "model.mass");
    else for (const auto& relation : relations) add_row(relation.target, relation.expression);
    add_delete_row_buttons(content_layout(), table_, settings, [this] { add_row(); });
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
    std::set<std::string> targets;
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, 0));
        const QString target = combo == nullptr ? QString{} : combo->currentText().trimmed();
        const QString expression = table_->item(row, 1) == nullptr ? QString{} : table_->item(row, 1)->text().trimmed();
        if (target.isEmpty() && expression.isEmpty()) continue;
        static const QRegularExpression identifier(
            QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (target.isEmpty() || !identifier.match(target).hasMatch() ||
            expression.isEmpty() || targets.contains(target.toStdString())) {
            QMessageBox::warning(this, windowTitle(), tr("Cíl a výraz musí být neprázdné a cíle jedinečné."));
            return false;
        }
        targets.insert(target.toStdString()); relations.push_back({target.toStdString(), expression.toStdString()});
    }
    try { parameters_ = zima::document::evaluate_relations(
        parameters_, relations, model_values_, decimal_places_); }
    catch (const std::exception& error) { QMessageBox::warning(this, windowTitle(), error.what()); return false; }
    accepted_(std::move(parameters_), std::move(relations));
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
    auto* add_instance_button = new QPushButton(settings.text("dialog.family_table.add_instance", "Přidat instanci"));
    auto* delete_instance = new QPushButton(settings.text("button.delete", "Odstranit"));
    auto* add_column_button = new QPushButton(settings.text("dialog.family_table.add_column", "Přidat sloupec"));
    auto* delete_column = new QPushButton(settings.text("dialog.family_table.delete_column", "Smazat sloupec"));
    connect(add_instance_button, &QPushButton::clicked, this, &FamilyTableDialog::add_instance);
    connect(delete_instance, &QPushButton::clicked, this, [this] {
        std::set<int, std::greater<>> rows;
        for (const auto& index : table_->selectionModel()->selectedIndexes())
            if (index.row() > 0) rows.insert(index.row());
        for (const int row : rows) table_->removeRow(row);
    });
    connect(add_column_button, &QPushButton::clicked, this, &FamilyTableDialog::add_column);
    connect(delete_column, &QPushButton::clicked, this, [this] {
        std::set<int, std::greater<>> columns;
        for (const auto& index : table_->selectionModel()->selectedIndexes())
            if (index.column() > 0) columns.insert(index.column());
        for (const int column : columns) table_->removeColumn(column);
    });
    for (auto* button : {add_instance_button, delete_instance, add_column_button, delete_column}) actions->addWidget(button);
    actions->addStretch(); content_layout()->addLayout(actions);
}

void FamilyTableDialog::add_instance() {
    int index = 1; std::set<QString> names;
    for (int row = 0; row < table_->rowCount(); ++row) if (table_->item(row, 0)) names.insert(table_->item(row, 0)->text());
    while (names.contains(QStringLiteral("INSTANCE_%1").arg(index))) ++index;
    const int row = table_->rowCount(); table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("INSTANCE_%1").arg(index)));
    for (int column = 1; column < table_->columnCount(); ++column) table_->setItem(row, column, new QTableWidgetItem);
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
    nlohmann::json columns = nlohmann::json::array(); std::set<std::string> unique_columns;
    for (int column = 1; column < table_->columnCount(); ++column) {
        const auto key = table_->horizontalHeaderItem(column)->text().trimmed().toStdString();
        if (key.empty() || !unique_columns.insert(key).second) {
            QMessageBox::warning(this, windowTitle(), tr("Názvy sloupců musí být neprázdné a jedinečné."));
            return false;
        }
        columns.push_back(key);
    }
    nlohmann::json instances = nlohmann::json::array(); std::set<std::string> names;
    for (int row = 1; row < table_->rowCount(); ++row) {
        const auto name = table_->item(row, 0) == nullptr ? std::string{} : table_->item(row, 0)->text().trimmed().toStdString();
        if (name.empty() || name == generic_name_.toStdString() || !names.insert(name).second) {
            QMessageBox::warning(this, windowTitle(), tr("Názvy instancí musí být neprázdné a jedinečné."));
            return false;
        }
        nlohmann::json values = nlohmann::json::object();
        for (int column = 1; column < table_->columnCount(); ++column) values[table_->horizontalHeaderItem(column)->text().toStdString()] = table_->item(row, column) == nullptr ? "" : table_->item(row, column)->text().toStdString();
        instances.push_back({{"name", name}, {"values", std::move(values)}});
    }
    data_.family_table = nlohmann::json{{"columns", columns}, {"instances", instances}}.dump();
    accepted_(std::move(data_)); return true;
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
    connect(load, &QPushButton::clicked, this, &MaterialDialog::load_library); top->addWidget(load); content_layout()->addLayout(top);
    table_ = new QTableWidget(0, 4, this); table_->setObjectName("materialTable");
    table_->setHorizontalHeaderLabels({settings.text("column.parameter", "Parametr"), settings.text("column.value", "Hodnota"), settings.text("column.unit", "Jednotka"), settings.text("column.description", "Popis")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); content_layout()->addWidget(table_);
    for (const auto& [key, value] : data_.physical_parameters) {
        const auto unit = data_.physical_parameter_units.find(key);
        const auto description = data_.descriptions.find(key);
        QString description_text;
        if (description != data_.descriptions.end()) {
            auto localized = description->second.find(settings.language.toStdString());
            if (localized != description->second.end()) description_text = QString::fromStdString(localized->second);
        }
        add_row(QString::fromStdString(key), QString::fromStdString(value), unit == data_.physical_parameter_units.end() ? QString{} : QString::fromStdString(unit->second), description_text);
    }
    add_delete_row_buttons(content_layout(), table_, settings, [this] { add_row("NEW_PROPERTY"); });
}

void MaterialDialog::add_row(const QString& name, const QString& value, const QString& unit, const QString& description) {
    const int row = table_->rowCount(); table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(name)); table_->setItem(row, 1, new QTableWidgetItem(value));
    auto* combo = new NoWheelComboBox(table_); combo->setEditable(false);
    combo->addItems({"", "1", "mm", "cm", "m", "in", "deg", "rad",
        "kg", "g", "t", "lb", "s", "min", "C", "K", "F", "Pa",
        "kPa", "MPa", "GPa", "psi", "kg/mm^3", "g/cm^3", "kg/m^3",
        "lb/in^3", "1/C", "1/K", "1/F", "mm*kg/(s^3*C)", "W/(m*K)",
        "mm^2/(s^2*C)", "J/(kg*K)"});
    const QMap<QString, QStringList> property_units{
        {"YOUNG_MODULUS", {"MPa", "GPa", "kPa", "Pa", "psi"}},
        {"SHEAR_MODULUS", {"MPa", "GPa", "kPa", "Pa", "psi"}},
        {"STRESS_LIMIT_FOR_TENSION", {"MPa", "GPa", "kPa", "Pa", "psi"}},
        {"STRESS_LIMIT_FOR_COMPRESSION", {"MPa", "GPa", "kPa", "Pa", "psi"}},
        {"STRESS_LIMIT_FOR_SHEAR", {"MPa", "GPa", "kPa", "Pa", "psi"}},
        {"MASS_DENSITY", {"kg/mm^3", "kg/m^3", "g/cm^3", "lb/in^3"}},
        {"THERMAL_EXPANSION_COEFFICIENT", {"1/C", "1/K", "1/F"}},
        {"THERM_EXPANSION_REF_TEMPERATURE", {"C", "K", "F"}},
        {"THERMAL_CONDUCTIVITY", {"mm*kg/(s^3*C)", "W/(m*K)"}},
        {"SPECIFIC_HEAT", {"mm^2/(s^2*C)", "J/(kg*K)"}}};
    if (property_units.contains(name)) {
        combo->clear(); combo->addItems(property_units.value(name));
    } else if (QStringList{"POISSON_RATIO", "STRUCTURAL_DAMPING_COEFFICIENT",
                           "EMISSIVITY", "SHEETMETAL_K_FACTOR"}.contains(name)) {
        combo->clear(); combo->addItem("1");
    } else if (QStringList{"MATERIAL_NAME", "HARDNESS", "CONDITION"}.contains(name)) {
        combo->clear(); combo->addItem(""); combo->setEnabled(false);
    }
    combo->setCurrentText(unit); table_->setCellWidget(row, 2, combo);
    auto* description_item = new QTableWidgetItem(description);
    description_item->setFlags(description_item->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, 3, description_item);
}

void MaterialDialog::load_library() {
    const QString file = open_file(this, settings_.text("file.select_material", "Vybrat materiál"), settings_.resolved_paths.value("Materials"), settings_.text("file.filter.material", "Materiál ZIMA-CAD (*.matz)"), settings_.translations);
    if (file.isEmpty()) return;
    QSettings material(file, QSettings::IniFormat); table_->setRowCount(0); data_.descriptions.clear();
    material.beginGroup("ParameterDescriptions");
    for (const auto& raw_key : material.childKeys()) {
        const int separator = raw_key.lastIndexOf('\\');
        const QString key = separator < 0 ? raw_key : raw_key.left(separator);
        const QString language = separator < 0 ? QString{} : raw_key.mid(separator + 1);
        data_.descriptions[key.toStdString()][language.toStdString()] =
            material.value(raw_key).toString().toStdString();
    }
    material.endGroup();
    material.beginGroup("Material");
    const QString material_name = material.value("Name").toString();
    material.endGroup();
    if (!material_name.isEmpty()) {
        const auto descriptions = data_.descriptions.find("MATERIAL_NAME");
        QString description;
        if (descriptions != data_.descriptions.end()) {
            const auto localized = descriptions->second.find(settings_.language.toStdString());
            if (localized != descriptions->second.end()) description = QString::fromStdString(localized->second);
        }
        add_row("MATERIAL_NAME", material_name, {}, description);
    }
    material.beginGroup("Properties"); const auto keys = material.childKeys();
    for (const auto& key : keys) {
        material.endGroup(); material.beginGroup("PropertyUnits"); const QString unit = material.value(key).toString(); material.endGroup();
        const QString description_key = key + "\\" + settings_.language; material.beginGroup("ParameterDescriptions"); const QString description = material.value(description_key).toString(); material.endGroup();
        material.beginGroup("Properties"); add_row(key, material.value(key).toString(), unit, description);
    }
    material.endGroup();
}

bool MaterialDialog::submit() {
    data_.physical_parameters.clear(); data_.physical_parameter_units.clear();
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString key = table_->item(row, 0) == nullptr ? QString{} : table_->item(row, 0)->text().trimmed();
        if (key.isEmpty()) return false;
        data_.physical_parameters[key.toStdString()] = table_->item(row, 1) == nullptr ? "" : table_->item(row, 1)->text().toStdString();
        if (auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, 2)); combo != nullptr && !combo->currentText().isEmpty()) data_.physical_parameter_units[key.toStdString()] = combo->currentText().toStdString();
        if (table_->item(row, 3) != nullptr && !table_->item(row, 3)->text().isEmpty()) data_.descriptions[key.toStdString()][settings_.language.toStdString()] = table_->item(row, 3)->text().toStdString();
    }
    accepted_(std::move(data_)); return true;
}

}  // namespace zima::app
