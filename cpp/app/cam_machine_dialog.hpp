#pragma once
#include <zima/cam/program.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <array>
#include <functional>

namespace zima::app {
class CamMachineDialog final : public ui::PropertiesSubWindow {
public:
    std::function<void()> import_machine;
    CamMachineDialog(cam::Machine value,std::function<void(cam::Machine)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("CAM machine properties"),parent),pending_(std::move(value)),commit_(std::move(commit)) {
        setObjectName("camMachineDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* library=new QPushButton(this);library->setObjectName("camImportMachine");content_layout()->addWidget(library,0,Qt::AlignLeft);
        translate_.push_back([library]{library->setText(tr("Import from library…"));});connect(library,&QPushButton::clicked,this,[this]{if(import_machine)import_machine();});
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending_.name),this);name->setObjectName("camMachineName");
        label(form,QT_TR_NOOP("Název"),name);connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending_.name=text.toStdString();clear_status();});
        kind_=new QComboBox(this);kind_->setObjectName("camMachineKind");kind_->addItem({});kind_->addItem({});
        kind_->setCurrentIndex(pending_.kind==cam::MachineKind::Milling?0:1);label(form,QT_TR_NOOP("Machining type"),kind_);
        connect(kind_,&QComboBox::currentIndexChanged,this,[this](int i){pending_.kind=i==0?cam::MachineKind::Milling:cam::MachineKind::Turning;clear_status();});
        auto* axes=new QSpinBox(this);axes->setObjectName("camMachineAxes");axes->setRange(2,5);axes->setValue(int(pending_.axes));axes->setMaximumWidth(96);label(form,QT_TR_NOOP("Axis count"),axes);
        connect(axes,&QSpinBox::valueChanged,this,[this](int value){pending_.axes=unsigned(value);clear_status();});
        auto* post=new QLineEdit(QString::fromStdString(pending_.postprocessor_id),this);post->setObjectName("camMachinePostprocessor");label(form,QT_TR_NOOP("Postprocessor identifier"),post);
        connect(post,&QLineEdit::textChanged,this,[this](const QString& text){pending_.postprocessor_id=text.toStdString();clear_status();});
        auto* capability=new QLabel(this);capability->setObjectName("camMachineCapability");capability->setWordWrap(true);content_layout()->addWidget(capability);
        translate_.push_back([capability]{capability->setText(tr("Toolpath generation currently supports 3-axis milling."));});
        travel_=new QTableWidget(3,3,this);travel_->setObjectName("camMachineTravel");travel_->verticalHeader()->hide();
        travel_->setSelectionMode(QAbstractItemView::NoSelection);travel_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        travel_->verticalHeader()->setDefaultSectionSize(32);travel_->setFixedHeight(134);
        travel_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        travel_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);travel_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        // Shared Properties sizing keeps numeric columns compact. Size this
        // all-numeric table to those columns instead of leaving an empty viewport.
        const auto fit_travel=[this]{travel_->setFixedWidth(travel_->horizontalHeader()->length()+2*travel_->frameWidth()+2);};
        connect(travel_->horizontalHeader(),&QHeaderView::sectionResized,this,[fit_travel]{fit_travel();});
        const std::array<double*,3> minimum{&pending_.minimum.x,&pending_.minimum.y,&pending_.minimum.z};
        const std::array<double*,3> maximum{&pending_.maximum.x,&pending_.maximum.y,&pending_.maximum.z};
        for(int i=0;i<3;++i) {
            const auto axis=QString(QChar('X'+i));travel_->setItem(i,0,new QTableWidgetItem(axis));
            travel_->setCellWidget(i,1,number("camMinimum"+axis,*minimum[i],-1e9,1e9," mm"));
            travel_->setCellWidget(i,2,number("camMaximum"+axis,*maximum[i],-1e9,1e9," mm"));
        }
        content_layout()->addWidget(travel_,0,Qt::AlignLeft);fit_travel();
        auto* speeds=new QFormLayout;speeds->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);content_layout()->addLayout(speeds);
        label(speeds,QT_TR_NOOP("Maximum cutting feed"),number("camMaximumFeed",pending_.maximum_feed_mm_min,.000001,1e9," mm/min"));
        label(speeds,QT_TR_NOOP("Rapid feed"),number("camRapidFeed",pending_.rapid_mm_min,.000001,1e9," mm/min"));
        label(speeds,QT_TR_NOOP("Maximum spindle speed"),number("camMaximumSpindle",pending_.maximum_spindle_rpm,.000001,1e9," min⁻¹"));
        auto* note=new QLabel(this);note->setWordWrap(true);content_layout()->addWidget(note);
        translate_.push_back([note]{note->setText(tr("Travel limits apply to the tool mounting point in machine coordinates. Cutting feed and spindle commands are checked against these limits."));});
        status_=new QLabel(this);status_->setObjectName("camMachineStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        retranslate();set_initial_size({600,590});
    }
    const cam::Machine& pending_value()const{return pending_;}
    void replace_machine(cam::Machine value){
        cam::validate(value);pending_=std::move(value);
        for(const auto& pair:std::array<std::pair<const char*,std::string>,2>{{{"camMachineName",pending_.name},{"camMachinePostprocessor",pending_.postprocessor_id}}}){
            auto* field=findChild<QLineEdit*>(pair.first);const QSignalBlocker block(field);field->setText(QString::fromStdString(pair.second));}
        {const QSignalBlocker block(kind_);kind_->setCurrentIndex(pending_.kind==cam::MachineKind::Milling?0:1);}
        auto* axes=findChild<QSpinBox*>("camMachineAxes");{const QSignalBlocker block(axes);axes->setValue(int(pending_.axes));}
        const std::array<std::pair<const char*,double>,9> values{{{"camMinimumX",pending_.minimum.x},{"camMinimumY",pending_.minimum.y},{"camMinimumZ",pending_.minimum.z},
            {"camMaximumX",pending_.maximum.x},{"camMaximumY",pending_.maximum.y},{"camMaximumZ",pending_.maximum.z},{"camMaximumFeed",pending_.maximum_feed_mm_min},
            {"camRapidFeed",pending_.rapid_mm_min},{"camMaximumSpindle",pending_.maximum_spindle_rpm}}};
        for(const auto& [name,n]:values){auto* spin=findChild<QDoubleSpinBox*>(name);const QSignalBlocker block(spin);spin->setValue(n);}clear_status();
    }
protected:
    bool submit()override {
        if(QString::fromStdString(pending_.name).trimmed().isEmpty())return error(QT_TR_NOOP("Specify a nonempty object name."));
        if(pending_.minimum.x>=pending_.maximum.x||pending_.minimum.y>=pending_.maximum.y||pending_.minimum.z>=pending_.maximum.z)
            return error(QT_TR_NOOP("Each axis minimum must be smaller than its maximum."));
        try{cam::validate(pending_);commit_(pending_);return true;}
        catch(const std::exception& e){error_key_=e.what();status_->setText(tr(error_key_.c_str()));return false;}
    }
    void changeEvent(QEvent* event)override {
        PropertiesSubWindow::changeEvent(event);if(event->type()==QEvent::LanguageChange&&status_)retranslate();
    }
private:
    cam::Machine pending_;std::function<void(cam::Machine)> commit_;
    std::vector<std::function<void()>> translate_;QComboBox* kind_{};QTableWidget* travel_{};QLabel* status_{};
    std::string error_key_;
    void clear_status(){error_key_.clear();if(status_)status_->clear();}
    bool error(const char* key){error_key_=key;status_->setText(tr(key));return false;}
    void label(QFormLayout* form,const char* key,QWidget* widget) {
        auto* text=new QLabel(this);form->addRow(text,widget);translate_.push_back([text,key]{text->setText(tr(key));});
    }
    QDoubleSpinBox* number(const QString& id,double& value,double low,double high,const QString& unit) {
        auto* spin=new QDoubleSpinBox(this);spin->setObjectName(id);spin->setDecimals(6);spin->setRange(low,high);spin->setSuffix(unit);spin->setValue(value);
        // Connections follow initialization: unchanged confirmation retains all
        // authored precision, even when the editor displays fewer digits.
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,&value](double v){value=v;clear_status();});return spin;
    }
    void retranslate() {
        set_internal_title(tr("CAM machine properties"));kind_->setItemText(0,tr("Milling"));kind_->setItemText(1,tr("Turning"));
        travel_->setHorizontalHeaderLabels({tr("Osa"),tr("Minimum travel"),tr("Maximum travel")});
        for(const auto& f:translate_)f();if(!error_key_.empty())status_->setText(tr(error_key_.c_str()));
    }
};
} // namespace zima::app
