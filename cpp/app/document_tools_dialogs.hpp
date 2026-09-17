#pragma once

#include "application_settings.hpp"

#include <zima/document/relations.hpp>
#include <zima/document/metadata.hpp>
#include <zima/document/dimension_identifiers.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/workspace/family_operations.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace zima::app {

struct DocumentToolData {
    std::optional<zima::document::SheetMetalDefaults> sheet_metal;
    std::map<std::string, std::string> units;
    std::map<std::string, std::string> precision;
    std::map<std::string, std::string> physical_parameters;
    std::map<std::string, std::string> physical_parameter_units;
    std::map<std::string, std::map<std::string, std::string>> descriptions;
    std::string family_table;
};

using ToolDataAccepted = std::function<void(DocumentToolData)>;

using UserParameterData = zima::document::UserParameterData;

class UserParametersDialog final : public zima::ui::PropertiesSubWindow {
public:
    UserParametersDialog(UserParameterData data, QString language,
                         std::function<void(UserParameterData)> accepted,
                         const ApplicationSettings& settings, QWidget* parent);
protected:
    bool submit() override;
private:
    void populate();
    bool read_table();
    void add_row();
    UserParameterData data_;
    QString language_;
    std::function<void(UserParameterData)> accepted_;
    QComboBox* language_combo_{};
    QTableWidget* table_{};
};

class FileSettingsDialog final : public zima::ui::PropertiesSubWindow {
public:
    FileSettingsDialog(DocumentToolData data, ToolDataAccepted accepted,
                       const ApplicationSettings& settings, QWidget* parent);
    void show_sheet_metal_page();
protected:
    bool submit() override;
private:
    DocumentToolData data_;
    ToolDataAccepted accepted_;
    std::map<std::string, QComboBox*> units_;
    QDoubleSpinBox* linear_{};
    QDoubleSpinBox* angular_{};
    QDoubleSpinBox* mesh_{};
    QSpinBox* decimals_{};
    QTabWidget* pages_{};
    QDoubleSpinBox* thickness_{};
    QDoubleSpinBox* k_factor_{};
    QDoubleSpinBox* sheet_cut_tolerance_{};
};

class RelationsDialog final : public zima::ui::PropertiesSubWindow {
public:
    RelationsDialog(std::map<std::string, std::string> parameters,
                    std::vector<zima::document::ModelRelation> relations,
                    std::function<void(std::vector<zima::document::ModelRelation>)> accepted,
                    const ApplicationSettings& settings, QWidget* parent);
    void set_dimension_catalog(std::vector<zima::document::DimensionParameter> parameters,
        const zima::document::DimensionIdentifiers& identifiers);
protected:
    bool submit() override;
private:
    void add_row(const std::string& target = {}, const std::string& expression = {});
    std::map<std::string, std::string> parameters_;
    std::function<void(std::vector<zima::document::ModelRelation>)> accepted_;
    QTableWidget* table_{};
};

class FamilyTableDialog final : public zima::ui::PropertiesSubWindow {
public:
    FamilyTableDialog(QString generic_name, DocumentToolData data,
                      ToolDataAccepted accepted,
                      const ApplicationSettings& settings, QWidget* parent);
    void set_references(std::vector<zima::workspace::FamilyReference>);
    void choose_reference(const zima::workspace::FamilyReference&);
    void end_entry();
    int active_column() const { return active_column_; }
    std::vector<zima::document::FamilyColumn> inspected_references() const;
    std::function<void()> entry_changed;
    std::function<void(const std::string&)> open_instance;
protected:
    bool submit() override;
private:
    void add_instance();
    void add_column();
    void refresh_references();
    void arm_column(int);
    zima::document::FamilyTable read_table() const;
    QTableWidget* table_{};
    QString generic_name_;
    DocumentToolData data_;
    ToolDataAccepted accepted_;
    ApplicationSettings settings_;
    std::vector<zima::workspace::FamilyReference> references_;
    std::vector<std::optional<zima::workspace::FamilyReference>> columns_;
    std::set<int> inspected_;
    int active_column_{-1};
    std::string requested_instance_;
};

class FamilyInstanceDialog final : public zima::ui::PropertiesSubWindow {
public:
    FamilyInstanceDialog(QString generic_name, const zima::document::FamilyTable&,
        const std::string& selected_row, bool replacing,
        std::function<void(const std::string&)> accepted, QWidget* parent);
protected:
    bool submit() override;
private:
    QTableWidget* table_{};
    std::function<void(const std::string&)> accepted_;
};

class MaterialDialog final : public zima::ui::PropertiesSubWindow {
public:
    MaterialDialog(DocumentToolData data, ToolDataAccepted accepted,
                   const ApplicationSettings& settings, QWidget* parent);
protected:
    bool submit() override;
private:
    void add_row(const QString& name = {}, const QString& value = {},
                 const QString& unit = {}, const QString& description = {});
    void load_library();
    void populate();
    DocumentToolData data_;
    ToolDataAccepted accepted_;
    ApplicationSettings settings_;
    QTableWidget* table_{};
};

}  // namespace zima::app
