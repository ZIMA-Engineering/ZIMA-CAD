#include "controller_input.hpp"
#include <QTimer>
#include <chrono>

namespace zima::app::controller {
qint64 monotonic_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
struct InputService::Impl {
    Preferences preferences;
    QVector<Device> hid_devices;
    QMap<QString,MotionState> states;
    QMap<QString,MotionState> diagnostics;
    std::unique_ptr<HidBackend> hid;
    SerialReader serial;
    SerialSample serial_state;
    QString target_id,selected;
    qint64 previous_tick{monotonic_ms()};
    qint64 last_serial_received{-1};QString last_serial_port;
};
InputService::InputService(quintptr window,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()) {
    impl_->hid=make_hid_backend(window,{
        [this](QVector<Device> devices){refresh_devices(std::move(devices));},
        [this](const QString& id,const Axes& values,unsigned mask){receive_motion(id,values,mask);},
        [this](const QString& id,const QSet<int>& buttons,bool dispatch){receive_buttons(id,buttons,dispatch);}});
    impl_->hid->scan();
    auto* enumeration=new QTimer(this);enumeration->setInterval(2000);
    connect(enumeration,&QTimer::timeout,this,[this]{impl_->hid->scan();});enumeration->start();
    auto* timer=new QTimer(this);timer->setInterval(16);timer->setTimerType(Qt::PreciseTimer);
    connect(timer,&QTimer::timeout,this,[this]{impl_->hid->poll();advance(monotonic_ms());});timer->start();
}
InputService::~InputService()=default;
void InputService::configure(const Preferences& preferences) {
    if(impl_->preferences==preferences)return;
    impl_->preferences=preferences;impl_->serial.select(preferences.navigo_port);suspend();
}
const Preferences& InputService::preferences()const{return impl_->preferences;}
QVector<Device> InputService::devices()const {
    auto result=impl_->hid_devices;
    if(!impl_->preferences.navigo_port.isEmpty())result.push_back({
        "navigo:"+impl_->preferences.navigo_port,"NaviGo ("+impl_->preferences.navigo_port+")","USB CDC",{},
        impl_->serial_state.connected&&impl_->serial_state.port==impl_->preferences.navigo_port});
    return result;
}
QString InputService::selected_device()const {
    const auto available=devices();
    if(!impl_->preferences.selected_device.isEmpty()) {
        for(const auto& device:available)if(device.id==impl_->preferences.selected_device&&device.connected)return device.id;
        return {};
    }
    for(const auto& device:available)if(device.connected)return device.id;
    return {};
}
MotionState InputService::state(const QString& device)const{return impl_->diagnostics.value(device);}
void InputService::suspend() {
    for(auto& state:impl_->states)state.clear_motion();
    impl_->target_id.clear();impl_->previous_tick=monotonic_ms();
}
void InputService::refresh_devices(QVector<Device> devices) {
    for(const auto& old:impl_->hid_devices) {
        bool found=false;for(const auto& next:devices)found|=old.id==next.id&&next.connected;
        if(!found){impl_->states.remove(old.id);impl_->diagnostics.remove(old.id);}
    }
    impl_->hid_devices=std::move(devices);
}
void InputService::receive_motion(const QString& device,const Axes& values,unsigned mask) {
    if(device.isEmpty())return;
    impl_->states[device].update(values,mask,monotonic_ms());
    impl_->diagnostics[device].update(values,mask,monotonic_ms());
}
void InputService::receive_buttons(const QString& device,const QSet<int>& next,bool dispatch) {
    impl_->diagnostics[device].held=next;
    auto& state=impl_->states[device];const auto pressed=state.buttons(next);
    const auto active=target?target():Target{};
    if(!dispatch||!impl_->preferences.enabled||active.identity.isEmpty()||!active.commands||
        selected_device()!=device||active.identity!=impl_->target_id)return;
    const auto profile=impl_->preferences.profile(device,active.context);
    auto ordered=pressed.values();std::sort(ordered.begin(),ordered.end());
    for(int button:ordered) {
        // A preceding button can open a dialog or switch tabs. Recheck ownership.
        const auto current=target?target():Target{};
        if(!current.commands||current.identity!=active.identity)break;
        const auto binding=profile.buttons.value(button);
        if(!binding.isEmpty()&&command)command(binding);
    }
}
void InputService::advance(qint64 now) {
    auto serial=impl_->serial.take();
    impl_->serial_state=serial;
    const auto serial_id="navigo:"+serial.port;
    if(serial.connected&&serial.port==impl_->preferences.navigo_port&&serial.received>=0) {
        auto& state=impl_->states[serial_id];
        if(impl_->last_serial_received!=serial.received||impl_->last_serial_port!=serial.port) {
            auto values=serial.axes;for(auto& v:values)v/=impl_->preferences.navigo_range;
            values[2]=0;state.update(values,0x3f,serial.received);
            impl_->diagnostics[serial_id].update(values,0x3f,serial.received);
            impl_->last_serial_received=serial.received;impl_->last_serial_port=serial.port;
        }
        state.zoom_impulse+=serial.zoom_steps*std::log(1.15);
        if(serial.zoom_steps!=0)state.impulse_received=serial.received;
    } else if(!serial.port.isEmpty()){impl_->states[serial_id].clear();impl_->diagnostics[serial_id].clear();}
    const auto active=target?target():Target{};
    const auto selected=selected_device();
    const bool changed=active.identity!=impl_->target_id||selected!=impl_->selected;
    const double elapsed=(now-impl_->previous_tick)/1000.;impl_->previous_tick=now;
    if(changed||active.identity.isEmpty()||!impl_->preferences.enabled) {
        for(auto& state:impl_->states)state.clear_motion();
        impl_->target_id=active.identity;impl_->selected=selected;return;
    }
    if(selected.isEmpty())return;
    const auto motion=impl_->states[selected].advance(impl_->preferences.profile(selected,active.context),elapsed,now);
    if(!motion.empty()&&navigate)navigate(motion);
}
} // namespace zima::app::controller
