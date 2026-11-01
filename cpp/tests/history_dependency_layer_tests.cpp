#include <zima/workspace/history_policy.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

template<class Function> void require_rejected(Function&& function,
        const char* message) {
    bool rejected{};
    try {function();}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,message);
}

} // namespace

int main() try {
    using namespace zima::workspace;
    const std::vector<std::string> order{
        "base-a","base-b","fillet-a","fillet-b","bridge",
        "fillet-c","fillet-d"};
    const std::set<std::string> treatments{
        "fillet-a","fillet-b","fillet-c","fillet-d"};
    const HistoryDependencies dependencies{
        {"base-a","fillet-a"},
        {"base-b","fillet-b"},
        {"fillet-a","bridge"},
        {"bridge","fillet-c"},
        {"fillet-b","fillet-c"},
        {"fillet-b","fillet-d"}};
    const std::vector<std::vector<std::string>> expected{
        {"fillet-a","fillet-b"},{"fillet-c","fillet-d"}};
    require(selected_history_dependency_layers(
                order,treatments,dependencies)==expected,
        "Treatment layers ignored an indirect dependency or changed history order");

    const std::vector<std::string> reordered{
        "base-a","base-b","fillet-b","fillet-a","bridge",
        "fillet-d","fillet-c"};
    const std::vector<std::vector<std::string>> reordered_expected{
        {"fillet-b","fillet-a"},{"fillet-d","fillet-c"}};
    require(selected_history_dependency_layers(
                reordered,treatments,dependencies)==reordered_expected,
        "Treatment layers are not deterministic in supplied history order");

    auto cyclic=dependencies;
    cyclic.emplace("fillet-c","fillet-a");
    require_rejected([&] {
        static_cast<void>(selected_history_dependency_layers(
            order,treatments,cyclic));
    },"Treatment layers accepted a dependency cycle");
    auto duplicate_order=order;
    duplicate_order.push_back("fillet-a");
    require_rejected([&] {
        static_cast<void>(selected_history_dependency_layers(
            duplicate_order,treatments,dependencies));
    },"Treatment layers accepted a duplicate history object");
    auto absent=treatments;
    absent.insert("missing-fillet");
    require_rejected([&] {
        static_cast<void>(selected_history_dependency_layers(
            order,absent,dependencies));
    },"Treatment layers accepted an object absent from history");

    std::cout<<"History dependency layers: independent, indirect, stable-order and rejection checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
