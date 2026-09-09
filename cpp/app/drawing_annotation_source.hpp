#pragma once
#include <zima/drawing/model_annotations.hpp>
namespace zima::workspace {
class Workspace;
}
namespace zima::app {
std::vector<drawing::ModelAnnotationSource>
drawing_annotation_sources(workspace::Workspace *, const std::string &,
                           const std::filesystem::path &);
}
