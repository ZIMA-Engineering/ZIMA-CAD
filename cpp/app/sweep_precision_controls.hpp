#pragma once
#include <zima/document/sweep_precision.hpp>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <functional>

namespace zima::app {
class SweepPrecisionControls final : public QWidget {
public:
    SweepPrecisionControls(document::SweepPrecision initial,QWidget* parent,std::function<void()> changed)
        : QWidget(parent), initial_(initial) {
        auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);
        custom_=new QCheckBox(tr("Vlastní přesnost"),this);
        custom_->setObjectName("sweepCustomPrecision");
        tolerance_=new QDoubleSpinBox(this);tolerance_->setObjectName("sweepPrecisionTolerance");
        tolerance_->setDecimals(9);tolerance_->setRange(1e-9,1e6);tolerance_->setSingleStep(.001);
        tolerance_->setSuffix(" mm");tolerance_->setValue(initial.effective());
        displayed_initial_=tolerance_->value();
        custom_->setChecked(initial.custom_tolerance.has_value());
        tolerance_->setEnabled(custom_->isChecked());
        setToolTip(tr("Tolerance aproximace tažení. Menší hodnota znamená přesnější a obvykle pomalejší výpočet."));
        row->addWidget(custom_);row->addWidget(tolerance_);row->addStretch();
        connect(custom_,&QCheckBox::toggled,this,[this,changed](bool enabled){
            tolerance_->setEnabled(enabled);
            if(changed)changed();
        });
        connect(tolerance_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[changed]{if(changed)changed();});
    }
    [[nodiscard]] document::SweepPrecision value() const {
        auto result=initial_;
        result.custom_tolerance=custom_->isChecked()?std::optional<double>(
            tolerance_->value()==displayed_initial_?initial_.effective():tolerance_->value()):std::nullopt;
        return result;
    }
private:
    document::SweepPrecision initial_;
    double displayed_initial_{};
    QCheckBox* custom_{};
    QDoubleSpinBox* tolerance_{};
};
}
