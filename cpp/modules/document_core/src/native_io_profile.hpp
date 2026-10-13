#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace zima::document {
class NativeIoProfile {
    bool enabled_=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    const char* operation_;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now(),previous_=start_;
public:
    explicit NativeIoProfile(const char* operation):operation_(operation){}
    void phase(const char* name){if(!enabled_)return;const auto now=std::chrono::steady_clock::now();std::fprintf(stderr,"Native IO %s %s: %.3f ms\n",operation_,name,std::chrono::duration<double,std::milli>(now-previous_).count());previous_=now;}
    ~NativeIoProfile(){if(enabled_)std::fprintf(stderr,"Native IO %s total: %.3f ms\n",operation_,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count());}
};
} // namespace zima::document
