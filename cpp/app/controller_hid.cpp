#include "controller_input.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <map>
#include <set>
#include <vector>
#include <bit>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#else
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace zima::app::controller {
namespace {
QString identity(unsigned vendor,unsigned product,const QByteArray& path) {
    return QStringLiteral("hid:%1:%2:%3").arg(vendor,4,16,QChar('0')).arg(product,4,16,QChar('0'))
        .arg(QString::fromLatin1(QCryptographicHash::hash(path,QCryptographicHash::Sha256).toHex().left(16)));
}
bool vendor_supported(unsigned vendor){return vendor==0x046d||vendor==0x256f;}
// Device coordinates: X is horizontal, Z is vertical, Y is depth.
// Pulling toward the user zooms in; pushing away zooms out (user default).
// Both native transports use the same camera-space convention.
Axes camera_axes(const Axes& raw){return {raw[0],-raw[2],raw[1],raw[3],-raw[5],raw[4]};}
unsigned camera_mask(unsigned raw) {
    constexpr int source[]{0,2,1,3,5,4};unsigned mask=0;
    for(int i=0;i<6;++i)if(raw&(1u<<source[i]))mask|=1u<<i;
    return mask;
}
#ifdef _WIN32
class WindowsHid final:public HidBackend,public QAbstractNativeEventFilter {
    struct Entry {
        Device device;std::vector<std::byte> preparsed;
        std::vector<HIDP_VALUE_CAPS> axes;std::vector<HIDP_BUTTON_CAPS> buttons;
        QMap<int,QSet<int>> button_reports;
    };
    HidCallbacks callbacks_;std::map<HANDLE,Entry> entries_;HWND window_{};bool registered_{};
public:
    WindowsHid(quintptr window,HidCallbacks callbacks):callbacks_(std::move(callbacks)),window_(reinterpret_cast<HWND>(window)) {
        if(window_) {
            RAWINPUTDEVICE device{1,8,RIDEV_DEVNOTIFY,window_};
            registered_=RegisterRawInputDevices(&device,1,sizeof(device));
            if(registered_)QCoreApplication::instance()->installNativeEventFilter(this);
        }
    }
    ~WindowsHid()override {
        if(registered_) {
            QCoreApplication::instance()->removeNativeEventFilter(this);
            RAWINPUTDEVICE device{1,8,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&device,1,sizeof(device));
        }
    }
    void poll()override{}
    void scan()override {
        UINT count{};if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1)||count>4096)return;
        std::vector<RAWINPUTDEVICELIST> devices(count);
        const auto actual=GetRawInputDeviceList(devices.data(),&count,sizeof(RAWINPUTDEVICELIST));
        if(actual==UINT(-1))return;
        std::set<HANDLE> present;QVector<Device> available;
        for(UINT i=0;i<actual;++i) {
            const auto& device=devices[i];if(device.dwType!=RIM_TYPEHID)continue;
            RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT size=sizeof(info);
            if(GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICEINFO,&info,&size)==UINT(-1)||
                !vendor_supported(info.hid.dwVendorId)||info.hid.usUsagePage!=1||info.hid.usUsage!=8)continue;
            if(!entries_.contains(device.hDevice)) {
                Entry entry;size=0;
                if(GetRawInputDeviceInfoW(device.hDevice,RIDI_PREPARSEDDATA,nullptr,&size)==UINT(-1)||size>65536||!size)continue;
                entry.preparsed.resize(size);
                if(GetRawInputDeviceInfoW(device.hDevice,RIDI_PREPARSEDDATA,entry.preparsed.data(),&size)==UINT(-1))continue;
                auto* parsed=reinterpret_cast<PHIDP_PREPARSED_DATA>(entry.preparsed.data());
                HIDP_CAPS caps{};if(HidP_GetCaps(parsed,&caps)!=HIDP_STATUS_SUCCESS||caps.NumberInputValueCaps>128||caps.NumberInputButtonCaps>128)continue;
                USHORT axis_count=caps.NumberInputValueCaps;entry.axes.resize(axis_count);
                if(HidP_GetValueCaps(HidP_Input,entry.axes.data(),&axis_count,parsed)!=HIDP_STATUS_SUCCESS)continue;
                entry.axes.resize(axis_count);unsigned axes_mask=0;
                for(const auto& cap:entry.axes)if(cap.UsagePage==1&&cap.BitSize>0&&cap.BitSize<=32) {
                    const int first=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage;
                    const int last=cap.IsRange?cap.Range.UsageMax:first;
                    for(int usage=0x30;usage<=0x35;++usage)if(usage>=first&&usage<=last)axes_mask|=1u<<(usage-0x30);
                }
                if(axes_mask!=0x3f)continue;
                USHORT button_count=caps.NumberInputButtonCaps;entry.buttons.resize(button_count);
                if(button_count&&HidP_GetButtonCaps(HidP_Input,entry.buttons.data(),&button_count,parsed)!=HIDP_STATUS_SUCCESS)continue;
                entry.buttons.resize(button_count);
                for(const auto& cap:entry.buttons)if(cap.UsagePage==9) {
                    const int first=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage;
                    const int last=cap.IsRange?cap.Range.UsageMax:first;
                    for(int button=std::max(1,first);button<=std::min(128,last);++button)
                        if(!entry.device.buttons.contains(button))entry.device.buttons.push_back(button);
                }
                size=0;GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICENAME,nullptr,&size);
                if(!size||size>4096)continue;
                std::wstring path(size+1,L'\0');if(GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICENAME,path.data(),&size)==UINT(-1))continue;
                path.resize(wcslen(path.c_str()));
                entry.device.id=identity(info.hid.dwVendorId,info.hid.dwProductId,QString::fromStdWString(path).toUtf8());
                entry.device.name=QStringLiteral("3Dconnexion HID (%1:%2)").arg(info.hid.dwVendorId,4,16,QChar('0')).arg(info.hid.dwProductId,4,16,QChar('0'));
                const auto handle=CreateFileW(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
                if(handle!=INVALID_HANDLE_VALUE) {
                    wchar_t name[256]{};if(HidD_GetProductString(handle,name,sizeof(name))&&name[0])entry.device.name=QString::fromWCharArray(name);
                    CloseHandle(handle);
                }
                entry.device.transport="USB HID";entry.device.connected=registered_;
                std::sort(entry.device.buttons.begin(),entry.device.buttons.end());
                entries_.emplace(device.hDevice,std::move(entry));
            }
            present.insert(device.hDevice);available.push_back(entries_.at(device.hDevice).device);
        }
        std::erase_if(entries_,[&](const auto& entry){return !present.contains(entry.first);});
        callbacks_.devices(std::move(available));
    }
    bool nativeEventFilter(const QByteArray&,void* message,qintptr*)override {
        auto* msg=static_cast<MSG*>(message);if(msg->hwnd!=window_)return false;
        if(msg->message==WM_INPUT_DEVICE_CHANGE){scan();return false;}
        if(msg->message!=WM_INPUT)return false;
        UINT size{};const auto input=reinterpret_cast<HRAWINPUT>(msg->lParam);
        if(GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==UINT(-1)||size<sizeof(RAWINPUTHEADER)||size>65536)return false;
        std::vector<std::byte> bytes(size);
        if(GetRawInputData(input,RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))==UINT(-1))return false;
        const auto* raw=reinterpret_cast<const RAWINPUT*>(bytes.data());
        if(raw->header.dwType!=RIM_TYPEHID)return false;
        if(bytes.size()<offsetof(RAWINPUT,data.hid.bRawData))return false;
        auto it=entries_.find(raw->header.hDevice);if(it==entries_.end()){scan();it=entries_.find(raw->header.hDevice);}
        if(it==entries_.end())return false;
        const auto& hid=raw->data.hid;
        const auto offset=reinterpret_cast<const std::byte*>(hid.bRawData)-bytes.data();
        if(!hid.dwSizeHid||hid.dwCount>256||offset<0||size_t(offset)>bytes.size()||
            uint64_t(hid.dwSizeHid)*hid.dwCount>bytes.size()-size_t(offset))return false;
        auto& entry=it->second;auto* parsed=reinterpret_cast<PHIDP_PREPARSED_DATA>(entry.preparsed.data());
        for(DWORD packet=0;packet<hid.dwCount;++packet) {
            auto* report=reinterpret_cast<char*>(const_cast<BYTE*>(hid.bRawData+packet*hid.dwSizeHid));
            const auto report_id=static_cast<unsigned char>(report[0]);Axes axes{};unsigned mask=0;
            for(const auto& cap:entry.axes)if(cap.UsagePage==1&&cap.ReportID==report_id&&cap.BitSize>0&&cap.BitSize<=32) {
                const int first=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage,last=cap.IsRange?cap.Range.UsageMax:first;
                for(int usage=0x30;usage<=0x35;++usage)if(usage>=first&&usage<=last) {
                    ULONG raw_value{};
                    if(HidP_GetUsageValue(HidP_Input,1,cap.LinkCollection,USHORT(usage),&raw_value,parsed,report,hid.dwSizeHid)!=HIDP_STATUS_SUCCESS)continue;
                    if(cap.LogicalMin<0&&cap.BitSize<32&&(raw_value&(1u<<(cap.BitSize-1))))raw_value|=~((1u<<cap.BitSize)-1);
                    const double value=cap.LogicalMin<0?double(std::bit_cast<int32_t>(uint32_t(raw_value))):double(raw_value);
                    const double limit=std::max(std::abs(double(cap.LogicalMin)),std::abs(double(cap.LogicalMax)));
                    if(limit<=0)continue;axes[usage-0x30]=std::clamp(value/limit,-1.,1.);mask|=1u<<(usage-0x30);
                }
            }
            if(mask)callbacks_.motion(entry.device.id,camera_axes(axes),camera_mask(mask));
            QSet<int> held;bool button_report=false;
            for(const auto& cap:entry.buttons)if(cap.UsagePage==9&&cap.ReportID==report_id) {
                USAGE usages[128]{};ULONG count=128;
                if(HidP_GetUsages(HidP_Input,9,cap.LinkCollection,usages,&count,parsed,report,hid.dwSizeHid)!=HIDP_STATUS_SUCCESS)continue;
                button_report=true;for(ULONG i=0;i<count;++i)if(usages[i]>0&&usages[i]<=128)held.insert(usages[i]);
            }
            if(button_report) {
                entry.button_reports[report_id]=held;QSet<int> combined;
                for(const auto& buttons:entry.button_reports)combined.unite(buttons);
                callbacks_.buttons(entry.device.id,combined,true);
            }
        }
        return false; // Let Qt/DefWindowProc complete ordinary Raw Input cleanup.
    }
};
#else
class LinuxHid final:public HidBackend {
    struct Entry {int fd{-1};Device device;Axes raw{};QSet<int> held;unsigned changed{};bool dropped{};};
    HidCallbacks callbacks_;std::map<QString,Entry> entries_;
    template<size_t N>static bool bit(const std::array<unsigned long,N>& bits,int index){
        constexpr int width=sizeof(unsigned long)*8;return index>=0&&size_t(index/width)<N&&(bits[index/width]&(1ul<<(index%width)));
    }
public:
    explicit LinuxHid(HidCallbacks callbacks):callbacks_(std::move(callbacks)){}
    ~LinuxHid()override{for(auto& [path,entry]:entries_)::close(entry.fd);}
    void scan()override {
        std::erase_if(entries_,[](const auto& pair){if(pair.second.device.connected)return false;::close(pair.second.fd);return true;});
        QSet<QString> present;QVector<Device> devices;
        for(const auto& name:QDir("/dev/input").entryList({"event*"},QDir::System|QDir::Files)) {
            const auto path="/dev/input/"+name;
            if(!entries_.contains(path)) {
                const int fd=::open(path.toLocal8Bit().constData(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
                input_id id{};std::array<unsigned long,16> relative{},keys{};
                if(ioctl(fd,EVIOCGID,&id)<0||!vendor_supported(id.vendor)||
                    ioctl(fd,EVIOCGBIT(EV_REL,sizeof(relative)),relative.data())<0) {::close(fd);continue;}
                bool six=true;for(int axis=REL_X;axis<=REL_RZ;++axis)six&=bit(relative,axis);
                if(!six){::close(fd);continue;}
                char label[256]{},physical[256]{};ioctl(fd,EVIOCGNAME(sizeof(label)),label);ioctl(fd,EVIOCGPHYS(sizeof(physical)),physical);
                Entry entry;entry.fd=fd;entry.device.id=identity(id.vendor,id.product,QByteArray(physical[0]?physical:path.toLocal8Bit().constData()));
                entry.device.name=label[0]?QString::fromLocal8Bit(label):QStringLiteral("3Dconnexion HID");entry.device.transport="USB HID";
                if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(keys)),keys.data())>=0)
                    for(int key=BTN_0;key<BTN_0+128;++key)if(bit(keys,key))entry.device.buttons.push_back(key-BTN_0+1);
                entries_.emplace(path,std::move(entry));
            }
            present.insert(path);devices.push_back(entries_.at(path).device);
        }
        std::erase_if(entries_,[&](auto& pair){if(present.contains(pair.first))return false;::close(pair.second.fd);return true;});
        callbacks_.devices(std::move(devices));
    }
    void poll()override {
        bool disconnected=false;
        for(auto& [path,entry]:entries_) {
            input_event events[64];const auto bytes=::read(entry.fd,events,sizeof(events));
            if(bytes==0||(bytes<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)){entry.device.connected=false;disconnected=true;continue;}
            for(ssize_t i=0;i<bytes/ssize_t(sizeof(input_event));++i) {
                const auto& event=events[i];
                if(event.type==EV_SYN&&event.code==SYN_DROPPED){entry.dropped=true;entry.raw={};entry.changed=0;callbacks_.motion(entry.device.id,{},0x3f);continue;}
                if(entry.dropped) {
                    if(event.type==EV_SYN&&event.code==SYN_REPORT) {
                        entry.dropped=false;entry.held.clear();std::array<unsigned long,16> keys{};
                        if(ioctl(entry.fd,EVIOCGKEY(sizeof(keys)),keys.data())>=0)
                            for(int button:entry.device.buttons)if(bit(keys,BTN_0+button-1))entry.held.insert(button);
                        callbacks_.buttons(entry.device.id,entry.held,false);
                    }
                    continue;
                }
                if(event.type==EV_REL&&event.code<=REL_RZ){entry.raw[event.code]=std::clamp(event.value/500.,-1.,1.);entry.changed|=1u<<event.code;}
                if(event.type==EV_KEY&&event.value!=2&&event.code>=BTN_0&&event.code<BTN_0+128) {
                    const int button=event.code-BTN_0+1;if(event.value)entry.held.insert(button);else entry.held.remove(button);
                    callbacks_.buttons(entry.device.id,entry.held,true);
                }
                if(event.type==EV_SYN&&event.code==SYN_REPORT&&entry.changed) {
                    callbacks_.motion(entry.device.id,camera_axes(entry.raw),camera_mask(entry.changed));entry.changed=0;
                }
            }
        }
        if(disconnected){QVector<Device> devices;for(const auto& [path,entry]:entries_)devices.push_back(entry.device);callbacks_.devices(std::move(devices));}
    }
};
#endif
}
std::unique_ptr<HidBackend> make_hid_backend(quintptr window,HidCallbacks callbacks) {
#ifdef _WIN32
    return std::make_unique<WindowsHid>(window,std::move(callbacks));
#else
    (void)window;return std::make_unique<LinuxHid>(std::move(callbacks));
#endif
}
} // namespace zima::app::controller
