#pragma once

#include <zima/ui/properties_subwindow.hpp>

class QCheckBox;

namespace zima::app {

class ExportOptionsDialog final : public zima::ui::PropertiesSubWindow {
public:
    ExportOptionsDialog(const QString& path, QWidget* parent);
    [[nodiscard]] bool include_skeletons() const;

protected:
    bool submit() override;

private:
    QCheckBox* skeletons_{};
};

}
