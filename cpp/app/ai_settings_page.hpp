#pragma once
#include "aipreferences.h"
#include <QWidget>
class AiProvider;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
namespace zima::app {
class AiSettingsPage final : public QWidget {
public:
    AiSettingsPage(const QString& preferencesPath, QWidget* parent, AiProvider* provider = nullptr);
    CadAi::Preferences values() const;
private:
    void refresh();
    AiProvider* provider_;
    QLineEdit* executable_;
    QComboBox* model_;
    QLabel* status_;
    QPushButton *connect_, *login_, *logout_, *stop_, *browse_;
};
}
