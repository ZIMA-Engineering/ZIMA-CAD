#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/metadata.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
[[nodiscard]] document::UserParameterData user_parameters(const Workspace&,const std::string& document);
[[nodiscard]] document::FileSettingsData file_settings(const Workspace&,const std::string& document);
// Parameter edits preserve calculated bodies and parent snapshots.
[[nodiscard]] bool set_user_parameters(Workspace&,const std::string& document,document::UserParameterData);
struct SettingsChange { bool changed{},calculated{}; };
// A changed calculation precision explicitly calculates affected local geometry.
[[nodiscard]] SettingsChange set_file_settings(Workspace&,const kernel::OcctKernel&,const std::string& document,document::FileSettingsData);
}
