#pragma once
#include <zima/ui/properties_subwindow.hpp>
class QDoubleSpinBox;
namespace zima::app {
class ImportOptionsDialog final : public zima::ui::PropertiesSubWindow {
public:
    ImportOptionsDialog(const QString& path, double default_deflection, QWidget* parent);
    [[nodiscard]] double mesh_deflection() const;
protected:
    bool submit() override;
private:
    QDoubleSpinBox* deflection_{};
};
}
