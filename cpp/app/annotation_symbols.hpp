#pragma once
#include <QMenu>
#include <QToolButton>
#include <functional>
namespace zima::app {
inline QToolButton* annotation_symbols(QWidget* parent,std::function<void(const QString&)> insert) {
    auto* button=new QToolButton(parent);button->setText(QStringLiteral("⌀"));
    button->setToolTip(QObject::tr("Vložit symbol"));button->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(button);
    for(const auto& symbol:{QStringLiteral("⌀"),QStringLiteral("○"),QStringLiteral("●"),QStringLiteral("R"),
        QStringLiteral("SR"),QStringLiteral("S⌀"),QStringLiteral("□"),QStringLiteral("⌴"),QStringLiteral("⌵"),
        QStringLiteral("↧"),QStringLiteral("⌒"),QStringLiteral("∠"),QStringLiteral("°"),QStringLiteral("±"),QStringLiteral("×"),QStringLiteral("≈")}) {
        auto* action=menu->addAction(symbol);
        QObject::connect(action,&QAction::triggered,button,[insert,symbol]{insert(symbol);});
    }
    button->setMenu(menu);return button;
}
}
