#pragma once
#include <filesystem>
#include <functional>
#include <istream>
#include <string>
#include <vector>
namespace zima::cli {
// write must emit all bytes or throw; no later command runs after output failure.
int run(const std::vector<std::string>& arguments,const std::filesystem::path& executable,
    std::istream& input,const std::function<void(const std::string&)>& write);
}
