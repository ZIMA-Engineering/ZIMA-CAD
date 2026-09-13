#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/appearance.hpp>
#include <set>
namespace zima::workspace {
class AppearanceError : public std::runtime_error {
public:
    AppearanceError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
struct AppearanceEdit {
    std::string document_id,occurrence_id,body_id;
    std::uint64_t revision{};
    std::shared_ptr<const int> runtime_identity;
    kernel::Appearance initial;
};
[[nodiscard]] kernel::Appearance part_appearance(const document::PartDocument&);
[[nodiscard]] kernel::Appearance occurrence_appearance(const Workspace&,const assembly::PartOccurrence&);
[[nodiscard]] AppearanceEdit prepare_appearance_edit(const Workspace&,const std::string& document,
    const std::string& occurrence = {},const std::optional<std::string>& body = {});
// Keys identify only persisted display faces, scoped by this exact source/occurrence.
[[nodiscard]] std::set<std::pair<std::string,std::string>> appearance_faces(const Workspace&,const AppearanceEdit&);
[[nodiscard]] bool commit_appearance(Workspace&,const AppearanceEdit&,const kernel::Appearance&);
[[nodiscard]] bool inherit_occurrence_appearance(Workspace&,const AppearanceEdit&);
void reset_appearance(kernel::Appearance&,const std::string& body);
}
