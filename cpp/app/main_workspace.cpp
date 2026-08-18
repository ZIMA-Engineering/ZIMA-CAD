#include "assembly_workspace_window.hpp"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    zima::app::AssemblyWorkspaceWindow window;
    window.show();
    return application.exec();
}
