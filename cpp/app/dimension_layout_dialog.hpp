#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <functional>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/ui/properties_subwindow.hpp>
namespace zima::app {
class DimensionLayoutDialog final : public ui::PropertiesSubWindow {
  public:
    DimensionLayoutDialog(kernel::ViewerDimension dimension, kernel::DimensionLayout initial,
                          std::function<void(kernel::DimensionLayout)> commit, QWidget *parent)
        : PropertiesSubWindow(tr("Zobrazení kóty"), parent), commit_(std::move(commit)) {
        preserved_ = initial;
        setObjectName("dimensionLayoutDialog");
        set_initial_size({350, 310});
        auto *form = new QFormLayout;
        content_layout()->addLayout(form);
        plane_ = new QComboBox(this);
        plane_->setObjectName("dimensionProjectionPlane");
        plane_->addItems({tr("Původní rovina modelu"), tr("Kolmá rovina (90°)"),
                          tr("Opačná strana (180°)"), tr("Kolmá rovina (270°)")});
        plane_->setCurrentIndex(initial.plane_quarter_turns);
        plane_->setEnabled(dimension.kind != kernel::ViewerDimensionKind::Angular);
        form->addRow(tr("Rovina zobrazení"), plane_);
        attach_ = new QCheckBox(tr("Uchytit k obálce modelu"), this);
        attach_->setObjectName("dimensionEnvelopeAttachment");
        attach_->setChecked(initial.envelope_offset.has_value());
        form->addRow(attach_);
        const auto field = [&](const char *name, double value) {
            auto *f = new QDoubleSpinBox(this);
            f->setObjectName(name);
            f->setRange(-1000000, 1000000);
            f->setDecimals(ui::numeric_decimal_places(parent));
            f->setSuffix(" mm");
            f->setValue(value);
            return f;
        };
        offset_ = field("dimensionEnvelopeOffset", initial.envelope_offset.value_or(8));
        offset_->setMinimum(0);
        form->addRow(tr("Odsazení od obálky"), offset_);
        along_ = field("dimensionTextAlong", initial.text_along);
        outward_ = field("dimensionTextOutward", initial.line_offset);
        form->addRow(tr("Posunutí textu podél kóty"), along_);
        form->addRow(tr("Posunutí kótovací čáry"), outward_);
        offset_->setEnabled(attach_->isChecked());
        connect(attach_, &QCheckBox::toggled, offset_, &QDoubleSpinBox::setEnabled);
        auto *hint = new QLabel(
            dimension.kind == kernel::ViewerDimensionKind::Angular
                ? tr("Rovinu úhlové kóty určují měřená ramena. Měnit lze odsazení a polohu textu.")
                : tr("Rovina se otáčí kolem směru měření. Reference a hodnota zůstávají stejné."),
            this);
        hint->setWordWrap(true);
        content_layout()->addWidget(hint);
    }

  protected:
    bool submit() override {
        kernel::DimensionLayout value = preserved_;
        value.envelope_offset.reset();
        value.plane_quarter_turns = plane_->currentIndex();
        if (attach_->isChecked())
            value.envelope_offset = offset_->value();
        value.text_along = along_->value();
        value.line_offset = outward_->value();
        kernel::validate_dimension_layout(value);
        commit_(value);
        return true;
    }

  private:
    kernel::DimensionLayout preserved_;
    std::function<void(kernel::DimensionLayout)> commit_;
    QComboBox *plane_{};
    QCheckBox *attach_{};
    QDoubleSpinBox *offset_{}, *along_{}, *outward_{};
};
} // namespace zima::app
