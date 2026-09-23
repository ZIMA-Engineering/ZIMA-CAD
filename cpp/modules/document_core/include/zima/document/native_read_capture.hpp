#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace zima::document {
// An explicit, short-lived read transaction can retain the native bytes it
// consumed. Reuse checks bytes, including missing files, never timestamps alone.
class NativeReadCapture {
    std::map<std::filesystem::path,std::optional<std::string>> files_;
    inline static thread_local NativeReadCapture* active_{};
    static std::optional<std::string> read(const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file)return std::nullopt;
        const auto size=file.tellg();if(size<0)return std::nullopt;
        std::string bytes(static_cast<std::size_t>(size),'\0');file.seekg(0);
        if(!file.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))return std::nullopt;
        return bytes;
    }
public:
    class Scope {
        NativeReadCapture* previous_;
    public:
        explicit Scope(NativeReadCapture& capture):previous_(active_){active_=&capture;}
        ~Scope(){active_=previous_;}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
    static void observe(const std::filesystem::path& path) {
        if(!active_||path.empty())return;
        const auto key=std::filesystem::absolute(path).lexically_normal();
        if(!active_->files_.contains(key))active_->files_.emplace(key,read(key));
    }
    bool unchanged() const {
        for(const auto& [path,bytes]:files_)if(read(path)!=bytes)return false;
        return true;
    }
};
}
