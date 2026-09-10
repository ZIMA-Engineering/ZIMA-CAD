#pragma once
#include <filesystem>
class QApplication;
namespace zima::app {
class AssemblyWorkspaceWindow;
int verify_measurement_inspector(QApplication&,AssemblyWorkspaceWindow&,const std::filesystem::path&);
}
