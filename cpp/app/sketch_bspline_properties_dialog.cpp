#include "sketch_bspline_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

namespace zima::app {

SketchBSplinePropertiesDialog::SketchBSplinePropertiesDialog(
    unsigned degree, bool closed,
    std::vector<std::array<double, 2>> control_points,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(tr("B-spline"), parent),
      commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(390);
    auto* form = new QFormLayout;
    degree_ = new QSpinBox(this);
    degree_->setObjectName("bsplineDegree");
    degree_->setRange(1, static_cast<int>(control_points.size() - 1));
    degree_->setValue(static_cast<int>(degree));
    form->addRow(tr("Stupeň"), degree_);
    closed_ = new QCheckBox(tr("Uzavřená periodická křivka"), this);
    closed_->setObjectName("bsplineClosed");
    closed_->setChecked(closed);
    form->addRow(tr("Tvar"), closed_);
    content_layout()->addLayout(form);

    auto* points = new QGridLayout;
    points->addWidget(new QLabel(tr("Řídicí bod"), this), 0, 0);
    points->addWidget(new QLabel(tr("X [mm]"), this), 0, 1);
    points->addWidget(new QLabel(tr("Y [mm]"), this), 0, 2);
    for (std::size_t index = 0; index < control_points.size(); ++index) {
        auto* x = new QDoubleSpinBox(this);
        auto* y = new QDoubleSpinBox(this);
        for (auto* value : {x, y}) {
            value->setRange(-1'000'000.0, 1'000'000.0);
            value->setDecimals(3);
            value->setSuffix(" mm");
        }
        x->setValue(control_points[index][0]);
        y->setValue(control_points[index][1]);
        x_.push_back(x);
        y_.push_back(y);
        points->addWidget(new QLabel(tr("P%1").arg(index + 1), this), index + 1, 0);
        points->addWidget(x, index + 1, 1);
        points->addWidget(y, index + 1, 2);
    }
    content_layout()->addLayout(points);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    content_layout()->addWidget(error_);
}

bool SketchBSplinePropertiesDialog::submit() {
    std::vector<std::array<double, 2>> points;
    points.reserve(x_.size());
    for (std::size_t index = 0; index < x_.size(); ++index) {
        points.push_back({x_[index]->value(), y_[index]->value()});
        if (index > 0 && points[index] == points[index - 1]) {
            error_->setText(tr("Sousední řídicí body musí být odlišné."));
            return false;
        }
    }
    try {
        commit_(static_cast<unsigned>(degree_->value()), closed_->isChecked(), points);
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
