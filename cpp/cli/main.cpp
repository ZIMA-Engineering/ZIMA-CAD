#include "runner.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {
// Keep the protocol channel separate from C/OCCT stdout diagnostics as well as
// std::cout. The saved descriptor is written synchronously after every command.
class Output {
    int descriptor_=-1;
public:
    Output(){
        std::cout.flush();std::fflush(stdout);
#ifdef _WIN32
        descriptor_=_dup(_fileno(stdout));
        if(descriptor_<0||_dup2(_fileno(stderr),_fileno(stdout))!=0)throw std::runtime_error("Cannot reserve stdout for command results");
        _setmode(descriptor_,_O_BINARY);
#else
        descriptor_=dup(STDOUT_FILENO);
        if(descriptor_<0||dup2(STDERR_FILENO,STDOUT_FILENO)<0)throw std::runtime_error("Cannot reserve stdout for command results");
        std::signal(SIGPIPE,SIG_IGN);
#endif
    }
    ~Output(){
#ifdef _WIN32
        if(descriptor_>=0)_close(descriptor_);
#else
        if(descriptor_>=0)close(descriptor_);
#endif
    }
    void write(const std::string& text) const{
        std::size_t offset=0;
        while(offset<text.size()){
            const auto size=std::min<std::size_t>(text.size()-offset,65536);
#ifdef _WIN32
            const auto written=_write(descriptor_,text.data()+offset,static_cast<unsigned>(size));
#else
            const auto written=::write(descriptor_,text.data()+offset,size);
#endif
            if(written<0&&errno==EINTR)continue;
            if(written<=0)throw std::runtime_error("Command result output failed");
            offset+=static_cast<std::size_t>(written);
        }
    }
};
#ifdef _WIN32
std::string utf8(const wchar_t* text){
    const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);
    if(!length)throw std::runtime_error("Invalid Unicode argument");
    std::string result(length,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,result.data(),length,nullptr,nullptr);
    result.pop_back();return result;
}
struct ConsoleEncoding {
    UINT input=GetConsoleCP(),output=GetConsoleOutputCP();
    ConsoleEncoding(){if(input)SetConsoleCP(CP_UTF8);if(output)SetConsoleOutputCP(CP_UTF8);}
    ~ConsoleEncoding(){if(input)SetConsoleCP(input);if(output)SetConsoleOutputCP(output);}
};
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
#else
int main(int argc,char** argv){
#endif
    try{
#ifdef _WIN32
        ConsoleEncoding encoding;
#endif
        Output output;std::vector<std::string> arguments;
        for(int i=1;i<argc;++i){
#ifdef _WIN32
            arguments.push_back(utf8(argv[i]));
#else
            arguments.emplace_back(argv[i]);
#endif
        }
        std::filesystem::path executable=std::filesystem::absolute(argv[0]);
#ifdef _WIN32
        std::wstring module(1024,L'\0');
        for(;;){const auto size=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
            if(!size)break;if(size<module.size()){module.resize(size);executable=module;break;}module.resize(module.size()*2);}
#elif defined(__linux__)
        std::error_code error;const auto module=std::filesystem::read_symlink("/proc/self/exe",error);if(!error)executable=module;
#endif
        return zima::cli::run(arguments,executable,std::cin,[&](const std::string& text){output.write(text);});
    }catch(const std::exception& error){std::cerr<<"zima-cad-cli: "<<error.what()<<'\n';return 2;}
}
