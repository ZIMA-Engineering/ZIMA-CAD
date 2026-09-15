#pragma once
#include <filesystem>
class QApplication;
namespace zima::app {
class AssemblyWorkspaceWindow;
int verify_command_console(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_work_plane_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_rotation_handle_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_holes_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
}
