#pragma once

#include "application_settings.hpp"

#include <zima/ui/properties_subwindow.hpp>

#include <QMap>

class QComboBox;
class QLineEdit;
class QTabWidget;
class UpdatesPage;

namespace zima::app {

class GlobalSettingsDialog final : public zima::ui::PropertiesSubWindow {
public:
    explicit GlobalSettingsDialog(ApplicationSettings settings, QWidget* parent);
    [[nodiscard]] const ApplicationSettings& settings() const;
    void show_updates();

protected:
    bool submit() override;

private:
    void browse_path(const QString& key);

    ApplicationSettings settings_;
    QTabWidget* sections_{};
    UpdatesPage* updates_{};
    QComboBox* language_{};
    QComboBox* application_font_{};
    QMap<QString, QComboBox*> unit_fields_;
    QMap<QString, QLineEdit*> path_fields_;
};

}  // namespace zima::app
