#pragma once

#include <QDir>
#include <QFormLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <functional>
#include <stdexcept>
#include <zima/ui/properties_subwindow.hpp>

namespace zima::app {
class PathNameDialog final : public zima::ui::PropertiesSubWindow {
public:
  PathNameDialog(const QString &title, const QString &initial,
                 std::function<void(const QString &)> create, QWidget *parent)
      : PropertiesSubWindow(title, parent), create_(std::move(create)) {
    setObjectName("pathNameDialog");
    auto *form = new QFormLayout;
    name_ = new QLineEdit(initial, this);
    name_->setObjectName("pathName");
    form->addRow(QObject::tr("Name"), name_);
    content_layout()->addLayout(form);
    set_initial_size({440, 180});
    name_->selectAll();
    name_->setFocus();
  }

protected:
  bool submit() override {
    const QString name = name_->text().trimmed();
    static const QRegularExpression invalid(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name == "." || name == ".." || name.endsWith('.') ||
        invalid.match(name).hasMatch() || reserved.match(name).hasMatch())
      throw std::runtime_error(
          QObject::tr("Enter a valid file or folder name.").toStdString());
    create_(name);
    return true;
  }

private:
  QLineEdit *name_{};
  std::function<void(const QString &)> create_;
};
} // namespace zima::app
