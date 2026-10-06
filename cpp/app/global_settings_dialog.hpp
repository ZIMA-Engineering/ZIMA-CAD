#pragma once

#include "application_settings.hpp"
#include "desktop_integration.hpp"

#include <zima/ui/properties_subwindow.hpp>

#include <QMap>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QTabWidget;
class UpdatesPage;

namespace zima::app {
class AiSettingsPage;

class GlobalSettingsDialog final : public zima::ui::PropertiesSubWindow {
public:
    explicit GlobalSettingsDialog(ApplicationSettings settings, QWidget* parent,
        bool system_setup=false,bool first_launch=false,const desktop::Context* desktop_context=nullptr);
    [[nodiscard]] static bool needs_initial_setup(const ApplicationSettings& settings);
    [[nodiscard]] const ApplicationSettings& settings() const;
    void show_updates();
    void show_ai();

protected:
    bool submit() override;

private:
    void browse_path(const QString& key);

    ApplicationSettings settings_;
    bool system_setup_{};
    bool first_launch_{};
    QComboBox* unit_preset_{};
    QComboBox* part_template_{};
    QComboBox* assembly_template_{};
    QWidget* template_page_{};
    QTabWidget* sections_{};
    UpdatesPage* updates_{};
    AiSettingsPage* ai_{};
    QWidget* desktop_{};
    QString ai_preferences_path_;
    QComboBox* language_{};
    QComboBox* theme_{};
    QCheckBox* application_font_{};
    QComboBox* tolerance_layout_{};
    QCheckBox* names_uppercase_{};
    QCheckBox* names_diacritics_{};
    QCheckBox* names_spaces_{};
    QDoubleSpinBox* sheet_cut_tolerance_{};
    QComboBox* drawing_view_style_{};
    QLineEdit* drawing_pdf_directory_{};
    QLineEdit* drawing_dxf_directory_{};
    QMap<QString, QComboBox*> unit_fields_;
    QMap<QString, QLineEdit*> path_fields_;
};

}  // namespace zima::app
