#pragma once
#include <zima/workspace/drawing_operations.hpp>
namespace zima::workspace {
// Whole-document draft operations: failure leaves the caller unchanged.
std::size_t regenerate_drawing_views(drawing::DrawingDocument&,const Workspace*,const std::filesystem::path& drawing_path);
std::vector<std::string> delete_drawing_view(drawing::DrawingDocument&,const std::string&);
}
