#include "controller_input.hpp"
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace zima::app::controller {
namespace {
class SerialPort {
public:
    ~SerialPort(){close();}
    bool open(const QString& port) {
        close();
#ifdef _WIN32
        static const QRegularExpression valid(QStringLiteral("^COM[1-9][0-9]{0,4}$"),QRegularExpression::CaseInsensitiveOption);
        if(!valid.match(port).hasMatch())return false;
        const auto path=("\\\\.\\"+port).toStdWString();
        handle_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)return false;
        DCB settings{};settings.DCBlength=sizeof(settings);
        if(!GetCommState(handle_,&settings)){close();return false;}
        settings.BaudRate=CBR_115200;settings.ByteSize=8;settings.Parity=NOPARITY;settings.StopBits=ONESTOPBIT;
        settings.fBinary=TRUE;settings.fParity=FALSE;settings.fOutxCtsFlow=FALSE;settings.fOutxDsrFlow=FALSE;
        settings.fDtrControl=DTR_CONTROL_ENABLE;settings.fRtsControl=RTS_CONTROL_ENABLE;
        settings.fDsrSensitivity=FALSE;settings.fOutX=FALSE;settings.fInX=FALSE;settings.fAbortOnError=FALSE;
        COMMTIMEOUTS timeouts{};timeouts.ReadIntervalTimeout=MAXDWORD;
        if(!SetCommState(handle_,&settings)||!SetCommTimeouts(handle_,&timeouts)){close();return false;}
#else
        if(!port.startsWith("/dev/")||port.split('/').contains(".."))return false;
        descriptor_=::open(port.toLocal8Bit().constData(),O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
        if(descriptor_<0)return false;
        struct stat info{};termios settings{};
        if(fstat(descriptor_,&info)!=0||!S_ISCHR(info.st_mode)||tcgetattr(descriptor_,&settings)!=0){close();return false;}
        cfmakeraw(&settings);cfsetispeed(&settings,B115200);cfsetospeed(&settings,B115200);
        settings.c_cflag&=~(PARENB|CSTOPB|CSIZE|CRTSCTS);settings.c_cflag|=CS8|CLOCAL|CREAD;
        settings.c_cc[VMIN]=0;settings.c_cc[VTIME]=0;
        if(tcsetattr(descriptor_,TCSANOW,&settings)!=0){close();return false;}
        int lines=TIOCM_DTR|TIOCM_RTS;ioctl(descriptor_,TIOCMBIS,&lines);
#endif
        return true;
    }
    void close() {
#ifdef _WIN32
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;
#else
        if(descriptor_>=0)::close(descriptor_);descriptor_=-1;
#endif
    }
    int read(char* buffer,int capacity) {
#ifdef _WIN32
        DWORD count{};
        if(!ReadFile(handle_,buffer,DWORD(capacity),&count,nullptr))return -1;
        return int(count);
#else
        pollfd readiness{descriptor_,POLLIN,0};
        if(::poll(&readiness,1,0)<0)return errno==EINTR?0:-1;
        if(readiness.revents&(POLLHUP|POLLERR|POLLNVAL))return -1;
        const auto count=::read(descriptor_,buffer,capacity);
        return count<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)?0:int(count);
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};
}
struct SerialReader::Impl {
    std::mutex mutex;std::condition_variable wake;QString requested;SerialSample latest;
    std::jthread thread;
    Impl():thread([this](std::stop_token stop){run(stop);}){}
    ~Impl(){thread.request_stop();wake.notify_all();thread.join();}
    void run(std::stop_token stop) {
        SerialPort port;NaviGoParser parser;QString current;bool connected=false;qint64 retry=0;
        while(!stop.stop_requested()) {
            QString desired;{std::lock_guard lock(mutex);desired=requested;}
            if(current!=desired) {
                port.close();parser.clear();current=desired;connected=false;retry=0;
                std::lock_guard lock(mutex);latest={};latest.port=current;
            }
            const auto now=monotonic_ms();
            if(!current.isEmpty()&&!connected&&now>=retry) {
                connected=port.open(current);retry=now+1000;
                std::lock_guard lock(mutex);latest.connected=connected;
            }
            if(connected) {
                char buffer[4096];const int count=port.read(buffer,sizeof(buffer));
                if(count<0) {
                    port.close();connected=false;parser.clear();retry=now+1000;
                    std::lock_guard lock(mutex);latest={};latest.port=current;
                } else if(count>0) {
                    parser.feed(QByteArray(buffer,count),[&](const Axes& axes){
                        std::lock_guard lock(mutex);
                        latest.axes=axes;latest.axes[2]=0;latest.received=monotonic_ms();
                        latest.zoom_steps+=axes[2]/50.;
                    });
                }
            }
            std::unique_lock lock(mutex);
            wake.wait_for(lock,std::chrono::milliseconds(current.isEmpty()?1000:8),[&]{return stop.stop_requested()||requested!=current;});
        }
    }
};
SerialReader::SerialReader():impl_(std::make_unique<Impl>()){}
SerialReader::~SerialReader()=default;
void SerialReader::select(const QString& port) {
    {std::lock_guard lock(impl_->mutex);impl_->requested=port.trimmed();}impl_->wake.notify_all();
}
SerialSample SerialReader::take() {
    std::lock_guard lock(impl_->mutex);auto result=impl_->latest;impl_->latest.zoom_steps=0;return result;
}
QStringList SerialReader::ports() {
    QStringList result;
#ifdef _WIN32
    QSettings serial(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"),QSettings::NativeFormat);
    for(const auto& key:serial.allKeys())result.push_back(serial.value(key).toString());
#else
    for(const auto& name:QDir("/dev").entryList({"ttyACM*","ttyUSB*"},QDir::System|QDir::Files))result.push_back("/dev/"+name);
    for(const auto& name:QDir("/dev/serial/by-id").entryList(QDir::System|QDir::Files))result.push_back("/dev/serial/by-id/"+name);
#endif
    result.removeDuplicates();std::sort(result.begin(),result.end());return result;
}
} // namespace zima::app::controller
