#pragma once
#include "controller_preferences.hpp"
#include <QByteArray>
#include <QSet>
#include <functional>

namespace zima::app::controller {
using Axes=std::array<double,6>;
struct Motion {
    double pan_x{},pan_y{},zoom{}; // View-height fractions, and logarithmic zoom in.
    std::array<double,3> rotation{}; // Camera-space rotation vector in degrees.
    [[nodiscard]] bool empty()const {
        return pan_x==0&&pan_y==0&&zoom==0&&rotation==std::array<double,3>{};
    }
};
inline double response(double input,double dead_zone) {
    if(!std::isfinite(input))return 0;
    const auto magnitude=std::clamp(std::abs(input),0.,1.);
    if(magnitude<=dead_zone)return 0;
    return std::copysign((magnitude-dead_zone)/(1-dead_zone),input);
}
// Kept independent from widgets/transports so stale input, impulses and timing
// can be verified without a connected device or a geometry calculation.
class MotionState {
public:
    Axes axes{};
    std::array<qint64,6> received{-1,-1,-1,-1,-1,-1};
    QSet<int> held;
    double zoom_impulse{};
    qint64 impulse_received{-1};
    void clear_motion(){axes={};received.fill(-1);zoom_impulse=0;impulse_received=-1;}
    void clear(){clear_motion();held.clear();}
    void update(const Axes& values,unsigned mask,qint64 now) {
        for(int i=0;i<6;++i)if(mask&(1u<<i)) {
            axes[i]=std::isfinite(values[i])?std::clamp(values[i],-1.,1.):0;received[i]=now;
        }
    }
    [[nodiscard]] QSet<int> buttons(const QSet<int>& next) {
        auto pressed=next-held;held=next;return pressed;
    }
    [[nodiscard]] Motion advance(const Profile& profile,double seconds,qint64 now) {
        Motion result;
        if(!std::isfinite(seconds)||seconds<=0||seconds>.1) {clear_motion();return result;}
        Axes filtered{};
        for(int i=0;i<6;++i)if(received[i]>=0&&now>=received[i]&&now-received[i]<=150)
            filtered[i]=response(axes[i],profile.dead_zone)*(profile.inverted[i]?-1:1);
        result.pan_x=filtered[0]*seconds*profile.pan_speed;
        result.pan_y=filtered[1]*seconds*profile.pan_speed;
        result.zoom=filtered[2]*seconds*1.5*profile.zoom_speed;
        if(impulse_received>=0&&now>=impulse_received&&now-impulse_received<=150)
            result.zoom+=zoom_impulse*profile.zoom_speed*(profile.inverted[2]?-1:1);
        zoom_impulse=0;impulse_received=-1;
        if(profile.rotation)for(int i=0;i<3;++i)
            result.rotation[i]=filtered[i+3]*seconds*90*profile.rotation_speed;
        return result;
    }
};
class NaviGoParser {
public:
    // Independent implementation of the documented newline CSV protocol.
    // Preserve every Z impulse, including when a later idle line is in the
    // same serial chunk. Status/invalid lines never overwrite valid motion.
    void feed(const QByteArray& bytes,const std::function<void(const Axes&)>& frame) {
        for(char byte:bytes) {
            if(byte=='\n') {
                if(!discarding_)parse(frame);
                pending_.clear();discarding_=false;
            } else if(!discarding_) {
                if(pending_.size()>=256){pending_.clear();discarding_=true;}
                else pending_.append(byte);
            }
        }
    }
    void clear(){pending_.clear();discarding_=false;}
private:
    QByteArray pending_;bool discarding_{};
    void parse(const std::function<void(const Axes&)>& frame) {
        const auto fields=pending_.trimmed().split(',');if(fields.size()!=6)return;
        Axes values{};
        for(int i=0;i<6;++i) {
            bool ok=false;values[i]=fields[i].trimmed().toDouble(&ok);
            if(!ok||!std::isfinite(values[i])||std::abs(values[i])>1000000)return;
        }
        frame(values);
    }
};
} // namespace zima::app::controller
