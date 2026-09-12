#include "sketch_text_properties_dialog.hpp"
#include <zima/sketcher/text_geometry.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace zima::app {
SketchTextPropertiesDialog::SketchTextPropertiesDialog(
    zima::sketcher::SketchText initial,
    std::optional<std::array<double, 2>> anchor,
    PreviewCallback preview, CommitCallback commit, QWidget* parent, bool y_up)
    : PropertiesSubWindow(tr("Text skici"), parent),
      initial_(std::move(initial)), anchor_(anchor),
      preview_(std::move(preview)), commit_(std::move(commit)), y_up_(y_up) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setProperty("dialogKind", QStringLiteral("sketchText"));
    setObjectName("sketchTextProperties");
    setMinimumWidth(420);

    auto* form = new QFormLayout;
    value_ = new QPlainTextEdit(this);
    value_->setObjectName("sketchTextValue");
    value_->setPlainText(QString::fromStdString(initial_.value));
    value_->setFixedHeight(value_->fontMetrics().lineSpacing() * 5 + 16);
    form->addRow(new QLabel(tr("Text"), this));
    form->addRow(value_);
    mode_ = new QComboBox(this);
    mode_->setObjectName("sketchTextMode");
    mode_->addItem(tr("Běžný text"), false);
    mode_->addItem(tr("Geometrie pro modelování"), true);
    mode_->setCurrentIndex(initial_.modeling_geometry ? 1 : 0);
    form->addRow(tr("Režim textu"), mode_);
    connect(mode_, &QComboBox::currentIndexChanged,
            this, &SketchTextPropertiesDialog::update_preview);

    height_ = new QDoubleSpinBox(this);
    height_->setObjectName("sketchTextHeight");
    height_->setRange(0.01, 1'000'000.0);
    height_->setDecimals(zima::ui::numeric_decimal_places(this,3));
    height_->setSuffix(tr(" mm"));
    height_->setValue(initial_.height);
    form->addRow(tr("Výška"), height_);

    horizontal_ = new QComboBox(this);
    horizontal_->setObjectName("sketchTextHorizontalAlignment");
    horizontal_->addItem(tr("Vlevo"),
        static_cast<int>(zima::sketcher::TextHorizontalAlignment::Left));
    horizontal_->addItem(tr("Na střed"),
        static_cast<int>(zima::sketcher::TextHorizontalAlignment::Center));
    horizontal_->addItem(tr("Vpravo"),
        static_cast<int>(zima::sketcher::TextHorizontalAlignment::Right));
    horizontal_->setCurrentIndex(horizontal_->findData(
        static_cast<int>(initial_.horizontal)));
    form->addRow(tr("Vodorovné zarovnání"), horizontal_);

    vertical_ = new QComboBox(this);
    vertical_->setObjectName("sketchTextVerticalAlignment");
    vertical_->addItem(tr("Dole"),
        static_cast<int>(zima::sketcher::TextVerticalAlignment::Bottom));
    vertical_->addItem(tr("Uprostřed"),
        static_cast<int>(zima::sketcher::TextVerticalAlignment::Middle));
    vertical_->addItem(tr("Nahoře"),
        static_cast<int>(zima::sketcher::TextVerticalAlignment::Top));
    vertical_->setCurrentIndex(vertical_->findData(
        static_cast<int>(initial_.vertical)));
    form->addRow(tr("Svislé zarovnání"), vertical_);

    font_ = new QComboBox(this);
    font_->setObjectName("sketchTextFont");
    font_->addItem(tr("ISO (osifont)"), QStringLiteral("osifont"));
    form->addRow(tr("Písmo"), font_);

    color_ = new QComboBox(this);
    color_->setObjectName("sketchTextColor");
    color_->addItem(tr("Zelená"),
        static_cast<int>(zima::sketcher::SketchTextColor::Green));
    color_->addItem(tr("Bílá"),
        static_cast<int>(zima::sketcher::SketchTextColor::White));
    color_->addItem(tr("Žlutá"),
        static_cast<int>(zima::sketcher::SketchTextColor::Yellow));
    color_->addItem(tr("Červená"), static_cast<int>(zima::sketcher::SketchTextColor::Red));
    color_->setCurrentIndex(color_->findData(static_cast<int>(initial_.color)));
    form->addRow(tr("Barva"), color_);

    angle_ = new QDoubleSpinBox(this);
    angle_->setObjectName("sketchTextAngle");
    angle_->setRange(-360'000.0, 360'000.0);
    angle_->setDecimals(zima::ui::numeric_decimal_places(this,3));
    angle_->setSuffix(tr("°"));
    angle_->setValue(initial_.angle_degrees);
    form->addRow(tr("Natočení"), angle_);
    content_layout()->addLayout(form);

    flipped_ = new QCheckBox(tr("Převrátit vodorovně"), this);
    flipped_->setObjectName("sketchTextFlipped");
    // Template X grows to the left. Its stored flip is the upright baseline,
    // not a user-requested mirror; keep storage/contours exactly as before.
    flipped_->setChecked(initial_.flipped != y_up_);
    content_layout()->addWidget(flipped_);

    error_ = new QLabel(this);
    error_->setObjectName("sketchTextError");
    error_->setStyleSheet(QStringLiteral("color:#c64b4b;"));
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);

    connect(value_, &QPlainTextEdit::textChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(height_, &QDoubleSpinBox::valueChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(horizontal_, &QComboBox::currentIndexChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(vertical_, &QComboBox::currentIndexChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(color_, &QComboBox::currentIndexChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(angle_, &QDoubleSpinBox::valueChanged,
            this, &SketchTextPropertiesDialog::update_preview);
    connect(flipped_, &QCheckBox::toggled,
            this, &SketchTextPropertiesDialog::update_preview);
    update_preview();
}

void SketchTextPropertiesDialog::set_anchor(double x, double y) {
    anchor_ = std::array{x, y};
    error_->clear();
    update_preview();
}

void rebuild_sketch_text_contours(zima::sketcher::SketchText& text, bool y_up) {
    zima::sketcher::rebuild_text_contours(text,y_up);
}

zima::sketcher::SketchText SketchTextPropertiesDialog::build_text() const {
    if (!anchor_) throw std::runtime_error("Nejprve určete polohu textu ve skice.");
    const QString value = value_->toPlainText();
    if (value.isEmpty()) throw std::runtime_error("Text nesmí být prázdný.");

    auto text = initial_;
    text.value = value.toStdString();
    text.modeling_geometry = mode_->currentData().toBool();
    text.anchor_x = (*anchor_)[0]; text.anchor_y = (*anchor_)[1];
    text.height = height_->value();
    text.horizontal = static_cast<zima::sketcher::TextHorizontalAlignment>(horizontal_->currentData().toInt());
    text.vertical = static_cast<zima::sketcher::TextVerticalAlignment>(vertical_->currentData().toInt());
    text.angle_degrees = angle_->value(); text.flipped = flipped_->isChecked() != y_up_;
    text.color = static_cast<zima::sketcher::SketchTextColor>(color_->currentData().toInt());
    text.font = font_->currentData().toString().toStdString();
    rebuild_sketch_text_contours(text, y_up_);
    return text;
}

void SketchTextPropertiesDialog::update_preview() {
    error_->clear();
    if (!preview_) return;
    try {
        preview_(anchor_ ? std::optional{build_text()} : std::nullopt);
    } catch (const std::exception&) {
        preview_(std::nullopt);
    }
}

bool SketchTextPropertiesDialog::submit() {
    try {
        auto text = build_text();
        commit_(std::move(text));
        return true;
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
}

}  // namespace zima::app
