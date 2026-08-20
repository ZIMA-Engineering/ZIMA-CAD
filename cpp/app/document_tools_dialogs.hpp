#pragma once

#include "application_settings.hpp"

#include <zima/document/relations.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;

namespace zima::app {

struct DocumentToolData {
    std::map<std::string, std::string> units;
    std::map<std::string, std::string> precision;
    std::map<std::string, std::string> physical_parameters;
    std::map<std::string, std::string> physical_parameter_units;
    std::map<std::string, std::map<std::string, std::string>> descriptions;
    std::string family_table;
};

using ToolDataAccepted = std::function<void(DocumentToolData)>;

struct UserParameterData {
    std::map<std::string, std::string> flat;
    std::vector<std::string> order;
    std::map<std::string, std::map<std::string, std::string>> labels;
    std::map<std::string, std::map<std::string, std::string>> values;
};

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
    void delete_rows();
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
};

class RelationsDialog final : public zima::ui::PropertiesSubWindow {
public:
    RelationsDialog(std::map<std::string, std::string> parameters,
                    std::vector<zima::document::ModelRelation> relations,
                    std::function<void(std::map<std::string, std::string>,
                        std::vector<zima::document::ModelRelation>)> accepted,
                    const ApplicationSettings& settings, QWidget* parent,
                    std::map<std::string, double> model_values = {},
                    int decimal_places = 3);
protected:
    bool submit() override;
private:
    void add_row(const std::string& target = {}, const std::string& expression = {});
    std::map<std::string, std::string> parameters_;
    std::function<void(std::map<std::string, std::string>,
        std::vector<zima::document::ModelRelation>)> accepted_;
    QTableWidget* table_{};
    std::map<std::string, double> model_values_;
    int decimal_places_{3};
};

class FamilyTableDialog final : public zima::ui::PropertiesSubWindow {
public:
    FamilyTableDialog(QString generic_name, DocumentToolData data,
                      ToolDataAccepted accepted,
                      const ApplicationSettings& settings, QWidget* parent);
protected:
    bool submit() override;
private:
    void add_instance();
    void add_column();
    QTableWidget* table_{};
    QString generic_name_;
    DocumentToolData data_;
    ToolDataAccepted accepted_;
    ApplicationSettings settings_;
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
    DocumentToolData data_;
    ToolDataAccepted accepted_;
    ApplicationSettings settings_;
    QTableWidget* table_{};
};

}  // namespace zima::app
