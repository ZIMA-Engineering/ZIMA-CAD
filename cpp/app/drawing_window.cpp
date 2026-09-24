#include "../common/interaction_colors.hpp"
#include "symbol_properties_dialog.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QOpenGLWidget>
#include <QResizeEvent>
#include <QHideEvent>
#include <QDate>
#include <QElapsedTimer>
#include "drawing_detail_dialog.hpp"
#include <zima/kernel/stable_id.hpp>
#include <zima/drawing_render/crop_path.hpp>
#include "drawing_break_editor.hpp"
#include <zima/drawing/annotation_guides.hpp>
#include "inline_dimension_edit.hpp"
#include "numeric_expression_edit.hpp"
#include "toolbar_style.hpp"
#include "sketch_text_properties_dialog.hpp"
#include <zima/document/file_path.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/drawing_label_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <zima/workspace/drawing_dimension_operations.hpp>
#include <zima/workspace/drawing_title_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include "drawing_dimension_dialog.hpp"
#include "drawing_balloon_dialog.hpp"
#include <zima/workspace/drawing_balloon_operations.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <QCursor>
#include <zima/viewer/dimension_text_layer.hpp>
#include "dimension_properties_fields.hpp"
#include "resource_icon.hpp"
#include <zima/drawing_render/dxf_export.hpp>
#include <zima/drawing_render/image_export.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include "drawing_annotation_layout.hpp"
#include "show_erase_dialog.hpp"
#include <zima/viewer/annotation_arrow.hpp>
#include "section_source.hpp"
#include "section_properties_dialog.hpp"
#include <QScrollArea>
#include <QGroupBox>
#include <QGridLayout>
#include "drawing_shading.hpp"
#include "drawing_sheet_surface.hpp"
#include <QPaintEngine>
#include <zima/drawing_render/pdf_export.hpp>
#include <QPageSize>
#include <QSaveFile>
#include <zima/ui/numeric_value_lock.hpp>
#include "drawing_window.hpp"
#include <zima/workspace/document_operations.hpp>
#include <zima/viewer/embedded_image.hpp>
#include "file_dialog.hpp"
#include "application_settings.hpp"

#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QToolButton>
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
#include <QFrame>
#include <QDoubleSpinBox>
#include <QSpinBox>
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
#include <bit>
#include <chrono>
#include <cmath>
#include <sstream>
#include <memory>
#include <set>

namespace zima::app {
using zima::workspace::drawing_annotation_sources;
using drawing_render::SheetRenderer;
using drawing_render::drawing_font_family;
using zima::workspace::build_bom_rows_for_source;
using zima::workspace::build_title_block_context_for_source;
namespace {

bool raise_open_properties(QWidget* owner) {
    for (auto* dialog : owner->findChildren<QDialog*>())
        if (dynamic_cast<zima::ui::PropertiesSubWindow*>(dialog) && dialog->isVisible()) {
            dialog->raise(); return true;
        }
    return false;
}

struct DrawingSourceChoice {
    std::string id;
    std::filesystem::path path;
    QString name;
    bool evaluated{true};
};
std::vector<DrawingSourceChoice> family_source_choices(const zima::workspace::Workspace*,
    const std::string&,const std::filesystem::path&);

class SourceRemovalConfirmation final : public zima::ui::PropertiesSubWindow {
public:
    SourceRemovalConfirmation(QWidget* owner,const QString& message,std::function<void()> confirm)
        :PropertiesSubWindow(QObject::tr("Odebrat zdroj výkresu"),owner),confirm_(std::move(confirm)) {
        setObjectName("drawingSourceRemovalConfirmation");setAttribute(Qt::WA_DeleteOnClose);
        auto* label=new QLabel(message,this);label->setWordWrap(true);label->setTextFormat(Qt::PlainText);
        content_layout()->addWidget(label);set_initial_size({560,300});
    }
private:
    bool submit() override {confirm_();return true;}
    std::function<void()> confirm_;
};

class DrawingSettingsDialog final : public zima::ui::PropertiesSubWindow {
public:
    DrawingSettingsDialog(QWidget* owner,zima::drawing::DrawingDocument document,
        std::filesystem::path drawing_path,const zima::workspace::Workspace* live,
        std::function<void(zima::drawing::DrawingDocument)> commit,std::function<void(QDialog*)> focus)
        :PropertiesSubWindow(QObject::tr("Nastavení výkresu"),owner),pending_(std::move(document)),
         drawing_path_(std::move(drawing_path)),live_(live),commit_(std::move(commit)),focus_(std::move(focus)) {
        setObjectName("drawingSettingsDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({720,440});
        content_layout()->addWidget(new QLabel(QObject::tr("Zdroje dat — díly a sestavy"),this));
        table_=new QTableWidget(this);table_->setObjectName("drawingDataSources");
        table_->setColumnCount(2);table_->setHorizontalHeaderLabels({QString{},QObject::tr("Soubor")});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setStyleSheet("QTableWidget{background:#20252b;color:#e6edf3;gridline-color:#47515c;}");
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);table_->setColumnWidth(0,30);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        zima::ui::install_reference_cell_delegate(table_);content_layout()->addWidget(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int){
            if(row==table_->rowCount()-1)add_source();
        });
        refresh_rows();
        connect(this,&QDialog::finished,this,[this]{if(confirmation_)confirmation_->reject();});
    }
private:
    zima::drawing::DrawingDocument pending_;
    std::filesystem::path drawing_path_;
    const zima::workspace::Workspace* live_{};
    std::function<void(zima::drawing::DrawingDocument)> commit_;
    std::function<void(QDialog*)> focus_;
    QTableWidget* table_{};
    QPointer<QDialog> confirmation_;
    bool changed_{};
    bool selecting_file_{};
    bool submit() override {if(confirmation_||selecting_file_)return false;if(changed_)commit_(pending_);return true;}
    void refresh_rows() {
        const auto sources=pending_.data_sources();table_->setRowCount(0);
        table_->setRowCount(static_cast<int>(sources.size())+1);
        for(int row=0;row<static_cast<int>(sources.size());++row) {
            const auto& source=sources[row];
            auto* indicator=zima::ui::build_reference_row_indicator([this,id=source.document_id]{remove_source(id);});
            zima::ui::set_reference_row_populated(indicator,true);table_->setCellWidget(row,0,indicator);
            const auto text=QString::fromStdString(zima::document::path_to_utf8(source.source_path));
            auto* item=new zima::ui::ReferenceCellItem(text);item->set_reference(QString::fromStdString(source.document_id));
            item->setToolTip(text);table_->setItem(row,1,item);
        }
        const auto row=static_cast<int>(sources.size());
        table_->setCellWidget(row,0,zima::ui::build_reference_row_indicator([]{}));
        auto* item=new zima::ui::ReferenceCellItem(QObject::tr("Přidat zdroj…"));
        item->set_placeholder_style(QColor("#999999"));table_->setItem(row,1,item);
    }
    void add_source() {
        auto* item=static_cast<zima::ui::ReferenceCellItem*>(table_->item(table_->rowCount()-1,1));
        item->set_active_input(true);table_->viewport()->update();
        selecting_file_=true;buttons()->button(QDialogButtonBox::Ok)->setEnabled(false);
        const auto file=open_file(this,QObject::tr("Přidat zdroj výkresu"),
            QString::fromStdString(zima::document::path_to_utf8(drawing_path_.parent_path())),
            QObject::tr("Díly a sestavy ZIMA-CAD (*.prtz *.asmz)"));
        selecting_file_=false;buttons()->button(QDialogButtonBox::Ok)->setEnabled(true);
        item->set_active_input(false);
        if(file.isEmpty())return;
        const auto path=std::filesystem::u8path(file.toStdString());
        try {
            if(file.endsWith(".prtz",Qt::CaseInsensitive)==false&&file.endsWith(".asmz",Qt::CaseInsensitive)==false)throw std::runtime_error("Vyberte soubor .prtz nebo .asmz.");
            const auto choices=family_source_choices(live_,{},path);
            if(choices.empty())throw std::runtime_error("Zdrojový dokument nelze načíst.");
            pending_.add_data_source({choices.front().id,path,zima::document::path_to_utf8(path.stem())});changed_=true;refresh_rows();
        } catch(const std::exception& error) {table_->item(table_->rowCount()-1,1)->setText(QObject::tr(error.what()));}
    }
    void remove_source(const std::string& id) {
        if(confirmation_)return;
        auto next=pending_;const auto removed=next.remove_data_source(id);
        QStringList names;
        for(const auto& view_id:removed)if(const auto* view=pending_.find_view(view_id))names<<QString::fromStdString(view->name);
        QString message=QObject::tr("Odebrat zdroj %1?\n\nZ výkresu budou odebrány všechny varianty tohoto zdroje a %2 pohledů včetně jejich kót a pozic.")
            .arg(QString::fromStdString(zima::document::path_to_utf8(pending_.data_source_path(id).filename()))).arg(removed.size());
        if(!names.isEmpty())message+="\n"+names.mid(0,10).join(", ")+(names.size()>10?QStringLiteral("…"):QString{});
        message+=QObject::tr("\nÚdaje razítka a kusovníku navázané na tento zdroj budou odpojeny. Zdrojový soubor zůstane zachovaný.\n\nZměny se provedou až tlačítkem OK v Nastavení výkresu.");
        auto* dialog=new SourceRemovalConfirmation(parentWidget(),message,[this,next=std::move(next)]()mutable{pending_=std::move(next);changed_=true;refresh_rows();});
        confirmation_=dialog;setEnabled(false);if(focus_)focus_(dialog);
        connect(dialog,&QDialog::finished,this,[this]{confirmation_.clear();setEnabled(true);if(focus_)focus_(this);raise();});
        dialog->show();
    }
};

// Reading the chooser and its previews consumes only persisted model data.
std::vector<DrawingSourceChoice> family_source_choices(const zima::workspace::Workspace* live,
    const std::string& requested, const std::filesystem::path& path) {
    std::vector<DrawingSourceChoice> result;
    auto root=requested.substr(0,requested.find(":family:"));
    if(root.empty()&&live)if(const auto open=live->document_id_for_path(path))root=open->substr(0,open->find(":family:"));
    const auto append=[&](const auto& model,const auto& source_path) {
        result.push_back({model.document_id,source_path,QString::fromStdString(model.name)+" — "+QObject::tr("Výchozí (nativní)")});
        for(const auto& row:zima::document::parse_family_table(model.family_table).instances)
            result.push_back({model.document_id+":family:"+row.id,source_path,QString::fromStdString(row.name),model.family.evaluated.contains(row.id)});
    };
    if(live) {
        if(const auto* part=live->open_part(root)){append(part->session.document(),part->path);return result;}
        if(const auto* assembly=live->open_assembly(root)){append(assembly->session.document(),assembly->path);return result;}
    }
    if(QString::fromStdString(path.extension().string()).compare(".prtz",Qt::CaseInsensitive)==0) {
        std::vector<zima::kernel::BodyResult> cache;append(zima::workspace::read_family_part(live,path,root,cache),path);
    } else append(zima::workspace::read_family_assembly(live,path,root,false),path);
    return result;
}

// Called only by OK. Calculate an unopened row in a private workspace, and
// publish its native packet only after the complete Drawing edit succeeds.
std::function<void()> prepare_drawing_family_variant(zima::workspace::Workspace* live,
    zima::workspace::Workspace& draft,const zima::drawing::DrawingView& view,const std::filesystem::path& drawing_path) {
    const auto separator=view.source_document_id.find(":family:");
    if(separator==std::string::npos)return []{};
    const auto root=view.source_document_id.substr(0,separator),row_id=view.source_document_id.substr(separator+8);
    auto path=view.source_path;if(path.is_relative()&&!drawing_path.empty())path=drawing_path.parent_path()/path;
    if(!draft.find(root)) {
        auto source=zima::workspace::read_native_document(path);
        if(source.id()!=root)throw std::runtime_error("The drawing source file belongs to a different document.");
        static_cast<void>(zima::workspace::insert_native_document(draft,std::move(source)));
    }
    const auto family=draft.open_part(root)?draft.open_part(root)->session.document().family:draft.open_assembly(root)->session.document().family;
    if(family.evaluated.contains(row_id))return []{};
    if(!live)throw std::runtime_error("Open the Family Table variant before selecting it in the Drawing.");
    const auto table=zima::document::parse_family_table(draft.open_part(root)?draft.open_part(root)->session.document().family_table:draft.open_assembly(root)->session.document().family_table);
    const auto row=std::ranges::find(table.instances,row_id,&zima::document::FamilyInstance::id);
    if(row==table.instances.end())throw std::runtime_error("Family Table variant no longer exists.");
    zima::kernel::OcctKernel kernel;
    static_cast<void>(zima::workspace::open_family_instance(draft,kernel,root,row->name,false));
    if(const auto* part=draft.open_part(root)) {
        const auto model=part->session.document();const auto cache=part->session.calculated_boundaries();
        return [live,root,model,cache,path] {
            if(auto* parent=live->open_part(root))parent->session.update_family_evaluated(model.family);
            else {live->add_part(model,cache,path);live->open_part(root)->session.update_family_evaluated(model.family);}
        };
    }
    const auto model=draft.open_assembly(root)->session.document();
    return [live,root,model,path] {
        if(auto* parent=live->open_assembly(root))parent->session.update_family_evaluated(model.family);
        else {live->add_assembly(model,path);live->open_assembly(root)->session.update_family_evaluated(model.family);}
    };
}

class ViewPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    ViewPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingView initial,
        std::vector<DrawingSourceChoice> sources, double sheet_scale,
        std::function<bool(zima::drawing::DrawingView)> accepted,
        std::function<void(std::optional<zima::drawing::DrawingView>)> preview,
        std::function<std::vector<zima::document::SectionDefinition>(const std::string&,const std::filesystem::path&)> sections,
        std::function<std::vector<DrawingSourceChoice>(const std::filesystem::path&)> browse_sources,
        std::function<void(ViewPropertiesDialog*,drawing::DrawingView)> edit_breaks,
        std::function<void(ViewPropertiesDialog*,drawing::DrawingView,int,std::string)> edit_crop,
        bool creating)
        : PropertiesSubWindow(QObject::tr("Vlastnosti pohledu"), parent),
          value_(std::move(initial)), sources_(std::move(sources)),
          sections_(std::move(sections)), sheet_scale_(sheet_scale), accepted_(std::move(accepted)), preview_(std::move(preview)) {
        setObjectName("drawingViewProperties");
        auto* content = new QWidget(this);
        auto* form = new QVBoxLayout(content);
        const auto group=[&](const QString& title) {
            auto* box=new QGroupBox(title,content);auto* grid=new QGridLayout(box);
            grid->setHorizontalSpacing(12);grid->setVerticalSpacing(4);grid->setAlignment(Qt::AlignTop);form->addWidget(box);return grid;
        };
        const auto field=[](QGridLayout* grid,int row,int column,const QString& label,QWidget* widget,int span=1) {
            auto* caption=new QLabel(label);caption->setBuddy(widget);
            grid->addWidget(caption,row*2,column,1,span);
            grid->addWidget(widget,row*2+1,column,1,span);
            grid->setColumnStretch(column,1);
        };
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
            source_->addItem(sources_[i].name,QString::fromStdString(sources_[i].id));
            if ((!value_.source_document_id.empty() && sources_[i].id == value_.source_document_id) ||
                (value_.source_document_id.empty() && !value_.source_path.empty() && sources_[i].path == value_.source_path)) selected = static_cast<int>(i);
        }
        source_->setCurrentIndex(selected >= 0 ? selected : (sources_.empty() ? -1 : 0));
        auto* source_row = new QWidget(content);
        auto* source_layout = new QHBoxLayout(source_row);
        source_layout->setContentsMargins(0,0,0,0);
        auto* browse = new QPushButton(QObject::tr("Soubor…"), source_row);
        browse->setObjectName("drawingViewBrowseSource");
        source_layout->addWidget(source_, 1); source_layout->addWidget(browse);source_row->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        source_row->setEnabled(value_.parent_view_id.empty());
        connect(browse, &QPushButton::clicked, this, [this,browse_sources] {
            const auto path = open_file(this, tr("Zdroj pohledu"),
                QString::fromStdString(value_.source_path.string()), tr("Model ZIMA-CAD (*.prtz *.asmz)"));
            if (path.isEmpty()) return;
            try {
                const auto choices=browse_sources(std::filesystem::u8path(path.toStdString()));
                const int first=source_->count();
                for(const auto& choice:choices){sources_.push_back(choice);source_->addItem(choice.name,QString::fromStdString(choice.id));}
                source_->setCurrentIndex(first);
            }catch(const std::exception& error){set_error(tr(error.what()));}
        });
        orientation_ = new QComboBox(content);
        orientation_->setObjectName("drawingViewOrientation");
        const char* orientations[]{QT_TR_NOOP("Přední"), QT_TR_NOOP("Zadní"), QT_TR_NOOP("Levý"), QT_TR_NOOP("Pravý"), QT_TR_NOOP("Horní"), QT_TR_NOOP("Dolní"), QT_TR_NOOP("Izometrický")};
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
        auto* identity=group(tr("Pohled"));
        field(identity,0,0,tr("Zdroj"),source_row);
        auto* name_row=new QWidget(content);auto* name_layout=new QHBoxLayout(name_row);name_layout->setContentsMargins(0,0,0,0);
        name_layout->addWidget(name_,1);name_layout->addWidget(caption_);field(identity,0,1,tr("Název"),name_row);
        auto* orientation=group(tr("Orientace"));
        field(orientation,0,0,tr("Základní pohled"),orientation_,3);
        rotation_base_=value_.camera;
        for(int axis=0;axis<3;++axis) {
            auto* row=new QWidget(content);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
            auto* spin=new ExpressionDoubleSpinBox(row);rotation_values_[axis]=spin;
            spin->setObjectName(axis==0?"drawingRotationHorizontal":axis==1?"drawingRotationVertical":"drawingRotationRoll");
            spin->setRange(-3600,3600);spin->setDecimals(3);spin->setSingleStep(1);
            spin->setToolTip(tr("Pootočení vůči pohledu při otevření vlastností. Krok šipek je 1°."));
            layout->addWidget(spin,1);
            for(int sign:{-1,1}) {
                auto* button=new QPushButton(sign<0?QStringLiteral("−90°"):QStringLiteral("+90°"),row);
                button->setObjectName(axis==0?(sign<0?"drawingRotateLeft":"drawingRotateRight"):axis==1?(sign<0?"drawingRotateDown":"drawingRotateUp"):(sign<0?"drawingRotateClockwise":"drawingRotateCounterclockwise"));
                button->setAutoDefault(false);button->setFixedWidth(48);layout->addWidget(button);rotation_buttons_.push_back(button);
                connect(button,&QPushButton::clicked,this,[spin,sign]{spin->setValue(spin->value()+sign*90);});
            }
            connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{
                value_.camera=drawing::rotated_camera(rotation_base_,rotation_values_[0]->value(),rotation_values_[1]->value(),rotation_values_[2]->value());
                {QSignalBlocker block(orientation_);orientation_->setCurrentIndex(7);}preview_values();
            });
            field(orientation,1,axis,axis==0?tr("Vodorovně [°]"):axis==1?tr("Svisle [°]"):tr("V rovině pohledu [°]"),row);
        }
        auto* appearance=group(tr("Zobrazení a čáry"));
        field(appearance,0,0,tr("Zobrazení"),display_);field(appearance,0,1,tr("Tečné hrany"),tangent_style_);
        thread_leadins_=new QCheckBox(tr("Zobrazit kružnice náběhu závitu"),content);thread_leadins_->setObjectName("drawingThreadLeadins");thread_leadins_->setChecked(value_.show_thread_leadins);appearance->addWidget(thread_leadins_,2,0,1,3);connect(thread_leadins_,&QCheckBox::toggled,this,[this]{preview_values();});
        auto* placement=group(tr("Měřítko a poloha na listu"));
        field(placement,0,0,tr("Měřítko"),scale_mode_);field(placement,0,1,tr("Hodnota"),scale_);
        field(placement,1,0,tr("X [mm]"),x_);field(placement,1,1,tr("Y [mm]"),y_);
        guides_=new QCheckBox(tr("Zobrazovat vodítka i mimo přesouvání"),content);guides_->setObjectName("drawingDimensionGuides");guides_->setChecked(value_.show_dimension_guides);
        guide_offset_=new QDoubleSpinBox(content);guide_spacing_=new QDoubleSpinBox(content);guide_offset_->setObjectName("drawingGuideOffset");guide_spacing_->setObjectName("drawingGuideSpacing");
        guide_offset_->setDecimals(3);guide_offset_->setRange(.001,1000);guide_spacing_->setDecimals(3);guide_spacing_->setRange(.1,1000);guide_offset_->setValue(value_.dimension_guide_offset);guide_spacing_->setValue(value_.dimension_guide_spacing);
        guide_count_=new QSpinBox(content);guide_count_->setObjectName("drawingGuideCount");guide_count_->setRange(0,100);guide_count_->setValue(value_.dimension_guide_count);
        auto* guides=group(tr("Vodítka kót"));field(guides,0,0,tr("Počet"),guide_count_);connect(guide_count_,&QSpinBox::valueChanged,this,[this]{preview_values();});
        field(guides,0,1,tr("Odsazení [mm]"),guide_offset_);field(guides,0,2,tr("Rozteč [mm]"),guide_spacing_);guides->addWidget(guides_,2,0,1,3);
        connect(guides_,&QCheckBox::toggled,this,[this]{preview_values();});for(auto* control:{guide_offset_,guide_spacing_})connect(control,&QDoubleSpinBox::valueChanged,this,[this]{preview_values();});
        auto* metrics=new QHBoxLayout;metrics->setSpacing(10);
        form->removeWidget(placement->parentWidget());form->removeWidget(guides->parentWidget());
        metrics->addWidget(placement->parentWidget(),1);metrics->addWidget(guides->parentWidget(),1);form->addLayout(metrics);
        section_=new QComboBox(content);section_->setObjectName("drawingSection");
        section_label_=new QCheckBox(tr("Zobrazit označení řezu"),content);section_label_->setObjectName("drawingSectionLabel");section_label_->setChecked(value_.show_section_label);
        components_=new SectionComponentsWidget(content);
        auto* breaks=new QPushButton(tr("Editovat přerušení…"),content);breaks->setObjectName("drawingEditBreaks");auto* section_group=group(tr("Řezy a přerušení"));field(section_group,0,1,tr("Přerušení pohledu"),breaks);
        connect(breaks,&QPushButton::clicked,this,[this,edit_breaks]{edit_breaks(this,values());});
        field(section_group,0,0,tr("Řez"),section_);
        auto* crop=new QPushButton(tr("Omezit zobrazení…"),content);crop->setObjectName("drawingViewCrop");
        auto* crop_menu=new QMenu(crop);crop->setMenu(crop_menu);field(section_group,0,2,tr("Částečné zobrazení"),crop);
        const std::array<QString,4> crop_labels{tr("Kruh"),tr("Elipsa"),tr("Uzavřená spline"),tr("Upravit hranici")};
        edit_crop_=edit_crop;
        for(int mode=0;mode<4;++mode){auto* action=crop_menu->addAction(crop_labels[mode]);action->setObjectName(QString("drawingCropMode%1").arg(mode));connect(action,&QAction::triggered,this,[this,edit_crop,mode]{edit_crop(this,values(),mode,{});});}
        auto* remove_crop=crop_menu->addAction(tr("Odstranit omezení"),this,[this]{set_crop({});});remove_crop->setObjectName("drawingRemoveCrop");
        connect(crop_menu,&QMenu::aboutToShow,this,[this,crop_menu,remove_crop]{crop_menu->actions()[3]->setEnabled(value_.crop.has_value());remove_crop->setEnabled(value_.crop.has_value());});
        section_group->addWidget(section_label_,2,0);section_group->addWidget(components_,3,0,1,3);
        marker_table_=new QTableWidget(content);marker_table_->setObjectName("drawingSectionMarkers");marker_table_->setColumnCount(2);marker_table_->setHorizontalHeaderLabels({tr("Zobrazit trasy řezů"),tr("Omezení zobrazení")});marker_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);marker_table_->verticalHeader()->hide();section_group->addWidget(marker_table_,4,0,1,3);
        error_ = new QLabel(content); error_->setWordWrap(true);error_->hide();
        error_->setObjectName("drawingViewError");
        form->addWidget(error_);form->addStretch();
        auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setWidget(content);scroll->setMinimumHeight(420);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);content_layout()->addWidget(scroll);
        setMinimumWidth(760);set_initial_size({800,760});
        load_sections(value_.section_id);
        connect(source_,&QComboBox::currentIndexChanged,this,[this]{load_sections({});});
        connect(section_,&QComboBox::currentIndexChanged,this,[this]{set_section_components();preview_values();});
        components_->changed=[this]{preview_values();};
        connect(marker_table_,&QTableWidget::itemChanged,this,[this]{preview_values();});
        connect(section_label_,&QCheckBox::toggled,this,[this]{preview_values();});
        const auto preview_change = [this] { preview_values(); };
        zima::ui::bind_numeric_value_lock(x_,"x",value_.value_locks,preview_change);
        zima::ui::bind_numeric_value_lock(y_,"y",value_.value_locks,preview_change);
        zima::ui::bind_numeric_value_lock(scale_,"scale",value_.value_locks,preview_change);
        connect(orientation_,&QComboBox::currentIndexChanged,this,[this](int index){
            if(index>=0&&index<7){value_.orientation=static_cast<zima::drawing::ViewOrientation>(index);value_.camera=zima::drawing::standard_camera(value_.orientation);rotation_base_=value_.camera;
                for(auto* spin:rotation_values_){QSignalBlocker block(spin);spin->setValue(0);}preview_values();}
        });
        for (auto* combo : {source_, display_, scale_mode_,tangent_style_})
            connect(combo, &QComboBox::currentIndexChanged, this, [this,preview_change] {
                scale_->setEnabled(scale_mode_->currentIndex()==1);
                preview_change();
            });
        for (auto* spin : {scale_,x_,y_})
            connect(spin, &QDoubleSpinBox::valueChanged, this, preview_change);
        connect(name_, &QLineEdit::textChanged, this, preview_change);
        connect(caption_, &QCheckBox::toggled, this, preview_change);
        setAttribute(Qt::WA_DeleteOnClose);
        if(!creating)initial_settings_=settings_key(values());
    }
    void move_preview(zima::drawing::Point2 position) {
        {QSignalBlocker x_block(x_),y_block(y_);x_->setValue(position.x);y_->setValue(position.y);}
        preview_values();
    }
    void set_crop(std::optional<drawing::ViewCrop> crop){value_.crop=std::move(crop);preview_values();}
    void set_hatch_crop(const std::string& section,std::optional<drawing::ViewCrop> crop){
        if(crop)value_.section_hatch_crops[section]=std::move(*crop);else value_.section_hatch_crops.erase(section);
        preview_values();
    }
    void set_breaks(std::vector<drawing::ViewBreak> breaks) {value_.breaks=std::move(breaks);preview_values();}
    void resume_preview() {preview_values();}
    void set_error(const QString& error) { error_->setText(error);error_->setVisible(!error.isEmpty()); }
    zima::drawing::DrawingView values() const {
        auto result = value_;
        result.name = name_->text().trimmed().toStdString();
        result.show_caption = caption_->isChecked();result.show_thread_leadins=thread_leadins_->isChecked();
        result.dimension_guide_count=guide_count_->value();result.show_dimension_guides=guides_->isChecked();result.dimension_guide_offset=guide_offset_->value();result.dimension_guide_spacing=guide_spacing_->value();
        result.show_section_label=section_label_->isChecked();
        result.tangent_edge_style=static_cast<zima::drawing::TangentEdgeStyle>(tangent_style_->currentIndex());
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
            zima::workspace::set_drawing_section_components(result,section,components_->values(),true);
        }
        return result;
    }
private:
    // Compare normalized controls, not their rounded presentation with the
    // higher-precision stored view. Merely opening and confirming must preserve
    // those stored values and must not create an Undo transaction.
    static std::string settings_key(const drawing::DrawingView& v) {
        const auto point=[](const auto& p){return nlohmann::json::array({p.x,p.y});};
        const auto crop=[&](const std::optional<drawing::ViewCrop>& c)->nlohmann::json {
            if(!c)return nullptr;
            nlohmann::json points=nlohmann::json::array();for(const auto& p:c->points)points.push_back(point(p));
            return {{"shape",static_cast<int>(c->shape)},{"anchor",point(c->anchor)},{"points",points}};
        };
        nlohmann::json breaks=nlohmann::json::array(),hatch=nlohmann::json::object();
        for(const auto& b:v.breaks)breaks.push_back({b.id,b.vertical,b.start,b.length,b.gap,static_cast<int>(b.mark)});
        for(const auto& [id,c]:v.section_hatch_crops)hatch[id]=crop(c);
        return nlohmann::json::array({v.name,v.source_document_id,document::path_to_utf8(v.source_path),
            static_cast<int>(v.orientation),v.camera.horizontal.x,v.camera.horizontal.y,v.camera.horizontal.z,
            v.camera.vertical.x,v.camera.vertical.y,v.camera.vertical.z,v.camera.depth.x,v.camera.depth.y,v.camera.depth.z,
            v.x,v.y,v.scale,v.use_sheet_scale,static_cast<int>(v.display_style),static_cast<int>(v.hidden_edge_style),
            static_cast<int>(v.tangent_edge_style),v.show_thread_leadins,v.show_caption,v.show_section_label,
            v.show_dimension_guides,v.dimension_guide_count,v.dimension_guide_offset,v.dimension_guide_spacing,
            v.value_locks,v.section_id,document::serialize_sections(v.section_markers),
            document::serialize_sections(v.section_snapshot?std::vector{*v.section_snapshot}:std::vector<document::SectionDefinition>{}),
            v.hidden_hatch_components,breaks,crop(v.crop),hatch}).dump();
    }
    std::optional<std::string> initial_settings_;
    zima::drawing::DrawingView value_;
    std::vector<DrawingSourceChoice> sources_;
    std::function<std::vector<zima::document::SectionDefinition>(const std::string&,const std::filesystem::path&)> sections_;
    std::vector<zima::document::SectionDefinition> available_sections_;
    QSpinBox* guide_count_{};QCheckBox* guides_{};QDoubleSpinBox *guide_offset_{},*guide_spacing_{};
    QComboBox *section_{};QCheckBox *section_label_{};
    std::vector<QPushButton*> rotation_buttons_;
    std::array<QDoubleSpinBox*,3> rotation_values_{};
    drawing::ProjectionCamera rotation_base_;
    QTableWidget* marker_table_{};
    std::function<void(ViewPropertiesDialog*,drawing::DrawingView,int,std::string)> edit_crop_;
    void update_rotation_controls(){const bool enabled=value_.parent_view_id.empty();orientation_->setEnabled(enabled);for(auto* button:rotation_buttons_)button->setEnabled(enabled);for(auto* spin:rotation_values_)spin->setEnabled(enabled);}
    SectionComponentsWidget* components_{};
    void load_sections(const std::string& selected){
        QSignalBlocker block(section_);section_->clear();section_->addItem(tr("Bez řezu"),QString{});available_sections_.clear();
        try{const auto i=source_->currentIndex();if(i>=0&&sources_[i].evaluated)available_sections_=sections_(sources_[i].id,sources_[i].path);
            for(const auto& s:available_sections_)section_->addItem(QString::fromStdString(s.name),QString::fromStdString(s.id));
            auto index=section_->findData(QString::fromStdString(selected));
            if(index<0&&!selected.empty()){section_->addItem(tr("Chybějící řez"),QString::fromStdString(selected));index=section_->count()-1;}
            section_->setCurrentIndex(std::max(0,index));set_section_components();
            QSignalBlocker markers_block(marker_table_);marker_table_->setRowCount(0);
            for(const auto& s:available_sections_){const auto row=marker_table_->rowCount();marker_table_->insertRow(row);auto* item=new QTableWidgetItem(QString::fromStdString(s.name));item->setData(Qt::UserRole,QString::fromStdString(s.id));item->setFlags((item->flags()&~Qt::ItemIsEditable)|Qt::ItemIsUserCheckable);item->setCheckState(std::ranges::any_of(value_.section_markers,[&](const auto& saved){return saved.id==s.id;})?Qt::Checked:Qt::Unchecked);marker_table_->setItem(row,0,item);
                auto* button=new QPushButton(tr("Omezit šrafování…"),marker_table_);button->setObjectName("drawingHatchCrop_"+QString::fromStdString(s.id));
                auto* menu=new QMenu(button);button->setMenu(menu);const auto id=s.id;
                const std::array<QString,4> labels{tr("Kruh"),tr("Elipsa"),tr("Uzavřená spline"),tr("Upravit hranici")};
                for(int mode=0;mode<4;++mode)connect(menu->addAction(labels[mode]),&QAction::triggered,this,[this,id,mode]{auto pending=values();pending.section_id=id;edit_crop_(this,std::move(pending),mode,id);});
                auto* remove=menu->addAction(tr("Odstranit omezení"),this,[this,id]{set_hatch_crop(id,{});});
                connect(menu,&QMenu::aboutToShow,this,[this,id,menu,remove]{const bool exists=value_.section_hatch_crops.contains(id);menu->actions()[3]->setEnabled(exists);remove->setEnabled(exists);});
                marker_table_->setCellWidget(row,1,button);
            }
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
    std::function<void(std::optional<zima::drawing::DrawingView>)> preview_;
    void preview_values() {
        const int i=source_->currentIndex();
        if(i>=0&&!sources_[i].evaluated){preview_({});set_error(tr("Varianta se vypočítá po potvrzení OK."));return;}
        preview_(values());
    }
    QLineEdit* name_{};
    QCheckBox* caption_{};QCheckBox* thread_leadins_{};
    QComboBox *source_{}, *orientation_{}, *display_{}, *scale_mode_{}, *tangent_style_{};
    QDoubleSpinBox *scale_{}, *x_{}, *y_{};
    QLabel* error_{};
    bool submit() override {
        auto accepted=values();
        if(initial_settings_&&*initial_settings_==settings_key(accepted))return true;
        return accepted_(std::move(accepted));
    }
};

class SheetPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    SheetPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingSheet initial,
                          std::function<void(zima::drawing::DrawingSheet)> accepted)
        : PropertiesSubWindow(QObject::tr("List"), parent),
          value_(std::move(initial)), accepted_(std::move(accepted)) {
        setObjectName("drawingSheetProperties");
        auto* content = new QWidget(this); auto* form = new QFormLayout(content);
        format_ = new QComboBox(content);
        for (const auto& name : {"A4", "A3", "A2", "A1", "A0"}) format_->addItem(name);
        format_->setObjectName("drawingSheetFormat");format_->setCurrentIndex(static_cast<int>(value_.format));
        projection_ = new QComboBox(content);
        projection_->addItem(QObject::tr("První kvadrant"), 0);
        projection_->addItem(QObject::tr("Třetí kvadrant"), 1);
        projection_->setCurrentIndex(value_.projection_method == zima::drawing::ProjectionMethod::FirstAngle ? 0 : 1);
        scale_ = new QDoubleSpinBox(content); scale_->setRange(0.001, 1000.0);
        scale_->setObjectName("drawingSheetScale");scale_->setDecimals(3); scale_->setValue(value_.default_scale);
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
        value_.default_scale = scale_->value();try{accepted_(value_);}catch(const std::exception& e){throw std::runtime_error(tr(e.what()).toStdString());}return true;
    }
};

class TitleBlockPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    TitleBlockPropertiesDialog(QMainWindow* parent,
        const zima::workspace::DrawingTitleEdit& edit,
        const zima::drawing::DrawingSheet& sheet,
        std::function<void(const std::map<std::string,std::string>&)> accepted)
        : PropertiesSubWindow(QObject::tr("Hodnoty razítka"),parent), accepted_(std::move(accepted)) {
        setObjectName("drawingTitleBlockProperties");
        auto fields=edit.fields;const auto& context=edit.context;
        const auto is_parameter=[&edit](const auto& field){return workspace::drawing_title_field_is_source_parameter(field,edit);};
        // prepare_drawing_title_edit already follows the source parameter order.
        // Keep that order while separating sheet-owned values below the rule.
        std::stable_partition(fields.begin(),fields.end(),is_parameter);
        auto* content=new QWidget(this); auto* form=new QFormLayout(content);
        std::optional<bool> previous_group;
        for(const auto& field:fields) {
            const bool parameter=is_parameter(field);
            if(!previous_group||*previous_group!=parameter) {
                if(previous_group){auto* rule=new QFrame(content);rule->setObjectName("titleBlockValuesSeparator");rule->setFrameShape(QFrame::HLine);form->addRow(rule);}
                auto* heading=new QLabel(parameter?tr("Parametry souboru"):tr("Hodnoty razítka"),content);
                heading->setObjectName(parameter?"titleBlockParametersHeading":"titleBlockLocalHeading");
                auto font=heading->font();font.setBold(true);heading->setFont(font);form->addRow(heading);previous_group=parameter;
            }
            const auto value=zima::drawing::resolve_title_block_text(field,context,sheet);
            auto* editor=new QLineEdit(QString::fromStdString(value),content);
            editor->setObjectName(QString::fromStdString("titleBlockField:"+field.id));
            const auto tokens=zima::drawing::title_block_tokens(field.expression);
            const bool writable=zima::workspace::drawing_title_field_writable(field,edit);
            editor->setReadOnly(!writable);
            if(writable) {editors_[field.id]=editor;initial_[field.id]=value;}
            auto label=field.id;
            if(tokens.size()==1 && zima::drawing::title_block_token_scope(tokens.front())=="model") {
                const auto key=zima::drawing::title_block_parameter_key(tokens.front(),context);
                const auto labels=context.parameter_labels.find(key);
                label=key;
                if(labels!=context.parameter_labels.end())if(const auto value=labels->second.find(sheet.title_block_locale);value!=labels->second.end()&&!value->second.empty())label=value->second;
            }
            const auto option=[&](const std::string& key,const std::string& fallback){const auto it=field.action_settings.find(key);return QString::fromStdString(it==field.action_settings.end()?fallback:it->second);};
            const auto kind=option("kind","none");
            if(kind=="today") {
                auto* row=new QWidget(content);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->addWidget(editor);
                auto* today=new QPushButton(QObject::tr("Dnešní datum"),row);today->setObjectName(QString::fromStdString("titleBlockToday:"+field.id));today->setEnabled(writable);layout->addWidget(today);
                connect(today,&QPushButton::clicked,this,[editor,format=option("date_format","dd.MM.yyyy")]{editor->setText(QDate::currentDate().toString(format));});
                form->addRow(QString::fromStdString(label),row);
            } else if(kind=="list") {
                auto* choices=new QComboBox(content);choices->setObjectName(QString::fromStdString("titleBlockChoices:"+field.id));
                QStringList options;for(const auto& item:QJsonDocument::fromJson(option("choices","[]").toUtf8()).array())options.push_back(item.toString());for(auto& value:options)value=value.trimmed();options.removeAll("");options.removeDuplicates();choices->addItems(options);
                choices->setEditable(option("allow_custom","yes")=="yes");choices->setEnabled(writable);
                const auto current=QString::fromStdString(value);if(!choices->isEditable()&&choices->findText(current)<0)choices->insertItem(0,current);
                choices->setCurrentText(current);editor->hide();
                connect(choices,&QComboBox::currentTextChanged,editor,&QLineEdit::setText);
                form->addRow(QString::fromStdString(label),choices);
            } else form->addRow(QString::fromStdString(label),editor);
        }
        error_=new QLabel(this);error_->setWordWrap(true);error_->setObjectName("titleBlockError");
        content_layout()->addWidget(content);content_layout()->addWidget(error_);
        set_initial_size({3 * std::max(minimumWidth(), minimumSizeHint().width()) / 2, sizeHint().height()});
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
        } catch(const std::exception& error) {error_->setText(QObject::tr(error.what()));return false;}
    }
};

using zima::workspace::projection_placement;

// Placement uses already stored bounds data and never loads source geometry.
// A new view receives its authoritative geometry only when OK commits it.
QRectF placement_frame(const zima::drawing::DrawingView& view) {
    std::optional<zima::drawing::Point2> low,high;
    const auto include=[&](zima::kernel::Vec3 p) {
        const zima::drawing::Point2 q{zima::kernel::dimension_dot(p,view.camera.horizontal),zima::kernel::dimension_dot(p,view.camera.vertical)};
        if(!low){low=high=q;return;}
        low->x=std::min(low->x,q.x);low->y=std::min(low->y,q.y);
        high->x=std::max(high->x,q.x);high->y=std::max(high->y,q.y);
    };
    if(view.output_source)for(auto p:view.output_source->vertices)include(p);
    if(!low&&view.measurement_geometry)for(const auto& curve:view.measurement_geometry->curves)for(auto p:curve.points)include(p);
    // The first unsized view uses a provisional frame. No source is loaded just
    // to move a rectangle; OK determines its authoritative geometry and extent.
    return low?QRectF(QPointF(low->x,low->y),QPointF(high->x,high->y)):QRectF(-20,-15,40,30);
}

std::pair<std::string, zima::kernel::ViewerMesh> load_drawing_source(
    const std::filesystem::path& path, zima::workspace::Workspace* workspace = nullptr,
    const std::string& expected_document_id = {}) {
    try{return zima::workspace::read_drawing_source(workspace,path,expected_document_id);}
    catch(const std::exception& e){throw std::runtime_error(QObject::tr(e.what()).toStdString());}
}


}  // namespace

class DrawingGpuSurface final : public QOpenGLWidget {
    std::unique_ptr<DrawingSheetSurface> capture_surface_;
public:
    std::function<void(QPainter&)> paint;
    explicit DrawingGpuSurface(QWidget* parent):QOpenGLWidget(parent) {
        setObjectName("drawingGpuSurface");setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void paintGL() override {
        QPainter painter(this);if(!paint)return;
        // QWidget::grab redirects this painter to a raster target. Keep the
        // sheet on the GPU instead of replaying every model stroke on the CPU.
        if(painter.paintEngine()->type()!=QPaintEngine::OpenGL2) {
            if(!capture_surface_)capture_surface_=std::make_unique<DrawingSheetSurface>();
            const auto ratio=devicePixelRatioF();
            const auto image=capture_surface_->render(size()*ratio,ratio,paint);
            if(!image.isNull()){painter.drawImage(QPointF{},image);return;}
        }
        paint(painter);
    }
};
class DrawingCanvas final : public QWidget, public SheetRenderer {
    DrawingGpuSurface* gpu_surface_{};
    QImage placement_background_;
    std::unique_ptr<DrawingSheetSurface> placement_surface_;
    double placement_background_zoom_{};
    QPointF placement_background_origin_;
    bool align_mode_{};
    std::string align_first_;
    std::vector<std::string> alignment_selection()const {
        std::vector<std::string> result;
        for(const auto& key:entity_selection_){if(key.kind!=AnnotationKind::Dimension)return {};result.push_back(key.id);}
        return result;
    }
    void finish_alignment(const std::vector<std::string>& ids) {
        if(!sheet_)return;
        if(workspace::align_drawing_dimensions(*sheet_,ids)&&changed_)changed_();
        align_mode_=false;align_first_.clear();hovered_annotation_.reset();offered_annotations_.clear();update();
    }
    std::function<void(const std::string&)> choose_view_;
    QPointF chosen_view_point_;
    std::function<void()> choose_view_canceled_;
    std::optional<AnnotationHandle> dragged_model_;
    QPointF model_drag_start_;bool model_moved_{};
    std::optional<drawing::ModelAnnotation> model_drag_original_;
    std::vector<AnnotationHandle> offered_annotations_;
    std::size_t offered_annotation_index_{};
    std::optional<AnnotationHandle> dragged_section_end_;
    QPointF section_end_drag_start_;
    bool section_end_moved_{};
    void offer_annotations(QPointF point){
        std::vector<AnnotationHandle> candidates;
        for(auto it=annotation_handles_.rbegin();it!=annotation_handles_.rend();++it)if((!model_pick_||(it->key.kind==AnnotationKind::Model&&preview_&&it->key.view==preview_->id&&model_offered_.contains(it->key.id)))&&(it->hit.contains(point)||QLineF(point,it->point).length()<=8))candidates.push_back(*it);
        if(align_mode_) {
            std::erase_if(candidates,[&](const auto& h){return h.key.kind!=AnnotationKind::Dimension||!sheet_||
                (align_first_.empty()?!workspace::free_alignment_dimension(*sheet_,h.key.id):!workspace::can_align_drawing_dimensions(*sheet_,{align_first_,h.key.id}));});
        }
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){const auto da=QLineF(point,a.point).length(),db=QLineF(point,b.point).length();if((da<=8)!=(db<=8))return da<=8;return da<=8&&db<=8&&da<db;});
        if(model_pick_){std::set<std::pair<std::string,std::string>> entities;std::erase_if(candidates,[&](const auto& c){return !entities.emplace(c.key.view,c.key.id).second;});}
        if(align_mode_){std::set<std::string> ids;std::erase_if(candidates,[&](const auto& c){return !ids.insert(c.key.id).second;});}
        bool same=candidates.size()==offered_annotations_.size();for(std::size_t i=0;same&&i<candidates.size();++i)same=candidates[i].key==offered_annotations_[i].key;
        if(!same)offered_annotation_index_=0;offered_annotations_=std::move(candidates);
        hovered_annotation_=offered_annotations_.empty()?std::optional<AnnotationKey>{}:offered_annotations_[offered_annotation_index_].key;
    }
    bool ordinary_selection() const {
        return sheet_&&!align_mode_&&!witness_tool_&&!crop_edit_&&!preview_&&!placed_&&!choose_view_&&!dimension_command_&&!dimension_mode_&&!balloon_command_&&!text_editor_&&!model_pick_;
    }
    void select_entity(std::optional<AnnotationKey> key,bool toggle) {
        if(key&&key->kind==AnnotationKind::Dimension&&key->end%3==1&&sheet_) {
            const auto found=std::ranges::find(sheet_->dimensions,key->id,&drawing::DrawingDimension::id);
            if(found!=sheet_->dimensions.end()&&!found->chain_group.empty()) {
                const bool remove=toggle&&entity_selected(*key);
                if(!toggle)entity_selection_.clear();
                for(const auto& member:sheet_->dimensions)if(member.view_id==found->view_id&&member.chain_group==found->chain_group) {
                    AnnotationKey item{AnnotationKind::Dimension,member.view_id,member.id,0};
                    std::erase(entity_selection_,item);if(!remove)entity_selection_.push_back(item);
                }
                setProperty("drawingSelectionCount",int(entity_selection_.size()));return;
            }
        }
        if(!toggle)entity_selection_.clear();
        if(key){key->end=0;const auto it=std::ranges::find(entity_selection_,*key);
            if(it==entity_selection_.end())entity_selection_.push_back(*key);else entity_selection_.erase(it);}
        setProperty("drawingSelectionCount",int(entity_selection_.size()));
    }
    void erase_selection() {
        if(!ordinary_selection()||entity_selection_.empty())return;
        // Whole views require the explicit Delete View command.
        const auto selected=entity_selection_;
        if(std::ranges::all_of(selected,[](const auto& key){return key.kind==AnnotationKind::View;}))return;
        for(const auto& key:selected){
            if(key.kind==AnnotationKind::View)continue;
            if(key.kind==AnnotationKind::Text){std::erase_if(sheet_->texts,[&](const auto& t){return "text:"+t.id==key.id;});continue;}
            if(key.kind==AnnotationKind::Dimension){
                auto dimension=std::ranges::find(sheet_->dimensions,key.id,&drawing::DrawingDimension::id);
                const auto view=std::ranges::find(sheet_->views,key.view,&drawing::DrawingView::id);
                if(dimension!=sheet_->dimensions.end()&&!key.branch.empty()&&view!=sheet_->views.end()) {
                    drawing::erase_dimension_branch(*view,*dimension,key.branch);
                    if(dimension->segments.empty())sheet_->dimensions.erase(dimension);
                }else workspace::erase_drawing_dimension(*sheet_,key.id);
                continue;
            }
            if(key.kind==AnnotationKind::Balloon){std::erase_if(sheet_->balloons,[&](const auto& b){return b.id==key.id;});continue;}
            for(auto& view:sheet_->views)if(view.id==key.view){
                if(key.kind==AnnotationKind::Model)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==key.id)item.visible=false;
                if(key.kind==AnnotationKind::Caption)view.show_caption=false;
                if(key.kind==AnnotationKind::SectionLabel)view.show_section_label=false;
                if(key.kind==AnnotationKind::DetailLabel)view.show_detail_label=false;
                if(key.kind==AnnotationKind::SectionEnd){std::erase_if(view.section_markers,[&](const auto& marker){return marker.id==key.id;});view.section_marker_offsets.erase(key.id);}
            }
        }
        select_entity({},false);selected_.clear();selected_field_.clear();selected_dimension_id_.clear();selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear();
        if(changed_)changed_();if(selection_changed_)selection_changed_();update();
    }
#include "drawing_crop_canvas.inc"
#include "drawing_witness_canvas.inc"
    std::string snap_view_;
    std::optional<drawing::Point2> snap_point_;
    std::optional<drawing::AnnotationGuide> snap_line_;
    void clear_annotation_snap() {
        snap_view_.clear();snap_point_.reset();snap_line_.reset();
        setProperty("annotationSnapActive",false);setProperty("annotationSnapView",QString{});
    }
    drawing::Point2 snap_annotation_point(const drawing::DrawingView& view,drawing::Point2 point) {
        snap_view_=view.id;
        const auto snapped=drawing::snap_annotation(view,point,6/canvas_zoom());
        snap_line_=snapped.guide;snap_point_=snapped.guide?std::optional(snapped.point):std::nullopt;
        setProperty("annotationSnapActive",snap_point_.has_value());
        setProperty("annotationSnapView",QString::fromStdString(view.id));
        return snapped.point;
    }
    QPointF snap_measurement_point(const drawing::DrawingView& view,QPointF position) {
        const auto p=measurement_point(view,position);
        const auto snapped=snap_annotation_point(view,{p.x*view.scale,p.y*view.scale});
        return {snapped.x/view.scale,snapped.y/view.scale};
    }
public:
#include "drawing_balloon_canvas.inc"
public:
    explicit DrawingCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 480); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
        auto drawing_font = font();
        drawing_font.setFamily(drawing_font_family());
        drawing_font.setWeight(QFont::Normal);
        setFont(drawing_font);
        auto canvas_palette = palette();
        canvas_palette.setColor(QPalette::Window, QColor("#000000"));
        setPalette(canvas_palette);
        setAttribute(Qt::WA_OpaquePaintEvent);
        // Headless test platforms cannot host QOpenGLWidget. Geometry depth
        // rendering still uses an offscreen context where one is available.
        if(QGuiApplication::platformName()!="offscreen"&&QGuiApplication::platformName()!="minimal") {
            gpu_surface_=new DrawingGpuSurface(this);gpu_surface_->setGeometry(rect());
            gpu_surface_->paint=[this](QPainter& painter){paint_canvas(painter);};
        }
    }
    void update(){if(gpu_surface_)gpu_surface_->update();else QWidget::update();}
    void set_sheet(zima::drawing::DrawingSheet* sheet) {
        align_mode_=false;align_first_.clear();
        cancel_witness();witness_handles_.clear();
        select_entity({},false);clear_annotation_snap();sheet_ = sheet;set_render_sheet(sheet);shaded_cache_.clear();annotation_handles_.clear();offered_annotations_.clear();selected_annotation_.reset();hovered_annotation_.reset();dragged_section_end_.reset();
        selected_.clear();selected_field_.clear();hovered_field_.clear();field_regions_.clear();title_targets_.clear();
        selected_dimension_id_.clear();
        dragged_dimension_id_.clear();
        drag_view_id_.clear();
        dimension_mode_ = false;
        update();
    }
    [[nodiscard]] const std::string& selected_view_id() const { return selected_; }
    [[nodiscard]] const std::string& selected_dimension_id() const { return selected_dimension_id_; }
    void select_view_for_test(const std::string& view_id) {
        select_entity(AnnotationKey{AnnotationKind::View,view_id,{},0},false);selected_ = view_id; selected_dimension_id_.clear();selected_annotation_.reset();
        if (selection_changed_) selection_changed_();
        update();
    }
    void set_title_block_context(std::optional<zima::drawing::TitleBlockContext> context) {
        title_block_context_ = std::move(context);
        layout_cache_.reset();
        update();
    }
    void set_changed_callback(std::function<void()> callback) { changed_=std::move(callback); }
    void set_selection_changed_callback(std::function<void()> callback) {
        selection_changed_ = std::move(callback);
    }
    void set_preview_move_handler(std::function<void(zima::drawing::Point2)> handler) {
        preview_move_=std::move(handler);preview_dragging_=false;
    }
    void set_staged_model_previews(const std::vector<drawing::DrawingView>& views) {
        staged_model_previews_.clear();
        for(const auto& view:views)staged_model_previews_[view.id]=view;
        update();
    }
    void set_placement_frame(QRectF bounds){preview_frame_bounds_=bounds;}
    void set_preview(std::optional<zima::drawing::DrawingView> view,bool frame_only=false) {
        if(view)select_entity({},false);if(!view)preview_dragging_=false;
        if(preview_)shaded_cache_.erase(preview_->id);
        if(view)shaded_cache_.erase(view->id);
        preview_frame_bounds_=view&&frame_only?std::optional{placement_frame(*view)}:std::nullopt;
        preview_ = std::move(view); update();
    }
    void begin_placement(zima::drawing::DrawingView view,
        std::function<void(zima::drawing::DrawingView)> placed,
        std::function<void()> canceled,
        std::function<void(zima::drawing::DrawingView&, zima::drawing::Point2)> position = {}) {
        start_selection(); selected_.clear(); selected_dimension_id_.clear();
        if (selection_changed_) selection_changed_();
        preview_ = std::move(view); placed_ = std::move(placed);
        preview_frame_bounds_=placement_frame(*preview_);
        placement_background_={};
        canceled_ = std::move(canceled); position_ = std::move(position);
        setCursor(Qt::ArrowCursor); setFocus(); update();
    }
    void cancel_placement() {
        placed_ = {}; position_ = {}; preview_.reset();preview_frame_bounds_.reset(); unsetCursor();
        auto callback = std::move(canceled_); canceled_ = {};
        if (callback) callback();
        update();
    }
    void set_title_block_action(QAction* action) { title_action_=action; }
    void set_text_properties(std::function<void(const std::string&)> callback) {text_properties_=std::move(callback);}
    void set_text_editor(SketchTextPropertiesDialog* dialog) {
        if(dialog)select_entity({},false);text_editor_=dialog;
        if(dialog&&dialog->needs_anchor()&&sheet_) {
            const auto point=paper_point(mapFromGlobal(QCursor::pos()));dialog->set_preview_anchor(point.x,point.y);
        }
        update();
    }
    void set_text_preview(std::optional<drawing::DrawingText> text) {text_preview_=std::move(text);update();}
    drawing::DrawingText* editable_text(const std::string& field) {
        if(!sheet_||!field.starts_with("text:"))return nullptr;
        const auto it=std::ranges::find(sheet_->texts,field.substr(5),&drawing::DrawingText::id);
        return it==sheet_->texts.end()?nullptr:&*it;
    }
    drawing::Point2 paper_point(QPointF point) const {
        const auto zoom=canvas_zoom();const auto origin=canvas_origin(zoom);
        return {sheet_->width_mm()-(point.x()-origin.x())/zoom,sheet_->height_mm()-(point.y()-origin.y())/zoom};
    }
    void erase_text(const std::string& field) {
        if(!editable_text(field))return;
        std::erase_if(sheet_->texts,[&](const auto& text){return "text:"+text.id==field;});
        select_entity({},false);selected_field_.clear();hovered_field_.clear();if(changed_)changed_();update();
    }
    const std::string& selected_title_field() const { return selected_field_; }
    std::optional<zima::drawing::TitleBlockTextTarget> selected_title_target() const {
        const auto found=title_targets_.find(selected_field_);return found==title_targets_.end()?std::nullopt:std::optional{found->second};
    }
    std::optional<QPointF> title_field_center(const std::string& id) const {
        for(const auto& [key,polygon]:field_regions_)if(key==id)return polygon.boundingRect().center();
        return {};
    }
    std::optional<std::string> title_field_text(const std::string& id) const {
        if(!sheet_||!title_block_context_)return {};
        const auto field=std::ranges::find(sheet_->title_block_fields,id,&zima::drawing::TitleBlockField::id);
        if(field==sheet_->title_block_fields.end())return {};
        return zima::drawing::resolve_title_block_text(*field,*title_block_context_,*sheet_);
    }
    std::string field_at(QPointF point) const {
        for(auto it=field_regions_.rbegin();it!=field_regions_.rend();++it)
            if(it->second.containsPoint(point,Qt::OddEvenFill))return it->first;
        return {};
    }
    void set_context_actions(QAction* insert, QAction* edit, QAction* projected, QAction* remove) {
        insert_action_=insert; edit_action_=edit; projected_action_=projected; remove_action_=remove;
    }
    std::function<void(const std::string&,const std::string&,QPointF)> dimension_value_;
    std::string tree_selection_id() const {
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension)
            return tree_id(*selected_annotation_);
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Model)
            return selected_annotation_->view+":"+selected_annotation_->id;
        return selected_;
    }
    static std::string tree_id(const AnnotationKey& key) {
        switch(key.kind) {
        case AnnotationKind::View:return key.view;
        case AnnotationKind::Model:return key.view+":"+key.id;
        case AnnotationKind::Dimension:return "drawing-dimension:"+key.id+(key.branch.empty()?"":":branch:"+key.branch);
        case AnnotationKind::Text:return key.id;
        case AnnotationKind::Balloon:return "drawing-balloon:"+key.id;
        case AnnotationKind::Caption:return "drawing-caption:"+key.view;
        case AnnotationKind::SectionLabel:return "drawing-section-label:"+key.view;
        case AnnotationKind::DetailLabel:return "drawing-detail-label:"+key.view;
        case AnnotationKind::SectionEnd:return "drawing-section-end:"+key.view+":"+key.id;
        }
        return {};
    }
    static std::vector<AnnotationKey> sheet_entities(const drawing::DrawingSheet& sheet) {
        std::vector<AnnotationKey> keys;
        for(const auto& view:sheet.views) {
            keys.push_back({AnnotationKind::View,view.id,{},0});
            for(const auto& item:view.model_annotations)if(item.visible&&!drawing::origin_annotation(item.source))
                keys.push_back({AnnotationKind::Model,view.id,model_annotation_key(item.source),0});
            if(view.show_caption)keys.push_back({AnnotationKind::Caption,view.id,{},0});
            if(view.detail_view&&view.show_detail_label)keys.push_back({AnnotationKind::DetailLabel,view.id,view.parent_view_id,0});
            if(view.show_section_label&&!view.section_id.empty())keys.push_back({AnnotationKind::SectionLabel,view.id,{},0});
            for(const auto& marker:view.section_markers)keys.push_back({AnnotationKind::SectionEnd,view.id,marker.id,0});
        }
        for(const auto& item:sheet.dimensions){
            keys.push_back({AnnotationKind::Dimension,item.view_id,item.id,0});
            if(item.kind==drawing::DrawingDimensionKind::Chain&&item.chain_group.empty()&&!item.chain_datum_only)for(std::size_t i=0;i<item.segments.size();++i)
                keys.push_back({AnnotationKind::Dimension,item.view_id,item.id,0,item.segments[i].id});
        }
        for(const auto& item:sheet.texts)keys.push_back({AnnotationKind::Text,{},"text:"+item.id,0});
        for(const auto& item:sheet.balloons)if(item.visible)keys.push_back({AnnotationKind::Balloon,item.view_id,item.id,0});
        return keys;
    }
    std::vector<std::string> tree_selection_ids() const {
        std::vector<std::string> result;
        for(const auto& key:entity_selection_)if(auto id=tree_id(key);!id.empty())result.push_back(std::move(id));
        if(result.empty())if(auto id=tree_selection_id();!id.empty())result.push_back(std::move(id));
        return result;
    }
    bool accepts_tree_selection() const {return ordinary_selection();}
    void select_tree_entities(const std::vector<std::string>& ids) {
        if(!ordinary_selection())return;
        entity_selection_.clear();selected_.clear();selected_field_.clear();selected_dimension_id_.clear();selected_annotation_.reset();
        hovered_.clear();hovered_annotation_.reset();offered_annotations_.clear();
        const auto entities=sheet_entities(*sheet_);
        for(const auto& id:ids)for(const auto& key:entities)if(tree_id(key)==id){entity_selection_.push_back(key);break;}
        setProperty("drawingSelectionCount",int(entity_selection_.size()));
        if(entity_selection_.size()==1) {
            const auto& key=entity_selection_.front();
            if(key.kind==AnnotationKind::View)selected_=key.view;
            else if(key.kind==AnnotationKind::Text)selected_field_=key.id;
            else {selected_annotation_=key;if(key.kind==AnnotationKind::Dimension)selected_dimension_id_=key.id;}
        }
        if(selection_changed_)selection_changed_();update();
    }
    void erase_selected_entities() {erase_selection();}
    void edit_manual_entity(const AnnotationKey& key) {
        int segment=0;
        if(const auto* dimension=visible_dimension(key.id);dimension&&!key.branch.empty()) {
            const auto found=std::ranges::find(dimension->segments,key.branch,&drawing::DrawingDimensionSegment::id);
            if(found!=dimension->segments.end())segment=int(found-dimension->segments.begin());
        }
        if(manual_properties_)manual_properties_(key.id,0);
        if(auto* field=window()->findChild<QComboBox*>("drawingDimensionSegment"))field->setCurrentIndex(segment);
    }
    void populate_selection_menu(QMenu& menu) {
        if(!ordinary_selection()||entity_selection_.empty())return;
        const auto align_ids=alignment_selection();
        if(workspace::can_align_drawing_dimensions(*sheet_,align_ids))
            menu.addAction(resource_icon("drawing-align"),tr("Zarovnat kóty"),this,[this,align_ids]{finish_alignment(align_ids);})->setObjectName("drawingAlignDimensionsContextAction");
        if(entity_selection_.size()==1) {
            const auto key=entity_selection_.front();
            if(key.kind==AnnotationKind::View){menu.addAction(edit_action_);menu.addAction(projected_action_);menu.addAction(remove_action_);return;}
            QAction* properties=nullptr;
            if(key.kind==AnnotationKind::Text)properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti textu…"),this,[this,key]{if(text_properties_)text_properties_(key.id.substr(5));});
            if(key.kind==AnnotationKind::Balloon)properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti pozice…"),this,[this,key]{if(balloon_properties_)balloon_properties_(key.id);});
            if(key.kind==AnnotationKind::Dimension) {
                if(!key.branch.empty())menu.addAction(tr("Vybrat nadřazený"),this,[this,key]{select_manual(key.view,key.id);})->setObjectName("drawingSelectChainAction");
                properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti kóty…"),this,[this,key]{edit_manual_entity(key);});
                const auto* dimension=visible_dimension(key.id);
                if(dimension&&(dimension->kind==drawing::DrawingDimensionKind::Linear||dimension->kind==drawing::DrawingDimensionKind::Chain)) {
                    menu.addAction(resource_icon("drawing-jog"),tr("Vložit zalomení"),this,[this,key]{start_witness_tool(drawing::WitnessEditKind::Jog,key.id);})->setObjectName("drawingWitnessJogContextAction");
                    menu.addAction(resource_icon("drawing-break"),tr("Vložit přerušení"),this,[this,key]{start_witness_tool(drawing::WitnessEditKind::Break,key.id);})->setObjectName("drawingWitnessBreakContextAction");
                }
                if(dimension&&dimension->kind==drawing::DrawingDimensionKind::Linear)
                    menu.addAction(tr("Převést na řetězovou kótu…"),this,[this,key]{if(manual_properties_)manual_properties_(key.id,2);});
                if(dimension&&dimension->kind==drawing::DrawingDimensionKind::Chain) {
                    menu.addAction(tr("Převést na lineární kótu…"),this,[this,key]{if(manual_properties_)manual_properties_(key.id,4);})->setObjectName("convertDrawingLinearAction");
                    menu.addAction(tr("Přidat větev"),this,[this,key]{if(manual_properties_)manual_properties_(key.id,1);})->setObjectName("drawingAddChainBranchAction");
                }
            }
            if(key.kind==AnnotationKind::Caption||key.kind==AnnotationKind::SectionLabel||key.kind==AnnotationKind::DetailLabel)
                properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti…"),this,[this,key]{select_view_for_test(key.view);if(edit_action_)edit_action_->trigger();});
            if(key.kind==AnnotationKind::Model)for(const auto& view:sheet_->views)if(view.id==key.view)
                for(const auto& item:view.model_annotations)if(model_annotation_key(item.source)==key.id&&item.kind==drawing::ModelAnnotationKind::Dimension) {
                    properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti kóty…"),this,[this,key]{if(dimension_properties_)dimension_properties_(key.view,key.id);});
                    if(!item.unresolved&&item.model_dimension&&item.model_dimension->driving&&!item.model_dimension->locked&&item.model_dimension->display_text_override.empty())
                        menu.addAction(resource_icon("edit"),tr("Upravit hodnotu…"),this,[this,key]{if(dimension_value_)dimension_value_(key.view,key.id,model_handle(key.id,0,key.view).value_or(rect().center()));})->setObjectName("drawingEditDimensionValueAction");
                }
            if(properties)properties->setObjectName("drawingEntityPropertiesAction");
        }
        if(std::ranges::any_of(entity_selection_,[](const auto& key){return key.kind!=AnnotationKind::View;}))
            menu.addAction(tr("Odstranit"),this,[this]{erase_selection();})->setObjectName("drawingDeleteSelectionAction");
    }
    void select_manual(const std::string& view,const std::string& id) {
        select_entity(AnnotationKey{AnnotationKind::Dimension,view,id,0},false);selected_.clear();selected_dimension_id_=id;
        selected_annotation_=AnnotationKey{AnnotationKind::Dimension,view,id,0};
        if(selection_changed_)selection_changed_();update();
    }
    void select_model(const std::string& view,const std::string& key) {
        select_entity(AnnotationKey{AnnotationKind::Model,view,key,0},false);selected_=view;selected_dimension_id_.clear();
        selected_annotation_=AnnotationKey{AnnotationKind::Model,view,key,0};
        if(selection_changed_)selection_changed_();update();
    }
    void set_dimension_properties_callback(std::function<void(const std::string&,const std::string&)> callback){dimension_properties_=std::move(callback);}

    void set_manual_properties_callback(std::function<void(const std::string&,int)> callback){manual_properties_=std::move(callback);}
    void set_dimension_command(DrawingDimensionDialog* dialog){
        cancel_witness();
        if(dialog)select_entity({},false);dimension_command_=dialog;dimension_mode_=dialog!=nullptr;measurement_offered_.clear();measurement_index_=0;
        selected_annotation_.reset();selected_dimension_id_.clear();drag_view_id_.clear();hovered_annotation_.reset();offered_annotations_.clear();
        if(dialog){
            dialog->set_changed([this]{offer_measurements(measurement_pointer_);update();});
            selected_dimension_id_=dialog->value().id;
            if(sheet_&&std::ranges::any_of(sheet_->dimensions,[&](const auto& d){return d.id==selected_dimension_id_;})) {
                selected_.clear();selected_annotation_=AnnotationKey{AnnotationKind::Dimension,dialog->value().view_id,selected_dimension_id_,0};
            }
        }
        if(selection_changed_)selection_changed_();update();
    }
    drawing::Point2 measurement_point(const drawing::DrawingView& view,QPointF point)const{
        const auto origin=raw_view_origin(view);const double scale=canvas_zoom()*view.scale;
        return drawing::break_map(view,{(point.x()-origin.x())/scale,-(point.y()-origin.y())/scale},true);
    }
    void offer_measurements(QPointF point){
        measurement_pointer_=point;std::vector<OfferedMeasurement> candidates;
        if(dimension_command_&&dimension_command_->awaiting_chain_seed()&&sheet_) {
            offer_annotations(point);std::set<std::string> seeds;
            for(const auto& offered:offered_annotations_)if(offered.key.kind==AnnotationKind::Dimension)
                for(const auto& d:sheet_->dimensions)if(d.id==offered.key.id&&d.kind==drawing::DrawingDimensionKind::Chain&&seeds.insert(d.id).second)
                    candidates.push_back({d.view_id,{},d.id});
        }
        hovered_annotation_.reset();
        if(dimension_command_&&dimension_command_->entering()&&sheet_)for(const auto& view:sheet_->views){
            if(!dimension_command_->value().view_id.empty()&&dimension_command_->value().view_id!=view.id)continue;
            if(view.crop&&!drawing_render::crop_screen_path(view,raw_view_origin(view),canvas_zoom()*view.scale).contains(point))continue;
            const double scale=canvas_zoom()*view.scale;
            for(auto candidate:drawing::measurement_candidates(view,measurement_point(view,point),8/scale,dimension_command_->pick_request())){
                candidate.distance*=scale;candidates.push_back({view.id,std::move(candidate)});
            }
        }
        if(const auto id=balloon_reference_view();!id.empty()&&sheet_)for(const auto& view:sheet_->views)if(view.id==id){
            const double scale=canvas_zoom()*view.scale;
            drawing::MeasurementPickRequest request;request.mode=int(drawing::DimensionAttachmentKind::CurvePoint);
            for(auto candidate:drawing::measurement_candidates(view,measurement_point(view,point),8/scale,request))if(drawing::balloon_bom_row(*sheet_,view,candidate.attachment.reference)){
                candidate.distance*=scale;candidates.push_back({view.id,std::move(candidate)});
            }
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){if(a.chain_seed.empty()!=b.chain_seed.empty())return !a.chain_seed.empty();return drawing::measurement_candidate_precedes(a.candidate,b.candidate);});
        bool same=candidates.size()==measurement_offered_.size();
        for(std::size_t i=0;same&&i<candidates.size();++i)same=candidates[i].view==measurement_offered_[i].view&&candidates[i].chain_seed==measurement_offered_[i].chain_seed&&candidates[i].candidate.attachment==measurement_offered_[i].candidate.attachment;
        if(!same)measurement_index_=0;measurement_offered_=std::move(candidates);
        if(!measurement_offered_.empty()&&!measurement_offered_[measurement_index_].chain_seed.empty()) {
            const auto& seed=measurement_offered_[measurement_index_];hovered_annotation_=AnnotationKey{AnnotationKind::Dimension,seed.view,seed.chain_seed,0};
        }
    }
    drawing::DrawingDimension* editable_dimension(const std::string& id){
        if(dimension_command_)return nullptr;
        for(auto& d:sheet_->dimensions)if(d.id==id)return &d;return nullptr;
    }
    const drawing::DrawingDimension* visible_dimension(const std::string& id)const{
        if(dimension_command_&&dimension_command_->value().id==id)return &dimension_command_->value();
        for(const auto& d:sheet_->dimensions)if(d.id==id)return &d;return nullptr;
    }
    void store_presentation(drawing::DrawingDimension value){
        if(dimension_command_)dimension_command_->change_presentation(std::move(value));
        else if(auto* target=editable_dimension(value.id)){*target=value;workspace::synchronize_dimension_chain(*sheet_,value);}
        update();
    }
    void begin_manual_drag(const AnnotationHandle& candidate,QPointF pointer){
        if(const auto* d=visible_dimension(candidate.key.id)){
            dimension_drag_companions_.clear();
            if(!dimension_command_&&sheet_)for(const auto& item:sheet_->dimensions)
                if(item.id!=d->id&&entity_selected({AnnotationKind::Dimension,item.view_id,item.id,0}))dimension_drag_companions_.push_back(item);
            dragged_dimension_id_=d->id;dimension_drag_start_=pointer;dimension_drag_initial_=*d;dimension_drag_original_=*d;
            dimension_drag_handle_=candidate.key.end;dimension_grip_start_=candidate.point;
        }
    }

    void fit_sheet() { view_zoom_=1;view_pan_={};update(); }
    QRectF sheet_rectangle()const {
        if(!sheet_)return {};
        const auto zoom=canvas_zoom();
        return {canvas_origin(zoom),QSizeF(sheet_->width_mm()*zoom,sheet_->height_mm()*zoom)};
    }
    drawing::Point2 chosen_view_point(const drawing::DrawingView& view)const {const auto o=raw_view_origin(view);return {(chosen_view_point_.x()-o.x())/(canvas_zoom()*view.scale),(o.y()-chosen_view_point_.y())/(canvas_zoom()*view.scale)};}
    void choose_view(std::function<void(const std::string&)> pick,std::function<void()> canceled={}) { start_selection();choose_view_=std::move(pick);choose_view_canceled_=std::move(canceled);selected_.clear();if(selection_changed_)selection_changed_();update(); }
    void begin_witness_command(drawing::WitnessEditKind kind,const std::string& id={},const std::string& edit_id={}){
        start_witness_tool(kind,id);
        if(edit_id.empty())return;
        const auto* value=dimension_command_?&dimension_command_->value():visible_dimension(id);if(!value)return;
        for(const auto& handle:witness_handles_)if(handle.key.id==id) {
            const auto& edits=value->segments[handle.segment].witness_edits;
            const auto it=std::ranges::find(edits,edit_id,&drawing::WitnessEdit::id);
            if(it!=edits.end()&&it->side==handle.geometry.side){witness_active_=handle;witness_draft_=*value;witness_edit_index_=it-edits.begin();witness_first_ready_=true;return;}
        }
    }
    void end_witness_command(){cancel_witness();}
    std::optional<QPointF> witness_grip(const std::string& id,const std::string& edit,int end)const {
        for(const auto& handle:witness_handles_)if(handle.key.id==id)for(const auto& grip:handle.geometry.grips)if(grip.id==edit&&grip.end==end)return grip.point;
        return {};
    }
    void start_alignment(){start_selection();select_entity({},false);align_mode_=true;align_first_.clear();offered_annotations_.clear();hovered_annotation_.reset();setFocus();if(selection_changed_)selection_changed_();update();}
    void start_selection() { align_mode_=false;align_first_.clear();cancel_witness();choose_view_={};if(balloon_command_)balloon_command_->reject();if(dimension_command_)dimension_command_->reject();dimension_mode_=false;update(); }
    [[nodiscard]] bool dimension_mode() const { return dimension_mode_; }
    bool interacting() const { return bool(preview_); }
protected:
    QPointF raw_view_origin(const drawing::DrawingView& view)const{const auto zoom=canvas_zoom();const auto o=canvas_origin(zoom);return {o.x()+(sheet_->width_mm()-view.x)*zoom,o.y()+(sheet_->height_mm()-view.y)*zoom};}
    QPointF view_screen_point(const zima::drawing::DrawingView& view,
                            const zima::drawing::Point2& original) const {
        const auto point=drawing::break_map(view,original);
        const double zoom = canvas_zoom(); const auto origin = canvas_origin(zoom);
        return {origin.x() + (sheet_->width_mm() - view.x + point.x * view.scale)*zoom,
                origin.y() + (sheet_->height_mm() - view.y - point.y * view.scale)*zoom};
    }
    QRectF view_bounds(const zima::drawing::DrawingView& view) const {
        return view_bounds_at(view,canvas_zoom(),canvas_origin(canvas_zoom())).adjusted(-8,-8,8,8);
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
    std::optional<QPointF> detail_label_handle(const std::string& id)const{
        for(const auto& handle:annotation_handles_)if(handle.key.kind==AnnotationKind::DetailLabel&&handle.key.view==id)return handle.point;return {};
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
            if (view_bounds(*it).contains(point)&&(!it->crop||drawing_render::crop_screen_path(*it,raw_view_origin(*it),canvas_zoom()*it->scale).contains(point))) return it->id;
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
        if(align_mode_){if(!offered_annotations_.empty()){offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;}update();event->accept();return;}
        if(witness_tool_){if(witness_owner_.empty()&&*witness_tool_==drawing::WitnessEditKind::Break){if(!offered_annotations_.empty()){offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;}}else if(!witness_offered_.empty())witness_offer_index_=(witness_offer_index_+1)%witness_offered_.size();update();event->accept();return;}
        if(crop_edit_){event->accept();return;}
        if(ordinary_selection()&&!entity_selection_.empty()) {
            const auto field=field_at(event->pos());offer_annotations(event->pos());
            const bool on_selection=entity_selected({AnnotationKind::Text,{},field,0})||
                std::ranges::any_of(offered_annotations_,[&](const auto& h){return entity_selected(h.key);})||
                entity_selected({AnnotationKind::View,view_at(event->pos()),{},0});
            bool generic=entity_selection_.size()>1;
            if(entity_selection_.size()==1){const auto& key=entity_selection_.front();
                if(key.kind==AnnotationKind::Dimension)if(const auto* dimension=visible_dimension(key.id);dimension&&dimension->kind==drawing::DrawingDimensionKind::Chain&&on_selection) {
                    auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
                    populate_selection_menu(*menu);menu->popup(event->globalPos());event->accept();return;
                }
                generic=key.kind==AnnotationKind::Caption||key.kind==AnnotationKind::SectionLabel||key.kind==AnnotationKind::SectionEnd;
                if(key.kind==AnnotationKind::Model)for(const auto& v:sheet_->views)if(v.id==key.view)for(const auto& a:v.model_annotations)if(model_annotation_key(a.source)==key.id)generic=a.kind!=drawing::ModelAnnotationKind::Dimension;
            }
            if(on_selection&&generic){
                auto* menu=new QMenu(this);menu->setObjectName("drawingSelectionContextMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
                auto* remove=menu->addAction(tr("Odstranit"),this,[this]{erase_selection();});remove->setObjectName("drawingDeleteSelectionAction");
                menu->popup(event->globalPos());event->accept();return;
            }
        }
        if(balloon_context(event))return;
        if(text_editor_){event->accept();return;}
        if(const auto field=field_at(event->pos());editable_text(field)) {
            selected_field_=field;selected_.clear();selected_annotation_.reset();selected_dimension_id_.clear();update();
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->addAction(tr("Vlastnosti textu…"),this,[this,field]{if(text_properties_)text_properties_(field.substr(5));});
            menu->addAction(tr("Odstranit"),this,[this,field]{erase_text(field);});
            menu->popup(event->globalPos());event->accept();return;
        }
        if(dragged_model_||!dragged_dimension_id_.empty()){event->accept();return;}
        if(dimension_command_){
            if(dimension_command_->entering()&&!measurement_offered_.empty())measurement_index_=(measurement_index_+1)%measurement_offered_.size();
            offer_measurements(measurement_pointer_);
            update();event->accept();return;
        }

        if(model_pick_){offer_annotations(event->pos());if(!offered_annotations_.empty()){offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;update();}event->accept();return;}
        if (placed_ || preview_ || dimension_mode_ || view_panning_) return;
        if(const auto field=field_at(event->pos());!field.empty()) {
            selected_field_=field;selected_.clear();selected_dimension_id_.clear();selected_annotation_.reset();
            if(selection_changed_)selection_changed_();update();
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
            if(field.starts_with("symbol:"))menu->addAction(tr("Vlastnosti symbolu"),this,[this]{if(title_action_)title_action_->trigger();});
            else menu->addAction(title_action_);
            menu->popup(event->globalPos());event->accept();return;
        }
        selected_field_.clear();
        offer_annotations(event->pos());
        if(!offered_annotations_.empty()&&(!selected_annotation_||
            selected_annotation_->kind!=offered_annotations_[offered_annotation_index_].key.kind||
            selected_annotation_->view!=offered_annotations_[offered_annotation_index_].key.view||
            selected_annotation_->id!=offered_annotations_[offered_annotation_index_].key.id)){
            offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;update();event->accept();return;
        }
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Model&&!offered_annotations_.empty()&&dimension_properties_) {
            const auto key=*selected_annotation_;
            const auto view=std::ranges::find(sheet_->views,key.view,&drawing::DrawingView::id);
            if(view!=sheet_->views.end()&&std::ranges::any_of(view->model_annotations,[&](const auto& a){return a.kind==drawing::ModelAnnotationKind::Dimension&&model_annotation_key(a.source)==key.id;})) {
                auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
                auto* properties=menu->addAction(tr("Vlastnosti kóty…"));properties->setObjectName("dimensionLayoutPropertiesAction");
                connect(properties,&QAction::triggered,this,[this,key]{dimension_properties_(key.view,key.id);});menu->addAction(tr("Odstranit"),this,[this]{erase_selection();});menu->popup(event->globalPos());event->accept();return;
            }
        }
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension&&!offered_annotations_.empty()){
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);const auto id=selected_dimension_id_;
            menu->addAction(tr("Vlastnosti kóty…"),this,[this,id]{if(manual_properties_)manual_properties_(id,0);});
            if(const auto* value=visible_dimension(id);value&&(value->kind==drawing::DrawingDimensionKind::Linear||value->kind==drawing::DrawingDimensionKind::Chain)) {
                menu->addAction(resource_icon("drawing-jog"),tr("Vložit zalomení"),this,[this,id]{start_witness_tool(drawing::WitnessEditKind::Jog,id);});
                menu->addAction(resource_icon("drawing-break"),tr("Vložit přerušení"),this,[this,id]{start_witness_tool(drawing::WitnessEditKind::Break,id);});
            }
            const auto* dimension=visible_dimension(id);
            if(dimension&&(dimension->kind==drawing::DrawingDimensionKind::Linear||dimension->kind==drawing::DrawingDimensionKind::Chain)){
                if(dimension->kind==drawing::DrawingDimensionKind::Linear) {
                    auto* convert=menu->addAction(tr("Převést na řetězovou kótu…"),this,[this,id]{if(manual_properties_)manual_properties_(id,2);});
                    convert->setObjectName("convertDrawingChainAction");
                } else {
                    menu->addAction(tr("Převést na lineární kótu…"),this,[this,id]{if(manual_properties_)manual_properties_(id,4);})->setObjectName("convertDrawingLinearAction");
                    menu->addAction(tr("Přidat větev"),this,[this,id]{if(manual_properties_)manual_properties_(id,1);})->setObjectName("drawingAddChainBranchAction");
                }
            }
            menu->addAction(tr("Odstranit"),this,[this,id]{workspace::erase_drawing_dimension(*sheet_,id);selected_dimension_id_.clear();selected_annotation_.reset();if(changed_)changed_();if(selection_changed_)selection_changed_();update();});menu->popup(event->globalPos());event->accept();return;
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
        if(crop_edit_){if(event->button()==Qt::LeftButton)finish_crop(true);event->accept();return;}
        if(event->button()==Qt::LeftButton&&!preview_&&!dimension_command_&&!balloon_command_&&!dimension_mode_&&selected_annotation_&&
            selected_annotation_->kind==AnnotationKind::Model) {
            for(const auto& handle:annotation_handles_)if(handle.key.kind==AnnotationKind::Model&&
                handle.key.view==selected_annotation_->view&&handle.key.id==selected_annotation_->id&&
                handle.key.end==0&&(handle.text_hit.adjusted(-2,-2,2,2).contains(event->position())||
                    QLineF(handle.point,event->position()).length()<=8)) {
                dragged_model_.reset();model_drag_original_.reset();model_moved_=false;
                if(dimension_value_)dimension_value_(handle.key.view,handle.key.id,handle.point);
                event->accept();return;
            }
        }
        if(balloon_command_){event->accept();return;}
        if(event->button()==Qt::LeftButton&&selected_annotation_&&selected_annotation_->kind==AnnotationKind::Balloon){balloon_drag_original_.reset();if(balloon_properties_)balloon_properties_(selected_annotation_->id);event->accept();return;}
        if(event->button()==Qt::LeftButton&&selected_annotation_&&selected_annotation_->kind==AnnotationKind::DetailLabel){
            const auto id=selected_annotation_->view;dragged_detail_label_.reset();label_moved_=false;
            select_view_for_test(id);if(edit_action_)edit_action_->trigger();event->accept();return;
        }
        if(event->button()==Qt::LeftButton) {
            if(text_editor_){event->accept();return;}
            if(const auto field=field_at(event->position());editable_text(field)) {
                text_drag_original_.reset();selected_field_=field;
                if(text_properties_)text_properties_(field.substr(5));event->accept();return;
            }
        }
        if(event->button()==Qt::LeftButton&&selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension&&!dimension_command_){
            dragged_dimension_id_.clear();edit_manual_entity(*selected_annotation_);event->accept();return;
        }
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
        // The child owns painting and receives updates directly. Parent expose
        // and capture events must not enqueue a second full-sheet repaint.
        if(gpu_surface_&&gpu_surface_->isValid())return;
        QPainter painter(this);paint_canvas(painter);
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);if(gpu_surface_)gpu_surface_->setGeometry(rect());
    }
    void paint_canvas(QPainter& painter) {
        painter.fillRect(rect(),QColor("#000000"));
        if((placed_||preview_frame_bounds_)&&preview_) {
            const auto zoom=canvas_zoom();const auto origin=canvas_origin(zoom);
            const auto ratio=devicePixelRatioF();const auto pixels=size()*ratio;
            if(placement_background_.size()!=pixels||placement_background_.devicePixelRatio()!=ratio||
                placement_background_zoom_!=zoom||placement_background_origin_!=origin) {
                auto placement=std::move(preview_);preview_.reset();
                placement_background_={};
                const auto paint=[&](QPainter& background){paint_sheet(background,zoom,origin,false);};
                if(gpu_surface_){if(!placement_surface_)placement_surface_=std::make_unique<DrawingSheetSurface>();placement_background_=placement_surface_->render(pixels,ratio,paint);}
                if(placement_background_.isNull()||placement_background_.size()!=pixels) {
                    placement_background_=QImage(pixels,QImage::Format_ARGB32_Premultiplied);placement_background_.setDevicePixelRatio(ratio);placement_background_.fill(Qt::black);
                    QPainter background(&placement_background_);paint(background);
                }
                preview_=std::move(placement);placement_background_zoom_=zoom;placement_background_origin_=origin;
            }
            painter.drawImage(QPointF{},placement_background_);
            painter.save();painter.setPen(QPen(QColor("#00D1FF"),1));painter.setBrush(Qt::NoBrush);
            painter.drawRect(view_bounds(*preview_));painter.restore();
        }else {placement_background_={};paint_sheet(painter,canvas_zoom(),canvas_origin(canvas_zoom()),false);}
        paint_crop(painter);
        if(text_editor_&&text_editor_->needs_anchor()&&text_preview_&&underMouse()) {
            const auto zoom=canvas_zoom();const auto p=text_preview_->presentation.position;const auto origin=canvas_origin(zoom);
            const QPointF anchor(origin.x()+(sheet_->width_mm()-p.x)*zoom,origin.y()+(sheet_->height_mm()-p.y)*zoom);
            painter.setPen(Qt::NoPen);painter.setBrush(QColor("#4DD811"));painter.drawEllipse(anchor,3.5,3.5);
        }
    }
public:
    void set_model_command(std::set<std::string> offered,std::function<void(const std::string&)> pick){model_offered_=std::move(offered);model_pick_=std::move(pick);selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear();update();}
    std::optional<QPointF> model_handle(const std::string& key,int end,const std::string& view)const{for(const auto& h:annotation_handles_)if(h.key.kind==AnnotationKind::Model&&h.key.id==key&&h.key.end==end&&(view.empty()||h.key.view==view))return h.point;return {};}
    void set_lineweights(bool value){lineweights_=value;update();}
protected:
    const drawing::DrawingDimension* pending_dimension() const override {return witness_draft_?&*witness_draft_:dimension_command_?&dimension_command_->value():nullptr;}
    void paint_reference_overlay(QPainter& painter) override {
        paint_witness(painter);
        if(dimension_command_||balloon_command_||balloon_drag_original_){
            const auto highlight=[&](const drawing::DrawingView& view,const kernel::EdgeReference& ref,QColor color,bool points=true){
                painter.save();painter.setPen(QPen(color,2));painter.setBrush(Qt::NoBrush);
                for(const auto& curve:drawing::measurement_reference_geometry(view,ref)){QPolygonF line;for(const auto& p:curve)line<<view_screen_point(view,p);painter.drawPolyline(line);}
                for(const auto& p:view.measurement_geometry->points)if(points&&p.source==ref&&!drawing::break_hidden(view,{kernel::dimension_dot(p.position,view.camera.horizontal),kernel::dimension_dot(p.position,view.camera.vertical)}))painter.drawEllipse(view_screen_point(view,{kernel::dimension_dot(p.position,view.camera.horizontal),kernel::dimension_dot(p.position,view.camera.vertical)}),4,4);
                painter.restore();
            };
            if(!measurement_offered_.empty()){
                const auto& candidate=measurement_offered_[measurement_index_];
                for(const auto& view:sheet_->views)if(view.id==candidate.view&&candidate.chain_seed.empty()){
                    const QColor color=interaction::hover;
                    const bool line=candidate.candidate.attachment.kind==drawing::DimensionAttachmentKind::Line||
                        (dimension_command_&&(dimension_command_->value().kind==drawing::DrawingDimensionKind::Radius||dimension_command_->value().kind==drawing::DrawingDimensionKind::Diameter));
                    if(!dimension_command_||line){highlight(view,candidate.candidate.attachment.reference,color,!dimension_command_);highlight(view,candidate.candidate.attachment.other_reference,color,!dimension_command_);}
                    // Match the Sketcher point hover: filled, radius 5 px, 2 px outline.
                    const double radius=dimension_command_?5.0:4.0;
                    if(!dimension_command_||!line){painter.save();painter.setPen(QPen(color,2));painter.setBrush(dimension_command_?QBrush(color):QBrush(Qt::NoBrush));painter.drawEllipse(view_screen_point(view,candidate.candidate.position),radius,radius);painter.restore();}
                }
            }
            if(dimension_command_)for(const auto& view:sheet_->views)if(view.id==dimension_command_->value().view_id)for(const auto& ref:dimension_command_->inspected_references())highlight(view,ref,QColor("#00D1FF"));
            if(balloon_command_&&balloon_command_->inspecting_attachment())if(const auto* b=balloon_command_->selected_balloon())for(const auto& view:sheet_->views)if(view.id==b->view_id)highlight(view,b->attachment.reference,QColor("#00D1FF"));
        }
        if(sheet_&&!snap_view_.empty())for(const auto& view:sheet_->views)if(view.id==snap_view_) {
            const auto screen=[&](drawing::Point2 p){return view_screen_point(view,{p.x/view.scale,p.y/view.scale});};
            painter.save();painter.setPen(QPen(QColor("#777777"),1,Qt::DashLine));
            for(const auto& line:drawing::annotation_guides(view))painter.drawLine(screen(line.first),screen(line.second));
            if(snap_point_&&snap_line_) {
                painter.setPen(QPen(QColor("#80AA1A"),2));painter.drawLine(screen(snap_line_->first),screen(snap_line_->second));
            }
            painter.restore();
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(align_mode_&&event->button()==Qt::LeftButton){
            if(!offered_annotations_.empty()) {
                const auto key=offered_annotations_[offered_annotation_index_].key;
                if(align_first_.empty()){align_first_=key.id;select_entity(key,false);}
                else {select_entity(key,true);finish_alignment({align_first_,key.id});}
                offered_annotations_.clear();hovered_annotation_.reset();if(selection_changed_)selection_changed_();update();
            }
            event->accept();return;
        }
        if(witness_press(event)){event->accept();return;}
        if(crop_press(event))return;
        if (sheet_ == nullptr) return;
        if(event->button()==Qt::RightButton&&!dragged_dimension_id_.empty()&&(event->buttons()&Qt::LeftButton)){
            auto value=*visible_dimension(dragged_dimension_id_);const auto segment=dimension_drag_handle_/3;
            auto kind=value.kind==drawing::DrawingDimensionKind::Radius?kernel::ViewerDimensionKind::Radius:value.kind==drawing::DrawingDimensionKind::Diameter?kernel::ViewerDimensionKind::Diameter:kernel::ViewerDimensionKind::Linear;
            kernel::cycle_dimension_presentation(value.segments[segment].layout,kind);
            dimension_drag_initial_->segments[segment].layout.arrows_reversed=value.segments[segment].layout.arrows_reversed;
            dimension_drag_initial_->segments[segment].layout.radius_center_line_hidden=value.segments[segment].layout.radius_center_line_hidden;
            store_presentation(std::move(value));event->accept();return;
        }
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
        setFocus();
        if(text_editor_) {const auto p=paper_point(event->position());text_editor_->set_anchor(p.x,p.y);event->accept();return;}
        if(balloon_press(event))return;
        if(dimension_command_){
            setFocus();
            if(dimension_command_->entering()){
                if(!measurement_offered_.empty()){const auto candidate=measurement_offered_[measurement_index_];if(candidate.chain_seed.empty())dimension_command_->accept_candidate(candidate.view,candidate.candidate);
                    else if(const auto* seed=visible_dimension(candidate.chain_seed))dimension_command_->adopt_chain_seed(*seed);}
                else {selected_annotation_.reset();selected_.clear();if(selection_changed_)selection_changed_();}
            }else if(dimension_command_->placing()){
                for(const auto& view:sheet_->views)if(view.id==dimension_command_->value().view_id){const auto p=snap_measurement_point(view,event->position());dimension_command_->position({p.x(),p.y()},true);clear_annotation_snap();}
            }else{
                offer_annotations(event->position());
                if(!offered_annotations_.empty()){const auto candidate=offered_annotations_[offered_annotation_index_];if(candidate.key.kind==AnnotationKind::Dimension&&candidate.key.id==dimension_command_->value().id&&QLineF(candidate.point,event->position()).length()<=8)begin_manual_drag(candidate,event->position());}
            }
            update();event->accept();return;
        }
        if (choose_view_) {
            const auto id=view_at(event->position());
            selected_=id;selected_annotation_.reset();selected_field_.clear();selected_dimension_id_.clear();
            if(selection_changed_)selection_changed_();
            if(!id.empty()){chosen_view_point_=event->position();auto pick=std::move(choose_view_);choose_view_={};pick(id);}
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
        if(ordinary_selection()) {
            setFocus();offer_annotations(event->position());std::optional<AnnotationKey> key;
            const auto field=field_at(event->position());
            if(editable_text(field))key=AnnotationKey{AnnotationKind::Text,{},field,0};
            else if(field.empty()&&!offered_annotations_.empty())key=offered_annotations_[offered_annotation_index_].key;
            else if(field.empty())if(const auto view=view_at(event->position());!view.empty())key=AnnotationKey{AnnotationKind::View,view,{},0};
            if(event->modifiers().testFlag(Qt::ControlModifier)||!key||key->kind!=AnnotationKind::Dimension||!entity_selected(*key)||entity_selection_.size()<2)
                select_entity(key,event->modifiers().testFlag(Qt::ControlModifier));
            if(event->modifiers().testFlag(Qt::ControlModifier)){
                selected_.clear();selected_annotation_.reset();selected_field_.clear();selected_dimension_id_.clear();
                if(selection_changed_)selection_changed_();update();event->accept();return;
            }
        }
        selected_field_=dimension_mode_?std::string{}:field_at(event->position());
        if(!selected_field_.empty()) {
            selected_annotation_.reset();selected_.clear();selected_dimension_id_.clear();drag_view_id_.clear();
            if(const auto* text=editable_text(selected_field_)){text_drag_original_=*text;text_drag_start_=event->position();}
            if(selection_changed_)selection_changed_();update();event->accept();return;
        }
        if(!dimension_mode_){
            offer_annotations(event->position());
            if(!offered_annotations_.empty()){
                const auto candidate=offered_annotations_[offered_annotation_index_];selected_annotation_=candidate.key;selected_field_.clear();selected_dimension_id_.clear();drag_view_id_.clear();dragged_label_.reset();dragged_dimension_id_.clear();
                selected_=(candidate.key.kind==AnnotationKind::Dimension||candidate.key.kind==AnnotationKind::Balloon)?std::string{}:candidate.key.view;
                if(candidate.key.kind==AnnotationKind::Dimension)selected_dimension_id_=candidate.key.id;
                if(QLineF(candidate.point,event->position()).length()<=8||(candidate.key.kind==AnnotationKind::DetailLabel&&candidate.text_hit.contains(event->position()))){
                    if(candidate.key.kind==AnnotationKind::DetailLabel){
                        dragged_detail_label_=candidate;label_drag_start_=event->position();label_moved_=false;
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view)detail_label_original_=view.detail_label_position;
                    }else
                    if(candidate.key.kind==AnnotationKind::Caption||candidate.key.kind==AnnotationKind::SectionLabel){
                        dragged_label_=std::pair{candidate.key.view,candidate.key.kind==AnnotationKind::SectionLabel};label_drag_start_=event->position();label_moved_=false;
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view){const auto origin=raw_view_origin(view);label_position_start_={(candidate.point.x()-origin.x())/canvas_zoom(),(origin.y()-candidate.point.y())/canvas_zoom()};}
                    }else if(candidate.key.kind==AnnotationKind::Balloon){
                        begin_balloon_drag(candidate,event->position());
                    }else if(candidate.key.kind==AnnotationKind::Dimension){
                        begin_manual_drag(candidate,event->position());
                    }else if(candidate.key.kind==AnnotationKind::Model){
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view)for(const auto& item:view.model_annotations)if(model_annotation_key(item.source)==candidate.key.id&&item.kind==drawing::ModelAnnotationKind::Dimension){model_drag_original_=item;dragged_model_=candidate;model_drag_start_=event->position();model_moved_=false;model_drag_layout_initial_=item.view_layout.value_or(item.model_layout);if(!item.view_layout)model_drag_layout_initial_.plane_quarter_turns=0;model_drag_shown_=item.model_dimension?std::optional(drawing::drawing_model_dimension(view,item)):std::nullopt;}
                    }else{dragged_section_end_=candidate;section_end_drag_start_=event->position();section_end_moved_=false;}
                }
                if(selection_changed_)selection_changed_();update();event->accept();return;
            }
        }
        selected_annotation_.reset();
        setFocus();selected_dimension_id_.clear();
        selected_=view_at(event->position());
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
        if(align_mode_&&!view_panning_){offer_annotations(event->position());update();event->accept();return;}
        if(witness_tool_&&(event->buttons()&Qt::MiddleButton)&&!view_panning_){event->accept();return;}
        if((witness_tool_||witness_drag_end_>=0)&&!view_panning_){
            if(witness_draft_)move_witness(event->position());
            else if(witness_owner_.empty()&&*witness_tool_==drawing::WitnessEditKind::Break) {
                offer_annotations(event->position());
                std::erase_if(offered_annotations_,[&](const auto& handle){const auto* d=visible_dimension(handle.key.id);return handle.key.kind!=AnnotationKind::Dimension||!d||(d->kind!=drawing::DrawingDimensionKind::Linear&&d->kind!=drawing::DrawingDimensionKind::Chain);});
                if(offered_annotation_index_>=offered_annotations_.size())offered_annotation_index_=0;
                hovered_annotation_=offered_annotations_.empty()?std::optional<AnnotationKey>{}:offered_annotations_[offered_annotation_index_].key;
            }else offer_witness(event->position());
            update();event->accept();return;
        }
        offer_witness_grip(event->position());
        if(crop_move(event))return;
        if(text_editor_&&!view_panning_&&!(event->buttons()&Qt::MiddleButton)) {
            if(text_editor_->needs_anchor()){const auto p=paper_point(event->position());text_editor_->set_preview_anchor(p.x,p.y);}
            update();event->accept();return;
        }
        if(balloon_move(event))return;
        if(text_drag_original_&&!view_panning_&&(event->buttons()&Qt::LeftButton)) {
            if(auto* text=editable_text("text:"+text_drag_original_->id)) {
                const auto delta=(event->position()-text_drag_start_)/canvas_zoom();
                text->presentation.position={text_drag_original_->presentation.position.x-delta.x(),text_drag_original_->presentation.position.y-delta.y()};
            }
            update();event->accept();return;
        }
        if(dimension_command_&&!view_panning_&&!(event->buttons()&Qt::MiddleButton)&&dragged_dimension_id_.empty()){
            offer_measurements(event->position());
            if(dimension_command_->placing()){for(const auto& view:sheet_->views)if(view.id==dimension_command_->value().view_id){const auto p=snap_measurement_point(view,event->position());dimension_command_->position({p.x(),p.y()},false);}}
            else if(!dimension_command_->entering())offer_annotations(event->position());
            update();event->accept();return;
        }
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
                const auto origin=raw_view_origin(view);const auto point=dragged_model_->point+delta;
                auto p=snap_annotation_point(view,drawing::break_paper(view,{(point.x()-origin.x())/zoom,(origin.y()-point.y())/zoom},true));
                const auto start=drawing::break_paper(view,{(dragged_model_->point.x()-origin.x())/zoom,(origin.y()-dragged_model_->point.y())/zoom},true);
                const QPointF snapped_delta((p.x-start.x)*zoom,-(p.y-start.y)*zoom);
                item.handle_camera_horizontal={view.camera.horizontal.x,view.camera.horizontal.y,view.camera.horizontal.z};
                item.handle_camera_vertical={view.camera.vertical.x,view.camera.vertical.y,view.camera.vertical.z};
                const auto key=dragged_model_->key.end==0?"text":dragged_model_->key.end==1?"arrow_first":"arrow_second";
                const auto projected=drawing::project_model_annotation(view,item);
                if(item.model_dimension&&model_drag_shown_) {
                    const auto& shown=*model_drag_shown_;const bool angular=shown.kind==kernel::ViewerDimensionKind::Angular;
                    const auto u=kernel::dimension_unit(kernel::dimension_sub(angular?shown.line_first:shown.witness_second,shown.witness_first));
                    const auto v=kernel::dimension_unit(kernel::dimension_cross(shown.plane_normal,u));
                    const QPointF a(kernel::dimension_dot(u,view.camera.horizontal),kernel::dimension_dot(u,view.camera.vertical)),b(kernel::dimension_dot(v,view.camera.horizontal),kernel::dimension_dot(v,view.camera.vertical));
                    const QPointF move(snapped_delta.x()/(zoom*view.scale),-snapped_delta.y()/(zoom*view.scale));
                    if(const auto movement=viewer::dimension_plane_drag(move,a,b)){const double along=movement->x(),outward=movement->y();
                        item.view_layout=kernel::dragged_dimension_layout(shown,item.model_envelope,model_drag_layout_initial_,dragged_model_->key.end,along,outward);
                        if(shown.kind==kernel::ViewerDimensionKind::Linear) {
                            // Drawing placement is signed: crossing the object must not
                            // clamp the actual grip to the old envelope's positive side.
                            item.view_layout=model_drag_layout_initial_;
                            item.view_layout->line_offset+=outward;
                            if(dragged_model_->key.end==0)item.view_layout->text_along+=along;
                        }
                        if(dragged_model_->key.end==0){
                            const auto label=shown.label_position.value_or(kernel::dimension_scale(kernel::dimension_add(shown.line_first,shown.line_second),.5));
                            const QPointF original(start.x/view.scale,start.y/view.scale);
                            const auto correction=original-QPointF(kernel::dimension_dot(label,view.camera.horizontal),kernel::dimension_dot(label,view.camera.vertical));
                            if(const auto offset=viewer::dimension_plane_drag(correction,a,b)) {
                                item.view_layout->text_along+=offset->x();item.view_layout->text_outward+=offset->y();
                            }
                        }
                        item.paper_handles.clear();model_moved_=true;
                        if(snap_point_&&dragged_model_->key.end!=0) {
                            const auto placed=drawing::drawing_model_dimension(view,item);
                            const auto point=dragged_model_->key.end==1?placed.line_first:placed.line_second;
                            const drawing::Point2 actual{kernel::dimension_dot(point,view.camera.horizontal)*view.scale,kernel::dimension_dot(point,view.camera.vertical)*view.scale};
                            if(std::hypot(actual.x-snap_point_->x,actual.y-snap_point_->y)*zoom>1) {
                                snap_point_.reset();snap_line_.reset();setProperty("annotationSnapActive",false);
                            }
                        }
                    }
                    continue;
                }
                if(dragged_model_->key.end!=0&&item.dimension_kind==kernel::ViewerDimensionKind::Linear&&item.curves.size()>=3&&item.curves[1].size()>=2){auto a=item.curves[1].front(),b=item.curves[1].back();a={a.x*view.scale,a.y*view.scale};b={b.x*view.scale,b.y*view.scale};const auto length=std::hypot(b.x-a.x,b.y-a.y);if(length>1e-9){const double nx=-(b.y-a.y)/length,ny=(b.x-a.x)/length,offset=(p.x-a.x)*nx+(p.y-a.y)*ny;item.paper_handles["arrow_first"]={a.x+nx*offset,a.y+ny*offset};item.paper_handles["arrow_second"]={b.x+nx*offset,b.y+ny*offset};}}
                else {if(item.dimension_kind==kernel::ViewerDimensionKind::Angular&&dragged_model_->key.end!=0)item.paper_handles.erase(dragged_model_->key.end==1?"arrow_second":"arrow_first");item.paper_handles[key]=p;}model_moved_=true;
            }update();return;
        }
        if(dragged_section_end_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-section_end_drag_start_;if(!section_end_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_section_end_->key.view){
                const auto& end=*dragged_section_end_;const auto origin=raw_view_origin(view);
                const auto start=drawing::break_paper(view,{(end.point.x()-origin.x())/zoom,(origin.y()-end.point.y())/zoom},true);
                double movement=std::max(end.minimum-end.offset,(delta.x()*end.direction.x-delta.y()*end.direction.y)/zoom);
                clear_annotation_snap();double best=6/zoom;
                for(const auto& guide:drawing::annotation_guides(view)){
                    const double gx=guide.second.x-guide.first.x,gy=guide.second.y-guide.first.y;
                    const double determinant=end.direction.x*gy-end.direction.y*gx;if(std::abs(determinant)<1e-12)continue;
                    const double x=guide.first.x-start.x,y=guide.first.y-start.y;
                    const double t=(x*gy-y*gx)/determinant,u=(x*end.direction.y-y*end.direction.x)/determinant;
                    if(u<0||u>1||end.offset+t<end.minimum||std::abs(t-movement)>=best)continue;
                    best=std::abs(t-movement);snap_view_=view.id;snap_line_=guide;snap_point_=drawing::Point2{start.x+t*end.direction.x,start.y+t*end.direction.y};
                }
                if(snap_point_)movement=(snap_point_->x-start.x)*end.direction.x+(snap_point_->y-start.y)*end.direction.y;
                setProperty("annotationSnapActive",snap_point_.has_value());setProperty("annotationSnapView",QString::fromStdString(snap_view_));
                workspace::set_drawing_section_end(view,end.key.id,end.key.end,end.offset+movement,end.minimum);section_end_moved_=true;
            }update();return;
        }
        if(dragged_detail_label_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-label_drag_start_;
            if(!label_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            const auto parent=std::ranges::find(sheet_->views,dragged_detail_label_->key.id,&drawing::DrawingView::id);
            if(parent!=sheet_->views.end())for(auto& view:sheet_->views)if(view.id==dragged_detail_label_->key.view){
                const auto origin=raw_view_origin(*parent),point=dragged_detail_label_->point+delta;
                view.detail_label_position=snap_annotation_point(*parent,{(point.x()-origin.x())/zoom,(origin.y()-point.y())/zoom});label_moved_=true;
            }update();event->accept();return;
        }
        if(dragged_label_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-label_drag_start_;
            if(!label_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_label_->first){
                workspace::set_drawing_label_position(view,dragged_label_->second?workspace::DrawingLabel::Section:workspace::DrawingLabel::Caption,
                    drawing::break_paper(view,snap_annotation_point(view,drawing::break_paper(view,{label_position_start_.x+delta.x()/zoom,label_position_start_.y-delta.y()/zoom},true))));label_moved_=true;
            }update();return;
        }
        if(!dragged_dimension_id_.empty()&&dimension_drag_initial_&&(event->buttons()&Qt::LeftButton)){
            auto value=*dimension_drag_initial_;
            const auto view=std::ranges::find(sheet_->views,value.view_id,&drawing::DrawingView::id);
            if(view==sheet_->views.end())return;
            const auto target=snap_measurement_point(*view,dimension_grip_start_+event->position()-dimension_drag_start_);
            const auto start=measurement_point(*view,dimension_grip_start_);drawing::Point2 delta{target.x()-start.x,target.y()-start.y};
            const auto evaluation=drawing::evaluate_drawing_dimension(*view,value);const auto segment=dimension_drag_handle_/3;
            if(value.kind!=drawing::DrawingDimensionKind::Chain&&dimension_drag_handle_%3==0&&segment<int(evaluation.presentations.size())){
                const auto label=evaluation.presentations[segment].label_position.value();
                const auto actual=measurement_point(*view,dimension_grip_start_);
                delta.x+=actual.x-label.x;delta.y+=actual.y-label.y;
            }
            drawing::drag_drawing_dimension(*view,value,segment,dimension_drag_handle_%3,delta);
            store_presentation(std::move(value));
            std::set<std::string> moved_groups;
            if(dimension_drag_handle_%3!=0&&!dimension_drag_initial_->chain_group.empty())moved_groups.insert(dimension_drag_initial_->chain_group);
            for(auto companion:dimension_drag_companions_) {
                if(dimension_drag_handle_%3!=0&&!companion.chain_group.empty()&&!moved_groups.insert(companion.chain_group).second)continue;
                const auto companion_view=std::ranges::find(sheet_->views,companion.view_id,&drawing::DrawingView::id);
                if(companion_view==sheet_->views.end())continue;
                const double ratio=view->scale/companion_view->scale;
                drawing::drag_drawing_dimension(*companion_view,companion,0,dimension_drag_handle_%3,{delta.x*ratio,delta.y*ratio});
                store_presentation(std::move(companion));
            }
            event->accept();return;
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
        if(witness_tool_&&event->button()==Qt::MiddleButton){
            const bool click=(event->position()-witness_middle_origin_).manhattanLength()<QApplication::startDragDistance();
            view_panning_=false;unsetCursor();
            if(click){cancel_witness();if(dimension_command_)dimension_command_->end_entry();}
            update();event->accept();return;
        }
        if(witness_drag_end_>=0&&event->button()==Qt::LeftButton){move_witness(event->position());commit_witness();event->accept();return;}
        if(witness_tool_){event->accept();return;}
        if(crop_edit_){crop_grip_=-1;event->accept();return;}
        if(event->button()==Qt::LeftButton)clear_annotation_snap();
        if(balloon_release(event))return;
        if(text_drag_original_&&event->button()==Qt::LeftButton) {
            const auto* text=editable_text("text:"+text_drag_original_->id);
            const bool moved=text&&(text->presentation.position.x!=text_drag_original_->presentation.position.x||text->presentation.position.y!=text_drag_original_->presentation.position.y);
            text_drag_original_.reset();if(moved&&changed_)changed_();event->accept();return;
        }
        if(!dragged_dimension_id_.empty()&&event->button()==Qt::RightButton){event->accept();return;}
        if(dragged_model_&&event->button()==Qt::RightButton){event->accept();return;}
        if(dragged_detail_label_&&event->button()!=Qt::LeftButton){event->accept();return;}
        if(preview_dragging_&&event->button()==Qt::LeftButton){preview_dragging_=false;event->accept();return;}
        if (view_panning_ &&
            (!(event->buttons() & Qt::MiddleButton) ||
             !(event->buttons() & Qt::RightButton))) {
            view_panning_ = false;
            unsetCursor();
            event->accept();
        }
        const bool label_changed=label_moved_||section_end_moved_||model_moved_;dragged_model_.reset();model_moved_=false;dragged_label_.reset();label_moved_=false;dragged_section_end_.reset();section_end_moved_=false;
        dragged_detail_label_.reset();detail_label_original_.reset();
        if((!drag_view_id_.empty() || (!dragged_dimension_id_.empty()&&!dimension_command_&&dimension_drag_original_&&visible_dimension(dragged_dimension_id_)&&*visible_dimension(dragged_dimension_id_)!=*dimension_drag_original_) || label_changed) && changed_) changed_();
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
        if(align_mode_&&event->key()==Qt::Key_Escape){start_selection();event->accept();return;}
        if(event->key()==Qt::Key_Escape&&(witness_tool_||witness_draft_)){cancel_witness();if(dimension_command_)dimension_command_->end_entry();event->accept();return;}
        if(event->key()==Qt::Key_Escape&&dragged_detail_label_){
            for(auto& view:sheet_->views)if(view.id==dragged_detail_label_->key.view)view.detail_label_position=detail_label_original_;
            dragged_detail_label_.reset();detail_label_original_.reset();label_moved_=false;clear_annotation_snap();update();event->accept();return;
        }
        if(crop_edit_){if(event->key()==Qt::Key_Escape)finish_crop(false);else if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)finish_crop(true);event->accept();return;}
        if(event->key()==Qt::Key_Delete&&ordinary_selection()&&!entity_selection_.empty()){erase_selection();event->accept();return;}
        if(event->key()==Qt::Key_Escape){select_entity({},false);clear_annotation_snap();}
        if(dimension_command_&&(event->key()==Qt::Key_C||event->key()==Qt::Key_T||event->key()==Qt::Key_I)){
            dimension_command_->set_mode(int(event->key()==Qt::Key_C?drawing::DimensionAttachmentKind::Center:event->key()==Qt::Key_T?drawing::DimensionAttachmentKind::Tangent:drawing::DimensionAttachmentKind::Intersection));event->accept();return;
        }
        if(event->key()==Qt::Key_Escape) {
            if(balloon_escape()){event->accept();return;}
            if(text_editor_){text_editor_->reject();event->accept();return;}
            if(text_drag_original_){if(auto* text=editable_text("text:"+text_drag_original_->id))*text=*text_drag_original_;text_drag_original_.reset();update();event->accept();return;}
            selected_field_.clear();hovered_field_.clear();
            if(dragged_model_&&model_drag_original_&&sheet_){for(auto& view:sheet_->views)if(view.id==dragged_model_->key.view)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==dragged_model_->key.id)item=*model_drag_original_;dragged_model_.reset();model_drag_original_.reset();model_moved_=false;update();event->accept();return;}
            if (choose_view_) {choose_view_={};auto canceled=std::move(choose_view_canceled_);if(canceled)canceled();update();event->accept();return;}
            if (placed_) { cancel_placement(); event->accept(); return; }
            if(!dragged_dimension_id_.empty()&&dimension_drag_original_){store_presentation(*dimension_drag_original_);for(const auto& original:dimension_drag_companions_)store_presentation(original);dimension_drag_companions_.clear();dragged_dimension_id_.clear();event->accept();return;}
            if(dimension_command_){if(dimension_command_->entering())dimension_command_->end_entry();else dimension_command_->reject();event->accept();return;}
            else { selected_dimension_id_.clear(); selected_.clear();selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear(); }
            if (selection_changed_) selection_changed_();
            update(); event->accept(); return;
        }
        if(event->key()==Qt::Key_Delete && sheet_ && !dimension_command_ && !model_pick_ && !preview_ && !placed_ && !choose_view_) {
            if(text_editor_){event->accept();return;}
            if(editable_text(selected_field_)){erase_text(selected_field_);event->accept();return;}
            if(balloon_command_){event->accept();return;}
            if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Balloon){erase_balloon(selected_annotation_->id);event->accept();return;}
            bool removed=false;
            if(!selected_dimension_id_.empty())
                removed=workspace::erase_drawing_dimension(*sheet_,selected_dimension_id_);
            else if(selected_annotation_ && selected_annotation_->kind==AnnotationKind::Model)
                for(auto& view:sheet_->views)if(view.id==selected_annotation_->view)
                    for(auto& item:view.model_annotations)
                        if(model_annotation_key(item.source)==selected_annotation_->id &&
                           item.kind==drawing::ModelAnnotationKind::Dimension && item.visible) {
                            item.visible=false;removed=true;
                        }
            if(removed) {
                selected_dimension_id_.clear();selected_annotation_.reset();hovered_annotation_.reset();
                offered_annotations_.clear();selected_.clear();
                if(changed_)changed_();
                if(selection_changed_)selection_changed_();
                update();event->accept();return;
            }
        }
        QWidget::keyPressEvent(event);
    }
private:
    zima::drawing::DrawingSheet* sheet_{};
    QPointer<SketchTextPropertiesDialog> text_editor_;
    std::function<void(const std::string&)> text_properties_;
    std::optional<drawing::DrawingText> text_drag_original_;
    QPointF text_drag_start_;
    QAction* title_action_{};
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
    struct OfferedMeasurement {std::string view;drawing::MeasurementCandidate candidate;std::string chain_seed;};
    QPointer<DrawingDimensionDialog> dimension_command_;
    std::vector<OfferedMeasurement> measurement_offered_;std::size_t measurement_index_{};
    QPointF measurement_pointer_;
    std::function<void(const std::string&,int)> manual_properties_;
    std::optional<drawing::DrawingDimension> dimension_drag_initial_,dimension_drag_original_;
    std::vector<drawing::DrawingDimension> dimension_drag_companions_;
    int dimension_drag_handle_{};QPointF dimension_grip_start_;
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
    std::optional<AnnotationHandle> dragged_detail_label_;
    std::optional<drawing::Point2> detail_label_original_;
    QPointF label_drag_start_;
    zima::drawing::Point2 label_position_start_;
    bool label_moved_{};
    QPointF dimension_drag_start_;
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
    remove_sheet_action_->setIcon(resource_icon("delete"));
    edit_sheet_action_ = drawing->addAction(tr("Vlastnosti listu…"), this,
        [this] { edit_sheet(); });
    edit_sheet_action_->setObjectName("editDrawingSheetAction");
    drawing->addAction(tr("Načíst formát…"), this, [this] { load_frame(); });
    auto* remove_frame_action = drawing->addAction(
        tr("Odstranit formát"), this, [this] { remove_frame(); });
    remove_frame_action->setObjectName("removeDrawingFrameAction");
    remove_frame_action->setIcon(resource_icon("delete"));
    drawing->addAction(tr("Načíst razítko…"), this, [this] { load_title_block(); });
    auto* remove_title_block_action = drawing->addAction(
        tr("Odstranit razítko"), this, [this] { remove_title_block(); });
    remove_title_block_action->setObjectName("removeDrawingTitleBlockAction");
    remove_title_block_action->setIcon(resource_icon("delete"));
    edit_title_block_action_ = drawing->addAction(tr("Hodnoty razítka…"), this,
        [this] { edit_title_block(); });
    edit_title_block_action_->setObjectName("editDrawingTitleBlockAction");
    drawing->addSeparator();
    insert_view_action_ = drawing->addAction(tr("Vložit pohled…"), this,
        [this] { insert_view(); });
    insert_view_action_->setObjectName("insertDrawingViewAction");
    insert_detail_action_=drawing->addAction(resource_icon("drawing-detail"),tr("Vložit detail…"),this,[this]{insert_detail();});
    insert_detail_action_->setObjectName("insertDrawingDetailAction");
    projected_view_action_ = drawing->addAction(
        tr("Projekční pohled"), this,
        [this] { create_projected_view(); });
    projected_view_action_->setObjectName("projectDrawingViewAction");
    edit_view_action_ = drawing->addAction(tr("Vlastnosti pohledu…"), this,
        [this] { edit_selected_view(); });
    edit_view_action_->setObjectName("editDrawingViewAction");
    edit_view_action_->setIcon(resource_icon("properties"));
    show_erase_action_=drawing->addAction(tr("Zobrazit / skrýt kóty…"),this,[this]{show_erase();});
    show_erase_action_->setObjectName("drawingShowEraseAction");
    show_erase_action_->setIcon(resource_icon("show-erase"));
    regenerate_view_action_ = drawing->addAction(tr("Regenerovat"), this,
        [this] { regenerate_selected_view(); });
    regenerate_view_action_->setObjectName("regenerateDrawingViewAction");
    regenerate_view_action_->setIcon(resource_icon("regenerate"));
    delete_view_action_ = drawing->addAction(tr("Odstranit pohled"), this,
        [this] { delete_selected_view(); });
    delete_view_action_->setObjectName("deleteDrawingViewAction");
    delete_view_action_->setIcon(resource_icon("delete"));
    linear_dimension_action_ = drawing->addAction(tr("Kóta"), this,
        [this] { start_linear_dimension(); });
    linear_dimension_action_->setObjectName("drawingDimensionAction");
    linear_dimension_action_->setIcon(resource_icon("sketch-dimensions"));
    linear_dimension_action_->setCheckable(true);
    chain_dimension_action_=drawing->addAction(tr("Řetězová kóta"),this,[this]{show_dimension_properties({},3);});
    chain_dimension_action_->setObjectName("drawingChainDimensionAction");
    chain_dimension_action_->setIcon(resource_icon("sketch-dimensions"));
    dimension_jog_action_=drawing->addAction(tr("Vložit zalomení"),this,[this]{canvas_->start_selection();canvas_->begin_witness_command(drawing::WitnessEditKind::Jog);});
    dimension_jog_action_->setObjectName("drawingDimensionJogAction");
    dimension_break_action_=drawing->addAction(tr("Vložit přerušení"),this,[this]{canvas_->start_selection();canvas_->begin_witness_command(drawing::WitnessEditKind::Break);});
    dimension_break_action_->setObjectName("drawingDimensionBreakAction");
    dimension_jog_action_->setIcon(resource_icon("drawing-jog"));
    dimension_break_action_->setIcon(resource_icon("drawing-break"));
    dimension_align_action_=drawing->addAction(resource_icon("drawing-align"),tr("Zarovnat kóty"),this,[this]{canvas_->start_alignment();});
    dimension_align_action_->setObjectName("drawingDimensionAlignAction");
    text_action_=drawing->addAction(resource_icon("sketch-text"),tr("Text"),this,[this]{show_text_properties();});
    text_action_->setObjectName("drawingTextAction");
    balloon_action_=drawing->addAction(resource_icon("drawing-balloon"),tr("Pozice"),this,[this]{show_balloon_properties();});
    balloon_action_->setObjectName("drawingBalloonAction");
    selection_action_ = new QAction(tr("Výběr"), this);
    selection_action_->setObjectName("drawingSelectionAction");
    selection_action_->setCheckable(true);
    connect(selection_action_, &QAction::triggered, this,
        [this] { start_selection(); });

    drawing_toolbar_ = new QToolBar(tr("Výkres"), this);
    drawing_toolbar_->setObjectName("drawingToolbar");
    drawing_toolbar_->setStyleSheet(toolbar_separator_style());
    drawing_toolbar_->setMovable(false);
    drawing_toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Keep the application toolbar contract identical to the Python UI:
    // Selection, separator, Insert view, Dimension. Less frequent view/sheet
    // operations remain in the Drawing menu and context workflow.
    drawing_toolbar_->addAction(selection_action_);
    drawing_toolbar_->addSeparator();
    drawing_toolbar_->addAction(insert_view_action_);
    drawing_toolbar_->addAction(insert_detail_action_);
    drawing_toolbar_->addAction(show_erase_action_);
    drawing_toolbar_->addAction(linear_dimension_action_);
    drawing_toolbar_->addAction(chain_dimension_action_);
    drawing_toolbar_->addAction(text_action_);
    drawing_toolbar_->addAction(balloon_action_);
    drawing_toolbar_->addAction(dimension_jog_action_);
    drawing_toolbar_->addAction(dimension_break_action_);
    drawing_toolbar_->addAction(dimension_align_action_);
    drawing_toolbar_->addSeparator();
    quick_pdf_action_=drawing_toolbar_->addAction(resource_icon("export-pdf"),tr("PDF"),this,[this]{quick_export(true);});
    quick_pdf_action_->setObjectName("drawingQuickExportPdfAction");
    quick_pdf_action_->setToolTip(tr("Exportovat výkres do nastavené složky PDF"));
    quick_dxf_action_=drawing_toolbar_->addAction(resource_icon("export-dxf"),tr("DXF"),this,[this]{quick_export(false);});
    quick_dxf_action_->setObjectName("drawingQuickExportDxfAction");
    quick_dxf_action_->setToolTip(tr("Exportovat aktuální list do nastavené složky DXF"));
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
    canvas_->set_balloon_properties([this](const auto& id){show_balloon_properties(id);});
    canvas_->set_manual_properties_callback([this](const auto& id,int end){show_dimension_properties(id,end);});
    canvas_->dimension_value_=[this](const auto& view,const auto& key,QPointF point){edit_model_dimension_value(view,key,point);};
    canvas_->set_dimension_properties_callback([this](const auto& view,const auto& key){edit_model_dimension(view,key);});
    canvas_->set_changed_callback([this] {
        sync_workspace_document();
        update_action_states();
    });
    canvas_->set_selection_changed_callback([this] {
        update_action_states();
        if (selection_handler_) selection_handler_(canvas_->tree_selection_ids());
        const auto identifier = document_.dimension_identifiers.identifier(
            document_.document_id, "dimension:" + canvas_->selected_dimension_id());
        if (!identifier.empty()) set_status_message(tr("Kóta %1").arg(QString::fromStdString(identifier)));
    });
    canvas_->set_context_actions(insert_view_action_,edit_view_action_,projected_view_action_,delete_view_action_);
    canvas_->set_title_block_action(edit_title_block_action_);
    canvas_->set_text_properties([this](const auto& id){show_text_properties(id);});
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
        spin->setRange(0.001, 1000.0); spin->setDecimals(3); spin->setValue(1.0);
    }
    scale_numerator_->setObjectName("drawingSheetScaleNumerator");scale_denominator_->setObjectName("drawingSheetScaleDenominator");
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
    source_variant_->setToolTip(tr("Zdrojový díl nebo sestava a jeho varianty Family Table."));
    connect(source_variant_,&QComboBox::activated,this,[this](int index){
        if(!workspace_||view_dialog_)return;
        const auto* sheet=active_sheet();if(!sheet)return;
        const auto source=source_variant_->itemData(index).toString().toStdString();
        const auto current=selected_source_id();
        if(source==current)return;
        try {
            const auto root=source.substr(0,source.find(":family:"));
            if(!workspace_->find(root)) {
                auto path=document_.data_source_path(root);
                if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
                if(QString::fromStdWString(path.extension().wstring()).compare(".prtz",Qt::CaseInsensitive)==0) {
                    std::vector<zima::kernel::BodyResult> cache;
                    auto model=zima::document::PartDocument::load(path,&cache);
                    if(model.document_id!=root)throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                    workspace_->add_part(std::move(model),std::move(cache),path);
                } else {
                    auto model=zima::assembly::AssemblyDocument::load(path);
                    if(model.document_id!=root)throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                    workspace_->add_assembly(std::move(model),path);
                }
            }
            const auto separator=source.find(":family:");
            if(separator!=std::string::npos&&!workspace_->find(source)) {
                const auto owner=source.substr(0,separator),row_id=source.substr(separator+8);
                const auto table=zima::workspace::family_table(*workspace_,owner);
                const auto row=std::ranges::find(table.instances,row_id,&zima::document::FamilyInstance::id);
                if(row==table.instances.end())throw std::runtime_error("Family Table variant no longer exists.");
                zima::kernel::OcctKernel kernel;
                static_cast<void>(zima::workspace::open_family_instance(*workspace_,kernel,owner,row->name,false));
            }
            zima::workspace::select_family_drawing_source(document_,*workspace_,sheet->id,source);refresh();
        }
        catch(const std::exception& error){update_source_variant();set_status_message(tr(error.what()));}
    });
    auto* settings=new QToolButton(central);settings->setObjectName("drawingSettingsButton");
    settings->setIcon(resource_icon("settings"));settings->setToolTip(tr("Nastavení výkresu"));
    connect(settings,&QToolButton::clicked,this,[this]{show_drawing_settings();});bottom->addWidget(settings);
    bottom->addWidget(new QLabel(tr("Zdroj:"), central));
    bottom->addWidget(source_variant_);
    bottom->addWidget(sheets_, 1); bottom->addWidget(remove_sheet);
    bottom->addWidget(add_sheet); bottom->addSpacing(16);
    bottom->addWidget(new QLabel(tr("Tloušťky:"), central));
    bottom->addWidget(lineweight_mode_);
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
        auto* sheet=active_sheet();if(!sheet||index<0)return;auto value=zima::workspace::sheet_settings(*sheet);value.format=static_cast<zima::drawing::SheetFormat>(index);
        try{set_sheet_settings(sheet->id,value);}catch(const std::exception& e){set_status_message(QString::fromUtf8(e.what()));refresh(false);}
    });
    const auto change_scale=[this]{
        auto* sheet=active_sheet();if(!sheet)return;auto value=zima::workspace::sheet_settings(*sheet);value.scale=scale_numerator_->value()/scale_denominator_->value();
        try{set_sheet_settings(sheet->id,value);}catch(const std::exception& e){refresh(false);set_status_message(QString::fromUtf8(e.what()));}
    };
    connect(scale_numerator_,&QDoubleSpinBox::valueChanged,this,[change_scale](double){change_scale();});
    connect(scale_denominator_,&QDoubleSpinBox::valueChanged,this,[change_scale](double){change_scale();});
    connect(projection_method_,&QComboBox::currentIndexChanged,this,[this](int index){
        auto* sheet=active_sheet();if(!sheet||index<0)return;auto value=zima::workspace::sheet_settings(*sheet);value.projection=index==0?zima::drawing::ProjectionMethod::FirstAngle:zima::drawing::ProjectionMethod::ThirdAngle;
        try{set_sheet_settings(sheet->id,value);}catch(const std::exception& e){refresh(false);set_status_message(QString::fromUtf8(e.what()));}
    });
    layout->addWidget(canvas_, 1); layout->addWidget(sheet_controls_); layout->addWidget(state_);
    setCentralWidget(central);
    connect(sheets_, &QTabBar::currentChanged, this, [this] { if(title_dialog_)title_dialog_->reject();refresh(false); });
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
    document_.sheets.front().name=tr("List %1").arg(1).toStdString();
    workspace_document_id_.clear();
    if(workspace_!=nullptr) {
        workspace_->add_drawing(document_); workspace_document_id_=document_.document_id;
        workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
    }
    refresh(false);
}
void DrawingWindow::hideEvent(QHideEvent* event) {
    if(!event->spontaneous()&&title_dialog_)title_dialog_->reject();
    QMainWindow::hideEvent(event);
}
void DrawingWindow::edit_workspace_document(const std::string& document_id) {
    if(workspace_==nullptr) return;
    auto* state=workspace_->open_drawing(document_id); if(state==nullptr) return;
    if(workspace_document_id_!=document_id&&title_dialog_)title_dialog_->reject();
    if (workspace_document_id_ == document_id && canvas_->interacting()) return;
    if (view_dialog_) view_dialog_->reject();
    canvas_->cancel_placement();
    workspace_document_id_=document_id; document_=state->document(); path_=state->path; refresh(false);
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
void DrawingWindow::select_tree_entities(const std::vector<std::string>& ids,const std::string& current) {
    if(view_dialog_||!canvas_->accepts_tree_selection())return;
    const auto target=current.empty()?(ids.empty()?std::string{}:ids.front()):current;
    for(std::size_t i=0;i<document_.sheets.size();++i) {
        const auto keys=DrawingCanvas::sheet_entities(document_.sheets[i]);
        if(std::ranges::any_of(keys,[&](const auto& key){return DrawingCanvas::tree_id(key)==target;})) {
            if(sheets_->currentIndex()!=int(i))sheets_->setCurrentIndex(int(i));
            break;
        }
    }
    canvas_->select_tree_entities(ids);
}
void DrawingWindow::populate_selection_menu(QMenu& menu) {canvas_->populate_selection_menu(menu);}
void DrawingWindow::erase_selected_entities() {canvas_->erase_selected_entities();}
std::optional<QPointF> DrawingWindow::view_label_center_for_test(const std::string& id,bool section)const{return canvas_->label_center(id,section);}
std::optional<QPointF> DrawingWindow::annotation_handle_for_test(const std::string& id,int end,bool dimension)const{return canvas_->annotation_point(id,end,dimension);}
std::optional<QPointF> DrawingWindow::detail_label_handle_for_test(const std::string& id)const{return canvas_->detail_label_handle(id);}
void DrawingWindow::load_frame_for_test(const std::filesystem::path& path) {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    zima::workspace::load_drawing_template(document_,sheet->id,path,false); refresh();
}
std::optional<QPointF> DrawingWindow::view_rectangle_center_for_test(const std::string& id)const {
    return canvas_->rectangle_center(id);
}
std::optional<QPointF> DrawingWindow::title_field_center_for_test(const std::string& id) const {
    return canvas_->title_field_center(id);
}
std::optional<std::string> DrawingWindow::title_field_text_for_test(const std::string& id) const {
    return canvas_->title_field_text(id);
}
void DrawingWindow::load_title_block_for_test(const std::filesystem::path& path) {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    zima::workspace::load_drawing_template(document_,sheet->id,path,true,workspace_,path_); refresh();
}
void DrawingWindow::open_document() {
    const auto path = open_file(this, tr("Otevřít výkres"), {}, tr("Výkres ZIMA-CAD (*.drwz)"));
    if (path.isEmpty()) return;
    try {
        document_ = zima::drawing::DrawingDocument::load(path.toStdString()); path_ = path.toStdString();
        workspace_document_id_.clear();
        if(workspace_!=nullptr) {
            if(auto* existing=workspace_->open_drawing(document_.document_id)) {
                document_=existing->document(); path_=existing->path; }
            else workspace_->add_drawing(document_,path_);
            workspace_document_id_=document_.document_id;
            workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
        }
        refresh(false);
    }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze otevřít výkres"), error.what()); }
}
void DrawingWindow::quick_export(bool pdf) {
    if(path_.empty()) {set_status_message(tr("Před rychlým exportem uložte výkres."));return;}
    const auto* sheet=active_sheet();if(!sheet)return;
    try {
        const auto settings=ApplicationSettings::load();
        const auto relative=QDir::fromNativeSeparators(pdf?settings.drawing_pdf_directory:settings.drawing_dxf_directory);
        if(relative.trimmed().isEmpty()||QDir::isAbsolutePath(relative)||relative.contains(':')) {
            set_status_message(tr("Složka rychlého exportu musí být relativní k výkresu."));return;
        }
        const auto directory=std::filesystem::absolute(path_).parent_path()/std::filesystem::path(relative.toStdWString());
        auto filename=path_.stem();
        if(!pdf)filename+=std::filesystem::path("_"+std::to_string(sheet-document_.sheets.data()+1));
        filename+=pdf?".pdf":".dxf";
        std::filesystem::create_directories(directory);
        const auto target=directory/filename;
        if(pdf)export_pdf(target);else export_dxf(target);
        set_status_message(tr("Export uložen: %1").arg(QString::fromStdWString(target.wstring())));
    }catch(const std::exception& error){set_status_message(tr(error.what()));}
}
void DrawingWindow::save_pdf() {
    auto suggested=path_.empty()?std::filesystem::path(document_.name+".pdf"):path_;
    suggested.replace_extension(".pdf");
    const auto path=save_file(this,tr("Uložit jako PDF"),QString::fromStdString(suggested.string()),tr("PDF (*.pdf)"),"pdf");
    if(path.isEmpty())return;
    try {export_pdf(path.toStdString());set_status_message(tr("PDF uloženo. Tiskněte ve skutečné velikosti (100 %)."));}
    catch(const std::exception& error){set_status_message(tr(error.what()));}
}
QRectF DrawingWindow::sheet_rectangle_for_test()const{return canvas_->sheet_rectangle();}
std::optional<QPointF> DrawingWindow::witness_grip_for_test(const std::string& id,const std::string& edit,int end)const{return canvas_->witness_grip(id,edit,end);}
void DrawingWindow::export_dxf(const std::filesystem::path& path) {
    auto* sheet=active_sheet();
    if(!sheet) throw std::runtime_error("Drawing has no active sheet");
    drawing_render::export_dxf(document_,sheet->id,path,path_,workspace_,true);
}
void DrawingWindow::export_jpg(const std::filesystem::path& path) {
    drawing_render::write_image(canvas_->grab().toImage(),path,true);
}
void DrawingWindow::export_pdf(const std::filesystem::path& path) {
    drawing_render::export_pdf(document_,path,path_,workspace_,true);
}
void DrawingWindow::save_document() {
    auto path = path_.empty() ? save_file(this, tr("Uložit výkres"), "drawing.drwz", tr("Výkres ZIMA-CAD (*.drwz)"), "drwz") : QString::fromStdString(path_.string());
    if (path.isEmpty()) return;
    if (!path.endsWith(".drwz", Qt::CaseInsensitive)) path += ".drwz";
    try {
        if(workspace_ && workspace_->open_drawing(workspace_document_id_)) {
            auto pending=zima::workspace::prepare_document_save_if_needed(
                *workspace_,workspace_document_id_,path.toStdString());
            if(!pending)return;
            const auto saved=pending->write();
            if(!zima::workspace::complete_document_save(*workspace_,saved))
                throw std::runtime_error("Drawing was closed or retargeted during saving");
            const auto* state=workspace_->open_drawing(workspace_document_id_);
            document_=state->document();path_=state->path;
        } else { document_.save(path.toStdString());path_=path.toStdString(); }
        sync_workspace_document(false); set_status_message(tr("Výkres uložen."));
    }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze uložit výkres"), error.what()); }
}
void DrawingWindow::add_sheet() {
    const auto inherited=selected_source_id();
    zima::workspace::SheetSettings settings;settings.name=tr("List %1").arg(document_.sheets.size()+1).toStdString();
    zima::workspace::create_drawing_sheet(document_,settings);
    auto& added=document_.sheets.back();added.selected_source_document_id=inherited;
    refresh();sheets_->setCurrentIndex(static_cast<int>(document_.sheets.size()-1));
}
void DrawingWindow::remove_sheet() {
    const auto* sheet=active_sheet();if(!sheet||document_.sheets.size()<=1)return;
    try{zima::workspace::delete_drawing_sheet(document_,sheet->id);refresh();}catch(const std::exception& e){set_status_message(QString::fromUtf8(e.what()));}
}
void DrawingWindow::set_sheet_settings(const std::string& id,const zima::workspace::SheetSettings& settings){
    if(zima::workspace::set_drawing_sheet(document_,id,settings,[this](const auto& view){
        auto path=view.source_path;if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
        return zima::workspace::read_drawing_source(workspace_,path,view.source_document_id).second;
    }))refresh();
}
void DrawingWindow::edit_sheet() {
    const auto* sheet=active_sheet();if(!sheet||raise_open_properties(window()))return;
    auto* dialog=new SheetPropertiesDialog(this,*sheet,[this,id=sheet->id](const auto& accepted){set_sheet_settings(id,zima::workspace::sheet_settings(accepted));});
    view_dialog_=dialog;if(properties_handler_)properties_handler_(dialog);
    connect(dialog,&QDialog::finished,this,[this,dialog]{if(view_dialog_==dialog){view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);}update_action_states();});
    dialog->show();update_action_states();
}
void DrawingWindow::load_frame() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst formát"),formats_directory_,
                                                 tr("Formát výkresu (*.frmz)"));
    if(path.isEmpty()) return;
    try { zima::workspace::load_drawing_template(document_,sheet->id,std::filesystem::u8path(path.toStdString()),false); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst formát"),error.what()); }
}
void DrawingWindow::remove_frame() {
    const auto* sheet=active_sheet();if(sheet&&zima::workspace::clear_drawing_template(document_,sheet->id,false))refresh();
}
void DrawingWindow::remove_title_block() {
    const auto* sheet=active_sheet();if(sheet&&zima::workspace::clear_drawing_template(document_,sheet->id,true))refresh();
}
void DrawingWindow::load_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst razítko"),formats_directory_,
                                                 tr("Razítko výkresu (*.tblz)"));
    if(path.isEmpty()) return;
    try { zima::workspace::load_drawing_template(document_,sheet->id,std::filesystem::u8path(path.toStdString()),true,workspace_,path_); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst razítko"),error.what()); }
}
void DrawingWindow::edit_title_block() {
    auto* sheet=active_sheet();if(!sheet)return;
    const auto selected_symbol=canvas_->selected_title_field();
    if(selected_symbol.starts_with("symbol:")) {
        if(raise_open_properties(window()))return;
        const auto id=selected_symbol.substr(7),sheet_id=sheet->id;
        const auto found=std::ranges::find(sheet->title_block_symbols,id,&sketcher::SymbolInstance::id);
        if(found==sheet->title_block_symbols.end())return;
        auto* dialog=new SymbolDialog(*found,{},[this,id,sheet_id](auto symbol){
            auto* target=document_.find_sheet(sheet_id);if(!target)return;
            auto item=std::ranges::find(target->title_block_symbols,id,&sketcher::SymbolInstance::id);
            if(item==target->title_block_symbols.end())return;
            *item=std::move(symbol);refresh();
        },this);
        view_dialog_=dialog;if(properties_handler_)properties_handler_(dialog);
        connect(dialog,&QDialog::finished,this,[this,dialog]{if(view_dialog_==dialog){view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);}update_action_states();});
        dialog->show();update_action_states();return;
    }
    if(sheet->title_block_fields.empty()&&sheet->title_block_texts.empty())return;
    if(raise_open_properties(window()))return;
    try {
        const auto selected=canvas_->selected_title_target();std::string bom_row;
        if(selected&&selected->bom_row) {
            if(*selected->bom_row>=sheet->bom_rows.size())throw std::runtime_error("The stored BOM row does not exist.");
            bom_row=sheet->bom_rows[*selected->bom_row].designation;
        }
        const auto edit=workspace::prepare_drawing_title_edit(document_,sheet->id,workspace_,path_,bom_row);
        auto focus=canvas_->selected_title_field();
        if(selected)if(const auto field=workspace::drawing_title_focus_field(edit,selected->expression);!field.empty())focus=field;
        auto* dialog=new TitleBlockPropertiesDialog(this,edit,*sheet,[this,edit](const auto& changes) {
            if(workspace::edit_drawing_title(document_,workspace_,edit,changes).changed)refresh();
        });
        title_dialog_=dialog;
        if(properties_handler_)properties_handler_(dialog);
        connect(dialog,&QDialog::finished,this,[this,dialog]{if(title_dialog_==dialog){title_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);}});
        dialog->show();
        if(auto* choices=dialog->findChild<QComboBox*>(QString::fromStdString("titleBlockChoices:"+focus))) {
            choices->setFocus();if(choices->isEditable())choices->lineEdit()->selectAll();
        } else if(auto* editor=dialog->findChild<QLineEdit*>(QString::fromStdString("titleBlockField:"+focus))){editor->setFocus();editor->selectAll();}
    } catch(const std::exception& error){set_status_message(QObject::tr(error.what()));}
}
zima::drawing::DrawingSheet* DrawingWindow::active_sheet() { const auto index = sheets_->currentIndex(); return index < 0 || index >= static_cast<int>(document_.sheets.size()) ? nullptr : &document_.sheets[index]; }

void DrawingWindow::insert_view() {
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) return;
    const auto* sheet=active_sheet(); if (!sheet) return;
    const auto sheet_id=sheet->id,drawing_id=document_.document_id;
    auto source_id=selected_source_id();
    auto source_path=document_.data_source_path(source_id);
    try {
        if(source_id.empty()&&source_path.empty()) {
            set_status_message(tr("Výkres nemá zdrojový díl ani sestavu."));return;
        }
        if (!source_path.empty() && source_path.is_relative() && !path_.empty())
            source_path=path_.parent_path()/source_path;
        auto projection=std::make_shared<zima::workspace::DrawingProjection>(workspace_,path_);
        if(document_.document_id!=drawing_id)return;
        sheet=document_.find_sheet(sheet_id);if(!sheet)return;
        auto view=zima::drawing::DrawingDocument::create_view(source_id,source_path,{},
            zima::drawing::ViewOrientation::Isometric);
        const auto style=ApplicationSettings::load().drawing_view_style;
        view.display_style=style=="visible_edges"?drawing::DisplayStyle::VisibleEdges:
            style=="shaded"?drawing::DisplayStyle::Shaded:style=="shaded_with_edges"?drawing::DisplayStyle::ShadedWithEdges:drawing::DisplayStyle::HiddenEdges;
        for(const auto& existing:sheet->views)if(existing.source_document_id==source_id){view.output_source=existing.output_source;view.measurement_geometry=existing.measurement_geometry;break;}
        view.name=workspace::next_drawing_view_name(document_,tr("Pohled").toStdString());
        view.scale=sheet->default_scale; view.use_sheet_scale=true;
        canvas_->begin_placement(std::move(view), [this,projection](auto placed) {
            show_view_properties(std::move(placed),true,projection);
        }, [this] { start_selection(); });
        set_status_message(tr("Vložit pohled: klikněte na místo na listu. Esc zruší vložení."));
    } catch (const std::exception& error) {
        start_selection();set_status_message(QObject::tr(error.what()));
    }
}

void DrawingWindow::insert_detail() {
    if(view_dialog_||raise_open_properties(window())||!active_sheet()||active_sheet()->views.empty())return;
    drawing::DrawingView detail;detail.id=kernel::make_stable_id();detail.name=drawing::next_detail_name(document_);
    detail.detail_view=true;detail.use_sheet_scale=false;detail.show_caption=true;
    detail.scale=active_sheet()->default_scale*2;
    show_detail_properties(std::move(detail),true);
}
void DrawingWindow::show_detail_properties(drawing::DrawingView view,bool creating) {
    if(view_dialog_||raise_open_properties(window()))return;
    const auto* sheet=active_sheet();if(!sheet)return;
    const auto sheet_id=sheet->id,document_id=document_.document_id;
    auto* dialog=new DrawingDetailDialog(std::move(view),window());view_dialog_=dialog;
    const QPointer<DrawingDetailDialog> guarded(dialog);
    dialog->preview=[this](auto value) {
        if(const auto* parent=document_.find_view(value.parent_view_id)){drawing::refresh_detail_view(value,*parent);canvas_->set_preview(std::move(value));}
    };
    dialog->commit=[this,sheet_id,document_id,creating](auto value) {
        if(document_.document_id!=document_id)throw std::runtime_error("The Drawing changed while editing the detail.");
        workspace::DrawingProjection projection(workspace_,path_);
        workspace::edit_drawing_view(document_,sheet_id,value,creating,projection);
        canvas_->set_preview({});refresh();canvas_->select_view_for_test(value.id);
    };
    const auto resume=[this,guarded] {canvas_->set_preview({});if(guarded){guarded->show();guarded->raise();guarded->resume();}};
    dialog->place=[this,guarded,resume] {
        if(!guarded)return;auto detail=guarded->values();const auto* parent=document_.find_view(detail.parent_view_id);
        if(!parent||!detail.crop)return;
        drawing::refresh_detail_view(detail,*parent);guarded->hide();
        canvas_->begin_placement(std::move(detail),[guarded,resume](auto placed){if(guarded)guarded->set_placed(std::move(placed));resume();},resume,
            [](auto& pending,auto point){pending.x=point.x+pending.crop->anchor.x*pending.scale;pending.y=point.y-pending.crop->anchor.y*pending.scale;});
        canvas_->unsetCursor();
    };
    const auto boundary=[this,guarded,resume](std::optional<drawing::Point2> anchor) {
        if(!guarded)return;const auto detail=guarded->values();const auto* parent=document_.find_view(detail.parent_view_id);if(!parent)return;
        auto editing=*parent;
        const bool modify=detail.crop&&int(detail.crop->shape)==guarded->shape()&&!anchor;
        editing.crop=modify?detail.crop:std::nullopt;
        guarded->hide();canvas_->set_preview({});
        canvas_->begin_crop(std::move(editing),modify?3:guarded->shape(),[guarded,resume](auto crop,bool accepted){
            if(!guarded)return;
            if(accepted&&crop){guarded->set_boundary(*crop);guarded->place();}else resume();
        },anchor);
        canvas_->unsetCursor();
    };
    dialog->edit_boundary=[boundary]{boundary({});};
    dialog->choose_source=[this,guarded,boundary,resume] {
        if(!guarded)return;guarded->hide();canvas_->set_preview({});
        canvas_->choose_view([this,guarded,boundary](const auto& id){
            const auto* parent=document_.find_view(id);if(!guarded||!parent)return;
            guarded->set_source(*parent);boundary(canvas_->chosen_view_point(*parent));
        },resume);
    };
    if(!creating)dialog->findChild<QPushButton*>("detailSource")->setEnabled(false);
    canvas_->set_preview_move_handler([guarded](auto position){if(guarded)guarded->move(position);});
    connect(dialog,&QDialog::finished,this,[this,guarded] {
        canvas_->start_selection();canvas_->cancel_placement();canvas_->set_preview_move_handler({});canvas_->set_preview({});
        if(view_dialog_==guarded){view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);}
        update_action_states();set_status_message({});
    });
    if(properties_handler_)properties_handler_(dialog);dialog->show();dialog->resume();update_action_states();
}

void DrawingWindow::show_text_properties(const std::string& id) {
    if(view_dialog_){view_dialog_->raise();return;}
    if(raise_open_properties(window()))return;
    const auto* sheet=active_sheet();if(!sheet)return;
    const auto sheet_id=sheet->id,drawing_id=document_.document_id;
    sketcher::SketchText initial;initial.id=id.empty()?"text-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()):id;
    initial.value="";initial.height=2.5;initial.flipped=true;initial.color=sketcher::SketchTextColor::Green;initial.modeling_geometry=false;
    std::optional<std::array<double,2>> anchor;
    if(!id.empty()) {
        const auto found=std::ranges::find(sheet->texts,id,&drawing::DrawingText::id);if(found==sheet->texts.end())return;
        const auto& t=found->presentation;initial.value=t.text;initial.height=t.height;initial.font=t.font;
        initial.anchor_x=t.position.x;initial.anchor_y=t.position.y;initial.angle_degrees=t.angle;initial.flipped=t.flipped;
        initial.horizontal=t.alignment=="center"?sketcher::TextHorizontalAlignment::Center:t.alignment=="right"?sketcher::TextHorizontalAlignment::Right:sketcher::TextHorizontalAlignment::Left;
        initial.vertical=t.vertical_alignment=="top"?sketcher::TextVerticalAlignment::Top:t.vertical_alignment=="middle"?sketcher::TextVerticalAlignment::Middle:sketcher::TextVerticalAlignment::Bottom;
        initial.color=t.pen==drawing::DrawingPen::White?sketcher::SketchTextColor::White:t.pen==drawing::DrawingPen::Yellow?sketcher::SketchTextColor::Yellow:t.pen==drawing::DrawingPen::Red?sketcher::SketchTextColor::Red:sketcher::SketchTextColor::Green;
        anchor=std::array{t.position.x,t.position.y};
    }
    const auto convert=[](const sketcher::SketchText& value) {
        drawing::DrawingText result;result.id=value.id;auto& t=result.presentation;
        t.text=value.value;t.position={value.anchor_x,value.anchor_y};t.height=value.height;t.angle=value.angle_degrees;t.flipped=value.flipped;t.font=value.font;
        t.alignment=value.horizontal==sketcher::TextHorizontalAlignment::Center?"center":value.horizontal==sketcher::TextHorizontalAlignment::Right?"right":"left";
        t.vertical_alignment=value.vertical==sketcher::TextVerticalAlignment::Top?"top":value.vertical==sketcher::TextVerticalAlignment::Middle?"middle":"bottom";
        t.pen=value.color==sketcher::SketchTextColor::White?drawing::DrawingPen::White:value.color==sketcher::SketchTextColor::Yellow?drawing::DrawingPen::Yellow:value.color==sketcher::SketchTextColor::Red?drawing::DrawingPen::Red:drawing::DrawingPen::Green;
        return result;
    };
    canvas_->start_selection();
    auto* dialog=new SketchTextPropertiesDialog(initial,anchor,
        [this,convert](const auto& value){canvas_->set_text_preview(value?std::optional{convert(*value)}:std::nullopt);},
        [this,convert,sheet_id,drawing_id](auto value){
            if(document_.document_id!=drawing_id)throw std::runtime_error("The Drawing changed while editing text.");
            auto* sheet=document_.find_sheet(sheet_id);if(!sheet)throw std::runtime_error("The sheet no longer exists.");
            auto found=std::ranges::find(sheet->texts,value.id,&drawing::DrawingText::id);
            if(found==sheet->texts.end())sheet->texts.push_back(convert(value));else *found=convert(value);
            canvas_->set_text_preview({});refresh();
        },window(),true,true);
    view_dialog_=dialog;canvas_->set_text_editor(dialog);
    if(properties_handler_)properties_handler_(dialog);
    connect(dialog,&QDialog::finished,this,[this]{canvas_->set_text_editor(nullptr);canvas_->set_text_preview({});view_dialog_=nullptr;if(properties_handler_)properties_handler_(nullptr);update_action_states();});
    dialog->show();update_action_states();
    set_status_message(tr("Text: napište více řádků a kliknutím na list určete polohu. OK uloží, Cancel zruší."));
}

void DrawingWindow::commit_view(drawing::DrawingView accepted,const std::string& sheet_id,bool creating,
    workspace::DrawingProjection* cache) {
    QElapsedTimer timer;timer.start();
    const auto stamp=[&](const char* stage){if(qEnvironmentVariableIsSet("ZIMA_DRAWING_PROFILE_COMMIT"))std::fprintf(stderr,"commit %s %.3f ms\n",stage,timer.nsecsElapsed()/1e6);};
    workspace::Workspace source_models;
    if(workspace_)for(const auto& state:workspace_->documents())
        if(!std::holds_alternative<workspace::DrawingState>(state))source_models.documents().push_back(state);
    const auto publish_family=prepare_drawing_family_variant(workspace_,source_models,accepted,path_);
    stamp("source workspace");
    workspace::DrawingProjection projection(&source_models,path_,cache);
    auto next_document=document_;
    workspace::edit_drawing_view(next_document,sheet_id,accepted,creating,projection,true);
    stamp("edit projections");
    const auto id=accepted.id;
    const auto* result=next_document.find_view(id);
    std::function<void()> commit_source=[]{};
    if(result->section_snapshot) {
        const auto& source=projection.source(*result);
        const auto original=std::ranges::find(source.sections,result->section_id,&document::SectionDefinition::id);
        commit_source=prepare_section_component_commit(workspace_,result->source_document_id,source.path,*result->section_snapshot,
            original==source.sections.end()?nullptr:&*original);
    }
    commit_source();publish_family();document_=std::move(next_document);
    stamp("publish");
    canvas_->set_preview({});refresh();canvas_->select_view_for_test(id);
    stamp("refresh");
}

void DrawingWindow::show_view_properties(zima::drawing::DrawingView view, bool creating,
    std::shared_ptr<zima::workspace::DrawingProjection> cache) {
    if(view.detail_view){show_detail_properties(std::move(view),creating);return;}
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) { canvas_->set_preview({}); return; }
    auto* sheet=active_sheet(); if (!sheet) return;
    const auto sheet_id=sheet->id;
    const auto drawing_id=document_.document_id;
    std::vector<DrawingSourceChoice> sources;
    std::set<std::pair<std::string,std::filesystem::path>> added_families;
    const auto add_family=[&](const std::string& id,auto path) {
        if(!path.empty()&&path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
        if(!added_families.emplace(id.substr(0,id.find(":family:")),path.lexically_normal()).second)return;
        const auto choices=family_source_choices(workspace_,id,path);
        for(const auto& choice:choices)if(std::ranges::none_of(sources,[&](const auto& item){return item.id==choice.id;}))sources.push_back(choice);
    };
    try{if(!view.source_document_id.empty()||!view.source_path.empty())add_family(view.source_document_id,view.source_path);}catch(const std::exception&){}
    for(const auto& source:document_.data_sources())try{auto path=source.source_path;if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;add_family(source.document_id,path);}catch(const std::exception&){}
    if(creating&&workspace_)for(const auto& state:workspace_->documents())std::visit([&](const auto& item){
        if constexpr(requires{item.session;})if(item.session.document().family.parent_id.empty())add_family(item.session.document().document_id,item.path);
    },state);
    if((!view.source_document_id.empty()||!view.source_path.empty())&&std::ranges::none_of(sources,[&](const auto& source){return source.id==view.source_document_id;}))
        sources.push_back({view.source_document_id,view.source_path,QString::fromStdString(view.source_path.filename().string())});
    for(auto& source:sources)if(!view.source_path.empty()&&source.id==view.source_document_id)source.path=view.source_path;
    if(!cache)cache=std::make_shared<zima::workspace::DrawingProjection>(workspace_,path_);
    const auto geometry_key=[](const drawing::DrawingView& value){
        const auto& c=value.camera;
        const auto number=[](double x){return std::bit_cast<std::uint64_t>(x);};
        return nlohmann::json::array({value.source_document_id,document::path_to_utf8(value.source_path),
            number(c.horizontal.x),number(c.horizontal.y),number(c.horizontal.z),number(c.vertical.x),number(c.vertical.y),number(c.vertical.z),number(c.depth.x),number(c.depth.y),number(c.depth.z),
            value.section_id,document::serialize_sections(value.section_snapshot?std::vector{*value.section_snapshot}:std::vector<document::SectionDefinition>{})}).dump();
    };
    auto last_geometry=std::make_shared<std::optional<drawing::DrawingView>>(creating?std::nullopt:std::optional{view});
    const auto project=[cache,last_geometry,geometry_key](zima::drawing::DrawingView& value,bool pending_settings=false){
        if(*last_geometry&&geometry_key(**last_geometry)==geometry_key(value)) {
            value.projected_edges=(**last_geometry).projected_edges;value.projected_triangles=(**last_geometry).projected_triangles;
            value.output_source=(**last_geometry).output_source;value.measurement_geometry=(**last_geometry).measurement_geometry;
            value.model_annotations=(**last_geometry).model_annotations;
        }else {cache->project(value,{.pending_hatch=pending_settings,.interactive=true});*last_geometry=value;}
    };
    const auto error=[this](const QString& message) {
        if (auto* dialog=dynamic_cast<ViewPropertiesDialog*>(view_dialog_.data())) dialog->set_error(message);
    };
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new ViewPropertiesDialog(owner ? owner : this, view, std::move(sources),sheet->default_scale,
        [this,error,sheet_id,drawing_id,creating,cache](auto accepted) {
            if (document_.document_id!=drawing_id) return false;
            try {
                commit_view(std::move(accepted),sheet_id,creating,cache.get());
                return true;
            } catch (const std::exception& exception) { error(tr(exception.what())); return false; }
        }, [this,project,error,creating](auto pending) {
            if(!pending){canvas_->set_preview({});return;}
            try { if(!creating)project(*pending,true); canvas_->set_preview(std::move(pending),creating); error({}); }
            catch (const std::exception& exception) { canvas_->set_preview({});error(tr(exception.what())); }
        },[this](const auto& id,auto path){
            if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
            if(QString::fromStdString(path.extension().string()).compare(".asmz",Qt::CaseInsensitive)==0&&
                workspace::read_family_assembly(workspace_,path,id,false).sections.empty())return std::vector<document::SectionDefinition>{};
            return workspace::source_sections(workspace_,id,path);
        },
        [this](const auto& path){return family_source_choices(workspace_,{},path);},
        [this,project](ViewPropertiesDialog* properties,drawing::DrawingView pending){
            try{project(pending,true);}catch(const std::exception& e){properties->set_error(QString::fromUtf8(e.what()));return;}
            auto sheet=*active_sheet();auto owner=window();QPointer<ViewPropertiesDialog> guarded(properties);
            auto* editor=new DrawingBreakEditor(owner,std::move(sheet),pending,[guarded](auto breaks){if(guarded)guarded->set_breaks(std::move(breaks));});
            properties->hide();canvas_->set_preview({});canvas_->setEnabled(false);
            connect(editor,&QDialog::finished,this,[this,guarded,editor]{canvas_->setEnabled(true);if(guarded){guarded->show();guarded->raise();guarded->resume_preview();}editor->deleteLater();});editor->show();
        },[this,project](ViewPropertiesDialog* properties,drawing::DrawingView pending,int mode,std::string hatch_section){
            try{project(pending,true);}catch(const std::exception& e){properties->set_error(tr(e.what()));return;}
            QPointer<ViewPropertiesDialog> guarded(properties);properties->hide();
            if(!hatch_section.empty()){pending.crop.reset();if(auto found=pending.section_hatch_crops.find(hatch_section);found!=pending.section_hatch_crops.end())pending.crop=found->second;pending.section_hatch_crops.erase(hatch_section);}
            canvas_->begin_crop(std::move(pending),mode,[this,guarded,hatch_section](auto crop,bool accepted){
                if(guarded){if(accepted){if(hatch_section.empty())guarded->set_crop(std::move(crop));else guarded->set_hatch_crop(hatch_section,std::move(crop));}guarded->show();guarded->raise();guarded->resume_preview();}
                set_status_message({});
            });
        },creating);
    view_dialog_=dialog;
    canvas_->set_preview_move_handler([dialog=QPointer<ViewPropertiesDialog>(dialog)](auto position){if(dialog)dialog->move_preview(position);});
    if (properties_handler_) properties_handler_(dialog);
    dialog->resume_preview();
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
        auto projection=std::make_shared<workspace::DrawingProjection>(workspace_,path_);
        auto view=zima::drawing::DrawingDocument::create_view(parent_copy.source_document_id,parent_copy.source_path,{});
        view.output_source=parent_copy.output_source;view.measurement_geometry=parent_copy.measurement_geometry;
        view.name=workspace::next_drawing_view_name(document_,tr("Pohled").toStdString()); view.parent_view_id=parent_copy.id;
        view.scale=parent_copy.scale; view.use_sheet_scale=parent_copy.use_sheet_scale;
        view.display_style=parent_copy.display_style;
        view.hidden_edge_style=parent_copy.hidden_edge_style;view.tangent_edge_style=parent_copy.tangent_edge_style;
        view.show_thread_leadins=parent_copy.show_thread_leadins;view.show_caption=parent_copy.show_caption;
        view.show_section_label=parent_copy.show_section_label;view.show_dimension_guides=parent_copy.show_dimension_guides;
        view.dimension_guide_offset=parent_copy.dimension_guide_offset;view.dimension_guide_spacing=parent_copy.dimension_guide_spacing;
        view.dimension_guide_count=parent_copy.dimension_guide_count;
        canvas_->begin_placement(std::move(view), [this,projection,sheet_id=sheet->id,drawing_id=document_.document_id](auto placed) {
            if(document_.document_id!=drawing_id)return;
            try {commit_view(std::move(placed),sheet_id,true,projection.get());
                set_status_message(tr("Výběr: kliknutím do obdélníkové oblasti vyberte pohled."));update_action_states();}
            catch(const std::exception& exception){start_selection();QMessageBox::warning(this,tr("Projekční pohled"),tr(exception.what()));}
        }, [this] { start_selection(); },
        [this,parent_copy,method=sheet->projection_method](auto& pending,auto point) {
            const double dx=point.x-parent_copy.x, dy=point.y-parent_copy.y;
            constexpr double quarter_turn=0.7853981633974483;
            const int sector=(static_cast<int>(std::lround(std::atan2(dy,-dx)/quarter_turn))+8)%8;
            const auto direction=static_cast<zima::drawing::ProjectionDirection>(sector+1);
            if (direction!=pending.projection_direction) {
                pending.projection_direction=direction;
                pending.camera=zima::drawing::projected_camera(parent_copy.camera,direction,method);
                canvas_->set_placement_frame(placement_frame(pending));
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
void DrawingWindow::select_manual_dimension(const std::string& view,const std::string& id) {
    if(view_dialog_)return;
    select_view(view);canvas_->select_manual(view,id);
}
void DrawingWindow::select_model_annotation(const std::string& view,const std::string& key) {
    if(view_dialog_)return;
    select_view(view);canvas_->select_model(view,key);
}
void DrawingWindow::edit_model_dimension_value(const std::string& view_id,const std::string& key,QPointF point) {
    if(view_dialog_||!workspace_)return;
    const auto* view=document_.find_view(view_id);if(!view)return;
    const auto found=std::ranges::find_if(view->model_annotations,[&](const auto& a){return model_annotation_key(a.source)==key;});
    if(found==view->model_annotations.end()||!found->model_dimension)return;
    const auto& dimension=*found->model_dimension;
    if(found->unresolved||!dimension.driving||dimension.locked||!dimension.display_text_override.empty()) {
        set_status_message(tr("Tato kóta je pouze pro čtení."));return;
    }
    if(inline_dimension_edit_) {inline_dimension_edit_->setProperty("cancelled",true);inline_dimension_edit_->deleteLater();}
    auto* edit=new InlineDimensionEdit(canvas_);inline_dimension_edit_=edit;
    edit->setText(QString::fromStdString(kernel::dimension_number(dimension.value,ui::numeric_decimal_places(this))));
    edit->move(std::clamp(qRound(point.x())-52,0,std::max(0,canvas_->width()-104)),
        std::clamp(qRound(point.y())-14,0,std::max(0,canvas_->height()-28)));
    const QPointer<InlineDimensionEdit> guarded(edit);
    const auto commit=[this,guarded,view_id,reference=found->source] {
        if(!guarded||guarded->property("cancelled").toBool()||guarded->property("committed").toBool())return;
        double value;
        try {value=numeric_expression_value(guarded->text());if(!std::isfinite(value))throw std::invalid_argument("Invalid value");}
        catch(const std::exception&) {guarded->setStyleSheet(guarded->styleSheet()+" QLineEdit { border-color:#C64B4B; }");guarded->selectAll();return;}
        const double factor=std::pow(10.0,ui::numeric_decimal_places(this));
        value=std::round(value*factor)/factor;
        guarded->setProperty("committed",true);
        try {
            kernel::OcctKernel kernel;
            workspace::set_drawing_model_dimension(*workspace_,kernel,document_,path_,view_id,reference,value);
            guarded->hide();guarded->deleteLater();refresh();
            set_status_message(tr("Rozměr modelu i výkres byly aktualizovány."));
        } catch(const std::exception& error) {
            guarded->hide();guarded->deleteLater();set_status_message(tr(error.what()));
        }
    };
    connect(edit,&QLineEdit::returnPressed,this,commit);
    connect(edit,&QLineEdit::editingFinished,this,commit);
    edit->show();edit->raise();edit->setFocus(Qt::MouseFocusReason);edit->selectAll();
}
void DrawingWindow::edit_model_dimension(const std::string& view_id,const std::string& key) {
    if(view_dialog_)return;
    const auto* view=document_.find_view(view_id);if(!view)return;
    const auto item=std::ranges::find_if(view->model_annotations,[&](const auto& a){return model_annotation_key(a.source)==key;});
    if(item==view->model_annotations.end()||!item->model_dimension)return;
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new DimensionPropertiesDialog(*item->model_dimension,item->view_layout.value_or(item->model_layout),[this,view_id,reference=item->source](auto layout){
        if(workspace::set_drawing_annotation_layout(document_,view_id,reference,layout))sync_workspace_document();
        canvas_->update();
    },owner?owner:this);
    view_dialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&QDialog::finished,this,[this]{view_dialog_=nullptr;if(properties_handler_)properties_handler_(nullptr);update_action_states();});
    if(properties_handler_)properties_handler_(dialog);dialog->show();update_action_states();
}

void DrawingWindow::show_erase(){
    if(view_dialog_){view_dialog_->raise();return;}if(raise_open_properties(window()))return;
    const auto* view=document_.find_view(canvas_->selected_view_id());
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new ShowEraseDialog(view?*view:drawing::DrawingView{},[this](const auto& pending,const auto& offered,const auto& staged){
        canvas_->set_staged_model_previews(staged);
        std::set<std::string> ids;for(const auto& r:offered)ids.insert(model_annotation_key(r));
        canvas_->set_preview(pending);canvas_->set_model_command(std::move(ids),[this,id=pending.id](const auto& key){
            const auto* view=document_.find_view(id);if(!view)return;
            if(auto* dialog=dynamic_cast<ShowEraseDialog*>(view_dialog_.data()))for(const auto& r:view->model_annotations)if(model_annotation_key(r.source)==key){dialog->toggle(r.source);break;}
        });
    },[this](const auto& pending){
        auto next=document_;
        std::vector<workspace::AnnotationVisibility> values;
        for(const auto& view:pending)for(const auto& item:view.model_annotations)
            values.push_back({view.id,item.source,item.visible});
        if(workspace::set_drawing_annotation_visibility(next,values)) {
            document_=std::move(next);sync_workspace_document();if(changed_handler_)changed_handler_();
        }
    },owner?owner:this);
    view_dialog_=dialog;if(properties_handler_)properties_handler_(dialog);
    dialog->set_view_picker_cancel([this]{canvas_->start_selection();});
    dialog->set_view_picker([this,dialog]{
        canvas_->set_model_command({},{});canvas_->set_preview({});
        canvas_->choose_view([this,dialog](const auto& id){if(const auto* view=document_.find_view(id)){canvas_->select_view_for_test(id);dialog->set_view(*view);}});
    });
    connect(dialog,&QDialog::finished,this,[this,dialog]{const auto id=dialog->view_id();canvas_->set_staged_model_previews({});canvas_->start_selection();canvas_->set_model_command({},{});canvas_->set_preview({});view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);refresh(false);canvas_->select_view_for_test(id);});
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();if(!view)dialog->arm_view();update_action_states();
}

void DrawingWindow::regenerate_selected_view() {
    if(view_dialog_)return;
    try {
        const auto count=zima::workspace::regenerate_drawing_views(document_,workspace_,path_);
        if(count)refresh();
        set_status_message(tr("Výkres regenerován."));
    }catch(const std::exception& error){QMessageBox::warning(this,tr("Nelze regenerovat pohled"),tr(error.what()));}
}
void DrawingWindow::delete_selected_view() {
    const auto selected=canvas_->selected_view_id();if(selected.empty()||view_dialog_)return;
    try{zima::workspace::delete_drawing_view(document_,selected);refresh();}
    catch(const std::exception& error){set_status_message(tr(error.what()));}
}
void DrawingWindow::edit_selected_view() {
    if (const auto* view=document_.find_view(canvas_->selected_view_id())) show_view_properties(*view,false);
}
std::optional<QPointF> DrawingWindow::balloon_handle_for_test(const std::string& id,int end)const{return canvas_->balloon_handle(id,end);}
void DrawingWindow::show_balloon_properties(const std::string& id) {
    if(view_dialog_){view_dialog_->raise();return;}if(raise_open_properties(window()))return;
    const auto* sheet=active_sheet();if(!sheet||sheet->views.empty())return;
    const auto sheet_id=sheet->id,doc_id=document_.document_id;
    const auto* state=workspace_?workspace_->open_drawing(doc_id):nullptr;
    const auto generation=state?state->data_generation():0;
    const auto identity=state?state->runtime_identity:nullptr;
    canvas_->start_selection();
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new DrawingBalloonDialog(*sheet,canvas_->selected_view_id(),id,
        [this,sheet_id,doc_id,generation,identity](const auto& values){
            if(document_.document_id!=doc_id)throw std::runtime_error("The Drawing changed while editing balloons.");
            if(workspace_){const auto* current=workspace_->open_drawing(doc_id);if(!current||current->runtime_identity!=identity||current->data_generation()!=generation)throw std::runtime_error("The Drawing changed while editing balloons.");}
            if(workspace::set_drawing_balloons(document_,sheet_id,values))sync_workspace_document();
        },owner?owner:this);
    view_dialog_=dialog;canvas_->set_balloon_command(dialog);
    connect(dialog,&QDialog::finished,this,[this]{canvas_->set_balloon_command(nullptr);view_dialog_=nullptr;if(properties_handler_)properties_handler_(nullptr);refresh(false);});
    if(properties_handler_)properties_handler_(dialog);dialog->show();update_action_states();
}
void DrawingWindow::start_linear_dimension() {show_dimension_properties({},0);}
void DrawingWindow::show_dimension_properties(const std::string& id,int extend) {
    if(view_dialog_){view_dialog_->raise();return;}if(raise_open_properties(window()))return;
    auto* sheet=active_sheet();if(!sheet||sheet->views.empty())return;
    auto value=drawing::make_drawing_dimension(canvas_->selected_view_id());
    if(!id.empty()){
        const auto found=std::ranges::find(sheet->dimensions,id,&drawing::DrawingDimension::id);
        if(found==sheet->dimensions.end())return;value=*found;
    }
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new DrawingDimensionDialog(value,id.empty(),[this](const auto& view){return document_.find_view(view);},
        [this,id,extend](auto committed){
            auto* sheet=active_sheet();if(!sheet)throw std::runtime_error("List již neexistuje.");
            if((extend==3||extend==1||extend<0?workspace::commit_drawing_chain:workspace::edit_drawing_dimension)(document_,sheet->id,std::move(committed),id.empty())) {
                // Chain commits replace the document transactionally, invalidating
                // the canvas and renderer's sheet pointers. Rebind before callbacks.
                canvas_->set_sheet(active_sheet());
                sync_workspace_document();
            }
        },owner?owner:this);
    view_dialog_=dialog;canvas_->set_dimension_command(dialog);
    dialog->witness_requested=[this,dialog](auto kind,const auto& edit){canvas_->begin_witness_command(kind,dialog->value().id,edit);};
    dialog->witness_canceled=[this]{canvas_->end_witness_command();};
    if(extend==4)dialog->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(drawing::DrawingDimensionKind::Linear));
    else if(extend==2)dialog->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(drawing::DrawingDimensionKind::Chain));
    else if(extend){dialog->enable_chain_command();if(extend!=3)dialog->extend(extend<0);}
    connect(dialog,&QDialog::finished,this,[this,id,view=value.view_id]{
        canvas_->set_dimension_command(nullptr);view_dialog_=nullptr;
        if(!id.empty())if(const auto* sheet=active_sheet();sheet&&std::ranges::any_of(sheet->dimensions,[&](const auto& d){return d.id==id;}))canvas_->select_manual(view,id);
        if(properties_handler_)properties_handler_(nullptr);update_action_states();
    });
    if(properties_handler_)properties_handler_(dialog);dialog->show();update_action_states();
    set_status_message(tr("Kóta: vyberte vazby a umístění. OK potvrdí měřenou kótu."));
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
    source_variant_->setEnabled(!view_dialog_&&source_variant_->count()>1);
    const bool has_view = has_sheet && !sheet->views.empty();
    const bool selected_view = has_sheet &&
        document_.find_view(canvas_->selected_view_id()) != nullptr;
    save_action_->setEnabled(has_sheet);
    quick_pdf_action_->setEnabled(has_sheet);
    quick_dxf_action_->setEnabled(has_sheet);
    add_sheet_action_->setEnabled(!view_dialog_);
    remove_sheet_action_->setEnabled(!view_dialog_ && document_.sheets.size() > 1);
    edit_sheet_action_->setEnabled(has_sheet);
    edit_title_block_action_->setEnabled(
        has_sheet && (!sheet->title_block_fields.empty() || !sheet->title_block_texts.empty() || !sheet->title_block_symbols.empty()));
    insert_view_action_->setEnabled(has_sheet&&!selected_source_id().empty());
    insert_detail_action_->setEnabled(has_view);
    projected_view_action_->setEnabled(selected_view);
    edit_view_action_->setEnabled(selected_view);
    regenerate_view_action_->setEnabled(!view_dialog_&&std::ranges::any_of(document_.sheets,[](const auto& sheet){return !sheet.views.empty();}));
    delete_view_action_->setEnabled(selected_view);
    linear_dimension_action_->setEnabled(has_view);
    chain_dimension_action_->setEnabled(has_view);
    dimension_jog_action_->setEnabled(has_view);dimension_break_action_->setEnabled(has_view);
    dimension_align_action_->setEnabled(has_view);
    text_action_->setEnabled(has_sheet&&!view_dialog_);
    balloon_action_->setEnabled(has_view);
    linear_dimension_action_->setChecked(canvas_->dimension_mode());
    show_erase_action_->setEnabled(has_view);
    selection_action_->setEnabled(has_sheet);
    selection_action_->setChecked(!canvas_->dimension_mode());
}
std::string DrawingWindow::selected_source_id() const {
    const int index=sheets_?sheets_->currentIndex():-1;
    if(index>=0&&index<static_cast<int>(document_.sheets.size())) {
        const auto& selected=document_.sheets[index].selected_source_document_id;
        if(!selected.empty())return selected;
    }
    return document_.source_document_id;
}
void DrawingWindow::show_drawing_settings() {
    if(view_dialog_){view_dialog_->raise();return;}
    if(raise_open_properties(window()))return;
    auto* dialog=new DrawingSettingsDialog(window(),document_,path_,workspace_,[this](auto next) {
        document_=std::move(next);canvas_->select_view_for_test({});refresh();
    },properties_handler_);
    view_dialog_=dialog;if(properties_handler_)properties_handler_(dialog);
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if(view_dialog_==dialog){view_dialog_.clear();if(properties_handler_)properties_handler_(nullptr);}
        update_action_states();update_source_variant();
    });
    dialog->show();update_action_states();update_source_variant();
}
void DrawingWindow::update_source_variant() {
    QSignalBlocker blocker(source_variant_);source_variant_->clear();
    for(const auto& source:document_.data_sources()) {
        auto path=source.source_path;
        if(path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
        try {
            for(const auto& choice:family_source_choices(workspace_,source.document_id,path))
                source_variant_->addItem(resource_icon(QString::fromStdWString(path.extension().wstring()).compare(".asmz",Qt::CaseInsensitive)==0?"assembly":"part"),choice.name,QString::fromStdString(choice.id));
        } catch(const std::exception&) {
            source_variant_->addItem(QString::fromStdString(source.name.empty()?zima::document::path_to_utf8(path.filename()):source.name)+tr(" (nedostupný)"),QString::fromStdString(source.document_id));
        }
    }
    if(source_variant_->count()==0)source_variant_->addItem(tr("Bez zdroje"),QString{});
    auto selected=source_variant_->findData(QString::fromStdString(selected_source_id()));
    if(selected<0&&!selected_source_id().empty()){source_variant_->addItem(tr("Nedostupná varianta"),QString::fromStdString(selected_source_id()));selected=source_variant_->count()-1;}
    if(selected>=0)source_variant_->setCurrentIndex(selected);
    source_variant_->setEnabled(!view_dialog_&&source_variant_->count()>1);
}
void DrawingWindow::refresh(bool changed) {
    const int wanted = std::clamp(sheets_ ? sheets_->currentIndex() : 0, 0, static_cast<int>(document_.sheets.size() - 1));
    sheets_->blockSignals(true); while (sheets_->count()) sheets_->removeTab(0);
    for (const auto& sheet : document_.sheets) sheets_->addTab(QString::fromStdString(sheet.name));
    sheets_->setCurrentIndex(wanted); sheets_->blockSignals(false); canvas_->set_sheet(active_sheet());
    update_source_variant();
    if (const auto* sheet = active_sheet()) {
        const QSignalBlocker format_blocker(sheet_format_);
        const QSignalBlocker projection_blocker(projection_method_);
        const QSignalBlocker numerator_blocker(scale_numerator_);
        const QSignalBlocker denominator_blocker(scale_denominator_);
        sheet_format_->setCurrentIndex(static_cast<int>(sheet->format));
        projection_method_->setCurrentIndex(
            sheet->projection_method == zima::drawing::ProjectionMethod::FirstAngle ? 0 : 1);
        scale_numerator_->setValue(sheet->default_scale>=1?sheet->default_scale:1);
        scale_denominator_->setValue(sheet->default_scale>=1?1:1.0/sheet->default_scale);
    }
    const auto view_count = active_sheet() ? active_sheet()->views.size() : 0;
    set_status_message(view_count == 0
        ? tr("Nový výkres: použijte Vložit pohled a vyberte otevřený Part nebo sestavu.")
        : tr("Výkres: %1 listů, %2 pohledů")
            .arg(document_.sheets.size()).arg(view_count));
    update_action_states();
    sync_workspace_document(changed);
    refresh_title_block_context();
}

void DrawingWindow::refresh_title_block_context() {
    // The title block retains the source captured on insertion, independently
    // of subsequent chooser changes and view insertion order.
    const auto* sheet = active_sheet();
    if (!sheet) {canvas_->set_title_block_context(std::nullopt);return;}
    auto source_id=sheet->bom_source_document_id;
    auto source_path=document_.data_source_path(source_id);
    if(!source_path.empty() && source_path.is_relative() && !path_.empty())
        source_path=path_.parent_path()/source_path;
    if(source_id.empty() && workspace_)
        if(const auto open=workspace_->document_id_for_path(source_path))source_id=*open;
    zima::drawing::TitleBlockContext context;
    try {context=build_title_block_context_for_source(source_id,source_path,workspace_);}
    catch(const std::exception& error) {
        set_status_message(tr("Zdroj razítka není dostupný: %1").arg(QObject::tr(error.what())));
    }
    context.sheet_index=sheets_->currentIndex();context.sheet_count=static_cast<int>(document_.sheets.size());
    canvas_->set_title_block_context(std::move(context));
}

void DrawingWindow::sync_workspace_document(bool changed) {
    if(changed)document_.synchronize_dimension_identifiers();
    if(changed && workspace_!=nullptr) for(auto& sheet:document_.sheets) for(auto& view:sheet.views)
        if(view.source_path.empty()) {
            if(const auto* part=workspace_->open_part(view.source_document_id)) view.source_path=part->path;
            else if(const auto* assembly=workspace_->open_assembly(view.source_document_id))
                view.source_path=assembly->path;
        }
    if(workspace_!=nullptr && !workspace_document_id_.empty())
        if(auto* state=workspace_->open_drawing(workspace_document_id_)) {
            if(changed)state->commit(document_);
            state->path=path_;
        }
    if (changed_handler_) changed_handler_();
    if (selection_handler_) selection_handler_(canvas_->tree_selection_ids());
}

}  // namespace zima::app
