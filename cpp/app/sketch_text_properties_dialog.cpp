#include "sketch_text_properties_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QLabel>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace zima::app {
namespace {

QString iso_font_family() {
    static const QString family = [] {
        const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/zima/fonts/osifont-lgpl3fe.ttf"));
        const auto families = id < 0
            ? QStringList{} : QFontDatabase::applicationFontFamilies(id);
        return families.empty() ? QStringLiteral("osifont") : families.front();
    }();
    return family;
}

}  // namespace

SketchTextPropertiesDialog::SketchTextPropertiesDialog(
    zima::sketcher::SketchText initial,
    std::optional<std::array<double, 2>> anchor,
    PreviewCallback preview, CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(tr("Text skici"), parent),
      initial_(std::move(initial)), anchor_(anchor),
      preview_(std::move(preview)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setProperty("dialogKind", QStringLiteral("sketchText"));
    setMinimumWidth(420);

    auto* form = new QFormLayout;
    value_ = new QPlainTextEdit(this);
    value_->setObjectName("sketchTextValue");
    value_->setPlainText(QString::fromStdString(initial_.value));
    value_->setFixedHeight(value_->fontMetrics().lineSpacing() * 5 + 16);
    form->addRow(new QLabel(tr("Text"), this));
    form->addRow(value_);

    height_ = new QDoubleSpinBox(this);
    height_->setObjectName("sketchTextHeight");
    height_->setRange(0.01, 1'000'000.0);
    height_->setDecimals(3);
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
    color_->setCurrentIndex(color_->findData(static_cast<int>(initial_.color)));
    form->addRow(tr("Barva"), color_);

    angle_ = new QDoubleSpinBox(this);
    angle_->setObjectName("sketchTextAngle");
    angle_->setRange(-360'000.0, 360'000.0);
    angle_->setDecimals(3);
    angle_->setSuffix(tr("°"));
    angle_->setValue(initial_.angle_degrees);
    form->addRow(tr("Natočení"), angle_);
    content_layout()->addLayout(form);

    flipped_ = new QCheckBox(tr("Převrátit vodorovně"), this);
    flipped_->setObjectName("sketchTextFlipped");
    flipped_->setChecked(initial_.flipped);
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

zima::sketcher::SketchText SketchTextPropertiesDialog::build_text() const {
    if (!anchor_) throw std::runtime_error("Nejprve určete polohu textu ve skice.");
    const QString value = value_->toPlainText();
    if (value.isEmpty()) throw std::runtime_error("Text nesmí být prázdný.");

    QFont font(iso_font_family());
    font.setPixelSize(1000);
    const QFontMetricsF metrics(font);
    QPainterPath path;
    const auto lines = value.split('\n');
    for (qsizetype index = 0; index < lines.size(); ++index) {
        path.addText(QPointF(0.0, static_cast<double>(index) * metrics.lineSpacing()),
                     font, lines[index]);
    }
    const QRectF bounds = path.boundingRect();
    if (bounds.height() <= 1.0e-9) {
        throw std::runtime_error("Text nevytváří žádný platný obrys.");
    }
    const double scale = height_->value() / std::max(metrics.capHeight(), 1.0);
    const double scaled_left = bounds.left() * scale;
    const double scaled_bottom = bounds.bottom() * scale;
    std::vector<std::vector<std::array<double, 2>>> local_contours;
    for (const auto& polygon : path.toSubpathPolygons(
             QTransform::fromScale(scale, scale))) {
        std::vector<std::array<double, 2>> contour;
        contour.reserve(static_cast<std::size_t>(polygon.size()));
        for (const auto& point : polygon) {
            const std::array candidate{
                point.x() - scaled_left, scaled_bottom - point.y()};
            if (contour.empty() || std::hypot(
                    candidate[0] - contour.back()[0],
                    candidate[1] - contour.back()[1]) > 1.0e-9) {
                contour.push_back(candidate);
            }
        }
        if (contour.size() >= 2 && std::hypot(
                contour.front()[0] - contour.back()[0],
                contour.front()[1] - contour.back()[1]) <= 1.0e-9) {
            contour.pop_back();
        }
        if (contour.size() >= 3) local_contours.push_back(std::move(contour));
    }
    if (local_contours.empty()) {
        throw std::runtime_error("Text nevytváří žádný platný obrys.");
    }

    double width{};
    for (const auto& contour : local_contours) {
        for (const auto& point : contour) width = std::max(width, point[0]);
    }
    const auto horizontal = static_cast<zima::sketcher::TextHorizontalAlignment>(
        horizontal_->currentData().toInt());
    const auto vertical = static_cast<zima::sketcher::TextVerticalAlignment>(
        vertical_->currentData().toInt());
    const double horizontal_offset = horizontal ==
            zima::sketcher::TextHorizontalAlignment::Center ? -0.5 * width
        : horizontal == zima::sketcher::TextHorizontalAlignment::Right ? -width
        : 0.0;
    const double vertical_offset = vertical ==
            zima::sketcher::TextVerticalAlignment::Middle
            ? -0.5 * height_->value()
        : vertical == zima::sketcher::TextVerticalAlignment::Top
            ? -height_->value() : 0.0;
    constexpr double pi = 3.14159265358979323846;
    const double angle = angle_->value() * pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);

    auto text = initial_;
    text.value = value.toStdString();
    text.anchor_x = (*anchor_)[0];
    text.anchor_y = (*anchor_)[1];
    text.height = height_->value();
    text.horizontal = horizontal;
    text.vertical = vertical;
    text.angle_degrees = angle_->value();
    text.flipped = flipped_->isChecked();
    text.color = static_cast<zima::sketcher::SketchTextColor>(
        color_->currentData().toInt());
    text.font = font_->currentData().toString().toStdString();
    text.contours.clear();
    text.contours.reserve(local_contours.size());
    for (auto contour : local_contours) {
        for (auto& point : contour) {
            double x = point[0] + horizontal_offset;
            const double y = point[1] + vertical_offset;
            if (text.flipped) x = -x;
            point = {text.anchor_x + x * cosine - y * sine,
                     text.anchor_y + x * sine + y * cosine};
        }
        text.contours.push_back(std::move(contour));
    }
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
