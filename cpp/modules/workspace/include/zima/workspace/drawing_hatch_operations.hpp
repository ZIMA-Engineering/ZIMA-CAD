#pragma once
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
namespace zima::workspace {
using SectionComponents=std::map<std::string,document::SectionComponent>;
// Current source metadata, without projection, body calculation or opening tabs.
document::SectionDefinition drawing_source_section(const Workspace*,const drawing::DrawingView&,const std::filesystem::path& drawing_path);
// Table/command modes: cut_only hides hatching in this view, uncut is source-owned.
SectionComponents drawing_section_components(const drawing::DrawingView&,const document::SectionDefinition&);
void set_drawing_section_components(drawing::DrawingView&,const document::SectionDefinition&,const SectionComponents&,bool replace=false);
// Prepare only. The callback is invoked after all Drawing projections succeed.
// A changed closed source opens at commit, without activation or saving to disk.
std::function<bool()> prepare_section_component_commit(Workspace*,const std::string& source_id,
    const std::filesystem::path&,const document::SectionDefinition&,const document::SectionDefinition* expected=nullptr);
}
