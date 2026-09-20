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
    } else append(zima::workspace::read_family_assembly(live,path,root),path);
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
        std::function<void(ViewPropertiesDialog*,drawing::DrawingView)> edit_breaks)
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
        field(appearance,0,0,tr("Zobrazení"),display_);field(appearance,0,1,tr("Skryté hrany"),hidden_style_);field(appearance,0,2,tr("Tečné hrany"),tangent_style_);
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
        section_group->addWidget(section_label_,2,0);section_group->addWidget(components_,3,0,1,2);
        marker_table_=new QTableWidget(content);marker_table_->setObjectName("drawingSectionMarkers");marker_table_->setColumnCount(1);marker_table_->setHorizontalHeaderLabels({tr("Zobrazit trasy řezů")});marker_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);marker_table_->verticalHeader()->hide();section_group->addWidget(marker_table_,4,0,1,2);
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
        preview_values();
    }
    void set_breaks(std::vector<drawing::ViewBreak> breaks) {value_.breaks=std::move(breaks);preview_values();}
    void resume_preview() {preview_values();}
    void set_error(const QString& error) { error_->setText(error);error_->setVisible(!error.isEmpty()); }
    zima::drawing::DrawingView values() const {
        auto result = value_;
        result.name = name_->text().trimmed().toStdString();
        result.show_caption = caption_->isChecked();
        result.dimension_guide_count=guide_count_->value();result.show_dimension_guides=guides_->isChecked();result.dimension_guide_offset=guide_offset_->value();result.dimension_guide_spacing=guide_spacing_->value();
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
            zima::workspace::set_drawing_section_components(result,section,components_->values(),true);
        }
        return result;
    }
private:
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
    std::function<void(std::optional<zima::drawing::DrawingView>)> preview_;
    void preview_values() {
        const int i=source_->currentIndex();
        if(i>=0&&!sources_[i].evaluated){preview_({});set_error(tr("Varianta se vypočítá po potvrzení OK."));return;}
        preview_(values());
    }
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
        const auto& fields=edit.fields;const auto& context=edit.context;
        auto* content=new QWidget(this); auto* form=new QFormLayout(content);
        for(const auto& field:fields) {
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
        } catch(const std::exception& error) {error_->setText(QObject::tr(error.what()));return false;}
    }
};

using zima::workspace::projection_placement;

std::pair<std::string, zima::kernel::ViewerMesh> load_drawing_source(
    const std::filesystem::path& path, zima::workspace::Workspace* workspace = nullptr,
    const std::string& expected_document_id = {}) {
    try{return zima::workspace::read_drawing_source(workspace,path,expected_document_id);}
    catch(const std::exception& e){throw std::runtime_error(QObject::tr(e.what()).toStdString());}
}


}  // namespace

class DrawingCanvas final : public QWidget, public SheetRenderer {
    std::function<void(const std::string&)> choose_view_;
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
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){const auto da=QLineF(point,a.point).length(),db=QLineF(point,b.point).length();if((da<=8)!=(db<=8))return da<=8;return da<=8&&db<=8&&da<db;});
        if(model_pick_){std::set<std::pair<std::string,std::string>> entities;std::erase_if(candidates,[&](const auto& c){return !entities.emplace(c.key.view,c.key.id).second;});}
        bool same=candidates.size()==offered_annotations_.size();for(std::size_t i=0;same&&i<candidates.size();++i)same=candidates[i].key==offered_annotations_[i].key;
        if(!same)offered_annotation_index_=0;offered_annotations_=std::move(candidates);
        hovered_annotation_=offered_annotations_.empty()?std::optional<AnnotationKey>{}:offered_annotations_[offered_annotation_index_].key;
    }
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
        setFont(drawing_font);
        auto canvas_palette = palette();
        canvas_palette.setColor(QPalette::Window, QColor("#000000"));
        setPalette(canvas_palette);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void set_sheet(zima::drawing::DrawingSheet* sheet) {
        clear_annotation_snap();sheet_ = sheet;set_render_sheet(sheet);shaded_cache_.clear();annotation_handles_.clear();offered_annotations_.clear();selected_annotation_.reset();hovered_annotation_.reset();dragged_section_end_.reset();
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
    void set_staged_model_previews(const std::vector<drawing::DrawingView>& views) {
        staged_model_previews_.clear();
        for(const auto& view:views)staged_model_previews_[view.id]=view;
        update();
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
    void set_text_properties(std::function<void(const std::string&)> callback) {text_properties_=std::move(callback);}
    void set_text_editor(SketchTextPropertiesDialog* dialog) {
        text_editor_=dialog;
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
        selected_field_.clear();hovered_field_.clear();if(changed_)changed_();update();
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
            return "drawing-dimension:"+selected_annotation_->id;
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Model)
            return selected_annotation_->view+":"+selected_annotation_->id;
        return selected_;
    }
    void select_manual(const std::string& view,const std::string& id) {
        selected_.clear();selected_dimension_id_=id;
        selected_annotation_=AnnotationKey{AnnotationKind::Dimension,view,id,0};
        if(selection_changed_)selection_changed_();update();
    }
    void select_model(const std::string& view,const std::string& key) {
        selected_=view;selected_dimension_id_.clear();
        selected_annotation_=AnnotationKey{AnnotationKind::Model,view,key,0};
        if(selection_changed_)selection_changed_();update();
    }
    void set_dimension_properties_callback(std::function<void(const std::string&,const std::string&)> callback){dimension_properties_=std::move(callback);}

    void set_manual_properties_callback(std::function<void(const std::string&,int)> callback){manual_properties_=std::move(callback);}
    void set_dimension_command(DrawingDimensionDialog* dialog){
        dimension_command_=dialog;dimension_mode_=dialog!=nullptr;measurement_offered_.clear();measurement_index_=0;
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
        if(dimension_command_&&dimension_command_->entering()&&sheet_)for(const auto& view:sheet_->views){
            if(!dimension_command_->value().view_id.empty()&&dimension_command_->value().view_id!=view.id)continue;
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
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return drawing::measurement_candidate_precedes(a.candidate,b.candidate);});
        bool same=candidates.size()==measurement_offered_.size();
        for(std::size_t i=0;same&&i<candidates.size();++i)same=candidates[i].view==measurement_offered_[i].view&&candidates[i].candidate.attachment==measurement_offered_[i].candidate.attachment;
        if(!same)measurement_index_=0;measurement_offered_=std::move(candidates);
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
        else if(auto* target=editable_dimension(value.id))*target=std::move(value);
        update();
    }
    void begin_manual_drag(const AnnotationHandle& candidate,QPointF pointer){
        if(const auto* d=visible_dimension(candidate.key.id)){
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
    void choose_view(std::function<void(const std::string&)> pick) { start_selection();choose_view_=std::move(pick);selected_.clear();if(selection_changed_)selection_changed_();update(); }
    void start_selection() { choose_view_={};if(balloon_command_)balloon_command_->reject();if(dimension_command_)dimension_command_->reject();dimension_mode_=false;update(); }
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
            update();event->accept();return;
        }
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
                connect(properties,&QAction::triggered,this,[this,key]{dimension_properties_(key.view,key.id);});menu->popup(event->globalPos());event->accept();return;
            }
        }
        if(selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension&&!offered_annotations_.empty()){
            auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);const auto id=selected_dimension_id_;
            menu->addAction(tr("Vlastnosti kóty…"),this,[this,id]{if(manual_properties_)manual_properties_(id,0);});
            const auto* dimension=visible_dimension(id);
            if(dimension&&(dimension->kind==drawing::DrawingDimensionKind::Linear||dimension->kind==drawing::DrawingDimensionKind::Chain)){
                if(dimension->kind==drawing::DrawingDimensionKind::Linear) {
                    auto* convert=menu->addAction(tr("Převést na řetězovou kótu…"),this,[this,id]{if(manual_properties_)manual_properties_(id,2);});
                    convert->setObjectName("convertDrawingChainAction");
                } else {
                    menu->addAction(tr("Přidat bod na začátek…"),this,[this,id]{if(manual_properties_)manual_properties_(id,-1);});
                    menu->addAction(tr("Přidat bod na konec…"),this,[this,id]{if(manual_properties_)manual_properties_(id,1);});
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
        if(event->button()==Qt::LeftButton&&!preview_&&!dimension_command_&&!balloon_command_&&!dimension_mode_&&selected_annotation_&&
            selected_annotation_->kind==AnnotationKind::Model) {
            for(const auto& handle:annotation_handles_)if(handle.key.kind==AnnotationKind::Model&&
                handle.key.view==selected_annotation_->view&&handle.key.id==selected_annotation_->id&&
                handle.key.end==0&&handle.text_hit.adjusted(-2,-2,2,2).contains(event->position())) {
                dragged_model_.reset();model_drag_original_.reset();model_moved_=false;
                if(dimension_value_)dimension_value_(handle.key.view,handle.key.id,handle.point);
                event->accept();return;
            }
        }
        if(balloon_command_){event->accept();return;}
        if(event->button()==Qt::LeftButton&&selected_annotation_&&selected_annotation_->kind==AnnotationKind::Balloon){balloon_drag_original_.reset();if(balloon_properties_)balloon_properties_(selected_annotation_->id);event->accept();return;}
        if(event->button()==Qt::LeftButton) {
            if(text_editor_){event->accept();return;}
            if(const auto field=field_at(event->position());editable_text(field)) {
                text_drag_original_.reset();selected_field_=field;
                if(text_properties_)text_properties_(field.substr(5));event->accept();return;
            }
        }
        if(event->button()==Qt::LeftButton&&selected_annotation_&&selected_annotation_->kind==AnnotationKind::Dimension&&!dimension_command_){
            dragged_dimension_id_.clear();if(manual_properties_)manual_properties_(selected_annotation_->id,0);event->accept();return;
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
        QPainter painter(this);painter.fillRect(rect(),QColor("#000000"));
        paint_sheet(painter,canvas_zoom(),canvas_origin(canvas_zoom()),false);
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
    const drawing::DrawingDimension* pending_dimension() const override {return dimension_command_?&dimension_command_->value():nullptr;}
    void paint_reference_overlay(QPainter& painter) override {
        if(dimension_command_||balloon_command_||balloon_drag_original_){
            const auto highlight=[&](const drawing::DrawingView& view,const kernel::EdgeReference& ref,QColor color){
                painter.save();painter.setPen(QPen(color,2));painter.setBrush(Qt::NoBrush);
                for(const auto& curve:drawing::measurement_reference_geometry(view,ref)){QPolygonF line;for(const auto& p:curve)line<<view_screen_point(view,p);painter.drawPolyline(line);}
                for(const auto& p:view.measurement_geometry->points)if(p.source==ref&&!drawing::break_hidden(view,{kernel::dimension_dot(p.position,view.camera.horizontal),kernel::dimension_dot(p.position,view.camera.vertical)}))painter.drawEllipse(view_screen_point(view,{kernel::dimension_dot(p.position,view.camera.horizontal),kernel::dimension_dot(p.position,view.camera.vertical)}),4,4);
                painter.restore();
            };
            if(!measurement_offered_.empty()){
                const auto& candidate=measurement_offered_[measurement_index_];
                for(const auto& view:sheet_->views)if(view.id==candidate.view){
                    highlight(view,candidate.candidate.attachment.reference,QColor("#FF9300"));highlight(view,candidate.candidate.attachment.other_reference,QColor("#FF9300"));
                    painter.save();painter.setPen(QPen(QColor("#FF9300"),2));painter.setBrush(Qt::NoBrush);painter.drawEllipse(view_screen_point(view,candidate.candidate.position),4,4);painter.restore();
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
                const auto p=screen(*snap_point_);painter.setBrush(QColor("#171A1D"));
                painter.drawPolygon(QPolygonF{p+QPointF(0,-6),p+QPointF(6,0),p+QPointF(0,6),p+QPointF(-6,0)});
            }
            painter.restore();
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
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
                if(!measurement_offered_.empty()){const auto candidate=measurement_offered_[measurement_index_];dimension_command_->accept_candidate(candidate.view,candidate.candidate);}
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
            if(const auto* text=editable_text(selected_field_)){text_drag_original_=*text;text_drag_start_=event->position();}
            if(selection_changed_)selection_changed_();update();event->accept();return;
        }
        if(!dimension_mode_){
            offer_annotations(event->position());
            if(!offered_annotations_.empty()){
                const auto candidate=offered_annotations_[offered_annotation_index_];selected_annotation_=candidate.key;selected_field_.clear();selected_dimension_id_.clear();drag_view_id_.clear();dragged_label_.reset();dragged_dimension_id_.clear();
                selected_=(candidate.key.kind==AnnotationKind::Dimension||candidate.key.kind==AnnotationKind::Balloon)?std::string{}:candidate.key.view;
                if(candidate.key.kind==AnnotationKind::Dimension)selected_dimension_id_=candidate.key.id;
                if(QLineF(candidate.point,event->position()).length()<=8){
                    if(candidate.key.kind==AnnotationKind::Caption||candidate.key.kind==AnnotationKind::SectionLabel){
                        dragged_label_=std::pair{candidate.key.view,candidate.key.kind==AnnotationKind::SectionLabel};label_drag_start_=event->position();label_moved_=false;
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view){const auto origin=raw_view_origin(view);label_position_start_={(candidate.point.x()-origin.x())/canvas_zoom(),(origin.y()-candidate.point.y())/canvas_zoom()};}
                    }else if(candidate.key.kind==AnnotationKind::Balloon){
                        begin_balloon_drag(candidate,event->position());
                    }else if(candidate.key.kind==AnnotationKind::Dimension){
                        begin_manual_drag(candidate,event->position());
                    }else if(candidate.key.kind==AnnotationKind::Model){
                        for(const auto& view:sheet_->views)if(view.id==candidate.key.view)for(const auto& item:view.model_annotations)if(model_annotation_key(item.source)==candidate.key.id&&item.kind==drawing::ModelAnnotationKind::Dimension){model_drag_original_=item;dragged_model_=candidate;model_drag_start_=event->position();model_moved_=false;model_drag_layout_initial_=item.view_layout.value_or(item.model_layout);model_drag_shown_=item.model_dimension?std::optional(kernel::layout_dimension(*item.model_dimension,item.model_envelope,model_drag_layout_initial_)):std::nullopt;}
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
                            const auto placed=kernel::layout_dimension(*item.model_dimension,item.model_envelope,*item.view_layout);
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
            for(auto& view:sheet_->views)if(view.id==dragged_section_end_->key.view){const auto& end=*dragged_section_end_;workspace::set_drawing_section_end(view,end.key.id,end.key.end,end.offset+(delta.x()*end.direction.x-delta.y()*end.direction.y)/zoom,end.minimum);section_end_moved_=true;}update();return;
        }
        if(dragged_label_&&(event->buttons()&Qt::LeftButton)){
            const auto delta=event->position()-label_drag_start_;
            if(!label_moved_&&delta.manhattanLength()<QApplication::startDragDistance())return;
            for(auto& view:sheet_->views)if(view.id==dragged_label_->first){
                workspace::set_drawing_label_position(view,dragged_label_->second?workspace::DrawingLabel::Section:workspace::DrawingLabel::Caption,
                    drawing::Point2{label_position_start_.x+delta.x()/zoom,label_position_start_.y-delta.y()/zoom});label_moved_=true;
            }update();return;
        }
        if(!dragged_dimension_id_.empty()&&dimension_drag_initial_&&(event->buttons()&Qt::LeftButton)){
            auto value=*dimension_drag_initial_;
            const auto view=std::ranges::find(sheet_->views,value.view_id,&drawing::DrawingView::id);
            if(view==sheet_->views.end())return;
            const auto target=snap_measurement_point(*view,dimension_grip_start_+event->position()-dimension_drag_start_);
            const auto start=measurement_point(*view,dimension_grip_start_);drawing::Point2 delta{target.x()-start.x,target.y()-start.y};
            const auto evaluation=drawing::evaluate_drawing_dimension(*view,value);const auto segment=dimension_drag_handle_/3;
            if(dimension_drag_handle_%3==0&&segment<int(evaluation.presentations.size())){
                const auto label=evaluation.presentations[segment].label_position.value();
                const auto actual=measurement_point(*view,dimension_grip_start_);
                delta.x+=actual.x-label.x;delta.y+=actual.y-label.y;
            }
            drawing::drag_drawing_dimension(*view,value,segment,dimension_drag_handle_%3,delta);
            store_presentation(std::move(value));event->accept();return;
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
        if(event->button()==Qt::LeftButton)clear_annotation_snap();
        if(balloon_release(event))return;
        if(text_drag_original_&&event->button()==Qt::LeftButton) {
            const auto* text=editable_text("text:"+text_drag_original_->id);
            const bool moved=text&&(text->presentation.position.x!=text_drag_original_->presentation.position.x||text->presentation.position.y!=text_drag_original_->presentation.position.y);
            text_drag_original_.reset();if(moved&&changed_)changed_();event->accept();return;
        }
        if(!dragged_dimension_id_.empty()&&event->button()==Qt::RightButton){event->accept();return;}
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
        if(event->key()==Qt::Key_Escape)clear_annotation_snap();
        if(dimension_command_&&(event->key()==Qt::Key_C||event->key()==Qt::Key_T||event->key()==Qt::Key_I)){
            dimension_command_->set_mode(int(event->key()==Qt::Key_C?drawing::DimensionAttachmentKind::Center:event->key()==Qt::Key_T?drawing::DimensionAttachmentKind::Tangent:drawing::DimensionAttachmentKind::Intersection));event->accept();return;
        }
        if(event->key()==Qt::Key_Escape) {
            if(balloon_escape()){event->accept();return;}
            if(text_editor_){text_editor_->reject();event->accept();return;}
            if(text_drag_original_){if(auto* text=editable_text("text:"+text_drag_original_->id))*text=*text_drag_original_;text_drag_original_.reset();update();event->accept();return;}
            selected_field_.clear();hovered_field_.clear();
            if(dragged_model_&&model_drag_original_&&sheet_){for(auto& view:sheet_->views)if(view.id==dragged_model_->key.view)for(auto& item:view.model_annotations)if(model_annotation_key(item.source)==dragged_model_->key.id)item=*model_drag_original_;dragged_model_.reset();model_drag_original_.reset();model_moved_=false;update();event->accept();return;}
            if (choose_view_) {choose_view_={};update();event->accept();return;}
            if (placed_) { cancel_placement(); event->accept(); return; }
            if(!dragged_dimension_id_.empty()&&dimension_drag_original_){store_presentation(*dimension_drag_original_);dragged_dimension_id_.clear();event->accept();return;}
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
    struct OfferedMeasurement {std::string view;drawing::MeasurementCandidate candidate;};
    QPointer<DrawingDimensionDialog> dimension_command_;
    std::vector<OfferedMeasurement> measurement_offered_;std::size_t measurement_index_{};
    QPointF measurement_pointer_;
    std::function<void(const std::string&,int)> manual_properties_;
    std::optional<drawing::DrawingDimension> dimension_drag_initial_,dimension_drag_original_;
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
    show_erase_action_=drawing->addAction(tr("Zobrazit / skrýt kóty…"),this,[this]{show_erase();});
    show_erase_action_->setObjectName("drawingShowEraseAction");
    show_erase_action_->setIcon(resource_icon("show-erase"));
    regenerate_view_action_ = drawing->addAction(tr("Regenerovat"), this,
        [this] { regenerate_selected_view(); });
    regenerate_view_action_->setObjectName("regenerateDrawingViewAction");
    delete_view_action_ = drawing->addAction(tr("Odstranit pohled"), this,
        [this] { delete_selected_view(); });
    delete_view_action_->setObjectName("deleteDrawingViewAction");
    linear_dimension_action_ = drawing->addAction(tr("Kóta"), this,
        [this] { start_linear_dimension(); });
    linear_dimension_action_->setObjectName("drawingDimensionAction");
    linear_dimension_action_->setIcon(resource_icon("sketch-dimensions"));
    linear_dimension_action_->setCheckable(true);
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
    drawing_toolbar_->addAction(show_erase_action_);
    drawing_toolbar_->addAction(linear_dimension_action_);
    drawing_toolbar_->addAction(text_action_);
    drawing_toolbar_->addAction(balloon_action_);
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
        if (selection_handler_) selection_handler_(canvas_->tree_selection_id());
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
    connect(sheets_, &QTabBar::currentChanged, this, [this] { refresh(false); });
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
    refresh(false);
}
void DrawingWindow::edit_workspace_document(const std::string& document_id) {
    if(workspace_==nullptr) return;
    auto* state=workspace_->open_drawing(document_id); if(state==nullptr) return;
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
std::optional<QPointF> DrawingWindow::view_label_center_for_test(const std::string& id,bool section)const{return canvas_->label_center(id,section);}
std::optional<QPointF> DrawingWindow::annotation_handle_for_test(const std::string& id,int end,bool dimension)const{return canvas_->annotation_point(id,end,dimension);}
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
void DrawingWindow::save_pdf() {
    auto suggested=path_.empty()?std::filesystem::path(document_.name+".pdf"):path_;
    suggested.replace_extension(".pdf");
    const auto path=save_file(this,tr("Uložit jako PDF"),QString::fromStdString(suggested.string()),tr("PDF (*.pdf)"),"pdf");
    if(path.isEmpty())return;
    try {export_pdf(path.toStdString());set_status_message(tr("PDF uloženo. Tiskněte ve skutečné velikosti (100 %)."));}
    catch(const std::exception& error){set_status_message(tr(error.what()));}
}
QRectF DrawingWindow::sheet_rectangle_for_test()const{return canvas_->sheet_rectangle();}
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
    auto* sheet=active_sheet();if(!sheet||(sheet->title_block_fields.empty()&&sheet->title_block_texts.empty()))return;
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
        if(properties_handler_)properties_handler_(dialog);
        connect(dialog,&QObject::destroyed,this,[this]{if(properties_handler_)properties_handler_(nullptr);});
        dialog->show();
        if(auto* editor=dialog->findChild<QLineEdit*>(QString::fromStdString("titleBlockField:"+focus))){editor->setFocus();editor->selectAll();}
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
        auto source=load_drawing_source(source_path,workspace_,source_id);
        if(document_.document_id!=drawing_id)return;
        sheet=document_.find_sheet(sheet_id);if(!sheet)return;
        auto view=zima::drawing::DrawingDocument::create_view(source.first,source_path,source.second,
            zima::drawing::ViewOrientation::Isometric);
        view.name=workspace::next_drawing_view_name(document_,tr("Pohled").toStdString());
        view.scale=sheet->default_scale; view.use_sheet_scale=true;
        canvas_->begin_placement(std::move(view), [this](auto placed) {
            show_view_properties(std::move(placed),true);
        }, [this] { start_selection(); });
        set_status_message(tr("Vložit pohled: klikněte na místo na listu. Esc zruší vložení."));
    } catch (const std::exception& error) {
        start_selection();set_status_message(QString::fromUtf8(error.what()));
    }
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

void DrawingWindow::show_view_properties(zima::drawing::DrawingView view, bool creating) {
    if (view_dialog_) { view_dialog_->raise(); return; }
    if (raise_open_properties(window())) { canvas_->set_preview({}); return; }
    auto* sheet=active_sheet(); if (!sheet) return;
    const auto sheet_id=sheet->id;
    const auto drawing_id=document_.document_id;
    std::vector<DrawingSourceChoice> sources;
    const auto add_family=[&](const std::string& id,auto path) {
        if(!path.empty()&&path.is_relative()&&!path_.empty())path=path_.parent_path()/path;
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
    auto cache=std::make_shared<zima::workspace::DrawingProjection>(workspace_,path_);
    const auto project=[cache](zima::drawing::DrawingView& value,bool pending_settings=false){
        cache->project(value,{.pending_hatch=pending_settings});
    };
    const auto error=[this](const QString& message) {
        if (auto* dialog=dynamic_cast<ViewPropertiesDialog*>(view_dialog_.data())) dialog->set_error(message);
    };
    auto* owner=qobject_cast<QMainWindow*>(window());
    auto* dialog=new ViewPropertiesDialog(owner ? owner : this, view, std::move(sources),sheet->default_scale,
        [this,error,sheet_id,drawing_id,creating](auto accepted) {
            if (document_.document_id!=drawing_id) return false;
            try {
                auto source_models=workspace_?*workspace_:zima::workspace::Workspace{};
                const auto publish_family=prepare_drawing_family_variant(workspace_,source_models,accepted,path_);
                zima::workspace::DrawingProjection projection(&source_models,path_);
                auto next_document=document_;
                zima::workspace::edit_drawing_view(next_document,sheet_id,accepted,creating,projection,true);
                const auto id=accepted.id;
                const auto* result=next_document.find_view(id);
                std::function<void()> commit_source=[]{};
                if(result->section_snapshot) {
                    const auto& source=projection.source(*result);
                    const auto original=std::ranges::find(source.sections,result->section_id,&zima::document::SectionDefinition::id);
                    commit_source=prepare_section_component_commit(workspace_,result->source_document_id,source.path,*result->section_snapshot,
                        original==source.sections.end()?nullptr:&*original);
                }
                commit_source();publish_family();document_=std::move(next_document);
                canvas_->set_preview({}); refresh(); canvas_->select_view_for_test(id);
                return true;
            } catch (const std::exception& exception) { error(tr(exception.what())); return false; }
        }, [this,project,error](auto pending) {
            if(!pending){canvas_->set_preview({});return;}
            try { project(*pending,true); canvas_->set_preview(std::move(pending)); error({}); }
            catch (const std::exception& exception) { canvas_->set_preview({});error(tr(exception.what())); }
        },[this](const auto& id,auto path){if(!path.empty()&&path.is_relative()&&!path_.empty())path=path_.parent_path()/path;return source_sections(workspace_,id,path);},
        [this](const auto& path){return family_source_choices(workspace_,{},path);},
        [this,project](ViewPropertiesDialog* properties,drawing::DrawingView pending){
            try{project(pending,true);}catch(const std::exception& e){properties->set_error(QString::fromUtf8(e.what()));return;}
            auto sheet=*active_sheet();auto owner=window();QPointer<ViewPropertiesDialog> guarded(properties);
            auto* editor=new DrawingBreakEditor(owner,std::move(sheet),pending,[guarded](auto breaks){if(guarded)guarded->set_breaks(std::move(breaks));});
            properties->hide();canvas_->set_preview({});canvas_->setEnabled(false);
            connect(editor,&QDialog::finished,this,[this,guarded,editor]{canvas_->setEnabled(true);if(guarded){guarded->show();guarded->raise();guarded->resume_preview();}editor->deleteLater();});editor->show();
        });
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
        view.name=workspace::next_drawing_view_name(document_,tr("Pohled").toStdString()); view.parent_view_id=parent_copy.id;
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
        [this,id](auto committed){
            auto* sheet=active_sheet();if(!sheet)throw std::runtime_error("List již neexistuje.");
            if(workspace::edit_drawing_dimension(document_,sheet->id,std::move(committed),id.empty()))
                sync_workspace_document();
        },owner?owner:this);
    view_dialog_=dialog;canvas_->set_dimension_command(dialog);
    if(extend==2)dialog->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(drawing::DrawingDimensionKind::Chain));
    else if(extend)dialog->extend(extend<0);
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
    add_sheet_action_->setEnabled(!view_dialog_);
    remove_sheet_action_->setEnabled(!view_dialog_ && document_.sheets.size() > 1);
    edit_sheet_action_->setEnabled(has_sheet);
    edit_title_block_action_->setEnabled(
        has_sheet && (!sheet->title_block_fields.empty() || !sheet->title_block_texts.empty()));
    insert_view_action_->setEnabled(has_sheet&&!selected_source_id().empty());
    projected_view_action_->setEnabled(selected_view);
    edit_view_action_->setEnabled(selected_view);
    regenerate_view_action_->setEnabled(!view_dialog_&&std::ranges::any_of(document_.sheets,[](const auto& sheet){return !sheet.views.empty();}));
    delete_view_action_->setEnabled(selected_view);
    linear_dimension_action_->setEnabled(has_view);
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
        set_status_message(tr("Zdroj razítka není dostupný: %1").arg(QString::fromUtf8(error.what())));
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
    if (selection_handler_) selection_handler_(canvas_->tree_selection_id());
}

}  // namespace zima::app
