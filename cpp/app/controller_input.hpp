#pragma once
#include "controller_motion.hpp"
#include <QObject>
#include <QVector>
#include <memory>

namespace zima::app::controller {
struct Device {
    QString id,name,transport;
    QVector<int> buttons;
    bool connected{true};
    bool operator==(const Device&)const=default;
};
struct Target {
    QString identity; // Empty suspends navigation and consumes no commands.
    Context context{Context::Model};
    bool commands{};
};
struct HidCallbacks {
    std::function<void(QVector<Device>)> devices;
    std::function<void(const QString&,const Axes&,unsigned)> motion;
    std::function<void(const QString&,const QSet<int>&,bool)> buttons;
};
class HidBackend {
public:
    virtual ~HidBackend()=default;
    virtual void scan()=0;
    virtual void poll()=0;
};
std::unique_ptr<HidBackend> make_hid_backend(quintptr window,HidCallbacks callbacks);
qint64 monotonic_ms();
struct SerialSample {QString port;bool connected{};Axes axes{};double zoom_steps{};qint64 received{-1};};
class SerialReader {
public:
    SerialReader();~SerialReader();
    void select(const QString& port);
    SerialSample take();
    static QStringList ports();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
class InputService final:public QObject {
public:
    explicit InputService(quintptr window,QObject* parent=nullptr);
    ~InputService()override;
    void configure(const Preferences& preferences);
    [[nodiscard]] const Preferences& preferences()const;
    [[nodiscard]] QVector<Device> devices()const;
    [[nodiscard]] QString selected_device()const;
    [[nodiscard]] MotionState state(const QString& device)const;
    void suspend();
    std::function<Target()> target;
    std::function<void(const Motion&)> navigate;
    std::function<void(const QString&)> command;
    // Transport-independent entry points also used by deterministic GUI tests.
    void receive_motion(const QString&,const Axes&,unsigned);
    void receive_buttons(const QString&,const QSet<int>&,bool dispatch=true);
    void refresh_devices(QVector<Device>);
    void advance(qint64 now);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace zima::app::controller
