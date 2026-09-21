#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/metadata.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
// Display names are metadata; renaming never evaluates geometry or placement.
[[nodiscard]] std::optional<std::string> tree_object_name(const Workspace&,const std::string& document,const std::string& kind,const std::string& object);
[[nodiscard]] bool rename_tree_object(Workspace&,const std::string& document,const std::string& kind,const std::string& object,const std::string& name);
[[nodiscard]] document::UserParameterData user_parameters(const Workspace&,const std::string& document);
[[nodiscard]] document::FileSettingsData file_settings(const Workspace&,const std::string& document);
// Parameter edits preserve calculated bodies and parent snapshots.
[[nodiscard]] bool set_user_parameters(Workspace&,const std::string& document,document::UserParameterData);
struct SettingsChange { bool changed{},calculated{}; };
// A changed calculation precision explicitly calculates affected local geometry.
[[nodiscard]] SettingsChange set_file_settings(Workspace&,const kernel::OcctKernel&,const std::string& document,document::FileSettingsData);
}
