#pragma once
#include <filesystem>
class QApplication;
namespace zima::app {
class AssemblyWorkspaceWindow;
int verify_boundary_surface_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_sheet_cut_skin_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_general_surface_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_surface_intersection_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_surface_trim_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_command_console(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_work_plane_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_rotation_handle_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_holes_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
int verify_edge_treatment_ui(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
}
