#pragma once
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
struct FeatureOperationButtons { QPushButton* add; QPushButton* subtract; };
inline FeatureOperationButtons add_feature_operation_buttons(QWidget* parent,QVBoxLayout* layout,
        bool subtract,std::function<void(bool)> changed) {
    auto* row=new QWidget(parent);auto* buttons=new QHBoxLayout(row);
    buttons->setContentsMargins(0,0,0,0);buttons->setSpacing(8);
    auto* add=new QPushButton(QObject::tr("Přičíst"),parent);
    auto* cut=new QPushButton(QObject::tr("Odečíst"),parent);
    for(auto* button:{add,cut}){button->setCheckable(true);button->setMinimumHeight(40);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);buttons->addWidget(button);}
    add->setObjectName("primitiveAddOperation");cut->setObjectName("primitiveSubtractOperation");
    add->setStyleSheet("QPushButton{border:2px solid #2d5670;border-radius:6px;font-weight:700;padding:7px 14px} QPushButton:checked{background:#00d1ff;color:#101510;border-color:#6fe3ff}");
    cut->setStyleSheet("QPushButton{border:2px solid #713d3d;border-radius:6px;font-weight:700;padding:7px 14px} QPushButton:checked{background:#c64b4b;color:#ffffff;border-color:#ed7777}");
    add->setChecked(!subtract);cut->setChecked(subtract);
    auto* form=new QFormLayout;form->addRow(QObject::tr("Operace"),row);layout->addLayout(form);
    const auto select=[add,cut,changed=std::move(changed)](bool subtract){add->setChecked(!subtract);cut->setChecked(subtract);changed(subtract);};
    QObject::connect(add,&QPushButton::clicked,parent,[select]{select(false);});
    QObject::connect(cut,&QPushButton::clicked,parent,[select]{select(true);});
    return {add,cut};
}
}
