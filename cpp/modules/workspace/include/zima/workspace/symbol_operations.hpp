#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/symbols/definition.hpp>
namespace zima::workspace {
bool is_symbol_document(const Workspace&,const std::string&);
std::string open_symbol_document(Workspace&,const std::filesystem::path&);
std::string create_symbol_document(Workspace&,const std::string&,const std::filesystem::path&);
symbols::Definition edited_symbol_definition(const Workspace&,const std::string&);
void save_symbol_document(Workspace&,const std::string&,const std::filesystem::path&,bool copy=false);
// Model annotations belong to the addressed document; Drawing annotations
// require an explicit sheet. Each successful edit is one Undo transaction.
const std::vector<symbols::Placement>& symbol_annotations(const Workspace&,const std::string& document,const std::string& sheet={});
bool store_symbol_annotation(Workspace&,const std::string& document,const symbols::Placement&,const std::string& sheet={});
bool remove_symbol_annotation(Workspace&,const std::string& document,const std::string& symbol,const std::string& sheet={});
}
