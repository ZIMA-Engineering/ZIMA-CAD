#pragma once
#include "placement_reference_dialog.hpp"
#include <zima/document/body_history.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace zima::app {
class CamBodyDialog final : public ui::PropertiesSubWindow, public PlacementReferenceDialog {
public:
    std::function<void()> changed, edit_machine, begin_source_input;
    CamBodyDialog(document::BodyHistory initial, std::function<void(document::BodyHistory)> commit, QWidget* parent)
        : PropertiesSubWindow(tr("CAM setup properties"),parent),pending_(std::move(initial)),commit_(std::move(commit)) {
        frame_valid_=pending_.cam->work_frame.reference_valid;
        setObjectName("camBodyDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending_.name),this);name->setObjectName("camBodyName");label(form,QT_TR_NOOP("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending_.name=text.trimmed().toStdString();});
        table_=new QTableWidget(0,5,this);table_->setObjectName("camBodySources");
        table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int column:{0,1,4}){table_->setColumnWidth(column,32);table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        ui::install_reference_cell_delegate(table_);content_layout()->addWidget(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==3)arm(row);});
        placement_=new ui::ContainerPlacementSection(this,content_layout(),true,true);
        placement_->reference_table()->setObjectName("camPlacementReferences");
        placement_->orientation_table()->setObjectName("camOrientationReferences");
        placement_->initialize_numeric_values(pending_.cam->work_frame);
        placement_->initialize_from_references(pending_.cam->work_frame.references,[](const auto& key){return QString::fromStdString(key);});
        placement_->install_dof_label(content_layout());placement_->set_changed_callback([this]{notify();});
        auto* machine_form=new QFormLayout;content_layout()->addLayout(machine_form);
        machine_=new QPushButton(this);machine_->setObjectName("camEditMachine");label(machine_form,QT_TR_NOOP("Machine"),machine_);
        connect(machine_,&QPushButton::clicked,this,[this]{if(edit_machine)edit_machine();});
        auto* origin=new QWidget(this);auto* xyz=new QHBoxLayout(origin);xyz->setContentsMargins(0,0,0,0);
        auto& point=pending_.cam->machine_work_origin;
        const std::array<double*,3> values{&point.x,&point.y,&point.z};
        for(int i=0;i<3;++i){auto* field=new QDoubleSpinBox(origin);field->setObjectName(QString("camMachineOrigin%1").arg(QChar('X'+i)));
            field->setRange(-1e9,1e9);field->setDecimals(6);field->setPrefix(QString(QChar('X'+i))+" ");field->setSuffix(" mm");field->setValue(*values[i]);xyz->addWidget(field);
            connect(field,&QDoubleSpinBox::valueChanged,this,[value=values[i]](double n){*value=n;});}
        label(machine_form,QT_TR_NOOP("Work origin in machine coordinates"),origin);
        auto* help=new QLabel(this);help->setWordWrap(true);content_layout()->addWidget(help);
        translate_.push_back([help]{help->setText(tr("Select stock, target and fixture Bodies. The placement defines the machining work frame. Calculation is a separate action."));});
        if(!pending_.cam->clamping_sketch_ids.empty()){
            auto* warning=new QLabel(this);warning->setWordWrap(true);content_layout()->addWidget(warning);
            translate_.push_back([warning]{warning->setText(tr("CAM clamping Sketch checks are not available yet. Use fixture Bodies for collision checking."));});
        }
        status_=new QLabel(this);status_->setWordWrap(true);status_->setObjectName("camBodyStatus");content_layout()->addWidget(status_);
        retranslate();set_initial_size({760,850});
    }
    document::BodyHistory pending_value()const {
        auto value=pending_;value.cam->work_frame=placement_->numeric_placement();
        value.cam->work_frame.references=placement_->combined_references(3);
        value.cam->work_frame.reference_valid=frame_valid_;
        return value;
    }
    void set_sources(std::map<std::string,QString> names,std::set<std::string> stocks,std::set<std::string> material){names_=std::move(names);stocks_=std::move(stocks);material_=std::move(material);refresh_sources();}
    bool accepts_source(const std::string& id)const{return active_input()&&(active_==0?stocks_:material_).contains(id);}
    bool set_source(const std::string& id){
        if(!accepts_source(id))return false;
        auto& d=*pending_.cam;
        if(active_==0)d.stock_id=id;
        else if(active_==1)d.target_id=id;
        else {const auto found=std::ranges::find(d.fixture_ids,id);
            if(found!=d.fixture_ids.end()&&std::distance(d.fixture_ids.begin(),found)!=active_-2){set_error("This fixture is already included.");return false;}
            if(active_-2==int(d.fixture_ids.size()))d.fixture_ids.push_back(id);else d.fixture_ids[active_-2]=id;}
        inspected_.erase(active_);active_=-1;set_status({});refresh_sources();notify();return true;
    }
    void arm(int row){if(row<0||row>int(pending_.cam->fixture_ids.size())+2)return;
        if(begin_source_input)begin_source_input();active_=row;placement_->set_active_reference_index(std::nullopt);refresh_sources();notify();}
    void end_input(bool clear_inspection=false){active_=-1;if(clear_inspection){inspected_.clear();placement_->clear_reference_highlights();}refresh_sources();notify();}
    bool active_input()const{return active_>=0;}
    std::set<std::string> inspected_sources()const{std::set<std::string> ids;for(const auto row:inspected_)if(const auto id=source_id(row);!id.empty())ids.insert(id);return ids;}
    void set_machine(cam::Machine value){pending_.cam->machine=std::move(value);refresh_machine();}
    void set_status(const QString& text){error_key_.clear();status_->setText(text);}
    void set_error(const char* key){error_key_=key;status_->setText(tr(key));}
    void set_forbidden_owner(std::function<bool(const std::string&)> f){forbidden_=std::move(f);}
    document::Placement placement_seed()const override{return placement_->numeric_placement();}
    bool owns_reference_owner(const std::string& id)const override{return id==pending_.scope.id||id==pending_.origin().id||(forbidden_&&forbidden_(id));}
    auto references_without(std::size_t i)const->std::vector<document::ConstructionReference> override{return placement_->references_without(i);}
    document::ConstructionReference prepare_reference_candidate(std::size_t i,document::ConstructionReference r)const override{return placement_->prepare_reference(i,std::move(r));}
    bool set_reference(std::size_t i,document::ConstructionReference r,const QString& label)override{
        if(owns_reference_owner(r.owner_id))return false;QString error;
        const bool ok=placement_->set_reference(i,prepare_reference_candidate(i,std::move(r)),label,&error);set_status(error);return ok;
    }
    void set_reference_request_callback(std::function<void(std::size_t)> f){placement_->set_reference_request_callback(std::move(f));}
    void set_reference_highlights_changed_callback(std::function<void()> f){placement_->set_highlights_changed_callback(std::move(f));}
    auto highlighted_reference_entries()const{return placement_->highlighted_reference_entries();}
    std::size_t first_empty_position_index()const override{return placement_->first_empty_position_index();}
    void set_active_reference_index(std::optional<std::size_t> i)override{if(i&&active_input())end_input();placement_->set_active_reference_index(i);}
    void set_reference_inspected(std::size_t i,bool v)override{placement_->set_reference_inspected(i,v);}
    void clear_reference_highlights()override{inspected_.clear();placement_->clear_reference_highlights();refresh_sources();notify();}
    void set_origin_selection_mode_callback(std::function<void(bool)> f)override{placement_->set_origin_selection_mode_callback(std::move(f));}
    void set_origin_selection_mode_active(bool v)override{if(v&&active_input())end_input();placement_->set_origin_selection_mode_active(v);}
    void set_translation_constraint_state(const document::PointConstraintState& s,const kernel::Vec3& p)override{placement_->set_translation_constraint_state(s,p);}
    void set_remaining_rotation_dof(int n)override{placement_->set_remaining_rotation_dof(n);}
    void set_rotation_constraint_state(const document::OrientationConstraintState& s)override{placement_->set_rotation_constraint_state(s);}
    void set_orientation_base_rotation(const kernel::Vec3& v,bool c)override{placement_->set_orientation_base_rotation(v,c);}
    void set_resolved_rotation(const kernel::Vec3& v,bool valid=true)override{frame_valid_=valid;placement_->set_resolved_rotation(v,valid);}
    bool set_inline_parameter_value(std::string_view key,double value)override {
        const auto set=[value](QDoubleSpinBox* w){if(!w||!w->isEnabled()||w->isReadOnly()||!w->isVisible())return false;w->setValue(value);return true;};
        const std::array<std::string_view,3> xyz{"placement:x","placement:y","placement:z"},rot{"placement:rotation_x","placement:rotation_y","placement:rotation_z"};
        for(int i=0;i<3;++i){if(key==xyz[i])return set(placement_->translation_fields()[i]);if(key==rot[i]){if(placement_->rotation_fields()[i]->isEnabled()&&placement_->rotation_fields()[i]->isReadOnly())return false;return set(placement_->rotation_fields()[i])||set(placement_->rotation_offset_fields()[i]);}}
        constexpr std::string_view prefix="placement:reference_offset:";
        if(key.starts_with(prefix)){key.remove_prefix(prefix.size());std::size_t i{};const auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),i);return error==std::errc{}&&end==key.data()+key.size()&&placement_->set_reference_offset(i,value);}return false;
    }
protected:
    bool submit()override{try{auto value=pending_value();if(value.name.empty())throw std::invalid_argument("Specify a nonempty object name.");document::validate_cam_definition(*value.cam);commit_(std::move(value));return true;}
        catch(const std::exception& e){set_error(e.what());return false;}}
    void changeEvent(QEvent* e)override{PropertiesSubWindow::changeEvent(e);if(e->type()==QEvent::LanguageChange&&status_)retranslate();}
private:
    document::BodyHistory pending_;
    std::string error_key_;
    std::function<void(document::BodyHistory)> commit_;
    std::function<bool(const std::string&)> forbidden_;
    ui::ContainerPlacementSection* placement_{};QTableWidget* table_{};QPushButton* machine_{};QLabel* status_{};
    std::vector<std::function<void()>> translate_;
    std::map<std::string,QString> names_;std::set<std::string> stocks_,material_;std::set<int> inspected_;int active_{-1};bool frame_valid_{true};
    std::string source_id(int row)const{const auto& d=*pending_.cam;return row==0?d.stock_id:row==1?d.target_id:row>=2&&row<int(d.fixture_ids.size())+2?d.fixture_ids[row-2]:std::string{};}
    void notify(){if(changed)changed();}
    void label(QFormLayout* form,const char* key,QWidget* field){auto* text=new QLabel(this);form->addRow(text,field);translate_.push_back([text,key]{text->setText(tr(key));});}
    void refresh_machine(){machine_->setText(QString::fromStdString(pending_.cam->machine.name));machine_->setToolTip(tr("CAM machine properties"));}
    void retranslate(){set_internal_title(tr("CAM setup properties"));for(const auto& f:translate_)f();refresh_machine();refresh_sources();if(!error_key_.empty())status_->setText(tr(error_key_.c_str()));}
    void refresh_sources(){
        table_->setHorizontalHeaderLabels({"#",{},tr("Role"),tr("Zdroj"),{}});
        const auto& d=*pending_.cam;const int rows=int(d.fixture_ids.size())+3;table_->setRowCount(0);table_->setRowCount(rows);
        for(int row=0;row<rows;++row){const auto id=source_id(row);
            const bool available=(row==0?stocks_:material_).contains(id);
            table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            table_->setItem(row,2,new QTableWidgetItem(row==0?tr("Stock"):row==1?tr("Target Body"):tr("Fixture")));
            // Required references are replaceable, never removable. A fixture's
            // remove button deletes that list item and its row, not the Body.
            if(row>=2||id.empty()){
                auto* indicator=ui::build_reference_row_indicator([this,row,id]{if(row>=2&&!id.empty()){
                    std::set<int> retained;for(const auto index:inspected_)if(index!=row)retained.insert(index>row?index-1:index);inspected_=std::move(retained);
                    pending_.cam->fixture_ids.erase(pending_.cam->fixture_ids.begin()+row-2);arm(int(pending_.cam->fixture_ids.size())+2);
                }else arm(row);});
                ui::set_reference_row_populated(indicator,!id.empty());indicator->setToolTip(id.empty()?tr("Vyberte…"):tr("Remove this fixture from the setup."));table_->setCellWidget(row,1,indicator);
            }
            auto* field=new ui::ReferenceCellItem;table_->setItem(row,3,field);
            if(!id.empty()){field->set_reference(QString::fromStdString(id));field->setText(names_.contains(id)?names_.at(id):QString::fromStdString(id));field->set_missing(!available);}
            else{field->clear_reference();field->setText(tr("Vyberte…"));}
            field->set_active_input(row==active_);field->set_inspected(inspected_.contains(row));
            auto* eye=ui::build_reference_inspection_button(available,inspected_.contains(row),[this,row](bool on){if(on)inspected_.insert(row);else inspected_.erase(row);refresh_sources();notify();});
            table_->setCellWidget(row,4,ui::centered_cell_widget(eye));
        }
        table_->setFixedHeight(std::min(5,rows)*32+table_->horizontalHeader()->height()+4);
    }
};
} // namespace zima::app
