#pragma once
#include <zima/workspace/workspace.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace zima::workspace {
struct ValueLockInfo { std::string key; bool locked{}, editable{true}; };
class ValueLockError : public std::runtime_error {
public:
    ValueLockError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
// Canonical keys are the existing property-field lock keys, with the placement:
// prefix. Viewer parameter: and Assembly placement-reference: addresses resolve
// to those same stored slots. No geometry or reference solving is performed.
[[nodiscard]] std::pair<std::string,std::string> value_lock_address(std::string object,std::string key);
[[nodiscard]] std::vector<ValueLockInfo> value_locks(const Workspace&,const std::string& document,const std::string& object);
[[nodiscard]] std::optional<bool> value_locked(const Workspace&,const std::string& document,const std::string& object,const std::string& key);
[[nodiscard]] bool set_value_lock(Workspace&,const std::string& document,const std::string& object,const std::string& key,bool locked);
} // namespace zima::workspace
