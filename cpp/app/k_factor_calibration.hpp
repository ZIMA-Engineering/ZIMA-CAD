#pragma once
#include <zima/document/sheet_calibration.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <functional>
namespace zima::app {
class KFactorCalibration final:public QWidget {
public:
    KFactorCalibration(QDoubleSpinBox* factor,std::function<double()> thickness,
        std::function<double()> angle,QWidget* parent):QWidget(parent) {
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* row=new QHBoxLayout;layout->addLayout(row);
        correction_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
        correction_->setObjectName("kFactorCorrection");correction_->setRange(-1,1);correction_->setSingleStep(.01);correction_->set_display_decimals(4);
        correction_->setToolTip(tr("Change the developed length of one test bend. Positive extends it; negative shortens it."));
        row->addWidget(correction_,1);auto* calculate=new QPushButton(tr("OK"),this);
        calculate->setObjectName("kFactorCorrectionConfirm");calculate->setAutoDefault(false);row->addWidget(calculate);
        error_=new QLabel(this);error_->setObjectName("kFactorCorrectionError");error_->setWordWrap(true);error_->hide();layout->addWidget(error_);
        connect(calculate,&QPushButton::clicked,this,[this,factor,thickness=std::move(thickness),angle=std::move(angle)] {
            try {
                if(!factor->isEnabled()||factor->isReadOnly())return;
                const double next=document::calibrated_k_factor(factor->value(),thickness(),angle(),correction_->value());
                factor->setValue(next);correction_->setValue(0);error_->clear();error_->hide();
            }catch(const std::exception& failure){error_->setText(tr(failure.what()));error_->show();}
        });
    }
    void refresh_units(){correction_->set_quantity(ui::InputQuantity::Length);}
private:
    ui::UnitDoubleSpinBox* correction_{};
    QLabel* error_{};
};
}
