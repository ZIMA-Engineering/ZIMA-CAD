#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/drawing/model_annotations.hpp>
namespace zima::workspace {
// Read persisted source data. None of these operations regenerates a body.
std::vector<drawing::ModelAnnotationSource> drawing_annotation_sources(const Workspace*,const std::string&,const std::filesystem::path&);
std::vector<document::SectionDefinition> sections_for_part(const document::PartDocument&);
std::vector<document::SectionDefinition> sections_for_assembly(const assembly::AssemblyDocument&);
std::vector<document::SectionDefinition> source_sections(const Workspace*,const std::string&,const std::filesystem::path&);
drawing::TitleBlockContext build_title_block_context_for_source(const std::string&,const std::filesystem::path&,const Workspace*);
std::vector<drawing::BomRow> build_bom_rows_for_source(const std::string&,const std::filesystem::path&,const Workspace*);
}
