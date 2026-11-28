#include "controller_dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace zima::app::controller {
QString SettingsDialog::text(const char* key,const char* fallback)const{return settings_.text(key,QString::fromUtf8(fallback));}
SettingsDialog::SettingsDialog(const ApplicationSettings& settings,InputService& service,
    QVector<Command> commands,std::function<bool(const Preferences&,QString*)> save,QWidget* parent)
    :PropertiesSubWindow(settings.text("controller.title","3D controllers"),parent),settings_(settings),
     service_(service),pending_(settings.controllers),commands_(std::move(commands)),save_(std::move(save)) {
    setObjectName("controllerSettingsDialog");setMinimumWidth(670);set_initial_size({820,650});
    enabled_=new QCheckBox(text("controller.enabled","Enable 3D controllers"),this);
    enabled_->setObjectName("controllerEnabled");enabled_->setChecked(pending_.enabled);content_layout()->addWidget(enabled_);
    auto* form=new QFormLayout;
    device_=new QComboBox(this);device_->setObjectName("controllerDevice");
    form->addRow(text("controller.device","Device"),device_);
    status_=new QLabel(this);status_->setObjectName("controllerStatus");status_->setWordWrap(true);form->addRow(status_);
    context_=new QComboBox(this);context_->setObjectName("controllerContext");
    context_->addItem(text("controller.model","Modeling"),int(Context::Model));
    context_->addItem(text("controller.sketch","Sketch"),int(Context::Sketch));
    context_->addItem(text("controller.drawing","Drawing"),int(Context::Drawing));
    form->addRow(text("controller.profile","Profile"),context_);content_layout()->addLayout(form);
    tabs_=new QTabWidget(this);content_layout()->addWidget(tabs_);
    auto* motion_page=new QWidget(tabs_);auto* motion_layout=new QVBoxLayout(motion_page);
    auto* speeds=new QFormLayout;
    const auto speed=[&](const QString& label,const char* name,double minimum,double maximum,double step) {
        auto* box=new QDoubleSpinBox(motion_page);box->setObjectName(name);box->setRange(minimum,maximum);
        box->setSingleStep(step);box->setDecimals(2);speeds->addRow(label,box);return box;
    };
    pan_=speed(text("controller.pan_speed","Pan sensitivity"),"controllerPanSpeed",.05,10,.05);
    rotate_=speed(text("controller.rotation_speed","Rotation sensitivity"),"controllerRotationSpeed",.05,10,.05);
    zoom_=speed(text("controller.zoom_speed","Zoom sensitivity"),"controllerZoomSpeed",.05,10,.05);
    dead_zone_=speed(text("controller.dead_zone","Dead zone"),"controllerDeadZone",0,.5,.01);
    dead_zone_->setToolTip(text("controller.dead_zone_hint","Ignores small movements near the neutral position."));
    motion_layout->addLayout(speeds);
    rotation_=new QCheckBox(text("controller.rotation","Allow rotation"),motion_page);
    rotation_->setObjectName("controllerRotation");motion_layout->addWidget(rotation_);
    auto* axes=new QGridLayout;
    axes->addWidget(new QLabel(text("controller.input","Live input"),motion_page),0,1);
    axes->addWidget(new QLabel(text("controller.invert","Reverse"),motion_page),0,2);
    const char* keys[]{"controller.pan_x","controller.pan_y","controller.zoom","controller.pitch","controller.yaw","controller.roll"};
    const char* fallback[]{"Horizontal pan","Vertical pan","Zoom","Pitch","Yaw","Roll"};
    for(int i=0;i<6;++i) {
        const auto label=text(keys[i],fallback[i]);axes->addWidget(new QLabel(label,motion_page),i+1,0);
        levels_[i]=new QProgressBar(motion_page);levels_[i]->setObjectName("controllerAxis"+QString::number(i));
        levels_[i]->setRange(-100,100);levels_[i]->setValue(0);levels_[i]->setFormat("0");
        axes->addWidget(levels_[i],i+1,1);
        invert_[i]=new QCheckBox(motion_page);invert_[i]->setObjectName("controllerInvert"+QString::number(i));
        invert_[i]->setAccessibleName(text("controller.invert","Reverse")+" "+label);axes->addWidget(invert_[i],i+1,2);
    }
    axes->setColumnStretch(1,1);motion_layout->addLayout(axes);motion_layout->addStretch();
    tabs_->addTab(motion_page,text("controller.motion","Camera"));
    auto* button_page=new QWidget(tabs_);auto* button_layout=new QVBoxLayout(button_page);
    button_hint_=new QLabel(text("controller.buttons_hint","Press a device button to highlight its row. Commands run only in an active CAD view."),button_page);
    button_hint_->setWordWrap(true);button_layout->addWidget(button_hint_);
    buttons_=new QTableWidget(0,2,button_page);buttons_->setObjectName("controllerButtons");
    buttons_->setHorizontalHeaderLabels({text("controller.button","Button"),text("controller.command","Command")});
    buttons_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    buttons_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    buttons_->verticalHeader()->hide();buttons_->setSelectionMode(QAbstractItemView::NoSelection);
    button_layout->addWidget(buttons_);tabs_->addTab(button_page,text("controller.buttons","Buttons"));
    auto* connection_page=new QWidget(tabs_);auto* connection_layout=new QVBoxLayout(connection_page);auto* connection_form=new QFormLayout;
    port_=new QComboBox(connection_page);port_->setObjectName("controllerNaviGoPort");port_->setEditable(true);
    port_->addItem(QString{});port_->addItems(SerialReader::ports());port_->setCurrentText(pending_.navigo_port);
    connection_form->addRow(text("controller.navigo_port","NaviGo serial port"),port_);
    range_=new QDoubleSpinBox(connection_page);range_->setObjectName("controllerNaviGoRange");
    range_->setRange(.01,1000000);range_->setDecimals(2);range_->setValue(pending_.navigo_range);
    connection_form->addRow(text("controller.navigo_range","NaviGo input range"),range_);
    connection_layout->addLayout(connection_form);
    navigo_hint_=new QLabel(text("controller.navigo_hint","Select only the port belonging to NaviGo. The connection starts after OK (115200, 8N1). Its current protocol provides motion and zoom steps, not assignable button events."),connection_page);
    navigo_hint_->setWordWrap(true);connection_layout->addWidget(navigo_hint_);connection_layout->addStretch();
    tabs_->addTab(connection_page,text("controller.connection","Connection"));
    refresh_devices();load_editor();
    connect(device_,&QComboBox::currentIndexChanged,this,[this]{if(loading_)return;save_editor();pending_.selected_device=device_->currentData().toString();load_editor();});
    connect(context_,&QComboBox::currentIndexChanged,this,[this]{if(!loading_){save_editor();load_editor();}});
    auto* timer=new QTimer(this);timer->setInterval(33);
    connect(timer,&QTimer::timeout,this,[this]{refresh_devices();refresh_input();});timer->start();
}
QString SettingsDialog::edit_device()const {
    const auto selected=device_->currentData().toString();if(!selected.isEmpty())return selected;
    for(const auto& device:service_.devices())if(device.connected)return device.id;
    return {};
}
void SettingsDialog::save_editor() {
    if(loading_||editing_device_.isEmpty())return;
    auto profile=pending_.profile(editing_device_,editing_context_);
    profile.pan_speed=pan_->value();profile.rotation_speed=rotate_->value();profile.zoom_speed=zoom_->value();
    profile.dead_zone=dead_zone_->value();profile.rotation=rotation_->isChecked();
    for(int i=0;i<6;++i)profile.inverted[i]=invert_[i]->isChecked();
    for(int row=0;row<buttons_->rowCount();++row) {
        const int id=buttons_->item(row,0)->data(Qt::UserRole).toInt();
        auto* combo=qobject_cast<QComboBox*>(buttons_->cellWidget(row,1));
        const auto command=combo->currentData().toString();
        if(command.isEmpty())profile.buttons.remove(id);else profile.buttons[id]=command;
    }
    pending_.set_profile(editing_device_,editing_context_,profile);
}
void SettingsDialog::load_editor() {
    loading_=true;editing_device_=edit_device();editing_context_=Context(context_->currentData().toInt());
    const auto profile=pending_.profile(editing_device_,editing_context_);
    pan_->setValue(profile.pan_speed);rotate_->setValue(profile.rotation_speed);zoom_->setValue(profile.zoom_speed);
    dead_zone_->setValue(profile.dead_zone);rotation_->setChecked(profile.rotation);
    const bool drawing=editing_context_==Context::Drawing;
    rotation_->setEnabled(!drawing);rotate_->setEnabled(!drawing);
    if(drawing)rotation_->setChecked(false);
    for(int i=0;i<6;++i){invert_[i]->setChecked(profile.inverted[i]);invert_[i]->setEnabled(!drawing||i<3);}
    buttons_->setRowCount(0);
    for(const auto& device:service_.devices())if(device.id==editing_device_) {
        for(int id:device.buttons) {
            const int row=buttons_->rowCount();buttons_->insertRow(row);
            auto* item=new QTableWidgetItem(QString::number(id));item->setFlags(Qt::ItemIsEnabled);item->setData(Qt::UserRole,id);buttons_->setItem(row,0,item);
            auto* combo=new QComboBox(buttons_);combo->setObjectName("controllerButton"+QString::number(id));
            combo->addItem(text("controller.unassigned","Unassigned"),QString{});
            for(const auto& command:commands_)combo->addItem(command.icon,command.label,command.id);
            const auto binding=profile.buttons.value(id);int selected=combo->findData(binding);
            if(selected<0&&!binding.isEmpty()) {combo->addItem(text("controller.unavailable_command","Unavailable command"),binding);selected=combo->count()-1;}
            combo->setCurrentIndex(std::max(0,selected));buttons_->setCellWidget(row,1,combo);
        }
        break;
    }
    tabs_->setTabEnabled(0,!editing_device_.isEmpty());tabs_->setTabEnabled(1,!editing_device_.isEmpty());
    loading_=false;refresh_input();
}
void SettingsDialog::refresh_devices() {
    auto devices=service_.devices();if(devices==shown_devices_&&device_->count())return;
    if(device_->count())save_editor();shown_devices_=devices;loading_=true;
    device_->clear();device_->addItem(text("controller.automatic","Automatic"),QString{});
    for(const auto& device:devices)device_->addItem(device.name,device.id);
    int selected=device_->findData(pending_.selected_device);
    if(selected<0) {device_->addItem(text("controller.unavailable_device","Disconnected device"),pending_.selected_device);selected=device_->count()-1;}
    device_->setCurrentIndex(std::max(0,selected));loading_=false;load_editor();
}
void SettingsDialog::refresh_input() {
    bool connected=false;QString transport;
    for(const auto& device:service_.devices())if(device.id==editing_device_){connected=device.connected;transport=device.transport;}
    status_->setText(connected?text("controller.connected","Connected")+" · "+transport:text("controller.disconnected","No connected controller"));
    const auto state=service_.state(editing_device_);const auto now=monotonic_ms();
    for(int i=0;i<6;++i) {
        const int level=connected&&state.received[i]>=0&&now-state.received[i]<=150?int(std::lround(state.axes[i]*100)):0;
        levels_[i]->setValue(level);levels_[i]->setFormat(QString::number(level));
    }
    for(int row=0;row<buttons_->rowCount();++row) {
        auto* item=buttons_->item(row,0);const bool pressed=state.held.contains(item->data(Qt::UserRole).toInt());
        item->setData(Qt::BackgroundRole,pressed?QVariant(palette().brush(QPalette::Highlight)):QVariant{});
        item->setData(Qt::ForegroundRole,pressed?QVariant(palette().brush(QPalette::HighlightedText)):QVariant{});
    }
}
bool SettingsDialog::submit() {
    save_editor();pending_.enabled=enabled_->isChecked();pending_.selected_device=device_->currentData().toString();
    pending_.navigo_port=port_->currentText().trimmed();pending_.navigo_range=range_->value();
    if(!pending_.navigo_port.isEmpty()&&pending_.selected_device.isEmpty()&&service_.devices().isEmpty())
        pending_.selected_device="navigo:"+pending_.navigo_port;
    QString error;if(!save_(pending_,&error)){show_submission_error(error);return false;}
    return true;
}
}
