#include "dimension_layout_dialog.hpp"
#include "resource_icon.hpp"
#include "drawing_dxf_device.hpp"
#include "drawing_annotation_source.hpp"
#include "drawing_annotation_layout.hpp"
#include "show_erase_dialog.hpp"
#include <zima/viewer/annotation_arrow.hpp>
#include "section_source.hpp"
#include "section_properties_dialog.hpp"
#include <QScrollArea>
#include "drawing_shading.hpp"
#include <QPdfWriter>
#include <QPageSize>
#include <QSaveFile>
#include <zima/ui/numeric_value_lock.hpp>
#include "drawing_window.hpp"
#include <zima/viewer/embedded_image.hpp>
#include "file_dialog.hpp"
#include "application_settings.hpp"

#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/workspace/workspace.hpp>

#include <QAction>
#include <QApplication>
#include <QRegularExpression>
#include <QPainterPathStroker>
#include <QPainterPath>
#include <QComboBox>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalBlocker>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QTabBar>
#include <QToolBar>
#include <QWheelEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <memory>
#include <set>

namespace zima::app {
namespace {

bool raise_open_properties(QWidget* owner) {
    for (auto* dialog : owner->findChildren<QDialog*>())
        if (dynamic_cast<zima::ui::PropertiesSubWindow*>(dialog) && dialog->isVisible()) {
            dialog->raise(); return true;
        }
    return false;
}

QString drawing_font_family() {
    static const QString family = [] {
        const QString relative = QStringLiteral("config/fonts/osifont-lgpl3fe.ttf");
        QString path = relative;
        if (!QFileInfo::exists(path)) {
            path = QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("../../config/fonts/osifont-lgpl3fe.ttf"));
        }
        const int id = QFontDatabase::addApplicationFont(path);
        const auto families = id >= 0
            ? QFontDatabase::applicationFontFamilies(id) : QStringList{};
        return families.empty() ? QStringLiteral("sans-serif") : families.front();
    }();
    return family;
}

struct DrawingSourceChoice {
    std::string id;
    std::filesystem::path path;
    QString name;
};

class ViewPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    ViewPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingView initial,
        std::vector<DrawingSourceChoice> sources, double sheet_scale,
        std::function<bool(zima::drawing::DrawingView)> accepted,
        std::function<void(zima::drawing::DrawingView)> preview,
        std::function<std::vector<zima::document::SectionDefinition>(const std::string&,const std::filesystem::path&)> sections)
        : PropertiesSubWindow(QObject::tr("Vlastnosti pohledu"), parent),
          value_(std::move(initial)), sources_(std::move(sources)),
          sections_(std::move(sections)), sheet_scale_(sheet_scale), accepted_(std::move(accepted)), preview_(std::move(preview)) {
        setObjectName("drawingViewProperties");
        auto* content = new QWidget(this);
        auto* form = new QFormLayout(content);form->setFormAlignment(Qt::AlignTop);
        name_ = new QLineEdit(QString::fromStdString(value_.name), content);
        name_->setObjectName("drawingViewName");
        caption_ = new QCheckBox(QObject::tr("Zobrazit název pohledu"), content);
        caption_->setObjectName("drawingViewCaption");
        caption_->setChecked(value_.show_caption);
        source_ = new QComboBox(content);
        source_->setObjectName("drawingViewSource");
        source_->setMinimumContentsLength(24);
        source_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        int selected = -1;
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            source_->addItem(sources_[i].name);
            if ((!value_.source_document_id.empty() && sources_[i].id == value_.source_document_id) ||
                (!value_.source_path.empty() && sources_[i].path == value_.source_path)) selected = static_cast<int>(i);
        }
        source_->setCurrentIndex(selected >= 0 ? selected : (sources_.empty() ? -1 : 0));
        auto* source_row = new QWidget(content);
        auto* source_layout = new QHBoxLayout(source_row);
        source_layout->setContentsMargins(0,0,0,0);
        auto* browse = new QPushButton(QObject::tr("Soubor…"), source_row);
        browse->setObjectName("drawingViewBrowseSource");
        source_layout->addWidget(source_, 1); source_layout->addWidget(browse);source_row->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        source_row->setEnabled(value_.parent_view_id.empty());
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = open_file(this, tr("Zdroj pohledu"),
                QString::fromStdString(value_.source_path.string()), tr("Model ZIMA-CAD (*.prtz *.asmz)"));
            if (path.isEmpty()) return;
            sources_.push_back({{}, path.toStdString(), QFileInfo(path).fileName()});
            source_->addItem(sources_.back().name);
            source_->setCurrentIndex(source_->count()-1);
        });
        orientation_ = new QComboBox(content);
        orientation_->setObjectName("drawingViewOrientation");
        const char* orientations[]{"Přední", "Zadní", "Levý", "Pravý", "Horní", "Dolní", "Izometrický"};
        for (int i=0; i<7; ++i) orientation_->addItem(QObject::tr(orientations[i]), i);
        orientation_->addItem(tr("Vlastní orientace"),-1);
        const auto standard=zima::drawing::standard_camera(value_.orientation);
        const auto equal=[](auto a,auto b){return std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)<1e-9;};
        orientation_->setCurrentIndex(equal(standard.horizontal,value_.camera.horizontal)&&equal(standard.vertical,value_.camera.vertical)&&equal(standard.depth,value_.camera.depth)?static_cast<int>(value_.orientation):7);
        orientation_->setEnabled(value_.parent_view_id.empty());
        display_ = new QComboBox(content);
        display_->setObjectName("drawingViewDisplay");
        display_->addItem(QObject::tr("Pouze viditelné hrany"));
        display_->addItem(QObject::tr("Viditelné a skryté hrany"));
        display_->addItem(QObject::tr("Stínované s hranami"));
        display_->addItem(QObject::tr("Stínované bez hran"));
        hidden_style_=new QComboBox(content);hidden_style_->setObjectName("drawingHiddenEdgeStyle");
        hidden_style_->addItems({QObject::tr("Čárkované"),QObject::tr("Šedé")});
        hidden_style_->setCurrentIndex(static_cast<int>(value_.hidden_edge_style));
        tangent_style_=new QComboBox(content);tangent_style_->setObjectName("drawingTangentEdgeStyle");
        tangent_style_->addItems({QObject::tr("Silné čáry"),QObject::tr("Tenké čáry"),QObject::tr("Skrýt")});
        tangent_style_->setCurrentIndex(static_cast<int>(value_.tangent_edge_style));
        display_->setCurrentIndex(static_cast<int>(value_.display_style));
        scale_mode_ = new QComboBox(content);
        scale_mode_->setObjectName("drawingViewScaleMode");
        scale_mode_->addItem(QObject::tr("Podle listu"));
        scale_mode_->addItem(QObject::tr("Vlastní"));
        scale_mode_->setCurrentIndex(value_.use_sheet_scale ? 0 : 1);
        scale_ = new QDoubleSpinBox(content);
        scale_->setObjectName("drawingViewScale");
        scale_->setDecimals(3); scale_->setRange(0.001,1000.0); scale_->setValue(value_.scale);
        scale_->setEnabled(!value_.use_sheet_scale);
        x_ = new QDoubleSpinBox(content); y_ = new QDoubleSpinBox(content);
        for (auto* field : {x_, y_}) { field->setDecimals(3); field->setRange(-10000,10000); }
        x_->setValue(value_.x); y_->setValue(value_.y);
        x_->setObjectName("drawingViewX"); y_->setObjectName("drawingViewY");
        // Projected views keep the position constrained by their parent's ray.
        x_->setEnabled(value_.parent_view_id.empty()); y_->setEnabled(value_.parent_view_id.empty());
        form->addRow(QObject::tr("Název"), name_); form->addRow(caption_);
        form->addRow(QObject::tr("Zdroj"), source_row);
        form->addRow(QObject::tr("Orientace"), orientation_);
        auto* rotations=new QWidget(content);rotations->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);auto* rotation_row=new QHBoxLayout(rotations);rotation_row->setContentsMargins(0,0,0,0);
        const std::array directions{zima::drawing::ProjectionDirection::Left,zima::drawing::ProjectionDirection::Right,zima::drawing::ProjectionDirection::Top,zima::drawing::ProjectionDirection::Bottom};
        const QStringList labels{tr("Doleva"),tr("Doprava"),tr("Nahoru"),tr("Dolů")};
        const QStringList ids{"drawingRotateLeft","drawingRotateRight","drawingRotateUp","drawingRotateDown"};
        for(int i=0;i<4;++i){auto* button=new QPushButton(labels[i]+" 90°",rotations);button->setObjectName(ids[i]);button->setAutoDefault(false);rotation_row->addWidget(button);rotation_buttons_.push_back(button);
            connect(button,&QPushButton::clicked,this,[this,direction=directions[i]]{
                value_.camera=zima::drawing::projected_camera(value_.camera,direction,zima::drawing::ProjectionMethod::ThirdAngle);
                {QSignalBlocker block(orientation_);orientation_->setCurrentIndex(7);}preview_(values());
            });
        }
        form->addRow(tr("Otočit pohled"),rotations);
        form->addRow(QObject::tr("Zobrazení"), display_);
        form->addRow(QObject::tr("Skryté hrany"),hidden_style_);
        form->addRow(QObject::tr("Tečné hrany"),tangent_style_);
        form->addRow(QObject::tr("Měřítko"), scale_mode_);
        form->addRow(QObject::tr("Hodnota měřítka"), scale_);
        form->addRow(QObject::tr("Poloha X [mm]"), x_);
        form->addRow(QObject::tr("Poloha Y [mm]"), y_);
        guides_=new QCheckBox(tr("Pracovní vodítka kót"),content);guides_->setObjectName("drawingDimensionGuides");guides_->setChecked(value_.show_dimension_guides);
        guide_offset_=new QDoubleSpinBox(content);guide_spacing_=new QDoubleSpinBox(content);guide_offset_->setObjectName("drawingGuideOffset");guide_spacing_->setObjectName("drawingGuideSpacing");
        guide_offset_->setRange(0,1000);guide_spacing_->setRange(.1,1000);guide_offset_->setValue(value_.dimension_guide_offset);guide_spacing_->setValue(value_.dimension_guide_spacing);
        form->addRow(guides_);form->addRow(tr("První vodítko [mm]"),guide_offset_);form->addRow(tr("Rozteč vodítek [mm]"),guide_spacing_);
        connect(guides_,&QCheckBox::toggled,this,[this]{preview_(values());});for(auto* control:{guide_offset_,guide_spacing_})connect(control,&QDoubleSpinBox::valueChanged,this,[this]{preview_(values());});
        section_=new QComboBox(content);section_->setObjectName("drawingSection");
        section_label_=new QCheckBox(tr("Zobrazit označení řezu"),content);section_label_->setObjectName("drawingSectionLabel");section_label_->setChecked(value_.show_section_label);
        components_=new SectionComponentsWidget(content);
        form->addRow(tr("Řez"),section_);
        form->addRow(section_label_);form->addRow(components_);
        marker_table_=new QTableWidget(content);marker_table_->setObjectName("drawingSectionMarkers");marker_table_->setColumnCount(1);marker_table_->setHorizontalHeaderLabels({tr("Zobrazit trasy řezů")});marker_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);marker_table_->verticalHeader()->hide();form->addRow(marker_table_);
        error_ = new QLabel(content); error_->setWordWrap(true);error_->hide();
        error_->setObjectName("drawingViewError");
        form->addRow(error_);
        auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setWidget(content);scroll->setMinimumHeight(420);content_layout()->addWidget(scroll);
        setMinimumWidth(680);
        load_sections(value_.section_id);
        connect(source_,&QComboBox::currentIndexChanged,this,[this]{load_sections({});});
        connect(section_,&QComboBox::currentIndexChanged,this,[this]{set_section_components();preview_(values());});
        components_->changed=[this]{preview_(values());};
        connect(marker_table_,&QTableWidget::itemChanged,this,[this]{preview_(values());});
        connect(section_label_,&QCheckBox::toggled,this,[this]{preview_(values());});
        const auto preview_change = [this] { preview_(values()); };
        zima::ui::bind_numeric_value_lock(x_,"x",value_.value_locks,preview_change);
        zima::ui::bind_numeric_value_lock(y_,"y",value_.value_locks,preview_change);
        zima::ui::bind_numeric_value_lock(scale_,"scale",value_.value_locks,preview_change);
        connect(orientation_,&QComboBox::currentIndexChanged,this,[this](int index){
            if(index>=0&&index<7){value_.orientation=static_cast<zima::drawing::ViewOrientation>(index);value_.camera=zima::drawing::standard_camera(value_.orientation);preview_(values());}
        });
        for (auto* combo : {source_, display_, scale_mode_,hidden_style_,tangent_style_})
            connect(combo, &QComboBox::currentIndexChanged, this, [this,preview_change] {
                scale_->setEnabled(scale_mode_->currentIndex()==1);
                preview_change();
            });
        for (auto* spin : {scale_,x_,y_})
            connect(spin, &QDoubleSpinBox::valueChanged, this, preview_change);
        connect(name_, &QLineEdit::textChanged, this, preview_change);
        connect(caption_, &QCheckBox::toggled, this, preview_change);
        setAttribute(Qt::WA_DeleteOnClose);
    }
    void move_preview(zima::drawing::Point2 position) {
        {QSignalBlocker x_block(x_),y_block(y_);x_->setValue(position.x);y_->setValue(position.y);}
        preview_(values());
    }
    void set_error(const QString& error) { error_->setText(error);error_->setVisible(!error.isEmpty()); }
    zima::drawing::DrawingView values() const {
        auto result = value_;
        result.name = name_->text().trimmed().toStdString();
        result.show_caption = caption_->isChecked();
        result.show_dimension_guides=guides_->isChecked();result.dimension_guide_offset=guide_offset_->value();result.dimension_guide_spacing=guide_spacing_->value();
        result.show_section_label=section_label_->isChecked();
        result.tangent_edge_style=static_cast<zima::drawing::TangentEdgeStyle>(tangent_style_->currentIndex());
        result.hidden_edge_style=static_cast<zima::drawing::HiddenEdgeStyle>(hidden_style_->currentIndex());
        if (const int i = source_->currentIndex(); i>=0 && i<static_cast<int>(sources_.size())) {
            result.source_document_id = sources_[i].id; result.source_path = sources_[i].path;
        }
        result.orientation = value_.orientation;
        result.display_style = static_cast<zima::drawing::DisplayStyle>(display_->currentIndex());
        result.use_sheet_scale = scale_mode_->currentIndex()==0;
        result.scale = result.use_sheet_scale ? sheet_scale_ : scale_->value();
        result.x=x_->value(); result.y=y_->value();
        result.section_id=section_->currentData().toString().toStdString();
        result.section_markers.clear();
        for(int row=0;row<marker_table_->rowCount();++row)if(marker_table_->item(row,0)->checkState()==Qt::Checked){
            const auto id=marker_table_->item(row,0)->data(Qt::UserRole).toString().toStdString();
            const auto found=std::ranges::find(available_sections_,id,&zima::document::SectionDefinition::id);if(found!=available_sections_.end())result.section_markers.push_back(*found);
        }
        if(result.section_id.empty()){result.section_snapshot.reset();result.section_parent_id.clear();}
        else for(const auto& section:available_sections_)if(section.id==result.section_id){
            result.section_snapshot=section;result.hidden_hatch_components.clear();
            for(auto [key,c]:components_->values()){
                if(c.mode==1)result.hidden_hatch_components.insert(key);
                const auto stored=section.components.find(key);
                // The table's hatch visibility is local to this Drawing view.
                // Preserve the independent 3D visibility in the source model.
                if(c.mode!=2)c.mode=stored!=section.components.end()&&stored->second.mode==1?1:0;
                if(c==zima::document::SectionComponent{} && stored==section.components.end())continue;
                result.section_snapshot->components[key]=c;
            }
        }
        return result;
    }
private:
    zima::drawing::DrawingView value_;
    std::vector<DrawingSourceChoice> sources_;
    std::function<std::vector<zima::document::SectionDefinition>(const std::string&,const std::filesystem::path&)> sections_;
    std::vector<zima::document::SectionDefinition> available_sections_;
    QCheckBox* guides_{};QDoubleSpinBox *guide_offset_{},*guide_spacing_{};
    QComboBox *section_{};QCheckBox *section_label_{};
    std::vector<QPushButton*> rotation_buttons_;
    QTableWidget* marker_table_{};
    void update_rotation_controls(){const bool enabled=value_.parent_view_id.empty();orientation_->setEnabled(enabled);for(auto* button:rotation_buttons_)button->setEnabled(enabled);}
    SectionComponentsWidget* components_{};
    void load_sections(const std::string& selected){
        QSignalBlocker block(section_);section_->clear();section_->addItem(tr("Bez řezu"),QString{});available_sections_.clear();
        try{const auto i=source_->currentIndex();if(i>=0)available_sections_=sections_(sources_[i].id,sources_[i].path);
            for(const auto& s:available_sections_)section_->addItem(QString::fromStdString(s.name),QString::fromStdString(s.id));
            auto index=section_->findData(QString::fromStdString(selected));
            if(index<0&&!selected.empty()){section_->addItem(tr("Chybějící řez"),QString::fromStdString(selected));index=section_->count()-1;}
            section_->setCurrentIndex(std::max(0,index));set_section_components();
            QSignalBlocker markers_block(marker_table_);marker_table_->setRowCount(0);
            for(const auto& s:available_sections_){const auto row=marker_table_->rowCount();marker_table_->insertRow(row);auto* item=new QTableWidgetItem(QString::fromStdString(s.name));item->setData(Qt::UserRole,QString::fromStdString(s.id));item->setFlags((item->flags()&~Qt::ItemIsEditable)|Qt::ItemIsUserCheckable);item->setCheckState(std::ranges::any_of(value_.section_markers,[&](const auto& saved){return saved.id==s.id;})?Qt::Checked:Qt::Unchecked);marker_table_->setItem(row,0,item);}
            marker_table_->resizeRowsToContents();marker_table_->setFixedHeight(std::min(150,marker_table_->horizontalHeader()->height()+marker_table_->verticalHeader()->length()+2*marker_table_->frameWidth()));marker_table_->setVisible(marker_table_->rowCount()>0);
        }catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}
    }
    void set_section_components(){
        components_->set_components({},{});const auto id=section_->currentData().toString().toStdString();
        for(const auto& s:available_sections_)if(s.id==id){
            auto settings=s.components;
            for(const auto& [key,name]:s.component_names)if(settings[key].mode!=2)
                settings[key].mode=id==value_.section_id&&value_.hidden_hatch_components.contains(key)?1:0;
            components_->set_components(s.component_names,settings);
        }
        section_label_->setEnabled(!id.empty());update_rotation_controls();
    }
    double sheet_scale_;
    std::function<bool(zima::drawing::DrawingView)> accepted_;
    std::function<void(zima::drawing::DrawingView)> preview_;
    QLineEdit* name_{};
    QCheckBox* caption_{};
    QComboBox *source_{}, *orientation_{}, *display_{}, *scale_mode_{}, *hidden_style_{}, *tangent_style_{};
    QDoubleSpinBox *scale_{}, *x_{}, *y_{};
    QLabel* error_{};
    bool submit() override { return accepted_(values()); }
};

class SheetPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    SheetPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingSheet initial,
                          std::function<void(zima::drawing::DrawingSheet)> accepted)
        : PropertiesSubWindow(QObject::tr("List"), parent),
          value_(std::move(initial)), accepted_(std::move(accepted)) {
        auto* content = new QWidget(this); auto* form = new QFormLayout(content);
        format_ = new QComboBox(content);
        for (const auto& name : {"A4", "A3", "A2", "A1", "A0"}) format_->addItem(name);
        format_->setCurrentIndex(static_cast<int>(value_.format));
        projection_ = new QComboBox(content);
        projection_->addItem(QObject::tr("První kvadrant"), 0);
        projection_->addItem(QObject::tr("Třetí kvadrant"), 1);
        projection_->setCurrentIndex(value_.projection_method == zima::drawing::ProjectionMethod::FirstAngle ? 0 : 1);
        scale_ = new QDoubleSpinBox(content); scale_->setRange(0.001, 1000.0);
        scale_->setDecimals(3); scale_->setValue(value_.default_scale);
        form->addRow(QObject::tr("Formát"), format_);
        form->addRow(QObject::tr("Promítání"), projection_);
        form->addRow(QObject::tr("Výchozí měřítko"), scale_);
        language_=new QComboBox(content);language_->setObjectName("drawingSheetLanguage");
        language_->addItems({"cs","en","de","fr","ru"});language_->setEditable(true);
        language_->setCurrentText(QString::fromStdString(value_.title_block_locale));
        form->addRow(QObject::tr("Jazyk razítka"),language_);
        thick_=new QDoubleSpinBox(content);thin_=new QDoubleSpinBox(content);red_=new QDoubleSpinBox(content);
        for(auto* spin:{thick_,thin_,red_}){spin->setRange(0.05,2.0);spin->setDecimals(2);spin->setSingleStep(0.05);spin->setSuffix(" mm");}
        thick_->setObjectName("drawingThickLine");thin_->setObjectName("drawingThinLine");red_->setObjectName("drawingRedLine");
        thick_->setValue(value_.thick_line_mm);thin_->setValue(value_.thin_line_mm);red_->setValue(value_.red_line_mm);
        form->addRow(QObject::tr("Bílá – silná čára"),thick_);form->addRow(QObject::tr("Červená čára"),red_);
        form->addRow(QObject::tr("Žlutá, zelená a skrytá čára"),thin_);
        content_layout()->addWidget(content); setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    zima::drawing::DrawingSheet value_;
    std::function<void(zima::drawing::DrawingSheet)> accepted_;
    QComboBox* format_{}; QComboBox* projection_{}; QComboBox* language_{}; QDoubleSpinBox* scale_{}; QDoubleSpinBox *thick_{},*thin_{},*red_{};
    bool submit() override {
        value_.format = static_cast<zima::drawing::SheetFormat>(format_->currentIndex());
        value_.projection_method = projection_->currentIndex() == 0
            ? zima::drawing::ProjectionMethod::FirstAngle
            : zima::drawing::ProjectionMethod::ThirdAngle;
        value_.title_block_locale=language_->currentText().trimmed().toStdString();
        value_.thick_line_mm=thick_->value();value_.thin_line_mm=thin_->value();value_.red_line_mm=red_->value();
        value_.default_scale = scale_->value(); accepted_(std::move(value_)); return true;
    }
};

class TitleBlockPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    TitleBlockPropertiesDialog(QMainWindow* parent,
        const std::vector<zima::drawing::TitleBlockField>& fields,
        const zima::drawing::TitleBlockContext& context,
        const zima::drawing::DrawingSheet& sheet, const std::set<std::string>& calculated,
        std::function<void(const std::map<std::string,std::string>&)> accepted)
        : PropertiesSubWindow(QObject::tr("Hodnoty razítka"),parent), accepted_(std::move(accepted)) {
        setObjectName("drawingTitleBlockProperties");
        auto* content=new QWidget(this); auto* form=new QFormLayout(content);
        for(const auto& field:fields) {
            const auto value=zima::drawing::resolve_title_block_text(field,context,sheet);
            auto* editor=new QLineEdit(QString::fromStdString(value),content);
            editor->setObjectName(QString::fromStdString("titleBlockField:"+field.id));
            const auto tokens=zima::drawing::title_block_tokens(field.expression);
            bool writable=field.editable;
            if(!tokens.empty()) {
                writable=writable && tokens.size()==1 && field.expression=="&"+tokens.front();
                const auto scope=zima::drawing::title_block_token_scope(tokens.front());
                if(scope=="system" || (scope=="model" && (!field.write_back ||
                    calculated.contains(zima::drawing::title_block_parameter_key(tokens.front(),context))))) writable=false;
            }
            editor->setReadOnly(!writable);
            if(writable) {editors_[field.id]=editor;initial_[field.id]=value;}
            auto label=field.id;
            if(tokens.size()==1 && zima::drawing::title_block_token_scope(tokens.front())=="model") {
                const auto key=zima::drawing::title_block_parameter_key(tokens.front(),context);
                const auto labels=context.parameter_labels.find(key);
                label=key;
                if(labels!=context.parameter_labels.end())if(const auto value=labels->second.find(sheet.title_block_locale);value!=labels->second.end()&&!value->second.empty())label=value->second;
            }
            form->addRow(QString::fromStdString(label),editor);
        }
        error_=new QLabel(this);error_->setWordWrap(true);error_->setObjectName("titleBlockError");
        content_layout()->addWidget(content);content_layout()->addWidget(error_);
        setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    std::map<std::string,QLineEdit*> editors_;
    std::map<std::string,std::string> initial_;
    std::function<void(const std::map<std::string,std::string>&)> accepted_;
    QLabel* error_{};
    bool submit() override {
        try {
            std::map<std::string,std::string> changes;
            for(const auto& [id,editor]:editors_) if(editor->text().toStdString()!=initial_.at(id))
                changes[id]=editor->text().toStdString();
            accepted_(changes);return true;
        } catch(const std::exception& error) {error_->setText(QString::fromUtf8(error.what()));return false;}
    }
};

zima::drawing::Point2 projection_placement(
    zima::drawing::ProjectionDirection direction, double distance) {
    constexpr double diagonal = 0.7071067811865475244;
    switch (direction) {
        case zima::drawing::ProjectionDirection::Right: return {-distance,0};
        case zima::drawing::ProjectionDirection::TopRight: return {-distance*diagonal,distance*diagonal};
        case zima::drawing::ProjectionDirection::Top: return {0,distance};
        case zima::drawing::ProjectionDirection::TopLeft: return {distance*diagonal,distance*diagonal};
        case zima::drawing::ProjectionDirection::Left: return {distance,0};
        case zima::drawing::ProjectionDirection::BottomLeft: return {distance*diagonal,-distance*diagonal};
        case zima::drawing::ProjectionDirection::Bottom: return {0,-distance};
        case zima::drawing::ProjectionDirection::BottomRight: return {-distance*diagonal,-distance*diagonal};
        case zima::drawing::ProjectionDirection::None: return {};
    }
    return {};
}

std::pair<std::string, zima::kernel::ViewerMesh> load_drawing_source(
    const std::filesystem::path& path, zima::workspace::Workspace* workspace = nullptr,
    const std::string& expected_document_id = {}) {
    if (workspace != nullptr) {
        std::optional<std::string> open_id;
        if (!expected_document_id.empty() && workspace->find(expected_document_id) != nullptr)
            open_id = expected_document_id;
        else open_id = workspace->document_id_for_path(path);
        if (open_id && (workspace->open_part(*open_id) != nullptr ||
                        workspace->open_assembly(*open_id) != nullptr))
            return {*open_id, workspace->authoritative_viewer_mesh(*open_id)};
    }
    if (path.extension() == ".prtz") {
        std::vector<zima::kernel::BodyResult> boundaries;
        const auto part = zima::document::PartDocument::load(path, &boundaries);
        if(boundaries.empty()&&part.kernel_operations().empty())return {part.document_id,{}};
        if (boundaries.empty()) throw std::runtime_error(
            "Part nemá uložený vypočtený model. Nejprve jej regenerujte a uložte.");
        return {part.document_id, std::move(boundaries.back().mesh)};
    }
    if (path.extension() == ".asmz") {
        const auto assembly = zima::assembly::AssemblyDocument::load(path);
        return {assembly.document_id, assembly.build_scene()};
    }
    throw std::runtime_error("Nepodporovaný zdroj výkresového pohledu.");
}

zima::drawing::TitleBlockContext build_title_block_context_for_source(
    const std::string&, const std::filesystem::path&, zima::workspace::Workspace*);

// Builds BOM rows from the current state of an Assembly source (by open
// workspace document if available, otherwise by loading the .asmz file),
// so both initial view insertion and later view regeneration can rebuild
// the BOM from the assembly's up-to-date component list.
std::vector<zima::drawing::BomRow> build_bom_rows_for_source(
    const std::string& source_id, const std::filesystem::path& source_path,
    zima::workspace::Workspace* workspace) {
    std::vector<zima::drawing::BomRow> bom;
    const zima::assembly::AssemblyDocument* assembly{};
    std::optional<zima::assembly::AssemblyDocument> loaded;
    if (workspace != nullptr) if (const auto* open = workspace->open_assembly(source_id))
        assembly = &open->session.document();
    if (assembly == nullptr && !source_path.empty() && source_path.extension() == ".asmz") {
        loaded = zima::assembly::AssemblyDocument::load(source_path); assembly = &*loaded;
    }
    const auto append=[&](const std::string& id,std::filesystem::path path,const std::string& name) {
        if(path.is_relative()&&!source_path.empty())path=source_path.parent_path()/path;
        const auto key=id+"|"+path.lexically_normal().string();
        const auto existing=std::ranges::find(bom,key,&zima::drawing::BomRow::designation);
        if(existing!=bom.end()){++existing->quantity;return;}
        auto context=build_title_block_context_for_source(id,path,workspace);
        zima::drawing::BomRow row{static_cast<int>(bom.size()+1),1,name,key,{}};
        row.source_document_id=id;row.source_path=path;
        row.mass_unit=context.mass_unit;
        row.file_stem=context.file_stem;row.parameters=std::move(context.parameters);
        row.parameter_values=std::move(context.parameter_values);row.parameter_aliases=std::move(context.parameter_aliases);
        bom.push_back(std::move(row));
    };
    if(assembly) {
        const auto suppressed=assembly->effectively_suppressed_occurrences();
        for(const auto& component:assembly->components)if(!suppressed.contains(component.occurrence_id))
            append(component.source_document_id,component.source_path,component.name);
    }
    else if((workspace&&workspace->open_part(source_id))||source_path.extension()==".prtz")
        append(source_id,source_path.is_relative()?std::filesystem::absolute(source_path):source_path,source_path.stem().string());
    return bom;
}

// Read the authoritative source Parameters without a geometry calculation.
zima::drawing::TitleBlockContext build_title_block_context_for_source(
    const std::string& source_id, const std::filesystem::path& source_path,
    zima::workspace::Workspace* workspace) {
    zima::drawing::TitleBlockContext context;
    context.file_stem = source_path.stem().string();
    if(workspace && !workspace->find(source_id))if(const auto id=workspace->document_id_for_path(source_path))
        return build_title_block_context_for_source(*id,source_path,workspace);
    const zima::document::PartDocument* part{};
    std::optional<zima::document::PartDocument> loaded_part;
    if (workspace != nullptr) if (const auto* open = workspace->open_part(source_id))
        part = &open->session.document();
    if (part == nullptr && !source_path.empty() && source_path.extension() == ".prtz") {
        try { loaded_part = zima::document::PartDocument::load(source_path); part = &*loaded_part; }
        catch (const std::exception&) { part = nullptr; }
    }
    const auto use_parameters=[&](const auto& document) {
        if(context.file_stem.empty())context.file_stem=document.name;
        context.mass_unit = document.document_units.at("Mass");
        context.parameters = document.user_parameters;
        context.parameter_values = document.user_parameter_values;
        context.parameter_labels = document.user_parameter_labels;
        context.parameter_order = document.user_parameter_order;
        if(context.parameter_order.empty())for(const auto& [key,value]:document.user_parameters)
            context.parameter_order.push_back(key);
        for (const auto& [key, labels] : document.user_parameter_labels)
            for (const auto& [locale, label] : labels)
                if (!label.empty()) context.parameter_aliases[label] = key;
    };
    if (part != nullptr) use_parameters(*part);
    else if(workspace && workspace->open_assembly(source_id))
        use_parameters(workspace->open_assembly(source_id)->session.document());
    else if(!source_path.empty() && source_path.extension()==".asmz") {
        const auto assembly=zima::assembly::AssemblyDocument::load(source_path);use_parameters(assembly);
    }
    return context;
}

}  // namespace

class DrawingCanvas final : public QWidget {
    enum class AnnotationKind { Caption, SectionLabel, Dimension, SectionEnd, Model };
    struct AnnotationKey {
        AnnotationKind kind{};std::string view,id;int end{};
        bool operator==(const AnnotationKey&)const=default;
    };
    struct AnnotationHandle {
        AnnotationKey key;QPointF point;QPainterPath hit;
        zima::drawing::Point2 direction{};double offset{},minimum{};
    };
    std::set<std::string> model_offered_;
    std::function<void(const std::string&)> choose_view_;
    std::function<void(const std::string&)> model_pick_;
    std::optional<AnnotationHandle> dragged_model_;
    QPointF model_drag_start_;bool model_moved_{};
    std::optional<drawing::ModelAnnotation> model_drag_original_;
    std::map<std::string,zima::drawing::TitleBlockTextTarget> title_targets_;
    std::vector<AnnotationHandle> annotation_handles_;
    std::vector<AnnotationHandle> offered_annotations_;
    std::size_t offered_annotation_index_{};
    std::optional<AnnotationKey> selected_annotation_,hovered_annotation_;
    std::optional<AnnotationHandle> dragged_section_end_;
    QPointF section_end_drag_start_;
    bool section_end_moved_{};
    QColor annotation_color(const AnnotationKey& key,QColor normal,bool printing)const{
        if(printing)return normal;
        const auto same=[&](const auto& candidate){return candidate&&candidate->kind==key.kind&&candidate->view==key.view&&candidate->id==key.id;};
        if(same(selected_annotation_))return QColor("#00D1FF");if(same(hovered_annotation_))return QColor("#FF9300");return normal;
    }
    void offer_annotations(QPointF point){
        std::vector<AnnotationHandle> candidates;
        for(auto it=annotation_handles_.rbegin();it!=annotation_handles_.rend();++it)if((!model_pick_||(it->key.kind==AnnotationKind::Model&&preview_&&it->key.view==preview_->id&&model_offered_.contains(it->key.id)))&&(it->hit.contains(point)||QLineF(point,it->point).length()<=8))candidates.push_back(*it);
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){const auto da=QLineF(point,a.point).length(),db=QLineF(point,b.point).length();if((da<=8)!=(db<=8))return da<=8;return da<=8&&db<=8&&da<db;});
        if(model_pick_){std::set<std::pair<std::string,std::string>> entities;std::erase_if(candidates,[&](const auto& c){return !entities.emplace(c.key.view,c.key.id).second;});}
        bool same=candidates.size()==offered_annotations_.size();for(std::size_t i=0;same&&i<candidates.size();++i)same=candidates[i].key==offered_annotations_[i].key;
        if(!same)offered_annotation_index_=0;offered_annotations_=std::move(candidates);
        hovered_annotation_=offered_annotations_.empty()?std::optional<AnnotationKey>{}:offered_annotations_[offered_annotation_index_].key;
    }
public:
    explicit DrawingCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 480); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
        auto drawing_font = font();
        drawing_font.setFamily(drawing_font_family());
        setFont(drawing_font);
        auto canvas_palette = palette();
        canvas_palette.setColor(QPalette::Window, QColor("#000000"));
        setPalette(canvas_palette);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void set_sheet(zima::drawing::DrawingSheet* sheet) {
        sheet_ = sheet;shaded_cache_.clear();annotation_handles_.clear();offered_annotations_.clear();selected_annotation_.reset();hovered_annotation_.reset();dragged_section_end_.reset();
        selected_.clear();selected_field_.clear();hovered_field_.clear();field_regions_.clear();title_targets_.clear();
        selected_dimension_id_.clear();
        dragged_dimension_id_.clear();
        drag_view_id_.clear();
        first_edge_.reset();
        dimension_mode_ = false;
        update();
    }
    [[nodiscard]] const std::string& selected_view_id() const { return selected_; }
    [[nodiscard]] const std::string& selected_dimension_id() const { return selected_dimension_id_; }
    void select_view_for_test(const std::string& view_id) {
        selected_ = view_id; selected_dimension_id_.clear();selected_annotation_.reset();
        if (selection_changed_) selection_changed_();
        update();
    }
    void set_title_block_context(std::optional<zima::drawing::TitleBlockContext> context) {
        title_block_context_ = std::move(context);
        update();
    }
    void set_changed_callback(std::function<void()> callback) { changed_=std::move(callback); }
    void set_selection_changed_callback(std::function<void()> callback) {
        selection_changed_ = std::move(callback);
    }
    void set_preview_move_handler(std::function<void(zima::drawing::Point2)> handler) {
        preview_move_=std::move(handler);preview_dragging_=false;
    }
    void set_preview(std::optional<zima::drawing::DrawingView> view) {
        if(!view)preview_dragging_=false;
        if(preview_)shaded_cache_.erase(preview_->id);
        if(view)shaded_cache_.erase(view->id);
        preview_ = std::move(view); update();
    }
    void begin_placement(zima::drawing::DrawingView view,
        std::function<void(zima::drawing::DrawingView)> placed,
        std::function<void()> canceled,
        std::function<void(zima::drawing::DrawingView&, zima::drawing::Point2)> position = {}) {
        start_selection(); selected_.clear(); selected_dimension_id_.clear();
        if (selection_changed_) selection_changed_();
        preview_ = std::move(view); placed_ = std::move(placed);
        canceled_ = std::move(canceled); position_ = std::move(position);
        setCursor(Qt::CrossCursor); setFocus(); update();
    }
    void cancel_placement() {
        placed_ = {}; position_ = {}; preview_.reset(); unsetCursor();
        auto callback = std::move(canceled_); canceled_ = {};
        if (callback) callback();
        update();
    }
    void set_title_block_action(QAction* action) { title_action_=action; }
    const std::string& selected_title_field() const { return selected_field_; }
    std::optional<zima::drawing::TitleBlockTextTarget> selected_title_target() const {
        const auto found=title_targets_.find(selected_field_);return found==title_targets_.end()?std::nullopt:std::optional{found->second};
    }
    std::optional<QPointF> title_field_center(const std::string& id) const {
        for(const auto& [key,polygon]:field_regions_)if(key==id)return polygon.boundingRect().center();
        return {};
    }
    std::string field_at(QPointF point) const {
        for(auto it=field_regions_.rbegin();it!=field_regions_.rend();++it)
            if(it->second.containsPoint(point,Qt::OddEvenFill))return it->first;
        return {};
    }
    void set_context_actions(QAction* insert, QAction* edit, QAction* projected, QAction* remove) {
        insert_action_=insert; edit_action_=edit; projected_action_=projected; remove_action_=remove;
    }
    void set_dimension_properties_callback(std::function<void(const std::string&,const std::string&)> callback){dimension_properties_=std::move(callback);}
    void start_linear_dimension() { dimension_mode_ = true; first_edge_.reset(); update(); }
    void fit_sheet() { view_zoom_=1;view_pan_={};update(); }
    QRectF sheet_rectangle()const {
        if(!sheet_)return {};
        const auto zoom=canvas_zoom();
        return {canvas_origin(zoom),QSizeF(sheet_->width_mm()*zoom,sheet_->height_mm()*zoom)};
    }
    void choose_view(std::function<void(const std::string&)> pick) { start_selection();choose_view_=std::move(pick);selected_.clear();if(selection_changed_)selection_changed_();update(); }
    void start_selection() { choose_view_={};dimension_mode_ = false; first_edge_.reset(); update(); }
    [[nodiscard]] bool dimension_mode() const { return dimension_mode_; }
    bool interacting() const { return bool(preview_); }
protected:
    QPointF view_screen_point(const zima::drawing::DrawingView& view,
                            const zima::drawing::Point2& point) const {
        const double zoom = canvas_zoom(); const auto origin = canvas_origin(zoom);
        return {origin.x() + (sheet_->width_mm() - view.x + point.x * view.scale)*zoom,
                origin.y() + (sheet_->height_mm() - view.y - point.y * view.scale)*zoom};
    }
    QRectF view_bounds(const zima::drawing::DrawingView& view) const {
        return view_bounds_at(view,canvas_zoom(),canvas_origin(canvas_zoom())).adjusted(-8,-8,8,8);
    }
    QRectF view_bounds_at(const zima::drawing::DrawingView& view,double zoom,QPointF origin) const {
        bool first = true; double xmin{}, xmax{}, ymin{}, ymax{};
        const auto include = [&](const zima::drawing::Point2& point) {
            const auto screen = QPointF(origin.x()+(sheet_->width_mm()-view.x+point.x*view.scale)*zoom,origin.y()+(sheet_->height_mm()-view.y-point.y*view.scale)*zoom);
            if (first) { xmin=xmax=screen.x(); ymin=ymax=screen.y(); first=false; }
            else { xmin=std::min(xmin,screen.x()); xmax=std::max(xmax,screen.x());
                   ymin=std::min(ymin,screen.y()); ymax=std::max(ymax,screen.y()); }
        };
        for (const auto& edge : view.projected_edges) for (const auto& point : edge.points) include(point);
        for (const auto& triangle : view.projected_triangles) for (const auto& point : triangle.points) include(point);
        if (first) include({});
        return QRectF(QPointF(xmin,ymin),QPointF(xmax,ymax));
    }
    QString label_text(const zima::drawing::DrawingView& view,bool section,bool printing=false)const{
        if(section?(!view.show_section_label||view.section_id.empty()||!view.section_snapshot):!view.show_caption)return {};
        const auto value=QString::fromStdString(section?view.section_snapshot->name:view.name);
        return !printing&&value.trimmed().isEmpty()?QStringLiteral("-"):value;
    }
    QRectF label_bounds(const zima::drawing::DrawingView& view,bool section,double zoom,QPointF origin)const{
        const auto text=label_text(view,section);if(text.isEmpty())return {};
        QFont font(drawing_font_family());font.setPixelSize(1000);const QFontMetricsF metrics(font);
        const auto bounds=view_bounds_at(view,zoom,origin);const auto& position=section?view.section_label_position:view.caption_position;
        const bool both=view.show_caption&&view.show_section_label&&!view.section_id.empty()&&view.section_snapshot;
        const QPointF center=position?QPointF(origin.x()+(sheet_->width_mm()-view.x+position->x)*zoom,origin.y()+(sheet_->height_mm()-view.y-position->y)*zoom)
            :QPointF(bounds.center().x(),bounds.top()-((section?6.5:5.75)+(!section&&both?8:0))*zoom);
        const double height=section?5.0:3.5;
        const double width=std::max(1.0,metrics.horizontalAdvance(text))*height*zoom/metrics.capHeight();
        return QRectF(center.x()-width/2-zoom,center.y()-(height+2)*zoom/2,width+2*zoom,(height+2)*zoom);
    }
public:
    std::optional<QPointF> annotation_point(const std::string& id,int end,bool dimension)const{
        for(const auto& handle:annotation_handles_)if(handle.key.id==id&&handle.key.end==end&&handle.key.kind==(dimension?AnnotationKind::Dimension:AnnotationKind::SectionEnd))return handle.point;return {};
    }
    std::optional<QPointF> rectangle_center(const std::string& id)const {
        if(preview_&&preview_->id==id)return view_bounds(*preview_).center();
        if(sheet_)for(const auto& view:sheet_->views)if(view.id==id)return view_bounds(view).center();
        return {};
    }
    std::optional<QPointF> label_center(const std::string& id,bool section)const{
        if(sheet_)for(const auto& view:sheet_->views)if(view.id==id){const auto bounds=label_bounds(view,section,canvas_zoom(),canvas_origin(canvas_zoom()));if(!bounds.isEmpty())return bounds.center();}return {};
    }
protected:
    std::string view_at(QPointF point) const {
        if (!sheet_) return {};

        // Later views are painted on top. Hover and all confirmation paths
        // consume this same order and the same complete rectangular region.
        for (auto it=sheet_->views.rbegin(); it!=sheet_->views.rend(); ++it)
            if (view_bounds(*it).contains(point)) return it->id;
        return {};
    }
    void position_preview(QPointF point) {
        if (!preview_ || !sheet_) return;
        const double zoom=canvas_zoom(); const auto origin=canvas_origin(zoom);
        const zima::drawing::Point2 position{sheet_->width_mm()-(point.x()-origin.x())/zoom,
            sheet_->height_mm()-(point.y()-origin.y())/zoom};
        if (position_) position_(*preview_, position);
        else { preview_->x=position.x; preview_->y=position.y; }
        update();
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        if(dragged_model_){event->accept();return;}
        if(selected_annotation_)for(const auto& handle:annotation_handles_)
            if(handle.key.kind==selected_annotation_->kind && handle.key.view==selected_annotation_->view && handle.key.id==selected_annotation_->id && QLineF(handle.point,event->pos()).length()<=8){event->accept();return;}
        if(model_pick_){offer_annotations(event->pos());if(!offered_annotations_.empty()){offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;update();}event->accept();return;}
        if (placed_ || preview_ || dimension_mode_ || view_panning_) return;
        if(const auto field=field_at(event->pos());!field.empty()) {
            selected_field_=field;selected_.clear();selected_dimension_id_.clear();selected_annotation_.reset();
            if(selection_changed_)selection_changed_();update();
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->addAction(title_action_);menu->popup(event->globalPos());event->accept();return;
        }
        selected_field_.clear();
        offer_annotations(event->pos());
        if(!offered_annotations_.empty()&&(!selected_annotation_||*selected_annotation_!=offered_annotations_[offered_annotation_index_].key)){
            offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;update();event->accept();return;
        }
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Model&&!offered_annotations_.empty()&&dimension_properties_) {
            const auto key=*selected_annotation_;
            const auto view=std::ranges::find(sheet_->views,key.view,&drawing::DrawingView::id);
            if(view!=sheet_->views.end()&&std::ranges::any_of(view->model_annotations,[&](const auto& a){return a.kind==drawing::ModelAnnotationKind::Dimension&&model_annotation_key(a.source)==key.id;})) {
                auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
                auto* properties=menu->addAction(tr("Zobrazení kóty…"));properties->setObjectName("dimensionLayoutPropertiesAction");
                connect(properties,&QAction::triggered,this,[this,key]{dimension_properties_(key.view,key.id);});menu->popup(event->globalPos());event->accept();return;
            }
        }
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension&&!offered_annotations_.empty()){
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);const auto id=selected_dimension_id_;
            menu->addAction(tr("Odstranit"),this,[this,id]{std::erase_if(sheet_->dimensions,[&](const auto& d){return d.id==id;});selected_dimension_id_.clear();selected_annotation_.reset();if(changed_)changed_();if(selection_changed_)selection_changed_();update();});menu->popup(event->globalPos());event->accept();return;
        }
        const auto hit=!offered_annotations_.empty()?offered_annotations_[offered_annotation_index_].key.view:view_at(event->pos());
        if (hit != selected_) {
            selected_=hit; selected_dimension_id_.clear();
            if (selection_changed_) selection_changed_();
            update();
        }
        auto* menu = new QMenu(this); menu->setObjectName("drawingViewContextMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        if (hit.empty()) menu->addAction(insert_action_);
        else { menu->addAction(edit_action_); menu->addAction(projected_action_);
               menu->addSeparator(); menu->addAction(remove_action_); }
        menu->popup(event->globalPos()); event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton && !placed_ && !preview_ && !dimension_mode_) {
            const auto field=field_at(event->position());
            if(!field.empty()) {
                selected_field_=field;selected_.clear();selected_dimension_id_.clear();selected_annotation_.reset();drag_view_id_.clear();
                if(selection_changed_)selection_changed_();
                if(title_action_)title_action_->trigger();event->accept();return;
            }
        }
        if (event->button()==Qt::LeftButton && !preview_ && !dimension_mode_ && !view_at(event->position()).empty()) {
            selected_=view_at(event->position()); drag_view_id_.clear();
            if(selection_changed_) selection_changed_();
            if(edit_action_) edit_action_->trigger();
            event->accept(); return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }
    void leaveEvent(QEvent* event) override { hovered_field_.clear(); hovered_.clear();hovered_annotation_.reset();offered_annotations_.clear(); update(); QWidget::leaveEvent(event); }
    [[nodiscard]] double canvas_zoom() const {
        if (sheet_ == nullptr) return 1.0;
        const double margin = 24.0;
        const double fit = (height() - 2 * margin) / sheet_->height_mm();
        return std::max(1.0e-6, fit * view_zoom_);
    }
    [[nodiscard]] QPointF canvas_origin(double zoom) const {
        if (sheet_ == nullptr) return view_pan_;
        return QPointF((width() - sheet_->width_mm() * zoom) * 0.5,
                       (height() - sheet_->height_mm() * zoom) * 0.5) + view_pan_;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.fillRect(rect(),QColor("#000000"));
        paint_sheet(painter,canvas_zoom(),canvas_origin(canvas_zoom()),false);
    }
public:
    void set_model_command(std::set<std::string> offered,std::function<void(const std::string&)> pick){model_offered_=std::move(offered);model_pick_=std::move(pick);selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear();update();}
    std::optional<QPointF> model_handle(const std::string& key,int end,const std::string& view)const{for(const auto& h:annotation_handles_)if(h.key.kind==AnnotationKind::Model&&h.key.id==key&&h.key.end==end&&(view.empty()||h.key.view==view))return h.point;return {};}
    void set_lineweights(bool value){lineweights_=value;update();}
    void paint_sheet(QPainter& painter,double zoom,QPointF origin,bool printing) {
        if(!sheet_)return;
        if(!printing)annotation_handles_.clear();
        const auto width=[&](bool thick){return printing||lineweights_?zoom*(thick?sheet_->thick_line_mm:sheet_->thin_line_mm):1.0;};
        const auto ink=printing?QColor(Qt::black):QColor(Qt::white);
        std::vector<const zima::drawing::DrawingView*> views;
        for(const auto& view:sheet_->views)if(printing||!preview_||preview_->id!=view.id)views.push_back(&view);
        if(!printing&&preview_)views.push_back(&*preview_);
        const QRectF paper(origin.x(), origin.y(), sheet_->width_mm() * zoom,
                           sheet_->height_mm() * zoom);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont annotation_font(drawing_font_family());annotation_font.setPixelSize(std::max(1,static_cast<int>(3.5*zoom)));painter.setFont(annotation_font);
        painter.setPen(QPen(QColor("#808080"), 1.0));
        if(!printing)painter.drawRect(paper);
        painter.setPen(QPen(ink,width(false)));
        const auto screen=[&](const zima::drawing::Point2& point) {
            return QPointF(origin.x()+sheet_->width_mm()*zoom-point.x*zoom,
                           origin.y()+sheet_->height_mm()*zoom-point.y*zoom);
        };
        const auto pen_color=[&](zima::drawing::DrawingPen pen) {
            if(printing)return QColor(Qt::black);
            return pen==zima::drawing::DrawingPen::Red?QColor("#FF0000"):pen == zima::drawing::DrawingPen::Yellow ? QColor("#E6C85C")
                : pen == zima::drawing::DrawingPen::Green ? QColor("#4DD811") : QColor("#FFFFFF");
        };
        if (sheet_->frame_lines.empty()) {
            const double frame = 10.0 * zoom;
            painter.drawRect(paper.adjusted(frame, frame, -frame, -frame));
        }
        if(!printing)field_regions_.clear();
        const auto draw_handle=[&](QPointF point,bool selected) {
            painter.save();painter.setPen(Qt::NoPen);
            painter.setBrush(selected?QColor("#D05CFF"):QColor("#FF9300"));
            // Same 4.5 logical-pixel radius as ordinary Sketcher point markers.
            painter.drawEllipse(point,4.5,4.5);painter.restore();
        };
        const auto draw_text=[&](const zima::drawing::TemplateText& text) {
            auto value=QString::fromStdString(text.text);
            if(value.trimmed().isEmpty()){if(printing)return;value=QStringLiteral("-");}
            const bool selected=!printing&&!text.field_id.empty()&&selected_field_==text.field_id;
            const bool hovered=!printing&&!text.field_id.empty()&&hovered_field_==text.field_id;
            painter.save();painter.setPen(selected?QColor("#00D1FF"):hovered?QColor("#FF9300"):pen_color(text.pen));
            QFont font(QString::fromStdString(text.font));font.setPixelSize(1000);painter.setFont(font);
            const QFontMetricsF metrics(font);
            const auto ink=metrics.tightBoundingRect(value);const auto anchor=screen(text.position);
            const double scale=text.height/std::max(1.0,metrics.capHeight());
            const double angle=text.angle*3.141592653589793/180.0,flip=text.flipped?-1:1;
            const QPointF x=screen({text.position.x+flip*std::cos(angle)*scale,text.position.y+flip*std::sin(angle)*scale})-anchor;
            const QPointF y=screen({text.position.x+std::sin(angle)*scale,text.position.y-std::cos(angle)*scale})-anchor;
            const QTransform transform(x.x(),x.y(),y.x(),y.y(),anchor.x(),anchor.y());
            painter.setTransform(transform,true);
            auto alignment=QString::fromStdString(text.alignment).toLower();
            const double dx=alignment=="center"?-ink.center().x():alignment=="right"?-ink.right():-ink.left();
            const double dy=text.vertical_alignment=="top"?-ink.top():text.vertical_alignment=="middle"||text.vertical_alignment=="center"?-ink.center().y():text.vertical_alignment=="baseline"?0:-ink.bottom();
            painter.drawText(QPointF(dx,dy),value);painter.restore();
            if(!printing&&!text.field_id.empty()) {
                const auto polygon=transform.map(QPolygonF(ink.translated(dx,dy).adjusted(-60,-60,60,60)));
                field_regions_.push_back({text.field_id,polygon});
                // Fixed title-block fields remain ordinary text selection.
            }
        };
        const auto pen_width=[&](zima::drawing::DrawingPen pen){return printing||lineweights_?zoom*zima::drawing::drawing_pen_width_mm(*sheet_,pen):1.0;};
        const auto draw_template=[&](const auto& lines,const auto& texts,const auto& circles) {
            for(const auto& line:lines){painter.setPen(QPen(pen_color(line.pen),pen_width(line.pen)));painter.drawLine(screen(line.first),screen(line.second));}
            for(const auto& circle:circles){painter.setPen(QPen(pen_color(circle.pen),pen_width(circle.pen)));painter.setBrush(Qt::NoBrush);painter.drawEllipse(screen(circle.center),circle.radius*zoom,circle.radius*zoom);}
            for(const auto& text:texts)draw_text(text);
        };
        draw_template(sheet_->frame_lines,sheet_->frame_texts,sheet_->frame_circles);
        const auto layout=zima::drawing::title_block_layout(*sheet_,title_block_context_.value_or(zima::drawing::TitleBlockContext{}));
        if(!printing)title_targets_=layout.edit_targets;
        for(const auto& image:layout.images) {
            QPolygonF target;for(const auto& point:image.corners())target<<screen({point[0],point[1]});
            zima::viewer::paint_embedded_image(painter,image.data_base64,image.format,target);
        }
        draw_template(layout.lines,layout.texts,layout.circles);
        for (const auto* rendered_view : views) {
            const auto& view = *rendered_view;
            if (view.display_style != zima::drawing::DisplayStyle::ShadedWithEdges && view.display_style != zima::drawing::DisplayStyle::Shaded) continue;
            bool first=true;QRectF model_bounds;
            for(const auto& triangle:view.projected_triangles)for(const auto& point:triangle.points) {
                const QPointF p(point.x,point.y);
                if(first){model_bounds=QRectF(p,p);first=false;}
                else model_bounds=QRectF(QPointF(std::min(model_bounds.left(),p.x()),std::min(model_bounds.top(),p.y())),
                    QPointF(std::max(model_bounds.right(),p.x()),std::max(model_bounds.bottom(),p.y())));
            }
            const QRectF target(origin.x()+(sheet_->width_mm()-view.x+model_bounds.left()*view.scale)*zoom,
                origin.y()+(sheet_->height_mm()-view.y-model_bounds.bottom()*view.scale)*zoom,
                model_bounds.width()*view.scale*zoom,model_bounds.height()*view.scale*zoom);
            const double resolution=zoom*view.scale*(printing?1.0:2.0);
            auto& cached=shaded_cache_[view.id];
            if(cached.image.isNull()||cached.resolution!=resolution||cached.bounds!=model_bounds||cached.triangles!=view.projected_triangles.data()) {
                cached.image=drawing_shaded_fill(view,model_bounds,resolution);
                cached.resolution=resolution;cached.bounds=model_bounds;cached.triangles=view.projected_triangles.data();
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
            painter.drawImage(target,cached.image);

        }
        painter.setBrush(Qt::NoBrush);
        for (const auto* rendered_view : views) {
            const auto& view = *rendered_view;
            for(bool hidden_pass:{true,false})for (const auto& edge : view.projected_edges) {
                if(edge.hidden!=hidden_pass)continue;
                if(!zima::drawing::drawing_edge_visible(view,edge))continue;
                const bool gray=edge.hidden&&view.hidden_edge_style==zima::drawing::HiddenEdgeStyle::Gray;
                const QColor edge_color=!printing&&!model_pick_&&!selected_annotation_&&view.id==selected_?QColor("#00D1FF"):
                    edge.hatch&&!printing?QColor("#55BB77"):(edge.hidden||edge.tangent)&&!printing?QColor("#666666"):gray?QColor("#808080"):ink;
                QPen pen(edge_color,width(!edge.hatch&&!edge.hidden&&!(edge.tangent&&view.tangent_edge_style==zima::drawing::TangentEdgeStyle::Thin)));
                pen.setCapStyle(Qt::FlatCap);pen.setJoinStyle(Qt::RoundJoin);
                if((edge.hidden&&!gray)||(edge.hatch&&edge.hatch_pattern==2)){pen.setDashPattern({3.0*zoom/pen.widthF(),1.5*zoom/pen.widthF()});}
                painter.setPen(pen);
                if (edge.points.size() < 2) continue;
                QPolygonF line;
                for (const auto& point : edge.points) {
                    line << QPointF(origin.x() + sheet_->width_mm()*zoom -
                                        (view.x - point.x * view.scale) * zoom,
                                    origin.y() + sheet_->height_mm()*zoom -
                                        (view.y + point.y * view.scale) * zoom);
                }
                painter.drawPolyline(line);
            }
        }
        painter.setBrush(Qt::NoBrush);
        for (const auto* view : views) {
            const auto bounds = printing?view_bounds_at(*view,zoom,origin):view_bounds(*view);
            if (!printing&&((!selected_annotation_&&view->id==selected_) || view->id==hovered_ || (preview_ && preview_->id==view->id))) {
                painter.setPen(QPen(view->id==hovered_ && view->id!=selected_ ? QColor("#FF9300")
                    : QColor("#00D1FF"), 1, Qt::DashLine));
                painter.drawRect(bounds);
            }
            for(bool section:{false,true}){
                const auto text=label_text(*view,section,printing);if(text.isEmpty())continue;
                const auto rect=label_bounds(*view,section,zoom,origin);
                const AnnotationKey key{section?AnnotationKind::SectionLabel:AnnotationKind::Caption,view->id,{},0};
                if(!printing){QPainterPath hit;hit.addRect(rect);annotation_handles_.push_back({key,rect.center(),hit});}
                painter.save();painter.setPen(annotation_color(key,printing||section?ink:QColor("#4DD811"),printing));
                QFont font(drawing_font_family());font.setPixelSize(1000);painter.setFont(font);const QFontMetricsF metrics(font);
                painter.translate(rect.left()+zoom,rect.bottom()-zoom);const double scale=(section?5.0:3.5)*zoom/metrics.capHeight();painter.scale(scale,scale);painter.drawText(QPointF(0,0),text);painter.restore();
            }
        }
        // Model annotation strokes are shared by drawing, hit testing and PDF.
        for(const auto* view:views){
            const auto screen=[&](QPointF p){return QPointF(origin.x()+(sheet_->width_mm()-view->x+p.x())*zoom,origin.y()+(sheet_->height_mm()-view->y-p.y())*zoom);};
            const auto b=view_bounds_at(*view,zoom,origin);const auto o=screen({});
            const QRectF paper_bounds((b.left()-o.x())/zoom,(o.y()-b.bottom())/zoom,b.width()/zoom,b.height()/zoom);
            if(!printing&&view->show_dimension_guides){
                painter.save();painter.setPen(QPen(QColor("#666666"),1,Qt::DashLine));
                std::set<std::pair<std::string,std::string>> drawn;
                for(const auto& item:view->model_annotations)if(item.visible&&item.model_envelope.valid&&drawn.emplace(item.source.owner_id,item.source.instance_path).second){
                    for(int level=-1;level<4;++level){auto frame=item.model_envelope;const double offset=level<0?0:(view->dimension_guide_offset+level*view->dimension_guide_spacing)/view->scale;
                        frame.minimum=kernel::dimension_sub(frame.minimum,{offset,offset,offset});frame.maximum=kernel::dimension_add(frame.maximum,{offset,offset,offset});
                        const auto corners=frame.corners();const auto projected=[&](auto p){return screen({kernel::dimension_dot(p,view->camera.horizontal)*view->scale,kernel::dimension_dot(p,view->camera.vertical)*view->scale});};
                        for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit))painter.drawLine(projected(corners[i]),projected(corners[i|bit]));
                    }
                }painter.restore();
            }
            for(const auto& item:view->model_annotations){
                const auto id=model_annotation_key(item.source);const bool offered=!printing&&model_pick_&&preview_&&view->id==preview_->id&&model_offered_.contains(id);
                if(!item.visible&&!offered)continue;
                const auto layout=model_annotation_layout(*view,item,paper_bounds.adjusted(-5,-5,5,5),QFontMetricsF(painter.font()).horizontalAdvance(QString::fromStdString(item.text))/zoom);
                if(item.model_dimension && layout.curves.empty())continue;
                const AnnotationKey key{AnnotationKind::Model,view->id,id,0};
                QColor color=annotation_color(key,printing?ink:item.unresolved?QColor("#E05050"):!item.visible?QColor("#777777"):item.kind==drawing::ModelAnnotationKind::Dimension?QColor("#FFD400"):QColor("#E6C85C"),printing);
                painter.save();QPen pen(color,width(false));if(item.kind!=drawing::ModelAnnotationKind::Dimension)pen.setDashPattern({8*zoom/pen.widthF(),1.5*zoom/pen.widthF(),.5*zoom/pen.widthF(),1.5*zoom/pen.widthF()});painter.setPen(pen);painter.setBrush(Qt::NoBrush);QPainterPath stroke;
                for(const auto& line:layout.curves){if(line.empty())continue;QPolygonF polygon;for(auto p:line)polygon<<screen(p);painter.drawPolyline(polygon);stroke.moveTo(polygon.front());for(qsizetype i=1;i<polygon.size();++i)stroke.lineTo(polygon[i]);}
                for(const auto& center:layout.centers){painter.save();painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(screen(center),.35*zoom,.35*zoom);painter.restore();}
                for(const auto& [tip,direction]:layout.arrows){painter.save();painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawPolygon(viewer::annotation_arrow(screen(tip),{direction.x(),-direction.y()},2.5*zoom));painter.restore();}
                const auto text=QString::fromStdString(item.text);const auto text_point=screen(layout.text);
                QTransform text_transform;text_transform.translate(text_point.x(),text_point.y());text_transform.rotate(layout.text_angle);
                painter.save();painter.translate(text_point);painter.rotate(layout.text_angle);painter.drawText(QPointF{},text);painter.restore();
                if(!printing){QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);if(!text.isEmpty())hit.addRect(text_transform.mapRect(QFontMetricsF(painter.font()).boundingRect(text)));
                    if(layout.handles.empty()){if(!layout.curves.empty()&&!layout.curves[0].empty())annotation_handles_.push_back({key,screen(layout.curves[0].front()),hit});}
                    else for(const auto& [name,p]:layout.handles){auto handle=key;handle.end=name=="text"?0:name=="arrow_first"?1:2;annotation_handles_.push_back({handle,screen(p),hit});}
                }painter.restore();
            }
        }
        // Traces are independent of model topology. One paper-space layout
        // supplies View/PDF strokes and collision-free upright end letters.
        for(const auto* view:views){
            using Point=zima::drawing::Point2;
            std::vector<std::pair<const zima::document::SectionDefinition*,zima::drawing::SectionTraceLayout>> traces;
            std::vector<std::array<Point,2>> obstacles;
            for(const auto& edge:view->projected_edges)if(zima::drawing::drawing_edge_visible(*view,edge))for(std::size_t i=1;i<edge.points.size();++i)
                obstacles.push_back({Point{edge.points[i-1].x*view->scale,edge.points[i-1].y*view->scale},Point{edge.points[i].x*view->scale,edge.points[i].y*view->scale}});
            for(const auto& section:view->section_markers)if(auto layout=zima::drawing::section_trace_layout(*view,section,views)){
                obstacles.insert(obstacles.end(),layout->chain.begin(),layout->chain.end());obstacles.insert(obstacles.end(),layout->accents.begin(),layout->accents.end());
                for(int end=0;end<2;++end){const auto tip=layout->arrow_tips[end],d=layout->arrow_directions[end];
                    obstacles.push_back({Point{tip.x-d.x*8,tip.y-d.y*8},tip});
                    obstacles.push_back({Point{tip.x-d.x*3-d.y*zima::viewer::annotation_arrow_half_width(3),tip.y-d.y*3+d.x*zima::viewer::annotation_arrow_half_width(3)},Point{tip.x-d.x*3+d.y*zima::viewer::annotation_arrow_half_width(3),tip.y-d.y*3-d.x*zima::viewer::annotation_arrow_half_width(3)}});
                }
                traces.emplace_back(&section,std::move(*layout));
            }
            const auto screen=[&](Point p){return QPointF(origin.x()+(sheet_->width_mm()-view->x+p.x)*zoom,origin.y()+(sheet_->height_mm()-view->y-p.y)*zoom);};
            for(const auto& [section,layout]:traces){
                const AnnotationKey trace_key{AnnotationKind::SectionEnd,view->id,section->id,0};
                const auto trace_color=annotation_color(trace_key,ink,printing);
                QPen thin(annotation_color(trace_key,printing?ink:QColor("#E6C85C"),printing),width(false));thin.setCapStyle(Qt::FlatCap);thin.setDashPattern({8*zoom/thin.widthF(),1.5*zoom/thin.widthF(),.5*zoom/thin.widthF(),1.5*zoom/thin.widthF()});painter.setPen(thin);
                for(const auto& line:layout.chain)painter.drawLine(screen(line[0]),screen(line[1]));
                QPen thick(trace_color,width(true));thick.setCapStyle(Qt::FlatCap);painter.setPen(thick);
                for(const auto& line:layout.accents)painter.drawLine(screen(line[0]),screen(line[1]));
                auto letter=QString::fromStdString(section->name);const auto separator=letter.indexOf(QRegularExpression("[-–—]"));if(separator>0)letter=letter.left(separator).trimmed();
                QFont font(drawing_font_family());font.setPixelSize(1000);const QFontMetricsF metrics(font);const auto bounds=metrics.tightBoundingRect(letter);const double text_scale=5/metrics.capHeight();
                const Point text_size{bounds.width()*text_scale,bounds.height()*text_scale};
                for(int end=0;end<2;++end){const auto tip=screen(layout.arrow_tips[end]);const auto d=layout.arrow_directions[end];const QPointF direction(d.x,-d.y);
                    const auto tail=tip-direction*8*zoom;painter.drawLine(tail,tip);
                    painter.save();painter.setPen(Qt::NoPen);painter.setBrush(trace_color);painter.drawPolygon(zima::viewer::annotation_arrow(tip,direction,3*zoom));painter.restore();
                    const auto position=zima::drawing::section_letter_position(layout,end,text_size,obstacles,.75+sheet_->thick_line_mm/2);
                    obstacles.push_back({Point{position.x-text_size.x/2,position.y-text_size.y/2},Point{position.x+text_size.x/2,position.y+text_size.y/2}});
                    painter.save();painter.setFont(font);painter.translate(screen(position));painter.scale(text_scale*zoom,text_scale*zoom);
                    // Only translation and positive uniform scale: letters never
                    // rotate with the trace, mirror or turn upside down.
                    painter.drawText(QPointF(-bounds.center().x(),-bounds.center().y()),letter);painter.restore();
                    if(!printing){
                        QPainterPath stroke;for(auto line:layout.chain){stroke.moveTo(screen(line[0]));stroke.lineTo(screen(line[1]));}stroke.moveTo(tail);stroke.lineTo(tip);
                        QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);const auto center=screen(position);hit.addRect(QRectF(center.x()-text_size.x*zoom/2,center.y()-text_size.y*zoom/2,text_size.x*zoom,text_size.y*zoom));
                        const auto& line=end?layout.chain.back():layout.chain.front();auto outward=zima::drawing::Point2{line[end?1:0].x-line[end?0:1].x,line[end?1:0].y-line[end?0:1].y};const auto n=std::hypot(outward.x,outward.y);outward.x/=n;outward.y/=n;
                        annotation_handles_.push_back({{AnnotationKind::SectionEnd,view->id,section->id,end},tip,hit,outward,layout.end_offsets[end],layout.minimum_offsets[end]});
                    }
                }
            }
        }
        for (const auto& dimension : sheet_->dimensions) {
            const AnnotationKey dimension_key{AnnotationKind::Dimension,dimension.view_id,dimension.id,0};
            const QColor dimension_color=annotation_color(dimension_key,printing?ink:dimension.unresolved?QColor("#C62828"):QColor("#FFD400"),printing);
            painter.setPen(QPen(dimension_color,!printing&&dimension.id==selected_dimension_id_?2.0:width(false)));
            const auto* view = [&]() -> const zima::drawing::DrawingView* {
                const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
                    [&](const auto& item) { return item.id == dimension.view_id; });
                return found == sheet_->views.end() ? nullptr : &*found;
            }();
            if (view == nullptr) continue;
            const auto screen = [&](const zima::drawing::Point2& point) {
                return QPointF(origin.x() + sheet_->width_mm()*zoom -
                                   (view->x - point.x * view->scale) * zoom,
                               origin.y() + sheet_->height_mm()*zoom -
                                   (view->y + point.y * view->scale) * zoom);
            };
            const QPointF first = screen(dimension.first_point);
            const QPointF second = screen(dimension.second_point);
            const QPointF label = screen(dimension.label_position);
            const double mx=dimension.second_point.x-dimension.first_point.x;
            const double my=dimension.second_point.y-dimension.first_point.y;
            const double measured_length=std::hypot(mx,my);
            if(measured_length<=1e-9) continue;
            const double nx=mx/measured_length,ny=my/measured_length;
            const auto on_dimension_line=[&](const zima::drawing::Point2& witness) {
                const double projection=(witness.x-dimension.label_position.x)*nx+
                                        (witness.y-dimension.label_position.y)*ny;
                return zima::drawing::Point2{dimension.label_position.x+projection*nx,
                                            dimension.label_position.y+projection*ny};
            };
            const QPointF line_first=screen(on_dimension_line(dimension.first_point));
            const QPointF line_second=screen(on_dimension_line(dimension.second_point));
            kernel::ViewerDimension source;
            source.witness_first={dimension.first_point.x,dimension.first_point.y,0};source.witness_second={dimension.second_point.x,dimension.second_point.y,0};
            const auto line_a=on_dimension_line(dimension.first_point),line_b=on_dimension_line(dimension.second_point);
            source.line_first={line_a.x,line_a.y,0};source.line_second={line_b.x,line_b.y,0};source.label_position=kernel::Vec3{dimension.label_position.x,dimension.label_position.y,0};
            const auto text=QString::number(dimension.measured_value,'f',3)+tr(" mm");
            const auto layout=viewer::dimension_presentation(source,[&](kernel::Vec3 p){return screen({p.x,p.y});},QFontMetricsF(painter.font()).horizontalAdvance(text),2.5*zoom,.75*zoom);
            if(!layout.valid)continue;
            QPainterPath stroke;
            for(const auto& curve:layout.curves){painter.drawPolyline(curve);stroke.moveTo(curve.front());for(qsizetype i=1;i<curve.size();++i)stroke.lineTo(curve[i]);}
            painter.setBrush(dimension_color);for(const auto& [tip,direction]:layout.arrows)painter.drawPolygon(viewer::annotation_arrow(tip,direction,2.5*zoom));
            QTransform text_transform;text_transform.translate(layout.text_baseline.x(),layout.text_baseline.y());text_transform.rotate(layout.text_angle);
            painter.save();painter.translate(layout.text_baseline);painter.rotate(layout.text_angle);painter.drawText(QPointF{},text);painter.restore();
            if(!printing){QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);hit.addRect(text_transform.mapRect(QFontMetricsF(painter.font()).boundingRect(text)));annotation_handles_.push_back({dimension_key,layout.handles[0],hit});}
        }
        if(!printing&&!preview_&&!dimension_mode_)for(const auto& handle:annotation_handles_){
            const bool selected=selected_annotation_&&*selected_annotation_==handle.key,hovered=hovered_annotation_&&*hovered_annotation_==handle.key;
            const bool movable=handle.key.kind==AnnotationKind::Caption||handle.key.kind==AnnotationKind::SectionLabel||handle.key.kind==AnnotationKind::SectionEnd||handle.key.kind==AnnotationKind::Dimension||
                (handle.key.kind==AnnotationKind::Model&&std::ranges::any_of(sheet_->views,[&](const auto& view){return view.id==handle.key.view&&std::ranges::any_of(view.model_annotations,[&](const auto& item){return item.kind==drawing::ModelAnnotationKind::Dimension&&model_annotation_key(item.source)==handle.key.id;});}));
            if(movable&&(selected||hovered))draw_handle(handle.point,selected);
        }
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (sheet_ == nullptr) return;
        if(event->button()==Qt::RightButton&&dragged_model_&&(event->buttons()&Qt::LeftButton)) {
            for(auto& view:sheet_->views)if(view.id==dragged_model_->key.view)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==dragged_model_->key.id&&item.model_dimension){
                kernel::cycle_dimension_presentation(model_drag_layout_initial_,item.model_dimension->kind);
                auto layout=item.view_layout.value_or(item.model_layout);layout.arrows_reversed=model_drag_layout_initial_.arrows_reversed;layout.radius_center_line_hidden=model_drag_layout_initial_.radius_center_line_hidden;item.view_layout=layout;model_moved_=true;
            }update();event->accept();return;
        }
        if ((event->buttons() & Qt::MiddleButton) &&
            (event->buttons() & Qt::RightButton)) {
            view_panning_ = true;
            view_pan_start_ = view_pan_;
            view_pan_pointer_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() != Qt::LeftButton) return;
        if (choose_view_) {
            const auto id=view_at(event->position());
            selected_=id;selected_annotation_.reset();selected_field_.clear();selected_dimension_id_.clear();
            if(selection_changed_)selection_changed_();
            if(!id.empty()){auto pick=std::move(choose_view_);choose_view_={};pick(id);}
            update();event->accept();return;
        }
        if (placed_) {
            position_preview(event->position());
            const auto value = *preview_;
            auto callback = std::move(placed_); placed_={}; position_={}; canceled_={};
            unsetCursor(); callback(value); event->accept(); return;
        }
        if(model_pick_){offer_annotations(event->position());selected_dimension_id_.clear();selected_field_.clear();selected_annotation_.reset();selected_.clear();
            if(!offered_annotations_.empty()){const auto key=offered_annotations_[offered_annotation_index_].key;selected_annotation_=key;selected_=key.view;model_pick_(key.id);}if(selection_changed_)selection_changed_();update();event->accept();return;}
        if (preview_) {
            if(preview_move_&&view_bounds(*preview_).adjusted(-5,-5,5,5).contains(event->position())) {
                preview_dragging_=true;preview_drag_moved_=false;preview_drag_start_=event->position();preview_drag_origin_={preview_->x,preview_->y};setFocus();
            }
            event->accept();return;
        }
        selected_field_=dimension_mode_?std::string{}:field_at(event->position());
        if(!selected_field_.empty()) {
            selected_annotation_.reset();selected_.clear();selected_dimension_id_.clear();drag_view_id_.clear();
            if(selection_changed_)selection_changed_();update();event->accept();return;
        }
        if(!dimension_mode_){
            offer_annotations(event->position());
            if(!offered_annotations_.empty()){
                const auto candidate=offered_annotations_[offered_annotation_index_];selected_annotation_=candidate.key;selected_field_.clear();selected_dimension_id_.clear();drag_view_id_.clear();dragged_label_.reset();dragged_dimension_id_.clear();
                selected_=candidate.key.kind==AnnotationKind::Dimension?std::string{}:candidate.key.view;
                if(candidate.key.kind==AnnotationKind::Dimension)selected_dimension_id_=candidate.key.id;
                if(QLineF(candidate.point,event->position()).length()<=8){
                    if(candidate.key.kind==AnnotationKind::Caption||candidate.key.kind==AnnotationKind::SectionLabel){
                        dragged_label_=std::pair{candidate.key.view,candidate.key.kind==AnnotationKind::SectionLabel};label_drag_start_=event->position();label_moved_=false;
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view){const auto origin=view_screen_point(view,{});label_position_start_={(candidate.point.x()-origin.x())/canvas_zoom(),(origin.y()-candidate.point.y())/canvas_zoom()};}
                    }else if(candidate.key.kind==AnnotationKind::Dimension){
                        for(const auto& dimension:sheet_->dimensions)if(dimension.id==candidate.key.id){dragged_dimension_id_=dimension.id;dimension_drag_start_=event->position();dimension_label_start_=dimension.label_position;}
                    }else if(candidate.key.kind==AnnotationKind::Model){
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view)for(const auto& item:view.model_annotations)if(model_annotation_key(item.source)==candidate.key.id&&item.kind==drawing::ModelAnnotationKind::Dimension){model_drag_original_=item;dragged_model_=candidate;model_drag_start_=event->position();model_moved_=false;model_drag_layout_initial_=item.view_layout.value_or(item.model_layout);model_drag_shown_=item.model_dimension?std::optional(kernel::layout_dimension(*item.model_dimension,item.model_envelope,model_drag_layout_initial_)):std::nullopt;}
                    }else{dragged_section_end_=candidate;section_end_drag_start_=event->position();section_end_moved_=false;}
                }
                if(selection_changed_)selection_changed_();update();event->accept();return;
            }
        }
        selected_annotation_.reset();
        const double zoom = canvas_zoom();
        const QPointF origin = canvas_origin(zoom);
        const auto segment_distance = [](QPointF point, QPointF a, QPointF b) {
            const QPointF delta = b - a;
            const double length_squared = delta.x() * delta.x() + delta.y() * delta.y();
            if (length_squared <= 1e-12) return std::hypot(point.x() - a.x(), point.y() - a.y());
            const double parameter = std::clamp(
                ((point.x() - a.x()) * delta.x() + (point.y() - a.y()) * delta.y()) /
                    length_squared, 0.0, 1.0);
            const QPointF nearest = a + parameter * delta;
            return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
        };
        setFocus();
        selected_dimension_id_.clear();
        double best = 8.0;
        std::string hit;
        const zima::drawing::DrawingView* hit_view{};
        const zima::drawing::ProjectedEdge* hit_edge{};
        if (dimension_mode_) for (const auto& view : sheet_->views) for (const auto& edge : view.projected_edges) {
            if(edge.points.size()<2||edge.hatch||!edge.source.valid()||edge.silhouette||!zima::drawing::drawing_edge_visible(view,edge))continue;
            for (std::size_t point = 1; point < edge.points.size(); ++point) {
                const auto screen = [&](const auto& value) {
                    return QPointF(origin.x() + sheet_->width_mm()*zoom -
                                       (view.x - value.x * view.scale) * zoom,
                                   origin.y() + sheet_->height_mm()*zoom -
                                       (view.y + value.y * view.scale) * zoom);
                };
                const double distance = segment_distance(event->position(),
                    screen(edge.points[point - 1]), screen(edge.points[point]));
                if (distance < best) { best = distance; hit = view.id; hit_view = &view; hit_edge = &edge; }
            }
        }
        if (dimension_mode_ && hit_view != nullptr && hit_edge != nullptr && hit_edge->points.size() >= 2) {
            if (!hit_edge->source.valid()) { update(); return; }
            if (!first_edge_) {
                first_edge_ = PickedEdge{hit_view->id, *hit_edge};
            } else if (first_edge_->view_id == hit_view->id) {
                const auto& a = first_edge_->edge.points;
                const auto& b = hit_edge->points;
                const zima::drawing::Point2 a_mid{(a.front().x + a.back().x) * 0.5,
                                                   (a.front().y + a.back().y) * 0.5};
                const zima::drawing::Point2 b_mid{(b.front().x + b.back().x) * 0.5,
                                                   (b.front().y + b.back().y) * 0.5};
                const double dx = a.back().x - a.front().x;
                const double dy = a.back().y - a.front().y;
                const double length = std::hypot(dx, dy);
                const double bx = b.back().x - b.front().x;
                const double by = b.back().y - b.front().y;
                const double b_length = std::hypot(bx, by);
                if (length > 1e-9 && b_length > 1e-9 &&
                    std::abs(dx * by - dy * bx) <= 1e-5 * length * b_length) {
                    zima::drawing::LinearDimension dimension;
                    std::ostringstream id;
                    id << "dimension-" << std::chrono::steady_clock::now().time_since_epoch().count();
                    dimension.id = id.str(); dimension.view_id = hit_view->id;
                    dimension.first = first_edge_->edge.source; dimension.second = hit_edge->source;
                    dimension.first_point = a_mid; dimension.second_point = b_mid;
                    dimension.label_position = {(a_mid.x + b_mid.x) * 0.5,
                                                (a_mid.y + b_mid.y) * 0.5};
                    dimension.measured_value = std::abs((b_mid.x - a_mid.x) * -dy / length +
                                                        (b_mid.y - a_mid.y) * dx / length);
                    sheet_->dimensions.push_back(std::move(dimension));
                    dimension_mode_ = false; first_edge_.reset();
                    if(changed_) changed_();
                }
            }
            update(); return;
        }
        selected_ = dimension_mode_ ? std::move(hit) : view_at(event->position());
        drag_view_id_.clear();
        if (!selected_.empty()) {
            drag_view_id_ = selected_; drag_start_ = event->position();
            if (const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
                    [&](const auto& view) { return view.id == drag_view_id_; });
                found != sheet_->views.end()) drag_origin_ = {found->x, found->y};
        }
        if (selection_changed_) selection_changed_();
        update();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (placed_ && !view_panning_) { position_preview(event->position()); return; }
        if(choose_view_ && !view_panning_) {
            hovered_annotation_.reset();offered_annotations_.clear();hovered_field_.clear();
            hovered_=view_at(event->position());update();event->accept();return;
        }
        if ((!preview_||model_pick_) && !dimension_mode_) {
            const auto field=model_pick_?std::string{}:field_at(event->position());
            const auto previous=hovered_annotation_;if(field.empty())offer_annotations(event->position());else{hovered_annotation_.reset();offered_annotations_.clear();}
            const auto hit=!model_pick_&&field.empty()&&!hovered_annotation_?view_at(event->position()):std::string{};
            if(previous!=hovered_annotation_)update();
            if(hit!=hovered_ || field!=hovered_field_) { hovered_=hit;hovered_field_=field;update(); }
        }
        if(sheet_==nullptr) return;
        if (view_panning_) {
            if ((event->buttons() & Qt::MiddleButton) &&
                (event->buttons() & Qt::RightButton)) {
                view_pan_ = view_pan_start_ + event->position() - view_pan_pointer_;
                update();
                event->accept();
                return;
            }
            view_panning_ = false;
            unsetCursor();
        }
        const double zoom = canvas_zoom();
        if(preview_dragging_&&preview_&&preview_move_&&(event->buttons()&Qt::LeftButton)) {
            const auto delta=event->position()-preview_drag_start_;
            if(!preview_drag_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            preview_drag_moved_=true;
            const auto position=constrained_view_position(*preview_,preview_drag_origin_,
                {preview_drag_origin_.x-delta.x()/zoom,preview_drag_origin_.y-delta.y()/zoom});
            preview_move_(position);event->accept();return;
        }
        if(dragged_model_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-model_drag_start_;if(!model_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_model_->key.view)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==dragged_model_->key.id){
                const auto origin=view_screen_point(view,{});const auto point=dragged_model_->point+delta;drawing::Point2 p{(point.x()-origin.x())/zoom,(origin.y()-point.y())/zoom};
                item.handle_camera_horizontal={view.camera.horizontal.x,view.camera.horizontal.y,view.camera.horizontal.z};
                item.handle_camera_vertical={view.camera.vertical.x,view.camera.vertical.y,view.camera.vertical.z};
                const auto key=dragged_model_->key.end==0?"text":dragged_model_->key.end==1?"arrow_first":"arrow_second";
                const auto projected=drawing::project_model_annotation(view,item);
                const bool guide_parallel=[&]{
                    if(!model_drag_shown_||!item.model_envelope.valid||projected.dimension_kind!=kernel::ViewerDimensionKind::Linear)return false;
                    const auto& shown=*model_drag_shown_;const auto along=kernel::dimension_unit(kernel::dimension_sub(shown.witness_second,shown.witness_first));const auto side=kernel::dimension_unit(kernel::dimension_cross(shown.plane_normal,along));
                    const auto aligned=[&](auto v){return std::ranges::any_of(item.model_envelope.axes,[&](auto axis){return std::abs(kernel::dimension_dot(v,axis))>1-1e-6;});};return aligned(along)&&aligned(side);
                }();
                if(item.model_dimension&&model_drag_shown_) {
                    const auto& shown=*model_drag_shown_;const bool angular=shown.kind==kernel::ViewerDimensionKind::Angular;
                    const auto u=kernel::dimension_unit(kernel::dimension_sub(angular?shown.line_first:shown.witness_second,shown.witness_first));
                    const auto v=kernel::dimension_unit(kernel::dimension_cross(shown.plane_normal,u));
                    const QPointF a(kernel::dimension_dot(u,view.camera.horizontal),kernel::dimension_dot(u,view.camera.vertical)),b(kernel::dimension_dot(v,view.camera.horizontal),kernel::dimension_dot(v,view.camera.vertical));
                    const QPointF move(delta.x()/(zoom*view.scale),-delta.y()/(zoom*view.scale));const double det=a.x()*b.y()-a.y()*b.x();
                    if(std::abs(det)>1e-9){const double along=(move.x()*b.y()-move.y()*b.x())/det,outward=(a.x()*move.y()-a.y()*move.x())/det;
                        item.view_layout=kernel::dragged_dimension_layout(shown,item.model_envelope,model_drag_layout_initial_,dragged_model_->key.end,along,outward);if(view.show_dimension_guides&&guide_parallel&&dragged_model_->key.end!=0&&item.view_layout->envelope_offset){
                            const double paper=*item.view_layout->envelope_offset*view.scale;
                            const double snapped=view.dimension_guide_offset+std::max(0.,std::round((paper-view.dimension_guide_offset)/view.dimension_guide_spacing))*view.dimension_guide_spacing;
                            if(std::abs(snapped-paper)*zoom<5)item.view_layout->envelope_offset=snapped/view.scale;
                        }
                        if(dragged_model_->key.end==0){
                            const auto label=shown.label_position.value_or(kernel::dimension_scale(kernel::dimension_add(shown.line_first,shown.line_second),.5));
                            const QPointF original((dragged_model_->point.x()-origin.x())/(zoom*view.scale),(origin.y()-dragged_model_->point.y())/(zoom*view.scale));
                            const auto correction=original-QPointF(kernel::dimension_dot(label,view.camera.horizontal),kernel::dimension_dot(label,view.camera.vertical));
                            item.view_layout->text_along+=(correction.x()*b.y()-correction.y()*b.x())/det;
                            item.view_layout->text_outward+=(a.x()*correction.y()-a.y()*correction.x())/det;
                        }
                        item.paper_handles.clear();model_moved_=true;}
                    continue;
                }
                if(view.show_dimension_guides&&guide_parallel){const auto bounds=view_bounds_at(view,zoom,canvas_origin(zoom));double best_x=5/zoom,best_y=5/zoom;const auto near=[&](double value,double& coordinate,double& best){const auto distance=std::abs(value-coordinate);if(distance<best){best=distance;coordinate=value;}};
                    for(int i=0;i<4;++i){const auto offset=view.dimension_guide_offset+i*view.dimension_guide_spacing;near((bounds.left()-origin.x())/zoom-offset,p.x,best_x);near((bounds.right()-origin.x())/zoom+offset,p.x,best_x);near((origin.y()-bounds.top())/zoom+offset,p.y,best_y);near((origin.y()-bounds.bottom())/zoom-offset,p.y,best_y);}}
                if(dragged_model_->key.end!=0&&item.dimension_kind==kernel::ViewerDimensionKind::Linear&&item.curves.size()>=3&&item.curves[1].size()>=2){auto a=item.curves[1].front(),b=item.curves[1].back();a={a.x*view.scale,a.y*view.scale};b={b.x*view.scale,b.y*view.scale};const auto length=std::hypot(b.x-a.x,b.y-a.y);if(length>1e-9){const double nx=-(b.y-a.y)/length,ny=(b.x-a.x)/length,offset=(p.x-a.x)*nx+(p.y-a.y)*ny;item.paper_handles["arrow_first"]={a.x+nx*offset,a.y+ny*offset};item.paper_handles["arrow_second"]={b.x+nx*offset,b.y+ny*offset};}}
                else {if(item.dimension_kind==kernel::ViewerDimensionKind::Angular&&dragged_model_->key.end!=0)item.paper_handles.erase(dragged_model_->key.end==1?"arrow_second":"arrow_first");item.paper_handles[key]=p;}model_moved_=true;
            }update();return;
        }
        if(dragged_section_end_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-section_end_drag_start_;if(!section_end_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_section_end_->key.view){const auto& end=*dragged_section_end_;view.section_marker_offsets[end.key.id][end.key.end]=std::max(end.minimum,end.offset+(delta.x()*end.direction.x-delta.y()*end.direction.y)/zoom);section_end_moved_=true;}update();return;
        }
        if(dragged_label_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-label_drag_start_;
            if(!label_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_label_->first){
                auto& position=dragged_label_->second?view.section_label_position:view.caption_position;
                position=zima::drawing::Point2{label_position_start_.x+delta.x()/zoom,label_position_start_.y-delta.y()/zoom};label_moved_=true;
            }update();return;
        }
        if(!dragged_dimension_id_.empty() && (event->buttons()&Qt::LeftButton)) {
            const auto dimension=std::find_if(sheet_->dimensions.begin(),sheet_->dimensions.end(),
                [&](const auto& item){return item.id==dragged_dimension_id_;});
            if(dimension==sheet_->dimensions.end()) return;
            const auto view=std::find_if(sheet_->views.begin(),sheet_->views.end(),
                [&](const auto& item){return item.id==dimension->view_id;});
            if(view==sheet_->views.end() || zoom<=0.0 || view->scale<=0.0) return;
            dimension->label_position={dimension_label_start_.x+
                (event->position().x()-dimension_drag_start_.x())/(zoom*view->scale),
                dimension_label_start_.y-
                (event->position().y()-dimension_drag_start_.y())/(zoom*view->scale)};
            update(); return;
        }
        if(drag_view_id_.empty() || !(event->buttons()&Qt::LeftButton)) return;
        const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
            [&](const auto& view) { return view.id == drag_view_id_; });
        if (found == sheet_->views.end() || zoom <= 0.0) return;
        double next_x = drag_origin_.x - (event->position().x() - drag_start_.x()) / zoom;
        double next_y = drag_origin_.y - (event->position().y() - drag_start_.y()) / zoom;
        const auto constrained=constrained_view_position(*found,drag_origin_,{next_x,next_y});
        next_x=constrained.x;next_y=constrained.y;
        const double dx = next_x - found->x; const double dy = next_y - found->y;
        found->x = next_x; found->y = next_y;
        std::function<void(const std::string&)> move_children = [&](const std::string& parent) {
            for (auto& view : sheet_->views) if (view.parent_view_id == parent) {
                view.x += dx; view.y += dy; move_children(view.id);
            }
        };
        move_children(found->id);
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(dragged_model_&&event->button()==Qt::RightButton){event->accept();return;}
        if(preview_dragging_&&event->button()==Qt::LeftButton){preview_dragging_=false;event->accept();return;}
        if (view_panning_ &&
            (!(event->buttons() & Qt::MiddleButton) ||
             !(event->buttons() & Qt::RightButton))) {
            view_panning_ = false;
            unsetCursor();
            event->accept();
        }
        const bool label_changed=label_moved_||section_end_moved_||model_moved_;dragged_model_.reset();model_moved_=false;dragged_label_.reset();label_moved_=false;dragged_section_end_.reset();section_end_moved_=false;
        if((!drag_view_id_.empty() || !dragged_dimension_id_.empty() || label_changed) && changed_) changed_();
        drag_view_id_.clear();
        dragged_dimension_id_.clear();
    }
    void wheelEvent(QWheelEvent* event) override {
        if (sheet_ == nullptr || event->angleDelta().y() == 0) {
            QWidget::wheelEvent(event);
            return;
        }
        const double old_zoom = canvas_zoom();
        const QPointF old_origin = canvas_origin(old_zoom);
        const QPointF local = (event->position() - old_origin) / old_zoom;
        const double factor = std::pow(1.0 / 1.15,
            static_cast<double>(event->angleDelta().y()) / 120.0);
        view_zoom_ = std::clamp(view_zoom_ * factor, 0.05, 100.0);
        const double new_zoom = canvas_zoom();
        const QPointF centered((width() - sheet_->width_mm() * new_zoom) * 0.5,
            (height() - sheet_->height_mm() * new_zoom) * 0.5);
        view_pan_ = event->position() - local * new_zoom - centered;
        update();
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape) {
            if(dragged_model_&&model_drag_original_&&sheet_){for(auto& view:sheet_->views)if(view.id==dragged_model_->key.view)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==dragged_model_->key.id)item=*model_drag_original_;dragged_model_.reset();model_drag_original_.reset();model_moved_=false;update();event->accept();return;}
            if (choose_view_) {choose_view_={};update();event->accept();return;}
            if (placed_) { cancel_placement(); event->accept(); return; }
            if(dimension_mode_ && first_edge_) first_edge_.reset();
            else if(dimension_mode_) dimension_mode_=false;
            else { selected_dimension_id_.clear(); selected_.clear();selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear(); }
            if (selection_changed_) selection_changed_();
            update(); event->accept(); return;
        }
        if(event->key()==Qt::Key_Delete && sheet_!=nullptr &&
           !selected_dimension_id_.empty()) {
            std::erase_if(sheet_->dimensions,[&](const auto& dimension) {
                return dimension.id==selected_dimension_id_; });
            selected_dimension_id_.clear(); if(changed_) changed_(); update();
            if (selection_changed_) selection_changed_();
            event->accept(); return;
        }
        QWidget::keyPressEvent(event);
    }
private:
    zima::drawing::DrawingSheet* sheet_{};
    struct ShadedCache {double resolution{};QRectF bounds;const zima::drawing::ProjectedTriangle* triangles{};QImage image;};
    std::map<std::string,ShadedCache> shaded_cache_;
    bool lineweights_{};
    std::string selected_;
    std::string hovered_;
    std::string selected_field_,hovered_field_;
    std::vector<std::pair<std::string,QPolygonF>> field_regions_;
    QAction* title_action_{};
    std::optional<zima::drawing::DrawingView> preview_;
    std::function<void(zima::drawing::Point2)> preview_move_;
    bool preview_dragging_{},preview_drag_moved_{};
    QPointF preview_drag_start_;
    zima::drawing::Point2 preview_drag_origin_;
    zima::drawing::Point2 constrained_view_position(const zima::drawing::DrawingView& view,
        zima::drawing::Point2 origin,zima::drawing::Point2 next)const {
        if(view.value_locks.contains("x"))next.x=origin.x;
        if(view.value_locks.contains("y"))next.y=origin.y;
        if(sheet_&&!view.parent_view_id.empty()&&view.projection_direction!=zima::drawing::ProjectionDirection::None) {
            const auto parent=std::ranges::find(sheet_->views,view.parent_view_id,&zima::drawing::DrawingView::id);
            if(parent!=sheet_->views.end()) {
                const auto ray=projection_placement(view.projection_direction,1.0);
                if((view.value_locks.contains("x")&&std::abs(ray.x)>1e-9)||(view.value_locks.contains("y")&&std::abs(ray.y)>1e-9))return origin;
                const double distance=(next.x-parent->x)*ray.x+(next.y-parent->y)*ray.y;
                next={parent->x+distance*ray.x,parent->y+distance*ray.y};
            }
        }
        return next;
    }
    std::function<void(zima::drawing::DrawingView)> placed_;
    std::function<void()> canceled_;
    std::function<void(zima::drawing::DrawingView&, zima::drawing::Point2)> position_;
    QAction *insert_action_{}, *edit_action_{}, *projected_action_{}, *remove_action_{};
    struct PickedEdge { std::string view_id; zima::drawing::ProjectedEdge edge; };
    std::optional<PickedEdge> first_edge_;
    bool dimension_mode_{};
    std::string drag_view_id_;
    QPointF drag_start_;
    zima::drawing::Point2 drag_origin_;
    kernel::DimensionLayout model_drag_layout_initial_;
    std::optional<kernel::ViewerDimension> model_drag_shown_;
    std::function<void(const std::string&,const std::string&)> dimension_properties_;
    std::function<void()> changed_;
    std::function<void()> selection_changed_;
    std::string selected_dimension_id_;
    std::string dragged_dimension_id_;
    std::optional<std::pair<std::string,bool>> dragged_label_;
    QPointF label_drag_start_;
    zima::drawing::Point2 label_position_start_;
    bool label_moved_{};
    QPointF dimension_drag_start_;
    zima::drawing::Point2 dimension_label_start_;
    std::optional<zima::drawing::TitleBlockContext> title_block_context_;
    double view_zoom_{1.0};
    QPointF view_pan_;
    bool view_panning_{};
    QPointF view_pan_start_;
    QPointF view_pan_pointer_;
};

DrawingWindow::DrawingWindow(
    zima::workspace::Workspace* workspace, bool create_initial_document)
    : workspace_(workspace) {
    setWindowTitle(tr("ZIMA-CAD – Výkres")); resize(1180, 760);
    formats_directory_ = ApplicationSettings::load().resolved_paths.value("Formats");
    create_actions(); create_layout();
    if(create_initial_document) new_document();
}

void DrawingWindow::set_formats_directory(const QString& directory) {
    formats_directory_ = directory;
}

void DrawingWindow::create_actions() {
    auto* file = menuBar()->addMenu(tr("Soubor"));
    file->addAction(tr("Nový výkres"), this, [this] { new_document(); });
    file->addAction(tr("Otevřít výkres…"), this, [this] { open_document(); });
    save_action_ = file->addAction(tr("Uložit výkres…"), this,
        [this] { save_document(); });
    save_action_->setObjectName("drawingSaveAction");
    auto* drawing = menuBar()->addMenu(tr("Výkres"));
    auto* pdf=drawing->addAction(tr("Uložit jako PDF…"),this,[this]{save_pdf();});
    pdf->setObjectName("exportDrawingPdfAction");
    add_sheet_action_ = drawing->addAction(tr("Přidat list"), this,
        [this] { add_sheet(); });
    add_sheet_action_->setObjectName("addDrawingSheetAction");
    remove_sheet_action_ = drawing->addAction(tr("Odstranit list"), this,
        [this] { remove_sheet(); });
    remove_sheet_action_->setObjectName("removeDrawingSheetAction");
    edit_sheet_action_ = drawing->addAction(tr("Vlastnosti listu…"), this,
        [this] { edit_sheet(); });
    edit_sheet_action_->setObjectName("editDrawingSheetAction");
    drawing->addAction(tr("Načíst formát…"), this, [this] { load_frame(); });
    auto* remove_frame_action = drawing->addAction(
        tr("Odstranit formát"), this, [this] { remove_frame(); });
    remove_frame_action->setObjectName("removeDrawingFrameAction");
    drawing->addAction(tr("Načíst razítko…"), this, [this] { load_title_block(); });
    auto* remove_title_block_action = drawing->addAction(
        tr("Odstranit razítko"), this, [this] { remove_title_block(); });
    remove_title_block_action->setObjectName("removeDrawingTitleBlockAction");
    edit_title_block_action_ = drawing->addAction(tr("Hodnoty razítka…"), this,
        [this] { edit_title_block(); });
    edit_title_block_action_->setObjectName("editDrawingTitleBlockAction");
    drawing->addSeparator();
    insert_view_action_ = drawing->addAction(tr("Vložit pohled…"), this,
        [this] { insert_view(); });
    insert_view_action_->setObjectName("insertDrawingViewAction");
    projected_view_action_ = drawing->addAction(
        tr("Projekční pohled"), this,
        [this] { create_projected_view(); });
    projected_view_action_->setObjectName("projectDrawingViewAction");
    edit_view_action_ = drawing->addAction(tr("Vlastnosti pohledu…"), this,
        [this] { edit_selected_view(); });
    edit_view_action_->setObjectName("editDrawingViewAction");
    show_erase_action_=drawing->addAction(tr("Show / Erase…"),this,[this]{show_erase();});
    show_erase_action_->setObjectName("drawingShowEraseAction");
    show_erase_action_->setIcon(resource_icon("show-erase"));
    regenerate_view_action_ = drawing->addAction(tr("Regenerovat"), this,
        [this] { regenerate_selected_view(); });
    regenerate_view_action_->setObjectName("regenerateDrawingViewAction");
    delete_view_action_ = drawing->addAction(tr("Odstranit pohled"), this,
        [this] { delete_selected_view(); });
    delete_view_action_->setObjectName("deleteDrawingViewAction");
    linear_dimension_action_ = drawing->addAction(tr("Lineární kóta"), this,
        [this] { start_linear_dimension(); });
    linear_dimension_action_->setObjectName("drawingDimensionAction");
    linear_dimension_action_->setCheckable(true);
    selection_action_ = new QAction(tr("Výběr"), this);
    selection_action_->setObjectName("drawingSelectionAction");
    selection_action_->setCheckable(true);
    connect(selection_action_, &QAction::triggered, this,
        [this] { start_selection(); });

    drawing_toolbar_ = new QToolBar(tr("Výkres"), this);
    drawing_toolbar_->setObjectName("drawingToolbar");
    drawing_toolbar_->setMovable(false);
    drawing_toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Keep the application toolbar contract identical to the Python UI:
    // Selection, separator, Insert view, Dimension. Less frequent view/sheet
    // operations remain in the Drawing menu and context workflow.
    drawing_toolbar_->addAction(selection_action_);
    drawing_toolbar_->addSeparator();
    drawing_toolbar_->addAction(insert_view_action_);
    drawing_toolbar_->addAction(show_erase_action_);
    drawing_toolbar_->addAction(linear_dimension_action_);
    addToolBar(Qt::TopToolBarArea, drawing_toolbar_);
}

void DrawingWindow::create_layout() {
    auto* central = new QWidget(this); auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    sheets_ = new QTabBar(central); sheets_->setObjectName("drawingSheetTabs");
    sheets_->setExpanding(false);
    canvas_ = new DrawingCanvas(central); canvas_->setObjectName("drawingCanvas");
    state_ = new QLabel(central); state_->setObjectName("drawingState");
    canvas_->set_dimension_properties_callback([this](const auto& view,const auto& key){edit_model_dimension(view,key);});
    canvas_->set_changed_callback([this] {
        sync_workspace_document();
        update_action_states();
    });
    canvas_->set_selection_changed_callback([this] {
        update_action_states();
        if (selection_handler_) selection_handler_(canvas_->selected_view_id());
        const auto identifier = document_.dimension_identifiers.identifier(
            document_.document_id, "dimension:" + canvas_->selected_dimension_id());
        if (!identifier.empty()) set_status_message(tr("Kóta %1").arg(QString::fromStdString(identifier)));
    });
    canvas_->set_context_actions(insert_view_action_,edit_view_action_,projected_view_action_,delete_view_action_);
    canvas_->set_title_block_action(edit_title_block_action_);
    sheet_controls_ = new QWidget(central);
    auto* bottom = new QHBoxLayout(sheet_controls_);
    bottom->setContentsMargins(6, 3, 6, 3);
    auto* remove_sheet = new QPushButton(QStringLiteral("−"), central);
    auto* add_sheet = new QPushButton(QStringLiteral("+"), central);
    remove_sheet->setObjectName("drawingRemoveSheetButton");
    add_sheet->setObjectName("drawingAddSheetButton");
    remove_sheet->setFixedWidth(34); add_sheet->setFixedWidth(34);
    connect(remove_sheet, &QPushButton::clicked, this, [this] { this->remove_sheet(); });
    connect(add_sheet, &QPushButton::clicked, this, [this] { this->add_sheet(); });
    lineweight_mode_ = new QComboBox(central);
    lineweight_mode_->setObjectName("drawingLineweightMode");
    lineweight_mode_->addItem(tr("Tenké čáry"));
    lineweight_mode_->addItem(tr("Náhled tlouštěk"));
    connect(lineweight_mode_,&QComboBox::currentIndexChanged,this,[this](int index){canvas_->set_lineweights(index==1);});
    scale_numerator_ = new QDoubleSpinBox(central);
    scale_denominator_ = new QDoubleSpinBox(central);
    for (auto* spin : {scale_numerator_, scale_denominator_}) {
        spin->setRange(1.0, 1000.0); spin->setDecimals(0); spin->setValue(1.0);
    }
    sheet_format_ = new QComboBox(central);
    sheet_format_->setObjectName("drawingFormatCombo");
    for (const auto* format : {"A4", "A3", "A2", "A1", "A0"})
        sheet_format_->addItem(QString::fromLatin1(format));
    auto* add_format = new QPushButton(tr("Přidat formát"), central);
    add_format->setObjectName("drawingAddFormatButton");
    auto* remove_format = new QPushButton(tr("Odebrat formát"), central);
    auto* add_title = new QPushButton(tr("Přidat razítko"), central);
    add_title->setObjectName("drawingAddTitleBlockButton");
    auto* remove_title = new QPushButton(tr("Odebrat razítko"), central);
    projection_method_ = new QComboBox(central);
    projection_method_->setObjectName("drawingProjectionMethodCombo");
    projection_method_->addItem(tr("První kvadrant"));
    projection_method_->addItem(tr("Třetí kvadrant"));
    source_variant_ = new QComboBox(central);
    source_variant_->setObjectName("drawingSourceVariant");
    source_variant_->setMinimumContentsLength(14);
    source_variant_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    source_variant_->setToolTip(tr("Zdrojový díl nebo sestava. Varianty z Family Table budou doplněny později."));
    bottom->addWidget(new QLabel(tr("Varianta:"), central));
    bottom->addWidget(source_variant_);
    bottom->addWidget(sheets_, 1); bottom->addWidget(remove_sheet);
    bottom->addWidget(add_sheet); bottom->addSpacing(16);
    bottom->addWidget(new QLabel(tr("Tloušťky:"), central));
    bottom->addWidget(lineweight_mode_);
    auto* pdf_button=new QPushButton(tr("PDF…"),central);pdf_button->setObjectName("drawingExportPdf");
    connect(pdf_button,&QPushButton::clicked,this,[this]{save_pdf();});bottom->addWidget(pdf_button);
    bottom->addWidget(new QLabel(tr("Měřítko:"), central));
    bottom->addWidget(scale_numerator_); bottom->addWidget(new QLabel(":"));
    bottom->addWidget(scale_denominator_);
    bottom->addWidget(new QLabel(tr("Formát:"), central));
    bottom->addWidget(sheet_format_); bottom->addWidget(add_format);
    bottom->addWidget(remove_format); bottom->addWidget(add_title);
    bottom->addWidget(remove_title);
    bottom->addWidget(new QLabel(tr("Promítání:"), central));
    bottom->addWidget(projection_method_);
    connect(add_format, &QPushButton::clicked, this, [this] { load_frame(); });
    connect(remove_format, &QPushButton::clicked, this, [this] { remove_frame(); });
    connect(add_title, &QPushButton::clicked, this, [this] { load_title_block(); });
    connect(remove_title, &QPushButton::clicked, this, [this] { remove_title_block(); });
    connect(sheet_format_, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto* sheet = active_sheet(); if (sheet == nullptr || index < 0) return;
        const auto format = static_cast<zima::drawing::SheetFormat>(index);
        if (sheet->format == format) return;
        sheet->format = format; sheet->frame_lines.clear(); sheet->frame_texts.clear(); sheet->frame_circles.clear();
        sheet->title_block_lines.clear(); sheet->title_block_texts.clear();
        sheet->title_block_fields.clear();sheet->title_block_images.clear();sheet->title_block_circles.clear();sheet->repeat_regions.clear(); refresh();
    });
    const auto change_scale = [this] {
        auto* sheet = active_sheet(); if (sheet == nullptr) return;
        auto next=*sheet;next.default_scale=scale_numerator_->value()/scale_denominator_->value();
        try{for(auto& view:next.views)if(view.use_sheet_scale&&view.scale!=next.default_scale){view.scale=next.default_scale;if(!view.section_id.empty()){
            auto path=view.source_path;if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
            auto source=load_drawing_source(path,workspace_,view.source_document_id);zima::drawing::refresh_view_geometry(view,source.second);
        }}*sheet=std::move(next);canvas_->update();sync_workspace_document();}
        catch(const std::exception& e){set_status_message(QString::fromUtf8(e.what()));refresh();}
    };
    connect(scale_numerator_, &QDoubleSpinBox::valueChanged, this,
        [change_scale](double) { change_scale(); });
    connect(scale_denominator_, &QDoubleSpinBox::valueChanged, this,
        [change_scale](double) { change_scale(); });
    connect(projection_method_, &QComboBox::currentIndexChanged, this,
        [this](int index) {
            auto* sheet = active_sheet(); if (sheet == nullptr || index < 0) return;
            sheet->projection_method = index == 0
                ? zima::drawing::ProjectionMethod::FirstAngle
                : zima::drawing::ProjectionMethod::ThirdAngle;
            sync_workspace_document();
        });
    layout->addWidget(canvas_, 1); layout->addWidget(sheet_controls_); layout->addWidget(state_);
    setCentralWidget(central);
    connect(sheets_, &QTabBar::currentChanged, this, [this] { refresh(); });
}

void DrawingWindow::set_status_handler(std::function<void(const QString&)> handler) {
    status_handler_ = std::move(handler);
    state_->setVisible(!status_handler_);
    if (status_handler_) status_handler_(state_->text());
}

void DrawingWindow::set_status_message(const QString& message) {
    state_->setText(message);
    if (status_handler_) status_handler_(message);
}

void DrawingWindow::new_document() {
    document_ = zima::drawing::DrawingDocument::create_default(); path_.clear();
    workspace_document_id_.clear();
    if(workspace_!=nullptr) {
        workspace_->add_drawing(document_); workspace_document_id_=document_.document_id;
        workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
    }
    refresh();
}
void DrawingWindow::edit_workspace_document(const std::string& document_id) {
    if(workspace_==nullptr) return;
    auto* state=workspace_->open_drawing(document_id); if(state==nullptr) return;
    if (workspace_document_id_ == document_id && canvas_->interacting()) return;
    if (view_dialog_) view_dialog_->reject();
    canvas_->cancel_placement();
    workspace_document_id_=document_id; document_=state->document; path_=state->path; refresh();
}
void DrawingWindow::select_view(const std::string& view_id) {
    if (view_dialog_) return;
    for (std::size_t i=0;i<document_.sheets.size();++i)
        if (std::any_of(document_.sheets[i].views.begin(),document_.sheets[i].views.end(),
            [&](const auto& view){return view.id==view_id;})) {
            if (sheets_->currentIndex()!=static_cast<int>(i)) sheets_->setCurrentIndex(static_cast<int>(i));
            break;
        }
    canvas_->select_view_for_test(view_id);
}
void DrawingWindow::select_view_for_test(const std::string& view_id) { select_view(view_id); }
std::optional<QPointF> DrawingWindow::view_label_center_for_test(const std::string& id,bool section)const{return canvas_->label_center(id,section);}
std::optional<QPointF> DrawingWindow::annotation_handle_for_test(const std::string& id,int end,bool dimension)const{return canvas_->annotation_point(id,end,dimension);}
void DrawingWindow::load_frame_for_test(const std::filesystem::path& path) {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    zima::drawing::load_frame_template(*sheet, path); refresh();
}
std::optional<QPointF> DrawingWindow::view_rectangle_center_for_test(const std::string& id)const {
    return canvas_->rectangle_center(id);
}
std::optional<QPointF> DrawingWindow::title_field_center_for_test(const std::string& id) const {
    return canvas_->title_field_center(id);
}
void DrawingWindow::load_title_block_for_test(const std::filesystem::path& path) {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    zima::drawing::load_title_block_template(*sheet, path); refresh();
}
void DrawingWindow::open_document() {
    const auto path = open_file(this, tr("Otevřít výkres"), {}, tr("Výkres ZIMA-CAD (*.drwz)"));
    if (path.isEmpty()) return;
    try {
        document_ = zima::drawing::DrawingDocument::load(path.toStdString()); path_ = path.toStdString();
        workspace_document_id_.clear();
        if(workspace_!=nullptr) {
            if(auto* existing=workspace_->open_drawing(document_.document_id)) {
                existing->document=document_; existing->path=path_; }
            else workspace_->add_drawing(document_,path_);
            workspace_document_id_=document_.document_id;
            workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
        }
        refresh();
    }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze otevřít výkres"), error.what()); }
}
void DrawingWindow::save_pdf() {
    auto suggested=path_.empty()?std::filesystem::path(document_.name+".pdf"):path_;
    suggested.replace_extension(".pdf");
    const auto path=save_file(this,tr("Uložit jako PDF"),QString::fromStdString(suggested.string()),tr("PDF (*.pdf)"),"pdf");
    if(path.isEmpty())return;
    try {export_pdf(path.toStdString());set_status_message(tr("PDF uloženo. Tiskněte ve skutečné velikosti (100 %)."));}
    catch(const std::exception& error){set_status_message(QString::fromUtf8(error.what()));}
}
QRectF DrawingWindow::sheet_rectangle_for_test()const{return canvas_->sheet_rectangle();}
void DrawingWindow::export_dxf(const std::filesystem::path& path) {
    auto* sheet=active_sheet();
    if(!sheet) throw std::runtime_error("Drawing has no active sheet");
    DrawingDxfDevice device(sheet->width_mm(),sheet->height_mm());
    QPainter painter(&device);
    canvas_->paint_sheet(painter,1,{},true);
    if(!painter.end())throw std::runtime_error("Cannot finish DXF output");
    QSaveFile file(QString::fromStdString(path.string()));
    if(!file.open(QIODevice::WriteOnly))throw std::runtime_error(file.errorString().toStdString());
    const auto data=device.data();
    if(file.write(data)!=data.size() || !file.commit())throw std::runtime_error("Cannot write DXF output");
}
void DrawingWindow::export_jpg(const std::filesystem::path& path) {
    const auto image=canvas_->grab().toImage();
    QSaveFile file(QString::fromStdString(path.string()));
    if(image.isNull() || !file.open(QIODevice::WriteOnly) ||
       !image.save(&file,"JPG",95) || !file.commit())
        throw std::runtime_error("Cannot save current drawing view as JPG");
}
void DrawingWindow::export_pdf(const std::filesystem::path& path) {
    if(document_.sheets.empty())throw std::runtime_error("Drawing has no sheets");
    QSaveFile file(QString::fromStdString(path.string()));
    if(!file.open(QIODevice::WriteOnly))throw std::runtime_error(file.errorString().toStdString());
    {
        QPdfWriter writer(&file);writer.setResolution(720);writer.setTitle(QString::fromStdString(document_.name));writer.setCreator("ZIMA-CAD");
        QPainter painter;
        for(std::size_t index=0;index<document_.sheets.size();++index) {
            auto& sheet=document_.sheets[index];
            writer.setPageSize(QPageSize(QSizeF(sheet.width_mm(),sheet.height_mm()),QPageSize::Millimeter));
            writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Millimeter);
            if(index==0){if(!painter.begin(&writer))throw std::runtime_error("Cannot start PDF output");}
            else if(!writer.newPage())throw std::runtime_error("Cannot add PDF page");
            auto id=sheet.views.empty()?document_.source_document_id:sheet.views.front().source_document_id;
            auto source=sheet.views.empty()?document_.source_path:sheet.views.front().source_path;
            if(!source.empty()&&source.is_relative()&&!path_.empty())source=path_.parent_path()/source;
            auto context=build_title_block_context_for_source(id,source,workspace_);
            context.sheet_index=static_cast<int>(index);context.sheet_count=static_cast<int>(document_.sheets.size());
            DrawingCanvas output;output.set_sheet(&sheet);output.set_title_block_context(context);
            output.paint_sheet(painter,writer.resolution()/25.4,QPointF{},true);
        }
        if(!painter.end())throw std::runtime_error("Cannot finish PDF output");
    }
    if(!file.commit())throw std::runtime_error(file.errorString().toStdString());
}
void DrawingWindow::save_document() {
    auto path = path_.empty() ? save_file(this, tr("Uložit výkres"), "drawing.drwz", tr("Výkres ZIMA-CAD (*.drwz)"), "drwz") : QString::fromStdString(path_.string());
    if (path.isEmpty()) return;
    if (!path.endsWith(".drwz", Qt::CaseInsensitive)) path += ".drwz";
    try { document_.save(path.toStdString()); path_ = path.toStdString();
        sync_workspace_document(); set_status_message(tr("Výkres uložen.")); }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze uložit výkres"), error.what()); }
}
void DrawingWindow::add_sheet() {
    zima::drawing::DrawingSheet sheet; sheet.id = zima::drawing::DrawingDocument::create_default().sheets.front().id;
    sheet.name = tr("List %1").arg(document_.sheets.size() + 1).toStdString(); document_.sheets.push_back(std::move(sheet)); refresh(); sheets_->setCurrentIndex(static_cast<int>(document_.sheets.size() - 1));
}
void DrawingWindow::remove_sheet() { if (document_.sheets.size() <= 1) return; document_.sheets.erase(document_.sheets.begin() + sheets_->currentIndex()); refresh(); }
void DrawingWindow::edit_sheet() {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    auto* dialog = new SheetPropertiesDialog(this, *sheet,
        [this, id = sheet->id, old_format = sheet->format](auto accepted) {
        auto* target = document_.find_sheet(id); if (target == nullptr) return;
        if(accepted.format!=old_format) {
            accepted.frame_lines.clear(); accepted.frame_texts.clear();
            accepted.title_block_lines.clear(); accepted.title_block_texts.clear();
            accepted.title_block_fields.clear();
        }
        try{for(auto& view:accepted.views)if(view.use_sheet_scale&&view.scale!=accepted.default_scale){view.scale=accepted.default_scale;if(!view.section_id.empty()){
            auto path=view.source_path;if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
            auto source=load_drawing_source(path,workspace_,view.source_document_id);zima::drawing::refresh_view_geometry(view,source.second);
        }}}catch(const std::exception& e){set_status_message(QString::fromUtf8(e.what()));return;}
        accepted.id = id; *target = std::move(accepted); refresh();
    });
    dialog->show();
}
void DrawingWindow::load_frame() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst formát"),formats_directory_,
                                                 tr("Formát výkresu (*.frmz)"));
    if(path.isEmpty()) return;
    try { zima::drawing::load_frame_template(*sheet,path.toStdString()); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst formát"),error.what()); }
}
void DrawingWindow::remove_frame() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    sheet->frame_lines.clear(); sheet->frame_texts.clear();sheet->frame_circles.clear(); refresh();
}
void DrawingWindow::remove_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    sheet->title_block_lines.clear(); sheet->title_block_texts.clear();
    sheet->title_block_fields.clear(); refresh();
}
void DrawingWindow::load_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst razítko"),formats_directory_,
                                                 tr("Razítko výkresu (*.tblz)"));
    if(path.isEmpty()) return;
    try { zima::drawing::load_title_block_template(*sheet,path.toStdString()); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst razítko"),error.what()); }
}
void DrawingWindow::edit_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr || (sheet->title_block_fields.empty() && sheet->title_block_texts.empty())) return;
    if(raise_open_properties(window())) return;
    auto source_id=sheet->views.empty()?document_.source_document_id:sheet->views.front().source_document_id;
    auto source_path=sheet->views.empty()?document_.source_path:sheet->views.front().source_path;
    if(!source_path.empty() && source_path.is_relative() && !path_.empty())
        source_path=path_.parent_path()/source_path;
    if(workspace_ && !workspace_->find(source_id))
        if(const auto open=workspace_->document_id_for_path(source_path))source_id=*open;
    try {
        const auto selected=canvas_->selected_title_target();
        if(selected && selected->bom_row) {
            if(*selected->bom_row>=sheet->bom_rows.size())throw std::runtime_error("BOM row is no longer available");
            const auto current=build_bom_rows_for_source(source_id,source_path,workspace_);
            const auto row=std::ranges::find(current,sheet->bom_rows[*selected->bom_row].designation,&zima::drawing::BomRow::designation);
            if(row==current.end())throw std::runtime_error("Regenerate the drawing before editing this changed BOM row");
            source_id=row->source_document_id;source_path=row->source_path;
        }
        auto context=build_title_block_context_for_source(source_id,source_path,workspace_);
        // A BOM cell selects the source document, never a smaller editor.
        // Include the same fields and raw-text parameters for every entry point.
        auto fields=sheet->title_block_fields;
        std::string focus=canvas_->selected_title_field();
        const auto parameter_identity=[&](const std::string& token) {
            const auto scope=zima::drawing::title_block_token_scope(token);
            return scope+":"+(scope=="model"
                ?zima::drawing::title_block_parameter_key(token,context)
                :token.substr(token.find('.')+1));
        };
        const auto add_parameters=[&](const std::string& expression,bool focus_parameter) {
            for(const auto& token:zima::drawing::title_block_tokens(expression)) {
                if(zima::drawing::title_block_token_scope(token)=="system")continue;
                const auto identity=parameter_identity(token);
                auto found=std::ranges::find_if(fields,[&](const auto& f){
                    const auto tokens=zima::drawing::title_block_tokens(f.expression);
                    return tokens.size()==1 && f.expression=="&"+tokens.front() &&
                        parameter_identity(tokens.front())==identity;
                });
                std::string id;
                if(found!=fields.end())id=found->id;
                else {
                    id="parameter:"+token;
                    zima::drawing::TitleBlockField f;f.id=id;f.expression="&"+token;f.editable=true;f.write_back=true;fields.push_back(f);
                }
                if(focus_parameter){focus=id;focus_parameter=false;}
            }
        };
        for(const auto& field:sheet->title_block_fields)add_parameters(field.expression,false);
        for(const auto& text:sheet->title_block_texts)add_parameters(text.text,false);
        if(selected)add_parameters(selected->expression,true);
        const auto field_order=[&](const auto& field) {
            const auto tokens=zima::drawing::title_block_tokens(field.expression);
            if(tokens.size()==1 && field.expression=="&"+tokens.front() &&
                zima::drawing::title_block_token_scope(tokens.front())=="model") {
                const auto key=zima::drawing::title_block_parameter_key(tokens.front(),context);
                return std::ranges::find(context.parameter_order,key)-context.parameter_order.begin();
            }
            return context.parameter_order.end()-context.parameter_order.begin();
        };
        std::stable_sort(fields.begin(),fields.end(),[&](const auto& a,const auto& b){return field_order(a)<field_order(b);});
        context.sheet_index=sheets_->currentIndex();context.sheet_count=static_cast<int>(document_.sheets.size());
        std::set<std::string> calculated;
        const auto collect=[&](const auto& model){for(const auto& r:model.relations)calculated.insert(r.target);};
        if(workspace_ && workspace_->open_part(source_id)) collect(workspace_->open_part(source_id)->session.document());
        else if(workspace_ && workspace_->open_assembly(source_id)) collect(workspace_->open_assembly(source_id)->session.document());
        else if(source_path.extension()==".prtz")collect(zima::document::PartDocument::load(source_path));
        else if(source_path.extension()==".asmz")collect(zima::assembly::AssemblyDocument::load(source_path));
        auto* dialog=new TitleBlockPropertiesDialog(this,fields,context,*sheet,calculated,
            [this,id=sheet->id,source_id,source_path,context,fields](const auto& changes) {
                auto* target=document_.find_sheet(id);if(!target)throw std::runtime_error("Drawing sheet no longer exists");
                auto next=*target;
                std::map<std::string,std::string> updates;
                for(const auto& field:fields) if(changes.contains(field.id)) {
                    const auto& value=changes.at(field.id);
                    const auto tokens=zima::drawing::title_block_tokens(field.expression);
                    if(tokens.empty()){
                        const auto stored=std::ranges::find(next.title_block_fields,field.id,&zima::drawing::TitleBlockField::id);
                        if(stored==next.title_block_fields.end())throw std::runtime_error("Title block field no longer exists");
                        stored->expression=value;stored->value=value;continue;
                    }
                    if(tokens.size()!=1 || field.expression!="&"+tokens.front())throw std::runtime_error("Cannot edit a compound expression");
                    const auto& token=tokens.front();
                    const auto scope=zima::drawing::title_block_token_scope(token);
                    if(scope=="drawing")next.local_parameters[token.substr(token.find('.')+1)]=value;
                    else if(scope=="model" && field.write_back) {
                        const auto key=zima::drawing::title_block_parameter_key(token,context);
                        if(updates.contains(key)&&updates.at(key)!=value)throw std::runtime_error("Conflicting values for one parameter");
                        updates[key]=value;
                    }
                }
                if(!updates.empty()) {
                    if(!workspace_)throw std::runtime_error("Open the source model in the workspace to edit its parameters");
                    auto resolved_id=source_id;
                    if(!workspace_->find(resolved_id)) {
                        if(const auto open=workspace_->document_id_for_path(source_path))resolved_id=*open;
                        else if(source_path.extension()==".prtz") {
                            std::vector<zima::kernel::BodyResult> boundaries;
                            auto model=zima::document::PartDocument::load(source_path,&boundaries);resolved_id=model.document_id;
                            workspace_->add_part(std::move(model),std::move(boundaries),source_path);
                        } else if(source_path.extension()==".asmz") {
                            auto model=zima::assembly::AssemblyDocument::load(source_path);resolved_id=model.document_id;
                            workspace_->add_assembly(std::move(model),source_path);
                        }
                    }
                    const auto update=[&](auto& model) {
                        for(const auto& [key,value]:updates) {
                            if(std::ranges::any_of(model.relations,[&](const auto& r){return r.target==key;}))
                                throw std::runtime_error("Calculated parameters are read-only");
                            auto& values=model.user_parameter_values[key];
                            if(values.empty() || values.contains(""))values[""]=value;
                            else values[next.title_block_locale]=value;
                            model.user_parameters[key]=value;
                            if(std::ranges::find(model.user_parameter_order,key)==model.user_parameter_order.end())model.user_parameter_order.push_back(key);
                        }
                    };
                    if(auto* part=workspace_->open_part(resolved_id)) {
                        auto model=part->session.document();update(model);part->session.commit(std::move(model),part->session.calculated_boundaries());
                    } else if(auto* assembly=workspace_->open_assembly(resolved_id)) {
                        auto model=assembly->session.document();update(model);assembly->session.commit(std::move(model));
                    } else throw std::runtime_error("Source model is unavailable");
                    // Refresh metadata only. Editing one BOM row must neither
                    // recalculate its parent Assembly nor update another Part.
                    const auto fresh=build_title_block_context_for_source(resolved_id,source_path,workspace_);
                    const auto identity=source_id+"|"+source_path.lexically_normal().string();
                    const auto update_rows=[&](auto& rows){for(auto& row:rows)
                        if(row.designation==identity || (row.source_document_id==source_id && row.source_path.lexically_normal()==source_path.lexically_normal())) {
                            row.source_document_id=resolved_id;row.source_path=source_path;
                            row.parameters=fresh.parameters;row.parameter_values=fresh.parameter_values;
                            row.parameter_aliases=fresh.parameter_aliases;row.file_stem=fresh.file_stem;row.mass_unit=fresh.mass_unit;
                        }
                    };
                    update_rows(next.bom_rows);
                    for(auto& other:document_.sheets)if(other.id!=id)update_rows(other.bom_rows);
                }
                *target=std::move(next);refresh();
            });
        if(properties_handler_)properties_handler_(dialog);
        connect(dialog,&QObject::destroyed,this,[this]{if(properties_handler_)properties_handler_(nullptr);});
        dialog->show();
        if(auto* editor=dialog->findChild<QLineEdit*>(QString::fromStdString("titleBlockField:"+focus))) {
            editor->setFocus();editor->selectAll();
        }
    } catch(const std::exception& error) {set_status_message(QString::fromUtf8(error.what()));}
}
zima::drawing::DrawingSheet* DrawingWindow::active_sheet() { const auto index = sheets_->currentIndex(); return index < 0 || index >= static_cast<int>(document_.sheets.size()) ? nullptr : &document_.sheets[index]; }

void DrawingWindow::insert_view() {
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) return;
    const auto* sheet=active_sheet(); if (!sheet) return;
    auto source_id=document_.source_document_id;
    auto source_path=document_.source_path;
    if (source_id.empty() && source_path.empty() && workspace_) {
        for (const auto& state : workspace_->documents()) {
            std::visit([&](const auto& item) {
                using State=std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<State,zima::workspace::PartState> ||
                              std::is_same_v<State,zima::workspace::AssemblyState>) {
                    if (source_id.empty()) { source_id=item.session.document().document_id; source_path=item.path; }
                }
            },state);
        }
    }
    if (!source_path.empty() && source_path.is_relative() && !path_.empty())
        source_path=path_.parent_path()/source_path;
    zima::kernel::ViewerMesh mesh;
    try {
        if (!source_id.empty() || !source_path.empty()) {
            auto source=load_drawing_source(source_path,workspace_,source_id);
            source_id=source.first; mesh=std::move(source.second);
        }
    } catch (const std::exception&) {
        // The source can be replaced in the same Properties window after placement.
    }
    auto view=zima::drawing::DrawingDocument::create_view(source_id,source_path,mesh,
        zima::drawing::ViewOrientation::Isometric);
    view.scale=sheet->default_scale; view.use_sheet_scale=true;
    canvas_->begin_placement(std::move(view), [this](auto placed) {
        show_view_properties(std::move(placed),true);
    }, [this] { start_selection(); });
    set_status_message(tr("Vložit pohled: klikněte na místo na listu. Esc zruší vložení."));
}

void DrawingWindow::show_view_properties(zima::drawing::DrawingView view, bool creating) {
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) { canvas_->set_preview({}); return; }
    auto* sheet=active_sheet(); if (!sheet) return;
    const auto sheet_id=sheet->id;
    const auto drawing_id=document_.document_id;
    std::vector<DrawingSourceChoice> sources;
    if (workspace_) for (const auto& state : workspace_->documents())
        std::visit([&](const auto& item) {
            using State=std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State,zima::workspace::PartState> ||
                          std::is_same_v<State,zima::workspace::AssemblyState>)
                sources.push_back({item.session.document().document_id,item.path,
                    QString::fromStdString(item.path.empty() ? item.session.document().name : item.path.filename().string())});
        },state);
    if ((!view.source_document_id.empty() || !view.source_path.empty()) &&
        std::none_of(sources.begin(),sources.end(),[&](const auto& source) {
            return source.id==view.source_document_id && source.path==view.source_path;
        })) sources.push_back({view.source_document_id,view.source_path,
            QString::fromStdString(view.source_path.empty() ? view.source_document_id : view.source_path.filename().string())});
    struct SourceCache {
        std::string key;
        std::string id;
        zima::kernel::ViewerMesh mesh;
        std::vector<drawing::ModelAnnotationSource> annotations;
        std::map<std::array<double,9>,std::pair<std::vector<zima::drawing::ProjectedEdge>,std::vector<zima::drawing::ProjectedTriangle>>> projections;
    };
    auto cache=std::make_shared<SourceCache>();
    const auto project=[this,cache](zima::drawing::DrawingView& value,bool pending_settings=false) {
        auto source_path=value.source_path;
        if (!source_path.empty() && source_path.is_relative() && !path_.empty())
            source_path=path_.parent_path()/source_path;
        const auto key=value.source_document_id+"|"+source_path.generic_string();
        if (cache->key!=key) {
            auto [id,mesh]=load_drawing_source(source_path,workspace_,value.source_document_id);
            if (!value.source_document_id.empty() && id!=value.source_document_id)
                throw std::runtime_error("Zdroj pohledu patří jinému dokumentu.");
            if (mesh.edges.empty() && mesh.triangles.empty())
                throw std::runtime_error("Zdroj nemá vypočtenou geometrii. Nejprve jej regenerujte.");
            cache->key=key; cache->id=std::move(id); cache->mesh=std::move(mesh);cache->projections.clear();
            cache->annotations=drawing_annotation_sources(workspace_,cache->id,source_path);
        }
        value.source_document_id=cache->id; value.source_path=source_path;
        // The dialog owns the actual camera, including relative quarter turns.
        if(!value.section_id.empty()){
            const auto sections=source_sections(workspace_,value.source_document_id,source_path);
            const auto section=std::ranges::find(sections,value.section_id,&zima::document::SectionDefinition::id);
            if(section==sections.end())throw std::runtime_error("Zdrojový řez již neexistuje. Vyberte jiný řez ve vlastnostech pohledu.");
            const auto pending=value.section_snapshot;
            value.section_snapshot=*section;
            if(pending_settings&&pending&&pending->id==section->id)value.section_snapshot->components=pending->components;
            zima::drawing::refresh_view_geometry(value,cache->mesh);drawing::refresh_model_annotations(value,cache->annotations);return;
        }
        const auto& c=value.camera;
        const std::array camera_key{c.horizontal.x,c.horizontal.y,c.horizontal.z,c.vertical.x,c.vertical.y,c.vertical.z,c.depth.x,c.depth.y,c.depth.z};
        auto found=cache->projections.find(camera_key);
        if(found==cache->projections.end())found=cache->projections.emplace(camera_key,std::make_pair(
            zima::drawing::project_edges(cache->mesh,c),zima::drawing::project_triangles(cache->mesh,c))).first;
        value.projected_edges=found->second.first;value.projected_triangles=found->second.second;drawing::refresh_model_annotations(value,cache->annotations);
    };
    const auto error=[this](const QString& message) {
        if (auto* dialog=dynamic_cast<ViewPropertiesDialog*>(view_dialog_.data())) dialog->set_error(message);
    };
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new ViewPropertiesDialog(owner ? owner : this, view, std::move(sources),sheet->default_scale,
        [this,project,cache,error,sheet_id,drawing_id,creating](auto accepted) {
            if (document_.document_id!=drawing_id) return false;
            try {
                if (accepted.name.empty()) throw std::runtime_error("Vyplňte název pohledu.");
                project(accepted,true);
                auto* target_sheet=document_.find_sheet(sheet_id); if (!target_sheet) return false;
                auto next_document=document_;
                auto& next=*next_document.find_sheet(sheet_id);
                const auto id=accepted.id;
                if(!accepted.section_id.empty()&&accepted.section_parent_id.empty())for(auto& parent:next.views)
                    if(parent.id!=id&&parent.source_document_id==accepted.source_document_id&&parent.section_id.empty()){
                        accepted.section_parent_id=parent.id;
                        if(accepted.section_snapshot&&std::ranges::none_of(parent.section_markers,[&](const auto& s){return s.id==accepted.section_id;}))parent.section_markers.push_back(*accepted.section_snapshot);
                        break;
                    }
                std::vector<std::string> refreshed_views{id};
                if (creating) next.views.push_back(accepted);
                else {
                    const auto target=std::find_if(next.views.begin(),next.views.end(),[&](const auto& item) {return item.id==id;});
                    if (target==next.views.end()) return false;
                    const double dx=accepted.x-target->x, dy=accepted.y-target->y;
                    *target=accepted;
                    std::function<void(const zima::drawing::DrawingView&)> update_children;
                    update_children=[&](const auto& parent) {
                        for (auto& child : next.views) if (child.parent_view_id==parent.id) {
                            child.source_document_id=parent.source_document_id; child.source_path=parent.source_path;
                            child.camera=zima::drawing::projected_camera(parent.camera,child.projection_direction,next.projection_method);
                            child.x+=dx; child.y+=dy;
                            project(child); refreshed_views.push_back(child.id); update_children(child);
                        }
                    };
                    update_children(*target);
                }
                auto bom=build_bom_rows_for_source(accepted.source_document_id,accepted.source_path,workspace_);
                if (!bom.empty()) next.bom_rows=std::move(bom);
                std::function<void()> commit_source=[]{};
                if(accepted.section_snapshot){
                    commit_source=prepare_section_component_commit(workspace_,accepted.source_document_id,accepted.source_path,*accepted.section_snapshot);
                    for(auto& sheet:next_document.sheets)for(auto& other:sheet.views)
                        if(other.source_document_id==accepted.source_document_id&&other.section_id==accepted.section_id){
                            other.section_snapshot=accepted.section_snapshot;
                            next_document.refresh_view(other.id,cache->mesh);
                        }
                }
                // Both documents remain unchanged until every projection and
                // source parameter has passed validation.
                for(const auto& refreshed_id:refreshed_views)next_document.refresh_view(refreshed_id,cache->mesh);
                commit_source();document_=std::move(next_document);
                if (document_.source_document_id.empty()) {
                    document_.source_document_id=accepted.source_document_id;
                    document_.source_path=accepted.source_path;
                    document_.source_name=accepted.source_path.stem().string();
                    if (document_.source_name.empty()) {
                        if (workspace_) {
                            if (const auto* part=workspace_->open_part(accepted.source_document_id)) document_.source_name=part->session.document().name;
                            else if (const auto* assembly=workspace_->open_assembly(accepted.source_document_id)) document_.source_name=assembly->session.document().name;
                        }
                    }
                }
                canvas_->set_preview({}); refresh(); canvas_->select_view_for_test(id);
                return true;
            } catch (const std::exception& exception) { error(QString::fromUtf8(exception.what())); return false; }
        }, [this,project,error](auto pending) {
            try { project(pending,true); canvas_->set_preview(std::move(pending)); error({}); }
            catch (const std::exception& exception) { error(QString::fromUtf8(exception.what())); }
        },[this](const auto& id,auto path){if(!path.empty()&&path.is_relative()&&!path_.empty())path=path_.parent_path()/path;return source_sections(workspace_,id,path);});
    dialog->set_initial_size(QSize(820,980));
    view_dialog_=dialog;
    canvas_->set_preview_move_handler([dialog=QPointer<ViewPropertiesDialog>(dialog)](auto position){if(dialog)dialog->move_preview(position);});
    if (properties_handler_) properties_handler_(dialog);
    canvas_->set_preview(view);
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if (view_dialog_==dialog) { view_dialog_.clear(); if (properties_handler_) properties_handler_(nullptr); }
        canvas_->set_preview_move_handler({});canvas_->set_preview({}); update_action_states();
        set_status_message(tr("Výběr: kliknutím do obdélníkové oblasti vyberte pohled."));
    });
    dialog->show();
    update_action_states();
    set_status_message(tr("Vlastnosti pohledu: OK vloží nebo upraví pohled; Zrušit ponechá výkres beze změny."));
}

void DrawingWindow::create_projected_view() {
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) return;
    const auto* parent=document_.find_view(canvas_->selected_view_id());
    const auto* sheet=active_sheet(); if (!parent || !sheet) return;
    try {
        const auto parent_copy=*parent;
        auto [source_id,source_mesh]=load_drawing_source(parent_copy.source_path,workspace_,parent_copy.source_document_id);
        if (source_id!=parent_copy.source_document_id) throw std::runtime_error("Zdroj pohledu patří jinému dokumentu.");
        auto mesh=std::make_shared<zima::kernel::ViewerMesh>(std::move(source_mesh));
        auto view=zima::drawing::DrawingDocument::create_view(source_id,parent_copy.source_path,*mesh);
        view.name=tr("Projekční pohled").toStdString(); view.parent_view_id=parent_copy.id;
        view.scale=parent_copy.scale; view.use_sheet_scale=parent_copy.use_sheet_scale;
        view.display_style=parent_copy.display_style;
        canvas_->begin_placement(std::move(view), [this](auto placed) {
            show_view_properties(std::move(placed),true);
        }, [this] { start_selection(); },
        [parent_copy,mesh,method=sheet->projection_method](auto& pending,auto point) {
            const double dx=point.x-parent_copy.x, dy=point.y-parent_copy.y;
            constexpr double quarter_turn=0.7853981633974483;
            const int sector=(static_cast<int>(std::lround(std::atan2(dy,-dx)/quarter_turn))+8)%8;
            const auto direction=static_cast<zima::drawing::ProjectionDirection>(sector+1);
            if (direction!=pending.projection_direction) {
                pending.projection_direction=direction;
                pending.camera=zima::drawing::projected_camera(parent_copy.camera,direction,method);
                pending.projected_edges=zima::drawing::project_edges(*mesh,pending.camera);
                pending.projected_triangles=zima::drawing::project_triangles(*mesh,pending.camera);
            }
            const auto ray=projection_placement(direction,1.0);
            const double distance=dx*ray.x+dy*ray.y;
            pending.x=parent_copy.x+distance*ray.x; pending.y=parent_copy.y+distance*ray.y;
        });
        set_status_message(tr("Projekční pohled: pohybem zvolte směr, kliknutím umístěte. Esc zruší vložení."));
    } catch (const std::exception& exception) { QMessageBox::warning(this,tr("Projekční pohled"),exception.what()); }
}
QImage DrawingWindow::render_sheet_for_test(bool printing)const{QImage image(840,1188,QImage::Format_ARGB32_Premultiplied);image.fill(printing?Qt::white:Qt::black);QPainter painter(&image);canvas_->paint_sheet(painter,2,{},printing);return image;}
std::optional<QPointF> DrawingWindow::model_annotation_handle_for_test(const drawing::ModelAnnotationReference& id,int end,const std::string& view)const{return canvas_->model_handle(model_annotation_key(id),end,view);}
void DrawingWindow::edit_model_dimension(const std::string& view_id,const std::string& key) {
    if(view_dialog_)return;
    const auto* view=document_.find_view(view_id);if(!view)return;
    const auto item=std::ranges::find_if(view->model_annotations,[&](const auto& a){return model_annotation_key(a.source)==key;});
    if(item==view->model_annotations.end()||!item->model_dimension)return;
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new DimensionLayoutDialog(*item->model_dimension,item->view_layout.value_or(item->model_layout),[this,view_id,key](auto layout){
        auto* view=document_.find_view(view_id);if(!view)throw std::runtime_error("Drawing view no longer exists");
        auto item=std::ranges::find_if(view->model_annotations,[&](const auto& a){return model_annotation_key(a.source)==key;});
        if(item==view->model_annotations.end())throw std::runtime_error("Dimension no longer exists");
        item->view_layout=layout;item->paper_handles.clear();*item=drawing::project_model_annotation(*view,std::move(*item));
        sync_workspace_document();canvas_->update();
    },owner?owner:this);
    view_dialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&QDialog::finished,this,[this]{view_dialog_=nullptr;update_action_states();});
    if(properties_handler_)properties_handler_(dialog);dialog->show();update_action_states();
}

void DrawingWindow::show_erase(){
    if(view_dialog_){view_dialog_->raise();return;}if(raise_open_properties(window()))return;
    const auto* view=document_.find_view(canvas_->selected_view_id());
    if(!view){canvas_->choose_view([this](const std::string& id){canvas_->select_view_for_test(id);show_erase();});set_status_message(tr("Show / Erase: vyberte výkresový pohled. Esc zruší příkaz."));return;}
    const auto id=view->id;
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new ShowEraseDialog(*view,[this](const auto& pending,const auto& offered){
        std::set<std::string> ids;for(const auto& r:offered)ids.insert(model_annotation_key(r));
        canvas_->set_preview(pending);canvas_->set_model_command(std::move(ids),[this,id=pending.id](const auto& key){
            const auto* view=document_.find_view(id);if(!view)return;
            if(auto* dialog=dynamic_cast<ShowEraseDialog*>(view_dialog_.data()))for(const auto& r:view->model_annotations)if(model_annotation_key(r.source)==key){dialog->toggle(r.source);break;}
        });
    },[this,id](const auto& pending){auto* target=document_.find_view(id);if(!target)throw std::runtime_error("Pohled již neexistuje");target->model_annotations=pending.model_annotations;sync_workspace_document();if(changed_handler_)changed_handler_();},owner?owner:this);
    view_dialog_=dialog;if(properties_handler_)properties_handler_(dialog);
    connect(dialog,&QDialog::finished,this,[this,id]{canvas_->set_model_command({},{});canvas_->set_preview({});view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);refresh();canvas_->select_view_for_test(id);});
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();update_action_states();
}

void DrawingWindow::regenerate_selected_view() {
    if(view_dialog_)return;
    try {
        auto next=document_;
        struct Source {std::string id;zima::kernel::ViewerMesh mesh;std::vector<zima::drawing::BomRow> bom;std::vector<drawing::ModelAnnotationSource> annotations;};
        std::map<std::pair<std::string,std::filesystem::path>,Source> sources;
        for(auto& sheet:next.sheets) {
            std::optional<std::vector<zima::drawing::BomRow>> sheet_bom;
            for(auto& view:sheet.views) {
                const auto key=std::make_pair(view.source_document_id,view.source_path);
                auto found=sources.find(key);
                if(found==sources.end()) {
                    auto [id,mesh]=load_drawing_source(view.source_path,workspace_,view.source_document_id);
                    if(id!=view.source_document_id)throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                    auto bom=build_bom_rows_for_source(id,view.source_path,workspace_);
                    auto source_path=view.source_path;if(source_path.is_relative()&&!path_.empty())source_path=path_.parent_path()/source_path;
                    auto annotations=drawing_annotation_sources(workspace_,id,source_path);
                    found=sources.emplace(key,Source{id,std::move(mesh),std::move(bom),std::move(annotations)}).first;
                }
                if(!view.section_id.empty()){
                    auto path=view.source_path;if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
                    const auto sections=source_sections(workspace_,view.source_document_id,path);
                    const auto section=std::ranges::find(sections,view.section_id,&zima::document::SectionDefinition::id);
                    if(section==sections.end())throw std::runtime_error("Zdrojový řez již neexistuje. Vyberte jiný řez ve vlastnostech pohledu.");
                    view.section_snapshot=*section;
                }
                if(!view.section_markers.empty()){
                    auto source_path=view.source_path;if(source_path.is_relative()&&!path_.empty())source_path=path_.parent_path()/source_path;
                    const auto sections=source_sections(workspace_,view.source_document_id,source_path);
                    for(auto& marker:view.section_markers){const auto current=std::ranges::find(sections,marker.id,&zima::document::SectionDefinition::id);if(current==sections.end())throw std::runtime_error("Zdrojová trasa řezu již neexistuje. Upravte výběr tras ve vlastnostech pohledu.");marker=*current;}
                }
                if(!sheet_bom || view.source_document_id==next.source_document_id)sheet_bom=found->second.bom;
            }
            // Explicit regeneration evaluates the projection tree parent-first,
            // regardless of the order in which views were saved on the sheet.
            std::map<std::string,int> visit;
            std::function<void(zima::drawing::DrawingView&)> refresh_view;
            refresh_view=[&](auto& view) {
                auto& state=visit[view.id];
                if(state==2)return;
                if(state==1)throw std::runtime_error(tr("Pohledy obsahují cyklickou závislost.").toStdString());
                state=1;
                if(!view.parent_view_id.empty()) {
                    const auto parent=std::ranges::find(sheet.views,view.parent_view_id,&zima::drawing::DrawingView::id);
                    if(parent==sheet.views.end())throw std::runtime_error(tr("Nadřazený pohled není dostupný.").toStdString());
                    refresh_view(*parent);
                    view.camera=zima::drawing::projected_camera(parent->camera,view.projection_direction,sheet.projection_method);
                }
                next.refresh_view(view.id,sources.at({view.source_document_id,view.source_path}).mesh);
                drawing::refresh_model_annotations(view,sources.at({view.source_document_id,view.source_path}).annotations);
                state=2;
            };
            for(auto& view:sheet.views)refresh_view(view);
            if(sheet_bom)sheet.bom_rows=std::move(*sheet_bom);
        }
        document_=std::move(next);
        refresh();set_status_message(tr("Výkres regenerován."));
    } catch(const std::exception& error) {
        QMessageBox::warning(this,tr("Nelze regenerovat pohled"),error.what());
    }
}

void DrawingWindow::delete_selected_view() {
    auto* sheet = active_sheet(); const std::string selected = canvas_->selected_view_id();
    if (sheet == nullptr || selected.empty()) return;
    std::vector<std::string> removed{selected};
    for (std::size_t index = 0; index < removed.size(); ++index) {
        for (const auto& view : sheet->views)
            if (view.parent_view_id == removed[index] &&
                std::find(removed.begin(), removed.end(), view.id) == removed.end())
                removed.push_back(view.id);
    }
    std::erase_if(sheet->views, [&](const auto& view) {
        return std::find(removed.begin(), removed.end(), view.id) != removed.end();
    });
    std::erase_if(sheet->dimensions, [&](const auto& dimension) {
        return std::find(removed.begin(), removed.end(), dimension.view_id) != removed.end();
    });
    refresh();
}
void DrawingWindow::edit_selected_view() {
    if (const auto* view=document_.find_view(canvas_->selected_view_id())) show_view_properties(*view,false);
}
void DrawingWindow::start_linear_dimension() {
    canvas_->start_linear_dimension();
    update_action_states();
    set_status_message(tr("Lineární kóta: vyberte dvě rovnoběžné hrany stejného pohledu."));
}
void DrawingWindow::fit_sheet() { canvas_->fit_sheet(); }
void DrawingWindow::start_selection() {
    canvas_->start_selection();
    update_action_states();
    set_status_message(tr("Výběr: kliknutím vyberte pohled nebo kótu."));
}
void DrawingWindow::update_action_states() {
    const auto* sheet = active_sheet();
    const bool has_sheet = sheet != nullptr && !view_dialog_;
    sheet_controls_->setEnabled(!view_dialog_);
    const bool has_view = has_sheet && !sheet->views.empty();
    const bool selected_view = has_sheet &&
        document_.find_view(canvas_->selected_view_id()) != nullptr;
    save_action_->setEnabled(has_sheet);
    add_sheet_action_->setEnabled(!view_dialog_);
    remove_sheet_action_->setEnabled(!view_dialog_ && document_.sheets.size() > 1);
    edit_sheet_action_->setEnabled(has_sheet);
    edit_title_block_action_->setEnabled(
        has_sheet && (!sheet->title_block_fields.empty() || !sheet->title_block_texts.empty()));
    insert_view_action_->setEnabled(has_sheet);
    projected_view_action_->setEnabled(selected_view);
    edit_view_action_->setEnabled(selected_view);
    regenerate_view_action_->setEnabled(!view_dialog_&&std::ranges::any_of(document_.sheets,[](const auto& sheet){return !sheet.views.empty();}));
    delete_view_action_->setEnabled(selected_view);
    linear_dimension_action_->setEnabled(has_view);
    linear_dimension_action_->setChecked(canvas_->dimension_mode());
    show_erase_action_->setEnabled(has_view);
    selection_action_->setEnabled(has_sheet);
    selection_action_->setChecked(!canvas_->dimension_mode());
}
void DrawingWindow::update_source_variant() {
    QSignalBlocker blocker(source_variant_);
    source_variant_->clear();
    auto label = QString::fromStdString(document_.source_path.filename().string());
    if (label.isEmpty()) label = QString::fromStdString(document_.source_name);
    if (label.isEmpty() && workspace_) {
        if (const auto* part=workspace_->open_part(document_.source_document_id)) label=QString::fromStdString(part->session.document().name);
        else if (const auto* assembly=workspace_->open_assembly(document_.source_document_id)) label=QString::fromStdString(assembly->session.document().name);
    }
    if (label.isEmpty()) label=tr("Bez zdroje");
    source_variant_->addItem(label,QString::fromStdString(document_.source_document_id));
}
void DrawingWindow::refresh() {
    update_source_variant();
    const int wanted = std::clamp(sheets_ ? sheets_->currentIndex() : 0, 0, static_cast<int>(document_.sheets.size() - 1));
    sheets_->blockSignals(true); while (sheets_->count()) sheets_->removeTab(0);
    for (const auto& sheet : document_.sheets) sheets_->addTab(QString::fromStdString(sheet.name));
    sheets_->setCurrentIndex(wanted); sheets_->blockSignals(false); canvas_->set_sheet(active_sheet());
    if (const auto* sheet = active_sheet()) {
        const QSignalBlocker format_blocker(sheet_format_);
        const QSignalBlocker projection_blocker(projection_method_);
        const QSignalBlocker numerator_blocker(scale_numerator_);
        const QSignalBlocker denominator_blocker(scale_denominator_);
        sheet_format_->setCurrentIndex(static_cast<int>(sheet->format));
        projection_method_->setCurrentIndex(
            sheet->projection_method == zima::drawing::ProjectionMethod::FirstAngle ? 0 : 1);
        scale_numerator_->setValue(1.0);
        scale_denominator_->setValue(1.0 / std::max(sheet->default_scale, 0.001));
    }
    const auto view_count = active_sheet() ? active_sheet()->views.size() : 0;
    set_status_message(view_count == 0
        ? tr("Nový výkres: použijte Vložit pohled a vyberte otevřený Part nebo sestavu.")
        : tr("Výkres: %1 listů, %2 pohledů")
            .arg(document_.sheets.size()).arg(view_count));
    update_action_states();
    sync_workspace_document();
    refresh_title_block_context();
}

void DrawingWindow::refresh_title_block_context() {
    // Matches Python's App._refresh_drawing_title_block_context: the
    // title-block tokens resolve against the currently displayed sheet's
    // primary (first-inserted) view's source document.
    const auto* sheet = active_sheet();
    if (!sheet) {canvas_->set_title_block_context(std::nullopt);return;}
    auto source_id=sheet->views.empty()?document_.source_document_id:sheet->views.front().source_document_id;
    auto source_path=sheet->views.empty()?document_.source_path:sheet->views.front().source_path;
    if(!source_path.empty() && source_path.is_relative() && !path_.empty())
        source_path=path_.parent_path()/source_path;
    if(workspace_ && !workspace_->find(source_id))
        if(const auto open=workspace_->document_id_for_path(source_path))source_id=*open;
    auto context=build_title_block_context_for_source(source_id,source_path,workspace_);
    context.sheet_index=sheets_->currentIndex();context.sheet_count=static_cast<int>(document_.sheets.size());
    canvas_->set_title_block_context(std::move(context));
}

void DrawingWindow::sync_workspace_document() {
    document_.synchronize_dimension_identifiers();
    if(workspace_!=nullptr) for(auto& sheet:document_.sheets) for(auto& view:sheet.views)
        if(view.source_path.empty()) {
            if(const auto* part=workspace_->open_part(view.source_document_id)) view.source_path=part->path;
            else if(const auto* assembly=workspace_->open_assembly(view.source_document_id))
                view.source_path=assembly->path;
        }
    if(workspace_!=nullptr && !workspace_document_id_.empty())
        if(auto* state=workspace_->open_drawing(workspace_document_id_)) {
            state->document=document_; state->path=path_;
        }
    if (changed_handler_) changed_handler_();
    if (selection_handler_) selection_handler_(canvas_->selected_view_id());
}

}  // namespace zima::app
