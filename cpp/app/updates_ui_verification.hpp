#pragma once
#include <filesystem>
class QApplication;
namespace zima::app {
class AssemblyWorkspaceWindow;
int verify_updates_ui(QApplication&, AssemblyWorkspaceWindow&, const std::filesystem::path&);
}
