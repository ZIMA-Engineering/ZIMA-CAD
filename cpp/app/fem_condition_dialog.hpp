#pragma once
#include "placement_reference_dialog.hpp"
#include <zima/document/fem_condition.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <zima/sketcher/sketch.hpp>
#include "sketch_button_style.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <algorithm>
#include <charconv>

namespace zima::app {
inline QString fem_condition_name(document::FemConditionKind kind) {
    return kind==document::FemConditionKind::Force?QObject::tr("Force"):
        kind==document::FemConditionKind::Pressure?QObject::tr("Pressure"):QObject::tr("Constraint");
}
inline const char* fem_condition_icon(document::FemConditionKind kind) {
    return kind==document::FemConditionKind::Force?"fem-force":kind==document::FemConditionKind::Pressure?"fem-pressure":"fem-constraint";
}

class FemConditionDialog final : public ui::PropertiesSubWindow, public PlacementReferenceDialog {
public:
    std::function<void()> changed;
    std::function<QString(const document::FemRegionReference&)> reference_label;
    std::function<void(std::size_t)> request_geometry;
    std::function<void()> stop_geometry;
    std::function<void(sketcher::Sketch)> edit_sketch;
    FemConditionDialog(document::FemCondition value,std::function<void(document::FemCondition)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("FEM condition properties"),parent),initial_(std::move(value)),commit_(std::move(commit)) {
        setObjectName("femConditionDialog");setAttribute(Qt::WA_DeleteOnClose);setProperty("expandBottomTable",true);
        auto* form=new QFormLayout;
        form_=form;
        name_=new QLineEdit(QString::fromStdString(initial_.name),this);name_->setObjectName("femConditionName");
        kind_=new QComboBox(this);kind_->setObjectName("femConditionKind");
        for(auto k:{document::FemConditionKind::Constraint,document::FemConditionKind::Force,document::FemConditionKind::Pressure})
            kind_->addItem(fem_condition_name(k),static_cast<int>(k));
        kind_->setCurrentIndex(kind_->findData(static_cast<int>(initial_.kind)));
        form->addRow(tr("Condition type"),kind_);form->addRow(tr("Název"),name_);content_layout()->addLayout(form);
        placement_=new ui::ContainerPlacementSection(this,content_layout(),true,true);
        placement_->reference_table()->setObjectName("femPlacementReferences");
        placement_->orientation_table()->setObjectName("femOrientationReferences");
        placement_->initialize_numeric_values(initial_.placement);
        placement_->initialize_from_references(initial_.placement.references,[](const auto& key){return QString::fromStdString(key);});
        placement_->install_dof_label(content_layout());
        placement_->set_changed_callback([this]{notify();});
        auto* parameters=new QFormLayout;
        parameters_=parameters;
        region_=new QComboBox(this);region_->setObjectName("femConditionRegion");
        region_->addItem(tr("Bod"),int(document::FemRegionKind::Point));
        region_->addItem(tr("Křivka"),int(document::FemRegionKind::Curve));
        region_->addItem(tr("Plocha"),int(document::FemRegionKind::Surface));
        region_->setCurrentIndex(region_->findData(int(initial_.region)));
        parameters->addRow(tr("Region of action"),region_);
        project_=new QCheckBox(tr("Project a Sketch"),this);
        project_->setObjectName("femProjectSketch");
        project_->setChecked(initial_.projected_sketch.has_value());
        parameters->addRow(project_);
        sketch_offset_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
        sketch_offset_->setObjectName("femSketchOffset");
        sketch_offset_->setRange(-1e9,1e9);
        if(initial_.projected_sketch)sketch_offset_->setValue(
            sketcher::Sketch::from_serialized(initial_.projected_sketch->serialized).plane_offset);
        parameters->addRow(tr("Odsazení roviny"),sketch_offset_);
        sketch_button_=new QPushButton(tr("Skica"),this);
        sketch_button_->setObjectName("femEditSketch");
        style_sketch_button(sketch_button_);
        parameters->addRow(sketch_button_);
        force_=new QDoubleSpinBox(this);force_->setObjectName("femForceValue");force_->setDecimals(6);force_->setRange(-1e12,1e12);force_->setSuffix(" N");force_->setValue(initial_.force_N);
        pressure_=new QDoubleSpinBox(this);pressure_->setObjectName("femPressureValue");pressure_->setDecimals(6);pressure_->setRange(0,1e12);pressure_->setSuffix(" MPa");pressure_->setValue(initial_.pressure_MPa);
        initial_force_display_=force_->value();initial_pressure_display_=pressure_->value();
        force_label_=new QLabel(tr("Force"),this);pressure_label_=new QLabel(tr("Pressure"),this);
        parameters->addRow(force_label_,force_);parameters->addRow(pressure_label_,pressure_);
        restraints_=new QWidget(this);auto* row=new QHBoxLayout(restraints_);row->setContentsMargins(0,0,0,0);
        for(int i=0;i<3;++i){fixed_[i]=new QCheckBox(QString("U%1").arg(QChar('X'+i)),restraints_);fixed_[i]->setObjectName(QString("femRestraint%1").arg(i));fixed_[i]->setChecked(initial_.restrained[i]);row->addWidget(fixed_[i]);}
        restraint_label_=new QLabel(tr("Restrained translations"),this);parameters->addRow(restraint_label_,restraints_);
        reverse_=new QCheckBox(tr("Reverse pressure normal"),this);reverse_->setObjectName("femPressureReverse");reverse_->setChecked(initial_.pressure_reversed);parameters->addRow(reverse_);
        content_layout()->addLayout(parameters);
        table_=new ReferenceTable(this);table_->setObjectName("femConditionGeometry");table_->setColumnCount(4);
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Geometry"),{}});table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(32);table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        for(int col:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);table_->setColumnWidth(col,32);}
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        content_layout()->addWidget(table_,1);
        hint_=new QLabel(tr("Use a point, curve or bounded surface from Projection. An empty point region uses the container origin."),this);hint_->setWordWrap(true);content_layout()->addWidget(hint_);
        status_=new QLabel(this);status_->setObjectName("femConditionStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2)arm(row);});
        connect(kind_,&QComboBox::currentIndexChanged,this,[this]{
            if(current_kind()==document::FemConditionKind::Pressure&&current_region()!=document::FemRegionKind::Surface)
                region_->setCurrentIndex(region_->findData(int(document::FemRegionKind::Surface)));
            refresh_kind();notify();
        });
        connect(region_,&QComboBox::currentIndexChanged,this,[this]{
            reset_targets();
            if(current_region()==document::FemRegionKind::Point)project_->setChecked(false);
            refresh_kind();notify();
        });
        connect(project_,&QCheckBox::toggled,this,[this](bool enabled){
            reset_targets();
            if(enabled) {
                auto sketch=sketcher::Sketch::create_default();
                sketch.owner_container_id=initial_.id;
                sketch.plane_auto=false;
                set_owned_sketch(std::move(sketch));
            }else initial_.projected_sketch.reset();
            refresh_kind();notify();
        });
        connect(sketch_offset_,&QDoubleSpinBox::valueChanged,this,[this]{
            if(initial_.projected_sketch)
                set_owned_sketch(sketcher::Sketch::from_serialized(initial_.projected_sketch->serialized));
        });
        connect(sketch_button_,&QPushButton::clicked,this,[this]{
            if(edit_sketch&&initial_.projected_sketch)
                edit_sketch(sketcher::Sketch::from_serialized(initial_.projected_sketch->serialized));
        });
        for(auto* field:{force_,pressure_})connect(field,&QDoubleSpinBox::valueChanged,this,[this]{notify();});
        for(auto* field:fixed_)connect(field,&QCheckBox::toggled,this,[this]{notify();});
        connect(reverse_,&QCheckBox::toggled,this,[this]{notify();});
        connect(name_,&QLineEdit::textChanged,this,[this]{notify();});
        refresh_kind();refresh_references();set_initial_size({610,780});
    }
    document::FemCondition pending_value()const {
        auto v=initial_;v.name=name_->text().trimmed().toStdString();v.kind=current_kind();v.region=current_region();
        v.placement=placement_->numeric_placement();v.placement.references=placement_->combined_references(3);
        // Preserve exact loaded values until the field was actually changed.
        if(force_->value()!=initial_force_display_)v.force_N=force_->value();
        if(pressure_->value()!=initial_pressure_display_)v.pressure_MPa=pressure_->value();
        for(int i=0;i<3;++i)v.restrained[i]=fixed_[i]->isChecked();v.pressure_reversed=reverse_->isChecked();return v;
    }
    void set_status(const QString& text){status_->setText(text);}
    void arm(std::size_t row){active_=row;refresh_references();if(request_geometry)request_geometry(row);}
    void end_input(){active_.reset();refresh_references();}
    bool active_input()const{return active_.has_value();}
    void set_owned_sketch(sketcher::Sketch sketch) {
        if(!initial_.projected_sketch)initial_.projected_sketch.emplace();
        sketch.plane_offset=sketch_offset_->value();
        sketch.refresh_default_frame();
        initial_.projected_sketch->serialized=sketch.serialized();
        notify();
    }
    void set_geometry(std::size_t row,document::FemRegionReference value,std::optional<kernel::Vec3> seed={}) {
        if(row>initial_.references.size())return;
        const auto existing=std::ranges::find(initial_.references,value);
        if(existing!=initial_.references.end()&&std::size_t(existing-initial_.references.begin())!=row)return;
        if(initial_.projected_sketch&&current_region()==document::FemRegionKind::Surface) {
            if(!seed)return;
            auto& seeds=initial_.projected_sketch->surface_seeds;
            if(row==seeds.size())seeds.push_back(*seed);else seeds[row]=*seed;
            auto& keys=initial_.projected_sketch->surface_region_keys;
            if(!keys.empty()) {
                keys.resize(seeds.size());
                keys[row].clear();
            }
        }
        if(row==initial_.references.size())initial_.references.push_back(std::move(value));else initial_.references[row]=std::move(value);
        active_.reset();inspected_.clear();refresh_references();notify();
    }
    const auto& inspected_geometry()const{return inspected_;}
    void refresh_references(){
        table_->setRowCount(int(initial_.references.size()+1));
        for(int row=0;row<table_->rowCount();++row) {
            const bool filled=row<int(initial_.references.size());table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row]{
                if(row<int(initial_.references.size())){
                    initial_.references.erase(initial_.references.begin()+row);
                    if(initial_.projected_sketch&&row<int(initial_.projected_sketch->surface_seeds.size()))
                        initial_.projected_sketch->surface_seeds.erase(initial_.projected_sketch->surface_seeds.begin()+row);
                    if(initial_.projected_sketch&&row<int(initial_.projected_sketch->surface_region_keys.size()))
                        initial_.projected_sketch->surface_region_keys.erase(initial_.projected_sketch->surface_region_keys.begin()+row);
                    inspected_.clear();active_.reset();refresh_references();notify();arm(std::min<std::size_t>(row,initial_.references.size()));
                }
                else arm(row);
            });indicator->findChild<QPushButton*>()->setToolTip(tr("Odstranit cílovou referenci a její řádek"));
            ui::set_reference_row_populated(indicator,filled);table_->setCellWidget(row,1,indicator);
            auto* field=new ui::ReferenceCellItem;
            if(filled){const auto& ref=initial_.references[row];field->set_reference(QString::fromStdString(ref.owner_id+":"+ref.semantic_key));field->setText(reference_label?reference_label(ref):QString::fromStdString(ref.semantic_key));}
            else field->setText(tr("Vyberte…"));
            field->set_active_input(active_&&*active_==std::size_t(row));field->set_inspected(inspected_.contains(row));table_->setItem(row,2,field);
            auto* eye=ui::build_reference_inspection_button(filled,inspected_.contains(row),[this,row](bool on){if(on)inspected_.insert(row);else inspected_.erase(row);refresh_references();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eye));
        }
        table_->setMinimumHeight(table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth()+32*std::min(5,table_->rowCount()));
    }
    // The same placement section and View picker used by ordinary containers.
    document::Placement placement_seed()const override{return placement_->numeric_placement();}
    bool owns_reference_owner(const std::string& id)const override{return id==initial_.id||id==initial_.id+":origin";}
    auto references_without(std::size_t i)const->std::vector<document::ConstructionReference> override{return placement_->references_without(i);}
    document::ConstructionReference prepare_reference_candidate(std::size_t i,document::ConstructionReference r)const override{return placement_->prepare_reference(i,std::move(r));}
    bool set_reference(std::size_t i,document::ConstructionReference r,const QString& label)override{
        if(owns_reference_owner(r.owner_id))return false;QString error;bool ok=placement_->set_reference(i,std::move(r),label,&error);set_status(error);return ok;
    }
    void set_reference_request_callback(std::function<void(std::size_t)> f){placement_->set_reference_request_callback(std::move(f));}
    void set_reference_highlights_changed_callback(std::function<void()> f){placement_->set_highlights_changed_callback(std::move(f));}
    auto highlighted_reference_entries()const{return placement_->highlighted_reference_entries();}
    std::size_t first_empty_position_index()const override{return placement_->first_empty_position_index();}
    void set_active_reference_index(std::optional<std::size_t> i)override{placement_->set_active_reference_index(i);}
    void set_reference_inspected(std::size_t i,bool v)override{placement_->set_reference_inspected(i,v);}
    void clear_reference_highlights()override{placement_->clear_reference_highlights();}
    void set_origin_selection_mode_callback(std::function<void(bool)> f)override{placement_->set_origin_selection_mode_callback(std::move(f));}
    void set_origin_selection_mode_active(bool v)override{placement_->set_origin_selection_mode_active(v);}
    void set_translation_constraint_state(const document::PointConstraintState& s,const kernel::Vec3& p)override{placement_->set_translation_constraint_state(s,p);}
    void set_remaining_rotation_dof(int n)override{placement_->set_remaining_rotation_dof(n);}
    void set_rotation_constraint_state(const document::OrientationConstraintState& s)override{placement_->set_rotation_constraint_state(s);}
    void set_orientation_base_rotation(const kernel::Vec3& v,bool c)override{placement_->set_orientation_base_rotation(v,c);}
    void set_resolved_rotation(const kernel::Vec3& v,bool valid=true)override{placement_->set_resolved_rotation(v,valid);}
    bool set_inline_parameter_value(std::string_view key,double value)override {
        const auto set=[value](QDoubleSpinBox* w){if(!w||!w->isEnabled()||w->isReadOnly()||!w->isVisible())return false;w->setValue(value);return true;};
        const std::array<std::string_view,3> xyz{"placement:x","placement:y","placement:z"},rot{"placement:rotation_x","placement:rotation_y","placement:rotation_z"};
        for(int i=0;i<3;++i){if(key==xyz[i])return set(placement_->translation_fields()[i]);if(key==rot[i]){if(placement_->rotation_fields()[i]->isEnabled()&&placement_->rotation_fields()[i]->isReadOnly())return false;return set(placement_->rotation_fields()[i])||set(placement_->rotation_offset_fields()[i]);}}
        constexpr std::string_view prefix="placement:reference_offset:";
        if(key.starts_with(prefix)){key.remove_prefix(prefix.size());std::size_t i{};const auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),i);return error==std::errc{}&&end==key.data()+key.size()&&placement_->set_reference_offset(i,value);}
        return false;
    }
protected:
    void changeEvent(QEvent* event)override {
        PropertiesSubWindow::changeEvent(event);
        if(event->type()!=QEvent::LanguageChange||!table_)return;
        set_internal_title(tr("FEM condition properties"));
        const auto label=[](QFormLayout* form,QWidget* field,const QString& text){if(auto* l=qobject_cast<QLabel*>(form->labelForField(field)))l->setText(text);};
        label(form_,kind_,tr("Condition type"));label(form_,name_,tr("Název"));label(parameters_,region_,tr("Region of action"));
        for(int i=0;i<kind_->count();++i)kind_->setItemText(i,fem_condition_name(static_cast<document::FemConditionKind>(kind_->itemData(i).toInt())));
        region_->setItemText(0,tr("Bod"));region_->setItemText(1,tr("Křivka"));region_->setItemText(2,tr("Plocha"));
        force_label_->setText(tr("Force"));pressure_label_->setText(tr("Pressure"));restraint_label_->setText(tr("Restrained translations"));reverse_->setText(tr("Reverse pressure normal"));
        project_->setText(tr("Project a Sketch"));sketch_button_->setText(tr("Skica"));label(parameters_,sketch_offset_,tr("Odsazení roviny"));
        refresh_kind();refresh_references();
    }
    bool submit()override{try{auto v=pending_value();document::validate_fem_condition(v);commit_(std::move(v));return true;}catch(const std::exception& e){set_status(tr(e.what()));return false;}}
private:
    class ReferenceTable final:public QTableWidget {
    public:
        explicit ReferenceTable(QWidget* parent):QTableWidget(parent){}
        QSize sizeHint()const override {auto size=QTableWidget::sizeHint();size.setHeight(minimumHeight());return size;}
    };
    document::FemCondition initial_;std::function<void(document::FemCondition)> commit_;
    QLineEdit* name_{};QComboBox *kind_{},*region_{};QDoubleSpinBox *force_{},*pressure_{};
    QCheckBox* project_{};ui::UnitDoubleSpinBox* sketch_offset_{};QPushButton* sketch_button_{};
    QCheckBox* fixed_[3]{};QCheckBox* reverse_{};QWidget* restraints_{};QLabel *force_label_{},*pressure_label_{},*restraint_label_{},*hint_{},*status_{};
    QTableWidget* table_{};ui::ContainerPlacementSection* placement_{};std::optional<std::size_t> active_;std::set<std::size_t> inspected_;
    QFormLayout *form_{},*parameters_{};
    double initial_force_display_{},initial_pressure_display_{};
    document::FemConditionKind current_kind()const{return static_cast<document::FemConditionKind>(kind_->currentData().toInt());}
    document::FemRegionKind current_region()const{return static_cast<document::FemRegionKind>(region_->currentData().toInt());}
    void notify(){if(changed)changed();}
    void reset_targets() {
        if(stop_geometry)stop_geometry();
        initial_.references.clear();
        if(initial_.projected_sketch) {
            initial_.projected_sketch->surface_seeds.clear();
            initial_.projected_sketch->surface_region_keys.clear();
        }
        active_.reset();inspected_.clear();refresh_references();
    }
    void refresh_kind(){
        const auto k=current_kind();
        force_->setVisible(k==document::FemConditionKind::Force);force_label_->setVisible(force_->isVisibleTo(this));
        pressure_->setVisible(k==document::FemConditionKind::Pressure);pressure_label_->setVisible(pressure_->isVisibleTo(this));
        restraints_->setVisible(k==document::FemConditionKind::Constraint);restraint_label_->setVisible(restraints_->isVisibleTo(this));
        reverse_->setVisible(k==document::FemConditionKind::Pressure);region_->setEnabled(k!=document::FemConditionKind::Pressure);
        const bool can_project=k!=document::FemConditionKind::Pressure&&current_region()!=document::FemRegionKind::Point;
        if(!can_project&&project_->isChecked())project_->setChecked(false);
        project_->setVisible(can_project);
        const bool projecting=can_project&&project_->isChecked();
        parameters_->setRowVisible(sketch_offset_,projecting);
        parameters_->setRowVisible(sketch_button_,projecting);
        table_->setHorizontalHeaderLabels({tr("Č."),{},projecting?tr("Cílové plochy"):tr("Geometry"),{}});
        hint_->setText(projecting?(current_region()==document::FemRegionKind::Surface?
            tr("Select each target face inside the region to retain."):
            tr("Select the target faces for the Sketch."))
            :tr("Use a point, curve or bounded surface from Projection. An empty point region uses the container origin."));
    }
};
} // namespace zima::app
