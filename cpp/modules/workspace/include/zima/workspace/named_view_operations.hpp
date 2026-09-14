#pragma once
#include <zima/document/named_views.hpp>
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
[[nodiscard]] std::vector<document::NamedView> named_views(const Workspace& live,const std::string& id);
[[nodiscard]] document::NamedView named_view(const Workspace& live,const std::string& id,const std::string& name);
bool set_named_view(Workspace& live,const std::string& id,document::NamedView view);
bool delete_named_view(Workspace& live,const std::string& id,const std::string& name);
}
