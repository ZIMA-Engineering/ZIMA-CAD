#pragma once
#include "controller_input.hpp"
#include "application_settings.hpp"
#include <zima/ui/properties_subwindow.hpp>
#include <QIcon>
class QCheckBox;class QComboBox;class QDoubleSpinBox;class QLabel;
class QProgressBar;class QTableWidget;class QTabWidget;
namespace zima::app::controller {
struct Command {QString id,label;QIcon icon;};
class SettingsDialog final:public ui::PropertiesSubWindow {
public:
    SettingsDialog(const ApplicationSettings& settings,InputService& service,
        QVector<Command> commands,std::function<bool(const Preferences&,QString*)> save,QWidget* parent);
    [[nodiscard]] const Preferences& pending()const{return pending_;}
protected:
    bool submit()override;
private:
    const ApplicationSettings settings_;
    InputService& service_;Preferences pending_;
    QVector<Command> commands_;
    std::function<bool(const Preferences&,QString*)> save_;
    QComboBox *device_{},*context_{},*port_{};
    QCheckBox *enabled_{},*rotation_{};
    QDoubleSpinBox *pan_{},*rotate_{},*zoom_{},*dead_zone_{},*range_{};
    std::array<QCheckBox*,6> invert_{};
    std::array<QProgressBar*,6> levels_{};
    QTableWidget* buttons_{};QLabel *status_{},*button_hint_{},*navigo_hint_{};
    QTabWidget* tabs_{};QString editing_device_;Context editing_context_{Context::Model};
    bool loading_{};QVector<Device> shown_devices_;
    QString text(const char* key,const char* fallback)const;
    QString edit_device()const;
    void save_editor();void load_editor();void refresh_devices();void refresh_input();
};
}
