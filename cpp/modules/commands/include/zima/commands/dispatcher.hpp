#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace zima::commands {
using Json = nlohmann::json;
enum class ArgumentType { String, Number, Integer, Boolean, Object, Array };
struct Argument {
    std::string name;
    bool required{true};
    ArgumentType type{ArgumentType::String};
};
struct Command {
    std::string name;
    std::string description;
    std::vector<Argument> arguments;
    bool changes_state{};
};
struct Result {
    bool ok{};
    std::string code;
    std::string message;
    Json data = Json::object();
    static Result success(Json data = Json::object());
    static Result failure(std::string code, std::string message);
    [[nodiscard]] Json json() const;
};
// Transport- and GUI-independent command vocabulary. Hosts provide operations;
// neither text nor JSON requests are interpreted as shell/Python/C++ code.
class Dispatcher {
public:
    using Handler = std::function<Result(const Json&)>;
    using Guard = std::function<Result(const Command&)>;
    void add(Command command, Handler handler);
    void set_guard(Guard guard);
    [[nodiscard]] Json catalog() const;
    [[nodiscard]] Result execute(const Json& request) const;
    [[nodiscard]] Result execute_text(std::string_view text) const;
private:
    struct Entry { Command command; Handler handler; };
    std::map<std::string, Entry> entries_;
    Guard guard_;
};
} // namespace zima::commands
