#include "controller_input.hpp"
#include <QCoreApplication>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace zima::app::controller;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        Preferences preferences;preferences.selected_device="hid:test";
        Profile profile;profile.pan_speed=2.3;profile.rotation_speed=.8;profile.zoom_speed=1.6;
        profile.inverted[4]=true;profile.buttons={{1,"fitViewAction"},{21,"standardTopAction"}};
        preferences.set_profile("hid:test",Context::Model,profile);
        preferences.set_profile("hid:test",Context::Sketch,preferences.profile("hid:test",Context::Sketch));
        check(decode(encode(preferences))==preferences,"Device/context preferences did not round-trip");
        check(!Preferences{}.profile("test",Context::Sketch).rotation&&!Preferences{}.profile("test",Context::Drawing).rotation,"Planar contexts rotate by default");
        auto malformed=decode(R"({"schema":1,"navigoRange":-5,"profiles":{"test/model":{"pan":-3,"deadZone":8,"buttons":{"-1":"bad","129":"bad","2":"fitViewAction"}}}})");
        check(malformed.navigo_range==100&&malformed.profile("test",Context::Model).pan_speed==1,"Invalid preferences not bounded");
        check(malformed.profile("test",Context::Model).buttons.size()==1,"Invalid button IDs accepted");

        NaviGoParser parser;std::vector<Axes> frames;
        const auto feed=[&](const QByteArray& bytes){parser.feed(bytes,[&](const Axes& frame){frames.push_back(frame);});};
        feed("0,0,50,");check(frames.empty(),"Partial serial frame accepted");
        feed("0,0,0\r\n0,0,0,0,0,0\nINFO: ready\n1,2,0,3,4,5\n");
        check(frames.size()==3&&frames[0][2]==50&&frames[1][2]==0&&frames[2][5]==5,"Serial chunks or zoom impulse lost");
        feed("nan,0,0,0,0,0\n0,0,inf,0,0,0\n0,0,0,0,0\n0,0,0,0,0,0,0\n");
        check(frames.size()==3,"Invalid serial motion accepted");
        feed(QByteArray(10000,'x')+"0,0,50,0,0,0\n0,0,-50,0,0,0\n");
        check(frames.size()==4&&frames.back()[2]==-50,"Oversized serial line poisoned framing");
        for(int split=0;split<18;++split) {
            NaviGoParser separate;int count=0;const QByteArray line="1,2,3,4,5,6\r\n";
            separate.feed(line.left(split),[&](const Axes&){++count;});
            separate.feed(line.mid(split),[&](const Axes&){++count;});check(count==1,"Serial split boundary failed");
        }
        Profile defaults;defaults.dead_zone=0;
        check(response(.03,.04)==0&&response(-.04,.04)==0&&std::abs(response(.52,.04)-.5)<1e-12,"Dead zone response is wrong");
        {
            MotionState reverse;Profile profile;profile.dead_zone=0;profile.inverted.fill(true);profile.rotation=false;
            reverse.update({1,1,1,1,1,1},0x3f,10);const auto motion=reverse.advance(profile,.02,10);
            check(motion.pan_x<0&&motion.pan_y<0&&motion.zoom<0&&motion.rotation==std::array<double,3>{},"Axis reversal or rotation lock failed");
        }
        for(int rate:{30,60,120}) {
            MotionState state;Motion sum;
            for(int frame=1;frame<=2*rate;++frame) {
                const qint64 now=frame*1000/rate;state.update({.5,-.25,.2,.1,.2,.3},0x3f,now);
                const auto motion=state.advance(defaults,1./rate,now);
                sum.pan_x+=motion.pan_x;sum.pan_y+=motion.pan_y;sum.zoom+=motion.zoom;
                for(int i=0;i<3;++i)sum.rotation[i]+=motion.rotation[i];
            }
            check(std::abs(sum.pan_x-1)<1e-12&&std::abs(sum.pan_y+.5)<1e-12&&std::abs(sum.zoom-.6)<1e-12,"Motion depends on timer frequency");
            check(std::abs(sum.rotation[2]-54)<1e-10,"Rotation integration failed");
        }
        MotionState state;state.update({1,0,0,.5,0,0},9,100);
        check(state.advance(defaults,.016,251).empty(),"Stale motion did not stop");
        state.update({1,0,0,0,0,0},1,300);state.update({0,0,0,.5,0,0},8,500);
        const auto partial=state.advance(defaults,.016,500);
        check(partial.pan_x==0&&partial.rotation[0]>0,"Separate HID report freshness lost");
        state.update({1,0,0,0,0,0},1,600);
        check(state.advance(defaults,.2,600).empty(),"Long UI stall integrated stale motion");
        state.update({std::numeric_limits<double>::quiet_NaN(),0,0,0,0,0},1,700);
        check(state.advance(defaults,.016,700).empty(),"Nonfinite input moved camera");
        state.zoom_impulse=.4;state.impulse_received=800;
        check(std::abs(state.advance(defaults,.016,800).zoom-.4)<1e-12&&state.advance(defaults,.016,810).zoom==0,"Impulse repeated or disappeared");
        check(state.buttons({1,21})==QSet<int>({1,21})&&state.buttons({1,21}).isEmpty()&&state.buttons({21}).isEmpty(),"Button edges repeat");
        check(state.buttons({1,21})==QSet<int>({1}),"Released/repressed button not detected");

        InputService service(0);service.refresh_devices({{"test","Test","USB HID",{1,2},true}});
        Preferences config;config.selected_device="test";service.configure(config);
        Target target{"document/model",Context::Model,true};service.target=[&]{return target;};
        int commands=0,motions=0;service.command=[&](const QString& command){check(command=="fitViewAction","Unexpected command");++commands;};
        service.navigate=[&](const Motion&){++motions;};
        const auto start=monotonic_ms();service.advance(start);
        service.receive_buttons("test",{1});service.receive_buttons("test",{1});check(commands==1,"Held button repeated its command");
        target.commands=false;service.receive_buttons("test",{});service.receive_buttons("test",{1});check(commands==1,"Dialog/text entry allowed a command");
        service.receive_motion("test",{1,0,0,0,0,0},1);service.advance(start+16);check(motions==1,"Active controller did not navigate");
        target.identity.clear();service.advance(start+32);service.receive_motion("test",{1,0,0,0,0,0},1);service.advance(start+48);
        check(motions==1&&service.state("test").axes[0]==1,"Focus gating or independent diagnostics failed");
        target.identity="second/model";service.advance(start+64);check(motions==1,"Old motion leaked into a new tab");
        service.refresh_devices({{"test","Test","USB HID",{1,2},false}});
        check(service.selected_device().isEmpty()&&service.state("test").axes==Axes{}&&service.state("test").held.isEmpty(),"Disconnected descriptor retained input");
        service.refresh_devices({});check(service.selected_device().isEmpty()&&service.state("test").axes==Axes{},"Disconnect retained input");
        std::cout<<"Controller preferences, parser framing, impulse retention, finite/timed motion, focus, buttons and disconnect passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
