#pragma once
#include <zima/document/body_history.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <map>
#include <set>
#include <algorithm>

namespace zima::app {
class RenderBodyDialog final : public ui::PropertiesSubWindow {
public:
    document::BodyHistory pending;
    std::function<void()> changed;
    RenderBodyDialog(document::BodyHistory value,std::function<void(document::BodyHistory)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Render Body properties"),parent),pending(std::move(value)),commit_(std::move(commit)) {
        setObjectName("renderBodyDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("renderBodyName");
        label(form,QT_TR_NOOP("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        mode_=new QComboBox(this);mode_->setObjectName("renderSourceMode");mode_->addItem({});mode_->addItem({});
        mode_->setCurrentIndex(pending.render->visible_bodies?0:1);label(form,QT_TR_NOOP("Render sources"),mode_);
        table_=new QTableWidget(0,4,this);table_->setObjectName("renderBodySources");
        table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(const auto column:{0,1,3}) {table_->setColumnWidth(column,32);table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        ui::install_reference_cell_delegate(table_);content_layout()->addWidget(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2)arm(row);});
        connect(mode_,&QComboBox::currentIndexChanged,this,[this](int index){pending.render->visible_bodies=index==0;active_=-1;inspected_.clear();refresh_sources();notify();});
        auto* explanation=new QLabel(this);explanation->setWordWrap(true);content_layout()->addWidget(explanation);
        translate_.push_back([explanation]{explanation->setText(tr("One scene shares cameras, lights, shadows and reflections across the selected Bodies. Geometry and appearance come from Modeling."));});
        auto* settings=new QFormLayout;content_layout()->addLayout(settings);
        camera_=new QComboBox(this);camera_->setObjectName("renderActiveCamera");camera_->addItem({},QString{});
        for(const auto& o:pending.render->objects)if(o.kind==document::RenderObjectKind::Camera)
            camera_->addItem(QString::fromStdString(o.name),QString::fromStdString(o.id));
        const auto selected=QString::fromStdString(pending.render->active_camera_id);
        if(!selected.isEmpty()&&camera_->findData(selected)<0)camera_->addItem(selected,selected);
        camera_->setCurrentIndex(camera_->findData(selected));label(settings,QT_TR_NOOP("Active camera"),camera_);
        connect(camera_,&QComboBox::currentIndexChanged,this,[this]{pending.render->active_camera_id=camera_->currentData().toString().toStdString();});
        auto& s=pending.render->settings;
        integer(settings,QT_TR_NOOP("Image width"),"renderWidth",s.width,16,8192," px");
        integer(settings,QT_TR_NOOP("Image height"),"renderHeight",s.height,16,8192," px");
        integer(settings,QT_TR_NOOP("Samples per pixel"),"renderSamples",s.samples,1,4096);
        integer(settings,QT_TR_NOOP("Light bounces"),"renderBounces",s.bounces,1,12);
        number(settings,QT_TR_NOOP("Exposure"),"renderExposure",s.exposure_ev,-24,24," EV");
        number(settings,QT_TR_NOOP("Environment intensity"),"renderEnvironment",s.environment_strength,0,100);
        auto* finish=new QCheckBox(this);finish->setObjectName("renderUseModelFinish");finish->setChecked(s.use_model_finish);settings->addRow(finish);
        translate_.push_back([finish]{finish->setText(tr("Use surface finish from Modeling"));});
        auto* roughness=number(settings,QT_TR_NOOP("Roughness"),"renderRoughness",s.roughness,0,1);
        auto* metallic=number(settings,QT_TR_NOOP("Metallic"),"renderMetallic",s.metallic,0,1);
        const auto visibility=[=,this]{settings->setRowVisible(roughness,!pending.render->settings.use_model_finish);settings->setRowVisible(metallic,!pending.render->settings.use_model_finish);};
        connect(finish,&QCheckBox::toggled,this,[this,visibility](bool on){pending.render->settings.use_model_finish=on;visibility();});visibility();
        status_=new QLabel(this);status_->setObjectName("renderBodyStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        retranslate();set_initial_size({560,680});
    }
    void set_sources(std::map<std::string,QString> labels,std::set<std::string> available){labels_=std::move(labels);available_=std::move(available);refresh_sources();}
    void set_source(const std::string& id) {
        if(!active_input()||!available_.contains(id))return;
        auto& ids=pending.render->source_ids;const auto found=std::ranges::find(ids,id);
        if(found!=ids.end()&&std::distance(ids.begin(),found)!=active_){set_status(tr("This Body is already included in the render."));return;}
        if(active_==int(ids.size()))ids.push_back(id);else ids[active_]=id;
        active_=-1;set_status({});refresh_sources();notify();
    }
    void arm(int row){if(pending.render->visible_bodies||row<0||row>int(pending.render->source_ids.size()))return;active_=row;refresh_sources();notify();}
    void end_input(){active_=-1;refresh_sources();notify();}
    bool active_input()const{return !pending.render->visible_bodies&&active_>=0;}
    const std::set<std::string>& inspected_sources()const{return inspected_;}
    void set_status(const QString& text){status_->setText(text);}
protected:
    bool submit()override {
        try {
            if(QString::fromStdString(pending.name).trimmed().isEmpty())throw std::invalid_argument("Specify a nonempty object name.");
            document::validate_render_definition(*pending.render);commit_(pending);return true;
        }catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
    void changeEvent(QEvent* event)override {PropertiesSubWindow::changeEvent(event);if(event->type()==QEvent::LanguageChange&&status_)retranslate();}
private:
    std::function<void(document::BodyHistory)> commit_;
    std::vector<std::function<void()>> translate_;
    QTableWidget* table_{};QComboBox *mode_{},*camera_{};QLabel* status_{};
    std::map<std::string,QString> labels_;std::set<std::string> available_,inspected_;int active_{-1};
    void notify(){if(changed)changed();}
    void label(QFormLayout* form,const char* key,QWidget* field){auto* label=new QLabel(this);form->addRow(label,field);translate_.push_back([label,key]{label->setText(tr(key));});}
    void integer(QFormLayout* form,const char* key,const char* name,unsigned& value,int low,int high,const QString& suffix={}) {
        auto* spin=new QSpinBox(this);spin->setObjectName(name);spin->setRange(low,high);spin->setSuffix(suffix);spin->setValue(int(value));label(form,key,spin);
        connect(spin,&QSpinBox::valueChanged,this,[&value](int n){value=unsigned(n);});
    }
    QDoubleSpinBox* number(QFormLayout* form,const char* key,const char* name,double& value,double low,double high,const QString& suffix={}) {
        auto* spin=new QDoubleSpinBox(this);spin->setObjectName(name);spin->setDecimals(3);spin->setRange(low,high);spin->setSuffix(suffix);spin->setValue(value);label(form,key,spin);
        connect(spin,&QDoubleSpinBox::valueChanged,this,[&value](double n){value=n;});return spin;
    }
    void retranslate(){set_internal_title(tr("Render Body properties"));mode_->setItemText(0,tr("Visible Bodies"));mode_->setItemText(1,tr("Selected Bodies"));camera_->setItemText(0,tr("No active camera"));for(const auto& f:translate_)f();refresh_sources();}
    void refresh_sources() {
        table_->setVisible(!pending.render->visible_bodies);table_->setHorizontalHeaderLabels({"#",{},tr("Zdroj"),{}});
        const auto& ids=pending.render->source_ids;table_->setRowCount(0);table_->setRowCount(int(ids.size()+1));
        for(int row=0;row<=int(ids.size());++row) {
            const bool filled=row<int(ids.size());const std::string id=filled?ids[row]:std::string{};
            table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row,filled]{
                if(filled){inspected_.erase(pending.render->source_ids[row]);pending.render->source_ids.erase(pending.render->source_ids.begin()+row);active_=-1;refresh_sources();notify();}
                else arm(row);
            });
            ui::set_reference_row_populated(indicator,filled);indicator->setToolTip(filled?tr("Remove this Body from the render selection."):tr("Vyberte…"));table_->setCellWidget(row,1,indicator);
            auto* field=new ui::ReferenceCellItem;table_->setItem(row,2,field);
            if(filled){field->set_reference(QString::fromStdString(id));field->setText(labels_.contains(id)?labels_.at(id):QString::fromStdString(id));field->set_missing(!available_.contains(id));}
            else {field->clear_reference();field->setText(tr("Vyberte…"));}
            field->set_active_input(row==active_);field->set_inspected(inspected_.contains(id));
            auto* eye=ui::build_reference_inspection_button(filled&&available_.contains(id),inspected_.contains(id),[this,id](bool on){if(on)inspected_.insert(id);else inspected_.erase(id);refresh_sources();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eye));
        }
        table_->setFixedHeight(std::min(6,int(ids.size()+1))*32+table_->horizontalHeader()->height()+4);
    }
};
} // namespace zima::app
