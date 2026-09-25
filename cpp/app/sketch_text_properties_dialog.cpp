#include <QJsonDocument>
#include <QJsonArray>
#include "sketch_text_properties_dialog.hpp"
#include "annotation_symbols.hpp"
#include <zima/sketcher/text_geometry.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPlainTextEdit>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace zima::app {
SketchTextPropertiesDialog::SketchTextPropertiesDialog(
    zima::sketcher::SketchText initial,
    std::optional<std::array<double, 2>> anchor,
    PreviewCallback preview, CommitCallback commit, QWidget* parent, bool y_up, bool drawing_text, std::optional<std::map<std::string,std::string>> action_settings, bool symbol_definition)
    : PropertiesSubWindow(tr(drawing_text ? "Text výkresu" : "Text skici"), parent),
      initial_(std::move(initial)), anchor_(anchor),
      preview_(std::move(preview)), commit_(std::move(commit)), y_up_(y_up), drawing_text_(drawing_text) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    inverted_flip_ui_=y_up_&&!symbol_definition;
    setProperty("dialogKind", QStringLiteral("sketchText"));
    setObjectName(drawing_text ? "drawingTextProperties" : "sketchTextProperties");
    setMinimumWidth(420);

    auto* form = new QFormLayout;
    value_ = new QPlainTextEdit(this);
    value_->setObjectName("sketchTextValue");
    value_->setPlainText(QString::fromStdString(initial_.value));
    value_->setFixedHeight(value_->fontMetrics().lineSpacing() * 5 + 16);
    if(drawing_text_) {
        auto* heading=new QHBoxLayout;heading->setContentsMargins(0,0,0,0);
        heading->addWidget(new QLabel(tr("Text"),this));heading->addStretch();
        auto* symbols=annotation_symbols(this,[this](const QString& value){value_->insertPlainText(value);value_->setFocus();});
        symbols->setObjectName("drawingTextSymbols");heading->addWidget(symbols);form->addRow(heading);
        form->setFormAlignment(Qt::AlignTop);form->setVerticalSpacing(6);
    } else form->addRow(new QLabel(tr("Text"), this));
    form->addRow(value_);
    mode_ = new QComboBox(this);
    mode_->setObjectName("sketchTextMode");
    mode_->addItem(tr("Běžný text"), false);
    mode_->addItem(tr("Geometrie pro modelování"), true);
    mode_->setCurrentIndex(initial_.modeling_geometry ? 1 : 0);
    if(drawing_text_) {
        mode_->setCurrentIndex(0);mode_->hide();
        value_->setFixedHeight(220);set_initial_size(QSize(650,590));setSizeGripEnabled(true);
    } else form->addRow(tr("Režim textu"), mode_);
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
    if(symbol_definition) {
        drawing_orientation_=new QComboBox(this);drawing_orientation_->setObjectName("symbolTextDrawingOrientation");
        drawing_orientation_->addItem(tr("Se značkou"),false);
        drawing_orientation_->addItem(tr("Zachovat čitelnost"),true);
        drawing_orientation_->setCurrentIndex(initial_.drawing_keep_readable?1:0);
        form->addRow(tr("Orientace ve výkresu"),drawing_orientation_);
        connect(drawing_orientation_,&QComboBox::currentIndexChanged,this,&SketchTextPropertiesDialog::update_preview);
    }


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
    if(action_settings) {
        const auto get=[&](const std::string& key,const std::string& fallback){const auto it=action_settings->find(key);return QString::fromStdString(it==action_settings->end()?fallback:it->second);};
        field_action_=new QComboBox(this);field_action_->setObjectName("textFieldAction");
        field_action_->addItem(tr("Žádná"),"none");field_action_->addItem(tr("Dnešní datum"),"today");field_action_->addItem(tr("Výběr ze seznamu"),"list");
        field_action_->setCurrentIndex(std::max(0,field_action_->findData(get("kind","none"))));form->addRow(tr("Akce u hodnoty"),field_action_);
        date_format_=new QComboBox(this);date_format_->setObjectName("textFieldDateFormat");date_format_->addItems({"dd.MM.yyyy","yyyy-MM-dd","d. M. yyyy","MM/dd/yyyy"});date_format_->setCurrentText(get("date_format","dd.MM.yyyy"));form->addRow(tr("Formát data"),date_format_);
        choices_=new QPlainTextEdit(this);choices_->setObjectName("textFieldChoices");{QStringList choices;for(const auto& item:QJsonDocument::fromJson(get("choices","[]").toUtf8()).array())choices.push_back(item.toString());choices_->setPlainText(choices.join('\n'));}choices_->setFixedHeight(90);form->addRow(tr("Možnosti (každá na novém řádku)"),choices_);
        allow_custom_=new QCheckBox(tr("Povolit vlastní hodnotu"),this);allow_custom_->setObjectName("textFieldAllowCustom");allow_custom_->setChecked(get("allow_custom","yes")=="yes");form->addRow(allow_custom_);
        const auto update=[this,form]{const auto kind=field_action_->currentData().toString();form->setRowVisible(date_format_,kind=="today");form->setRowVisible(choices_,kind=="list");form->setRowVisible(allow_custom_,kind=="list");if(isVisible()){layout()->activate();adjustSize();}};
        connect(field_action_,&QComboBox::currentIndexChanged,this,update);update();
    }
    content_layout()->addLayout(form);

    flipped_ = new QCheckBox(tr("Převrátit vodorovně"), this);
    flipped_->setObjectName("sketchTextFlipped");
    // Template X grows to the left. Its stored flip is the upright baseline,
    // not a user-requested mirror; keep storage/contours exactly as before.
    flipped_->setChecked(initial_.flipped != inverted_flip_ui_);
    content_layout()->addWidget(flipped_);

    error_ = new QLabel(this);
    error_->setObjectName("sketchTextError");
    error_->setStyleSheet(QStringLiteral("color:#c64b4b;"));
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    if(drawing_text_)content_layout()->addStretch();

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

std::map<std::string,std::string> SketchTextPropertiesDialog::field_action() const {
    if(!field_action_||field_action_->currentData().toString()=="none")return {};
    return {{"kind",field_action_->currentData().toString().toStdString()},{"date_format",date_format_->currentText().toStdString()},{"choices",QJsonDocument(QJsonArray::fromStringList(choices_->toPlainText().split('\n',Qt::SkipEmptyParts))).toJson(QJsonDocument::Compact).toStdString()},{"allow_custom",allow_custom_->isChecked()?"yes":"no"}};
}

void SketchTextPropertiesDialog::set_anchor(double x, double y) {
    anchor_ = std::array{x, y};
    error_->clear();
    update_preview();
}

void SketchTextPropertiesDialog::set_preview_anchor(double x,double y) {
    if(anchor_||!drawing_text_)return;
    preview_anchor_=std::array{x,y};update_preview();
}

void rebuild_sketch_text_contours(zima::sketcher::SketchText& text, bool y_up) {
    zima::sketcher::rebuild_text_contours(text,y_up);
}

zima::sketcher::SketchText SketchTextPropertiesDialog::build_text(bool preview) const {
    const auto anchor=anchor_?anchor_:preview?preview_anchor_:std::nullopt;
    if (!anchor) throw std::runtime_error(drawing_text_ ? "Nejprve určete polohu textu na listu." : "Nejprve určete polohu textu ve skice.");
    const QString value = value_->toPlainText();
    if (value.isEmpty()&&!drawing_text_) throw std::runtime_error("Text nesmí být prázdný.");

    auto text = initial_;
    text.value = value.toStdString();
    text.modeling_geometry = mode_->currentData().toBool();
    if(drawing_orientation_)text.drawing_keep_readable=drawing_orientation_->currentData().toBool();
    text.anchor_x = (*anchor)[0]; text.anchor_y = (*anchor)[1];
    text.height = height_->value();
    text.horizontal = static_cast<zima::sketcher::TextHorizontalAlignment>(horizontal_->currentData().toInt());
    text.vertical = static_cast<zima::sketcher::TextVerticalAlignment>(vertical_->currentData().toInt());
    text.angle_degrees = angle_->value(); text.flipped = flipped_->isChecked() != inverted_flip_ui_;
    text.color = static_cast<zima::sketcher::SketchTextColor>(color_->currentData().toInt());
    text.font = font_->currentData().toString().toStdString();
    if(!drawing_text_)rebuild_sketch_text_contours(text, y_up_);
    return text;
}

void SketchTextPropertiesDialog::update_preview() {
    error_->clear();
    if (!preview_) return;
    try {
        preview_(anchor_||preview_anchor_ ? std::optional{build_text(true)} : std::nullopt);
    } catch (const std::exception&) {
        preview_(std::nullopt);
    }
}

bool SketchTextPropertiesDialog::submit() {
    try {
        auto text = build_text();
        if(field_action_&&field_action_->currentData().toString()=="list"&&choices_->toPlainText().trimmed().isEmpty())throw std::runtime_error("Zadejte alespoň jednu možnost seznamu.");
        commit_(std::move(text));
        return true;
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
}

}  // namespace zima::app
