#pragma once
#include <filesystem>
class QApplication;
namespace zima::app {
class AssemblyWorkspaceWindow;
int verify_command_console(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
}
